/**
 * @file performance_monitor.cc
 * @brief 性能监控器实现 - 流水线多线程架构性能统计
 * @author CL
 * @date 2025-11-20
 * 
 * 开发板部署监控:
 * - RK3588 NPU: int8 量化模型推理
 * - 资源占用: CPU/内存/NPU内存
 */

#include "app/performance_monitor.h"
#include <iostream>
#include <iomanip>
#include <numeric>
#include <fstream>
#include <sstream>
#include <cstring>

PerformanceMonitor::PerformanceMonitor(int report_interval)
    : smoothed_fps_(0.0)
    , report_interval_(report_interval)
    , frame_count_(0)
    , last_total_time_(0)
    , last_idle_time_(0)
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
    // 尝试读取 RKNN NPU 内存使用 (RK3588)
    // 方法1: 通过 /sys/kernel/debug/rknpu/load 读取
    std::ifstream file("/sys/kernel/debug/rknpu/load");
    if (file.is_open()) {
        std::string content;
        std::getline(file, content);
        file.close();
        // 解析 NPU 负载信息
        // 格式可能是: "NPU load: core0 xx%, core1 xx%, ..."
        return 0.0;  // NPU 内存占用需要通过 RKNN API 获取
    }
    
    // 方法2: 估算 - 基于加载的模型
    // YOLOv8-face (int8): ~15-20 MB
    // MobileFaceNet (int8): ~5-8 MB
    return 25.0;  // 估算值
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
    
    // 计算线程2理论 FPS (瓶颈线程)
    double thread2_fps = (avg_detect > 0) ? (1000.0 / avg_detect) : 0.0;
    
    // 获取资源占用
    double cpu_usage = get_cpu_usage();
    double mem_usage = get_memory_usage_mb();
    double npu_mem = get_npu_memory_mb();

    std::cout << "\n╔══════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║      流水线性能分析 (平均 " << std::setw(3) << report_interval_ << " 帧) - RK3588 部署      ║" << std::endl;
    std::cout << "╠══════════════════════════════════════════════════════════╣" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    
    // 线程耗时
    std::cout << "║ 【线程耗时】                                             ║" << std::endl;
    std::cout << "║  线程1 [采集+RGA]:          异步 (不阻塞)                ║" << std::endl;
    std::cout << "║  线程2 [YOLO检测]:    " << std::setw(6) << avg_detect << " ms  (" 
              << std::setw(5) << thread2_fps << " FPS)             ║" << std::endl;
    std::cout << "║  线程3 [识别+渲染]:   " << std::setw(6) << thread3_total << " ms                          ║" << std::endl;
    std::cout << "║    ├─ 人脸对齐:       " << std::setw(6) << avg_align << " ms                       ║" << std::endl;
    std::cout << "║    ├─ FaceNet:        " << std::setw(6) << avg_facenet << " ms                       ║" << std::endl;
    std::cout << "║    └─ 特征匹配:       " << std::setw(6) << avg_match << " ms                       ║" << std::endl;
    
    std::cout << "╠══════════════════════════════════════════════════════════╣" << std::endl;
    
    // 资源占用
    std::cout << "║ 【资源占用】                                             ║" << std::endl;
    std::cout << "║  CPU 使用率:          " << std::setw(6) << cpu_usage << " %                        ║" << std::endl;
    std::cout << "║  进程内存 (RSS):      " << std::setw(6) << mem_usage << " MB                       ║" << std::endl;
    std::cout << "║  NPU 内存 (估算):     " << std::setw(6) << npu_mem << " MB                       ║" << std::endl;
    std::cout << "║  模型量化:            int8 (RKNN)                        ║" << std::endl;
    
    std::cout << "╠══════════════════════════════════════════════════════════╣" << std::endl;
    
    // 综合性能
    std::cout << "║ 【综合性能】                                             ║" << std::endl;
    std::cout << "║  实际 FPS:            " << std::setw(6) << smoothed_fps_ << "                            ║" << std::endl;
    std::cout << "║  理论最大 FPS:        " << std::setw(6) << theoretical_fps << "                            ║" << std::endl;
    std::cout << "║  流水线瓶颈:          " 
              << (avg_detect >= thread3_total ? "线程2 (YOLO检测)            " : "线程3 (识别+渲染)           ")
              << "║" << std::endl;
    
    std::cout << "╚══════════════════════════════════════════════════════════╝" << std::endl;

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
