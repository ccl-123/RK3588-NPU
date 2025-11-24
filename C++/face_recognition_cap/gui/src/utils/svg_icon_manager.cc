#include "utils/svg_icon_manager.h"

#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QHash>
#include <QFile>

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

    QFile resource(resource_path);
    if (!resource.exists()) {
        return {};
    }

    QSvgRenderer renderer(resource_path);
    if (!renderer.isValid()) {
        return {};
    }

    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    if (color.isValid()) {
        painter.fillRect(pixmap.rect(), color);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    }

    renderer.render(&painter);

    QIcon icon(pixmap);
    icon_cache.insert(key, icon);
    return icon;
}

