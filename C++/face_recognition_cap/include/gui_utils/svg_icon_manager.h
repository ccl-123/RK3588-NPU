#pragma once

#include <QColor>
#include <QIcon>
#include <QSize>
#include <QString>

/**
 * @brief SvgIconManager 用于加载并缓存 SVG 图标，支持统一着色。
 */
class SvgIconManager {
public:
    /**
     * @brief 加载指定路径的 SVG 图标。
     * @param resource_path Qt 资源路径或绝对路径
     * @param size 输出尺寸
     * @param color 若提供则使用蒙版着色
     */
    static QIcon icon(const QString& resource_path,
                      const QSize& size = QSize(20, 20),
                      const QColor& color = QColor());
};

