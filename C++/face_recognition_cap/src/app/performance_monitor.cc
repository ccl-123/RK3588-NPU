/**
 * @file performance_monitor.cc
 * @brief 性能监控器实现 - 流水线多线程架构性能统计
 * @author CL
 * @date 2025-11-20
 *
 * 开发板部署监控:
 * - RK3588 NPU: int8 量化模型推理
 * - 资源占用: CPU/内存/NPU内存
 * - RGA加速: 可通过 Config::Performance::USE_RGA 控制
 */

#include "app/performance_monitor.h"
#include <iostream>
#include <iomanip>
#include <numeric>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <spdlog/spdlog.h>

PerformanceMonitor::PerformanceMonitor(int report_interval)
    : smoothed_fps_(0.0)
    , report_interval_(report_interval)
    , frame_count_(0)
    , last_report_time_(std::chrono::steady_clock::now())
    , last_total_time_(0)
    , last_idle_time_(0)
    , detector_ctx_(0)
    , facenet_ctx_(0)
{
}

void PerformanceMonitor::record_preprocess_time(double ms) {
    preprocess_times_.push_back(ms);
}

void PerformanceMonitor::record_decode_time(double ms) {
    decode_times_.push_back(ms);
}

void PerformanceMonitor::record_detection_time(double ms) {
    detection_times_.push_back(ms);
}

void PerformanceMonitor::record_detection_inputs_time(double ms) {
    detect_inputs_times_.push_back(ms);
}

void PerformanceMonitor::record_detection_run_time(double ms) {
    detect_run_times_.push_back(ms);
}

void PerformanceMonitor::record_detection_outputs_time(double ms) {
    detect_outputs_times_.push_back(ms);
}

void PerformanceMonitor::record_detection_copy_time(double ms) {
    detect_copy_times_.push_back(ms);
}

void PerformanceMonitor::record_postprocess_time(double ms) {
    postprocess_times_.push_back(ms);
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
        smoothed_fps_ = 0.1 * current_fps + 0.9 * smoothed_fps_;
    }
    frame_count_++;
}

bool PerformanceMonitor::should_print_report() {
    return (frame_count_ % report_interval_) == 0 && frame_count_ > 0;
}

double PerformanceMonitor::get_cpu_usage() {
    // 读取 /proc/stat 获取 CPU 使用率
    std::ifstream file("/proc/stat");
    if (!file.is_open()) return -1.0;
    
    std::string line;
    std::getline(file, line);
    file.close();
    
    // 解析 cpu 行: cpu user nice system idle iowait irq softirq steal guest guest_nice
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
    if (sscanf(line.c_str(), "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) != 8) {
        return -1.0;
    }
    
    uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
    uint64_t idle_time = idle + iowait;
    
    double cpu_usage = 0.0;
    if (last_total_time_ > 0) {
        uint64_t total_diff = total - last_total_time_;
        uint64_t idle_diff = idle_time - last_idle_time_;
        if (total_diff > 0) {
            cpu_usage = 100.0 * (1.0 - static_cast<double>(idle_diff) / total_diff);
        }
    }
    
    last_total_time_ = total;
    last_idle_time_ = idle_time;
    
    return cpu_usage;
}

double PerformanceMonitor::get_memory_usage_mb() {
    // 读取 /proc/self/status 获取当前进程内存使用
    std::ifstream file("/proc/self/status");
    if (!file.is_open()) return -1.0;
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("VmRSS:") == 0) {
            // VmRSS: 物理内存使用量 (kB)
            uint64_t mem_kb = 0;
            sscanf(line.c_str(), "VmRSS: %lu", &mem_kb);
            return mem_kb / 1024.0;  // 转换为 MB
        }
    }
    return -1.0;
}

double PerformanceMonitor::get_npu_memory_mb() {
    auto query_mem = [](rknn_context ctx, uint32_t& weight_kb, uint32_t& internal_kb, uint32_t& total_kb) -> bool {
        if (ctx == 0) return false;
        rknn_mem_size mem_size;
        memset(&mem_size, 0, sizeof(mem_size));
        int ret = rknn_query(ctx, RKNN_QUERY_MEM_SIZE, &mem_size, sizeof(mem_size));
        if (ret < 0) {
            spdlog::error("RKNN_QUERY_MEM_SIZE failed: {}", ret);
            return false;
        }
        weight_kb = mem_size.total_weight_size / 1024;
        internal_kb = mem_size.total_internal_size / 1024;
        total_kb = (mem_size.total_weight_size + mem_size.total_internal_size) / 1024;
        return true;
    };

    uint32_t total_kb_sum = 0;
    uint32_t w = 0, in = 0, t = 0;
    bool ok = false;
    if (query_mem(detector_ctx_, w, in, t)) {
        total_kb_sum += t;
        ok = true;
    }
    if (query_mem(facenet_ctx_, w, in, t)) {
        total_kb_sum += t;
        ok = true;
    }
    if (!ok) return -1.0;
    return total_kb_sum / 1024.0;
}

void PerformanceMonitor::set_npu_contexts(rknn_context detector_ctx, rknn_context facenet_ctx) {
    detector_ctx_ = detector_ctx;
    facenet_ctx_ = facenet_ctx;
}

void PerformanceMonitor::print_report() {
    if (!Config::Performance::ENABLE_PERF_REPORT) {
        reset();
        return;
    }

    if (detection_times_.empty()) return;

    double avg_dec   = get_average(decode_times_);
    double avg_pre   = get_average(preprocess_times_);
    double avg_detect = get_average(detection_times_);
    double avg_in = get_average(detect_inputs_times_);
    double avg_run = get_average(detect_run_times_);
    double avg_out = get_average(detect_outputs_times_);
    double avg_copy = get_average(detect_copy_times_);
    double avg_post = get_average(postprocess_times_);
    double avg_align = get_average(alignment_times_);
    double avg_facenet = get_average(recognition_times_);
    double avg_match = get_average(matching_times_);
    double avg_render = get_average(render_times_);

    double thread1_total = avg_dec + avg_pre;
    double thread3_total = avg_align + avg_facenet + avg_match + avg_render;
    double bottleneck = std::max({thread1_total, avg_detect, avg_post, thread3_total});
    double theoretical_fps = (bottleneck > 0) ? (1000.0 / bottleneck) : 0.0;
    
    // 线程2 FPS（仅检测线程需要显示 FPS）
    double thread2_fps = (avg_detect > 0) ? (1000.0 / avg_detect) : 0.0;
    
    // 获取资源占用
    double cpu_usage = get_cpu_usage();
    double mem_usage = get_memory_usage_mb();
    double npu_mem = get_npu_memory_mb();

    // 真实 FPS（按实际时间间隔计算，与屏幕显示保持一致）
    auto now = std::chrono::steady_clock::now();
    double elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_report_time_).count();
    double actual_fps = (elapsed_ms > 0) ? (report_interval_ * 1000.0 / elapsed_ms) : smoothed_fps_;
    last_report_time_ = now;

    std::cout << "\n╔══════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║      流水线性能分析 (平均 " << std::setw(3) << report_interval_ << " 帧) - RK3588 部署      ║" << std::endl;
    std::cout << "╠══════════════════════════════════════════════════════════╣" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    
    // 线程耗时
    std::cout << "║ 【线程耗时】                                             ║" << std::endl;
    std::cout << "║  线程1 [采集+解码+RGA]:" << std::setw(6) << thread1_total << " ms                        ║" << std::endl;
    std::cout << "║    ├─ MJPEG解码:      " << std::setw(6) << avg_dec   << " ms                       ║" << std::endl;
    std::cout << "║    └─ RGA预处理:      " << std::setw(6) << avg_pre   << " ms                       ║" << std::endl;
    std::cout << "║  线程2 [YOLO推理]:    " << std::setw(6) << avg_detect << " ms  (" 
              << std::setw(5) << thread2_fps << " FPS)             ║" << std::endl;
    std::cout << "║    ├─ inputs_set:     " << std::setw(6) << avg_in   << " ms                       ║" << std::endl;
    std::cout << "║    ├─ rknn_run:       " << std::setw(6) << avg_run  << " ms                       ║" << std::endl;
    std::cout << "║    ├─ outputs_get:    " << std::setw(6) << avg_out  << " ms                       ║" << std::endl;
    std::cout << "║    └─ memcpy_out:     " << std::setw(6) << avg_copy << " ms                       ║" << std::endl;
    std::cout << "║  线程2.5 [后处理]:    " << std::setw(6) << avg_post << " ms                        ║" << std::endl;
    std::cout << "║  线程3 [识别+渲染]:   " << std::setw(6) << thread3_total << " ms                          ║" << std::endl;
    std::cout << "║    ├─ 人脸对齐:       " << std::setw(6) << avg_align << " ms                       ║" << std::endl;
    std::cout << "║    ├─ FaceNet:        " << std::setw(6) << avg_facenet << " ms                       ║" << std::endl;
    std::cout << "║    ├─ 特征匹配:       " << std::setw(6) << avg_match << " ms                       ║" << std::endl;
    std::cout << "║    └─ 渲染显示:       " << std::setw(6) << avg_render << " ms                       ║" << std::endl;
    
    std::cout << "╠══════════════════════════════════════════════════════════╣" << std::endl;
    
    // 资源占用
    std::cout << "║ 【资源占用】                                             ║" << std::endl;
    std::cout << "║  CPU 使用率:          " << std::setw(6) << cpu_usage << " %                        ║" << std::endl;
    std::cout << "║  进程内存 (RSS):      " << std::setw(6) << mem_usage << " MB                       ║" << std::endl;
    std::cout << "║  NPU 内存 (RKNN):     " << std::setw(6) << npu_mem << " MB                       ║" << std::endl;
    std::cout << "║  模型量化:            int8 (RKNN)                        ║" << std::endl;
    
    std::cout << "╠══════════════════════════════════════════════════════════╣" << std::endl;
    
    // 综合性能
    std::cout << "║ 【综合性能】                                             ║" << std::endl;
    std::cout << "║  实际 FPS:            " << std::setw(6) << actual_fps << "                            ║" << std::endl;
    std::cout << "║  理论最大 FPS:        " << std::setw(6) << theoretical_fps << "                            ║" << std::endl;
    std::cout << "║  流水线瓶颈:          " 
              << (bottleneck == thread1_total ? "线程1 (采集+解码+RGA)      "
                  : (bottleneck == avg_detect ? "线程2 (YOLO推理)            "
                  : (bottleneck == avg_post ? "线程2.5 (后处理)            "
                  : "线程3 (识别+渲染)           ")))
              << "║" << std::endl;
    
    std::cout << "╚══════════════════════════════════════════════════════════╝" << std::endl;

    reset();
}

void PerformanceMonitor::reset() {
    decode_times_.clear();
    preprocess_times_.clear();
    detection_times_.clear();
    detect_inputs_times_.clear();
    detect_run_times_.clear();
    detect_outputs_times_.clear();
    detect_copy_times_.clear();
    postprocess_times_.clear();
    alignment_times_.clear();
    recognition_times_.clear();
    matching_times_.clear();
    render_times_.clear();
}

double PerformanceMonitor::get_average(const std::vector<double>& data) const {
    if (data.empty()) return 0.0;
    return std::accumulate(data.begin(), data.end(), 0.0) / data.size();
}
