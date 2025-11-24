#pragma once

#include <QString>
#include <QWidget>

class QLineEdit;
class QLabel;
class QTimer;

/**
 * @brief SearchInput 提供带前缀图标与防抖输出的输入框。
 */
class SearchInput : public QWidget {
    Q_OBJECT
public:
    explicit SearchInput(QWidget* parent = nullptr);

    void setPlaceholderText(const QString& text);
    void setDebounceInterval(int ms);
    QString text() const;

signals:
    void searchTriggered(const QString& text);

private slots:
    void onTextChanged(const QString& text);
    void emitSearch();

private:
    QLineEdit* line_edit_;
    QLabel* icon_label_;
    QTimer* debounce_timer_;
};

