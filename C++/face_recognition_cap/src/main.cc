// Copyright (c) 2021 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#define _BASETSD_H

#include "RgaUtils.h"
#include "im2d.h"
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include "postprocess.h"
#include "retinaface.h"
#include "facenet.h"
#include "camera_util.h"
#include "rga.h"
#include "rknn_api.h"

#define PERF_WITH_POST 1
#define WIDTH  1280  // 罗技C720摄像头分辨率
#define HEIGHT 720

double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }

/*-------------------------------------------
                  Main Functions
-------------------------------------------*/
int main(int argc, char** argv)
{
  	char*          retinaface_model_name = NULL;
  	rknn_context   retinaface_ctx;
  	int            retinaface_width      = 0;
  	int            retinaface_height     = 0;
  	int            retinaface_channel    = 0;
  	std::vector<float> retinaface_out_scales;
  	std::vector<int32_t> retinaface_out_zps;
  	rknn_input_output_num retinaface_io_num;
  	static unsigned char *retinaface_model_data;
  	
  	float dst_landmark[5][2] = {{54.7065, 73.8519},
				    {105.0454, 73.5734},
				    {80.036, 102.4808},
				    {59.3561, 131.9507},
				    {89.6141, 131.7201}};
	cv::Mat dst(5, 2, CV_32FC1, dst_landmark);
	memcpy(dst.data, dst_landmark, 2 * 5 * sizeof(float));
  	
  	char*          facenet_model_name = NULL;
  	rknn_context   facenet_ctx;
  	int            facenet_width      = 0;
  	int            facenet_height     = 0;
  	int            facenet_channel    = 0;
  	rknn_input_output_num facenet_io_num;
  	static unsigned char *facenet_model_data;
  	float *facenet_result;

  	const float    nms_threshold      = NMS_THRESH;
  	const float    box_conf_threshold = BOX_THRESH;
  	const float    facenet_threshold  = FACENET_THRESH;
  	struct timeval start_time, stop_time;
  	int            ret;

  	if (argc != 5) {
		printf("Usage: %s <retinaface model> <facenet model> <usb or mipi> <device number> \n", argv[0]);
		return -1;
  	}

  	printf("post process config: box_conf_threshold = %.2f, nms_threshold = %.2f\n", box_conf_threshold, nms_threshold);

  	retinaface_model_name = (char*)argv[1];
  	facenet_model_name = (char*)argv[2];
  	std::string camera_type = argv[3];
  	std::string device_number = argv[4];
  	
  	create_retinaface(retinaface_model_name, &retinaface_ctx, retinaface_width, retinaface_height, retinaface_channel, retinaface_out_scales, retinaface_out_zps, retinaface_io_num, retinaface_model_data);
  	create_facenet(facenet_model_name, &facenet_ctx, facenet_width, facenet_height, facenet_channel, facenet_io_num, facenet_model_data);
  	
  	rknn_input retinaface_inputs[1];
  	memset(retinaface_inputs, 0, sizeof(retinaface_inputs));
  	retinaface_inputs[0].index        = 0;
  	retinaface_inputs[0].type         = RKNN_TENSOR_UINT8;
  	retinaface_inputs[0].size         = retinaface_width * retinaface_height * retinaface_channel;
  	retinaface_inputs[0].fmt          = RKNN_TENSOR_NHWC;
  	retinaface_inputs[0].pass_through = 0;
  	
  	rknn_output retinaface_outputs[retinaface_io_num.n_output];
  	memset(retinaface_outputs, 0, sizeof(retinaface_outputs));
  	for (int i = 0; i < retinaface_io_num.n_output; i++) {
  		if (i != 1)
  		{
			retinaface_outputs[i].want_float = 0;
		}
		else
		{
			retinaface_outputs[i].want_float = 1;
		}
  	}
  	
  	rknn_input facenet_inputs[1];
  	memset(facenet_inputs, 0, sizeof(facenet_inputs));
  	facenet_inputs[0].index        = 0;
  	facenet_inputs[0].type         = RKNN_TENSOR_UINT8;
  	facenet_inputs[0].size         = facenet_width * facenet_height * facenet_channel;
  	facenet_inputs[0].fmt          = RKNN_TENSOR_NHWC;
  	facenet_inputs[0].pass_through = 0;
  	
  	rknn_output facenet_outputs[facenet_io_num.n_output];
  	memset(facenet_outputs, 0, sizeof(facenet_outputs));
  	for (int i = 0; i < facenet_io_num.n_output; i++) {
		facenet_outputs[i].want_float = 1;
  	}
  	
  	cv::namedWindow("Image Window");
  	cv::Mat orig_img;
	cv::Mat img;

	// 优化: USB摄像头使用异步读取模式
	bool use_async_usb = true;  // 设置为false可降级到串行模式

	if (camera_type == "usb") {
		if (use_async_usb) {
			ret = load_usb_camera_async(device_number, WIDTH, HEIGHT);
			if (ret == EXIT_SUCCESS) {
				start_usb_capture_thread();
				printf("USB camera async mode enabled\n");
			}
		} else {
			ret = load_usb_camera(device_number, WIDTH, HEIGHT);
		}
	}
	else if (camera_type == "mipi") {
		ret = load_mipi_camera(device_number, WIDTH, HEIGHT);
	}
	else {
		std::cout << "Unsupport camera type : " << camera_type << " !!!" << std::endl;
	}
  	
  	float scale_w, scale_h;
	int resize_w, resize_h, padding;
	if (WIDTH > HEIGHT) {
		scale_w = (float)retinaface_width / WIDTH;
		scale_h = scale_w;
		resize_w = retinaface_width;
		resize_h = (int)(resize_w * HEIGHT / WIDTH);
		padding = resize_w - resize_h;
	}
	else {
		scale_h = (float)retinaface_height / HEIGHT;
		scale_w = scale_h;
		resize_h = retinaface_height;
		resize_w = (int)(resize_h * WIDTH / HEIGHT);
		padding = resize_h - resize_w;
	}

  	std::vector<float>    out_scales;
  	std::vector<int32_t>  out_zps;
  	char text[256];

	int x1,y1,x2,y2;
	
	std::string face_lib = "./data/face_feature_lib/";
	DIR *pDir;
	struct dirent *ptr;
	if (!(pDir = opendir(face_lib.c_str())))
	{
		printf("Feature library doesn't Exist!\n");
		return -1;
	}
	
	std::vector<float*> lib_feature;
	std::vector<std::string> lib_face_name;
	while ((ptr = readdir(pDir)) != 0)
	{
		if (strcmp(ptr->d_name, ".") != 0 && strcmp(ptr->d_name, "..") != 0)
		{
			std::ifstream infile(face_lib + ptr->d_name);
			std::string tmp;

			float* tmp_lib_feature = new float[FACENET_FEATURE_DIM];

			int i = 0;
			while (getline(infile, tmp))
			{
				tmp_lib_feature[i] = atof(tmp.c_str());
				i++;
			}
			infile.close();
			
			lib_face_name.push_back(((std::string)ptr->d_name).substr(0, ((std::string)ptr->d_name).find_last_of(".")));
			lib_feature.push_back(tmp_lib_feature);
		}
	}
	
	float total_time = 0;
	int n = 0;

	// 性能分析变量
	struct timeval t1, t2, t3, t4, t5, t6, t7, t8;
	float time_camera = 0, time_flip = 0, time_resize = 0, time_retinaface = 0;
	float time_align = 0, time_facenet = 0, time_match = 0, time_display = 0;

  	while(1){
		gettimeofday(&start_time, NULL);

		// 1. 摄像头读取
		gettimeofday(&t1, NULL);
		if (camera_type == "usb") {
			if (use_async_usb) {
				read_usb_frame_async(&orig_img);  // 异步读取,无阻塞
			} else {
				read_usb_frame(&orig_img);  // 同步读取
			}
		}
		else if (camera_type == "mipi") {
			read_mipi_frame(&orig_img);
		}
		gettimeofday(&t2, NULL);

		// 2. 图像翻转 (先翻转原图,确保坐标系一致)
		cv::flip(orig_img, orig_img, 1);
		gettimeofday(&t3, NULL);

		// 3. 图像缩放 (使用RGA硬件加速)
		// 优化: 使用RGA替换OpenCV的resize

		// 分配目标图像内存
		if (img.empty() || img.cols != resize_w || img.rows != resize_h) {
			img = cv::Mat(resize_h, resize_w, CV_8UC3);
		}

		// 使用RGA硬件加速进行缩放
		rga_buffer_t src_buf = wrapbuffer_virtualaddr(orig_img.data, WIDTH, HEIGHT, RK_FORMAT_BGR_888);
		rga_buffer_t dst_buf = wrapbuffer_virtualaddr(img.data, resize_w, resize_h, RK_FORMAT_BGR_888);

		im_rect src_rect = {0, 0, WIDTH, HEIGHT};
		im_rect dst_rect = {0, 0, resize_w, resize_h};
		im_rect pat_rect = {0, 0, 0, 0};

		// 使用improcess进行缩放(不翻转)
		rga_buffer_t pat_buf = {};
		IM_STATUS status = improcess(src_buf, dst_buf, pat_buf, src_rect, dst_rect, pat_rect, 0);
		if (status != IM_STATUS_SUCCESS) {
			printf("RGA improcess failed: %s\n", imStrError(status));
			// 降级到OpenCV实现
			cv::resize(orig_img, img, cv::Size(resize_w, resize_h), 0, 0, cv::INTER_LINEAR);
		}

		gettimeofday(&t4, NULL);
		// 4. RetinaFace 人脸检测
		detect_result_group_t retinaface_detect_result_group;
		if (WIDTH > HEIGHT) {
			cv::copyMakeBorder(img, img, 0, padding, 0, 0, cv::BorderTypes::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
			retinaface_inference(&retinaface_ctx, img, retinaface_width, retinaface_height, retinaface_channel, box_conf_threshold, nms_threshold, WIDTH, WIDTH, retinaface_io_num, retinaface_inputs, retinaface_outputs, retinaface_out_scales, retinaface_out_zps, &retinaface_detect_result_group);
		}
		else{
			cv::copyMakeBorder(img, img, 0, 0, 0, padding, cv::BorderTypes::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
			retinaface_inference(&retinaface_ctx, img, retinaface_width, retinaface_height, retinaface_channel, box_conf_threshold, nms_threshold, HEIGHT, HEIGHT, retinaface_io_num, retinaface_inputs, retinaface_outputs, retinaface_out_scales, retinaface_out_zps, &retinaface_detect_result_group);
		}
		gettimeofday(&t5, NULL);

  		for (int i = 0; i < retinaface_detect_result_group.count; i++) {
			// 5. 人脸对齐
			gettimeofday(&t6, NULL);
			float landmark[5][2] = {{(float)retinaface_detect_result_group.results[i].point.point_1_x, (float)retinaface_detect_result_group.results[i].point.point_1_y},
						{(float)retinaface_detect_result_group.results[i].point.point_2_x, (float)retinaface_detect_result_group.results[i].point.point_2_y},
						{(float)retinaface_detect_result_group.results[i].point.point_3_x, (float)retinaface_detect_result_group.results[i].point.point_3_y},
						{(float)retinaface_detect_result_group.results[i].point.point_4_x, (float)retinaface_detect_result_group.results[i].point.point_4_y},
						{(float)retinaface_detect_result_group.results[i].point.point_5_x, (float)retinaface_detect_result_group.results[i].point.point_5_y}};

			cv::Mat src(5, 2, CV_32FC1, landmark);
			memcpy(src.data, landmark, 2 * 5 * sizeof(float));

			cv::Mat M = similarTransform(src, dst);
			cv::Mat warp;
			cv::warpPerspective(orig_img, warp, M, cv::Size(facenet_width, facenet_height));
			cv::cvtColor(warp, warp, cv::COLOR_BGR2RGB);
			gettimeofday(&t7, NULL);

			// 6. FaceNet 特征提取
			facenet_inference(&facenet_ctx, warp, facenet_io_num, facenet_inputs, facenet_outputs, &facenet_result);
			gettimeofday(&t8, NULL);

			// 7. 特征匹配
			struct timeval t_match_start, t_match_end;
			gettimeofday(&t_match_start, NULL);
			float max_score = 0;
			std::string name = "stranger";
			for (int i = 0; i < lib_feature.size(); i++)
  			{
				float cos_similar;

				cos_similar = cos_similarity(facenet_result, lib_feature[i]);
				if (cos_similar >= facenet_threshold && cos_similar > max_score)
				{
					max_score = cos_similar;
					name = lib_face_name[i];
				}
  			}
			gettimeofday(&t_match_end, NULL);
			time_match += (__get_us(t_match_end) - __get_us(t_match_start)) / 1000;

  			facenet_output_release(&facenet_ctx, facenet_io_num, facenet_outputs);

  			int x1 = retinaface_detect_result_group.results[i].box.left;
			int y1 = retinaface_detect_result_group.results[i].box.top;
			int x2 = retinaface_detect_result_group.results[i].box.right;
			int y2 = retinaface_detect_result_group.results[i].box.bottom;

			rectangle(orig_img, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(255, 0, 0, 255), 1);
			putText(orig_img, name, cv::Point(x1, y1 + 12), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 0, 0));

			// 累计人脸对齐和特征提取时间
			time_align += (__get_us(t7) - __get_us(t6)) / 1000;
			time_facenet += (__get_us(t8) - __get_us(t7)) / 1000;
  		}

		// 8. 显示渲染
		struct timeval t_display_start, t_display_end;
		gettimeofday(&t_display_start, NULL);

		// 计算当前帧FPS
		gettimeofday(&stop_time, NULL);
		float current_frame_time = (__get_us(stop_time) - __get_us(start_time)) / 1000.0;
		float current_fps = 1000.0 / current_frame_time;

		// 在画面上显示FPS (左上角)
		char fps_text[64];
		snprintf(fps_text, sizeof(fps_text), "FPS: %.1f (%.1f ms)", current_fps, current_frame_time);
		cv::putText(orig_img, fps_text, cv::Point(10, 30),
		            cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);

		// 显示优化状态
		cv::putText(orig_img, "RGA+Async Optimized", cv::Point(10, 60),
		            cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 2);

  		cv::imshow("Image Window",orig_img);
		cv::waitKey(1);
		gettimeofday(&t_display_end, NULL);

		// 累计各阶段时间
		time_camera += (__get_us(t2) - __get_us(t1)) / 1000;
		time_flip += (__get_us(t3) - __get_us(t2)) / 1000;  // 图像翻转(OpenCV)
		time_resize += (__get_us(t4) - __get_us(t3)) / 1000;  // 图像缩放(RGA硬件加速)
		time_retinaface += (__get_us(t5) - __get_us(t4)) / 1000;
		time_display += (__get_us(t_display_end) - __get_us(t_display_start)) / 1000;

		total_time += (__get_us(stop_time) - __get_us(start_time)) / 1000;
		n++;

		if (n == 10)
		{
			printf("\n========== 性能分析 (平均 10 帧) ==========\n");
			printf("1. 摄像头读取:    %6.2f ms (异步)\n", time_camera / 10);
			printf("2. 图像翻转:      %6.2f ms (OpenCV)\n", time_flip / 10);
			printf("3. 图像缩放:      %6.2f ms (RGA硬件加速)\n", time_resize / 10);
			printf("4. RetinaFace:    %6.2f ms (人脸检测)\n", time_retinaface / 10);
			printf("5. 人脸对齐:      %6.2f ms\n", time_align / 10);
			printf("6. FaceNet:       %6.2f ms (512维特征提取)\n", time_facenet / 10);
			printf("7. 特征匹配:      %6.2f ms\n", time_match / 10);
			printf("8. 显示渲染:      %6.2f ms\n", time_display / 10);
			printf("-------------------------------------------\n");
			printf("总耗时:           %6.2f ms (%.1f FPS)\n", total_time / 10, 10000.0 / total_time);
			printf("===========================================\n\n");

			// 重置计数器
			total_time = 0;
			time_camera = 0;
			time_flip = 0;
			time_resize = 0;
			time_retinaface = 0;
			time_align = 0;
			time_facenet = 0;
			time_match = 0;
			time_display = 0;
			n = 0;
		}
  	}
  	if (camera_type == "usb") {
		if (use_async_usb) {
			close_usb_camera_async();
		} else {
			close_usb_camera();
		}
	}
	else if (camera_type == "mipi") {
		close_mipi_camera();
	}

	release_retinaface(&retinaface_ctx, retinaface_model_data);
	release_facenet(&facenet_ctx, facenet_model_data);
  	return 0;
}
