/**
 * @file video_display_widget.cc
 * @brief 视频显示组件类实现
 * @author CL
 * @date 2025-11-20
 */

#include "gui/video_display_widget.h"
#include <QPainter>
#include <QMutexLocker>
#include <QResizeEvent>

VideoDisplayWidget::VideoDisplayWidget(QWidget* parent)
    : QWidget(parent)
    , show_fps_(true)
    , fps_(0.0)
{
    setMinimumSize(640, 480);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

VideoDisplayWidget::~VideoDisplayWidget() {
}

void VideoDisplayWidget::update_frame(const cv::Mat& frame) {
    QMutexLocker locker(&mutex_);
    
    if (frame.empty()) {
        return;
    }
    
    // 直接保存 Mat（浅拷贝 + 引用计数），避免每帧整帧 clone 带来的 CPU/内存开销
    // 注意：current_image_ 会引用 current_frame_ 的内存，所以必须先保存 current_frame_ 再生成 QImage。
    current_frame_ = frame;
    current_image_ = mat_to_qimage(current_frame_);
    
    update();
}

void VideoDisplayWidget::set_face_results(const std::vector<FaceResult>& results) {
    QMutexLocker locker(&mutex_);
    face_results_ = results;
}

void VideoDisplayWidget::clear() {
    QMutexLocker locker(&mutex_);
    current_frame_ = cv::Mat();
    current_image_ = QImage();
    face_results_.clear();
    update();
}

void VideoDisplayWidget::set_show_fps(bool show) {
    show_fps_ = show;
}

void VideoDisplayWidget::set_fps(double fps) {
    fps_ = fps;
}

void VideoDisplayWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 绘制背景
    painter.fillRect(rect(), Qt::black);
    
    QMutexLocker locker(&mutex_);
    
    // 绘制视频帧（靠上对齐）
    double scale = 1.0;
    int offset_x = 0;
    int offset_y = 0;
    if (!current_image_.isNull()) {
        // 避免每帧创建临时 scaled QImage（会产生额外分配/拷贝），用 drawImage + 目标矩形直接缩放绘制
        const int img_w = current_image_.width();
        const int img_h = current_image_.height();
        if (img_w > 0 && img_h > 0) {
            const double sx = static_cast<double>(width()) / img_w;
            const double sy = static_cast<double>(height()) / img_h;
            scale = std::min(sx, sy);

            const int draw_w = static_cast<int>(img_w * scale);
            const int draw_h = static_cast<int>(img_h * scale);
            offset_x = (width() - draw_w) / 2;
            offset_y = 0;  // 靠上对齐，不再居中

            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.drawImage(QRect(offset_x, offset_y, draw_w, draw_h), current_image_);
        }
    }
    
    // 绘制人脸识别结果
    draw_face_results(painter, scale, offset_x, offset_y);
    
    // 绘制 FPS
    if (show_fps_) {
        draw_fps(painter);
    }
}

void VideoDisplayWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
}

QImage VideoDisplayWidget::mat_to_qimage(const cv::Mat& mat) {
    if (mat.empty()) {
        return QImage();
    }
    
    switch (mat.type()) {
        case CV_8UC1: {
            // 灰度图
            // 注意：QImage 直接引用 Mat 内存；调用方需保证 Mat 生命周期覆盖 QImage 使用期
            return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8);
        }
        case CV_8UC3: {
            // OpenCV 默认 BGR，Qt 5.15 支持 Format_BGR888，可避免每帧 cvtColor + copy
            return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_BGR888);
        }
        case CV_8UC4: {
            // Qt 的 ARGB32 在 little-endian 下内存布局为 BGRA，可直接使用
            return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_ARGB32);
        }
        default:
            return QImage();
    }
}

void VideoDisplayWidget::draw_face_results(QPainter& painter, double scale, int offset_x, int offset_y) {
    if (face_results_.empty() || current_frame_.empty()) {
        return;
    }
    
    for (const auto& result : face_results_) {
        // 转换坐标
        int x = offset_x + static_cast<int>(result.box.x * scale);
        int y = offset_y + static_cast<int>(result.box.y * scale);
        int w = static_cast<int>(result.box.width * scale);
        int h = static_cast<int>(result.box.height * scale);

        // 注意：人脸框和名称已经由 OpenCV 在图像上绘制，这里不再重复绘制
        // 只绘制额外的提示信息（如"已签到"）

        // 如果已打卡，在人脸框上方显示状态提示
        if (result.is_recognized && result.is_duplicate) {
            painter.setFont(QFont("Arial", 12, QFont::Bold));
            painter.setPen(Qt::green);

            // 根据打卡类型显示不同文字
            QString status_text = (result.check_type == 2) ? 
                QString::fromUtf8("已签退") : QString::fromUtf8("已签到");
            QFontMetrics fm(painter.font());
            int text_width = fm.horizontalAdvance(status_text);
            int text_height = fm.height();

            // 在人脸框上方居中显示（名称上方）
            int text_x = x + (w - text_width) / 2;
            int text_y = y - 35;  // 在名称上方，留出空间

            // 绘制半透明背景
            painter.fillRect(text_x - 5, text_y - text_height + 5, text_width + 10, text_height + 5, QColor(0, 0, 0, 180));
            painter.drawText(text_x, text_y, status_text);
        }
    }
}

void VideoDisplayWidget::draw_fps(QPainter& painter) {
    // 绘制 FPS 和 REC 指示器
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 背景
    QRect bg_rect(10, 10, 140, 36);
    painter.setBrush(QColor(0, 0, 0, 150));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(bg_rect, 18, 18);
    
    // REC 红点
    painter.setBrush(QColor(255, 59, 48)); // iOS Red
    painter.drawEllipse(25, 23, 10, 10);
    
    // REC 文本
    painter.setPen(Qt::white);
    painter.setFont(QFont("Segoe UI", 10, QFont::Bold));
    painter.drawText(45, 32, "REC");
    
    // 分隔线
    painter.setPen(QColor(255, 255, 255, 100));
    painter.drawLine(80, 18, 80, 38);
    
    // FPS 文本
    QString fps_text = QString("%1 FPS").arg(fps_, 0, 'f', 1);
    painter.setPen(Qt::white);
    painter.drawText(90, 32, fps_text);
}
