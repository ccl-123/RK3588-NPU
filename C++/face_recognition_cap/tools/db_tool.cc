/**
 * @file db_tool.cc
 * @brief 数据库管理工具
 * @author CL
 * @date 2025-11-20
 * 
 * 功能:
 * - 初始化数据库
 * - 导入用户
 * - 查看统计信息
 */

#include "database/database_manager.h"
#include "database/user_dao.h"
#include "database/face_feature_dao.h"
#include "database/attendance_record_dao.h"
#include "service/user_service.h"
#include "service/attendance_service.h"
#include "app/feature_library.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " <command> [options]" << std::endl;
    std::cout << "\nCommands:" << std::endl;
    std::cout << "  init <db_path>                         - Initialize database" << std::endl;
    std::cout << "  add_user <db_path> <name> [dept]       - Add a new user" << std::endl;
    std::cout << "  list_users <db_path>                   - List all users" << std::endl;
    std::cout << "  stats <db_path>                        - Show database statistics" << std::endl;
    std::cout << "  attendance <db_path> <date>            - Show attendance for date (YYYY-MM-DD)" << std::endl;
    std::cout << "  import_features <db_path> <feat_dir>   - Import features from directory" << std::endl;
}

int cmd_init(const std::string& db_path) {
    std::cout << "Initializing database: " << db_path << std::endl;

    auto* db_manager = &db::DatabaseManager::instance();
    if (!db_manager->initialize(db_path)) {
        std::cerr << "Failed to initialize database" << std::endl;
        return -1;
    }

    std::cout << "Database initialized successfully" << std::endl;
    return 0;
}

int cmd_add_user(const std::string& db_path, const std::string& name, const std::string& dept) {
    auto* db_manager = &db::DatabaseManager::instance();
    if (!db_manager->initialize(db_path)) {
        std::cerr << "Failed to open database" << std::endl;
        return -1;
    }

    FeatureLibrary feature_lib;
    service::UserService user_service(db_manager, &feature_lib);

    auto result = user_service.register_user(name, "", dept);

    if (result.success) {
        std::cout << "User added successfully: " << name
                  << " (ID: " << result.user_id << ")" << std::endl;
        return 0;
    } else {
        std::cerr << "Failed to add user: " << result.message << std::endl;
        return -1;
    }
}

int cmd_list_users(const std::string& db_path) {
    auto* db_manager = &db::DatabaseManager::instance();
    if (!db_manager->initialize(db_path)) {
        std::cerr << "Failed to open database" << std::endl;
        return -1;
    }

    FeatureLibrary feature_lib;
    service::UserService user_service(db_manager, &feature_lib);

    auto users = user_service.get_all_users();

    std::cout << "\n=== User List ===" << std::endl;
    std::cout << "Total: " << users.size() << " users" << std::endl;
    std::cout << std::string(80, '-') << std::endl;
    std::cout << "ID\tName\t\tDepartment\tStatus" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    for (const auto& user : users) {
        std::cout << user.user_id << "\t"
                  << user.user_name << "\t\t"
                  << user.department << "\t\t"
                  << (user.status == 1 ? "Enabled" : "Disabled") << std::endl;
    }

    return 0;
}

int cmd_stats(const std::string& db_path) {
    auto* db_manager = &db::DatabaseManager::instance();
    if (!db_manager->initialize(db_path)) {
        std::cerr << "Failed to open database" << std::endl;
        return -1;
    }

    db::UserDAO user_dao(db_manager);
    db::FaceFeatureDAO feature_dao(db_manager);
    db::AttendanceRecordDAO record_dao(db_manager);

    int user_count = user_dao.count();
    int feature_count = feature_dao.count();
    int record_count = record_dao.count();

    std::cout << "\n=== Database Statistics ===" << std::endl;
    std::cout << "Users:              " << user_count << std::endl;
    std::cout << "Face Features:      " << feature_count << std::endl;
    std::cout << "Attendance Records: " << record_count << std::endl;

    return 0;
}

int cmd_attendance(const std::string& db_path, const std::string& date) {
    auto* db_manager = &db::DatabaseManager::instance();
    if (!db_manager->initialize(db_path)) {
        std::cerr << "Failed to open database" << std::endl;
        return -1;
    }

    service::AttendanceService attendance_service(db_manager);

    auto stats = attendance_service.get_statistics(date);

    std::cout << "\n=== Attendance Statistics for " << date << " ===" << std::endl;
    std::cout << "Total Check-ins:    " << stats.total_count << std::endl;
    std::cout << "Check-in:           " << stats.check_in_count << std::endl;
    std::cout << "Check-out:          " << stats.check_out_count << std::endl;
    std::cout << "Normal:             " << stats.normal_count << std::endl;
    std::cout << "Late:               " << stats.late_count << std::endl;
    std::cout << "Early Leave:        " << stats.early_leave_count << std::endl;

    return 0;
}

int cmd_import_features(const std::string& db_path, const std::string& feat_dir) {
    auto* db_manager = &db::DatabaseManager::instance();
    if (!db_manager->initialize(db_path)) {
        std::cerr << "Failed to open database" << std::endl;
        return -1;
    }

    FeatureLibrary feature_lib;
    service::UserService user_service(db_manager, &feature_lib);

    // 获取所有用户
    auto users = user_service.get_all_users();
    if (users.empty()) {
        std::cerr << "No users found in database" << std::endl;
        return -1;
    }

    std::cout << "\n=== Importing Features ===" << std::endl;

    int total_imported = 0;
    for (const auto& user : users) {
        // 查找该用户的特征文件 (user_name_*.dat)
        std::string pattern = user.user_name + "_";

        for (int i = 1; i <= 10; i++) {  // 最多尝试10个文件
            std::string filename = feat_dir + "/" + user.user_name + "_" + std::to_string(i) + ".dat";
            std::ifstream infile(filename);

            if (!infile.is_open()) {
                break;  // 没有更多文件了
            }

            // 读取512维特征向量
            std::vector<float> feature_vector;
            std::string line;
            while (std::getline(infile, line)) {
                feature_vector.push_back(std::atof(line.c_str()));
            }
            infile.close();

            if (feature_vector.size() != 512) {
                std::cerr << "Invalid feature size for " << filename
                          << ": " << feature_vector.size() << std::endl;
                continue;
            }

            // 添加到数据库
            int feature_id = user_service.add_face_feature(user.user_id, feature_vector, 1.0, filename);

            if (feature_id > 0) {
                std::cout << "✓ Imported: " << filename << " (feature_id: " << feature_id << ")" << std::endl;
                total_imported++;
            } else {
                std::cerr << "✗ Failed to import: " << filename << std::endl;
            }
        }
    }

    std::cout << "\n=== Import Complete ===" << std::endl;
    std::cout << "Total features imported: " << total_imported << std::endl;

    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return -1;
    }

    std::string command = argv[1];

    if (command == "init" && argc >= 3) {
        return cmd_init(argv[2]);
    } else if (command == "add_user" && argc >= 4) {
        std::string dept = argc >= 5 ? argv[4] : "";
        return cmd_add_user(argv[2], argv[3], dept);
    } else if (command == "list_users" && argc >= 3) {
        return cmd_list_users(argv[2]);
    } else if (command == "stats" && argc >= 3) {
        return cmd_stats(argv[2]);
    } else if (command == "attendance" && argc >= 4) {
        return cmd_attendance(argv[2], argv[3]);
    } else if (command == "import_features" && argc >= 4) {
        return cmd_import_features(argv[2], argv[3]);
    } else {
        print_usage(argv[0]);
        return -1;
    }

    return 0;
}

