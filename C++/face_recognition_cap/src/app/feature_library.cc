/**
 * @file feature_library.cc
 * @brief 人脸特征库管理器实现(支持数据库)
 * @author CL
 * @date 2025-11-20
 *
 * 优化说明：
 * - 使用 std::vector<float> 替代 float* 原始指针，自动内存管理
 * - 添加 shared_mutex 实现线程安全的读写锁
 * - 统一使用 spdlog 日志系统
 */

#include "app/feature_library.h"
#include "core/postprocess.h"
#include "database/database_manager.h"
#include "database/face_feature_dao.h"
#include "database/user_dao.h"
#include <spdlog/spdlog.h>
#include <dirent.h>
#include <fstream>
#include <cstring>

FeatureLibrary::FeatureLibrary()
    : feature_dim_(Config::Model::FEATURE_DIM)
    , db_manager_(nullptr)
{
}

FeatureLibrary::~FeatureLibrary() {
    clear();
}

int FeatureLibrary::load_from_directory(const std::string& lib_path, int feature_dim) {
    std::unique_lock<std::shared_mutex> lock(mutex_);  // 写锁

    feature_dim_ = feature_dim;
    lib_feature_.clear();
    lib_face_name_.clear();
    lib_user_ids_.clear();

    DIR* pDir = opendir(lib_path.c_str());
    if (!pDir) {
        spdlog::error("Feature library directory doesn't exist: {}", lib_path);
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
            spdlog::warn("Failed to open feature file: {}", file_path);
            continue;
        }

        // 读取特征向量到 vector（自动内存管理）
        std::vector<float> feature;
        feature.reserve(feature_dim_);
        std::string line;

        while (std::getline(infile, line) && static_cast<int>(feature.size()) < feature_dim_) {
            feature.push_back(std::stof(line));
        }
        infile.close();

        if (static_cast<int>(feature.size()) != feature_dim_) {
            spdlog::warn("Feature dimension mismatch in {} (expected {}, got {})",
                        file_path, feature_dim_, feature.size());
            continue;
        }

        // 提取人名 (去掉文件扩展名)
        std::string name = ptr->d_name;
        size_t dot_pos = name.find_last_of(".");
        if (dot_pos != std::string::npos) {
            name = name.substr(0, dot_pos);
        }

        lib_feature_.push_back(std::move(feature));
        lib_face_name_.push_back(name);
        lib_user_ids_.push_back(count);  // 文件模式使用序号作为ID
        count++;
    }

    closedir(pDir);

    spdlog::info("Loaded {} face features from {}", count, lib_path);
    return count;
}

int FeatureLibrary::load_from_database(db::DatabaseManager* db_manager, int feature_dim) {
    std::unique_lock<std::shared_mutex> lock(mutex_);  // 写锁

    if (!db_manager || !db_manager->is_initialized()) {
        spdlog::error("Database manager not initialized");
        return -1;
    }

    feature_dim_ = feature_dim;
    db_manager_ = db_manager;
    lib_feature_.clear();
    lib_face_name_.clear();
    lib_user_ids_.clear();

    // 创建DAO对象
    db::FaceFeatureDAO feature_dao(db_manager);
    db::UserDAO user_dao(db_manager);

    // 查询所有启用用户的特征
    auto features = feature_dao.find_all_active();

    if (features.empty()) {
        spdlog::warn("No face features found in database");
        return 0;
    }

    int count = 0;
    for (const auto& feature : features) {
        // 查询用户信息
        db::UserInfo user;
        if (!user_dao.find_by_id(feature.user_id, user)) {
            spdlog::warn("User not found for feature_id: {}", feature.feature_id);
            continue;
        }

        // 使用 vector 自动管理内存（直接复制）
        lib_feature_.push_back(feature.feature_vector);
        lib_face_name_.push_back(user.user_name);
        lib_user_ids_.push_back(user.user_id);
        count++;
    }

    spdlog::info("Loaded {} face features from database", count);
    return count;
}

bool FeatureLibrary::match_feature(const float* feature, float threshold,
                                   std::string& matched_name, float& max_score) {
    int user_id = 0;
    return match_feature_with_id(feature, threshold, user_id, matched_name, max_score);
}

bool FeatureLibrary::match_feature_with_id(const float* feature, float threshold,
                                           int& user_id, std::string& matched_name, float& max_score) {
    std::shared_lock<std::shared_mutex> lock(mutex_);  // 读锁

    max_score = -1.0f;  // 修复：使用 -1.0f 表示未计算，确保能捕获所有正相似度
    matched_name = "stranger";
    user_id = 0;
    bool found = false;

    int best_match_idx = -1;
    float best_similarity = -1.0f;

    for (size_t i = 0; i < lib_feature_.size(); i++) {
        float similarity = compute_cosine_similarity(feature, lib_feature_[i].data());

        // 关键修复：始终记录最高相似度（用于调试和显示）
        if (similarity > best_similarity) {
            best_similarity = similarity;
            best_match_idx = static_cast<int>(i);
        }
    }

    // 更新 max_score 为真实的最高相似度（无论是否超过阈值）
    max_score = best_similarity;

    // 只有当最高相似度超过阈值时才认为匹配成功
    if (best_match_idx >= 0 && best_similarity >= threshold) {
        matched_name = lib_face_name_[best_match_idx];
        user_id = lib_user_ids_[best_match_idx];
        found = true;
    }
    return found;
}

bool FeatureLibrary::add_feature(int user_id, const std::string& name, const float* feature) {
    if (!feature) return false;

    std::unique_lock<std::shared_mutex> lock(mutex_);  // 写锁

    // 使用 vector 自动管理内存
    std::vector<float> feature_copy(feature, feature + feature_dim_);

    lib_feature_.push_back(std::move(feature_copy));
    lib_face_name_.push_back(name);
    lib_user_ids_.push_back(user_id);

    spdlog::debug("Added feature for user: {} (ID: {})", name, user_id);
    return true;
}

bool FeatureLibrary::remove_feature(int user_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);  // 写锁

    bool removed = false;

    for (size_t i = 0; i < lib_user_ids_.size(); ) {
        if (lib_user_ids_[i] == user_id) {
            // vector 自动释放内存，无需手动 delete
            lib_feature_.erase(lib_feature_.begin() + i);
            lib_face_name_.erase(lib_face_name_.begin() + i);
            lib_user_ids_.erase(lib_user_ids_.begin() + i);

            removed = true;
            // 不增加i，因为删除后元素前移
        } else {
            i++;
        }
    }

    if (removed) {
        spdlog::debug("Removed feature(s) for user ID: {}", user_id);
    }
    return removed;
}

void FeatureLibrary::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);  // 写锁

    // vector 自动释放内存，无需手动循环 delete
    lib_feature_.clear();
    lib_face_name_.clear();
    lib_user_ids_.clear();
}

float FeatureLibrary::compute_cosine_similarity(const float* feature1, const float* feature2) const {
    // 使用 postprocess.h 中的 cos_similarity 函数
    return cos_similarity(const_cast<float*>(feature1), const_cast<float*>(feature2));
}
