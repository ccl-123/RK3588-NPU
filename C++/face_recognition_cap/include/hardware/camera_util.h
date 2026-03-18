#ifndef _CAMERA_UTIL_H_
#define _CAMERA_UTIL_H_

#include <string.h>
#include <string>
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
 * @brief 从帧缓存读取最新的一帧 (零拷贝/低延迟)
 * @param[out] orig_img 输出的 OpenCV Mat 对象
 * @param[in,out] consumer_sequence 调用方维护的帧游标；首次传 0，成功读取后会更新为最新序列号
 * @return true 读取成功, false 失败 (缓冲区为空或设备未就绪)
 * @note 使用 cv::Mat 浅拷贝，返回的 Mat 与内部帧共享数据。
 *       如需独立副本，调用者应使用 clone()。
 */
bool read_usb_frame(cv::Mat *orig_img, uint64_t *consumer_sequence);

/**
 * @brief 获取摄像头真实采集帧率
 * @return 摄像头采集帧率（约 30 FPS）
 */
double get_camera_fps();

/**
 * @brief 查询 USB 摄像头是否出现运行时错误（如热拔出）
 * @return true 出现错误, false 正常
 */
bool has_usb_camera_error();

/**
 * @brief 获取 USB 摄像头运行时错误信息
 * @return 错误信息；若无错误则返回空字符串
 */
std::string get_usb_camera_error();

/**
 * @brief 停止采集线程并关闭摄像头设备
 */
void close_usb_camera();

#endif
