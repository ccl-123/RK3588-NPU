/**
 * @file feature_library.cc
 * @brief 人脸特征库管理器实现
 * @author CL
 * @date 2025-11-20
 */

#include "app/feature_library.h"
#include "core/postprocess.h"
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <cstring>

FeatureLibrary::FeatureLibrary()
    : feature_dim_(512)
{
}

FeatureLibrary::~FeatureLibrary() {
    clear();
}

int FeatureLibrary::load_from_directory(const std::string& lib_path, int feature_dim) {
    feature_dim_ = feature_dim;
    clear();  // 清空现有数据

    DIR* pDir = opendir(lib_path.c_str());
    if (!pDir) {
        std::cerr << "Feature library directory doesn't exist: " << lib_path << std::endl;
        return -1;
    }

    struct dirent* ptr;
    int count = 0;

    while ((ptr = readdir(pDir)) != nullptr) {
        if (strcmp(ptr->d_name, ".") == 0 || strcmp(ptr->d_name, "..") == 0) {
            continue;
        }

        std::string file_path = lib_path + "/" + ptr->d_name;
        std::ifstream infile(file_path);
        if (!infile.is_open()) {
            std::cerr << "Failed to open feature file: " << file_path << std::endl;
            continue;
        }

        // 读取特征向量
        float* feature = new float[feature_dim_];
        std::string line;
        int i = 0;

        while (std::getline(infile, line) && i < feature_dim_) {
            feature[i] = atof(line.c_str());
            i++;
        }
        infile.close();

        if (i != feature_dim_) {
            std::cerr << "Warning: Feature dimension mismatch in " << file_path 
                      << " (expected " << feature_dim_ << ", got " << i << ")" << std::endl;
            delete[] feature;
            continue;
        }

        // 提取人名 (去掉文件扩展名)
        std::string name = ptr->d_name;
        size_t dot_pos = name.find_last_of(".");
        if (dot_pos != std::string::npos) {
            name = name.substr(0, dot_pos);
        }

        lib_feature_.push_back(feature);
        lib_face_name_.push_back(name);
        count++;
    }

    closedir(pDir);

    std::cout << "Loaded " << count << " face features from " << lib_path << std::endl;
    return count;
}

bool FeatureLibrary::match_feature(const float* feature, float threshold,
                                   std::string& matched_name, float& max_score) {
    max_score = 0.0f;
    matched_name = "stranger";
    bool found = false;

    for (size_t i = 0; i < lib_feature_.size(); i++) {
        float similarity = compute_cosine_similarity(feature, lib_feature_[i]);
        
        if (similarity >= threshold && similarity > max_score) {
            max_score = similarity;
            matched_name = lib_face_name_[i];
            found = true;
        }
    }

    return found;
}

void FeatureLibrary::clear() {
    for (auto feature : lib_feature_) {
        delete[] feature;
    }
    lib_feature_.clear();
    lib_face_name_.clear();
}

float FeatureLibrary::compute_cosine_similarity(const float* feature1, const float* feature2) const {
    // 使用 postprocess.h 中的 cos_similarity 函数
    return cos_similarity(const_cast<float*>(feature1), const_cast<float*>(feature2));
}
