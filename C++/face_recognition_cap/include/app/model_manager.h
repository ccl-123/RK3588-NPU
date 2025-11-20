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

/**
 * @brief 模型管理器类
 * 
 * 职责:
 * - 加载和初始化 RetinaFace 和 FaceNet 模型
 * - 管理模型上下文和配置参数
 * - 提供模型推理接口
 * - 释放模型资源
 */
class ModelManager {
public:
    ModelManager();
    ~ModelManager();

    /**
     * @brief 初始化 RetinaFace 模型
     * @param model_path 模型文件路径
     * @return 0 成功, -1 失败
     */
    int init_retinaface(const char* model_path);

    /**
     * @brief 初始化 FaceNet 模型
     * @param model_path 模型文件路径
     * @return 0 成功, -1 失败
     */
    int init_facenet(const char* model_path);

    /**
     * @brief 获取 RetinaFace 上下文
     */
    rknn_context* get_retinaface_ctx() { return &retinaface_ctx_; }

    /**
     * @brief 获取 FaceNet 上下文
     */
    rknn_context* get_facenet_ctx() { return &facenet_ctx_; }

    /**
     * @brief 获取 RetinaFace 输入配置
     */
    rknn_input* get_retinaface_inputs() { return retinaface_inputs_; }

    /**
     * @brief 获取 RetinaFace 输出配置
     */
    rknn_output* get_retinaface_outputs() { return retinaface_outputs_; }

    /**
     * @brief 获取 FaceNet 输入配置
     */
    rknn_input* get_facenet_inputs() { return facenet_inputs_; }

    /**
     * @brief 获取 FaceNet 输出配置
     */
    rknn_output* get_facenet_outputs() { return facenet_outputs_; }

    /**
     * @brief 获取 RetinaFace 模型尺寸
     */
    void get_retinaface_size(int& width, int& height, int& channel) const {
        width = retinaface_width_;
        height = retinaface_height_;
        channel = retinaface_channel_;
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
     * @brief 获取 RetinaFace 量化参数
     */
    const std::vector<float>& get_retinaface_out_scales() const { return retinaface_out_scales_; }
    const std::vector<int32_t>& get_retinaface_out_zps() const { return retinaface_out_zps_; }

    /**
     * @brief 获取 RetinaFace IO 数量
     */
    const rknn_input_output_num& get_retinaface_io_num() const { return retinaface_io_num_; }

    /**
     * @brief 获取 FaceNet IO 数量
     */
    const rknn_input_output_num& get_facenet_io_num() const { return facenet_io_num_; }

    /**
     * @brief 释放所有模型资源
     */
    void release();

private:
    // RetinaFace 模型相关
    rknn_context retinaface_ctx_;
    int retinaface_width_;
    int retinaface_height_;
    int retinaface_channel_;
    std::vector<float> retinaface_out_scales_;
    std::vector<int32_t> retinaface_out_zps_;
    rknn_input_output_num retinaface_io_num_;
    unsigned char* retinaface_model_data_;
    rknn_input retinaface_inputs_[1];
    rknn_output* retinaface_outputs_;

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
    bool retinaface_initialized_;
    bool facenet_initialized_;
};

#endif // _MODEL_MANAGER_H_

