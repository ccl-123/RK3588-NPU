#pragma once

#include <QObject>
#include <QString>

/**
 * @brief ThemeManager 负责加载并应用全局 QSS 主题。
 */
class ThemeManager : public QObject {
    Q_OBJECT
public:
    enum class Theme {
        Light,
        Dark
    };

    /**
     * @brief 加载并应用主题到整个应用或指定根对象。
     * @param theme 主题枚举
     */
    static void apply(Theme theme = Theme::Light);

    /**
     * @brief 读取主题样式文本
     * @param theme 主题枚举
     * @return 样式字符串，失败则为空
     */
    static QString loadStyleSheet(Theme theme = Theme::Light);
};

