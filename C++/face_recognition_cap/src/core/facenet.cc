/**
 * @file facenet.cc
 * @brief FaceNet 人脸特征提取模型实现
 * @details 基于 RKNN 的 MobileFaceNet + ArcFace 模型推理实现，
 *          输出 512 维归一化人脸特征向量
 * @author CL
 * @date 2025-11-20
 */

/*-------------------------------------------
                Includes
-------------------------------------------*/
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <dirent.h>
#include <iostream>
#include <fstream>
#include <typeinfo>

#define _BASETSD_H

#include "RgaUtils.h"
#include "im2d.h"
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include "postprocess.h"
#include "facenet.h"
#include "rga.h"
#include "rknn_api.h"

#define PERF_WITH_POST 1
/*-------------------------------------------
                  Functions
-------------------------------------------*/

static void dump_tensor_attr(rknn_tensor_attr* attr)
{
  printf("  index=%d, name=%s, n_dims=%d, dims=[%d, %d, %d, %d], n_elems=%d, size=%d, fmt=%s, type=%s, qnt_type=%s, "
         "zp=%d, scale=%f\n",
         attr->index, attr->name, attr->n_dims, attr->dims[0], attr->dims[1], attr->dims[2], attr->dims[3],
         attr->n_elems, attr->size, get_format_string(attr->fmt), get_type_string(attr->type),
         get_qnt_type_string(attr->qnt_type), attr->zp, attr->scale);
}

static unsigned char* load_data(FILE* fp, size_t ofst, size_t sz)
{
  	unsigned char* data;
  	int            ret;

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
  	ret = fread(data, 1, sz, fp);
  	return data;
}

static unsigned char* load_model(const char* filename, int* model_size)
{
  	FILE*          fp;
  	unsigned char* data;

  	fp = fopen(filename, "rb");
  	if (NULL == fp) {
		printf("Open file %s failed.\n", filename);
		return NULL;
  	}

  	fseek(fp, 0, SEEK_END);
  	int size = ftell(fp);

  	data = load_data(fp, 0, size);

  	fclose(fp);

  	*model_size = size;
  	return data;
}

static int saveFloat(const char* file_name, float* output, int element_size)
{
  	FILE* fp;
  	fp = fopen(file_name, "w");
  	for (int i = 0; i < element_size; i++) {
		fprintf(fp, "%.6f\n", output[i]);
  	}
  	fclose(fp);
  	return 0;
}

/*-------------------------------------------
                  Main Functions
-------------------------------------------*/
int create_facenet(char *model_name, rknn_context *ctx, int &width, int &height, int &channel, rknn_input_output_num &io_num, unsigned char *model_data)
{
  	int            status     = 0;
  	int            ret;

  	/* Create the neural network */
  	printf("Loading facenet model...\n");
  	int model_data_size = 0;
  	model_data          = load_model(model_name, &model_data_size);
  	// 启用高优先级
  	uint32_t flag = RKNN_FLAG_PRIOR_HIGH;
  	ret = rknn_init(ctx, model_data, model_data_size, flag, NULL);
  	if (ret < 0) {
		printf("rknn_init error ret=%d\n", ret);
		return -1;
  	}
  	
  	rknn_core_mask core_mask = RKNN_NPU_CORE_0_1_2;  // 使用核心0、1、2
  	ret = rknn_set_core_mask(*ctx, core_mask);
  	if (ret < 0) {
		printf("rknn_set_core_mask error ret=%d\n", ret);
		return -1;
  	}

  	rknn_sdk_version version;
  	ret = rknn_query(*ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(rknn_sdk_version));
  	if (ret < 0) {
		printf("rknn_init error ret=%d\n", ret);
		return -1;
  	}
  	printf("sdk version: %s driver version: %s\n", version.api_version, version.drv_version);

  	ret = rknn_query(*ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
  	if (ret < 0) {
		printf("rknn_init error ret=%d\n", ret);
		return -1;
  	}
  	printf("model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);

  	rknn_tensor_attr input_attrs[io_num.n_input];
  	memset(input_attrs, 0, sizeof(input_attrs));
  	for (int i = 0; i < io_num.n_input; i++) {
		input_attrs[i].index = i;
		ret                  = rknn_query(*ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
		if (ret < 0) {
	  		printf("rknn_init error ret=%d\n", ret);
	  		return -1;
		}
		dump_tensor_attr(&(input_attrs[i]));
  	}

  	rknn_tensor_attr output_attrs[io_num.n_output];
  	memset(output_attrs, 0, sizeof(output_attrs));
  	for (int i = 0; i < io_num.n_output; i++) {
		output_attrs[i].index = i;
		ret                   = rknn_query(*ctx, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
		dump_tensor_attr(&(output_attrs[i]));
  	}

  	if (input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
		printf("model is NCHW input fmt\n");
		channel = input_attrs[0].dims[1];
		width   = input_attrs[0].dims[3];
		height  = input_attrs[0].dims[2];
  	} else {
		printf("model is NHWC input fmt\n");
		width   = input_attrs[0].dims[2];
		height  = input_attrs[0].dims[1];
		channel = input_attrs[0].dims[3];
  	}

  	printf("model input height=%d, width=%d, channel=%d\n", height, width, channel);
  	
  	return ret;
}

int facenet_inference(rknn_context *ctx, cv::Mat img, rknn_input_output_num io_num, rknn_input *inputs, rknn_output *outputs, float **result){
    int ret;

    // 将 uint8 图像转换为 float16，保持 0~255 范围，匹配模型输入 (qnt scale=1, zp=0)
    cv::Mat img_f16;
    img.convertTo(img_f16, CV_16FC3);  // 不缩放，保持原始像素值

    inputs[0].buf = (void*)img_f16.data;
    inputs[0].size = img_f16.total() * img_f16.elemSize();  // 按半精度尺寸更新 size

    ret = rknn_inputs_set(*ctx, io_num.n_input, inputs);

    ret = rknn_run(*ctx, NULL);
    ret = rknn_outputs_get(*ctx, io_num.n_output, outputs, NULL);
    result[0] = (float*)outputs[0].buf;

    l2_normalize(result[0]);

    return ret;
}

int facenet_output_release(rknn_context *ctx, rknn_input_output_num io_num, rknn_output *outputs)
{
	int ret;
	
	ret = rknn_outputs_release(*ctx, io_num.n_output, outputs);
	
	return ret;
}

void release_facenet(rknn_context *ctx, unsigned char *model_data)
{
	int ret;
  	// release
  	ret = rknn_destroy(*ctx);

  	if (model_data) {
		free(model_data);
  	}
}
