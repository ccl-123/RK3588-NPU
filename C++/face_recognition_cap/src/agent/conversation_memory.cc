/**
 * @file conversation_memory.cc
 * @brief 对话记忆实现
 */

#include "agent/conversation_memory.h"
#include <QJsonDocument>
#include <spdlog/spdlog.h>

namespace agent {

ConversationMemory::ConversationMemory(int max_turns)
    : max_turns_(max_turns) {}

void ConversationMemory::addUserMessage(const QString& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    Message msg("user", content);
    messages_.push_back(msg);
    trimMessages();
    spdlog::debug("Added user message, total: {}", messages_.size());
}

void ConversationMemory::addAssistantMessage(const QString& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    Message msg("assistant", content);
    messages_.push_back(msg);
    trimMessages();
    spdlog::debug("Added assistant message, total: {}", messages_.size());
}

void ConversationMemory::addToolMessage(const QString& tool_name, const QString& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    Message msg("tool", result);
    msg.kind = "tool_result";
    msg.tool_name = tool_name;
    messages_.push_back(msg);
    trimMessages();
    spdlog::debug("Added tool message from '{}', total: {}",
        tool_name.toStdString(), messages_.size());
}

void ConversationMemory::addToolInvocationMessage(const ToolInvocation& invocation) {
    std::lock_guard<std::mutex> lock(mutex_);
    Message msg("tool", invocation.toString());
    msg.kind = "tool_call";
    msg.tool_name = invocation.name;
    msg.tool_call_id = invocation.call_id;
    msg.metadata["arguments"] = invocation.arguments;
    messages_.push_back(msg);
    trimMessages();
}

void ConversationMemory::addToolResultMessage(const ToolExecutionResult& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    Message msg("tool", result.promptText());
    msg.kind = "tool_result";
    msg.tool_name = result.name;
    msg.tool_call_id = result.call_id;
    msg.metadata["ok"] = result.ok;
    msg.metadata["output"] = result.output;
    if (!result.error.isEmpty()) {
        msg.metadata["error"] = result.error;
    }
    messages_.push_back(msg);
    trimMessages();
}

QString ConversationMemory::getContext(int max_turns) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (messages_.empty()) {
        return session_summary_.isEmpty() ? QString() : session_summary_;
    }

    int turns = (max_turns > 0) ? max_turns : max_turns_;
    QString context;

    // 添加摘要（如果有）
    if (!session_summary_.isEmpty()) {
        context = QString("[历史摘要]\n%1\n\n[最近对话]\n").arg(session_summary_);
    }

    // 计算起始位置
    int start = 0;
    if (static_cast<int>(messages_.size()) > turns * 2) {
        start = messages_.size() - turns * 2;
    }

    // 格式化消息
    for (size_t i = start; i < messages_.size(); ++i) {
        const auto& msg = messages_[i];

        if (msg.role == "user") {
            context += QString("用户: %1\n").arg(msg.content);
        } else if (msg.role == "assistant") {
            context += QString("助手: %1\n").arg(msg.content);
        } else if (msg.role == "tool") {
            if (msg.kind == "tool_call") {
                context += QString("[工具调用 %1 id=%2]: %3\n")
                    .arg(msg.tool_name,
                         msg.tool_call_id.isEmpty() ? "-" : msg.tool_call_id,
                         QString::fromUtf8(QJsonDocument(msg.metadata.value("arguments").toObject()).toJson(QJsonDocument::Compact)));
            } else {
                context += QString("[工具结果 %1 id=%2]: %3\n")
                    .arg(msg.tool_name,
                         msg.tool_call_id.isEmpty() ? "-" : msg.tool_call_id,
                         msg.content);
            }
        }
    }

    return context;
}

std::vector<Message> ConversationMemory::getFullHistory() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_;
}

Message ConversationMemory::getLastMessage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (messages_.empty()) {
        return Message();
    }
    return messages_.back();
}

void ConversationMemory::clearSession() {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.clear();
    session_summary_.clear();
    spdlog::info("Conversation memory cleared");
}

void ConversationMemory::setSummary(const QString& summary) {
    std::lock_guard<std::mutex> lock(mutex_);
    session_summary_ = summary;
    spdlog::debug("Session summary set: {}...",
        summary.left(50).toStdString());
}

QString ConversationMemory::getSummary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return session_summary_;
}

size_t ConversationMemory::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_.size();
}

bool ConversationMemory::isEmpty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_.empty();
}

void ConversationMemory::setMaxTurns(int max_turns) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_turns_ = max_turns;
    trimMessages();
}

void ConversationMemory::trimMessages() {
    // 保留最近 max_turns * 2 条消息（用户 + 助手各一条算一轮）
    int max_messages = max_turns_ * 2 + 5;  // 额外保留一些工具消息
    if (static_cast<int>(messages_.size()) > max_messages) {
        int to_remove = messages_.size() - max_messages;
        messages_.erase(messages_.begin(), messages_.begin() + to_remove);
        spdlog::debug("Trimmed {} old messages", to_remove);
    }
}

} // namespace agent
