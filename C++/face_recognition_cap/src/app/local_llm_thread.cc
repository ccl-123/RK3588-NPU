/**
 * @file local_llm_thread.cc
 * @brief 本地 LLM 推理线程实现
 * @author CL
 * @date 2025-12-25
 */

#include "app/local_llm_thread.h"
#include "config/config.h"
#include <spdlog/spdlog.h>
#include <QCoreApplication>
#include <iostream>
#include <mutex>

// 静态实例和线程安全初始化
static LocalLLMThread* s_instance = nullptr;
static std::once_flag s_instance_flag;

// 考勤助手系统提示词
const char* LocalLLMThread::SYSTEM_PROMPT = "你是考勤助手，负责分析考勤数据并回答问题。简洁回答，直接给出结论。用户不管问什么都必须回答。";

LocalLLMThread* LocalLLMThread::instance() {
    // 使用 std::call_once 保证线程安全的单例初始化
    std::call_once(s_instance_flag, []() {
        s_instance = new LocalLLMThread(qApp);
    });
    return s_instance;
}

LocalLLMThread::LocalLLMThread(QObject* parent)
    : QThread(parent)
    , llm_handle_(nullptr)
    , model_ready_(false)
    , init_in_progress_(false)
    , inferring_(false)
    , abort_requested_(false)
    , stop_requested_(false)
    , pending_request_(RequestType::None)
    , max_new_tokens_(Config::LocalLLM::MAX_NEW_TOKENS)
    , max_context_len_(Config::LocalLLM::MAX_CONTEXT_LEN) {
    
    // 启动线程循环
    start();
}

LocalLLMThread::~LocalLLMThread() {
    stop();
    wait();
}

void LocalLLMThread::stop() {
    stop_requested_ = true;
    abort_requested_ = true;
    if (llm_handle_ && inferring_) {
        rkllm_abort(llm_handle_);
    }
    condition_.wakeAll();
}

bool LocalLLMThread::initModel(const QString& model_path, int max_new_tokens, int max_context_len) {
    if (model_ready_) {
        spdlog::warn("Model already initialized");
        emit modelReady();
        return true;
    }

    QMutexLocker locker(&mutex_);
    if (init_in_progress_) {
        spdlog::info("Model initialization already in progress");
        return true;
    }

    model_path_ = model_path;
    max_new_tokens_ = max_new_tokens;
    max_context_len_ = max_context_len;
    init_in_progress_ = true;
    pending_request_ = RequestType::Init;
    condition_.wakeOne();
    
    return true;
}

void LocalLLMThread::requestInference(const QString& prompt) {
    if (!model_ready_) {
        if (init_in_progress_) {
            emit errorOccurred("模型正在初始化，请稍候");
            return;
        }
        emit errorOccurred("模型未初始化");
        return;
    }

    QMutexLocker locker(&mutex_);
    if (inferring_) {
        spdlog::info("Model busy, queueing new inference request");
        pending_prompt_ = prompt;
        pending_request_ = RequestType::Infer;
        abort_requested_ = true;
        if (llm_handle_) {
            rkllm_abort(llm_handle_);
        }
        return;
    }

    pending_prompt_ = prompt;
    pending_request_ = RequestType::Infer;
    condition_.wakeOne();
}

void LocalLLMThread::abortInference() {
    if (inferring_) {
        abort_requested_ = true;
        if (llm_handle_) {
            rkllm_abort(llm_handle_);
        }
    }
}

void LocalLLMThread::resetContext() {
    if (llm_handle_) {
        // 清除 KV cache (重置对话上下文)
        // keep_system_prompt=1, start=nullptr, end=nullptr => clear all except system prompt (if any)
        rkllm_clear_kv_cache(llm_handle_, 1, nullptr, nullptr);
        spdlog::info("Context reset");
    }
}

void LocalLLMThread::destroyModel() {
    if (llm_handle_) {
        rkllm_destroy(llm_handle_);
        llm_handle_ = nullptr;
    }
    model_ready_ = false;
    init_in_progress_ = false;
}

void LocalLLMThread::releaseModelAsync() {
    bool emit_released_immediately = false;
    {
        QMutexLocker locker(&mutex_);
        if (!model_ready_ && !init_in_progress_ && pending_request_ == RequestType::None) {
            emit_released_immediately = true;
        } else if (pending_request_ == RequestType::Init && llm_handle_ == nullptr) {
            spdlog::info("Cancelling queued LLM initialization before release");
            pending_request_ = RequestType::Destroy;
            init_in_progress_ = false;
            condition_.wakeOne();
            spdlog::info("LLM model release requested");
            return;
        } else {
            pending_request_ = RequestType::Destroy;
            condition_.wakeOne();
        }
    }

    if (emit_released_immediately) {
        spdlog::info("LLM model already released");
        emit modelReleased();
        return;
    }

    if (inferring_) {
        spdlog::warn("Cannot release model while inferring, aborting first...");
        abortInference();
    }
    spdlog::info("LLM model release requested");
}

// 线程主循环
void LocalLLMThread::run() {
    spdlog::info("LocalLLMThread started");

    while (!stop_requested_) {
        RequestType req = RequestType::None;
        {
            QMutexLocker locker(&mutex_);
            while (pending_request_ == RequestType::None && !stop_requested_) {
                condition_.wait(&mutex_);
            }
            if (stop_requested_) break;
            
            req = pending_request_;
            pending_request_ = RequestType::None;
        }

        if (req == RequestType::Init) {
            doInitModel();
        } else if (req == RequestType::Infer) {
            doInference();
        } else if (req == RequestType::Destroy) {
            // 释放 RKLLM 模型资源，为人脸识别腾出 NPU
            spdlog::info("Destroying RKLLM model to free NPU resources...");
            destroyModel();
            spdlog::info("RKLLM model destroyed, NPU resources freed");
            emit modelReleased();
        }
    }

    destroyModel();
    spdlog::info("LocalLLMThread finished");
}

void LocalLLMThread::doInitModel() {
    spdlog::info("Initializing RKLLM model: {}", model_path_.toStdString());

    RKLLMParam param = rkllm_createDefaultParam();
    std::string path_std = model_path_.toStdString();
    param.model_path = path_std.c_str();
    param.max_context_len = max_context_len_;
    param.max_new_tokens = max_new_tokens_;
    param.top_k = 1;
    param.top_p = 0.95;
    param.temperature = 0.7;
    param.skip_special_token = true;
    param.extend_param.base_domain_id = 0;
    param.extend_param.embed_flash = 1;

    int ret = rkllm_init(&llm_handle_, &param, llmCallback);
    if (ret != 0) {
        init_in_progress_ = false;
        spdlog::error("rkllm_init failed with code: {}", ret);
        emit modelFailed(QString("初始化失败: 错误码 %1").arg(ret));
        return;
    }

    // 设置考勤专家系统提示词和聊天模板（与 face_llm.cpp 保持一致）
    if (SYSTEM_PROMPT) {
        rkllm_set_chat_template(llm_handle_, (char*)SYSTEM_PROMPT, "<|im_start|>user\n", "<|im_start|>assistant\n<think>\n</think>\n");
        spdlog::info("Chat template set with system prompt");
    }

    spdlog::info("rkllm_init success");
    model_ready_ = true;
    init_in_progress_ = false;
    emit modelReady();
}

void LocalLLMThread::doInference() {
    inferring_ = true;
    abort_requested_ = false;
    emit inferenceStarted();

    QString current_prompt;
    {
        QMutexLocker locker(&mutex_);
        current_prompt = pending_prompt_;
    }

    spdlog::info("Starting inference, prompt length: {}", current_prompt.length());

    RKLLMInput input;
    memset(&input, 0, sizeof(RKLLMInput));
    input.input_type = RKLLM_INPUT_PROMPT;
    input.role = "user";
    
    std::string prompt_std = current_prompt.toStdString();
    input.prompt_input = prompt_std.c_str();

    RKLLMInferParam infer_param;
    memset(&infer_param, 0, sizeof(RKLLMInferParam));
    infer_param.mode = RKLLM_INFER_GENERATE;
    infer_param.keep_history = 1; // 保持多轮对话

    // 同步运行 run (但在独立线程中，所以不阻塞 UI)
    // 结果会通过 callback 回调
    spdlog::info("Calling rkllm_run...");
    int ret = rkllm_run(llm_handle_, &input, &infer_param, this);
    spdlog::info("rkllm_run returned: {}", ret);

    if (ret != 0) {
        spdlog::error("rkllm_run failed: {}", ret);
        // 估算 token 数（中文约1.5字符/token）
        int estimated_tokens = static_cast<int>(prompt_std.length() / 1.5);
        QString error_msg = QString("推理启动失败: 错误码 %1 (prompt约%2 tokens, 上下文限制%3)")
            .arg(ret)
            .arg(estimated_tokens)
            .arg(max_context_len_);
        emit errorOccurred(error_msg);
    }

    inferring_ = false;
    emit inferenceFinished();
    spdlog::info("Inference completed");
}

// 静态回调函数
int LocalLLMThread::llmCallback(RKLLMResult* result, void* userdata, LLMCallState state) {
    auto self = static_cast<LocalLLMThread*>(userdata);
    
    if (state == RKLLM_RUN_FINISH) {
        // 推理结束，刷新控制台缓冲（doInference 已有 "Inference completed" 日志）
        std::cout << std::endl;
    } else if (state == RKLLM_RUN_ERROR) {
        spdlog::error("LLM runtime error");
        self->emitError("RKLLM 运行时错误");
    } else if (state == RKLLM_RUN_NORMAL) {
        // 流式输出
        if (result && result->text) {
            QString chunk = QString::fromUtf8(result->text);
            
            // 输出到控制台（去掉每 token flush，减少 syscall 开销）
            std::cout << result->text;
            
            self->emitChunk(chunk);
        }
    } else if (state == RKLLM_RUN_WAITING) {
        spdlog::debug("LLM waiting for UTF-8");
    }
    
    // 检查是否需要中止
    if (self->abort_requested_) {
        return 1; // 中止推理
    }

    return 0;
}

void LocalLLMThread::emitChunk(const QString& text) {
    if (!text.isEmpty()) {
        emit chunkReady(text);
    }
}

void LocalLLMThread::emitFinished() {
    emit inferenceFinished();
}

void LocalLLMThread::emitError(const QString& error) {
    emit errorOccurred(error);
}
