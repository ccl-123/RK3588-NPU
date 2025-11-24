#include "themes/theme_manager.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QPointer>
#include <QTextStream>

namespace {
QString themeResourcePath(ThemeManager::Theme theme) {
    switch (theme) {
    case ThemeManager::Theme::Dark:
        return QStringLiteral(":/ui/themes/theme_dark.qss");
    case ThemeManager::Theme::Light:
    default:
        return QStringLiteral(":/ui/themes/theme.qss");
    }
}
}  // namespace

void ThemeManager::apply(ThemeManager::Theme theme) {
    QString style = loadStyleSheet(theme);
    if (style.isEmpty()) {
        return;
    }

    if (qApp) {
        qApp->setStyleSheet(style);
    }
}

QString ThemeManager::loadStyleSheet(ThemeManager::Theme theme) {
    const QString resource_path = themeResourcePath(theme);
    QFile file(resource_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("Failed to open theme file: %s", qPrintable(resource_path));
        return {};
    }

    QTextStream in(&file);
    return in.readAll();
}

