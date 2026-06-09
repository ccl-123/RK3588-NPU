#ifndef _CAMERA_USB_H_
#define _CAMERA_USB_H_

#include <cstdint>
#include <string>

/**
 * @brief 初始化 USB 摄像头 (V4L2 MJPEG) 并准备同步采集资源
 * @param device 设备节点名称 (例如 "0" 对应 /dev/video0，也可传入 /dev/video0)
 * @param camera_width 期望的采集宽度
 * @param camera_height 期望的采集高度
 * @return EXIT_SUCCESS 成功, EXIT_FAILURE 失败
 */
int load_usb_camera(std::string device, int camera_width, int camera_height);

/**
 * @brief 从 V4L2 硬件缓冲队列中获取当前 Raw MJPEG 数据包 (零拷贝 mmap 指针)
 * @param[out] packet_data 指向原始 MJPEG 缓冲区的指针
 * @param[out] packet_size MJPEG 数据大小（字节）
 * @param[out] buffer_index 缓冲区索引，处理完必须调用 release_usb_raw_packet 归还
 * @return true 成功获取, false 失败/无新数据
 */
bool read_usb_raw_packet(void** packet_data, uint32_t* packet_size, uint32_t* buffer_index);

/**
 * @brief 将 USB V4L2 缓冲区重新放入就绪队列 (QBUF)
 */
void release_usb_raw_packet(uint32_t buffer_index);

/**
 * @brief 获取 USB 摄像头真实采集帧率
 */
double get_usb_camera_fps();

/**
 * @brief 查询 USB 摄像头是否出现运行时错误（如热拔出）
 */
bool has_usb_camera_error();

/**
 * @brief 获取 USB 摄像头运行时错误信息
 */
std::string get_usb_camera_error();

/**
 * @brief 停止采集并关闭 USB 摄像头设备
 */
void close_usb_camera();

#endif // _CAMERA_USB_H_
