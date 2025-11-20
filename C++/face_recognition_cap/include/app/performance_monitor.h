/**
 * @file performance_monitor.h
 * @brief 性能监控器 - 统计和显示各阶段耗时
 * @author CL
 * @date 2025-11-20
 */

#ifndef _PERFORMANCE_MONITOR_H_
#define _PERFORMANCE_MONITOR_H_

#include <sys/time.h>
#include <string>
#include <vector>

/*-------------------------------------------
    性能监控类
    职责: 统计和显示各阶段耗时
-------------------------------------------*/
class PerformanceMonitor {
public:
    PerformanceMonitor(int report_interval = 10);
    ~PerformanceMonitor();

    // 记录各阶段耗时
    void record_camera_time(double ms);
    void record_preprocess_time(double ms);
    void record_detection_time(double ms);
    void record_alignment_time(double ms);
    void record_recognition_time(double ms);
    void record_matching_time(double ms);
    void record_render_time(double ms);

    // 更新FPS
    void update_fps(double current_fps);
    double get_smoothed_fps() const { return smoothed_fps_; }

    // 检查是否需要打印报告
    bool should_print_report();

    // 打印性能报告
    void print_report();

    // 重置统计
    void reset();

private:
    // 计算平均值
    double get_average(const std::vector<double>& data) const;

    // 时间转换
    double get_us(struct timeval t) const;

private:
    // 统计数据
    std::vector<double> camera_times_;
    std::vector<double> preprocess_times_;
    std::vector<double> detection_times_;
    std::vector<double> alignment_times_;
    std::vector<double> recognition_times_;
    std::vector<double> matching_times_;
    std::vector<double> render_times_;

    // FPS平滑
    double smoothed_fps_;
    double fps_alpha_;  // 平滑系数

    // 报告间隔
    int report_interval_;
    int frame_count_;
};

#endif // _PERFORMANCE_MONITOR_H_

