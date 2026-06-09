/**
 * @file camera_util.cc
 * @brief 摄像头上层共用选择与帧派发接口
 */

#include "hardware/camera_util.h"

#include <cstdlib>
#include <mutex>
#include <spdlog/spdlog.h>

namespace {

std::mutex active_camera_mutex;
CameraBackend active_backend = CameraBackend::kNone;

MipiCameraConfig to_mipi_config(const CameraConfig& config) {
    MipiCameraConfig mipi;
    mipi.device = config.device.empty() ? "/dev/video11" : config.device;
    mipi.width = config.width;
    mipi.height = config.height;
    mipi.fps = config.fps;
    mipi.sensor_subdev = config.sensor_subdev;
    mipi.crop_left = config.crop_left;
    mipi.crop_top = config.crop_top;
    mipi.crop_width = config.crop_width;
    mipi.crop_height = config.crop_height;
    mipi.sensor_exposure = config.sensor_exposure;
    mipi.sensor_vblank = config.sensor_vblank;
    mipi.sensor_analogue_gain = config.sensor_analogue_gain;
    return mipi;
}

} // namespace

CameraConfig make_usb_camera_config(const std::string& device,
                                    int width,
                                    int height)
{
    CameraConfig config;
    config.camera_type = "usb";
    config.device = device;
    config.width = width;
    config.height = height;
    config.fps = 30;
    return config;
}

CameraConfig make_mipi_ov13855_camera_config(const std::string& device,
                                             int width,
                                             int height)
{
    CameraConfig config;
    config.camera_type = "mipi";
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

int load_camera(const CameraConfig& config)
{
    std::lock_guard<std::mutex> lock(active_camera_mutex);
    if (active_backend != CameraBackend::kNone) {
        spdlog::warn("A camera backend is already active, close it before loading another camera");
        return EXIT_FAILURE;
    }

    int ret = EXIT_FAILURE;
    if (config.camera_type == "usb") {
        ret = load_usb_camera(config.device, config.width, config.height);
        if (ret == EXIT_SUCCESS) {
            active_backend = CameraBackend::kUsb;
        }
    } else if (config.camera_type == "mipi") {
        ret = load_mipi_camera(to_mipi_config(config));
        if (ret == EXIT_SUCCESS) {
            active_backend = CameraBackend::kMipi;
        }
    } else {
        spdlog::error("Unsupported camera type: {}", config.camera_type);
    }

    return ret;
}

bool read_camera_frame(CameraFrame* frame)
{
    if (frame == nullptr) {
        return false;
    }

    frame->type = CameraFrameType::kNone;
    frame->raw_data = nullptr;
    frame->raw_size = 0;
    frame->buffer_index = 0;
    frame->mipi = MipiDmaFrame{};

    CameraBackend backend;
    {
        std::lock_guard<std::mutex> lock(active_camera_mutex);
        backend = active_backend;
    }

    if (backend == CameraBackend::kUsb) {
        void* raw_data = nullptr;
        uint32_t raw_size = 0;
        uint32_t buffer_index = 0;
        if (!read_usb_raw_packet(&raw_data, &raw_size, &buffer_index)) {
            return false;
        }
        frame->type = CameraFrameType::kUsbMjpegPacket;
        frame->raw_data = raw_data;
        frame->raw_size = raw_size;
        frame->buffer_index = buffer_index;
        return true;
    }

    if (backend == CameraBackend::kMipi) {
        MipiDmaFrame mipi_frame;
        if (!read_mipi_dma_frame(&mipi_frame)) {
            return false;
        }
        frame->type = CameraFrameType::kMipiNv12Dma;
        frame->buffer_index = mipi_frame.buffer_index;
        frame->mipi = mipi_frame;
        return true;
    }

    return false;
}

void release_camera_frame(const CameraFrame& frame)
{
    if (frame.type == CameraFrameType::kUsbMjpegPacket) {
        release_usb_raw_packet(frame.buffer_index);
    } else if (frame.type == CameraFrameType::kMipiNv12Dma) {
        release_mipi_dma_frame(frame.mipi.buffer_index);
    }
}

double get_camera_fps()
{
    std::lock_guard<std::mutex> lock(active_camera_mutex);
    if (active_backend == CameraBackend::kUsb) {
        return get_usb_camera_fps();
    }
    if (active_backend == CameraBackend::kMipi) {
        return get_mipi_camera_fps();
    }
    return 0.0;
}

bool has_camera_error()
{
    std::lock_guard<std::mutex> lock(active_camera_mutex);
    if (active_backend == CameraBackend::kUsb) {
        return has_usb_camera_error();
    }
    if (active_backend == CameraBackend::kMipi) {
        return has_mipi_camera_error();
    }
    return false;
}

std::string get_camera_error()
{
    std::lock_guard<std::mutex> lock(active_camera_mutex);
    if (active_backend == CameraBackend::kUsb) {
        return get_usb_camera_error();
    }
    if (active_backend == CameraBackend::kMipi) {
        return get_mipi_camera_error();
    }
    return {};
}

void close_camera()
{
    std::lock_guard<std::mutex> lock(active_camera_mutex);
    if (active_backend == CameraBackend::kUsb) {
        close_usb_camera();
    } else if (active_backend == CameraBackend::kMipi) {
        close_mipi_camera();
    }
    active_backend = CameraBackend::kNone;
}

CameraBackend get_active_camera_backend()
{
    std::lock_guard<std::mutex> lock(active_camera_mutex);
    return active_backend;
}
