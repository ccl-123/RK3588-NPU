/**
 * @file model_manager.cc
 * @brief 模型管理器实现
 * @author CL
 * @date 2025-11-20
 */

#include "app/model_manager.h"
#include "core/yolov8_face.h"
#include "core/facenet.h"
#include <cstring>
#include <iostream>

ModelManager::ModelManager()
    : face_detector_width_(0)
    , face_detector_height_(0)
    , face_detector_channel_(0)
    , face_detector_model_data_(nullptr)
    , facenet_width_(0)
    , facenet_height_(0)
    , facenet_channel_(0)
    , facenet_model_data_(nullptr)
    , facenet_outputs_(nullptr)
    , face_detector_initialized_(false)
    , facenet_initialized_(false)
{
    memset(face_detector_outputs_, 0, sizeof(face_detector_outputs_));
    memset(face_detector_output_attrs_, 0, sizeof(face_detector_output_attrs_));
}

ModelManager::~ModelManager() {
    release();
}

int ModelManager::init_face_detector(const char* model_path) {
    if (face_detector_initialized_) {
        std::cerr << "Face detector already initialized" << std::endl;
        return -1;
    }

    // 调用 YOLOv8-face 创建函数
    int ret = create_yolov8_face(
        const_cast<char*>(model_path),
        &face_detector_ctx_,
        face_detector_width_,
        face_detector_height_,
        face_detector_channel_,
        face_detector_io_num_,
        face_detector_output_attrs_,
        face_detector_model_data_
    );

    if (ret != 0) {
        std::cerr << "Failed to create YOLOv8-face model" << std::endl;
        return -1;
    }

    // 配置输入
    memset(face_detector_inputs_, 0, sizeof(face_detector_inputs_));
    face_detector_inputs_[0].index = 0;
    face_detector_inputs_[0].type = RKNN_TENSOR_UINT8;
    face_detector_inputs_[0].size = face_detector_width_ * face_detector_height_ * face_detector_channel_;
    face_detector_inputs_[0].fmt = RKNN_TENSOR_NHWC;
    face_detector_inputs_[0].pass_through = 0;

    // 配置输出 - YOLOv8-face 有 4 个输出
    // 模型是 int8 量化的，设置 want_float=1 让 RKNN 自动反量化为 float
    memset(face_detector_outputs_, 0, sizeof(face_detector_outputs_));
    for (int i = 0; i < YOLOV8_FACE_OUTPUT_NUM; i++) {
        face_detector_outputs_[i].want_float = 1;
    }

    face_detector_initialized_ = true;
    std::cout << "YOLOv8-face model initialized: " << face_detector_width_ << "x" 
              << face_detector_height_ << "x" << face_detector_channel_ << std::endl;
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

    // 配置输出 - 模型是 int8 量化的，设置 want_float=1 让 RKNN 自动反量化为 float
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
    if (face_detector_initialized_) {
        release_yolov8_face(&face_detector_ctx_, face_detector_model_data_);
        face_detector_initialized_ = false;
        std::cout << "YOLOv8-face model released" << std::endl;
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
