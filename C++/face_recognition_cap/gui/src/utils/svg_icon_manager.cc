#include "utils/svg_icon_manager.h"

#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QHash>
#include <QFile>
#include <QByteArray>
#include <spdlog/spdlog.h>

namespace {
QString cacheKey(const QString& path, const QSize& size, const QColor& color) {
    return QStringLiteral("%1_%2x%3_%4")
        .arg(path)
        .arg(size.width())
        .arg(size.height())
        .arg(color.isValid() ? color.name(QColor::HexArgb) : QStringLiteral("none"));
}

QHash<QString, QIcon> icon_cache;
}  // namespace

QIcon SvgIconManager::icon(const QString& resource_path,
                           const QSize& size,
                           const QColor& color) {
    const QString key = cacheKey(resource_path, size, color);
    if (icon_cache.contains(key)) {
        return icon_cache.value(key);
    }

    // 读取 SVG 文件内容
    QFile resource(resource_path);
    if (!resource.open(QIODevice::ReadOnly)) {
        spdlog::warn("SvgIconManager: Cannot open file: {}", resource_path.toStdString());
        return {};
    }

    QByteArray svg_data = resource.readAll();
    resource.close();
    
    if (svg_data.isEmpty()) {
        spdlog::warn("SvgIconManager: Empty SVG file: {}", resource_path.toStdString());
        return {};
    }

    // 如果指定了颜色，替换 SVG 中的 currentColor 和默认颜色
    if (color.isValid()) {
        QString color_hex = color.name();  // 如 "#8c8c8c"
        
        // 替换 currentColor（Feather Icons 使用这种方式）
        svg_data.replace("currentColor", color_hex.toUtf8());
        
        // 替换常见的默认黑色（某些图标可能使用）
        svg_data.replace("stroke=\"#000\"", ("stroke=\"" + color_hex + "\"").toUtf8());
        svg_data.replace("stroke=\"#000000\"", ("stroke=\"" + color_hex + "\"").toUtf8());
        svg_data.replace("stroke=\"black\"", ("stroke=\"" + color_hex + "\"").toUtf8());
        svg_data.replace("fill=\"#000\"", ("fill=\"" + color_hex + "\"").toUtf8());
        svg_data.replace("fill=\"#000000\"", ("fill=\"" + color_hex + "\"").toUtf8());
        svg_data.replace("fill=\"black\"", ("fill=\"" + color_hex + "\"").toUtf8());
    }

    // 使用修改后的 SVG 数据创建渲染器
    QSvgRenderer renderer(svg_data);
    if (!renderer.isValid()) {
        spdlog::warn("SvgIconManager: Invalid SVG content: {}", resource_path.toStdString());
        return {};
    }

    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // 渲染 SVG 到 pixmap
    renderer.render(&painter);
    painter.end();

    QIcon icon(pixmap);
    icon_cache.insert(key, icon);
    return icon;
}

