/**
 * @file feature_library.h
 * @brief 人脸特征库管理器 - 负责特征库的加载和匹配(支持数据库)
 * @author CL
 * @date 2025-11-20
 *
 * 优化说明：
 * - 使用 std::vector<float> 替代 float* 原始指针，自动内存管理
 * - 添加 shared_mutex 实现线程安全的读写锁
 */

#ifndef _FEATURE_LIBRARY_H_
#define _FEATURE_LIBRARY_H_

#include <vector>
#include <string>
#include <shared_mutex>
#include "config/config.h"

// 前向声明
namespace db {
    class DatabaseManager;
}

/**
 * @brief 人脸特征库管理类
 *
 * 职责:
 * - 从文件系统或数据库加载人脸特征库
 * - 管理特征向量和对应的人名
 * - 提供特征匹配功能
 * - 释放特征库资源
 */
class FeatureLibrary {
public:
    FeatureLibrary();
    ~FeatureLibrary();

    /**
     * @brief 从指定目录加载特征库(文件系统模式)
     * @param lib_path 特征库目录路径
     * @param feature_dim 特征向量维度 (默认512)
     * @return 加载的特征数量, -1表示失败
     */
    int load_from_directory(const std::string& lib_path, int feature_dim = Config::Model::FEATURE_DIM);

    /**
     * @brief 从数据库加载特征库(数据库模式)
     * @param db_manager 数据库管理器
     * @param feature_dim 特征向量维度 (默认512)
     * @return 加载的特征数量, -1表示失败
     */
    int load_from_database(db::DatabaseManager* db_manager, int feature_dim = Config::Model::FEATURE_DIM);

    /**
     * @brief 匹配人脸特征
     * @param feature 待匹配的特征向量
     * @param threshold 匹配阈值
     * @param matched_name 输出匹配到的人名
     * @param max_score 输出最大相似度分数
     * @return true 匹配成功, false 未匹配到
     */
    bool match_feature(const float* feature, float threshold,
                      std::string& matched_name, float& max_score);

    /**
     * @brief 匹配人脸特征(返回user_id)
     * @param feature 待匹配的特征向量
     * @param threshold 匹配阈值
     * @param user_id 输出用户ID
     * @param matched_name 输出匹配到的人名
     * @param max_score 输出最大相似度分数
     * @return true 匹配成功, false 未匹配到
     */
    bool match_feature_with_id(const float* feature, float threshold,
                               int& user_id, std::string& matched_name, float& max_score);

    /**
     * @brief 添加新特征到特征库(仅内存)
     * @param user_id 用户ID
     * @param name 用户姓名
     * @param feature 特征向量
     * @return true成功, false失败
     */
    bool add_feature(int user_id, const std::string& name, const float* feature);

    /**
     * @brief 删除用户的所有特征(仅内存)
     * @param user_id 用户ID
     * @return true成功, false失败
     */
    bool remove_feature(int user_id);

    /**
     * @brief 获取特征库大小
     * @return 特征库中的人脸数量
     */
    size_t size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return lib_feature_.size();
    }

    /**
     * @brief 检查特征库是否为空
     */
    bool empty() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return lib_feature_.empty();
    }

    /**
     * @brief 清空特征库
     */
    void clear();

    /**
     * @brief 获取所有人名列表
     */
    std::vector<std::string> get_names() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return lib_face_name_;
    }

    /**
     * @brief 获取所有用户ID列表
     */
    std::vector<int> get_user_ids() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return lib_user_ids_;
    }

private:
    /**
     * @brief 计算余弦相似度
     * @param feature1 特征向量1
     * @param feature2 特征向量2
     * @return 余弦相似度 [0, 1]
     */
    float compute_cosine_similarity(const float* feature1, const float* feature2) const;

private:
    mutable std::shared_mutex mutex_;                    // 读写锁，保证线程安全
    std::vector<std::vector<float>> lib_feature_;        // 特征向量列表（使用 vector 自动管理内存）
    std::vector<std::string> lib_face_name_;             // 对应的人名列表
    std::vector<int> lib_user_ids_;                      // 对应的用户ID列表
    int feature_dim_;                                     // 特征向量维度
    db::DatabaseManager* db_manager_;                    // 数据库管理器
};

#endif // _FEATURE_LIBRARY_H_
