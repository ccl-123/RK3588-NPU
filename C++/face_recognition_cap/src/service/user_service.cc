/**
 * @file user_service.cc
 * @brief 用户管理业务服务实现
 * @author CL
 * @date 2025-11-20
 */

#include "service/user_service.h"
#include <spdlog/spdlog.h>

namespace service {

UserService::UserService(db::DatabaseManager* db_manager, FeatureLibrary* feature_library)
    : db_manager_(db_manager)
    , feature_library_(feature_library)
{
    user_dao_ = new db::UserDAO(db_manager_);
    feature_dao_ = new db::FaceFeatureDAO(db_manager_);
}

UserService::~UserService() {
    delete user_dao_;
    delete feature_dao_;
}

RegistrationResult UserService::register_user(const std::string& user_name,
                                              const std::string& employee_id,
                                              const std::string& department,
                                              const std::string& position,
                                              const std::string& phone,
                                              const std::string& email) {
    RegistrationResult result;
    
    // 检查用户名是否已存在
    if (user_dao_->exists_by_name(user_name)) {
        result.success = false;
        result.message = "User name already exists";
        return result;
    }
    
    // 创建用户信息
    db::UserInfo user;
    user.user_name = user_name;
    user.employee_id = employee_id;
    user.department = department;
    user.position = position;
    user.phone = phone;
    user.email = email;
    user.status = 1;  // 启用
    
    // 插入数据库
    int user_id = user_dao_->insert(user);
    
    if (user_id > 0) {
        result.success = true;
        result.user_id = user_id;
        result.message = "User registered successfully";
        spdlog::info("User registered: {} (ID: {})", user_name, user_id);
    } else {
        result.success = false;
        result.message = "Failed to insert user into database";
    }
    
    return result;
}

int UserService::add_face_feature(int user_id, const std::vector<float>& feature_vector,
                                  float quality, const std::string& source_image) {
    // 检查用户是否存在
    db::UserInfo user;
    if (!user_dao_->find_by_id(user_id, user)) {
        spdlog::error("User not found: {}", user_id);
        return -1;
    }
    
    // 检查特征向量维度
    if (feature_vector.size() != 512) {
        spdlog::error("Invalid feature vector size: {}", feature_vector.size());
        return -1;
    }
    
    // 创建特征记录
    db::FaceFeature feature;
    feature.user_id = user_id;
    feature.feature_vector = feature_vector;
    feature.feature_quality = quality;
    feature.source_image = source_image;
    
    // 插入数据库
    int feature_id = feature_dao_->insert(feature);
    
    if (feature_id > 0) {
        // 更新内存中的特征库
        feature_library_->add_feature(user_id, user.user_name, feature_vector.data());
        spdlog::info("Face feature added for user: {} (Feature ID: {})", user.user_name, feature_id);
    }
    
    return feature_id;
}

bool UserService::delete_user(int user_id) {
    // 先删除用户的所有特征
    if (!delete_all_features(user_id)) {
        spdlog::error("Failed to delete user features for user_id: {}", user_id);
        return false;
    }
    
    // 删除用户
    if (!user_dao_->remove(user_id)) {
        spdlog::error("Failed to delete user from database, user_id: {}", user_id);
        return false;
    }
    
    spdlog::info("User deleted: {}", user_id);
    return true;
}

bool UserService::update_user(const db::UserInfo& user) {
    return user_dao_->update(user);
}

bool UserService::get_user(int user_id, db::UserInfo& user) {
    return user_dao_->find_by_id(user_id, user);
}

std::vector<db::UserInfo> UserService::get_all_users(int status) {
    return user_dao_->find_all(status);
}

bool UserService::set_user_status(int user_id, bool enabled) {
    int status = enabled ? 1 : 0;
    
    if (!user_dao_->update_status(user_id, status)) {
        return false;
    }
    
    // 如果禁用用户，从内存特征库中移除
    if (!enabled) {
        feature_library_->remove_feature(user_id);
    } else {
        // 如果启用用户，重新加载特征库
        reload_feature_library();
    }
    
    return true;
}

int UserService::reload_feature_library() {
    return feature_library_->load_from_database(db_manager_, 512);
}

bool UserService::user_exists(const std::string& user_name) {
    return user_dao_->exists_by_name(user_name);
}

int UserService::get_feature_count(int user_id) {
    return feature_dao_->count_by_user_id(user_id);
}

bool UserService::delete_all_features(int user_id) {
    // 从数据库删除
    if (!feature_dao_->remove_by_user_id(user_id)) {
        return false;
    }
    
    // 从内存特征库删除
    feature_library_->remove_feature(user_id);
    
    return true;
}

} // namespace service

