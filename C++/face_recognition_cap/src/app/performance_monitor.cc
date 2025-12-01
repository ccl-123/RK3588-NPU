/**
 * @file performance_monitor.cc
 * @brief 性能监控器实现
 * @author CL
 * @date 2025-11-20
 */

#include "app/performance_monitor.h"
#include <iostream>
#include <iomanip>
#include <numeric>

PerformanceMonitor::PerformanceMonitor(int report_interval)
    : smoothed_fps_(0.0)
    , fps_alpha_(0.1)  // 平滑系数
    , report_interval_(report_interval)
    , frame_count_(0)
{
}

PerformanceMonitor::~PerformanceMonitor() {
}

void PerformanceMonitor::record_camera_time(double ms) {
    camera_times_.push_back(ms);
}

void PerformanceMonitor::record_preprocess_time(double ms) {
    preprocess_times_.push_back(ms);
}

void PerformanceMonitor::record_detection_time(double ms) {
    detection_times_.push_back(ms);
}

void PerformanceMonitor::record_alignment_time(double ms) {
    alignment_times_.push_back(ms);
}

void PerformanceMonitor::record_recognition_time(double ms) {
    recognition_times_.push_back(ms);
}

void PerformanceMonitor::record_matching_time(double ms) {
    matching_times_.push_back(ms);
}

void PerformanceMonitor::record_render_time(double ms) {
    render_times_.push_back(ms);
}

void PerformanceMonitor::update_fps(double current_fps) {
    if (smoothed_fps_ == 0.0) {
        smoothed_fps_ = current_fps;
    } else {
        smoothed_fps_ = fps_alpha_ * current_fps + (1.0 - fps_alpha_) * smoothed_fps_;
    }
    frame_count_++;
}

bool PerformanceMonitor::should_print_report() {
    return (frame_count_ % report_interval_) == 0 && frame_count_ > 0;
}

void PerformanceMonitor::print_report() {
    if (camera_times_.empty()) return;

    double avg_camera = get_average(camera_times_);
    double avg_preprocess = get_average(preprocess_times_);
    double avg_detection = get_average(detection_times_);
    double avg_alignment = get_average(alignment_times_);
    double avg_recognition = get_average(recognition_times_);
    double avg_matching = get_average(matching_times_);
    double avg_render = get_average(render_times_);

    double main_thread_time = avg_detection + avg_alignment + avg_recognition + avg_matching;
    double theoretical_fps = (main_thread_time > 0) ? (1000.0 / main_thread_time) : 0.0;

    std::cout << "\n========== 性能分析 (平均 " << report_interval_ << " 帧) ==========" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "1. 摄像头读取:      " << avg_camera << " ms (线程1-异步)" << std::endl;
    std::cout << "2. RGA预处理:       " << avg_preprocess << " ms (线程2-异步)" << std::endl;
    std::cout << "3. YOLOv8-face:    " << avg_detection << " ms (主线程-人脸检测)" << std::endl;
    std::cout << "4. 人脸对齐:        " << avg_alignment << " ms" << std::endl;
    std::cout << "5. FaceNet:         " << avg_recognition << " ms (512维特征提取)" << std::endl;
    std::cout << "6. 特征匹配:        " << avg_matching << " ms" << std::endl;
    std::cout << "7. 显示渲染:        " << avg_render << " ms (线程3-异步)" << std::endl;
    std::cout << "-------------------------------------------" << std::endl;
    std::cout << "主线程耗时:        " << main_thread_time << " ms (" << smoothed_fps_ << " FPS)" << std::endl;
    std::cout << "理论最大FPS:      " << theoretical_fps << " (瓶颈: YOLOv8-face)" << std::endl;
    std::cout << "===========================================" << std::endl;

    // 重置统计
    reset();
}

void PerformanceMonitor::reset() {
    camera_times_.clear();
    preprocess_times_.clear();
    detection_times_.clear();
    alignment_times_.clear();
    recognition_times_.clear();
    matching_times_.clear();
    render_times_.clear();
}

double PerformanceMonitor::get_average(const std::vector<double>& data) const {
    if (data.empty()) return 0.0;
    return std::accumulate(data.begin(), data.end(), 0.0) / data.size();
}

double PerformanceMonitor::get_us(struct timeval t) const {
    return (t.tv_sec * 1000000 + t.tv_usec);
}

