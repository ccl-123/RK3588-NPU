/**
 * @file yolov8_face.h
 * @brief YOLOv8-face 人脸检测模型头文件
 * @details 使用 airockchip RKOPT 格式 (4个输出)
 *          - 输出0-2: [1,65,H,W] DFL bbox + conf
 *          - 输出3: [1,5,3,8400] 5个关键点
 */

#ifndef __YOLOV8_FACE_H__
#define __YOLOV8_FACE_H__

#include <stdint.h>
#include <vector>
#include <array>
#include "rknn_api.h"
#include "core/postprocess.h"

// YOLOv8-face RKOPT 输出数量
#define YOLOV8_FACE_OUTPUT_NUM 4

struct YoloRunTimings {
    double inputs_set_ms{0.0};
    double run_ms{0.0};
    double outputs_get_ms{0.0};
    double copy_ms{0.0};
};

/**
 * @brief 创建 YOLOv8-face 模型
 * @param model_name      模型文件路径
 * @param ctx             RKNN 上下文
 * @param width           输出模型输入宽度
 * @param height          输出模型输入高度
 * @param channel         输出模型输入通道数
 * @param io_num          输出输入输出数量
 * @param output_attrs    输出属性数组 (需要预分配 YOLOV8_FACE_OUTPUT_NUM 个)
 * @param model_data      模型数据指针
 * @return 0 成功, 其他失败
 */
int create_yolov8_face(char* model_name, rknn_context* ctx,
                       int& width, int& height, int& channel,
                       rknn_input_output_num& io_num,
                       rknn_tensor_attr* output_attrs,
                       unsigned char*& model_data);  // 引用传递，确保内存正确释放

/**
 * @brief YOLOv8-face 推理（仅 NPU 运行 + 取输出）
 * @param ctx          RKNN 上下文
 * @param img          输入图像 (已预处理到模型输入尺寸)
 * @param width        模型输入宽度
 * @param height       模型输入高度
 * @param channel      模型输入通道数
 * @param img_width    padding 后的宽
 * @param img_height   padding 后的高
 * @param io_num       输入输出数量
 * @param inputs       输入数组
 * @param outputs      输出数组
 * @param output_attrs 输出属性数组
 * @param output_buffers 输出原始数据拷贝（int8）
 * @return 0 成功, 其他失败
 */
int yolov8_face_run(rknn_context* ctx, const cv::Mat& img,
                    int width, int height, int channel,
                    int img_width, int img_height,
                    const rknn_input_output_num& io_num,
                    rknn_input* inputs, rknn_output* outputs,
                    rknn_tensor_attr* output_attrs,
                    std::array<std::vector<uint8_t>, YOLOV8_FACE_OUTPUT_NUM>& output_buffers,
                    YoloRunTimings* timings = nullptr);

/**
 * @brief YOLOv8-face 后处理（独立线程使用）
 * @param output_buffers   YOLO 原始输出拷贝
 * @param output_attrs     输出属性
 * @param n_output         输出数量
 * @param model_in_h       模型输入高
 * @param model_in_w       模型输入宽
 * @param img_width        padding 后的宽
 * @param img_height       padding 后的高
 * @param box_conf_threshold 置信度阈值
 * @param nms_threshold    NMS 阈值
 * @param detect_result_group 检测结果
 * @return 0 成功, 其他失败
 */
int yolov8_face_postprocess(
    const std::array<std::vector<uint8_t>, YOLOV8_FACE_OUTPUT_NUM>& output_buffers,
    rknn_tensor_attr* output_attrs,
    int n_output,
    int model_in_h, int model_in_w,
    int img_width, int img_height,
    float box_conf_threshold, float nms_threshold,
    detect_result_group_t* detect_result_group);

/**
 * @brief Zero-Copy 零拷贝初始化与绑定
 */
int yolov8_face_init_zero_copy(rknn_context ctx,
                               rknn_tensor_mem** input_mem,
                               std::vector<rknn_tensor_mem*>& output_mems);

/**
 * @brief Zero-Copy 极速推理
 */
int yolov8_face_run_zero_copy(rknn_context ctx);

/**
 * @brief Zero-Copy 内存销毁与释放
 */
int yolov8_face_release_zero_copy(rknn_context ctx,
                                  rknn_tensor_mem* input_mem,
                                  std::vector<rknn_tensor_mem*>& output_mems);

/**
 * @brief 释放 YOLOv8-face 模型资源
 */
void release_yolov8_face(rknn_context* ctx, unsigned char* model_data);

#endif // __YOLOV8_FACE_H__
