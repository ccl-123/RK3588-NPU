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
#include <chrono>

VideoDisplayWidget::VideoDisplayWidget(QWidget* parent)
    : QWidget(parent)
    , show_fps_(true)
    , npu_fps_(0.0)
    , camera_fps_(0.0)
    , display_fps_(0.0)
    , display_frame_count_(0)
    , last_display_fps_time_(std::chrono::steady_clock::now())
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
    
    // 统计真实的显示帧率（在新帧到达时统计，而不是 paintEvent）
    // 因为 paintEvent 可能被 Qt 事件循环频繁触发，而 update_frame 只在新帧到达时调用
    display_frame_count_++;
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_display_fps_time_);
    if (duration.count() >= 1000) {
        display_fps_ = display_frame_count_ * 1000.0 / duration.count();
        display_frame_count_ = 0;
        last_display_fps_time_ = now;
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

void VideoDisplayWidget::set_npu_fps(double fps) {
    npu_fps_ = fps;
}

void VideoDisplayWidget::set_camera_fps(double fps) {
    camera_fps_ = fps;
}

void VideoDisplayWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
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

        QColor box_color = result.is_recognized ? QColor(0, 255, 0) : QColor(255, 0, 0);
        painter.setPen(QPen(box_color, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(x, y, w, h);

        // 绘制名称和相似度（使用 Qt 绘制以支持中文）
        QString name_text = QString::fromStdString(result.name);
        // 使用 %1/%2 占位符并限制相似度为两位小数
        QString label_text = QString("%1 (%2)").arg(name_text).arg(result.similarity, 0, 'f', 2);

        // 设置字体
        QFont font("WenQuanYi Micro Hei", 14, QFont::Bold);  // 使用支持中文的字体
        painter.setFont(font);
        QFontMetrics fm(font);
        int text_width = fm.horizontalAdvance(label_text);
        int text_height = fm.height();

        // 名称位置：人脸框上方
        int name_x = x;
        int name_y = y - 10;

        // 绘制名称背景
        QColor bg_color(0, 0, 0, 160);
        painter.fillRect(name_x - 2, name_y - text_height, text_width + 4, text_height + 4, bg_color);

        // 绘制名称文字（识别成功绿色，否则红色）
        painter.setPen(box_color);
        painter.drawText(name_x, name_y, label_text);

        // 如果已打卡，在名称上方显示状态提示
        if (result.is_recognized && result.is_duplicate) {
            QFont status_font("WenQuanYi Micro Hei", 12, QFont::Bold);
            painter.setFont(status_font);
            painter.setPen(Qt::green);

            // 根据打卡类型显示不同文字
            QString status_text = (result.check_type == 2) ?
                QString::fromUtf8("已签退") : QString::fromUtf8("已签到");
            QFontMetrics status_fm(status_font);
            int status_width = status_fm.horizontalAdvance(status_text);
            int status_height = status_fm.height();

            // 在名称上方居中显示
            int status_x = x + (w - status_width) / 2;
            int status_y = name_y - text_height - 5;

            // 绘制半透明背景
            painter.fillRect(status_x - 5, status_y - status_height + 5, status_width + 10, status_height + 5, QColor(0, 0, 0, 180));
            painter.drawText(status_x, status_y, status_text);
        }
    }
}

void VideoDisplayWidget::draw_fps(QPainter& painter) {
    // 绘制 FPS 和 REC 指示器
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 背景 - 加宽以容纳三行 FPS
    QRect bg_rect(10, 10, 180, 76);
    painter.setBrush(QColor(0, 0, 0, 150));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(bg_rect, 12, 12);
    
    // REC 红点
    painter.setBrush(QColor(255, 59, 48)); // iOS Red
    painter.drawEllipse(20, 20, 10, 10);
    
    // REC 文本
    painter.setPen(Qt::white);
    painter.setFont(QFont("Segoe UI", 10, QFont::Bold));
    painter.drawText(35, 29, "REC");
    
    // 分隔线
    painter.setPen(QColor(255, 255, 255, 100));
    painter.drawLine(70, 16, 70, 40);
    
    painter.setFont(QFont("Segoe UI", 9, QFont::Bold));
    
    // NPU FPS（第一行）- 检测能力帧率
    QString npu_fps_text = QString("NPU: %1").arg(npu_fps_, 0, 'f', 1);
    painter.setPen(QColor(255, 200, 100));  // 橙黄色
    painter.drawText(78, 27, npu_fps_text);
    
    // 摄像头 FPS（第二行）- 采集帧率
    QString camera_fps_text = QString("CAM: %1").arg(camera_fps_, 0, 'f', 1);
    painter.setPen(QColor(100, 255, 100));  // 浅绿色
    painter.drawText(78, 47, camera_fps_text);
    
    // 显示 FPS（第三行）- 实际渲染帧率
    QString display_fps_text = QString("DSP: %1").arg(display_fps_, 0, 'f', 1);
    painter.setPen(QColor(100, 200, 255));  // 浅蓝色
    painter.drawText(78, 67, display_fps_text);
}
