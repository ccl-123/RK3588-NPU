#ifndef _CAMERA_UTIL_H_

#include <string.h>
#include <opencv2/opencv.hpp>
#include <thread>
#include <mutex>
#include <atomic>

#define REQ_COUNT 4

struct Buffer {
    void* start;
    size_t length;
};

/**
 * @brief 初始化 USB 摄像头 (V4L2) 并准备异步采集资源
 * @param device 设备节点名称 (例如 "0" 对应 /dev/video0)
 * @param camera_width 期望的采集宽度
 * @param camera_height 期望的采集高度
 * @return EXIT_SUCCESS 成功, EXIT_FAILURE 失败
 */
int load_usb_camera(std::string device, int camera_width, int camera_height);

/**
 * @brief 启动 USB 摄像头采集线程
 */
void start_usb_capture_thread();

/**
 * @brief 停止 USB 摄像头采集线程
 */
void stop_usb_capture_thread();

/**
 * @brief 从双缓冲中读取最新的一帧 (非阻塞/低延迟)
 * @param[out] orig_img 输出的 OpenCV Mat 对象
 * @return true 读取成功, false 失败 (缓冲区为空或设备未就绪)
 */
bool read_usb_frame(cv::Mat *orig_img);

/**
 * @brief 停止采集线程并关闭摄像头设备
 */
void close_usb_camera();

#endif
