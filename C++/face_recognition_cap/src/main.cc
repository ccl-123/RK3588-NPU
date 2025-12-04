/**
 * @file main.cc
 * @brief 人脸识别应用程序入口
 * @author CL
 * @date 2025-11-20
 *
 * 功能: 实时人脸识别系统
 * - 支持 USB/MIPI 摄像头
 * - YOLOv8-face 人脸检测
 * - FaceNet 特征提取和识别
 * - 多线程优化 (预处理、渲染、异步采集)
 * - 支持数据库模式和考勤记录
 */

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "config/config.h"
#include "app/face_recognition_app.h"
#include "service/attendance_service.h"
#include "database/database_manager.h"

// 全局考勤服务
service::AttendanceService* g_attendance_service = nullptr;

/**
 * @brief 识别结果回调 - 记录考勤
 */
void on_recognition_callback(const RecognitionResult& result) {
    if (!g_attendance_service) return;

    // 记录考勤
    g_attendance_service->record_attendance(
        result.user_id,
        result.user_name,
        result.similarity,
        "",  // 可选: 保存人脸图像路径
        1    // 签到
    );
}

/**
 * @brief 主函数 - 程序入口
 *
 * @param argc 参数个数
 * @param argv 参数列表
 *   argv[1]: YOLOv8-face 模型路径
 *   argv[2]: FaceNet 模型路径
 *   argv[3]: 摄像头类型 (usb/mipi)
 *   argv[4]: 设备编号
 *   argv[5]: (可选) --db 启用数据库模式
 *
 * @return 0 成功, -1 失败
 */
int main(int argc, char** argv)
{
    // 1. 参数检查
    if (argc < 5) {
        printf("Usage: %s <yolov8-face model> <facenet model> <usb or mipi> <device number> [--db]\n", argv[0]);
        printf("\nExample:\n");
        printf("  File mode:     %s %s %s usb 21\n", argv[0], Config::Path::YOLO_MODEL, Config::Path::FACENET_MODEL);
        printf("  Database mode: %s %s %s usb 21 --db\n", argv[0], Config::Path::YOLO_MODEL, Config::Path::FACENET_MODEL);
        return -1;
    }

    // 2. 解析命令行参数
    AppConfig config;
    config.retinaface_model_path = argv[1];  // 兼容性：使用旧字段名
    config.facenet_model_path = argv[2];
    config.camera_type = argv[3];
    config.device_number = argv[4];

    // 检查是否启用数据库模式
    bool use_database = false;
    if (argc >= 6 && strcmp(argv[5], "--db") == 0) {
        use_database = true;
        config.use_database = true;
        config.database_path = Config::Path::DATABASE;
    }

    // 3. 创建应用实例
    FaceRecognitionApp app;

    // 4. 如果启用数据库模式，设置考勤回调
    if (use_database) {
        auto* db_manager = &db::DatabaseManager::instance();
        g_attendance_service = new service::AttendanceService(db_manager);
        app.set_recognition_callback(on_recognition_callback);

        std::cout << "Database mode enabled" << std::endl;
        std::cout << "Attendance records will be saved to database" << std::endl;
    }

    // 5. 初始化应用
    std::cout << "========================================" << std::endl;
    std::cout << "  Face Recognition System (YOLOv8-face)" << std::endl;
    std::cout << "  Mode: " << (use_database ? "Database" : "File") << std::endl;
    std::cout << "========================================" << std::endl;

    if (app.initialize(config) != 0) {
        std::cerr << "Failed to initialize application" << std::endl;
        if (g_attendance_service) delete g_attendance_service;
        return -1;
    }

    // 6. 运行主循环
    int ret = app.run();

    // 7. 清理资源
    if (g_attendance_service) {
        delete g_attendance_service;
    }

    // 8. 正常退出
    std::cout << "Application exited" << std::endl;
    return ret;
}
