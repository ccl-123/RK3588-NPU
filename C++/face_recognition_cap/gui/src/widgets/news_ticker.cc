/**
 * @file news_ticker.cc
 * @brief 简易跑马灯实现，按序滚动热点标题（双标签无缝滚动）
 */

#include "widgets/news_ticker.h"

#include <QLabel>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSettings>
#include <QTimer>
#include <QElapsedTimer>
#include <algorithm>
#include "config/config.h"

int NewsTicker::global_offset_ = 0;
bool NewsTicker::offset_loaded_ = false;

NewsTicker::NewsTicker(QWidget* parent)
    : QWidget(parent)
    , label_(new QLabel(this))
    , label_next_(new QLabel(this))
    , timer_(new QTimer(this))
    , elapsed_(new QElapsedTimer())
    , current_index_(0)
    , next_ptr_(0)
    , spacing_(Config::UI::NEWS_TICKER_SPACING)
    , speed_px_per_sec_(Config::UI::NEWS_TICKER_SPEED_PX_PER_SEC)
    , pending_start_(false) {
    setObjectName("NewsTicker");
    setFixedHeight(36);
    setAttribute(Qt::WA_StyledBackground, true);
    loadOffset();

    auto initLabel = [this](QLabel* lbl) {
        lbl->setObjectName("NewsTickerLabel");
        lbl->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        lbl->setTextFormat(Qt::RichText);
        QFont f = lbl->font();
        f.setPointSize(Config::UI::NEWS_TICKER_FONT_SIZE);
        lbl->setFont(f);
    };
    initLabel(label_);
    initLabel(label_next_);
    label_next_->hide();

    timer_->setInterval(16);  // ~60fps
    connect(timer_, &QTimer::timeout, this, &NewsTicker::tick);
}

NewsTicker::~NewsTicker() = default;

void NewsTicker::setHeadlines(const QStringList& headlines) {
    timer_->stop();
    elapsed_->invalidate();
    headlines_ = headlines;
    current_index_ = (headlines_.isEmpty() ? 0 : (global_offset_ % headlines_.size()));
    next_ptr_ = current_index_;
    pending_start_ = true;
    ensureStarted();
}

void NewsTicker::clear() {
    timer_->stop();
    elapsed_->invalidate();
    headlines_.clear();
    label_->setText("");
    label_next_->setText("");
    current_index_ = 0;
}

void NewsTicker::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    ensureStarted();
}

void NewsTicker::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ensureStarted();
}

void NewsTicker::ensureStarted() {
    if (!isVisible() || width() <= 0) {
        return;
    }
    if (pending_start_) {
        pending_start_ = false;
        startCurrent();
    }
}

void NewsTicker::startCurrent() {
    if (headlines_.isEmpty()) {
        label_->setText(tr("暂无热点"));
        label_->move(0, 0);
        label_next_->hide();
        timer_->stop();
        return;
    }

    if (current_index_ >= headlines_.size()) {
        current_index_ = 0;
    }

    // 主标签放在右侧起点
    next_ptr_ = current_index_;
    label_->setText(headlines_.at(next_ptr_));
    int w1 = textWidth(label_);
    int start1 = width();
    label_->setGeometry(start1, 0, w1, height());

    // 次标签紧随其后，确保无缝
    next_ptr_ = (next_ptr_ + 1) % headlines_.size();
    label_next_->setText(headlines_.at(next_ptr_));
    int w2 = textWidth(label_next_);
    int start2 = start1 + w1 + spacing_;
    label_next_->setGeometry(start2, 0, w2, height());
    label_next_->show();

    elapsed_->restart();
    if (!timer_->isActive()) {
        timer_->start();
    }
}

void NewsTicker::tick() {
    if (!elapsed_->isValid() || headlines_.isEmpty()) return;
    qint64 ms = elapsed_->restart();
    double dx = (speed_px_per_sec_ * ms) / 1000.0;

    auto moveLabel = [dx](QLabel* lbl) {
        auto g = lbl->geometry();
        g.moveLeft(static_cast<int>(g.x() - dx));
        lbl->setGeometry(g);
    };

    moveLabel(label_);
    moveLabel(label_next_);

    auto recycleIfNeeded = [this](QLabel* lbl, QLabel* other) {
        if (lbl->x() + lbl->width() <= 0) {
            assignNext(lbl, other);
        }
    };

    recycleIfNeeded(label_, label_next_);
    recycleIfNeeded(label_next_, label_);
}

int NewsTicker::textWidth(QLabel* lbl) const {
    QFontMetrics fm(lbl->font());
    return fm.horizontalAdvance(lbl->text()) + 2;  // 略留余量
}

void NewsTicker::assignNext(QLabel* lbl, QLabel* other) {
    if (headlines_.isEmpty()) return;
    // 使用 next_ptr_ 驱动连续播放
    lbl->setText(headlines_.at(next_ptr_));
    int w = textWidth(lbl);
    int new_x = std::max(width(), other->x() + other->width() + spacing_);
    lbl->setGeometry(new_x, 0, w, height());

    // 前进指针并持久化（全局继续点）
    next_ptr_ = (next_ptr_ + 1) % headlines_.size();
    global_offset_ = next_ptr_;
    saveOffset();
}

void NewsTicker::loadOffset() {
    if (offset_loaded_) return;
    QSettings settings("FaceRecognitionApp", "GUI");
    global_offset_ = settings.value("news_ticker/offset", 0).toInt();
    offset_loaded_ = true;
}

void NewsTicker::saveOffset() const {
    QSettings settings("FaceRecognitionApp", "GUI");
    settings.setValue("news_ticker/offset", global_offset_);
}
