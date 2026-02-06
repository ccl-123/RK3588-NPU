/**
 * @file yolov8_face.cc
 * @brief YOLOv8-face 人脸检测模型实现
 * @details 使用 airockchip RKOPT 格式 (4个输出)
 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <iostream>
#include <vector>
#include <array>
#include <cstring>
#include <limits>

#define _BASETSD_H

#include "RgaUtils.h"
#include "im2d.h"
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include "core/postprocess.h"
#include "core/yolov8_face.h"
#include "rga.h"
#include "rknn_api.h"
#include <chrono>

static void dump_tensor_attr(rknn_tensor_attr* attr) {
    printf("  index=%d, name=%s, n_dims=%d, dims=[%d, %d, %d, %d], n_elems=%d, size=%d, fmt=%s, type=%s, qnt_type=%s, "
           "zp=%d, scale=%f\n",
           attr->index, attr->name, attr->n_dims, attr->dims[0], attr->dims[1], attr->dims[2], attr->dims[3],
           attr->n_elems, attr->size, get_format_string(attr->fmt), get_type_string(attr->type),
           get_qnt_type_string(attr->qnt_type), attr->zp, attr->scale);
}

static unsigned char* load_data(FILE* fp, size_t ofst, size_t sz) {
    unsigned char* data;
    int ret;

    data = NULL;

    if (NULL == fp) {
        return NULL;
    }

    ret = fseek(fp, ofst, SEEK_SET);
    if (ret != 0) {
        printf("blob seek failure.\n");
        return NULL;
    }

    data = (unsigned char*)malloc(sz);
    if (data == NULL) {
        printf("buffer malloc failure.\n");
        return NULL;
    }
    size_t read_size = fread(data, 1, sz, fp);
    if (read_size != sz) {
        printf("blob read failure, expect=%zu actual=%zu.\n", sz, read_size);
        free(data);
        return NULL;
    }
    return data;
}

static unsigned char* load_model(const char* filename, int* model_size) {
    FILE* fp;
    unsigned char* data;

    fp = fopen(filename, "rb");
    if (NULL == fp) {
        printf("Open file %s failed.\n", filename);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        printf("Seek file %s failed.\n", filename);
        fclose(fp);
        return NULL;
    }
    long size_long = ftell(fp);
    if (size_long <= 0 || size_long > std::numeric_limits<int>::max()) {
        printf("Invalid model size for %s: %ld\n", filename, size_long);
        fclose(fp);
        return NULL;
    }
    int size = static_cast<int>(size_long);

    data = load_data(fp, 0, size);
    if (data == NULL) {
        fclose(fp);
        return NULL;
    }

    fclose(fp);

    *model_size = size;
    return data;
}

int create_yolov8_face(char* model_name, rknn_context* ctx,
                       int& width, int& height, int& channel,
                       rknn_input_output_num& io_num,
                       rknn_tensor_attr* output_attrs,
                       unsigned char*& model_data) {
    int ret = -1;
    bool rknn_inited = false;
    std::vector<rknn_tensor_attr> input_attrs;

    if (ctx == nullptr || output_attrs == nullptr) {
        printf("create_yolov8_face invalid args: null pointer\n");
        return -1;
    }

    // 加载模型
    printf("Loading YOLOv8-face model...\n");
    int model_data_size = 0;
    model_data = load_model(model_name, &model_data_size);
    if (model_data == nullptr || model_data_size <= 0) {
        printf("load_model failed, model=%s size=%d\n", model_name, model_data_size);
        return -1;
    }

    // 启用高优先级
    uint32_t flag = RKNN_FLAG_PRIOR_HIGH;
    ret = rknn_init(ctx, model_data, model_data_size, flag, NULL);
    if (ret < 0) {
        printf("rknn_init error ret=%d\n", ret);
        free(model_data);
        model_data = nullptr;
        return -1;
    }
    rknn_inited = true;

    // 设置 NPU 核心 - 使用所有核心提高性能
    rknn_core_mask core_mask = RKNN_NPU_CORE_0_1_2;  // 使用核心0、1、2
    ret = rknn_set_core_mask(*ctx, core_mask);
    if (ret < 0) {
        printf("rknn_set_core_mask error ret=%d\n", ret);
        goto create_failed;
    }

    // 查询 SDK 版本
    rknn_sdk_version version;
    ret = rknn_query(*ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(rknn_sdk_version));
    if (ret < 0) {
        printf("rknn_query SDK version error ret=%d\n", ret);
        goto create_failed;
    }
    printf("sdk version: %s driver version: %s\n", version.api_version, version.drv_version);

    // 查询输入输出数量
    ret = rknn_query(*ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0) {
        printf("rknn_query io_num error ret=%d\n", ret);
        goto create_failed;
    }
    if (io_num.n_input == 0) {
        printf("invalid model io: n_input=%u\n", io_num.n_input);
        ret = -1;
        goto create_failed;
    }
    printf("model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);

    // 验证输出数量
    if (io_num.n_output != YOLOV8_FACE_OUTPUT_NUM) {
        printf("Error: Expected %d outputs for YOLOv8-face RKOPT format, got %d\n",
               YOLOV8_FACE_OUTPUT_NUM, io_num.n_output);
        ret = -1;
        goto create_failed;
    }

    // 查询输入属性
    input_attrs.resize(io_num.n_input);
    memset(input_attrs.data(), 0, sizeof(rknn_tensor_attr) * input_attrs.size());
    for (uint32_t i = 0; i < io_num.n_input; ++i) {
        input_attrs[i].index = i;
        ret = rknn_query(*ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0) {
            printf("rknn_query input attr error ret=%d\n", ret);
            goto create_failed;
        }
        printf("Input %u:\n", i);
        dump_tensor_attr(&(input_attrs[i]));
    }

    // 解析输入尺寸
    if (input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
        printf("model is NCHW input fmt\n");
        channel = input_attrs[0].dims[1];
        height = input_attrs[0].dims[2];
        width = input_attrs[0].dims[3];
    } else {
        printf("model is NHWC input fmt\n");
        height = input_attrs[0].dims[1];
        width = input_attrs[0].dims[2];
        channel = input_attrs[0].dims[3];
    }
    printf("model input height=%d, width=%d, channel=%d\n", height, width, channel);

    // 查询输出属性
    memset(output_attrs, 0, sizeof(rknn_tensor_attr) * YOLOV8_FACE_OUTPUT_NUM);
    for (uint32_t i = 0; i < io_num.n_output; ++i) {
        output_attrs[i].index = i;
        ret = rknn_query(*ctx, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0) {
            printf("rknn_query output attr error ret=%d\n", ret);
            goto create_failed;
        }
        printf("Output %u:\n", i);
        dump_tensor_attr(&(output_attrs[i]));
    }

    return 0;

create_failed:
    if (rknn_inited) {
        rknn_destroy(*ctx);
    }
    if (model_data) {
        free(model_data);
        model_data = nullptr;
    }
    return ret;
}

int yolov8_face_run(rknn_context* ctx, const cv::Mat& img,
                    int width, int height, int channel,
                    int img_width, int img_height,
                    const rknn_input_output_num& io_num,
                    rknn_input* inputs, rknn_output* outputs,
                    rknn_tensor_attr* output_attrs,
                    std::array<std::vector<uint8_t>, YOLOV8_FACE_OUTPUT_NUM>& output_buffers,
                    YoloRunTimings* timings) {
    int ret;
    if (ctx == nullptr || inputs == nullptr || outputs == nullptr || img.empty()) {
        printf("yolov8_face_run invalid args\n");
        return -1;
    }
    if (io_num.n_input == 0 || io_num.n_output == 0) {
        printf("yolov8_face_run invalid io_num: in=%u out=%u\n", io_num.n_input, io_num.n_output);
        return -1;
    }
    if (io_num.n_output > YOLOV8_FACE_OUTPUT_NUM) {
        printf("rknn output num overflow: %u > %d\n", io_num.n_output, YOLOV8_FACE_OUTPUT_NUM);
        return -1;
    }
    (void)channel;
    (void)img_height;
    (void)img_width;
    (void)width;
    (void)height;
    (void)output_attrs;

    auto t_start = std::chrono::steady_clock::now();
    inputs[0].buf = const_cast<void*>(reinterpret_cast<const void*>(img.data));

    ret = rknn_inputs_set(*ctx, io_num.n_input, inputs);
    auto t_after_inputs = std::chrono::steady_clock::now();
    if (ret < 0) {
        printf("rknn_inputs_set error ret=%d\n", ret);
        return ret;
    }

    ret = rknn_run(*ctx, NULL);
    auto t_after_run = std::chrono::steady_clock::now();
    if (ret < 0) {
        printf("rknn_run error ret=%d\n", ret);
        return ret;
    }

    ret = rknn_outputs_get(*ctx, io_num.n_output, outputs, NULL);
    auto t_after_outputs = std::chrono::steady_clock::now();
    if (ret < 0) {
        printf("rknn_outputs_get error ret=%d\n", ret);
        return ret;
    }

    // 拷贝输出到自管 buffer，便于跨线程传递
    for (uint32_t i = 0; i < io_num.n_output; ++i) {
        output_buffers[static_cast<size_t>(i)].resize(outputs[i].size);
        if (!output_buffers[static_cast<size_t>(i)].empty() && outputs[i].buf) {
            memcpy(output_buffers[static_cast<size_t>(i)].data(), outputs[i].buf, outputs[i].size);
        }
    }
    auto t_after_copy = std::chrono::steady_clock::now();

    if (timings) {
        timings->inputs_set_ms = std::chrono::duration_cast<std::chrono::microseconds>(t_after_inputs - t_start).count() / 1000.0;
        timings->run_ms = std::chrono::duration_cast<std::chrono::microseconds>(t_after_run - t_after_inputs).count() / 1000.0;
        timings->outputs_get_ms = std::chrono::duration_cast<std::chrono::microseconds>(t_after_outputs - t_after_run).count() / 1000.0;
        timings->copy_ms = std::chrono::duration_cast<std::chrono::microseconds>(t_after_copy - t_after_outputs).count() / 1000.0;
    }

    ret = rknn_outputs_release(*ctx, io_num.n_output, outputs);
    return ret;
}

int yolov8_face_postprocess(
    const std::array<std::vector<uint8_t>, YOLOV8_FACE_OUTPUT_NUM>& output_buffers,
    rknn_tensor_attr* output_attrs,
    int n_output,
    int model_in_h, int model_in_w,
    int img_width, int img_height,
    float box_conf_threshold, float nms_threshold,
    detect_result_group_t* detect_result_group) {
    if (output_attrs == nullptr || detect_result_group == nullptr) {
        printf("yolov8_face_postprocess invalid args: null pointer\n");
        return -1;
    }
    if (n_output <= 0 || n_output > YOLOV8_FACE_OUTPUT_NUM) {
        printf("yolov8_face_postprocess invalid n_output: %d\n", n_output);
        return -1;
    }
    if (model_in_h <= 0 || model_in_w <= 0 || img_width <= 0 || img_height <= 0) {
        printf("yolov8_face_postprocess invalid image/model size\n");
        return -1;
    }

    // 构造临时 rknn_output 指向已拷贝的数据
    rknn_output outputs[YOLOV8_FACE_OUTPUT_NUM];
    memset(outputs, 0, sizeof(outputs));
    for (int i = 0; i < n_output; ++i) {
        if (output_buffers[i].empty()) {
            printf("yolov8_face_postprocess empty output buffer at index %d\n", i);
            return -1;
        }
        outputs[i].is_prealloc = 1;
        outputs[i].want_float = 0;
        outputs[i].buf = const_cast<uint8_t*>(output_buffers[i].data());
        outputs[i].size = output_buffers[i].size();
    }

    float scale_w = static_cast<float>(model_in_w) / img_width;
    float scale_h = static_cast<float>(model_in_h) / img_height;

    memset(detect_result_group, 0, sizeof(detect_result_group_t));
    int ret = post_process_yolov8_face(outputs, output_attrs, n_output,
                                       model_in_h, model_in_w,
                                       box_conf_threshold, nms_threshold,
                                       scale_w, scale_h,
                                       detect_result_group);
    return ret;
}

void release_yolov8_face(rknn_context* ctx, unsigned char* model_data) {
    deinitPostProcess();
    rknn_destroy(*ctx);

    if (model_data) {
        free(model_data);
    }
}
