/**
 * @file feature_library.h
 * @brief 人脸特征库管理器 - 负责特征库的加载和匹配
 * @author Augment Agent
 * @date 2025-11-20
 */

#ifndef _FEATURE_LIBRARY_H_
#define _FEATURE_LIBRARY_H_

#include <vector>
#include <string>

/**
 * @brief 人脸特征库管理类
 * 
 * 职责:
 * - 从文件系统加载人脸特征库
 * - 管理特征向量和对应的人名
 * - 提供特征匹配功能
 * - 释放特征库资源
 */
class FeatureLibrary {
public:
    FeatureLibrary();
    ~FeatureLibrary();

    /**
     * @brief 从指定目录加载特征库
     * @param lib_path 特征库目录路径
     * @param feature_dim 特征向量维度 (默认512)
     * @return 加载的特征数量, -1表示失败
     */
    int load_from_directory(const std::string& lib_path, int feature_dim = 512);

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
     * @brief 获取特征库大小
     * @return 特征库中的人脸数量
     */
    size_t size() const { return lib_feature_.size(); }

    /**
     * @brief 检查特征库是否为空
     */
    bool empty() const { return lib_feature_.empty(); }

    /**
     * @brief 清空特征库
     */
    void clear();

    /**
     * @brief 获取所有人名列表
     */
    const std::vector<std::string>& get_names() const { return lib_face_name_; }

private:
    /**
     * @brief 计算余弦相似度
     * @param feature1 特征向量1
     * @param feature2 特征向量2
     * @return 余弦相似度 [0, 1]
     */
    float compute_cosine_similarity(const float* feature1, const float* feature2) const;

private:
    std::vector<float*> lib_feature_;        // 特征向量列表
    std::vector<std::string> lib_face_name_; // 对应的人名列表
    int feature_dim_;                         // 特征向量维度
};

#endif // _FEATURE_LIBRARY_H_

