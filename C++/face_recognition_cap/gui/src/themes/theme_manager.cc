/**
 * @file theme_manager.cc
 * @brief 现代化主题管理器实现
 * @author CL
 * @date 2025-11-25
 */

#include "themes/theme_manager.h"
#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QSettings>
#include <spdlog/spdlog.h>

ThemeManager* ThemeManager::instance_ = nullptr;

ThemeManager* ThemeManager::instance() {
    if (!instance_) {
        instance_ = new ThemeManager(qApp);
    }
    return instance_;
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
    , currentTheme_(ThemeType::Light)
{
    initializeColorMaps();
}

bool ThemeManager::initialize() {
    spdlog::info("Initializing ThemeManager v2.0");
    
    // 加载用户保存的主题设置
    loadThemeSettings();
    
    // 应用主题
    applyTheme(currentTheme_);
    
    spdlog::info("ThemeManager initialized, current theme: {}",
                 currentTheme_ == ThemeType::Light ? "Light" : "Dark");
    
    return true;
}

void ThemeManager::applyTheme(ThemeType type) {
    QString qssPath;
    
    switch (type) {
        case ThemeType::Light:
            qssPath = ":/themes/modern_theme.qss";
            break;
        case ThemeType::Dark:
            qssPath = ":/themes/modern_theme_dark.qss";
            break;
        case ThemeType::Auto:
            // TODO: 检测系统主题
            qssPath = ":/themes/modern_theme.qss";
            break;
    }
    
    QString qssContent = loadQssFile(qssPath);
    if (qssContent.isEmpty()) {
        // 尝试旧路径
        qssPath = (type == ThemeType::Dark) ? 
            ":/ui/themes/theme_dark.qss" : ":/ui/themes/theme.qss";
        qssContent = loadQssFile(qssPath);
        
        if (qssContent.isEmpty()) {
            spdlog::warn("Failed to load QSS file, using default style");
        return;
    }
    }
    
    // 应用 QSS
    qApp->setStyleSheet(qssContent);
    
    // 更新当前主题
    ThemeType oldTheme = currentTheme_;
    currentTheme_ = type;
    
    // 发送主题变化信号
    if (oldTheme != currentTheme_) {
        emit themeChanged(currentTheme_);
        spdlog::info("Theme changed to: {}",
                     currentTheme_ == ThemeType::Light ? "Light" : "Dark");
    }
}

void ThemeManager::toggleTheme() {
    ThemeType newTheme = (currentTheme_ == ThemeType::Light) ? 
        ThemeType::Dark : ThemeType::Light;
    
    applyTheme(newTheme);
    saveThemeSettings();
}

QColor ThemeManager::getColor(const QString& colorName) const {
    const QMap<QString, QColor>& colorMap = 
        (currentTheme_ == ThemeType::Light) ? lightColors_ : darkColors_;
    
    return colorMap.value(colorName, QColor(Qt::black));
}

void ThemeManager::reloadTheme() {
    applyTheme(currentTheme_);
}

void ThemeManager::saveThemeSettings() {
    QSettings settings("FaceRecognitionSystem", "Theme");
    settings.setValue("currentTheme", static_cast<int>(currentTheme_));
    
    spdlog::debug("Theme settings saved");
}

void ThemeManager::loadThemeSettings() {
    QSettings settings("FaceRecognitionSystem", "Theme");
    int themeValue = settings.value("currentTheme", 
                                     static_cast<int>(ThemeType::Light)).toInt();
    
    currentTheme_ = static_cast<ThemeType>(themeValue);
    
    spdlog::debug("Theme settings loaded: {}",
                  currentTheme_ == ThemeType::Light ? "Light" : "Dark");
}

QString ThemeManager::loadQssFile(const QString& filePath) {
    QFile file(filePath);
    
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        spdlog::debug("Cannot open QSS file: {}", filePath.toStdString());
        return QString();
    }

    QTextStream in(&file);
    QString qssContent = in.readAll();
    file.close();
    
    spdlog::debug("Loaded QSS file: {} ({} bytes)",
                  filePath.toStdString(), qssContent.size());
    
    return qssContent;
}

void ThemeManager::initializeColorMaps() {
    // 浅色主题颜色
    lightColors_ = {
        {"primary", QColor("#1890ff")},
        {"primary-hover", QColor("#40a9ff")},
        {"primary-pressed", QColor("#096dd9")},
        {"primary-light", QColor("#e6f7ff")},
        
        {"success", QColor("#52c41a")},
        {"success-light", QColor("#f6ffed")},
        
        {"warning", QColor("#faad14")},
        {"warning-light", QColor("#fffbe6")},
        
        {"error", QColor("#ff4d4f")},
        {"error-light", QColor("#fff2f0")},
        
        {"text-primary", QColor("#262626")},
        {"text-secondary", QColor("#595959")},
        {"text-tertiary", QColor("#8c8c8c")},
        
        {"bg-primary", QColor("#ffffff")},
        {"bg-secondary", QColor("#fafafa")},
        {"bg-tertiary", QColor("#f5f5f7")},
        
        {"border-primary", QColor("#d9d9d9")},
        {"border-secondary", QColor("#e8e8e8")},
    };
    
    // 暗色主题颜色
    darkColors_ = {
        {"primary", QColor("#177ddc")},
        {"primary-hover", QColor("#1890ff")},
        {"primary-pressed", QColor("#0958d9")},
        {"primary-light", QColor("#003a70")},
        
        {"success", QColor("#49aa19")},
        {"success-light", QColor("#274916")},
        
        {"warning", QColor("#d89614")},
        {"warning-light", QColor("#3f3012")},
        
        {"error", QColor("#d32029")},
        {"error-light", QColor("#3f1214")},
        
        {"text-primary", QColor("#e8e8e8")},
        {"text-secondary", QColor("#b0b0b0")},
        {"text-tertiary", QColor("#8c8c8c")},
        
        {"bg-primary", QColor("#2a2a2a")},
        {"bg-secondary", QColor("#262626")},
        {"bg-tertiary", QColor("#1f1f1f")},
        
        {"border-primary", QColor("#3a3a3a")},
        {"border-secondary", QColor("#4a4a4a")},
    };
}

// 向后兼容的静态方法
void ThemeManager::apply(Theme theme) {
    ThemeType type = (theme == Theme::Dark) ? ThemeType::Dark : ThemeType::Light;
    instance()->applyTheme(type);
}

QString ThemeManager::loadStyleSheet(Theme theme) {
    QString qssPath = (theme == Theme::Dark) ? 
        ":/themes/modern_theme_dark.qss" : ":/themes/modern_theme.qss";
    return instance()->loadQssFile(qssPath);
}
