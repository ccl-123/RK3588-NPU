/**
 * @file face_feature_dao.cc
 * @brief 人脸特征数据访问对象实现
 * @author CL
 * @date 2025-11-20
 */

#include "database/face_feature_dao.h"
#include <cstring>
#include <spdlog/spdlog.h>

namespace db {

namespace {

constexpr const char* kFaceFeatureSelectColumns =
    "feature_id, user_id, feature_vector, feature_quality, source_image";

}  // namespace

FaceFeatureDAO::FaceFeatureDAO(DatabaseManager* db_manager)
    : db_manager_(db_manager) {
}

FaceFeatureDAO::~FaceFeatureDAO() {
}

int FaceFeatureDAO::insert(const FaceFeature& feature) {
    std::string sql = R"(
        INSERT INTO face_features (user_id, feature_vector, feature_quality, source_image)
        VALUES (?, ?, ?, ?)
    )";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return -1;
    
    stmt->bind_int(1, feature.user_id);
    
    // 将512维float数组转换为BLOB
    const void* blob_data = feature.feature_vector.data();
    int blob_size = feature.feature_vector.size() * sizeof(float);
    stmt->bind_blob(2, blob_data, blob_size);
    
    stmt->bind_double(3, feature.feature_quality);
    stmt->bind_string(4, feature.source_image);
    
    if (!stmt->execute()) {
        spdlog::error("Failed to insert face feature for user_id: {}", feature.user_id);
        return -1;
    }
    
    return static_cast<int>(stmt->last_insert_id());
}

bool FaceFeatureDAO::remove(int feature_id) {
    std::string sql = "DELETE FROM face_features WHERE feature_id = ?";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return false;
    
    stmt->bind_int(1, feature_id);
    return stmt->execute();
}

bool FaceFeatureDAO::remove_by_user_id(int user_id) {
    std::string sql = "DELETE FROM face_features WHERE user_id = ?";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return false;
    
    stmt->bind_int(1, user_id);
    return stmt->execute();
}

bool FaceFeatureDAO::find_by_id(int feature_id, FaceFeature& feature) {
    std::string sql = std::string("SELECT ") + kFaceFeatureSelectColumns +
        " FROM face_features WHERE feature_id = ?";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return false;
    
    stmt->bind_int(1, feature_id);
    
    if (stmt->step()) {
        fill_feature_from_stmt(stmt.get(), feature);
        return true;
    }
    
    return false;
}

std::vector<FaceFeature> FaceFeatureDAO::find_by_user_id(int user_id) {
    std::vector<FaceFeature> features;
    
    std::string sql = std::string("SELECT ") + kFaceFeatureSelectColumns +
        " FROM face_features WHERE user_id = ? ORDER BY feature_id";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return features;
    
    stmt->bind_int(1, user_id);
    
    while (stmt->step()) {
        FaceFeature feature;
        fill_feature_from_stmt(stmt.get(), feature);
        features.push_back(feature);
    }
    
    return features;
}

std::vector<FaceFeature> FaceFeatureDAO::find_all() {
    std::vector<FaceFeature> features;
    
    std::string sql = std::string("SELECT ") + kFaceFeatureSelectColumns +
        " FROM face_features ORDER BY user_id, feature_id";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return features;
    
    while (stmt->step()) {
        FaceFeature feature;
        fill_feature_from_stmt(stmt.get(), feature);
        features.push_back(feature);
    }
    
    return features;
}

std::vector<FaceFeature> FaceFeatureDAO::find_all_active() {
    std::vector<FaceFeature> features;

    std::string count_sql = R"(
        SELECT COUNT(*)
        FROM face_features f
        INNER JOIN users u ON f.user_id = u.user_id
        WHERE u.status = 1
    )";
    auto count_stmt = db_manager_->prepare(count_sql);
    if (count_stmt && count_stmt->step()) {
        features.reserve(static_cast<size_t>(count_stmt->get_column_int(0)));
    }
    
    std::string sql = R"(
        SELECT f.feature_id, f.user_id, f.feature_vector, f.feature_quality, f.source_image
        FROM face_features f
        INNER JOIN users u ON f.user_id = u.user_id
        WHERE u.status = 1
        ORDER BY f.user_id, f.feature_id
    )";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return features;
    
    while (stmt->step()) {
        FaceFeature feature;
        fill_feature_from_stmt(stmt.get(), feature);
        features.push_back(feature);
    }
    
    return features;
}

int FaceFeatureDAO::count_by_user_id(int user_id) {
    std::string sql = "SELECT COUNT(*) FROM face_features WHERE user_id = ?";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return 0;
    
    stmt->bind_int(1, user_id);
    
    if (stmt->step()) {
        return stmt->get_column_int(0);
    }
    
    return 0;
}

int FaceFeatureDAO::count() {
    std::string sql = "SELECT COUNT(*) FROM face_features";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return 0;
    
    if (stmt->step()) {
        return stmt->get_column_int(0);
    }
    
    return 0;
}

bool FaceFeatureDAO::batch_insert(const std::vector<FaceFeature>& features) {
    if (features.empty()) return true;
    
    // 开启事务
    if (!db_manager_->begin_transaction()) {
        return false;
    }

    std::string sql = R"(
        INSERT INTO face_features (user_id, feature_vector, feature_quality, source_image)
        VALUES (?, ?, ?, ?)
    )";
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) {
        db_manager_->rollback();
        return false;
    }
    
    try {
        for (const auto& feature : features) {
            const void* blob_data = feature.feature_vector.data();
            int blob_size = feature.feature_vector.size() * sizeof(float);

            stmt->bind_int(1, feature.user_id);
            stmt->bind_blob(2, blob_data, blob_size);
            stmt->bind_double(3, feature.feature_quality);
            stmt->bind_string(4, feature.source_image);

            if (!stmt->execute()) {
                db_manager_->rollback();
                return false;
            }
            stmt->reset();
        }
        
        return db_manager_->commit();
        
    } catch (...) {
        db_manager_->rollback();
        return false;
    }
}

void FaceFeatureDAO::fill_feature_from_stmt(PreparedStatement* stmt, FaceFeature& feature) {
    feature.feature_id = stmt->get_column_int(0);
    feature.user_id = stmt->get_column_int(1);
    
    // 读取BLOB数据
    int blob_size = 0;
    const void* blob_data = stmt->get_column_blob(2, blob_size);
    
    if (blob_data && blob_size == 512 * sizeof(float)) {
        feature.feature_vector.resize(512);
        std::memcpy(feature.feature_vector.data(), blob_data, blob_size);
    } else {
        spdlog::warn("Invalid feature vector size: {}", blob_size);
        feature.feature_vector.resize(512, 0.0f);
    }
    
    feature.feature_quality = stmt->get_column_double(3);
    feature.source_image = stmt->get_column_string(4);
    // created_at 可以根据需要解析
}

} // namespace db
