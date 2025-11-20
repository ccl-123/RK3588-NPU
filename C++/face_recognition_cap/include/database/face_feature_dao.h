/**
 * @file face_feature_dao.h
 * @brief 人脸特征数据访问对象
 * @author CL
 * @date 2025-11-20
 */

#ifndef _FACE_FEATURE_DAO_H_
#define _FACE_FEATURE_DAO_H_

#include "database_manager.h"
#include "database_types.h"
#include <vector>
#include <memory>

namespace db {

/**
 * @brief 人脸特征数据访问类
 */
class FaceFeatureDAO {
public:
    FaceFeatureDAO(DatabaseManager* db_manager);
    ~FaceFeatureDAO();
    
    /**
     * @brief 插入新特征
     * @param feature 特征信息
     * @return 新特征ID, -1表示失败
     */
    int insert(const FaceFeature& feature);
    
    /**
     * @brief 删除特征
     * @param feature_id 特征ID
     * @return true成功, false失败
     */
    bool remove(int feature_id);
    
    /**
     * @brief 删除用户的所有特征
     * @param user_id 用户ID
     * @return true成功, false失败
     */
    bool remove_by_user_id(int user_id);
    
    /**
     * @brief 根据ID查询特征
     * @param feature_id 特征ID
     * @param feature 输出特征信息
     * @return true成功, false失败
     */
    bool find_by_id(int feature_id, FaceFeature& feature);
    
    /**
     * @brief 查询用户的所有特征
     * @param user_id 用户ID
     * @return 特征列表
     */
    std::vector<FaceFeature> find_by_user_id(int user_id);
    
    /**
     * @brief 查询所有特征(用于加载特征库)
     * @return 特征列表
     */
    std::vector<FaceFeature> find_all();
    
    /**
     * @brief 查询所有启用用户的特征(用于加载特征库)
     * @return 特征列表
     */
    std::vector<FaceFeature> find_all_active();
    
    /**
     * @brief 获取用户的特征数量
     * @param user_id 用户ID
     * @return 特征数量
     */
    int count_by_user_id(int user_id);
    
    /**
     * @brief 获取特征总数
     * @return 特征数量
     */
    int count();
    
    /**
     * @brief 批量插入特征(使用事务)
     * @param features 特征列表
     * @return true成功, false失败
     */
    bool batch_insert(const std::vector<FaceFeature>& features);
    
private:
    /**
     * @brief 从结果集填充特征信息
     */
    void fill_feature_from_stmt(PreparedStatement* stmt, FaceFeature& feature);
    
private:
    DatabaseManager* db_manager_;
};

} // namespace db

#endif // _FACE_FEATURE_DAO_H_

