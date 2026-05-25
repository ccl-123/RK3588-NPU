/**
 * @file performance_monitor.cc
 * @brief 性能监控器实现 - 流水线多线程架构性能统计
 * @author CL
 * @date 2025-11-20
 *
 * 开发板部署监控:
 * - RK3588 NPU: int8 量化模型推理
 * - 资源占用: CPU/内存/NPU内存
 * - 解码链路: MPP MJPEG任务接口输出 DRM/NV12
 * - 输入链路: RGA直写 NPU 输入，必要时 CPU 降级
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

void PerformanceMonitor::record_preprocess_timings(const PreprocessTimings& timings) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    mjpeg_packet_kb_.push_back(static_cast<double>(timings.mjpeg_bytes) / 1024.0);
    mpp_input_copy_times_.push_back(timings.mpp_input_copy_ms);
    mpp_decode_times_.push_back(timings.mpp_decode_ms);
    npu_input_wait_times_.push_back(timings.npu_input_wait_ms);
    rga_input_times_.push_back(timings.rga_input_ms);
    preview_times_.push_back(timings.preview_ms);
    cpu_fallback_times_.push_back(timings.cpu_fallback_ms);
    preprocess_frame_count_++;
    if (timings.used_cpu_fallback) {
        cpu_fallback_frame_count_++;
    }
}

void PerformanceMonitor::record_detection_time(double ms) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    detection_times_.push_back(ms);
}

void PerformanceMonitor::record_detection_run_time(double ms) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    detect_run_times_.push_back(ms);
}

void PerformanceMonitor::record_detection_copy_time(double ms) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    detect_copy_times_.push_back(ms);
}

void PerformanceMonitor::record_postprocess_time(double ms) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    postprocess_times_.push_back(ms);
}

void PerformanceMonitor::record_alignment_time(double ms) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    alignment_times_.push_back(ms);
}

void PerformanceMonitor::record_recognition_time(double ms) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    recognition_times_.push_back(ms);
}

void PerformanceMonitor::record_matching_time(double ms) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    matching_times_.push_back(ms);
}

void PerformanceMonitor::record_render_time(double ms) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    render_times_.push_back(ms);
}

void PerformanceMonitor::update_fps(double current_fps) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    if (smoothed_fps_ == 0.0) {
        smoothed_fps_ = current_fps;
    } else {
        smoothed_fps_ = 0.1 * current_fps + 0.9 * smoothed_fps_;
    }
    frame_count_++;
}

bool PerformanceMonitor::should_print_report() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return (frame_count_ % report_interval_) == 0 && frame_count_ > 0;
}

double PerformanceMonitor::get_smoothed_fps() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return smoothed_fps_;
}

void PerformanceMonitor::set_report_interval(int report_interval) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    report_interval_ = report_interval;
    frame_count_ = 0;
    smoothed_fps_ = 0.0;
    last_report_time_ = std::chrono::steady_clock::now();
    clear_samples_locked();
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

    rknn_context detector_ctx = 0;
    rknn_context facenet_ctx = 0;
    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        detector_ctx = detector_ctx_;
        facenet_ctx = facenet_ctx_;
    }

    uint32_t total_kb_sum = 0;
    uint32_t w = 0, in = 0, t = 0;
    bool ok = false;
    if (query_mem(detector_ctx, w, in, t)) {
        total_kb_sum += t;
        ok = true;
    }
    if (query_mem(facenet_ctx, w, in, t)) {
        total_kb_sum += t;
        ok = true;
    }
    if (!ok) return -1.0;
    return total_kb_sum / 1024.0;
}

void PerformanceMonitor::set_npu_contexts(rknn_context detector_ctx, rknn_context facenet_ctx) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    detector_ctx_ = detector_ctx;
    facenet_ctx_ = facenet_ctx;
}

void PerformanceMonitor::print_report() {
    if (!Config::Performance::ENABLE_PERF_REPORT) {
        reset();
        return;
    }

    std::vector<double> mjpeg_packet_kb;
    std::vector<double> mpp_input_copy_times;
    std::vector<double> mpp_decode_times;
    std::vector<double> npu_input_wait_times;
    std::vector<double> rga_input_times;
    std::vector<double> preview_times;
    std::vector<double> cpu_fallback_times;
    std::vector<double> detection_times;
    std::vector<double> detect_run_times;
    std::vector<double> detect_copy_times;
    std::vector<double> postprocess_times;
    std::vector<double> alignment_times;
    std::vector<double> recognition_times;
    std::vector<double> matching_times;
    std::vector<double> render_times;
    uint64_t preprocess_frames = 0;
    uint64_t cpu_fallback_frames = 0;
    double smoothed_fps = 0.0;
    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        if (detection_times_.empty()) return;

        mjpeg_packet_kb.swap(mjpeg_packet_kb_);
        mpp_input_copy_times.swap(mpp_input_copy_times_);
        mpp_decode_times.swap(mpp_decode_times_);
        npu_input_wait_times.swap(npu_input_wait_times_);
        rga_input_times.swap(rga_input_times_);
        preview_times.swap(preview_times_);
        cpu_fallback_times.swap(cpu_fallback_times_);
        detection_times.swap(detection_times_);
        detect_run_times.swap(detect_run_times_);
        detect_copy_times.swap(detect_copy_times_);
        postprocess_times.swap(postprocess_times_);
        alignment_times.swap(alignment_times_);
        recognition_times.swap(recognition_times_);
        matching_times.swap(matching_times_);
        render_times.swap(render_times_);
        preprocess_frames = preprocess_frame_count_;
        cpu_fallback_frames = cpu_fallback_frame_count_;
        preprocess_frame_count_ = 0;
        cpu_fallback_frame_count_ = 0;
        smoothed_fps = smoothed_fps_;
    }

    double avg_mjpeg_packet_kb = get_average(mjpeg_packet_kb);
    double avg_mpp_input_copy = get_average(mpp_input_copy_times);
    double avg_mpp_decode = get_average(mpp_decode_times);
    double avg_npu_wait = get_average(npu_input_wait_times);
    double avg_rga_input = get_average(rga_input_times);
    double avg_preview = get_average(preview_times);
    double avg_cpu_fallback = get_average(cpu_fallback_times);
    double avg_detect = get_average(detection_times);
    double avg_run = get_average(detect_run_times);
    double avg_copy = get_average(detect_copy_times);
    double avg_post = get_average(postprocess_times);
    double avg_align = get_average(alignment_times);
    double avg_facenet = get_average(recognition_times);
    double avg_match = get_average(matching_times);
    double avg_render = get_average(render_times);
    double fallback_percent = preprocess_frames > 0
        ? (100.0 * cpu_fallback_frames / preprocess_frames) : 0.0;

    double thread1_total = avg_mpp_input_copy + avg_mpp_decode + avg_npu_wait +
                           avg_rga_input + avg_preview + avg_cpu_fallback;
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
    double actual_fps = (elapsed_ms > 0) ? (report_interval_ * 1000.0 / elapsed_ms) : smoothed_fps;
    last_report_time_ = now;

    std::cout << "\n╔══════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║      流水线性能分析 (平均 " << std::setw(3) << report_interval_ << " 帧) - RK3588 部署      ║" << std::endl;
    std::cout << "╠══════════════════════════════════════════════════════════╣" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    
    // 线程耗时
    std::cout << "║ 【线程耗时】                                             ║" << std::endl;
    std::cout << "║  线程1 [解码+输入+预览]:" << std::setw(6) << thread1_total << " ms                      ║" << std::endl;
    std::cout << "║    ├─ MJPEG包大小:     " << std::setw(6) << avg_mjpeg_packet_kb << " KB/帧                    ║" << std::endl;
    std::cout << "║    ├─ V4L2->MPP复制:  " << std::setw(6) << avg_mpp_input_copy << " ms                       ║" << std::endl;
    std::cout << "║    ├─ MPP任务处理:    " << std::setw(6) << avg_mpp_decode << " ms                       ║" << std::endl;
    std::cout << "║    ├─ 等待NPU输入:    " << std::setw(6) << avg_npu_wait << " ms                       ║" << std::endl;
    std::cout << "║    ├─ RGA写NPU输入:   " << std::setw(6) << avg_rga_input << " ms                       ║" << std::endl;
    std::cout << "║    ├─ RGA生成预览:    " << std::setw(6) << avg_preview << " ms                       ║" << std::endl;
    std::cout << "║    └─ CPU降级写入:    " << std::setw(6) << avg_cpu_fallback << " ms  ("
              << std::setw(5) << fallback_percent << "%)             ║" << std::endl;
    std::cout << "║  线程2 [RKNN零拷贝]:  " << std::setw(6) << avg_detect << " ms  ("
              << std::setw(5) << thread2_fps << " FPS)             ║" << std::endl;
    std::cout << "║    ├─ rknn_run:       " << std::setw(6) << avg_run << " ms                       ║" << std::endl;
    std::cout << "║    └─ 输出隔离拷贝:   " << std::setw(6) << avg_copy << " ms                       ║" << std::endl;
    std::cout << "║  线程2.5 [后处理]:    " << std::setw(6) << avg_post << " ms                        ║" << std::endl;
    std::cout << "║  线程3 [识别+渲染]:   " << std::setw(6) << thread3_total << " ms                          ║" << std::endl;
    std::cout << "║    ├─ 人脸对齐:       " << std::setw(6) << avg_align << " ms                       ║" << std::endl;
    std::cout << "║    ├─ FaceNet:        " << std::setw(6) << avg_facenet << " ms                       ║" << std::endl;
    std::cout << "║    ├─ 特征匹配:       " << std::setw(6) << avg_match << " ms                       ║" << std::endl;
    std::cout << "║    └─ 提交/软件绘制:  " << std::setw(6) << avg_render << " ms                       ║" << std::endl;
    
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
              << (bottleneck == thread1_total ? "线程1 (解码+输入+预览)      "
                  : (bottleneck == avg_detect ? "线程2 (RKNN零拷贝)          "
                  : (bottleneck == avg_post ? "线程2.5 (后处理)            "
                  : "线程3 (识别+渲染)           ")))
              << "║" << std::endl;
    
    std::cout << "╚══════════════════════════════════════════════════════════╝" << std::endl;

}

void PerformanceMonitor::reset() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    clear_samples_locked();
}

void PerformanceMonitor::clear_samples_locked() {
    mjpeg_packet_kb_.clear();
    mpp_input_copy_times_.clear();
    mpp_decode_times_.clear();
    npu_input_wait_times_.clear();
    rga_input_times_.clear();
    preview_times_.clear();
    cpu_fallback_times_.clear();
    preprocess_frame_count_ = 0;
    cpu_fallback_frame_count_ = 0;
    detection_times_.clear();
    detect_run_times_.clear();
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
