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
#include <QNetworkProxyFactory>
#include <QStyleFactory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <cstdlib>  // for setenv, getenv

#include "gui/main_window.h"
#include "gui_utils/config_manager.h"
#include "themes/theme_manager.h"
#include "config/config.h"

void setup_logger() {
    try {
        // 创建控制台 sink
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        // 控制台日志过多会影响 FPS/CPU，占用主要发生在运行阶段；保留文件日志用于排障
        console_sink->set_level(spdlog::level::info);
        
        // 创建文件 sink (10MB, 3个文件轮转)
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            "logs/face_recognition_gui.log", 1024 * 1024 * 10, 3);
        file_sink->set_level(spdlog::level::info);
        
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
    // 设置输入法环境变量（必须在 QApplication 创建之前）
    // 支持 fcitx5 中文输入
    qputenv("QT_IM_MODULE", "fcitx5");
    qputenv("XMODIFIERS", "@im=fcitx5");
    
    // 设置运行时库路径（确保能找到 RKNN/RGA/RKLLM 库）
    const char* current_ld_path = std::getenv("LD_LIBRARY_PATH");
    std::string new_ld_path = "./lib";
    if (current_ld_path && strlen(current_ld_path) > 0) {
        new_ld_path += ":";
        new_ld_path += current_ld_path;
    }
    setenv("LD_LIBRARY_PATH", new_ld_path.c_str(), 1);

    // 初始化日志系统
    setup_logger();

    // 创建 Qt 应用
    QApplication app(argc, argv);
    app.setApplicationName("人脸识别考勤系统");
    app.setApplicationVersion("2.1.0");
    app.setOrganizationName("FaceRecognition");
    
    spdlog::info("Application started");
    
    // 初始化现代化主题系统
    if (!ThemeManager::instance()->initialize()) {
        spdlog::warn("Theme manager initialization failed, using default style");
    } else {
        spdlog::info("Theme manager initialized successfully");
    }
    spdlog::info("Qt version: {}", qVersion());
    
    // 获取可执行文件所在目录（应该是 install/face_recognition_cap）
    QString appDir = QCoreApplication::applicationDirPath();
    spdlog::info("Application directory: {}", appDir.toStdString());
    
    // 使用固定配置的模型路径 (相对于可执行文件目录)
    std::string yolov8_face_model = (appDir + "/" + Config::Path::YOLO_MODEL).toStdString();
    std::string facenet_model = (appDir + "/" + Config::Path::FACENET_MODEL).toStdString();
    
    // 从配置文件加载摄像头设置（固定 USB + 异步）
    std::string camera_source = "usb";
    int camera_id = ConfigManager::instance()->getCameraId();
    
    // 数据库路径 (相对于可执行文件目录)
    std::string db_path = (appDir + "/" + Config::Path::DATABASE).toStdString();
    
    // 确保数据库目录存在
    QDir dbDir = QFileInfo(QString::fromStdString(db_path)).dir();
    if (!dbDir.exists()) {
        dbDir.mkpath(".");
        spdlog::info("Created database directory: {}", dbDir.absolutePath().toStdString());
    }
    
    if (argc >= 3) {
        yolov8_face_model = argv[1];
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
    spdlog::info("  YOLOv8-face model: {}", yolov8_face_model);
    spdlog::info("  FaceNet model: {}", facenet_model);
    spdlog::info("  Camera source: {}", camera_source);
    spdlog::info("  Camera ID: {}", camera_id);
    spdlog::info("  Database: {}", db_path);
    
    // 创建主窗口
    MainWindow main_window;

    // 先显示主窗口，避免初始化模型/摄像头阻塞导致“窗口很久才出来”
    main_window.show();

    // 后台初始化系统（完成后自动启动识别）
    main_window.initialize_async(yolov8_face_model, facenet_model,
                                 camera_source, camera_id, db_path);
    
    spdlog::info("Main window shown, entering event loop");
    
    // 进入事件循环
    int ret = app.exec();
    
    spdlog::info("Application exited with code: {}", ret);
    return ret;
}
