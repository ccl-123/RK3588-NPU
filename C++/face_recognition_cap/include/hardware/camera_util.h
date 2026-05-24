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
 * @brief 从 V4L2 硬件缓冲队列中获取当前最新的 Raw MJPEG 数据包 (零拷贝)
 * @param[out] packet_data 指向原始 MJPEG 缓冲区的指针
 * @param[out] packet_size MJPEG 数据的大小（字节）
 * @param[out] buffer_index 缓冲区的底层索引，后续必须调用 release_usb_raw_packet 归还
 * @return true 成功获取, false 失败/无新数据
 */
bool read_usb_raw_packet(void** packet_data, uint32_t* packet_size, uint32_t* buffer_index);

/**
 * @brief 将处理完的硬件缓冲区重新放入就绪队列 (QBUF)
 * @param buffer_index 缓冲区的底层索引
 */
void release_usb_raw_packet(uint32_t buffer_index);

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
