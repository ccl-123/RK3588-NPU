/**
 * @file theme_manager.h
 * @brief 现代化主题管理器
 * @author CL
 * @date 2025-11-25
 * 
 * 功能：
 * - 浅色/暗色主题切换
 * - QSS 动态加载
 * - 主题配置持久化
 * - 全局主题信号
 */

#pragma once

#include <QObject>
#include <QString>
#include <QColor>
#include <QMap>

/**
 * @brief 主题类型枚举
 */
enum class ThemeType {
    Light,      // 浅色主题
    Dark,       // 暗色主题
    Auto        // 跟随系统（未来扩展）
};

/**
 * @brief 主题管理器类（单例）
 * 
 * 负责全局主题的加载、切换和管理
 */
class ThemeManager : public QObject {
    Q_OBJECT
    
public:
    // 向后兼容的枚举
    enum class Theme {
        Light = static_cast<int>(ThemeType::Light),
        Dark = static_cast<int>(ThemeType::Dark)
    };

    /**
     * @brief 获取单例实例
     */
    static ThemeManager* instance();
    
    /**
     * @brief 初始化主题系统
     * @return 是否成功
     */
    bool initialize();
    
    /**
     * @brief 应用主题
     * @param type 主题类型
     */
    void applyTheme(ThemeType type);
    
    /**
     * @brief 切换主题（浅色 <-> 暗色）
     */
    void toggleTheme();
    
    /**
     * @brief 获取当前主题类型
     */
    ThemeType currentTheme() const { return currentTheme_; }
    
    /**
     * @brief 是否为暗色主题
     */
    bool isDarkMode() const { return currentTheme_ == ThemeType::Dark; }

    /**
     * @brief 获取主题颜色
     * @param colorName 颜色名称（如 "primary", "success"）
     * @return QColor 对象
     */
    QColor getColor(const QString& colorName) const;
    
    /**
     * @brief 重新加载当前主题
     */
    void reloadTheme();
    
    /**
     * @brief 保存主题设置
     */
    void saveThemeSettings();
    
    /**
     * @brief 加载主题设置
     */
    void loadThemeSettings();
    
    // 向后兼容的静态方法
    static void apply(Theme theme = Theme::Light);
    static QString loadStyleSheet(Theme theme = Theme::Light);

signals:
    /**
     * @brief 主题已更改信号
     * @param type 新主题类型
     */
    void themeChanged(ThemeType type);
    
private:
    explicit ThemeManager(QObject* parent = nullptr);
    ~ThemeManager() = default;
    
    // 禁用拷贝
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;
    
    /**
     * @brief 加载 QSS 文件
     * @param filePath QSS 文件路径
     * @return QSS 内容
     */
    QString loadQssFile(const QString& filePath);
    
    /**
     * @brief 初始化颜色映射表
     */
    void initializeColorMaps();
    
private:
    static ThemeManager* instance_;
    ThemeType currentTheme_;
    
    // 颜色映射表
    QMap<QString, QColor> lightColors_;
    QMap<QString, QColor> darkColors_;
};

/**
 * @brief 全局主题管理器访问函数
 */
inline ThemeManager* theme() {
    return ThemeManager::instance();
}
