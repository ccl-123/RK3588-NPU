/**
 * @file postprocess.cc
 * @brief YOLOv8-face 后处理实现
 * @details 参考 rknn_model_zoo/examples/yolov8_pose/cpp/postprocess.cc
 */

#include "core/postprocess.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <algorithm>
#include <set>
#include <vector>

// ============================================
// 辅助函数
// ============================================

inline static int clamp(float val, int min, int max) {
    return val > min ? (val < max ? val : max) : min;
}

inline static int32_t __clip(float val, float min, float max) {
  float f = val <= min ? min : (val >= max ? max : val);
  return f;
}

static float sigmoid(float x) {
    return 1.0f / (1.0f + expf(-x));
}

static float unsigmoid(float y) {
    return -1.0f * logf((1.0f / y) - 1.0f);
}

// 量化/反量化函数
static int8_t qnt_f32_to_affine(float f32, int32_t zp, float scale) {
    float dst_val = (f32 / scale) + zp;
    int8_t res = (int8_t)__clip(dst_val, -128, 127);
  return res;
}

static float deqnt_affine_to_f32(int8_t qnt, int32_t zp, float scale) {
    return ((float)qnt - (float)zp) * scale;
}

// IoU 计算
static float CalculateOverlap(float xmin0, float ymin0, float xmax0, float ymax0,
                              float xmin1, float ymin1, float xmax1, float ymax1) {
    float w = fmax(0.f, fmin(xmax0, xmax1) - fmax(xmin0, xmin1) + 1.0f);
    float h = fmax(0.f, fmin(ymax0, ymax1) - fmax(ymin0, ymin1) + 1.0f);
    float i = w * h;
    float u = (xmax0 - xmin0 + 1.0f) * (ymax0 - ymin0 + 1.0f) + 
              (xmax1 - xmin1 + 1.0f) * (ymax1 - ymin1 + 1.0f) - i;
    return u <= 0.f ? 0.f : (i / u);
}

// Softmax 函数 (inplace)
static void softmax(float* input, int size) {
    float max_val = input[0];
    for (int i = 1; i < size; ++i) {
        if (input[i] > max_val) {
            max_val = input[i];
        }
    }

    float sum_exp = 0.0f;
    for (int i = 0; i < size; ++i) {
        sum_exp += expf(input[i] - max_val);
    }

    for (int i = 0; i < size; ++i) {
        input[i] = expf(input[i] - max_val) / sum_exp;
    }
}

// NMS
static int nms(int validCount, std::vector<float>& outputLocations,
               std::vector<int> classIds, std::vector<int>& order,
               int filterId, float threshold) {
    for (int i = 0; i < validCount; ++i) {
        int n = order[i];
        if (n == -1 || classIds[n] != filterId) {
            continue;
        }
        for (int j = i + 1; j < validCount; ++j) {
            int m = order[j];
            if (m == -1 || classIds[m] != filterId) {
                continue;
            }
            float xmin0 = outputLocations[n * 5 + 0];
            float ymin0 = outputLocations[n * 5 + 1];
            float xmax0 = outputLocations[n * 5 + 0] + outputLocations[n * 5 + 2];
            float ymax0 = outputLocations[n * 5 + 1] + outputLocations[n * 5 + 3];

            float xmin1 = outputLocations[m * 5 + 0];
            float ymin1 = outputLocations[m * 5 + 1];
            float xmax1 = outputLocations[m * 5 + 0] + outputLocations[m * 5 + 2];
            float ymax1 = outputLocations[m * 5 + 1] + outputLocations[m * 5 + 3];

            float iou = CalculateOverlap(xmin0, ymin0, xmax0, ymax0, xmin1, ymin1, xmax1, ymax1);

            if (iou > threshold) {
                order[j] = -1;
            }
        }
    }
    return 0;
}

// 快速排序 (降序)
static int quick_sort_indice_inverse(std::vector<float>& input, int left, int right,
                                     std::vector<int>& indices) {
    float key;
    int key_index;
    int low = left;
    int high = right;
    if (left < right) {
        key_index = indices[left];
        key = input[left];
        while (low < high) {
            while (low < high && input[high] <= key) {
                high--;
            }
            input[low] = input[high];
            indices[low] = indices[high];
            while (low < high && input[low] >= key) {
                low++;
            }
            input[high] = input[low];
            indices[high] = indices[low];
        }
        input[low] = key;
        indices[low] = key_index;
        quick_sort_indice_inverse(input, left, low - 1, indices);
        quick_sort_indice_inverse(input, low + 1, right, indices);
    }
    return low;
}

// ============================================
// 处理单个特征图 (int8 量化)
// ============================================
static int process_i8(int8_t* input, int grid_h, int grid_w, int stride,
                      std::vector<float>& boxes, std::vector<float>& boxScores,
                      std::vector<int>& classId, float threshold,
                      int32_t zp, float scale, int index) {
    int input_loc_len = 64;  // DFL: 4 * 16
    int validCount = 0;
    int8_t thres_i8 = qnt_f32_to_affine(unsigmoid(threshold), zp, scale);

    for (int h = 0; h < grid_h; h++) {
        for (int w = 0; w < grid_w; w++) {
            int offset = h * grid_w + w;
            // 置信度在第65通道
            int8_t conf_i8 = input[64 * grid_h * grid_w + offset];

            if (conf_i8 >= thres_i8) {
                float box_conf_f32 = sigmoid(deqnt_affine_to_f32(conf_i8, zp, scale));

                // 提取并反量化 DFL 数据
                std::vector<float> loc(input_loc_len);
                for (int i = 0; i < input_loc_len; ++i) {
                    loc[i] = deqnt_affine_to_f32(input[i * grid_h * grid_w + offset], zp, scale);
  }
  
                // DFL 解码
                for (int i = 0; i < 4; ++i) {
                    softmax(&loc[i * 16], 16);
                }

                float xywh_[4] = {0, 0, 0, 0};
                for (int dfl = 0; dfl < 16; ++dfl) {
                    xywh_[0] += loc[0 * 16 + dfl] * dfl;
                    xywh_[1] += loc[1 * 16 + dfl] * dfl;
                    xywh_[2] += loc[2 * 16 + dfl] * dfl;
                    xywh_[3] += loc[3 * 16 + dfl] * dfl;
                }

                float x1_grid = (w + 0.5f) - xywh_[0];
                float y1_grid = (h + 0.5f) - xywh_[1];
                float x2_grid = (w + 0.5f) + xywh_[2];
                float y2_grid = (h + 0.5f) + xywh_[3];

                float cx = ((x1_grid + x2_grid) / 2) * stride;
                float cy = ((y1_grid + y2_grid) / 2) * stride;
                float bw = (x2_grid - x1_grid) * stride;
                float bh = (y2_grid - y1_grid) * stride;
                float x1 = cx - bw / 2;
                float y1 = cy - bh / 2;

                boxes.push_back(x1);
                boxes.push_back(y1);
                boxes.push_back(bw);
                boxes.push_back(bh);
                boxes.push_back(float(index + h * grid_w + w));

                boxScores.push_back(box_conf_f32);
                classId.push_back(0);
                validCount++;
            }
        }
    }

    return validCount;
}


// ============================================
// YOLOv8-face 主后处理函数
// ============================================
int post_process_yolov8_face(rknn_output* outputs, rknn_tensor_attr* output_attrs, int n_output,
                             int model_in_h, int model_in_w,
                             float conf_threshold, float nms_threshold,
                             float scale_w, float scale_h,
                             detect_result_group_t* group) {
    
    memset(group, 0, sizeof(detect_result_group_t));

    if (n_output != 4) {
        printf("Error: Expected 4 outputs for YOLOv8-face, got %d\n", n_output);
        return -1;
    }

    std::vector<float> filterBoxes;
    std::vector<float> objProbs;
    std::vector<int> classId;
    int validCount = 0;
    int index = 0;

    // 处理前3个输出 (bbox + conf)，强制使用 INT8 路径
    for (int i = 0; i < 3; i++) {
        int grid_h = output_attrs[i].dims[2];
        int grid_w = output_attrs[i].dims[3];
        int stride = model_in_h / grid_h;

        if (output_attrs[i].type != RKNN_TENSOR_INT8) {
            printf("Error: YOLO output %d not INT8 (type=%d)\n", i, output_attrs[i].type);
            return -1;
        }

            validCount += process_i8((int8_t*)outputs[i].buf, grid_h, grid_w, stride,
                                     filterBoxes, objProbs, classId, conf_threshold,
                                     output_attrs[i].zp, output_attrs[i].scale, index);
        index += grid_h * grid_w;
    }

    // 没有检测到目标
  if (validCount <= 0) {
    return 0;
  }

    // 排序
    std::vector<int> indexArray;
    for (int i = 0; i < validCount; ++i) {
        indexArray.push_back(i);
    }
    quick_sort_indice_inverse(objProbs, 0, validCount - 1, indexArray);

    // NMS
    std::set<int> class_set(std::begin(classId), std::end(classId));
    for (auto c : class_set) {
        nms(validCount, filterBoxes, classId, indexArray, c, nms_threshold);
    }

    // 获取关键点输出 - 格式: [1, 5, 3, 8400]
    float* kpt_output = (float*)outputs[3].buf;
    int32_t kpt_zp = output_attrs[3].zp;
    float kpt_scale = output_attrs[3].scale;
    // 检查关键点是否也是 float (want_float=1 时)
    bool kpt_is_float = (outputs[3].size == output_attrs[3].n_elems * sizeof(float));

    // 提取结果
  int last_count = 0;
    group->count = 0;

  for (int i = 0; i < validCount; ++i) {
        if (indexArray[i] == -1 || last_count >= OBJ_NUMB_MAX_SIZE) {
      continue;
    }

        int n = indexArray[i];
        float x1 = filterBoxes[n * 5 + 0];
        float y1 = filterBoxes[n * 5 + 1];
        float w = filterBoxes[n * 5 + 2];
        float h = filterBoxes[n * 5 + 3];
        int kpt_index = (int)filterBoxes[n * 5 + 4];

        // 获取 5 个关键点 - 输出格式: [1, 5, 3, 8400]
        float kpts[5][3];  // 5个点，每个点 (x, y, visibility)
        for (int j = 0; j < 5; ++j) {
            if (kpt_is_float) {
                // want_float=1，数据已经是 float
                kpts[j][0] = kpt_output[j * 3 * 8400 + 0 * 8400 + kpt_index];
                kpts[j][1] = kpt_output[j * 3 * 8400 + 1 * 8400 + kpt_index];
                kpts[j][2] = kpt_output[j * 3 * 8400 + 2 * 8400 + kpt_index];
            } else {
                // 原始 INT8，需要反量化
                int8_t* kpt_i8 = (int8_t*)outputs[3].buf;
                kpts[j][0] = deqnt_affine_to_f32(kpt_i8[j * 3 * 8400 + 0 * 8400 + kpt_index], kpt_zp, kpt_scale);
                kpts[j][1] = deqnt_affine_to_f32(kpt_i8[j * 3 * 8400 + 1 * 8400 + kpt_index], kpt_zp, kpt_scale);
                kpts[j][2] = deqnt_affine_to_f32(kpt_i8[j * 3 * 8400 + 2 * 8400 + kpt_index], kpt_zp, kpt_scale);
            }
        }

        // 坐标转换 (模型坐标 -> 原图坐标)
    group->results[last_count].box.left   = (int)(clamp(x1, 0, model_in_w) / scale_w);
    group->results[last_count].box.top    = (int)(clamp(y1, 0, model_in_h) / scale_h);
        group->results[last_count].box.right  = (int)(clamp(x1 + w, 0, model_in_w) / scale_w);
        group->results[last_count].box.bottom = (int)(clamp(y1 + h, 0, model_in_h) / scale_h);
        group->results[last_count].prop = objProbs[i];

        // 关键点坐标转换
        group->results[last_count].point.point_1_x = (int)(clamp(kpts[0][0], 0, model_in_w) / scale_w);
        group->results[last_count].point.point_1_y = (int)(clamp(kpts[0][1], 0, model_in_h) / scale_h);
        group->results[last_count].point.point_2_x = (int)(clamp(kpts[1][0], 0, model_in_w) / scale_w);
        group->results[last_count].point.point_2_y = (int)(clamp(kpts[1][1], 0, model_in_h) / scale_h);
        group->results[last_count].point.point_3_x = (int)(clamp(kpts[2][0], 0, model_in_w) / scale_w);
        group->results[last_count].point.point_3_y = (int)(clamp(kpts[2][1], 0, model_in_h) / scale_h);
        group->results[last_count].point.point_4_x = (int)(clamp(kpts[3][0], 0, model_in_w) / scale_w);
        group->results[last_count].point.point_4_y = (int)(clamp(kpts[3][1], 0, model_in_h) / scale_h);
        group->results[last_count].point.point_5_x = (int)(clamp(kpts[4][0], 0, model_in_w) / scale_w);
        group->results[last_count].point.point_5_y = (int)(clamp(kpts[4][1], 0, model_in_h) / scale_h);

        strncpy(group->results[last_count].name, "face", OBJ_NAME_MAX_SIZE);
    last_count++;
  }

  group->count = last_count;
  return 0;
}

// ============================================
// 人脸对齐辅助函数
// ============================================

cv::Mat meanAxis0(const cv::Mat& src) {
        int num = src.rows;
        int dim = src.cols;
    cv::Mat output(1, dim, CV_32F);
    for (int i = 0; i < dim; i++) {
        float sum = 0;
        for (int j = 0; j < num; j++) {
            sum += src.at<float>(j, i);
            }
        output.at<float>(0, i) = sum / num;
        }
        return output;
}

cv::Mat elementwiseMinus(const cv::Mat& A, const cv::Mat& B) {
    cv::Mat output(A.rows, A.cols, A.type());
        assert(B.cols == A.cols);
    if (B.cols == A.cols) {
        for (int i = 0; i < A.rows; i++) {
            for (int j = 0; j < B.cols; j++) {
                output.at<float>(i, j) = A.at<float>(i, j) - B.at<float>(0, j);
                }
            }
        }
        return output;
}

cv::Mat varAxis0(const cv::Mat& src) {
    cv::Mat temp_ = elementwiseMinus(src, meanAxis0(src));
    cv::multiply(temp_, temp_, temp_);
        return meanAxis0(temp_);
}

int MatrixRank(cv::Mat M) {
	cv::Mat w, u, vt;
	cv::SVD::compute(M, w, u, vt);
	cv::Mat1b nonZeroSingularValues = w > 0.0001;
    int rank = cv::countNonZero(nonZeroSingularValues);
        return rank;
}

cv::Mat similarTransform(cv::Mat src, cv::Mat dst) {
        int num = src.rows;
        int dim = src.cols;
        cv::Mat src_mean = meanAxis0(src);
        cv::Mat dst_mean = meanAxis0(dst);
        cv::Mat src_demean = elementwiseMinus(src, src_mean);
        cv::Mat dst_demean = elementwiseMinus(dst, dst_mean);
        cv::Mat A = (dst_demean.t() * src_demean) / static_cast<float>(num);
        cv::Mat d(dim, 1, CV_32F);
        d.setTo(1.0f);
        if (cv::determinant(A) < 0) {
            d.at<float>(dim - 1, 0) = -1;
        }
	cv::Mat T = cv::Mat::eye(dim + 1, dim + 1, CV_32F);
        cv::Mat U, S, V;
    cv::SVD::compute(A, S, U, V);

        int rank = MatrixRank(A);
        if (rank == 0) {
            assert(rank == 0);
        } else if (rank == dim - 1) {
            if (cv::determinant(U) * cv::determinant(V) > 0) {
                T.rowRange(0, dim).colRange(0, dim) = U * V;
            } else {
                int s = d.at<float>(dim - 1, 0) = -1;
                d.at<float>(dim - 1, 0) = -1;
                T.rowRange(0, dim).colRange(0, dim) = U * V;
                cv::Mat diag_ = cv::Mat::diag(d);
            cv::Mat twp = diag_ * V;
            T.rowRange(0, dim).colRange(0, dim) = U * twp;
                d.at<float>(dim - 1, 0) = s;
        }
    } else {
            cv::Mat diag_ = cv::Mat::diag(d);
        cv::Mat twp = diag_ * V.t();
        cv::Mat res = U * twp;
        T.rowRange(0, dim).colRange(0, dim) = -U.t() * twp;
        }
        cv::Mat var_ = varAxis0(src_demean);
        float val = cv::sum(var_).val[0];
        cv::Mat res;
    cv::multiply(d, S, res);
    float scale = 1.0 / val * cv::sum(res).val[0];
    T.rowRange(0, dim).colRange(0, dim) = -T.rowRange(0, dim).colRange(0, dim).t();
    cv::Mat temp1 = T.rowRange(0, dim).colRange(0, dim);
    cv::Mat temp2 = src_mean.t();
    cv::Mat temp3 = temp1 * temp2;
    cv::Mat temp4 = scale * temp3;
    T.rowRange(0, dim).colRange(dim, dim + 1) = -(temp4 - dst_mean.t());
        T.rowRange(0, dim).colRange(0, dim) *= scale;
        return T;
}

// ============================================
// 特征比较函数
// ============================================

static float eu_distance(float* input) {
    float sum = 0;
    for (int i = 0; i < FACENET_FEATURE_DIM; ++i) {
        sum = sum + input[i] * input[i];
    }
    return sqrt(sum);
}

void l2_normalize(float* input) {
	float sum = 0;
    for (int i = 0; i < FACENET_FEATURE_DIM; ++i) {
		sum = sum + input[i] * input[i];
	}
	sum = sqrt(sum);
    for (int i = 0; i < FACENET_FEATURE_DIM; ++i) {
		input[i] = input[i] / sum;
	}
}

float compare_eu_distance(float* input1, float* input2) {
	float sum = 0;
    for (int i = 0; i < FACENET_FEATURE_DIM; ++i) {
		sum = sum + (input1[i] - input2[i]) * (input1[i] - input2[i]);
	}
    return sqrt(sum);
}

float cos_similarity(float* input1, float* input2) {
	float sum = 0;
    for (int i = 0; i < FACENET_FEATURE_DIM; ++i) {
		sum = sum + input1[i] * input2[i];
	}
	float tmp1 = eu_distance(input1);
	float tmp2 = eu_distance(input2);
	return sum / (tmp1 * tmp2);
}

void deinitPostProcess() {
    // 清理资源
}
