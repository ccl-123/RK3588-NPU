/**
 * @file main_window.cc
 * @brief 主窗口类实现
 * @author CL
 * @date 2025-11-20
 */

#include "gui/main_window.h"
#include "gui/video_display_widget.h"
#include "gui/face_registration_dialog.h"
#include "gui/attendance_query_widget.h"
#include "gui/user_management_widget.h"
#include "gui/settings_dialog.h"
#include "gui/about_dialog.h"

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QCloseEvent>
#include <QHeaderView>
#include <QAction>
#include <QIcon>
#include <spdlog/spdlog.h>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , db_manager_(nullptr)
    , video_widget_(nullptr)
    , user_list_dock_(nullptr)
    , attendance_dock_(nullptr)
    , user_table_(nullptr)
    , attendance_table_(nullptr)
    , status_label_(nullptr)
    , fps_label_(nullptr)
    , recognition_label_(nullptr)
    , frame_timer_(nullptr)
    , status_timer_(nullptr)
    , registration_dialog_(nullptr)
    , attendance_query_widget_(nullptr)
    , is_running_(false)
    , frame_count_(0)
    , fps_(0.0)
    , camera_id_(0)
{
    setup_ui();
    
    // 初始化定时器
    frame_timer_ = new QTimer(this);
    connect(frame_timer_, &QTimer::timeout, this, &MainWindow::update_frame);
    
    status_timer_ = new QTimer(this);
    connect(status_timer_, &QTimer::timeout, this, &MainWindow::update_status);
    status_timer_->start(1000);  // 每秒更新一次状态
    
    last_fps_time_ = std::chrono::steady_clock::now();
    
    spdlog::info("MainWindow initialized");
}

MainWindow::~MainWindow() {
    stop_recognition();
    spdlog::info("MainWindow destroyed");
}

bool MainWindow::initialize(const std::string& retinaface_model,
                            const std::string& facenet_model,
                            const std::string& camera_source,
                            int camera_id,
                            const std::string& db_path) {
    retinaface_model_ = retinaface_model;
    facenet_model_ = facenet_model;
    camera_source_ = camera_source;
    camera_id_ = camera_id;
    db_path_ = db_path;
    
    // 初始化数据库
    db_manager_ = db::DatabaseManager::instance();
    if (!db_manager_->initialize(db_path)) {
        QMessageBox::critical(this, "错误", "数据库初始化失败");
        return false;
    }
    
    // 初始化服务
    user_service_ = std::make_unique<service::UserService>(db_manager_, nullptr);
    attendance_service_ = std::make_unique<service::AttendanceService>(db_manager_);

    // 初始化识别应用
    recognition_app_ = std::make_unique<FaceRecognitionApp>();

    AppConfig config;
    config.retinaface_model_path = retinaface_model;
    config.facenet_model_path = facenet_model;
    config.camera_type = camera_source;
    config.device_number = std::to_string(camera_id);
    config.use_database = true;
    config.database_path = db_path;

    if (recognition_app_->initialize(config) != 0) {
        QMessageBox::critical(this, "错误", "人脸识别系统初始化失败");
        return false;
    }
    
    // 设置识别回调
    recognition_app_->set_recognition_callback([this](const RecognitionResult& result) {
        emit on_recognition_result(result.user_id, 
                                   QString::fromStdString(result.user_name),
                                   result.similarity);
        
        // 记录考勤
        if (attendance_service_) {
            attendance_service_->record_attendance(result.user_id, 
                                                  result.user_name,
                                                  result.similarity);
        }
    });
    
    spdlog::info("System initialized successfully");
    return true;
}

void MainWindow::setup_ui() {
    setWindowTitle("人脸识别考勤系统");
    resize(1280, 720);
    
    // 创建中央视频显示组件
    video_widget_ = new VideoDisplayWidget(this);
    video_widget_->set_show_fps(true);
    setCentralWidget(video_widget_);
    
    // 创建菜单栏
    create_menus();
    
    // 创建工具栏
    create_toolbars();
    
    // 创建状态栏
    create_status_bar();
    
    // 创建停靠窗口
    create_dock_widgets();
}

void MainWindow::create_menus() {
    // 文件菜单
    QMenu* file_menu = menuBar()->addMenu("文件(&F)");
    
    QAction* open_camera_action = file_menu->addAction("打开摄像头(&O)");
    connect(open_camera_action, &QAction::triggered, this, &MainWindow::on_action_open_camera);
    
    QAction* close_camera_action = file_menu->addAction("关闭摄像头(&C)");
    connect(close_camera_action, &QAction::triggered, this, &MainWindow::on_action_close_camera);
    
    file_menu->addSeparator();
    
    QAction* exit_action = file_menu->addAction("退出(&X)");
    connect(exit_action, &QAction::triggered, this, &MainWindow::on_action_exit);
    
    // 用户菜单
    QMenu* user_menu = menuBar()->addMenu("用户(&U)");
    
    QAction* register_action = user_menu->addAction("注册人脸(&R)");
    connect(register_action, &QAction::triggered, this, &MainWindow::on_action_register_face);
    
    QAction* user_mgmt_action = user_menu->addAction("用户管理(&M)");
    connect(user_mgmt_action, &QAction::triggered, this, &MainWindow::on_action_user_management);

    // 考勤菜单
    QMenu* attendance_menu = menuBar()->addMenu("考勤(&A)");

    QAction* query_action = attendance_menu->addAction("考勤查询(&Q)");
    connect(query_action, &QAction::triggered, this, &MainWindow::on_action_query_attendance);

    // 设置菜单
    QMenu* settings_menu = menuBar()->addMenu("设置(&S)");

    QAction* settings_action = settings_menu->addAction("系统设置(&S)");
    connect(settings_action, &QAction::triggered, this, &MainWindow::on_action_settings);

    // 帮助菜单
    QMenu* help_menu = menuBar()->addMenu("帮助(&H)");

    QAction* about_action = help_menu->addAction("关于(&A)");
    connect(about_action, &QAction::triggered, this, &MainWindow::on_action_about);
}

void MainWindow::create_toolbars() {
    QToolBar* toolbar = addToolBar("主工具栏");
    toolbar->setMovable(false);

    // 打开摄像头
    QAction* open_action = toolbar->addAction("打开摄像头");
    connect(open_action, &QAction::triggered, this, &MainWindow::on_action_open_camera);

    // 关闭摄像头
    QAction* close_action = toolbar->addAction("关闭摄像头");
    connect(close_action, &QAction::triggered, this, &MainWindow::on_action_close_camera);

    toolbar->addSeparator();

    // 注册人脸
    QAction* register_action = toolbar->addAction("注册人脸");
    connect(register_action, &QAction::triggered, this, &MainWindow::on_action_register_face);

    // 考勤查询
    QAction* query_action = toolbar->addAction("考勤查询");
    connect(query_action, &QAction::triggered, this, &MainWindow::on_action_query_attendance);
}

void MainWindow::create_status_bar() {
    status_label_ = new QLabel("就绪");
    statusBar()->addWidget(status_label_, 1);

    fps_label_ = new QLabel("FPS: 0");
    statusBar()->addPermanentWidget(fps_label_);

    recognition_label_ = new QLabel("未识别");
    statusBar()->addPermanentWidget(recognition_label_);
}

void MainWindow::create_dock_widgets() {
    // 用户列表停靠窗口
    user_list_dock_ = new QDockWidget("用户列表", this);
    user_table_ = new QTableWidget(user_list_dock_);
    user_table_->setColumnCount(3);
    user_table_->setHorizontalHeaderLabels({"姓名", "工号", "部门"});
    user_table_->horizontalHeader()->setStretchLastSection(true);
    user_list_dock_->setWidget(user_table_);
    addDockWidget(Qt::RightDockWidgetArea, user_list_dock_);

    // 考勤记录停靠窗口
    attendance_dock_ = new QDockWidget("今日考勤", this);
    attendance_table_ = new QTableWidget(attendance_dock_);
    attendance_table_->setColumnCount(4);
    attendance_table_->setHorizontalHeaderLabels({"姓名", "时间", "类型", "相似度"});
    attendance_table_->horizontalHeader()->setStretchLastSection(true);
    attendance_dock_->setWidget(attendance_table_);
    addDockWidget(Qt::RightDockWidgetArea, attendance_dock_);
}

void MainWindow::start_recognition() {
    if (is_running_) {
        return;
    }

    is_running_ = true;
    frame_timer_->start(33);  // 约30 FPS
    status_label_->setText("运行中");

    spdlog::info("Recognition started");
}

void MainWindow::stop_recognition() {
    if (!is_running_) {
        return;
    }

    is_running_ = false;
    frame_timer_->stop();
    status_label_->setText("已停止");

    spdlog::info("Recognition stopped");
}

void MainWindow::update_frame() {
    if (!recognition_app_ || !is_running_) {
        return;
    }

    // 处理单帧
    cv::Mat frame;
    std::vector<RecognitionResult> results;

    if (recognition_app_->process_single_frame(frame, results)) {
        // 更新视频显示
        video_widget_->update_frame(frame);

        // 转换识别结果为 FaceResult
        std::vector<FaceResult> face_results;
        for (const auto& result : results) {
            FaceResult fr;
            fr.box = result.face_box;
            fr.name = result.user_name;
            fr.similarity = result.similarity;
            fr.is_recognized = (result.user_id > 0);
            face_results.push_back(fr);
        }
        video_widget_->set_face_results(face_results);

        frame_count_++;

        // 计算 FPS
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fps_time_);
        if (duration.count() >= 1000) {
            fps_ = frame_count_ * 1000.0 / duration.count();
            frame_count_ = 0;
            last_fps_time_ = now;
            video_widget_->set_fps(fps_);
        }
    }
}

void MainWindow::update_status() {
    fps_label_->setText(QString("FPS: %1").arg(fps_, 0, 'f', 1));
}

void MainWindow::on_recognition_result(int user_id, const QString& name, float similarity) {
    recognition_label_->setText(QString("识别: %1 (%.2f)").arg(name).arg(similarity));

    // 更新考勤表格
    int row = attendance_table_->rowCount();
    attendance_table_->insertRow(row);
    attendance_table_->setItem(row, 0, new QTableWidgetItem(name));
    attendance_table_->setItem(row, 1, new QTableWidgetItem(
        QDateTime::currentDateTime().toString("hh:mm:ss")));
    attendance_table_->setItem(row, 2, new QTableWidgetItem("签到"));
    attendance_table_->setItem(row, 3, new QTableWidgetItem(
        QString::number(similarity, 'f', 2)));

    spdlog::info("Recognition: {} (ID: {}, similarity: {:.2f})",
                 name.toStdString(), user_id, similarity);
}

void MainWindow::on_action_open_camera() {
    start_recognition();
}

void MainWindow::on_action_close_camera() {
    stop_recognition();
}

void MainWindow::on_action_settings() {
    if (!recognition_app_) {
        QMessageBox::warning(this, "警告", "请先初始化系统");
        return;
    }

    // 获取当前配置（需要添加 getter 方法）
    AppConfig config;  // 临时配置
    SettingsDialog dialog(&config, this);
    dialog.exec();
}

void MainWindow::on_action_exit() {
    close();
}

void MainWindow::on_action_register_face() {
    if (!registration_dialog_) {
        registration_dialog_ = new FaceRegistrationDialog(
            recognition_app_.get(), user_service_.get(), this);
    }
    registration_dialog_->exec();
}

void MainWindow::on_action_query_attendance() {
    if (!attendance_query_widget_) {
        attendance_query_widget_ = new AttendanceQueryWidget(
            attendance_service_.get(), this);
    }
    attendance_query_widget_->show();
}

void MainWindow::on_action_user_management() {
    if (!user_service_) {
        QMessageBox::warning(this, "警告", "用户服务未初始化");
        return;
    }

    UserManagementWidget* user_mgmt = new UserManagementWidget(user_service_.get(), this);
    user_mgmt->setAttribute(Qt::WA_DeleteOnClose);
    user_mgmt->setWindowTitle("用户管理");
    user_mgmt->resize(800, 600);
    user_mgmt->show();
}

void MainWindow::on_action_about() {
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    stop_recognition();
    event->accept();
}

