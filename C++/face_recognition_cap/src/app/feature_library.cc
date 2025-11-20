/**
 * @file feature_library.cc
 * @brief 人脸特征库管理器实现(支持数据库)
 * @author CL
 * @date 2025-11-20
 */

#include "app/feature_library.h"
#include "core/postprocess.h"
#include "database/database_manager.h"
#include "database/face_feature_dao.h"
#include "database/user_dao.h"
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <cstring>

FeatureLibrary::FeatureLibrary()
    : feature_dim_(512)
    , db_manager_(nullptr)
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

int FeatureLibrary::load_from_database(db::DatabaseManager* db_manager, int feature_dim) {
    if (!db_manager || !db_manager->is_initialized()) {
        std::cerr << "Database manager not initialized" << std::endl;
        return -1;
    }

    feature_dim_ = feature_dim;
    db_manager_ = db_manager;
    clear();  // 清空现有数据

    // 创建DAO对象
    db::FaceFeatureDAO feature_dao(db_manager);
    db::UserDAO user_dao(db_manager);

    // 查询所有启用用户的特征
    auto features = feature_dao.find_all_active();

    if (features.empty()) {
        std::cerr << "No face features found in database" << std::endl;
        return 0;
    }

    int count = 0;
    for (const auto& feature : features) {
        // 查询用户信息
        db::UserInfo user;
        if (!user_dao.find_by_id(feature.user_id, user)) {
            std::cerr << "Warning: User not found for feature_id: " << feature.feature_id << std::endl;
            continue;
        }

        // 复制特征向量到内存
        float* feature_copy = new float[feature_dim_];
        std::memcpy(feature_copy, feature.feature_vector.data(), feature_dim_ * sizeof(float));

        lib_feature_.push_back(feature_copy);
        lib_face_name_.push_back(user.user_name);
        lib_user_ids_.push_back(user.user_id);
        count++;
    }

    std::cout << "Loaded " << count << " face features from database" << std::endl;
    return count;
}

bool FeatureLibrary::match_feature(const float* feature, float threshold,
                                   std::string& matched_name, float& max_score) {
    int user_id = 0;
    return match_feature_with_id(feature, threshold, user_id, matched_name, max_score);
}

bool FeatureLibrary::match_feature_with_id(const float* feature, float threshold,
                                           int& user_id, std::string& matched_name, float& max_score) {
    max_score = 0.0f;
    matched_name = "stranger";
    user_id = 0;
    bool found = false;

    for (size_t i = 0; i < lib_feature_.size(); i++) {
        float similarity = compute_cosine_similarity(feature, lib_feature_[i]);

        if (similarity >= threshold && similarity > max_score) {
            max_score = similarity;
            matched_name = lib_face_name_[i];
            user_id = lib_user_ids_[i];
            found = true;
        }
    }

    return found;
}

bool FeatureLibrary::add_feature(int user_id, const std::string& name, const float* feature) {
    if (!feature) return false;

    // 复制特征向量
    float* feature_copy = new float[feature_dim_];
    std::memcpy(feature_copy, feature, feature_dim_ * sizeof(float));

    lib_feature_.push_back(feature_copy);
    lib_face_name_.push_back(name);
    lib_user_ids_.push_back(user_id);

    return true;
}

bool FeatureLibrary::remove_feature(int user_id) {
    bool removed = false;

    for (size_t i = 0; i < lib_user_ids_.size(); ) {
        if (lib_user_ids_[i] == user_id) {
            // 释放内存
            delete[] lib_feature_[i];

            // 删除元素
            lib_feature_.erase(lib_feature_.begin() + i);
            lib_face_name_.erase(lib_face_name_.begin() + i);
            lib_user_ids_.erase(lib_user_ids_.begin() + i);

            removed = true;
            // 不增加i，因为删除后元素前移
        } else {
            i++;
        }
    }

    return removed;
}

void FeatureLibrary::clear() {
    for (auto feature : lib_feature_) {
        delete[] feature;
    }
    lib_feature_.clear();
    lib_face_name_.clear();
    lib_user_ids_.clear();
}

float FeatureLibrary::compute_cosine_similarity(const float* feature1, const float* feature2) const {
    // 使用 postprocess.h 中的 cos_similarity 函数
    return cos_similarity(const_cast<float*>(feature1), const_cast<float*>(feature2));
}
