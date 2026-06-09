#ifndef _CAMERA_MIPI_H_
#define _CAMERA_MIPI_H_

#include <cstddef>
#include <cstdint>
#include <string>

/**
 * @brief OV13855/MIPI 摄像头 V4L2 配置
 */
struct MipiCameraConfig {
    std::string device = "/dev/video11";
    int width = 1920;
    int height = 1080;
    int fps = 30;

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
 * @brief 单平面 NV12 DMA-BUF 帧描述
 *
 * dma_fd 由底层 V4L2 缓冲导出并在 close_mipi_camera() 时统一关闭；
 * 调用方在 RGA/NPU 使用完成后必须调用 release_mipi_dma_frame(buffer_index)。
 */
struct MipiDmaFrame {
    int dma_fd = -1;
    std::size_t dma_offset = 0;
    std::size_t dma_bytesused = 0;
    std::size_t dma_length = 0;

    void* mapped_data = nullptr;
    std::size_t mapped_length = 0;

    int width = 0;
    int height = 0;
    int horizontal_stride = 0;
    int vertical_stride = 0;

    uint32_t buffer_index = 0;
    uint64_t sequence = 0;
};

MipiCameraConfig make_default_ov13855_config(const std::string& device,
                                             int width,
                                             int height);

int load_mipi_camera(const MipiCameraConfig& config);
bool read_mipi_dma_frame(MipiDmaFrame* frame);
void release_mipi_dma_frame(uint32_t buffer_index);
double get_mipi_camera_fps();
bool has_mipi_camera_error();
std::string get_mipi_camera_error();
void close_mipi_camera();

#endif // _CAMERA_MIPI_H_
