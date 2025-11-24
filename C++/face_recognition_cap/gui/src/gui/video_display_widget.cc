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
    , scale_x_(1.0)
    , scale_y_(1.0)
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
    
    current_frame_ = frame.clone();
    current_image_ = mat_to_qimage(current_frame_);
    
    // 计算缩放比例
    if (!current_frame_.empty()) {
        scale_x_ = static_cast<double>(width()) / current_frame_.cols;
        scale_y_ = static_cast<double>(height()) / current_frame_.rows;
    }
    
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
    if (!current_image_.isNull()) {
        QImage scaled_image = current_image_.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int x = (width() - scaled_image.width()) / 2;
        int y = 0;  // 靠上对齐，不再居中
        painter.drawImage(x, y, scaled_image);
    }
    
    // 绘制人脸识别结果
    draw_face_results(painter);
    
    // 绘制 FPS
    if (show_fps_) {
        draw_fps(painter);
    }
}

void VideoDisplayWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    
    QMutexLocker locker(&mutex_);
    if (!current_frame_.empty()) {
        scale_x_ = static_cast<double>(width()) / current_frame_.cols;
        scale_y_ = static_cast<double>(height()) / current_frame_.rows;
    }
}

QImage VideoDisplayWidget::mat_to_qimage(const cv::Mat& mat) {
    if (mat.empty()) {
        return QImage();
    }
    
    switch (mat.type()) {
        case CV_8UC1: {
            // 灰度图
            QImage image(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8);
            return image.copy();
        }
        case CV_8UC3: {
            // BGR 转 RGB
            cv::Mat rgb;
            cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
            QImage image(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
            return image.copy();
        }
        case CV_8UC4: {
            // BGRA 转 RGBA
            cv::Mat rgba;
            cv::cvtColor(mat, rgba, cv::COLOR_BGRA2RGBA);
            QImage image(rgba.data, rgba.cols, rgba.rows, rgba.step, QImage::Format_RGBA8888);
            return image.copy();
        }
        default:
            return QImage();
    }
}

void VideoDisplayWidget::draw_face_results(QPainter& painter) {
    if (face_results_.empty() || current_frame_.empty()) {
        return;
    }
    
    // 计算图像显示区域（靠上对齐）
    QImage scaled_image = current_image_.scaled(size(), Qt::KeepAspectRatio);
    int offset_x = (width() - scaled_image.width()) / 2;
    int offset_y = 0;  // 靠上对齐，不再居中
    double scale = static_cast<double>(scaled_image.width()) / current_frame_.cols;
    
    for (const auto& result : face_results_) {
        // 转换坐标
        int x = offset_x + static_cast<int>(result.box.x * scale);
        int y = offset_y + static_cast<int>(result.box.y * scale);
        int w = static_cast<int>(result.box.width * scale);
        int h = static_cast<int>(result.box.height * scale);

        // 注意：人脸框和名称已经由 OpenCV 在图像上绘制，这里不再重复绘制
        // 只绘制额外的提示信息（如"已签到"）

        // 如果已签到，在人脸框上方显示"已签到"提示
        if (result.is_recognized && result.is_duplicate) {
            painter.setFont(QFont("Arial", 12, QFont::Bold));
            painter.setPen(Qt::green);

            QString status_text = QString::fromUtf8("已签到");
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

