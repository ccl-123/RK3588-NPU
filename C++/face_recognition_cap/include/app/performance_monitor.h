/**
 * @file performance_monitor.h
 * @brief 性能监控器 - 流水线多线程架构性能统计
 * @author CL
 * @date 2025-11-20
 * 
 * 流水线架构：
 * - 线程1: V4L2采集 + MPP MJPEG硬解 + NPU输入准备
 * - 线程2: YOLO零拷贝推理 (主线程)
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
#include <mutex>
#include "config/config.h"
#include "rknn_api.h"

class PerformanceMonitor {
public:
    struct PreprocessTimings {
        uint32_t mjpeg_bytes = 0;
        double mpp_input_copy_ms = 0.0;
        double mpp_decode_ms = 0.0;
        double npu_input_wait_ms = 0.0;
        double rga_input_ms = 0.0;
        double preview_ms = 0.0;
        double cpu_fallback_ms = 0.0;
        bool used_cpu_fallback = false;
    };

    PerformanceMonitor(int report_interval = Config::Performance::REPORT_INTERVAL);
    ~PerformanceMonitor() = default;

    // 线程1：压缩流传入、MPP硬解、NPU输入和预览准备耗时
    void record_preprocess_timings(const PreprocessTimings& timings);

    // 线程2：YOLO零拷贝检测耗时
    void record_detection_time(double ms);
    void record_detection_run_time(double ms);
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
    double get_smoothed_fps() const;

    // 报告
    bool should_print_report();
    void print_report();
    void reset();
    void set_report_interval(int report_interval);
    
    // 设置 NPU 上下文，用于查询真实内存占用
    void set_npu_contexts(rknn_context detector_ctx, rknn_context facenet_ctx);

private:
    double get_average(const std::vector<double>& data) const;
    void clear_samples_locked();

    // 资源监控 (Linux /proc)
    double get_cpu_usage();
    double get_memory_usage_mb();
    double get_npu_memory_mb();

private:
    mutable std::mutex metrics_mutex_;
    std::vector<double> mjpeg_packet_kb_;
    std::vector<double> mpp_input_copy_times_;
    std::vector<double> mpp_decode_times_;
    std::vector<double> npu_input_wait_times_;
    std::vector<double> rga_input_times_;
    std::vector<double> preview_times_;
    std::vector<double> cpu_fallback_times_;
    uint64_t preprocess_frame_count_ = 0;
    uint64_t cpu_fallback_frame_count_ = 0;
    std::vector<double> detection_times_;
    std::vector<double> detect_run_times_;
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
