/**
 * @file local_llm_thread.h
 * @brief 本地 LLM 推理线程 - 直接调用 RKLLM API
 * @author CL
 * @date 2025-12-25
 */

#ifndef LOCAL_LLM_THREAD_H
#define LOCAL_LLM_THREAD_H

#include <QThread>
#include <QString>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include <string>
// RKLLM API
#include "rkllm.h"
#include "config/config.h"

/**
 * @brief 本地 LLM 推理线程
 * 
 * 在独立线程中运行 RKLLM 模型，避免阻塞 UI 线程。
 * 通过 Qt 信号发射流式结果到主线程。
 * 
 * 使用方法：
 *   1. LocalLLMThread::instance()->initModel(model_path)
 *   2. connect(thread, &LocalLLMThread::chunkReady, ...)
 *   3. thread->requestInference(prompt)
 */
class LocalLLMThread : public QThread {
    Q_OBJECT

public:
    static LocalLLMThread* instance();

    ~LocalLLMThread();

    /**
     * @brief 初始化模型
     * @param model_path 模型文件路径 (.rkllm)
     * @param max_new_tokens 单次最大生成 token 数
     * @param max_context_len 最大上下文长度
     * @return 是否成功开始初始化（异步，通过信号通知结果）
     */
    bool initModel(const QString& model_path,
                   int max_new_tokens = Config::LocalLLM::MAX_NEW_TOKENS,
                   int max_context_len = Config::LocalLLM::MAX_CONTEXT_LEN);

    // 检查模型是否已初始化
    bool isModelReady() const { return model_ready_.load(); }
    bool isInitInProgress() const { return init_in_progress_.load(); }

    // 检查是否正在推理
    bool isInferring() const { return inferring_.load(); }

    /**
     * @brief 请求推理（异步）
     * @param prompt 用户输入的提示词
     */
    void requestInference(const QString& prompt);

    // 中止当前推理
    void abortInference();

    // 重置对话上下文（清除 KV cache）
    void resetContext();

    // 销毁模型并释放资源
    void destroyModel();

    /**
     * @brief 异步释放模型资源（不停止线程）
     * @note 用于释放 NPU 资源给人脸识别使用
     *       之后可以调用 initModel() 重新加载
     */
    void releaseModelAsync();

    // 停止线程
    void stop();

signals:
    // 模型初始化完成
    void modelReady();
    // 模型初始化失败
    void modelFailed(const QString& error);
    // 推理开始
    void inferenceStarted();
    // 收到增量内容（流式输出）
    void chunkReady(const QString& chunk);
    // 推理完成
    void inferenceFinished();
    // 发生错误
    void errorOccurred(const QString& error);
    // 模型资源已释放
    void modelReleased();

protected:
    void run() override;

private:
    explicit LocalLLMThread(QObject* parent = nullptr);

    // RKLLM 回调函数（静态，因为 C API 需要函数指针）
    static int llmCallback(RKLLMResult* result, void* userdata, LLMCallState state);

    // 辅助函数：从回调线程发射信号
    void emitChunk(const QString& text);
    void emitFinished();
    void emitError(const QString& error);

    // 内部处理函数
    void doInitModel();
    void doInference();

    LLMHandle llm_handle_;
    std::atomic<bool> model_ready_;
    std::atomic<bool> init_in_progress_;
    std::atomic<bool> inferring_;
    std::atomic<bool> abort_requested_;
    std::atomic<bool> stop_requested_;
    std::atomic<bool> owns_npu_resource_;
    
    // 待处理的请求
    enum class RequestType { None, Init, Infer, Destroy };
    RequestType pending_request_;
    QString pending_prompt_;
    QMutex mutex_;
    QWaitCondition condition_;
    
    // 模型参数
    QString model_path_;
    int max_new_tokens_;
    int max_context_len_;
    
};

#endif // LOCAL_LLM_THREAD_H
