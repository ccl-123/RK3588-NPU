/**
 * @file tool_registry.cc
 * @brief 工具注册表实现
 */

#include "agent/tool_registry.h"
#include <spdlog/spdlog.h>

namespace agent {

void ToolRegistry::registerTool(std::unique_ptr<BaseTool> tool) {
    if (!tool) {
        spdlog::warn("Attempted to register null tool");
        return;
    }

    QString name = tool->name();
    if (tools_.find(name) != tools_.end()) {
        spdlog::warn("Tool '{}' already registered, replacing", name.toStdString());
    }

    spdlog::info("Registering tool: {}", name.toStdString());
    tools_[name] = std::move(tool);
}

BaseTool* ToolRegistry::getTool(const QString& name) const {
    auto it = tools_.find(name);
    if (it != tools_.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool ToolRegistry::hasTool(const QString& name) const {
    return tools_.find(name) != tools_.end();
}

QString ToolRegistry::getToolsJson() const {
    QJsonArray tools_array;
    for (const auto& pair : tools_) {
        tools_array.append(pair.second->toToolDefinition());
    }
    return QString::fromUtf8(
        QJsonDocument(tools_array).toJson(QJsonDocument::Compact)
    );
}

std::vector<QString> ToolRegistry::getToolNames() const {
    std::vector<QString> names;
    names.reserve(tools_.size());
    for (const auto& pair : tools_) {
        names.push_back(pair.first);
    }
    return names;
}

} // namespace agent
