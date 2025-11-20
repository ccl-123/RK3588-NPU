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

// USB摄像头相关函数
int load_usb_camera(std::string device, int camera_width, int camera_height);
void read_usb_frame(cv::Mat *orig_img);
void close_usb_camera();

// USB摄像头异步读取相关函数 (优化版本)
int load_usb_camera_async(std::string device, int camera_width, int camera_height);
void start_usb_capture_thread();
void stop_usb_capture_thread();
void read_usb_frame_async(cv::Mat *orig_img);
void close_usb_camera_async();

// MIPI摄像头相关函数 (保持不变)
int load_mipi_camera(std::string device, int camera_width, int camera_height);
void read_mipi_frame(cv::Mat *orig_img);
void close_mipi_camera();

#endif
