/**
 * @file performance_monitor.cc
 * @brief 性能监控器实现 - 流水线多线程架构性能统计
 * @author CL
 * @date 2025-11-20
 */

#include "app/performance_monitor.h"
#include <iostream>
#include <iomanip>
#include <numeric>

PerformanceMonitor::PerformanceMonitor(int report_interval)
    : smoothed_fps_(0.0)
    , report_interval_(report_interval)
    , frame_count_(0)
{
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

void PerformanceMonitor::update_fps(double current_fps) {
    if (smoothed_fps_ == 0.0) {
        smoothed_fps_ = current_fps;
    } else {
        smoothed_fps_ = 0.1 * current_fps + 0.9 * smoothed_fps_;
    }
    frame_count_++;
}

bool PerformanceMonitor::should_print_report() {
    return (frame_count_ % report_interval_) == 0 && frame_count_ > 0;
}

void PerformanceMonitor::print_report() {
    if (detection_times_.empty()) return;

    double avg_detect = get_average(detection_times_);
    double avg_align = get_average(alignment_times_);
    double avg_facenet = get_average(recognition_times_);
    double avg_match = get_average(matching_times_);
    
    double thread3_total = avg_align + avg_facenet + avg_match;
    double bottleneck = std::max(avg_detect, thread3_total);
    double theoretical_fps = (bottleneck > 0) ? (1000.0 / bottleneck) : 0.0;
    
    // 计算各线程理论 FPS
    double thread2_fps = (avg_detect > 0) ? (1000.0 / avg_detect) : 0.0;
    double thread3_fps = (thread3_total > 0) ? (1000.0 / thread3_total) : 0.0;

    std::cout << "\n============ 流水线性能分析 (平均 " << report_interval_ << " 帧) ============" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    
    std::cout << "┌─────────────────────────────────────────────────────┐" << std::endl;
    std::cout << "│ 线程1 [采集+RGA]:          异步 (不阻塞)            │" << std::endl;
    std::cout << "├─────────────────────────────────────────────────────┤" << std::endl;
    std::cout << "│ 线程2 [YOLO检测]:    " << std::setw(6) << avg_detect << " ms  (" 
              << std::setw(5) << thread2_fps << " FPS)         │" << std::endl;
    std::cout << "├─────────────────────────────────────────────────────┤" << std::endl;
    std::cout << "│ 线程3 [识别+渲染]:   " << std::setw(6) << thread3_total << " ms  (" 
              << std::setw(5) << thread3_fps << " FPS)         │" << std::endl;
    std::cout << "│   ├─ 人脸对齐:       " << std::setw(6) << avg_align << " ms                   │" << std::endl;
    std::cout << "│   ├─ FaceNet:        " << std::setw(6) << avg_facenet << " ms                   │" << std::endl;
    std::cout << "│   └─ 特征匹配:       " << std::setw(6) << avg_match << " ms                   │" << std::endl;
    std::cout << "└─────────────────────────────────────────────────────┘" << std::endl;
    
    std::cout << "实际 FPS: " << std::setw(5) << smoothed_fps_ 
              << "   理论最大: " << std::setw(5) << theoretical_fps << " FPS" << std::endl;
    std::cout << "瓶颈: " << (avg_detect >= thread3_total ? "线程2 (YOLO检测)" : "线程3 (识别)") << std::endl;
    std::cout << "========================================================" << std::endl;

    reset();
}

void PerformanceMonitor::reset() {
    detection_times_.clear();
    alignment_times_.clear();
    recognition_times_.clear();
    matching_times_.clear();
}

double PerformanceMonitor::get_average(const std::vector<double>& data) const {
    if (data.empty()) return 0.0;
    return std::accumulate(data.begin(), data.end(), 0.0) / data.size();
}
