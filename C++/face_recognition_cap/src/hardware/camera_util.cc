/**
 * @file camera_util.cc
 * @brief 摄像头底层控制与高性能采集模块 (精简合并版)
 * @details
 * 1. 核心技术：基于 Linux V4L2 框架，利用 mmap 内存映射进行 raw 数据拉取。
 * 2. 简练架构：去除了异步 CPU 软解码线程，仅作为底层的 V4L2 队列提取器，由 PreprocessingThread 同步驱动。
 * 3. 健壮性保证：完备的 IOCTL 错误检查、异常资源清理以及原子态生命周期管理。
 *
 * @author CL
 * @date 2026-05-25
 */

#include <string.h>
#include <cerrno>
#include <memory>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <spdlog/spdlog.h>
#include "camera_util.h"

// ==================== 全局内部变量 (Internal Global States) ====================

/** @brief 摄像头文件描述符 */
static int fd = -1;

/** @brief V4L2 内核缓冲区描述符 (用于 VIDIOC_DQBUF/QBUF) */
static v4l2_buffer buf;

/** @brief 用户空间映射的缓冲区管理数组 */
static Buffer* buffers = nullptr;

/** @brief 摄像头格式配置信息 */
static v4l2_format v4l2_fmt = {};

/** @brief 设备状态标志位 */
static bool camera_opened = false;

// ==================== 摄像头帧率统计 ====================
/** @brief 采集帧计数器 */
static std::atomic<int> capture_frame_count(0);

/** @brief 上次统计时间点 */
static std::chrono::steady_clock::time_point last_fps_calc_time = std::chrono::steady_clock::now();

/** @brief 摄像头真实采集帧率 */
static std::atomic<double> camera_fps(0.0);

/** @brief 运行时故障状态（如设备热拔出） */
static std::atomic<bool> camera_faulted(false);

/** @brief 运行时故障信息 */
static std::mutex camera_error_mutex;
static std::string camera_error_message;

/** @brief 当前打开的设备路径（用于日志和报错） */
static std::string current_device_path;

// ==================== 内部辅助函数 (Helper Functions) ====================

/**
 * @brief 释放已映射的内核缓冲区资源
 * @param count 成功映射过的缓冲区数量
 */
static void cleanup_buffers(unsigned int count) {
    if (buffers != nullptr) {
        for (unsigned int i = 0; i < count; ++i) {
            if (buffers[i].start != nullptr && buffers[i].start != MAP_FAILED) {
                munmap(buffers[i].start, buffers[i].length); // 解除内存映射
                buffers[i].start = nullptr;
            }
        }
        delete[] buffers; // 释放管理数组
        buffers = nullptr;
    }
}

/**
 * @brief 安全关闭文件描述符并清理状态
 */
static void cleanup_fd() {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
    camera_opened = false;
    current_device_path.clear();
}

static void clear_camera_error_state() {
    camera_faulted.store(false, std::memory_order_release);
    camera_fps.store(0.0, std::memory_order_release);
    std::lock_guard<std::mutex> lock(camera_error_mutex);
    camera_error_message.clear();
}

static void set_camera_error_state(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(camera_error_mutex);
        camera_error_message = message;
    }
    camera_faulted.store(true, std::memory_order_release);
    camera_fps.store(0.0, std::memory_order_release);
}

// ==================== 核心功能接口实现 ====================

/**
 * @brief 初始化 USB 摄像头设备并配置 V4L2 环境
 * @return EXIT_SUCCESS 成功, EXIT_FAILURE 失败
 */
int load_usb_camera(std::string device, int camera_width, int camera_height)
{
    if (camera_opened) {
        spdlog::warn("Camera already opened, close it first");
        return EXIT_FAILURE;
    }

    std::string device_path = "/dev/video" + device;
    current_device_path = device_path;
    clear_camera_error_state();

    fd = open(device_path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror(("Failed to open " + device_path).c_str());
        return EXIT_FAILURE;
    }

    // 1. 查询设备能力
    v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
        perror("VIDIOC_QUERYCAP");
        cleanup_fd();
        return EXIT_FAILURE;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        spdlog::error("Device does not support video capture");
        cleanup_fd();
        return EXIT_FAILURE;
    }

    // 2. 配置采集格式：默认使用 MJPEG 以支持高帧率
    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_fmt.fmt.pix.width = camera_width;
    v4l2_fmt.fmt.pix.height = camera_height;
    v4l2_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    v4l2_fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &v4l2_fmt) == -1) {
        perror("VIDIOC_S_FMT");
        cleanup_fd();
        return EXIT_FAILURE;
    }

    spdlog::info("USB camera initialized: {}x{}", v4l2_fmt.fmt.pix.width, v4l2_fmt.fmt.pix.height);

    // 2.5 设置帧率为 30 FPS
    v4l2_streamparm parm = {};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = 30;  // 请求 30 FPS
    if (ioctl(fd, VIDIOC_S_PARM, &parm) == -1) {
        perror("VIDIOC_S_PARM (set frame rate)");
    } else {
        spdlog::info("Camera frame rate set to: {}/{} FPS",
                     parm.parm.capture.timeperframe.denominator,
                     parm.parm.capture.timeperframe.numerator);
    }

    // 2.6 尝试禁用自动曝光优先级（强制帧率优先）
    v4l2_control ctrl;
    ctrl.id = V4L2_CID_EXPOSURE_AUTO_PRIORITY;
    ctrl.value = 0; 
    if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) == -1) {
        // Camera does not support V4L2_CID_EXPOSURE_AUTO_PRIORITY
    } else {
        spdlog::info("Disabled V4L2_CID_EXPOSURE_AUTO_PRIORITY (Force Frame Rate)");
    }

    // 2.7 尝试禁用工频去闪烁
    ctrl.id = V4L2_CID_POWER_LINE_FREQUENCY;
    ctrl.value = 0; 
    if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) == -1) {
        // Power line frequency setting not supported
    } else {
        spdlog::info("Disabled V4L2_CID_POWER_LINE_FREQUENCY");
    }

    // 3. 申请内核级内存缓冲区队列 (Memory Map 模式)
    v4l2_requestbuffers req = {};
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = REQ_COUNT; 

    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        perror("VIDIOC_REQBUFS");
        cleanup_fd();
        return EXIT_FAILURE;
    }

    buffers = new Buffer[REQ_COUNT];
    memset(buffers, 0, sizeof(Buffer) * REQ_COUNT);

    unsigned int mapped_count = 0;
    for (unsigned i = 0; i < req.count; ++i) {
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
            perror("VIDIOC_QUERYBUF");
            cleanup_buffers(mapped_count);
            cleanup_fd();
            return EXIT_FAILURE;
        }

        // 执行内存映射
        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("mmap failed");
            cleanup_buffers(mapped_count);
            cleanup_fd();
            return EXIT_FAILURE;
        }
        mapped_count++;

        // QBUF
        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            perror("VIDIOC_QBUF");
            cleanup_buffers(mapped_count);
            cleanup_fd();
            return EXIT_FAILURE;
        }
    }

    // 4. 正式开启硬件数据流
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        perror("VIDIOC_STREAMON");
        cleanup_buffers(REQ_COUNT);
        cleanup_fd();
        return EXIT_FAILURE;
    }

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    camera_opened = true;
    last_fps_calc_time = std::chrono::steady_clock::now();
    capture_frame_count = 0;
    return EXIT_SUCCESS;
}

/**
 * @brief 从 V4L2 硬件缓冲队列中获取当前最新的 Raw MJPEG 数据包 (零拷贝)
 */
bool read_usb_raw_packet(void** packet_data, uint32_t* packet_size, uint32_t* buffer_index)
{
    if (!camera_opened || fd < 0 || buffers == nullptr) {
        return false;
    }

    v4l2_buffer thread_buf;
    memset(&thread_buf, 0, sizeof(thread_buf));
    thread_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    thread_buf.memory = V4L2_MEMORY_MMAP;

    // [NON-BLOCKING] 从硬件就绪队列中弹出一个已填充数据的缓冲区
    if (ioctl(fd, VIDIOC_DQBUF, &thread_buf) == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false; // 无新帧就绪
        }
        if (errno == EINVAL) {
            return false;
        }

        const std::string error_message =
            "USB camera " + current_device_path + " disconnected or became unavailable during DQBUF: " +
            std::strerror(errno);
        set_camera_error_state(error_message);
        spdlog::error("{}", error_message);
        return false;
    }

    if (thread_buf.index >= REQ_COUNT || buffers[thread_buf.index].start == nullptr) {
        spdlog::error("V4L2 returned an invalid buffer index={}", thread_buf.index);
        return false;
    }

    if (thread_buf.bytesused == 0) {
        spdlog::warn("V4L2 returned an empty frame.");
        // 归还缓冲区
        ioctl(fd, VIDIOC_QBUF, &thread_buf);
        return false;
    }

    *packet_data = buffers[thread_buf.index].start;
    *packet_size = thread_buf.bytesused;
    *buffer_index = thread_buf.index;

    // 统计摄像头真实采集帧率
    capture_frame_count++;
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fps_calc_time);
    if (duration.count() >= 1000) {
        camera_fps = capture_frame_count * 1000.0 / duration.count();
        capture_frame_count = 0;
        last_fps_calc_time = now;
    }

    return true;
}

/**
 * @brief 将处理完的硬件缓冲区重新放入就绪队列 (QBUF)
 */
void release_usb_raw_packet(uint32_t buffer_index)
{
    if (fd < 0 || buffer_index >= REQ_COUNT) {
        return;
    }

    v4l2_buffer thread_buf;
    memset(&thread_buf, 0, sizeof(thread_buf));
    thread_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    thread_buf.memory = V4L2_MEMORY_MMAP;
    thread_buf.index = buffer_index;

    if (ioctl(fd, VIDIOC_QBUF, &thread_buf) == -1) {
        perror("release_usb_raw_packet VIDIOC_QBUF");
    }
}

/**
 * @brief 获取摄像头真实采集帧率
 */
double get_camera_fps()
{
    return camera_fps.load();
}

bool has_usb_camera_error()
{
    return camera_faulted.load(std::memory_order_acquire);
}

std::string get_usb_camera_error()
{
    std::lock_guard<std::mutex> lock(camera_error_mutex);
    return camera_error_message;
}

/**
 * @brief 完整关闭摄像头系统并释放所有硬件资源
 */
void close_usb_camera()
{
    if (!camera_opened) return;

    // 停止视频流
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (fd >= 0) ioctl(fd, VIDIOC_STREAMOFF, &type);

    camera_fps.store(0.0, std::memory_order_release);

    cleanup_buffers(REQ_COUNT);
    cleanup_fd();
}
