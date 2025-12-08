/**
 * @file news_ticker.h
 * @brief 简易跑马灯组件，用于顺序滚动热点标题
 */

#pragma once

#include <QWidget>
#include <QStringList>

class QLabel;
class QPropertyAnimation;
class QResizeEvent;
class QShowEvent;
class QSettings;
class QTimer;
class QElapsedTimer;

class NewsTicker : public QWidget {
    Q_OBJECT
public:
    explicit NewsTicker(QWidget* parent = nullptr);
    ~NewsTicker();

    // 设置新的标题列表并立即开始滚动
    void setHeadlines(const QStringList& headlines);
    void clear();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void startCurrent();
    void ensureStarted();
    void tick();
    int textWidth(QLabel* label) const;
    void assignNext(QLabel* label, QLabel* other);
    void loadOffset();
    void saveOffset() const;

    static int global_offset_;
    static bool offset_loaded_;

    QLabel* label_;
    QLabel* label_next_;
    QTimer* timer_;
    QElapsedTimer* elapsed_;
    QStringList headlines_;
    int current_index_;
    int next_ptr_;
    int spacing_;
    int speed_px_per_sec_;
    bool pending_start_;
};


