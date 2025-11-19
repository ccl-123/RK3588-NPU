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

#define CHECK_IOCTL(fd, request, arg) \
    if (ioctl(fd, request, arg) == -1) { \
        std::cerr << "IOCTL failed: " #request << std::endl; \
        exit(EXIT_FAILURE); \
    }


int fd;
v4l2_buffer buf;
Buffer *buffers = new Buffer[REQ_COUNT];
v4l2_format fmt = {};
int width, height;

// 异步读取相关变量
static std::thread capture_thread;
static std::atomic<bool> capture_running(false);
static std::mutex frame_mutex;
static cv::Mat double_buffer[2];  // 双缓冲
static int write_idx = 0;
static int read_idx = 1;

int load_usb_camera(std::string device, int camera_width, int camera_height)
{
	std::string prefix = "/dev/video";
    fd = open((prefix + device).c_str(), O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return EXIT_FAILURE;
    }

    v4l2_capability cap;
    CHECK_IOCTL(fd, VIDIOC_QUERYCAP, &cap);
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        std::cerr << "Device does not support video capture" << std::endl;
        close(fd);
        return EXIT_FAILURE;
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = camera_width;
    fmt.fmt.pix.height = camera_height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    CHECK_IOCTL(fd, VIDIOC_S_FMT, &fmt);

    v4l2_requestbuffers req = {};
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = REQ_COUNT;
    CHECK_IOCTL(fd, VIDIOC_REQBUFS, &req);

    for (unsigned i = 0; i < req.count; ++i) {
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        CHECK_IOCTL(fd, VIDIOC_QUERYBUF, &buf);

        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length,
                               PROT_READ | PROT_WRITE,
                               MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("Memory mapping failed");
            exit(EXIT_FAILURE);
        }
        CHECK_IOCTL(fd, VIDIOC_QBUF, &buf);
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    CHECK_IOCTL(fd, VIDIOC_STREAMON, &type);

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;

    return EXIT_SUCCESS;
}

void read_usb_frame(cv::Mat *orig_img)
{
    CHECK_IOCTL(fd, VIDIOC_DQBUF, &buf);
    cv::Mat raw_data(1, buf.bytesused, CV_8UC1, buffers[buf.index].start);
    *orig_img = cv::imdecode(raw_data, cv::IMREAD_COLOR);
    CHECK_IOCTL(fd, VIDIOC_QBUF, &buf);
}

void close_usb_camera()
{
	v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    CHECK_IOCTL(fd, VIDIOC_STREAMOFF, &type);
    for (unsigned i = 0; i < REQ_COUNT; ++i) {
        munmap(buffers[i].start, buffers[i].length);
    }
    delete[] buffers;
    close(fd);
}

int load_mipi_camera(std::string device, int camera_width, int camera_height)
{
	std::string prefix = "/dev/video";
    fd = open((prefix + device).c_str(), O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return EXIT_FAILURE;
    }

    v4l2_capability cap;
    CHECK_IOCTL(fd, VIDIOC_QUERYCAP, &cap);
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
        std::cerr << "Device does not support video capture" << std::endl;
        close(fd);
        return EXIT_FAILURE;
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = camera_width;
    fmt.fmt.pix_mp.height = camera_height;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes = 2;
    CHECK_IOCTL(fd, VIDIOC_S_FMT, &fmt);
    
    width = fmt.fmt.pix_mp.width;
    height = fmt.fmt.pix_mp.height;

    v4l2_requestbuffers req = {};
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = REQ_COUNT;
    CHECK_IOCTL(fd, VIDIOC_REQBUFS, &req);

    for (unsigned i = 0; i < req.count; ++i) {
        v4l2_plane planes[VIDEO_MAX_PLANES];
        
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = planes;
        buf.length = VIDEO_MAX_PLANES;
        CHECK_IOCTL(fd, VIDIOC_QUERYBUF, &buf);

        buffers[i].length = buf.m.planes[0].length;
        buffers[i].start = mmap(NULL, buf.m.planes[0].length,
                               PROT_READ | PROT_WRITE,
                               MAP_SHARED, fd, buf.m.planes[0].m.mem_offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("Memory mapping failed");
            exit(EXIT_FAILURE);
        }
        CHECK_IOCTL(fd, VIDIOC_QBUF, &buf);
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    CHECK_IOCTL(fd, VIDIOC_STREAMON, &type);

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;

    return EXIT_SUCCESS;
}

void read_mipi_frame(cv::Mat *orig_img)
{
    CHECK_IOCTL(fd, VIDIOC_DQBUF, &buf);
    cv::Mat raw_data(height * 3 / 2, width, CV_8UC1, buffers[buf.index].start);
    
    cv::cvtColor(raw_data, *orig_img, cv::COLOR_YUV2BGR_NV12);
    CHECK_IOCTL(fd, VIDIOC_QBUF, &buf);
}


void close_mipi_camera()
{
	v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    CHECK_IOCTL(fd, VIDIOC_STREAMOFF, &type);
    for (unsigned i = 0; i < REQ_COUNT; ++i) {
        munmap(buffers[i].start, buffers[i].length);
    }
    delete[] buffers;
    close(fd);
}

// ==================== USB摄像头异步读取实现 ====================
// 优化: 使用独立线程异步读取摄像头数据,避免主线程阻塞

// 摄像头捕获线程函数
static void usb_capture_thread_func()
{
    v4l2_buffer thread_buf;
    thread_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    thread_buf.memory = V4L2_MEMORY_MMAP;

    while (capture_running) {
        // 从摄像头读取帧
        if (ioctl(fd, VIDIOC_DQBUF, &thread_buf) == -1) {
            if (errno == EAGAIN) {
                continue;
            }
            std::cerr << "VIDIOC_DQBUF failed in capture thread" << std::endl;
            break;
        }

        // 解码MJPEG
        cv::Mat raw_data(1, thread_buf.bytesused, CV_8UC1, buffers[thread_buf.index].start);
        cv::Mat decoded_frame = cv::imdecode(raw_data, cv::IMREAD_COLOR);

        // 写入双缓冲
        {
            std::lock_guard<std::mutex> lock(frame_mutex);
            decoded_frame.copyTo(double_buffer[write_idx]);
            // 交换读写索引
            std::swap(write_idx, read_idx);
        }

        // 归还缓冲区
        if (ioctl(fd, VIDIOC_QBUF, &thread_buf) == -1) {
            std::cerr << "VIDIOC_QBUF failed in capture thread" << std::endl;
            break;
        }
    }
}

// 加载USB摄像头(异步版本)
int load_usb_camera_async(std::string device, int camera_width, int camera_height)
{
    // 调用原始的load_usb_camera
    int ret = load_usb_camera(device, camera_width, camera_height);
    if (ret != EXIT_SUCCESS) {
        return ret;
    }

    // 初始化双缓冲
    double_buffer[0] = cv::Mat(camera_height, camera_width, CV_8UC3);
    double_buffer[1] = cv::Mat(camera_height, camera_width, CV_8UC3);

    return EXIT_SUCCESS;
}

// 启动捕获线程
void start_usb_capture_thread()
{
    if (!capture_running) {
        capture_running = true;
        capture_thread = std::thread(usb_capture_thread_func);
    }
}

// 停止捕获线程
void stop_usb_capture_thread()
{
    if (capture_running) {
        capture_running = false;
        if (capture_thread.joinable()) {
            capture_thread.join();
        }
    }
}

// 读取帧(异步版本) - 从双缓冲读取
void read_usb_frame_async(cv::Mat *orig_img)
{
    std::lock_guard<std::mutex> lock(frame_mutex);
    if (!double_buffer[read_idx].empty()) {
        double_buffer[read_idx].copyTo(*orig_img);
    }
}

// 关闭USB摄像头(异步版本)
void close_usb_camera_async()
{
    stop_usb_capture_thread();
    close_usb_camera();
}
