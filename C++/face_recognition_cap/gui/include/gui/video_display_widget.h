/**
 * @file video_display_widget.h
 * @brief 视频显示组件类定义
 * @author CL
 * @date 2025-11-20
 *
 * 用于显示实时视频流和人脸识别结果的 Qt 组件。
 * 支持人脸框绘制、姓名和相似度标注。
 */

#ifndef VIDEO_DISPLAY_WIDGET_H
#define VIDEO_DISPLAY_WIDGET_H

#include <QWidget>
#include <QImage>
#include <QPixmap>
#include <QPainter>
#include <QMutex>
#include <opencv2/opencv.hpp>
#include <vector>
#include <chrono>

/**
 * @brief 人脸识别结果
 */
struct FaceResult {
    cv::Rect box;           // 人脸框
    std::string name;       // 识别的姓名
    float similarity;       // 相似度
    bool is_recognized;     // 是否识别成功
    bool is_duplicate;      // 是否已打卡（5分钟内重复）
    int check_type;         // 打卡类型（1=签到, 2=签退）
};

/**
 * @brief 视频显示组件
 * 
 * 功能：
 * - 实时显示摄像头画面
 * - 绘制人脸框和识别结果
 * - 性能优化（降低刷新率）
 */
class VideoDisplayWidget : public QWidget {
    Q_OBJECT

public:
    explicit VideoDisplayWidget(QWidget* parent = nullptr);
    ~VideoDisplayWidget();

    // 更新显示帧
    void update_frame(const cv::Mat& frame);
    
    // 设置人脸识别结果
    void set_face_results(const std::vector<FaceResult>& results);
    
    // 清空显示
    void clear();
    
    // 设置是否显示 FPS
    void set_show_fps(bool show);
    
    // 设置 NPU 帧率（YOLO 检测能力，约 50+ FPS）
    void set_npu_fps(double fps);
    
    // 设置摄像头采集帧率（约 30 FPS）
    void set_camera_fps(double fps);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // 将 cv::Mat 转换为 QImage
    QImage mat_to_qimage(const cv::Mat& mat);
    
    // 绘制人脸框和识别结果
    void draw_face_results(QPainter& painter, double scale, int offset_x, int offset_y);
    
    // 绘制 FPS
    void draw_fps(QPainter& painter);
    
    // 数据成员
    QMutex mutex_;
    cv::Mat current_frame_;
    QImage current_image_;
    std::vector<FaceResult> face_results_;
    
    // 显示选项
    bool show_fps_;
    double npu_fps_;      // NPU 帧率（YOLO 检测能力，约 50+ FPS）
    double camera_fps_;   // 摄像头采集帧率（约 30 FPS）
    double display_fps_;  // 显示帧率（Qt 渲染帧率，约 28-29 FPS）
    
    // 显示帧率计算（在 paintEvent 中统计）
    int display_frame_count_;
    std::chrono::steady_clock::time_point last_display_fps_time_;
};

#endif // VIDEO_DISPLAY_WIDGET_H
