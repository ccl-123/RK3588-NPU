/**
 * @file model_manager.h
 * @brief 模型管理器 - 负责RKNN模型的加载、配置和释放
 * @author CL
 * @date 2025-11-20
 */

#ifndef _MODEL_MANAGER_H_
#define _MODEL_MANAGER_H_

#include <vector>
#include <string>
#include "rknn_api.h"
#include "opencv2/core/core.hpp"
#include "core/postprocess.h"
#include "core/yolov8_face.h"

/**
 * @brief 模型管理器类
 * 
 * 职责:
 * - 加载和初始化 YOLOv8-face 和 FaceNet 模型
 * - 管理模型上下文和配置参数
 * - 提供模型推理接口
 * - 释放模型资源
 */
class ModelManager {
public:
    ModelManager();
    ~ModelManager();

    /**
     * @brief 初始化人脸检测模型 (YOLOv8-face)
     * @param model_path 模型文件路径
     * @return 0 成功, -1 失败
     */
    int init_face_detector(const char* model_path);

    /**
     * @brief 初始化 FaceNet 模型
     * @param model_path 模型文件路径
     * @return 0 成功, -1 失败
     */
    int init_facenet(const char* model_path);

    /**
     * @brief 获取人脸检测模型上下文
     */
    rknn_context* get_face_detector_ctx() { return &face_detector_ctx_; }

    /**
     * @brief 获取 FaceNet 上下文
     */
    rknn_context* get_facenet_ctx() { return &facenet_ctx_; }

    /**
     * @brief 获取人脸检测模型输入配置
     */
    rknn_input* get_face_detector_inputs() { return face_detector_inputs_; }

    /**
     * @brief 获取人脸检测模型输出配置
     */
    rknn_output* get_face_detector_outputs() { return face_detector_outputs_; }

    /**
     * @brief 获取人脸检测模型输出属性
     */
    rknn_tensor_attr* get_face_detector_output_attrs() { return face_detector_output_attrs_; }

    /**
     * @brief 获取 FaceNet 输入配置
     */
    rknn_input* get_facenet_inputs() { return facenet_inputs_; }

    /**
     * @brief 获取 FaceNet 输出配置
     */
    rknn_output* get_facenet_outputs() { return facenet_outputs_; }

    /**
     * @brief 获取人脸检测模型尺寸
     */
    void get_face_detector_size(int& width, int& height, int& channel) const {
        width = face_detector_width_;
        height = face_detector_height_;
        channel = face_detector_channel_;
    }

    /**
     * @brief 获取 FaceNet 模型尺寸
     */
    void get_facenet_size(int& width, int& height, int& channel) const {
        width = facenet_width_;
        height = facenet_height_;
        channel = facenet_channel_;
    }

    /**
     * @brief 获取人脸检测模型 IO 数量
     */
    const rknn_input_output_num& get_face_detector_io_num() const { return face_detector_io_num_; }

    /**
     * @brief 获取 FaceNet IO 数量
     */
    const rknn_input_output_num& get_facenet_io_num() const { return facenet_io_num_; }

    /**
     * @brief 释放所有模型资源
     */
    void release();

    // ============================================
    // 兼容性别名 (保持向后兼容)
    // ============================================
    int init_retinaface(const char* model_path) { return init_face_detector(model_path); }
    rknn_context* get_retinaface_ctx() { return get_face_detector_ctx(); }
    rknn_input* get_retinaface_inputs() { return get_face_detector_inputs(); }
    rknn_output* get_retinaface_outputs() { return get_face_detector_outputs(); }
    void get_retinaface_size(int& width, int& height, int& channel) const {
        get_face_detector_size(width, height, channel);
    }
    const rknn_input_output_num& get_retinaface_io_num() const { return get_face_detector_io_num(); }

private:
    // YOLOv8-face 人脸检测模型相关
    rknn_context face_detector_ctx_;
    int face_detector_width_;
    int face_detector_height_;
    int face_detector_channel_;
    rknn_input_output_num face_detector_io_num_;
    unsigned char* face_detector_model_data_;
    rknn_input face_detector_inputs_[1];
    rknn_output face_detector_outputs_[YOLOV8_FACE_OUTPUT_NUM];
    rknn_tensor_attr face_detector_output_attrs_[YOLOV8_FACE_OUTPUT_NUM];

    // FaceNet 模型相关
    rknn_context facenet_ctx_;
    int facenet_width_;
    int facenet_height_;
    int facenet_channel_;
    rknn_input_output_num facenet_io_num_;
    unsigned char* facenet_model_data_;
    rknn_input facenet_inputs_[1];
    rknn_output* facenet_outputs_;

    // 初始化标志
    bool face_detector_initialized_;
    bool facenet_initialized_;
};

#endif // _MODEL_MANAGER_H_
