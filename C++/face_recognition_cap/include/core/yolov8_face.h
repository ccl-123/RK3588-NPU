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
#include "rknn_api.h"
#include "core/postprocess.h"

// YOLOv8-face RKOPT 输出数量
#define YOLOV8_FACE_OUTPUT_NUM 4

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
 * @brief YOLOv8-face 推理
 * @param ctx                   RKNN 上下文
 * @param img                   输入图像 (已预处理到模型输入尺寸)
 * @param width                 模型输入宽度
 * @param height                模型输入高度
 * @param channel               模型输入通道数
 * @param box_conf_threshold    置信度阈值
 * @param nms_threshold         NMS 阈值
 * @param img_width             原图宽度 (padding 后的正方形)
 * @param img_height            原图高度 (padding 后的正方形)
 * @param io_num                输入输出数量
 * @param inputs                输入数组
 * @param outputs               输出数组
 * @param output_attrs          输出属性数组
 * @param detect_result_group   检测结果
 * @return 0 成功, 其他失败
 */
int yolov8_face_inference(rknn_context* ctx, cv::Mat img,
                          int width, int height, int channel,
                          float box_conf_threshold, float nms_threshold,
                          int img_width, int img_height,
                          rknn_input_output_num io_num,
                          rknn_input* inputs, rknn_output* outputs,
                          rknn_tensor_attr* output_attrs,
                          detect_result_group_t* detect_result_group);

/**
 * @brief 释放 YOLOv8-face 模型资源
 */
void release_yolov8_face(rknn_context* ctx, unsigned char* model_data);

#endif // __YOLOV8_FACE_H__
