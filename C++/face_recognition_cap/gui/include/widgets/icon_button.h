#pragma once

#include <QSize>
#include <QToolButton>

/**
 * @brief IconButton 支持 SVG 图标与徽标的图标按钮。
 */
class IconButton : public QToolButton {
    Q_OBJECT
public:
    explicit IconButton(QWidget* parent = nullptr);

    void setSvg(const QString& resource_path, const QSize& size = QSize(20, 20));
    void setBadgeCount(int count);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateBadgePosition();

    int badge_count_ = 0;
    QWidget* badge_;
    QString current_icon_path_;
    QSize current_icon_size_;
};

