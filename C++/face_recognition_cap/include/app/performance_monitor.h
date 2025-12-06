/**
 * @file performance_monitor.h
 * @brief 性能监控器 - 流水线多线程架构性能统计
 * @author CL
 * @date 2025-11-20
 * 
 * 流水线架构：
 * - 线程1: 采集 + RGA预处理
 * - 线程2: YOLO检测 (主线程)
 * - 线程2.5: YOLO后处理 (DFL/NMS)
 * - 线程3: 对齐 + FaceNet + 匹配 + 渲染
 * 
 * 监控内容：
 * - 各线程耗时及 FPS
 * - CPU/内存资源占用
 */

#ifndef _PERFORMANCE_MONITOR_H_
#define _PERFORMANCE_MONITOR_H_

#include <vector>
#include <cstdint>
#include <chrono>
#include "config/config.h"
#include "rknn_api.h"

class PerformanceMonitor {
public:
    PerformanceMonitor(int report_interval = Config::Performance::REPORT_INTERVAL);
    ~PerformanceMonitor() = default;

    // 线程1：预处理耗时
    void record_preprocess_time(double ms);

    // 线程2：YOLO检测耗时
    void record_detection_time(double ms);
    void record_detection_inputs_time(double ms);
    void record_detection_run_time(double ms);
    void record_detection_outputs_time(double ms);
    void record_detection_copy_time(double ms);

    // 线程2.5：YOLO后处理耗时
    void record_postprocess_time(double ms);
    
    // 线程3：识别阶段耗时
    void record_alignment_time(double ms);
    void record_recognition_time(double ms);
    void record_matching_time(double ms);
    void record_render_time(double ms);

    // FPS 统计
    void update_fps(double current_fps);
    double get_smoothed_fps() const { return smoothed_fps_; }

    // 报告
    bool should_print_report();
    void print_report();
    void reset();
    
    // 设置 NPU 上下文，用于查询真实内存占用
    void set_npu_contexts(rknn_context detector_ctx, rknn_context facenet_ctx);

private:
    double get_average(const std::vector<double>& data) const;

    // 资源监控 (Linux /proc)
    double get_cpu_usage();
    double get_memory_usage_mb();
    double get_npu_memory_mb();

private:
    std::vector<double> preprocess_times_;
    std::vector<double> detection_times_;
    std::vector<double> detect_inputs_times_;
    std::vector<double> detect_run_times_;
    std::vector<double> detect_outputs_times_;
    std::vector<double> detect_copy_times_;
    std::vector<double> postprocess_times_;
    std::vector<double> alignment_times_;
    std::vector<double> recognition_times_;
    std::vector<double> matching_times_;
    std::vector<double> render_times_;

    double smoothed_fps_;
    int report_interval_;
    int frame_count_;
    std::chrono::steady_clock::time_point last_report_time_;
    
    // CPU 使用率计算 (上次采样值)
    uint64_t last_total_time_;
    uint64_t last_idle_time_;

    // NPU 上下文（值存储，避免悬空指针）
    rknn_context detector_ctx_;
    rknn_context facenet_ctx_;
};

#endif // _PERFORMANCE_MONITOR_H_
