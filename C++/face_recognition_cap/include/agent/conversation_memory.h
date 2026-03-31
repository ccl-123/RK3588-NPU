/**
 * @file conversation_memory.h
 * @brief 对话记忆 - 管理 Agent 对话历史
 */

#pragma once

#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <vector>
#include <mutex>
#include "agent/tool_types.h"

namespace agent {

/**
 * @brief 对话消息
 */
struct Message {
    QString role;             ///< 角色: "user", "assistant", "tool"
    QString kind;             ///< user / assistant / tool_call / tool_result
    QString content;          ///< 消息内容
    QDateTime timestamp;      ///< 时间戳
    QString tool_name;        ///< 工具名称（仅当 role == "tool" 时有效）
    QString tool_call_id;     ///< 工具调用 ID
    QJsonObject metadata;     ///< 附加结构化元信息

    Message() = default;
    Message(const QString& r, const QString& c)
        : role(r), kind(r), content(c), timestamp(QDateTime::currentDateTime()) {}
};

/**
 * @brief 对话记忆 - 管理对话历史和上下文
 *
 * 功能:
 * - 存储用户、助手、工具的对话消息
 * - 限制上下文长度（最近 N 轮）
 * - 支持会话摘要压缩
 * - 线程安全
 */
class ConversationMemory {
public:
    /**
     * @brief 构造函数
     * @param max_turns 最大对话轮数
     */
    explicit ConversationMemory(int max_turns = 10);

    /**
     * @brief 添加用户消息
     * @param content 消息内容
     */
    void addUserMessage(const QString& content);

    /**
     * @brief 添加助手消息
     * @param content 消息内容
     */
    void addAssistantMessage(const QString& content);

    /**
     * @brief 添加工具消息
     * @param tool_name 工具名称
     * @param result 工具执行结果
     */
    void addToolMessage(const QString& tool_name, const QString& result);

    /**
     * @brief 添加工具调用消息
     * @param invocation 工具调用
     */
    void addToolInvocationMessage(const ToolInvocation& invocation);

    /**
     * @brief 添加工具结果消息
     * @param result 工具执行结果
     */
    void addToolResultMessage(const ToolExecutionResult& result);

    /**
     * @brief 获取上下文（格式化的对话历史）
     * @param max_turns 最大轮数（-1 使用默认值）
     * @return 格式化的对话历史字符串
     */
    QString getContext(int max_turns = -1) const;

    /**
     * @brief 获取完整历史
     * @return 所有消息列表
     */
    std::vector<Message> getFullHistory() const;

    /**
     * @brief 获取最后一条消息
     * @return 最后一条消息，如果为空则返回空 Message
     */
    Message getLastMessage() const;

    /**
     * @brief 清空当前会话
     */
    void clearSession();

    /**
     * @brief 设置会话摘要（用于压缩历史）
     * @param summary 摘要内容
     */
    void setSummary(const QString& summary);

    /**
     * @brief 获取会话摘要
     * @return 摘要内容
     */
    QString getSummary() const;

    /**
     * @brief 获取消息数量
     * @return 当前消息数量
     */
    size_t size() const;

    /**
     * @brief 是否为空
     * @return 是否没有任何消息
     */
    bool isEmpty() const;

    /**
     * @brief 设置最大轮数
     * @param max_turns 最大轮数
     */
    void setMaxTurns(int max_turns);

private:
    mutable std::mutex mutex_;
    std::vector<Message> messages_;
    QString session_summary_;
    int max_turns_;

    /**
     * @brief 裁剪消息到最大轮数
     */
    void trimMessages();

    /**
     * @brief 查找最近 N 个用户轮次对应的起始消息下标
     * @note 仅在已持有 mutex_ 时调用
     */
    size_t findStartIndexForRecentTurnsLocked(int turns) const;
};

} // namespace agent
