/**
 * @file camera_util.cc
 * @brief 摄像头控制工具实现
 * @details 基于 V4L2 的 USB摄像头控制实现，
 *          默认使用异步采集模式 (独立线程 + 双缓冲) 以提高性能。
 *          使用 mmap 实现内核到用户空间的零拷贝采集。
 * @author CL
 * @date 2025-11-20
 */

#include <string.h>
#include <iostream>
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

// 保护双缓冲读写的互斥锁
static std::mutex frame_mutex;

// 双缓冲：用于隔离采集线程(写)和处理线程(读)
// double_buffer[0] 和 double_buffer[1] 交替使用
static cv::Mat double_buffer[2];

// 当前写入缓冲区的索引 (0 或 1)
static int write_idx = 0;

// 当前读取缓冲区的索引 (1 或 0)
static int read_idx = 1;

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

    // 初始化双缓冲 (异步模式必备，分配用户空间内存)
    double_buffer[0] = cv::Mat(camera_height, camera_width, CV_8UC3);
    double_buffer[1] = cv::Mat(camera_height, camera_width, CV_8UC3);

    camera_opened = true;
    return EXIT_SUCCESS;
}

/**
 * @brief 摄像头捕获线程函数
 * @details 负责从 V4L2 驱动循环读取数据，解码，并写入双缓冲。
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
            continue;
        }

        // 解码MJPEG (CPU 密集型操作)
        // 注意：buffers[i].start 直接指向 mmap 的内存，这里没有发生内核到用户的拷贝
        cv::Mat raw_data(1, thread_buf.bytesused, CV_8UC1, buffers[thread_buf.index].start);
        cv::Mat decoded_frame = cv::imdecode(raw_data, cv::IMREAD_COLOR);

        // 写入双缓冲 (内存拷贝)
        // 使用锁保护，将解码后的帧深拷贝到双缓冲
        if (!decoded_frame.empty()) {
            std::lock_guard<std::mutex> lock(frame_mutex);
            decoded_frame.copyTo(double_buffer[write_idx]);
            // 交换读写索引
            std::swap(write_idx, read_idx);
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
 */
void start_usb_capture_thread()
{
    if (!capture_running) {
        capture_running = true;
        capture_thread = std::thread(usb_capture_thread_func);
    }
}

/**
 * @brief 停止 USB 摄像头采集线程
 * @note 会阻塞等待线程结束 (join)
 */
void stop_usb_capture_thread()
{
    if (capture_running) {
        capture_running = false;
        if (capture_thread.joinable()) {
            capture_thread.join();
        }
    }
}

/**
 * @brief 从双缓冲中读取最新的一帧 (非阻塞/低延迟)
 * @param[out] orig_img 输出的 OpenCV Mat 对象
 * @return true 读取成功, false 失败 (缓冲区为空或设备未就绪)
 * @note 此函数从双缓冲的"读"缓冲区拷贝数据，不会阻塞采集线程的写入(仅在 swap 时短暂锁住)。
 */
bool read_usb_frame(cv::Mat *orig_img)
{
    std::lock_guard<std::mutex> lock(frame_mutex);
    if (!double_buffer[read_idx].empty()) {
        double_buffer[read_idx].copyTo(*orig_img);
        return true;
    }
    return false; // 缓冲区为空
}

/**
 * @brief 停止采集线程并关闭摄像头设备
 */
void close_usb_camera()
{
    // 先停止异步线程
    stop_usb_capture_thread();

    if (!camera_opened) {
        std::cerr << "Warning: Camera not opened, skip close" << std::endl;
        return;
    }

    // 停止视频流
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (fd >= 0 && ioctl(fd, VIDIOC_STREAMOFF, &type) == -1) {
        std::cerr << "Warning: VIDIOC_STREAMOFF failed" << std::endl;
        perror("VIDIOC_STREAMOFF");
        // 继续清理，不要退出
    }

    // 使用辅助函数清理资源
    cleanup_buffers(REQ_COUNT);
    cleanup_fd();
}

/*
 ==============================================================================
 * 附录：数据流与内存拷贝分析 (Data Flow & Memory Copy Analysis)
 ==============================================================================
 * ┌─────────────────┬────────────┬─────────────┬────────────────────────────┐
  │ 阶段            │ 动作       │ 方式        │ 是否拷贝/耗时              │
  ├─────────────────┼────────────┼─────────────┼────────────────────────────┤
  │ Cam -> Kernel   │ 采集       │ DMA         │ 0 拷贝 (硬件完成)          │
  │ Kernel -> User  │ 获取数据   │ mmap        │ 0 拷贝 (指针映射)          │
  │ User            │ MJPEG 解码 │ CPU 解码    │ 耗时操作 (生成新数据)      │
  │ User            │ 写入双缓冲 │ copyTo      │ 1 次深拷贝 (架构解耦)      │
  │ User            │ 读出双缓冲 │ copyTo      │ 1 次深拷贝 (API 设计)      │
  │ User            │ RGA 预处理 │ RGA 硬件    │ 硬件搬运 (极快)            │
  │ User -> Display │ Qt 显示    │ QImage 转换 │ 1-2 次拷贝 (格式转换+渲染) │
  └─────────────────┴────────────┴─────────────┴────────────────────────────┘
 * 1. 硬件层 -> 内核层 (Hardware -> Kernel): [0 拷贝]
 *    摄像头传感器 -> USB 总线 -> 内存 (DMA)。
 *    数据直接由 DMA 传输到内核分配的 videobuf2 缓冲区中。
 *
 * 2. 内核层 -> 用户层 (Kernel -> User Space): [0 拷贝]
 *    通过 mmap() 机制，用户空间的 buffers[i].start 指针直接映射到内核缓冲区。
 *    usb_capture_thread_func 访问 buffers[i].start 时，无需数据拷贝。
 *
 * 3. MJPEG 解码 (MJPEG Decoding): [解码/生成新数据]
 *    cv::imdecode(raw_data, ...)
 *    USB 摄像头通常输出压缩的 MJPEG 格式。这一步必须在 CPU 上进行解码。
 *    解码后的 BGR 数据被写入新分配的 cv::Mat 内存 (decoded_frame)。
 *    这是整个流程中 CPU 开销最大的步骤。
 *
 * 4. 写入双缓冲 (Write to Double Buffer): [1 次深拷贝]
 *    decoded_frame.copyTo(double_buffer[write_idx])
 *    为了解耦采集线程和主处理线程，使用了双缓冲机制。
 *    这里发生了一次全帧内存拷贝。
 *
 * 5. 读取双缓冲 (Read from Double Buffer): [1 次深拷贝]
 *    double_buffer[read_idx].copyTo(*orig_img)
 *    FaceRecognitionApp/PreprocessingThread 从双缓冲读取数据。
 *    为了保证数据完整性和线程安全，再次发生一次深拷贝。
 *
 * 总结：虽然 V4L2 接口本身实现了零拷贝，但为了处理压缩视频流(MJPEG)和
 * 实现稳健的异步多线程架构，应用层发生了解码和两次内存拷贝。
*优化建议（针对极致性能）：
    1. 减少双缓冲拷贝：可以改造 double_buffer 机制，使用 std::shared_ptr<cv::Mat> 或直接交换 cv::Mat 对象（cv::Mat 的赋值是浅拷贝，只增加引用计数），而不是调用 copyTo（深拷贝）。这样可以消除第 4 和第 5
        步的拷贝开销。
 ==============================================================================
 */