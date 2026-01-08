/**
 * @file tool_registry.h
 * @brief 工具注册表 - 管理所有可用的 Agent 工具
 */

#pragma once

#include <memory>
#include <vector>
#include <map>
#include <QString>
#include <QJsonArray>
#include <QJsonDocument>
#include "tools/base_tool.h"

namespace agent {

/**
 * @brief 工具注册表 - 集中管理所有 Agent 工具
 *
 * 使用方式:
 *   ToolRegistry registry;
 *   registry.registerTool(std::make_unique<AttendanceTool>(svc));
 *   QString tools_json = registry.getToolsJson();
 */
class ToolRegistry {
public:
    ToolRegistry() = default;
    ~ToolRegistry() = default;

    // 禁止拷贝
    ToolRegistry(const ToolRegistry&) = delete;
    ToolRegistry& operator=(const ToolRegistry&) = delete;

    /**
     * @brief 注册工具
     * @param tool 工具实例（所有权转移）
     */
    void registerTool(std::unique_ptr<BaseTool> tool);

    /**
     * @brief 获取工具
     * @param name 工具名称
     * @return 工具指针，未找到返回 nullptr
     */
    BaseTool* getTool(const QString& name) const;

    /**
     * @brief 检查工具是否存在
     * @param name 工具名称
     * @return 是否存在
     */
    bool hasTool(const QString& name) const;

    /**
     * @brief 获取所有工具定义的 JSON 字符串
     * @return 用于 rkllm_set_function_tools() 的 JSON
     */
    QString getToolsJson() const;

    /**
     * @brief 获取所有工具名称
     * @return 工具名称列表
     */
    std::vector<QString> getToolNames() const;

    /**
     * @brief 获取工具数量
     * @return 已注册的工具数量
     */
    size_t size() const { return tools_.size(); }

    /**
     * @brief 清空所有工具
     */
    void clear() { tools_.clear(); }

private:
    std::map<QString, std::unique_ptr<BaseTool>> tools_;
};

} // namespace agent
