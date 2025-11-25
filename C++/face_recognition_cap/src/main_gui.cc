/**
 * @file main_gui.cc
 * @brief GUI 版本主程序入口
 * @author CL
 * @date 2025-11-20
 *
 * 人脸识别考勤系统 GUI 版本的主程序，初始化 Qt 应用和日志系统。
 */

#include <QApplication>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include "gui/main_window.h"
#include "utils/config_manager.h"

void setup_logger() {
    try {
        // 创建控制台 sink
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::debug);
        
        // 创建文件 sink (10MB, 3个文件轮转)
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            "logs/face_recognition_gui.log", 1024 * 1024 * 10, 3);
        file_sink->set_level(spdlog::level::debug);
        
        // 创建 logger
        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        auto logger = std::make_shared<spdlog::logger>("main", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::debug);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
        
        spdlog::set_default_logger(logger);
        spdlog::info("Logger initialized");
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
    }
}

int main(int argc, char* argv[]) {
    // 初始化日志系统
    setup_logger();
    
    // 创建 Qt 应用
    QApplication app(argc, argv);
    app.setApplicationName("人脸识别考勤系统");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("FaceRecognition");
    
    spdlog::info("Application started");
    spdlog::info("Qt version: {}", qVersion());
    
    // 获取可执行文件所在目录（应该是 install/face_recognition_cap）
    QString appDir = QCoreApplication::applicationDirPath();
    spdlog::info("Application directory: {}", appDir.toStdString());
    
    // 解析命令行参数
    // 使用相对于可执行文件的路径
    std::string retinaface_model = (appDir + "/data/model/retinaface.rknn").toStdString();
    std::string facenet_model = (appDir + "/data/model/w600k_mbf.rknn").toStdString();
    
    // 从配置文件加载摄像头设置（固定 USB + 异步）
    std::string camera_source = "usb";
    int camera_id = ConfigManager::instance()->getCameraId();
    
    // 数据库使用相对于可执行文件的路径，确保唯一性
    std::string db_path = (appDir + "/data/database/face_recognition.db").toStdString();
    
    // 确保数据库目录存在
    QDir dbDir = QFileInfo(QString::fromStdString(db_path)).dir();
    if (!dbDir.exists()) {
        dbDir.mkpath(".");
        spdlog::info("Created database directory: {}", dbDir.absolutePath().toStdString());
    }
    
    if (argc >= 3) {
        retinaface_model = argv[1];
        facenet_model = argv[2];
    }
    if (argc >= 5) {
        camera_source = argv[3];
        camera_id = std::atoi(argv[4]);
    }
    if (argc >= 6) {
        db_path = argv[5];
    }
    
    spdlog::info("Configuration:");
    spdlog::info("  RetinaFace model: {}", retinaface_model);
    spdlog::info("  FaceNet model: {}", facenet_model);
    spdlog::info("  Camera source: {}", camera_source);
    spdlog::info("  Camera ID: {}", camera_id);
    spdlog::info("  Database: {}", db_path);
    
    // 创建主窗口
    MainWindow main_window;
    
    // 初始化系统
    if (!main_window.initialize(retinaface_model, facenet_model, 
                                camera_source, camera_id, db_path)) {
        QMessageBox::critical(nullptr, "错误", "系统初始化失败");
        spdlog::error("System initialization failed");
        return -1;
    }
    
    // 显示主窗口
    main_window.show();
    
    // 自动启动识别
    main_window.start_recognition();
    
    spdlog::info("Main window shown, entering event loop");
    
    // 进入事件循环
    int ret = app.exec();
    
    spdlog::info("Application exited with code: {}", ret);
    return ret;
}

