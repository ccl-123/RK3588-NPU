/**
 * @file model_manager.cc
 * @brief 模型管理器实现
 * @author CL
 * @date 2025-11-20
 */

#include "app/model_manager.h"
#include "core/retinaface.h"
#include "core/facenet.h"
#include <cstring>
#include <iostream>

ModelManager::ModelManager()
    : retinaface_width_(0)
    , retinaface_height_(0)
    , retinaface_channel_(0)
    , retinaface_model_data_(nullptr)
    , retinaface_outputs_(nullptr)
    , facenet_width_(0)
    , facenet_height_(0)
    , facenet_channel_(0)
    , facenet_model_data_(nullptr)
    , facenet_outputs_(nullptr)
    , retinaface_initialized_(false)
    , facenet_initialized_(false)
{
}

ModelManager::~ModelManager() {
    release();
}

int ModelManager::init_retinaface(const char* model_path) {
    if (retinaface_initialized_) {
        std::cerr << "RetinaFace already initialized" << std::endl;
        return -1;
    }

    // 调用 core 层的创建函数
    int ret = create_retinaface(
        const_cast<char*>(model_path),
        &retinaface_ctx_,
        retinaface_width_,
        retinaface_height_,
        retinaface_channel_,
        retinaface_out_scales_,
        retinaface_out_zps_,
        retinaface_io_num_,
        retinaface_model_data_
    );

    if (ret != 0) {
        std::cerr << "Failed to create RetinaFace model" << std::endl;
        return -1;
    }

    // 配置输入
    memset(retinaface_inputs_, 0, sizeof(retinaface_inputs_));
    retinaface_inputs_[0].index = 0;
    retinaface_inputs_[0].type = RKNN_TENSOR_UINT8;
    retinaface_inputs_[0].size = retinaface_width_ * retinaface_height_ * retinaface_channel_;
    retinaface_inputs_[0].fmt = RKNN_TENSOR_NHWC;
    retinaface_inputs_[0].pass_through = 0;

    // 配置输出
    retinaface_outputs_ = new rknn_output[retinaface_io_num_.n_output];
    memset(retinaface_outputs_, 0, sizeof(rknn_output) * retinaface_io_num_.n_output);
    for (int i = 0; i < retinaface_io_num_.n_output; i++) {
        if (i != 1) {
            retinaface_outputs_[i].want_float = 0;
        } else {
            retinaface_outputs_[i].want_float = 1;
        }
    }

    retinaface_initialized_ = true;
    std::cout << "RetinaFace model initialized: " << retinaface_width_ << "x" 
              << retinaface_height_ << "x" << retinaface_channel_ << std::endl;
    return 0;
}

int ModelManager::init_facenet(const char* model_path) {
    if (facenet_initialized_) {
        std::cerr << "FaceNet already initialized" << std::endl;
        return -1;
    }

    // 调用 core 层的创建函数
    int ret = create_facenet(
        const_cast<char*>(model_path),
        &facenet_ctx_,
        facenet_width_,
        facenet_height_,
        facenet_channel_,
        facenet_io_num_,
        facenet_model_data_
    );

    if (ret != 0) {
        std::cerr << "Failed to create FaceNet model" << std::endl;
        return -1;
    }

    // 配置输入
    memset(facenet_inputs_, 0, sizeof(facenet_inputs_));
    facenet_inputs_[0].index = 0;
    facenet_inputs_[0].type = RKNN_TENSOR_UINT8;
    facenet_inputs_[0].size = facenet_width_ * facenet_height_ * facenet_channel_;
    facenet_inputs_[0].fmt = RKNN_TENSOR_NHWC;
    facenet_inputs_[0].pass_through = 0;

    // 配置输出
    facenet_outputs_ = new rknn_output[facenet_io_num_.n_output];
    memset(facenet_outputs_, 0, sizeof(rknn_output) * facenet_io_num_.n_output);
    for (int i = 0; i < facenet_io_num_.n_output; i++) {
        facenet_outputs_[i].want_float = 1;
    }

    facenet_initialized_ = true;
    std::cout << "FaceNet model initialized: " << facenet_width_ << "x" 
              << facenet_height_ << "x" << facenet_channel_ << std::endl;
    return 0;
}

void ModelManager::release() {
    if (retinaface_initialized_) {
        release_retinaface(&retinaface_ctx_, retinaface_model_data_);
        if (retinaface_outputs_) {
            delete[] retinaface_outputs_;
            retinaface_outputs_ = nullptr;
        }
        retinaface_initialized_ = false;
        std::cout << "RetinaFace model released" << std::endl;
    }

    if (facenet_initialized_) {
        release_facenet(&facenet_ctx_, facenet_model_data_);
        if (facenet_outputs_) {
            delete[] facenet_outputs_;
            facenet_outputs_ = nullptr;
        }
        facenet_initialized_ = false;
        std::cout << "FaceNet model released" << std::endl;
    }
}
