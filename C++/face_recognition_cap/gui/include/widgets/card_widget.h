#pragma once

#include <QFrame>
#include <QString>

class QLabel;
class QVBoxLayout;

/**
 * @brief CardWidget 提供统一的卡片式容器，包含 header/body/footer 槽位。
 */
class CardWidget : public QFrame {
    Q_OBJECT
public:
    explicit CardWidget(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setSubtitle(const QString& subtitle);

    void setHeaderWidget(QWidget* widget);
    QWidget* bodyContainer() const;
    void setFooterWidget(QWidget* widget);

    void setVariant(const QString& variant);
    QString variant() const;

private:
    void rebuildHeader();

    QString variant_;
    QWidget* header_container_;
    QWidget* body_container_;
    QWidget* footer_container_;
    QLabel* title_label_;
    QLabel* subtitle_label_;
    QVBoxLayout* main_layout_;
};

