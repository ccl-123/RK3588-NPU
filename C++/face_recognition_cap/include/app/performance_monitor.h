/**
 * @file performance_monitor.h
 * @brief 性能监控器 - 流水线多线程架构性能统计
 * @author CL
 * @date 2025-11-20
 * 
 * 流水线架构：
 * - 线程1: 采集 + RGA预处理
 * - 线程2: YOLO检测 (主线程)
 * - 线程3: 对齐 + FaceNet + 匹配 + 渲染
 */

#ifndef _PERFORMANCE_MONITOR_H_
#define _PERFORMANCE_MONITOR_H_

#include <vector>

class PerformanceMonitor {
public:
    PerformanceMonitor(int report_interval = 10);
    ~PerformanceMonitor() = default;

    // 线程2：YOLO检测耗时
    void record_detection_time(double ms);
    
    // 线程3：识别阶段耗时
    void record_alignment_time(double ms);
    void record_recognition_time(double ms);
    void record_matching_time(double ms);

    // FPS 统计
    void update_fps(double current_fps);
    double get_smoothed_fps() const { return smoothed_fps_; }

    // 报告
    bool should_print_report();
    void print_report();
    void reset();

private:
    double get_average(const std::vector<double>& data) const;

private:
    std::vector<double> detection_times_;
    std::vector<double> alignment_times_;
    std::vector<double> recognition_times_;
    std::vector<double> matching_times_;

    double smoothed_fps_;
    int report_interval_;
    int frame_count_;
};

#endif // _PERFORMANCE_MONITOR_H_
