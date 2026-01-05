/**
 * @file camera_util.cc
 * @brief 摄像头底层控制与高性能采集模块
 * @details
 * 1. 核心技术：基于 Linux V4L2 框架，利用 mmap 内存映射实现内核到用户空间的零拷贝采集。
 * 2. 高性能设计：采用独立采集线程 + 智能指针帧管理，消除应用层常见的深拷贝性能瓶颈。
 * 3. 健壮性保证：完备的 IOCTL 错误检查、异常资源清理以及原子态线程生命周期管理。
 *
 * @author CL
 * @date 2025-11-20
 */

#include <string.h>
#include <memory>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <opencv2/opencv.hpp>
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

// ==================== 异步采集与内存同步变量 ====================

/** @brief 负责持续从硬件拉取数据的独立后台线程 */
static std::thread capture_thread;

/** @brief 采集线程运行状态原子标志 */
static std::atomic<bool> capture_running(false);

/** @brief 互斥锁：保护全局帧指针 current_frame 的原子替换 */
static std::mutex frame_mutex;

/** 
 * @brief 全局最新的帧数据容器
 * @details 
 * 核心优化：使用 shared_ptr 存储 cv::Mat。
 * 采集线程生产新帧时，原子性地替换此指针；处理线程读取时，获取其引用的浅拷贝副本。
 * 此机制配合 cv::Mat 的内部引用计数，实现了真正的读写分离与零拷贝传递。
 */
static std::shared_ptr<cv::Mat> current_frame;

// ==================== 摄像头帧率统计 ====================
/** @brief 采集帧计数器 */
static std::atomic<int> capture_frame_count(0);

/** @brief 上次统计时间点 */
static std::chrono::steady_clock::time_point last_fps_calc_time = std::chrono::steady_clock::now();

/** @brief 摄像头真实采集帧率 */
static std::atomic<double> camera_fps(0.0);

/** @brief 帧序列号（每次采集到新帧时递增，用于检测是否有新帧） */
static std::atomic<uint64_t> frame_sequence(0);

/** @brief 上次读取时的帧序列号 */
static std::atomic<uint64_t> last_read_sequence(0);

// ==================== 内部辅助函数 (Helper Functions) ====================

/**
 * @brief 释放已映射的内核缓冲区资源
 * @param count 成功映射过的缓冲区数量
 * @note 逆序清理，确保在初始化失败或设备关闭时不会发生内存泄漏。
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
    fd = open(device_path.c_str(), O_RDWR);
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
        // 不视为致命错误，继续执行
    } else {
        spdlog::info("Camera frame rate set to: {}/{} FPS",
                     parm.parm.capture.timeperframe.denominator,
                     parm.parm.capture.timeperframe.numerator);
    }

    // 2.6 尝试禁用自动曝光优先级（强制帧率优先）
    // 许多 USB 摄像头在光线不足时会自动降低帧率以增加曝光时间。
    // 将此值设为 0 可以告诉摄像头优先保持帧率，即使图像可能会变暗。
    v4l2_control ctrl;
    ctrl.id = V4L2_CID_EXPOSURE_AUTO_PRIORITY;
    ctrl.value = 0; // 0 = Disable auto priority (Maintain Frame Rate)
    if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) == -1) {
        // Camera does not support V4L2_CID_EXPOSURE_AUTO_PRIORITY
    } else {
        spdlog::info("Disabled V4L2_CID_EXPOSURE_AUTO_PRIORITY (Force Frame Rate)");
    }

    // 2.7 尝试禁用工频去闪烁（可能限制帧率为 25/50 或 30/60）
    // V4L2_CID_POWER_LINE_FREQUENCY: 0=Disabled, 1=50Hz, 2=60Hz
    ctrl.id = V4L2_CID_POWER_LINE_FREQUENCY;
    ctrl.value = 0; // Disabled
    if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) == -1) {
        // perror("V4L2_CID_POWER_LINE_FREQUENCY");
    } else {
        spdlog::info("Disabled V4L2_CID_POWER_LINE_FREQUENCY");
    }

    // 3. 申请内核级内存缓冲区队列 (Memory Map 模式)
    v4l2_requestbuffers req = {};
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = REQ_COUNT; // 默认申请 4 个缓冲区

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

        // 执行内存映射：将内核分配的硬件缓冲区映射到用户空间指针
        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("mmap failed");
            cleanup_buffers(mapped_count);
            cleanup_fd();
            return EXIT_FAILURE;
        }
        mapped_count++;

        // 将映射好的缓冲区重新放入硬件就绪队列 (QBUF)
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

    // 初始化状态
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    current_frame.reset();
    camera_opened = true;
    return EXIT_SUCCESS;
}

/**
 * @brief 采集线程核心逻辑：负责循环拉取数据并完成解码
 */
static void usb_capture_thread_func()
{
    v4l2_buffer thread_buf;
    thread_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    thread_buf.memory = V4L2_MEMORY_MMAP;

    // 初始化帧率统计
    last_fps_calc_time = std::chrono::steady_clock::now();
    capture_frame_count = 0;

    while (capture_running) {
        // [BLOCKING] 从硬件就绪队列中弹出一个已填充数据的缓冲区
        if (ioctl(fd, VIDIOC_DQBUF, &thread_buf) == -1) {
            if (errno == EAGAIN) continue;
            perror("VIDIOC_DQBUF failed in capture thread");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!capture_running) break;
            continue;
        }

        // [CPU-INTENSIVE] 解码 MJPEG 到 BGR 格式
        // 指针 buffers[i].start 通过 mmap 直接指向内核内存，此处为 0 拷贝访问
        cv::Mat raw_data(1, thread_buf.bytesused, CV_8UC1, buffers[thread_buf.index].start);
        cv::Mat decoded_frame = cv::imdecode(raw_data, cv::IMREAD_COLOR);

        if (!decoded_frame.empty()) {
            // [OPTIMIZATION] 使用移动语义将解码后的帧封装入 shared_ptr，原子性更新全局指针
            // 旧帧引用的引用计数会在 current_frame 被替换时自动递减
            auto new_frame = std::make_shared<cv::Mat>(std::move(decoded_frame));
            {
                std::lock_guard<std::mutex> lock(frame_mutex);
                current_frame = new_frame;
                frame_sequence++;  // 递增帧序列号，表示有新帧
            }
            
            // 统计摄像头真实采集帧率
            capture_frame_count++;
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fps_calc_time);
            if (duration.count() >= 1000) {
                camera_fps = capture_frame_count * 1000.0 / duration.count();
                capture_frame_count = 0;
                last_fps_calc_time = now;
            }
        }

        // 将缓冲区重新放入硬件接收队列
        if (ioctl(fd, VIDIOC_QBUF, &thread_buf) == -1) {
            perror("VIDIOC_QBUF");
        }
    }
}

/**
 * @brief 线程安全地启动后台采集任务
 */
void start_usb_capture_thread()
{
    bool expected = false;
    // 使用 CAS 原子操作保证只启动一个采集线程
    if (capture_running.compare_exchange_strong(expected, true)) {
        // 重置帧序列号
        frame_sequence.store(0);
        last_read_sequence.store(0);
        capture_thread = std::thread(usb_capture_thread_func);
    }
}

/**
 * @brief 安全停止采集任务：保证线程完全退出且不发生阻塞
 */
void stop_usb_capture_thread()
{
    bool expected = true;
    if (capture_running.compare_exchange_strong(expected, false)) {
        // 关键点：提前发送 STREAMOFF 信号，强行中断可能阻塞在 DQBUF 的 ioctl 调用
        if (fd >= 0) {
            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(fd, VIDIOC_STREAMOFF, &type);
        }

        if (capture_thread.joinable()) {
            capture_thread.join();
        }
    }
}

/**
 * @brief 从全局缓冲区读取最新的图像帧
 * @param[out] orig_img 输出的图像容器
 * @return true 有新帧可用, false 无新帧或缓冲区为空
 * @note [ZERO-COPY] 利用 cv::Mat 的浅拷贝（引用计数机制）实现，不产生像素级拷贝。
 *       只有当帧序列号变化时才返回 true，避免流水线重复处理同一帧。
 */
bool read_usb_frame(cv::Mat *orig_img)
{
    std::lock_guard<std::mutex> lock(frame_mutex);
    
    // 检查帧序列号是否变化（是否有新帧）
    uint64_t current_seq = frame_sequence.load();
    if (current_seq == last_read_sequence.load()) {
        // 没有新帧，返回 false
        return false;
    }
    
    if (current_frame && !current_frame->empty()) {
        // 浅拷贝：orig_img 与 current_frame 共享同一块像素内存，底层引用计数 +1
        *orig_img = *current_frame;
        last_read_sequence.store(current_seq);  // 更新已读序列号
        return true;
    }
    return false;
}

/**
 * @brief 获取摄像头真实采集帧率
 * @return 摄像头采集帧率（约 30 FPS）
 */
double get_camera_fps()
{
    return camera_fps.load();
}

/**
 * @brief 完整关闭摄像头系统并释放所有硬件资源
 */
void close_usb_camera()
{
    stop_usb_capture_thread(); // 首先停止并销毁采集线程

    if (!camera_opened) return;

    // 停止视频流
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (fd >= 0) ioctl(fd, VIDIOC_STREAMOFF, &type);

    // 清空全局帧引用，触发最后一帧的内存回收
    {
        std::lock_guard<std::mutex> lock(frame_mutex);
        current_frame.reset();
    }

    cleanup_buffers(REQ_COUNT);
    cleanup_fd();
}

/**
 * ==============================================================================
 * @section 内存管理与多线程机制深度解析 (Technical Appendices)
 * ==============================================================================
 * 
 * 1. 数据流与内存拷贝分析 (Data Flow & Memory Copy Analysis):
 * 
 * ┌─────────────────┬────────────┬──────────────────┬───────────────────────────┐
 * │ 阶段            │ 动作       │ 方式             │ 是否拷贝/耗时             │
 * ├─────────────────┼────────────┼──────────────────┼───────────────────────────┤
 * │ Cam -> Kernel   │ 采集       │ DMA              │ 0 拷贝 (硬件完成)         │
 * │ Kernel -> User  │ 获取数据   │ mmap             │ 0 拷贝 (指针映射)         │
 * │ User            │ MJPEG 解码 │ CPU 解码         │ 耗时操作 (生成新数据)     │
 * │ User            │ 存储帧     │ shared_ptr+move  │ 0 拷贝 (所有权转移)  ← 优化 │
 * │ User            │ 读取帧     │ cv::Mat 浅拷贝   │ 0 拷贝 (引用计数)    ← 优化 │
 * │ User            │ RGA 预处理 │ RGA 硬件         │ 硬件搬运 (极快)           │
 * │ User -> Display │ Qt 显示    │ QImage 转换      │ 1-2 次拷贝 (格式转换+渲染)│
 * └─────────────────┴────────────┴──────────────────┴───────────────────────────┘
 * 
 * 2. cv::Mat 的引用计数机制 (OpenCV Ref-counting):
 *    cv::Mat 由"矩阵头"和"像素数据指针"组成。当执行 `Mat A = B` 时，仅复制矩阵头，
 *    并让底层数据的引用计数加 1。数据只在计数归零时被释放。
 * 
 * 2. 浅拷贝 vs 深拷贝 (Shallow vs Deep Copy):
 *    - 浅拷贝 (Shallow)：`A = B`。速度极快，共享像素内存。本模块核心使用。
 *    - 深拷贝 (Deep)：`A = B.clone()`。完全复制数据，消耗大量 CPU 和带宽。
 * 
 * 3. shared_ptr 内存管理 (Smart Pointer Management):
 *    - `current_frame` 使用 `std::shared_ptr` 进一步包装了 `cv::Mat`。
 *    - 采集线程产生新帧后，替换全局 `shared_ptr`。旧帧如果没有其他引用者，
 *      会在此刻立即释放。如果处理线程还在使用旧帧，旧帧会存活至处理线程结束。
 * 
 * 4. 多线程环境下的内存安全 (Thread Safety):
 *    - 采集标志：`std::atomic<bool>` 配合 `compare_exchange_strong` 保证了 
 *      Start/Stop 的原子性，消除了竞态条件。
 *    - 帧交换：`std::lock_guard` 保护 `current_frame` 的指针替换过程。
 *      由于仅交换指针（几个字节），锁的粒度极小（纳秒级），处理线程不会造成
 *      采集线程的实质性阻塞。
 * 
 * 5. 帧数据的生命周期 (Frame Lifecycle):
 *    [采集线程解码] -> [封装进 shared_ptr] -> [原子替换 current_frame] ->
 *    [处理线程 read_usb_frame 浅拷贝] -> [数据在处理线程中处理] ->
 *    [处理任务结束 Mat 析构] -> [引用计数归零，底层堆内存释放]。
 * 
 * ==============================================================================
 */
