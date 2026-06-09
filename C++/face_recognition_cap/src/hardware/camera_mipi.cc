/**
 * @file camera_mipi.cc
 * @brief OV13855/MIPI V4L2 NV12 DMA-BUF 零拷贝采集实现
 */

#include "hardware/camera_mipi.h"
#include "hardware/camera_util.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/v4l2-controls.h>
#include <linux/videodev2.h>
#include <mutex>
#include <sstream>
#include <spdlog/spdlog.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace {

struct MipiBuffer {
    struct Plane {
        void* start = nullptr;
        std::size_t length = 0;
        int dmabuf_fd = -1;
    };
    std::vector<Plane> planes;
};

int fd = -1;
bool camera_opened = false;
v4l2_buf_type active_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
std::vector<MipiBuffer> buffers;
std::string current_device_path;

int active_width = 0;
int active_height = 0;
int active_horizontal_stride = 0;
int active_vertical_stride = 0;
uint64_t sequence = 0;

std::atomic<int> capture_frame_count(0);
std::chrono::steady_clock::time_point last_fps_calc_time = std::chrono::steady_clock::now();
std::atomic<double> camera_fps(0.0);

std::atomic<bool> camera_faulted(false);
std::mutex camera_error_mutex;
std::string camera_error_message;

bool is_mplane_type(v4l2_buf_type type) {
    return type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
}

std::string normalize_video_device(const std::string& device) {
    if (device.empty()) {
        return "/dev/video11";
    }
    if (device.rfind("/dev/", 0) == 0) {
        return device;
    }
    return "/dev/video" + device;
}

std::string fourcc_to_string(uint32_t format) {
    std::string value;
    value.push_back(static_cast<char>(format & 0xFF));
    value.push_back(static_cast<char>((format >> 8) & 0xFF));
    value.push_back(static_cast<char>((format >> 16) & 0xFF));
    value.push_back(static_cast<char>((format >> 24) & 0xFF));
    return value;
}

std::string errno_text(const std::string& prefix) {
    std::ostringstream out;
    out << prefix << ": " << std::strerror(errno);
    return out.str();
}

bool xioctl(int device_fd, unsigned long request, void* arg, const std::string& name) {
    for (;;) {
        if (ioctl(device_fd, request, arg) == 0) {
            return true;
        }
        if (errno == EINTR) {
            continue;
        }
        spdlog::error("{}: {}", name, std::strerror(errno));
        return false;
    }
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

void cleanup_buffers() {
    for (auto& buffer : buffers) {
        for (auto& plane : buffer.planes) {
            if (plane.dmabuf_fd >= 0) {
                close(plane.dmabuf_fd);
                plane.dmabuf_fd = -1;
            }
            if (plane.start != nullptr && plane.start != MAP_FAILED) {
                munmap(plane.start, plane.length);
                plane.start = nullptr;
            }
        }
    }
    buffers.clear();
}

void cleanup_fd() {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
    camera_opened = false;
    current_device_path.clear();
    active_width = 0;
    active_height = 0;
    active_horizontal_stride = 0;
    active_vertical_stride = 0;
}

bool queue_buffer(uint32_t index) {
    if (fd < 0 || index >= buffers.size()) {
        return false;
    }

    v4l2_buffer buf{};
    v4l2_plane planes[VIDEO_MAX_PLANES]{};
    buf.type = active_type;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    if (is_mplane_type(active_type)) {
        buf.m.planes = planes;
        buf.length = static_cast<uint32_t>(buffers[index].planes.size());
    }
    if (ioctl(fd, VIDIOC_QBUF, &buf) != 0) {
        spdlog::error("MIPI VIDIOC_QBUF index={} failed: {}", index, std::strerror(errno));
        return false;
    }
    return true;
}

void configure_video_crop(int device_fd,
                          v4l2_buf_type type,
                          const MipiCameraConfig& config,
                          const std::string& device) {
    if (config.crop_left < 0 || config.crop_top < 0 ||
        config.crop_width <= 0 || config.crop_height <= 0) {
        return;
    }

    v4l2_selection selection{};
    selection.type = type;
    selection.target = V4L2_SEL_TGT_CROP;
    selection.r.left = config.crop_left;
    selection.r.top = config.crop_top;
    selection.r.width = config.crop_width;
    selection.r.height = config.crop_height;
    if (ioctl(device_fd, VIDIOC_S_SELECTION, &selection) != 0) {
        spdlog::warn("MIPI VIDIOC_S_SELECTION crop {} failed: {}", device, std::strerror(errno));
        return;
    }

    spdlog::info("MIPI crop applied: left={} top={} size={}x{}",
                 selection.r.left, selection.r.top, selection.r.width, selection.r.height);
}

void configure_sensor_controls(const MipiCameraConfig& config) {
    if (config.sensor_subdev.empty()) {
        return;
    }

    const int sensor_fd = open(config.sensor_subdev.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (sensor_fd < 0) {
        spdlog::warn("Failed to open MIPI sensor subdevice {}: {}",
                     config.sensor_subdev, std::strerror(errno));
        return;
    }

    const auto set_control = [sensor_fd, &config](uint32_t id, int value, const char* name) {
        if (value < 0) {
            return true;
        }
        v4l2_control control{};
        control.id = id;
        control.value = value;
        if (ioctl(sensor_fd, VIDIOC_S_CTRL, &control) == 0) {
            return true;
        }
        spdlog::warn("Failed to set {}={} on {}: {}",
                     name, value, config.sensor_subdev, std::strerror(errno));
        return false;
    };

    const bool exposure_ok =
        config.sensor_exposure <= 0 ||
        set_control(V4L2_CID_EXPOSURE, config.sensor_exposure, "exposure");
    const bool vblank_ok =
        set_control(V4L2_CID_VBLANK, config.sensor_vblank, "vblank");
    const bool gain_ok =
        set_control(V4L2_CID_ANALOGUE_GAIN, config.sensor_analogue_gain, "analogue_gain");

    if (exposure_ok && vblank_ok && gain_ok) {
        spdlog::info("MIPI sensor controls applied: exposure={} vblank={} analogue_gain={}",
                     config.sensor_exposure, config.sensor_vblank, config.sensor_analogue_gain);
    }
    close(sensor_fd);
}

int calculate_vertical_stride(int height, int horizontal_stride, std::size_t sizeimage) {
    if (horizontal_stride <= 0 || sizeimage == 0) {
        return height;
    }
    const int derived = static_cast<int>((sizeimage * 2) /
                                         (static_cast<std::size_t>(horizontal_stride) * 3));
    return std::max(height, derived);
}

void update_fps_counter() {
    capture_frame_count++;
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fps_calc_time);
    if (duration.count() >= 1000) {
        camera_fps = capture_frame_count * 1000.0 / duration.count();
        capture_frame_count = 0;
        last_fps_calc_time = now;
    }
}

} // namespace

MipiCameraConfig make_default_ov13855_config(const std::string& device,
                                             int width,
                                             int height)
{
    MipiCameraConfig config;
    config.device = device.empty() ? "/dev/video11" : device;
    config.width = width;
    config.height = height;
    config.fps = 30;
    config.sensor_subdev = "/dev/v4l-subdev2";
    config.crop_left = 0;
    config.crop_top = 380;
    config.crop_width = 4224;
    config.crop_height = 2376;
    config.sensor_exposure = 1928;
    config.sensor_vblank = 78;
    config.sensor_analogue_gain = 1536;
    return config;
}

int load_mipi_camera(const MipiCameraConfig& config)
{
    if (camera_opened) {
        spdlog::warn("MIPI camera already opened, close it first");
        return EXIT_FAILURE;
    }

    clear_camera_error_state();
    current_device_path = normalize_video_device(config.device);

    fd = open(current_device_path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        spdlog::error("Failed to open MIPI camera {}: {}", current_device_path, std::strerror(errno));
        return EXIT_FAILURE;
    }

    v4l2_capability cap{};
    if (!xioctl(fd, VIDIOC_QUERYCAP, &cap, "MIPI VIDIOC_QUERYCAP " + current_device_path)) {
        cleanup_fd();
        return EXIT_FAILURE;
    }

    const uint32_t caps = cap.device_caps != 0 ? cap.device_caps : cap.capabilities;
    if ((caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0) {
        active_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    } else if ((caps & V4L2_CAP_VIDEO_CAPTURE) != 0) {
        active_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    } else {
        spdlog::error("MIPI camera {} does not support capture streaming", current_device_path);
        cleanup_fd();
        return EXIT_FAILURE;
    }
    if ((caps & V4L2_CAP_STREAMING) == 0) {
        spdlog::error("MIPI camera {} does not support V4L2 streaming", current_device_path);
        cleanup_fd();
        return EXIT_FAILURE;
    }

    v4l2_format fmt{};
    fmt.type = active_type;
    if (is_mplane_type(active_type)) {
        fmt.fmt.pix_mp.width = static_cast<uint32_t>(config.width);
        fmt.fmt.pix_mp.height = static_cast<uint32_t>(config.height);
        fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    } else {
        fmt.fmt.pix.width = static_cast<uint32_t>(config.width);
        fmt.fmt.pix.height = static_cast<uint32_t>(config.height);
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
    }
    if (!xioctl(fd, VIDIOC_S_FMT, &fmt, "MIPI VIDIOC_S_FMT " + current_device_path)) {
        cleanup_fd();
        return EXIT_FAILURE;
    }

    const uint32_t active_fourcc =
        is_mplane_type(active_type) ? fmt.fmt.pix_mp.pixelformat : fmt.fmt.pix.pixelformat;
    if (active_fourcc != V4L2_PIX_FMT_NV12) {
        spdlog::error("MIPI camera negotiated {} instead of requested NV12",
                      fourcc_to_string(active_fourcc));
        cleanup_fd();
        return EXIT_FAILURE;
    }

    configure_video_crop(fd, active_type, config, current_device_path);

    v4l2_streamparm parm{};
    parm.type = active_type;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = static_cast<uint32_t>(config.fps);
    if (ioctl(fd, VIDIOC_S_PARM, &parm) != 0) {
        spdlog::warn("MIPI VIDIOC_S_PARM {} failed: {}", current_device_path, std::strerror(errno));
    }

    v4l2_control ctrl{};
    ctrl.id = V4L2_CID_EXPOSURE_AUTO_PRIORITY;
    ctrl.value = 0;
    if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) == 0) {
        spdlog::info("Disabled MIPI V4L2_CID_EXPOSURE_AUTO_PRIORITY");
    }

    configure_sensor_controls(config);

    v4l2_requestbuffers req{};
    req.count = CAMERA_REQ_COUNT;
    req.type = active_type;
    req.memory = V4L2_MEMORY_MMAP;
    if (!xioctl(fd, VIDIOC_REQBUFS, &req, "MIPI VIDIOC_REQBUFS " + current_device_path) ||
        req.count < 2) {
        spdlog::error("MIPI camera returned insufficient V4L2 buffers: {}", req.count);
        cleanup_fd();
        return EXIT_FAILURE;
    }

    buffers.resize(req.count);
    bool map_ok = true;
    for (uint32_t i = 0; i < req.count; ++i) {
        v4l2_buffer query{};
        v4l2_plane planes[VIDEO_MAX_PLANES]{};
        query.type = active_type;
        query.memory = V4L2_MEMORY_MMAP;
        query.index = i;
        if (is_mplane_type(active_type)) {
            query.m.planes = planes;
            query.length = VIDEO_MAX_PLANES;
        }

        if (!xioctl(fd, VIDIOC_QUERYBUF, &query, "MIPI VIDIOC_QUERYBUF " + current_device_path)) {
            map_ok = false;
            break;
        }

        const uint32_t plane_count = is_mplane_type(active_type) ? query.length : 1U;
        if (plane_count != 1U) {
            spdlog::error("MIPI NV12 DMA-BUF path requires single-plane buffers, got {} planes",
                          plane_count);
            map_ok = false;
            break;
        }

        buffers[i].planes.resize(plane_count);
        for (uint32_t p = 0; p < plane_count; ++p) {
            const std::size_t length = is_mplane_type(active_type) ? planes[p].length : query.length;
            const off_t offset = static_cast<off_t>(
                is_mplane_type(active_type) ? planes[p].m.mem_offset : query.m.offset);
            auto& plane = buffers[i].planes[p];
            plane.length = length;
            plane.start = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
            if (plane.start == MAP_FAILED) {
                spdlog::error("MIPI mmap buffer failed: {}", std::strerror(errno));
                map_ok = false;
                break;
            }

            v4l2_exportbuffer expbuf{};
            expbuf.type = active_type;
            expbuf.index = i;
            expbuf.plane = p;
            expbuf.flags = O_CLOEXEC;
            if (ioctl(fd, VIDIOC_EXPBUF, &expbuf) != 0) {
                spdlog::error("MIPI VIDIOC_EXPBUF failed for buffer={} plane={}: {}",
                              i, p, std::strerror(errno));
                map_ok = false;
                break;
            }
            plane.dmabuf_fd = expbuf.fd;
        }
        if (!map_ok) {
            break;
        }
    }

    if (!map_ok) {
        cleanup_buffers();
        cleanup_fd();
        return EXIT_FAILURE;
    }

    for (uint32_t i = 0; i < buffers.size(); ++i) {
        if (!queue_buffer(i)) {
            cleanup_buffers();
            cleanup_fd();
            return EXIT_FAILURE;
        }
    }

    if (!xioctl(fd, VIDIOC_STREAMON, &active_type, "MIPI VIDIOC_STREAMON " + current_device_path)) {
        cleanup_buffers();
        cleanup_fd();
        return EXIT_FAILURE;
    }

    // Re-apply sensor controls right after stream starts to override any default controls
    // set during the hardware stream-on sequence or initial 3A server handshake.
    configure_sensor_controls(config);

    active_width = static_cast<int>(
        is_mplane_type(active_type) ? fmt.fmt.pix_mp.width : fmt.fmt.pix.width);
    active_height = static_cast<int>(
        is_mplane_type(active_type) ? fmt.fmt.pix_mp.height : fmt.fmt.pix.height);
    active_horizontal_stride = static_cast<int>(
        is_mplane_type(active_type) ? fmt.fmt.pix_mp.plane_fmt[0].bytesperline
                                    : fmt.fmt.pix.bytesperline);
    const std::size_t sizeimage =
        is_mplane_type(active_type) ? fmt.fmt.pix_mp.plane_fmt[0].sizeimage
                                    : fmt.fmt.pix.sizeimage;
    active_vertical_stride = calculate_vertical_stride(active_height,
                                                       active_horizontal_stride,
                                                       sizeimage);
    sequence = 0;
    last_fps_calc_time = std::chrono::steady_clock::now();
    capture_frame_count = 0;
    camera_opened = true;

    spdlog::info("MIPI OV13855 camera initialized: {} type={} {}x{} NV12 stride={} vstride={} fps={} sensor_subdev={}",
                 current_device_path,
                 is_mplane_type(active_type) ? "mplane" : "single",
                 active_width, active_height,
                 active_horizontal_stride, active_vertical_stride,
                 config.fps, config.sensor_subdev);

    return EXIT_SUCCESS;
}

bool read_mipi_dma_frame(MipiDmaFrame* frame)
{
    if (!camera_opened || fd < 0 || frame == nullptr) {
        return false;
    }

    *frame = MipiDmaFrame{};

    v4l2_buffer buf{};
    v4l2_plane planes[VIDEO_MAX_PLANES]{};
    buf.type = active_type;
    buf.memory = V4L2_MEMORY_MMAP;
    if (is_mplane_type(active_type)) {
        buf.m.planes = planes;
        buf.length = VIDEO_MAX_PLANES;
    }

    if (ioctl(fd, VIDIOC_DQBUF, &buf) != 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return false;
        }
        const std::string message = errno_text("MIPI VIDIOC_DQBUF " + current_device_path);
        set_camera_error_state(message);
        spdlog::error("{}", message);
        return false;
    }

    if (buf.index >= buffers.size() || buffers[buf.index].planes.empty()) {
        const std::string message = "MIPI driver returned invalid buffer index";
        set_camera_error_state(message);
        spdlog::error("{}", message);
        return false;
    }

    const uint32_t plane_count = is_mplane_type(active_type) ? buf.length : 1U;
    if (plane_count != 1U) {
        spdlog::warn("Skipping MIPI frame with unsupported plane count={}", plane_count);
        queue_buffer(buf.index);
        return false;
    }

    const auto& mapped = buffers[buf.index].planes[0];
    const std::size_t data_offset = is_mplane_type(active_type) ? planes[0].data_offset : 0U;
    const std::size_t bytesused = is_mplane_type(active_type) ? planes[0].bytesused : buf.bytesused;
    if (data_offset != 0U) {
        spdlog::warn("Skipping MIPI frame with non-zero data_offset={} unsupported by RGA wrapbuffer_fd",
                     data_offset);
        queue_buffer(buf.index);
        return false;
    }
    if (data_offset > mapped.length || bytesused <= data_offset || mapped.dmabuf_fd < 0) {
        spdlog::warn("Skipping invalid MIPI DMA frame index={} bytesused={} offset={} fd={}",
                     buf.index, bytesused, data_offset, mapped.dmabuf_fd);
        queue_buffer(buf.index);
        return false;
    }

    frame->dma_fd = mapped.dmabuf_fd;
    frame->dma_offset = data_offset;
    frame->dma_bytesused = std::min(bytesused, mapped.length) - data_offset;
    frame->dma_length = mapped.length;
    frame->mapped_data = static_cast<uint8_t*>(mapped.start) + data_offset;
    frame->mapped_length = mapped.length - data_offset;
    frame->width = active_width;
    frame->height = active_height;
    frame->horizontal_stride = active_horizontal_stride;
    frame->vertical_stride = active_vertical_stride;
    frame->buffer_index = buf.index;
    frame->sequence = sequence++;

    update_fps_counter();
    return true;
}

void release_mipi_dma_frame(uint32_t buffer_index)
{
    queue_buffer(buffer_index);
}

double get_mipi_camera_fps()
{
    return camera_fps.load(std::memory_order_acquire);
}

bool has_mipi_camera_error()
{
    return camera_faulted.load(std::memory_order_acquire);
}

std::string get_mipi_camera_error()
{
    std::lock_guard<std::mutex> lock(camera_error_mutex);
    return camera_error_message;
}

void close_mipi_camera()
{
    if (!camera_opened) {
        return;
    }

    if (fd >= 0) {
        ioctl(fd, VIDIOC_STREAMOFF, &active_type);
    }

    camera_fps.store(0.0, std::memory_order_release);
    cleanup_buffers();
    cleanup_fd();
}
