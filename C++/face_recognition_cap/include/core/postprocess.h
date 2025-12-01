/**
 * @file postprocess.h
 * @brief 后处理头文件
 * @details 支持 YOLOv8-face 后处理 (RKOPT 格式)
 */

#ifndef _RKNN_YOLOV8_FACE_POSTPROCESS_H_
#define _RKNN_YOLOV8_FACE_POSTPROCESS_H_

#include <stdint.h>
#include <vector>
#include <opencv2/opencv.hpp>
#include "rknn_api.h"

// ============================================
// 常量定义
// ============================================
#define OBJ_NAME_MAX_SIZE 16
#define OBJ_NUMB_MAX_SIZE 64
#define OBJ_CLASS_NUM     1           // YOLOv8-face 只有 1 个类别 (face)
#define NMS_THRESH        0.45
#define BOX_THRESH        0.5
#define FACENET_THRESH    0.5

// 人脸特征向量维度 (w600k_mbf.rknn: 512维)
#define FACENET_FEATURE_DIM 512

// YOLOv8 DFL 参数
#define DFL_LEN           16          // DFL 每个边界 16 个 bins

// ============================================
// 数据结构定义
// ============================================

// 边界框
typedef struct _BOX_RECT {
    int left;
    int right;
    int top;
    int bottom;
} BOX_RECT;

// 5 个关键点 (左眼、右眼、鼻子、左嘴角、右嘴角)
typedef struct _KEY_POINT {
    int point_1_x;  // 左眼 x
    int point_1_y;  // 左眼 y
    int point_2_x;  // 右眼 x
    int point_2_y;  // 右眼 y
    int point_3_x;  // 鼻子 x
    int point_3_y;  // 鼻子 y
    int point_4_x;  // 左嘴角 x
    int point_4_y;  // 左嘴角 y
    int point_5_x;  // 右嘴角 x
    int point_5_y;  // 右嘴角 y
} KEY_POINT;

// 单个检测结果
typedef struct __detect_result_t {
    char name[OBJ_NAME_MAX_SIZE];
    BOX_RECT box;
    KEY_POINT point;
    float prop;     // 置信度
} detect_result_t;

// 检测结果组
typedef struct _detect_result_group_t {
    int id;
    int count;
    detect_result_t results[OBJ_NUMB_MAX_SIZE];
} detect_result_group_t;

// ============================================
// YOLOv8-face 后处理函数
// ============================================

/**
 * @brief YOLOv8-face 后处理函数 (RKOPT 格式)
 * @param outputs       RKNN 输出数组 (4个输出)
 *                      - outputs[0]: [1,65,80,80] DFL bbox + conf (stride=8)
 *                      - outputs[1]: [1,65,40,40] DFL bbox + conf (stride=16)
 *                      - outputs[2]: [1,65,20,20] DFL bbox + conf (stride=32)
 *                      - outputs[3]: [1,5,3,8400] 关键点 (5点×3维×8400anchor)
 * @param output_attrs  输出 tensor 属性
 * @param n_output      输出数量 (应为4)
 * @param model_in_h    模型输入高度 (640)
 * @param model_in_w    模型输入宽度 (640)
 * @param conf_threshold 置信度阈值
 * @param nms_threshold  NMS 阈值
 * @param scale_w       宽度缩放比例
 * @param scale_h       高度缩放比例
 * @param group         输出检测结果
 * @return 0 成功, 其他失败
 */
int post_process_yolov8_face(rknn_output* outputs, rknn_tensor_attr* output_attrs, int n_output,
                             int model_in_h, int model_in_w,
                             float conf_threshold, float nms_threshold,
                             float scale_w, float scale_h,
                             detect_result_group_t* group);

// ============================================
// 人脸对齐函数
// ============================================

/**
 * @brief 计算相似变换矩阵
 * @param src 源关键点 (5×2)
 * @param dst 目标关键点 (5×2)
 * @return 3×3 变换矩阵
 */
cv::Mat similarTransform(cv::Mat src, cv::Mat dst);

// ============================================
// 特征比较函数
// ============================================

/**
 * @brief L2 归一化
 */
void l2_normalize(float* input);

/**
 * @brief 欧氏距离
 */
float compare_eu_distance(float* input1, float* input2);

/**
 * @brief 余弦相似度
 */
float cos_similarity(float* input1, float* input2);

// ============================================
// 清理函数
// ============================================

void deinitPostProcess();

#endif // _RKNN_YOLOV8_FACE_POSTPROCESS_H_
