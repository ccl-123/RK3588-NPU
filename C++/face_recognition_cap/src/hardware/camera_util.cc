/**
 * @file camera_util.cc
 * @brief 摄像头控制工具实现
 * @details 基于 V4L2 的 USB摄像头控制实现，
 *          使用异步采集模式 (独立线程 + shared_ptr 帧管理) 以提高性能。
 *          使用 mmap 实现内核到用户空间的零拷贝采集。
 *          优化：使用 shared_ptr + cv::Mat 浅拷贝消除双缓冲深拷贝开销。
 * @author CL
 * @date 2025-11-20
 */

#include <string.h>
#include <iostream>
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
#include "camera_util.h"

// ==================== 全局状态变量 ====================

// 摄像头文件描述符
static int fd = -1;

// V4L2 缓冲区信息 (用于 ioctl 调用)
static v4l2_buffer buf;

// 用户空间映射的缓冲区指针数组
static Buffer* buffers = nullptr;

// 摄像头格式配置
static v4l2_format fmt = {};

// 标记摄像头是否已成功打开并初始化
static bool camera_opened = false;

// ==================== 异步采集变量 ====================

// 采集线程对象
static std::thread capture_thread;

// 采集线程运行标志
static std::atomic<bool> capture_running(false);

// 保护帧指针读写的互斥锁
static std::mutex frame_mutex;

// 优化：使用 shared_ptr 存储当前帧，消除深拷贝开销
// 采集线程创建新帧后原子替换指针，读取线程获取引用计数副本
// cv::Mat 本身是引用计数的，赋值操作只增加引用计数，不拷贝数据
static std::shared_ptr<cv::Mat> current_frame;

/**
 * @brief 清理已映射的 V4L2 缓冲区
 * @param count 需要清理的缓冲区数量
 * @note 此函数用于错误处理路径和关闭摄像头时的资源释放。
 *       它会调用 munmap 解除内存映射，并释放 buffers 数组内存。
 */
static void cleanup_buffers(unsigned int count) {
    if (buffers != nullptr) {
        for (unsigned int i = 0; i < count; ++i) {
            if (buffers[i].start != nullptr && buffers[i].start != MAP_FAILED) {
                munmap(buffers[i].start, buffers[i].length);
                buffers[i].start = nullptr;
            }
        }
        delete[] buffers;
        buffers = nullptr;
    }
}

/**
 * @brief 关闭文件描述符并重置状态
 * @note 将 fd 重置为 -1 并设置 camera_opened = false。
 *       这是资源清理的最后一步。
 */
static void cleanup_fd() {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
    camera_opened = false;
}

/**
 * @brief 初始化 USB 摄像头 (V4L2) 并准备异步采集资源
 * 
 * 此函数执行以下步骤：
 * 1. 打开视频设备文件 (/dev/videoX)。
 * 2. 检查设备能力 (是否支持 Video Capture)。
 * 3. 设置采集格式 (MJPEG, 分辨率)。
 * 4. 申请内核缓冲区 (REQBUFS)。
 * 5. 执行内存映射 (mmap)，实现零拷贝访问。
 * 6. 启动视频流 (STREAMON)。
 * 7. 初始化双缓冲内存。
 * 
 * @param device 设备节点名称 (例如 "0" 对应 /dev/video0)
 * @param camera_width 期望的采集宽度
 * @param camera_height 期望的采集高度
 * @return EXIT_SUCCESS 成功, EXIT_FAILURE 失败
 * @note 如果初始化失败，函数内部会自动清理已分配的资源。
 */
int load_usb_camera(std::string device, int camera_width, int camera_height)
{
    // 防止重复打开
    if (camera_opened) {
        std::cerr << "Camera already opened, close it first" << std::endl;
        return EXIT_FAILURE;
    }

    std::string prefix = "/dev/video";
    std::string device_path = prefix + device;

    fd = open(device_path.c_str(), O_RDWR);
    if (fd < 0) {
        std::cerr << "Failed to open device: " << device_path << std::endl;
        perror("open");
        return EXIT_FAILURE;
    }

    v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
        std::cerr << "IOCTL failed: VIDIOC_QUERYCAP" << std::endl;
        perror("VIDIOC_QUERYCAP");
        cleanup_fd();
        return EXIT_FAILURE;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        std::cerr << "Device does not support video capture" << std::endl;
        cleanup_fd();
        return EXIT_FAILURE;
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = camera_width;
    fmt.fmt.pix.height = camera_height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        std::cerr << "IOCTL failed: VIDIOC_S_FMT (requested: " << camera_width << "x" << camera_height << ")" << std::endl;
        perror("VIDIOC_S_FMT");
        cleanup_fd();
        return EXIT_FAILURE;
    }

    // 打印实际设置的分辨率
    std::cout << "USB camera initialized: " << fmt.fmt.pix.width << "x" << fmt.fmt.pix.height << std::endl;

    v4l2_requestbuffers req = {};
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = REQ_COUNT;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        std::cerr << "IOCTL failed: VIDIOC_REQBUFS" << std::endl;
        perror("VIDIOC_REQBUFS");
        cleanup_fd();
        return EXIT_FAILURE;
    }

    // 分配缓冲区数组
    buffers = new Buffer[REQ_COUNT];
    memset(buffers, 0, sizeof(Buffer) * REQ_COUNT);  // 初始化为 0

    unsigned int mapped_count = 0;  // 记录已成功映射的缓冲区数量

    for (unsigned i = 0; i < req.count; ++i) {
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
            std::cerr << "IOCTL failed: VIDIOC_QUERYBUF at index " << i << std::endl;
            perror("VIDIOC_QUERYBUF");
            cleanup_buffers(mapped_count);  // 清理已映射的缓冲区
            cleanup_fd();
            return EXIT_FAILURE;
        }

        // 关键步骤：mmap 实现内核到用户的零拷贝访问
        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length,
                               PROT_READ | PROT_WRITE,
                               MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            std::cerr << "Memory mapping failed at index " << i << std::endl;
            perror("mmap");
            buffers[i].start = nullptr;  // 标记为未映射
            cleanup_buffers(mapped_count);  // 清理已映射的缓冲区
            cleanup_fd();
            return EXIT_FAILURE;
        }
        mapped_count++;

        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            std::cerr << "IOCTL failed: VIDIOC_QBUF at index " << i << std::endl;
            perror("VIDIOC_QBUF");
            cleanup_buffers(mapped_count);  // 清理已映射的缓冲区
            cleanup_fd();
            return EXIT_FAILURE;
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        std::cerr << "IOCTL failed: VIDIOC_STREAMON" << std::endl;
        perror("VIDIOC_STREAMON");
        cleanup_buffers(REQ_COUNT);
        cleanup_fd();
        return EXIT_FAILURE;
    }

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    // 优化：无需预分配双缓冲，采集线程会动态创建帧
    // 确保 current_frame 为空状态
    current_frame.reset();

    camera_opened = true;
    return EXIT_SUCCESS;
}

/**
 * @brief 摄像头捕获线程函数
 * @details 负责从 V4L2 驱动循环读取数据，解码，并更新 shared_ptr 帧指针。
 *          优化：使用 std::shared_ptr<cv::Mat> 替代双缓冲深拷贝，
 *          解码后的帧通过 move 语义转移到 shared_ptr，无数据拷贝。
 */
static void usb_capture_thread_func()
{
    v4l2_buffer thread_buf;
    thread_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    thread_buf.memory = V4L2_MEMORY_MMAP;

    while (capture_running) {
        // 从摄像头读取帧 (出队)
        // 这一步是非阻塞的或短暂阻塞，数据已经在内核缓冲区中准备好
        if (ioctl(fd, VIDIOC_DQBUF, &thread_buf) == -1) {
            if (errno == EAGAIN) {
                continue;
            }
            std::cerr << "VIDIOC_DQBUF failed in capture thread" << std::endl;
            perror("VIDIOC_DQBUF");
            // 错误处理: 暂停一会，避免疯狂循环日志
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!capture_running) break; // 如果已请求停止，立即退出
            continue;
        }

        // 解码MJPEG (CPU 密集型操作)
        // 注意：buffers[i].start 直接指向 mmap 的内存，这里没有发生内核到用户的拷贝
        cv::Mat raw_data(1, thread_buf.bytesused, CV_8UC1, buffers[thread_buf.index].start);
        cv::Mat decoded_frame = cv::imdecode(raw_data, cv::IMREAD_COLOR);

        // 优化：使用 shared_ptr 原子替换，消除深拷贝
        // std::move 将 decoded_frame 的数据所有权转移到 shared_ptr，无内存拷贝
        if (!decoded_frame.empty()) {
            auto new_frame = std::make_shared<cv::Mat>(std::move(decoded_frame));
            {
                std::lock_guard<std::mutex> lock(frame_mutex);
                current_frame = new_frame;  // 原子替换指针，旧帧引用计数减1后自动释放
            }
        }

        // 归还缓冲区 (入队)
        if (ioctl(fd, VIDIOC_QBUF, &thread_buf) == -1) {
            std::cerr << "VIDIOC_QBUF failed in capture thread" << std::endl;
            perror("VIDIOC_QBUF");
        }
    }
}

/**
 * @brief 启动 USB 摄像头采集线程
 * @note 使用 compare_exchange_strong 保证线程安全，防止多次调用创建多个线程
 */
void start_usb_capture_thread()
{
    // 优化：使用原子 CAS 操作，修复 TOCTOU 竞态条件
    bool expected = false;
    if (capture_running.compare_exchange_strong(expected, true)) {
        capture_thread = std::thread(usb_capture_thread_func);
    }
}

/**
 * @brief 停止 USB 摄像头采集线程
 * @note 会尝试停止视频流以唤醒阻塞的 ioctl，并阻塞等待线程结束 (join)
 */
void stop_usb_capture_thread()
{
    // 优化：使用原子 CAS 操作，确保只停止一次
    bool expected = true;
    if (capture_running.compare_exchange_strong(expected, false)) {
        // 尝试停止流，以唤醒可能阻塞在 VIDIOC_DQBUF 的线程
        if (fd >= 0) {
            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (ioctl(fd, VIDIOC_STREAMOFF, &type) == -1) {
                // 忽略错误，仅仅是为了尝试唤醒
            }
        }

        if (capture_thread.joinable()) {
            capture_thread.join();
        }
    }
}

/**
 * @brief 从 shared_ptr 帧指针读取最新的一帧 (零拷贝/低延迟)
 * @param[out] orig_img 输出的 OpenCV Mat 对象
 * @return true 读取成功, false 失败 (缓冲区为空或设备未就绪)
 * @note 优化：使用 cv::Mat 的浅拷贝（引用计数），不复制像素数据。
 *       调用者获得的 Mat 与 shared_ptr 中的 Mat 共享底层数据。
 *       如果调用者需要独立副本，应自行调用 clone()。
 */
bool read_usb_frame(cv::Mat *orig_img)
{
    std::lock_guard<std::mutex> lock(frame_mutex);
    if (current_frame && !current_frame->empty()) {
        // 优化：cv::Mat 赋值是浅拷贝，只增加引用计数，不复制像素数据
        // 调用者与 current_frame 共享底层数据，直到下一帧到来或调用者释放
        *orig_img = *current_frame;
        return true;
    }
    return false; // 缓冲区为空
}

/**
 * @brief 停止采集线程并关闭摄像头设备
 */
void close_usb_camera()
{
    // 先停止异步线程 (内部会尝试 STREAMOFF 以唤醒线程)
    stop_usb_capture_thread();

    if (!camera_opened) {
        std::cerr << "Warning: Camera not opened, skip close" << std::endl;
        return;
    }

    // 再次确保停止视频流 (以防 stop_usb_capture_thread 中因 fd 问题没执行)
    if (fd >= 0) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(fd, VIDIOC_STREAMOFF, &type);
    }

    // 清理 shared_ptr 帧指针
    {
        std::lock_guard<std::mutex> lock(frame_mutex);
        current_frame.reset();
    }

    // 使用辅助函数清理资源
    cleanup_buffers(REQ_COUNT);
    cleanup_fd();
}

/*
 ==============================================================================
 * 附录：数据流与内存拷贝分析 (Data Flow & Memory Copy Analysis) - 优化后版本
 ==============================================================================
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
 *
 * 1. 硬件层 -> 内核层 (Hardware -> Kernel): [0 拷贝]
 *    摄像头传感器 -> USB 总线 -> 内存 (DMA)。
 *    数据直接由 DMA 传输到内核分配的 videobuf2 缓冲区中。
 *
 * 2. 内核层 -> 用户层 (Kernel -> User Space): [0 拷贝]
 *    通过 mmap() 机制，用户空间的 buffers[i].start 指针直接映射到内核缓冲区。
 *
 * 3. MJPEG 解码 (MJPEG Decoding): [生成新数据]
 *    cv::imdecode() 必须在 CPU 上进行解码，生成 BGR cv::Mat。
 *    这是整个流程中 CPU 开销最大的步骤，无法避免。
 *
 * 4. 存储帧 (Store Frame): [0 拷贝] ← 优化后
 *    auto new_frame = std::make_shared<cv::Mat>(std::move(decoded_frame));
 *    current_frame = new_frame;
 *    使用 std::move 将解码后的 Mat 数据所有权转移到 shared_ptr，
 *    然后原子替换 current_frame 指针。无内存拷贝发生。
 *
 * 5. 读取帧 (Read Frame): [0 拷贝] ← 优化后
 *    *orig_img = *current_frame;
 *    cv::Mat 赋值操作是浅拷贝，只增加引用计数，不复制像素数据。
 *    调用者与 shared_ptr 共享底层数据，直到帧被替换或调用者释放。
 *
 * 注意事项：
 * ----------
 * - 调用者获取的 cv::Mat 与 current_frame 共享数据，如需独立副本应调用 clone()
 * - 当新帧到来时，旧帧的 shared_ptr 引用计数减 1，如果调用者仍持有引用则数据不会释放
 ==============================================================================
 */