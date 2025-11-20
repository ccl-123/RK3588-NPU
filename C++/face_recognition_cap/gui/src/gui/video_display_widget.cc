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
    
    // 绘制视频帧
    if (!current_image_.isNull()) {
        QImage scaled_image = current_image_.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int x = (width() - scaled_image.width()) / 2;
        int y = (height() - scaled_image.height()) / 2;
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
    
    // 计算图像显示区域
    QImage scaled_image = current_image_.scaled(size(), Qt::KeepAspectRatio);
    int offset_x = (width() - scaled_image.width()) / 2;
    int offset_y = (height() - scaled_image.height()) / 2;
    double scale = static_cast<double>(scaled_image.width()) / current_frame_.cols;
    
    for (const auto& result : face_results_) {
        // 转换坐标
        int x = offset_x + static_cast<int>(result.box.x * scale);
        int y = offset_y + static_cast<int>(result.box.y * scale);
        int w = static_cast<int>(result.box.width * scale);
        int h = static_cast<int>(result.box.height * scale);

        // 绘制人脸框
        if (result.is_recognized) {
            painter.setPen(QPen(Qt::green, 2));
        } else {
            painter.setPen(QPen(Qt::red, 2));
        }
        painter.drawRect(x, y, w, h);

        // 如果已签到，在人脸框下方显示"已签到"提示
        if (result.is_recognized && result.is_duplicate) {
            painter.setFont(QFont("Arial", 12, QFont::Bold));
            painter.setPen(Qt::green);

            QString status_text = QString::fromUtf8("已签到");
            QFontMetrics fm(painter.font());
            int text_width = fm.horizontalAdvance(status_text);

            // 在人脸框下方居中显示
            int text_x = x + (w - text_width) / 2;
            int text_y = y + h + 20;

            // 绘制半透明背景
            painter.fillRect(text_x - 5, text_y - 15, text_width + 10, 20, QColor(0, 0, 0, 180));
            painter.drawText(text_x, text_y, status_text);
        }

        // 注意：姓名和相似度已经由 OpenCV 在检测框上方绘制，这里不再重复绘制
    }
}

void VideoDisplayWidget::draw_fps(QPainter& painter) {
    QString fps_text = QString("FPS: %1").arg(fps_, 0, 'f', 1);
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 14, QFont::Bold));
    painter.fillRect(10, 10, 100, 30, QColor(0, 0, 0, 180));
    painter.drawText(15, 30, fps_text);
}

