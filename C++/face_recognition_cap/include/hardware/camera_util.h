#ifndef _CAMERA_UTIL_H_
#define _CAMERA_UTIL_H_

#include <cstdint>
#include <string>

#include "hardware/camera_mipi.h"
#include "hardware/camera_usb.h"

constexpr unsigned int CAMERA_REQ_COUNT = 4;

enum class CameraBackend {
    kNone,
    kUsb,
    kMipi,
};

enum class CameraFrameType {
    kNone,
    kUsbMjpegPacket,
    kMipiNv12Dma,
};

/**
 * @brief 上层共用摄像头配置
 */
struct CameraConfig {
    std::string camera_type = "usb";     // "usb" 或 "mipi"
    std::string device;                  // "21" 或 "/dev/video21"
    int width = 1280;
    int height = 720;
    int fps = 30;

    // OV13855/MIPI 专用配置
    std::string sensor_subdev = "/dev/v4l-subdev2";
    int crop_left = 0;
    int crop_top = 380;
    int crop_width = 4224;
    int crop_height = 2376;
    int sensor_exposure = 1928;
    int sensor_vblank = 78;
    int sensor_analogue_gain = 1536;
};

/**
 * @brief 上层共用帧对象
 *
 * USB 路径返回 MJPEG mmap 指针；MIPI 路径返回 NV12 DMA-BUF fd。
 * 调用方处理完成后必须调用 release_camera_frame()。
 */
struct CameraFrame {
    CameraFrameType type = CameraFrameType::kNone;

    void* raw_data = nullptr;
    uint32_t raw_size = 0;
    uint32_t buffer_index = 0;

    MipiDmaFrame mipi;

    bool is_usb_mjpeg_packet() const {
        return type == CameraFrameType::kUsbMjpegPacket;
    }

    bool is_mipi_nv12_dma() const {
        return type == CameraFrameType::kMipiNv12Dma && mipi.dma_fd >= 0;
    }
};

CameraConfig make_usb_camera_config(const std::string& device,
                                    int width,
                                    int height);
CameraConfig make_mipi_ov13855_camera_config(const std::string& device,
                                             int width,
                                             int height);

int load_camera(const CameraConfig& config);
bool read_camera_frame(CameraFrame* frame);
void release_camera_frame(const CameraFrame& frame);
double get_camera_fps();
bool has_camera_error();
std::string get_camera_error();
void close_camera();
CameraBackend get_active_camera_backend();

#endif // _CAMERA_UTIL_H_
