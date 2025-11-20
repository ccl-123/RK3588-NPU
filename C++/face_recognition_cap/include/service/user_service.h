/**
 * @file user_service.h
 * @brief 用户管理业务服务
 * @author Augment Agent
 * @date 2025-11-20
 */

#ifndef _USER_SERVICE_H_
#define _USER_SERVICE_H_

#include "database/database_manager.h"
#include "database/user_dao.h"
#include "database/face_feature_dao.h"
#include "app/feature_library.h"
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>

namespace service {

/**
 * @brief 用户注册结果
 */
struct RegistrationResult {
    bool success;
    int user_id;
    std::string message;
    
    RegistrationResult() : success(false), user_id(0) {}
};

/**
 * @brief 用户管理业务服务类
 */
class UserService {
public:
    UserService(db::DatabaseManager* db_manager, FeatureLibrary* feature_library);
    ~UserService();
    
    /**
     * @brief 注册新用户
     * @param user_name 用户姓名
     * @param employee_id 工号
     * @param department 部门
     * @param position 职位
     * @param phone 电话
     * @param email 邮箱
     * @return 注册结果
     */
    RegistrationResult register_user(const std::string& user_name,
                                     const std::string& employee_id = "",
                                     const std::string& department = "",
                                     const std::string& position = "",
                                     const std::string& phone = "",
                                     const std::string& email = "");
    
    /**
     * @brief 为用户添加人脸特征
     * @param user_id 用户ID
     * @param feature_vector 特征向量(512维)
     * @param quality 特征质量
     * @param source_image 源图像路径
     * @return 特征ID, -1表示失败
     */
    int add_face_feature(int user_id, const std::vector<float>& feature_vector,
                        float quality = 0.0f, const std::string& source_image = "");
    
    /**
     * @brief 删除用户
     * @param user_id 用户ID
     * @return true成功, false失败
     */
    bool delete_user(int user_id);
    
    /**
     * @brief 更新用户信息
     * @param user 用户信息
     * @return true成功, false失败
     */
    bool update_user(const db::UserInfo& user);
    
    /**
     * @brief 查询用户信息
     * @param user_id 用户ID
     * @param user 输出用户信息
     * @return true成功, false失败
     */
    bool get_user(int user_id, db::UserInfo& user);
    
    /**
     * @brief 查询所有用户
     * @param status 状态过滤(-1表示全部)
     * @return 用户列表
     */
    std::vector<db::UserInfo> get_all_users(int status = -1);
    
    /**
     * @brief 启用/禁用用户
     * @param user_id 用户ID
     * @param enabled true启用, false禁用
     * @return true成功, false失败
     */
    bool set_user_status(int user_id, bool enabled);
    
    /**
     * @brief 重新加载特征库(从数据库)
     * @return 加载的特征数量, -1表示失败
     */
    int reload_feature_library();
    
    /**
     * @brief 检查用户名是否存在
     * @param user_name 用户姓名
     * @return true存在, false不存在
     */
    bool user_exists(const std::string& user_name);
    
    /**
     * @brief 获取用户的人脸特征数量
     * @param user_id 用户ID
     * @return 特征数量
     */
    int get_feature_count(int user_id);
    
    /**
     * @brief 删除用户的所有人脸特征
     * @param user_id 用户ID
     * @return true成功, false失败
     */
    bool delete_all_features(int user_id);
    
private:
    db::DatabaseManager* db_manager_;
    db::UserDAO* user_dao_;
    db::FaceFeatureDAO* feature_dao_;
    FeatureLibrary* feature_library_;
};

} // namespace service

#endif // _USER_SERVICE_H_

