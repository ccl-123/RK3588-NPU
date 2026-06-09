/**
 * @file camera_usb.cc
 * @brief USB 摄像头底层控制与 MJPEG 零拷贝包采集
 */

#include "hardware/camera_usb.h"
#include "hardware/camera_util.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

struct UsbBuffer {
    void* start = nullptr;
    size_t length = 0;
};

int fd = -1;
v4l2_buffer buf = {};
UsbBuffer* buffers = nullptr;
unsigned int buffer_count = 0;
v4l2_format v4l2_fmt = {};
bool camera_opened = false;

std::atomic<int> capture_frame_count(0);
std::chrono::steady_clock::time_point last_fps_calc_time = std::chrono::steady_clock::now();
std::atomic<double> camera_fps(0.0);

std::atomic<bool> camera_faulted(false);
std::mutex camera_error_mutex;
std::string camera_error_message;
std::string current_device_path;

std::string normalize_video_device(const std::string& device) {
    if (device.rfind("/dev/", 0) == 0) {
        return device;
    }
    return "/dev/video" + device;
}

void cleanup_buffers(unsigned int count) {
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
    buffer_count = 0;
}

void cleanup_fd() {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
    camera_opened = false;
    current_device_path.clear();
}

void clear_camera_error_state() {
    camera_faulted.store(false, std::memory_order_release);
    camera_fps.store(0.0, std::memory_order_release);
    std::lock_guard<std::mutex> lock(camera_error_mutex);
    camera_error_message.clear();
}

void set_camera_error_state(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(camera_error_mutex);
        camera_error_message = message;
    }
    camera_faulted.store(true, std::memory_order_release);
    camera_fps.store(0.0, std::memory_order_release);
}

} // namespace

int load_usb_camera(std::string device, int camera_width, int camera_height)
{
    if (camera_opened) {
        spdlog::warn("USB camera already opened, close it first");
        return EXIT_FAILURE;
    }

    std::string device_path = normalize_video_device(device);
    current_device_path = device_path;
    clear_camera_error_state();

    fd = open(device_path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror(("Failed to open " + device_path).c_str());
        return EXIT_FAILURE;
    }

    v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
        perror("VIDIOC_QUERYCAP");
        cleanup_fd();
        return EXIT_FAILURE;
    }

    const uint32_t caps = cap.device_caps != 0 ? cap.device_caps : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) {
        spdlog::error("USB camera {} does not support streaming video capture", device_path);
        cleanup_fd();
        return EXIT_FAILURE;
    }

    v4l2_fmt = {};
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

    spdlog::info("USB camera initialized: {} {}x{} MJPEG",
                 device_path, v4l2_fmt.fmt.pix.width, v4l2_fmt.fmt.pix.height);

    v4l2_streamparm parm = {};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = 30;
    if (ioctl(fd, VIDIOC_S_PARM, &parm) == -1) {
        perror("VIDIOC_S_PARM (set frame rate)");
    } else {
        spdlog::info("USB camera frame rate set to: {}/{} FPS",
                     parm.parm.capture.timeperframe.denominator,
                     parm.parm.capture.timeperframe.numerator);
    }

    v4l2_control ctrl;
    ctrl.id = V4L2_CID_EXPOSURE_AUTO_PRIORITY;
    ctrl.value = 0;
    if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) == 0) {
        spdlog::info("Disabled USB V4L2_CID_EXPOSURE_AUTO_PRIORITY");
    }

    ctrl.id = V4L2_CID_POWER_LINE_FREQUENCY;
    ctrl.value = 0;
    if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) == 0) {
        spdlog::info("Disabled USB V4L2_CID_POWER_LINE_FREQUENCY");
    }

    v4l2_requestbuffers req = {};
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = CAMERA_REQ_COUNT;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        perror("VIDIOC_REQBUFS");
        cleanup_fd();
        return EXIT_FAILURE;
    }
    if (req.count < 2) {
        spdlog::error("USB camera returned insufficient V4L2 buffers: {}", req.count);
        cleanup_fd();
        return EXIT_FAILURE;
    }

    buffer_count = req.count;
    buffers = new UsbBuffer[buffer_count];

    unsigned int mapped_count = 0;
    for (unsigned i = 0; i < req.count; ++i) {
        buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
            perror("VIDIOC_QUERYBUF");
            cleanup_buffers(mapped_count);
            cleanup_fd();
            return EXIT_FAILURE;
        }

        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                                fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("mmap failed");
            cleanup_buffers(mapped_count);
            cleanup_fd();
            return EXIT_FAILURE;
        }
        mapped_count++;

        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            perror("VIDIOC_QBUF");
            cleanup_buffers(mapped_count);
            cleanup_fd();
            return EXIT_FAILURE;
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        perror("VIDIOC_STREAMON");
        cleanup_buffers(mapped_count);
        cleanup_fd();
        return EXIT_FAILURE;
    }

    camera_opened = true;
    last_fps_calc_time = std::chrono::steady_clock::now();
    capture_frame_count = 0;
    return EXIT_SUCCESS;
}

bool read_usb_raw_packet(void** packet_data, uint32_t* packet_size, uint32_t* buffer_index)
{
    if (!camera_opened || fd < 0 || buffers == nullptr ||
        packet_data == nullptr || packet_size == nullptr || buffer_index == nullptr) {
        return false;
    }

    v4l2_buffer thread_buf;
    memset(&thread_buf, 0, sizeof(thread_buf));
    thread_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    thread_buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_DQBUF, &thread_buf) == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINVAL) {
            return false;
        }

        const std::string error_message =
            "USB camera " + current_device_path + " disconnected or became unavailable during DQBUF: " +
            std::strerror(errno);
        set_camera_error_state(error_message);
        spdlog::error("{}", error_message);
        return false;
    }

    if (thread_buf.index >= buffer_count || buffers[thread_buf.index].start == nullptr) {
        spdlog::error("V4L2 returned an invalid USB buffer index={}", thread_buf.index);
        return false;
    }

    if (thread_buf.bytesused == 0) {
        spdlog::warn("USB V4L2 returned an empty frame.");
        ioctl(fd, VIDIOC_QBUF, &thread_buf);
        return false;
    }

    *packet_data = buffers[thread_buf.index].start;
    *packet_size = thread_buf.bytesused;
    *buffer_index = thread_buf.index;

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

void release_usb_raw_packet(uint32_t buffer_index)
{
    if (fd < 0 || buffer_index >= buffer_count) {
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

double get_usb_camera_fps()
{
    return camera_fps.load(std::memory_order_acquire);
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

void close_usb_camera()
{
    if (!camera_opened) {
        return;
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (fd >= 0) {
        ioctl(fd, VIDIOC_STREAMOFF, &type);
    }

    camera_fps.store(0.0, std::memory_order_release);
    cleanup_buffers(buffer_count);
    cleanup_fd();
}
