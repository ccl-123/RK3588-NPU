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

// 静态实例
static LocalLLMThread* s_instance = nullptr;

// 考勤专家系统提示词（与 face_llm.cpp 保持一致）
const char* LocalLLMThread::SYSTEM_PROMPT = R"(# 任务目标（优先回答用户自身问题！）
基于系统提供的结构化考勤数据生成准确、简洁、可解释、可落地的分析结论，用于指导管理决策。

# 任务流程
1. **接收用户问题或关注点**：
   - 优先回答用户自身问题（如：今天迟到情况、异常人员、周趋势等）。
   - 用户未说明问题默认按【今日】分析；未明确关注点输出【综合分析】，按价值排序。

2. **分析数据**：
   - 若未明确时间维度，分析今日数据并附带"趋势概览"。
   - 综合分析时先说明最重要、最有风险的问题，其余简要带过。

3. **生成结论与建议**：
   - 只输出纯文本。禁止输出任何表格形式（包括 Markdown 表格、伪表格、JSON、代码块）。
   - 不得原样复述系统输入数据。
   - 不粘贴明细，不逐条列举记录，不输出完整名单或完整时间序列。
   - 只能做归纳总结。
   - 不编造数据，缺少字段必须明确说明"未提供该数据"。
   - 直接给结论与建议，不要过程性解释。

4. **解释与判断**：
   - 所有判断必须可解释。优先解释变化原因、潜在风险。
   - 对比规则：仅在系统明确提供对比数据时对比，否则说明无法对比。
   - 原因判断必须区分：已知事实、可能原因（需核实）、管理层可采取的动作。

# 限制条件
- 只输出纯文本。禁止输出JSON。
- 禁止输出任何表格形式。
- 不粘贴明细，不逐条列举记录。
- 只能做归纳总结。
- 直接给结论与建议，不要过程性解释。)";

LocalLLMThread* LocalLLMThread::instance() {
    if (!s_instance) {
        s_instance = new LocalLLMThread(qApp);
    }
    return s_instance;
}

LocalLLMThread::LocalLLMThread(QObject* parent)
    : QThread(parent)
    , llm_handle_(nullptr)
    , model_ready_(false)
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
    condition_.wakeAll();
}

bool LocalLLMThread::initModel(const QString& model_path, int max_new_tokens, int max_context_len) {
    if (model_ready_) {
        spdlog::warn("Model already initialized");
        emit modelReady();
        return true;
    }

    QMutexLocker locker(&mutex_);
    model_path_ = model_path;
    max_new_tokens_ = max_new_tokens;
    max_context_len_ = max_context_len;
    
    pending_request_ = RequestType::Init;
    condition_.wakeOne();
    
    return true;
}

void LocalLLMThread::requestInference(const QString& prompt) {
    if (!model_ready_) {
        emit errorOccurred("模型未初始化");
        return;
    }

    if (inferring_) {
        emit errorOccurred("模型正在忙碌");
        return;
    }

    QMutexLocker locker(&mutex_);
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
}

void LocalLLMThread::releaseModelAsync() {
    if (!model_ready_) {
        spdlog::info("LLM model already released");
        emit modelReleased();
        return;
    }

    if (inferring_) {
        spdlog::warn("Cannot release model while inferring, aborting first...");
        abortInference();
    }

    QMutexLocker locker(&mutex_);
    pending_request_ = RequestType::Destroy;
    condition_.wakeOne();
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
        spdlog::error("rkllm_init failed with code: {}", ret);
        emit modelFailed(QString("初始化失败: 错误码 %1").arg(ret));
        return;
    }

    // 设置考勤专家系统提示词和聊天模板（与 face_llm.cpp 保持一致）
    if (SYSTEM_PROMPT) {
        rkllm_set_chat_template(llm_handle_, (char*)SYSTEM_PROMPT, "<|im_start|>user\n", "<|im_start|>assistant\n");
        spdlog::info("Chat template set with system prompt");
    }

    spdlog::info("rkllm_init success");
    model_ready_ = true;
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
        emit errorOccurred(QString("推理启动失败: 错误码 %1").arg(ret));
    }

    inferring_ = false;
    emit inferenceFinished();
    spdlog::info("Inference completed");
}

// 静态回调函数
int LocalLLMThread::llmCallback(RKLLMResult* result, void* userdata, LLMCallState state) {
    auto self = static_cast<LocalLLMThread*>(userdata);
    
    spdlog::debug("LLM callback: state={}", static_cast<int>(state));

    if (state == RKLLM_RUN_FINISH) {
        spdlog::info("LLM inference finished");
        // 推理结束，打印换行
        std::cout << std::endl;
    } else if (state == RKLLM_RUN_ERROR) {
        spdlog::error("LLM runtime error");
        self->emitError("RKLLM 运行时错误");
    } else if (state == RKLLM_RUN_NORMAL) {
        // 流式输出
        if (result && result->text) {
            QString chunk = QString::fromUtf8(result->text);
            spdlog::debug("LLM chunk: {}", chunk.toStdString());
            
            // 同时输出到控制台（流式效果，不换行）
            std::cout << result->text << std::flush;
            
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
