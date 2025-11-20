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

#include <QApplication>
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
#include <QTimer>
#include <QThread>
#include <spdlog/spdlog.h>

// 注册 Qt 元类型（用于跨线程信号槽）
Q_DECLARE_METATYPE(cv::Mat)
Q_DECLARE_METATYPE(std::vector<RecognitionResult>)

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
    , attendance_status_label_(nullptr)
    , status_timer_(nullptr)
    , registration_dialog_(nullptr)
    , is_running_(false)
    , frame_count_(0)
    , fps_(0.0)
    , camera_id_(0)
    , is_dark_theme_(false)  // 默认使用浅色主题
{
    // 注册 Qt 元类型（必须在使用前注册）
    qRegisterMetaType<cv::Mat>("cv::Mat");
    qRegisterMetaType<std::vector<RecognitionResult>>("std::vector<RecognitionResult>");

    setup_ui();

    // 初始化定时器（只保留状态更新定时器）
    status_timer_ = new QTimer(this);
    connect(status_timer_, &QTimer::timeout, this, &MainWindow::update_status);
    status_timer_->start(1000);  // 每秒更新一次状态

    last_fps_time_ = std::chrono::steady_clock::now();

    spdlog::info("MainWindow initialized");
}

void MainWindow::load_stylesheet() {
    const QString style = R"(
        /* Global */
        QWidget {
            font-family: "Segoe UI", "Microsoft YaHei", sans-serif;
            font-size: 14px;
            color: #F0F0F0;
            background-color: #2D2D2D;
        }
        
        /* Buttons */
        QPushButton {
            background-color: #3E3E42;
            border: 1px solid #555555;
            border-radius: 4px;
            padding: 6px 16px;
            min-height: 20px;
        }
        QPushButton:hover {
            background-color: #4E4E52;
            border-color: #007ACC;
        }
        QPushButton:pressed {
            background-color: #007ACC;
            border-color: #007ACC;
        }
        QPushButton:disabled {
            background-color: #2D2D2D;
            color: #666666;
            border-color: #444444;
        }
        
        /* Primary Button */
        QPushButton[class="primary"] {
            background-color: #007ACC;
            border: 1px solid #007ACC;
            color: white;
            font-weight: bold;
        }
        QPushButton[class="primary"]:hover {
            background-color: #1E8AD6;
        }
        QPushButton[class="primary"]:pressed {
            background-color: #005A9E;
        }
        QPushButton[class="primary"]:disabled {
            background-color: #2D2D2D;
            border-color: #444444;
            color: #666666;
        }

        /* Danger Button */
        QPushButton[class="danger"] {
            background-color: #C42B1C;
            border: 1px solid #C42B1C;
            color: white;
        }
        QPushButton[class="danger"]:hover {
            background-color: #D93626;
        }

        /* Inputs */
        QLineEdit, QComboBox, QDateEdit, QSpinBox {
            background-color: #1E1E1E;
            border: 1px solid #3E3E42;
            border-radius: 4px;
            padding: 5px;
            color: #F0F0F0;
            selection-background-color: #007ACC;
        }
        QLineEdit:focus, QComboBox:focus {
            border: 1px solid #007ACC;
        }
        QComboBox::drop-down {
            border: none;
            width: 20px;
        }

        /* Tables */
        QTableWidget {
            background-color: #1E1E1E;
            alternate-background-color: #252526;
            gridline-color: #3E3E42;
            border: 1px solid #3E3E42;
            selection-background-color: #007ACC;
            selection-color: white;
        }
        QHeaderView::section {
            background-color: #2D2D2D;
            padding: 8px;
            border: none;
            border-right: 1px solid #3E3E42;
            border-bottom: 1px solid #3E3E42;
            font-weight: bold;
        }
        QTableCornerButton::section {
            background-color: #2D2D2D;
            border: none;
        }

        /* Menu & Toolbar */
        QMenuBar {
            background-color: #2D2D2D;
            border-bottom: 1px solid #3E3E42;
        }
        QMenuBar::item:selected {
            background-color: #3E3E42;
        }
        QMenu {
            background-color: #2D2D2D;
            border: 1px solid #3E3E42;
        }
        QMenu::item:selected {
            background-color: #007ACC;
        }
        QToolBar {
            background-color: #2D2D2D;
            border-bottom: 1px solid #3E3E42;
            spacing: 8px;
            padding: 4px;
        }
        QToolButton {
            background-color: transparent;
            border-radius: 4px;
            padding: 4px;
        }
        QToolButton:hover {
            background-color: #3E3E42;
        }

        /* Dock Widget */
        QDockWidget {
            titlebar-close-icon: url(:/icons/close.png);
            titlebar-normal-icon: url(:/icons/float.png);
        }
        QDockWidget::title {
            background-color: #252526;
            padding: 8px;
            border-bottom: 1px solid #3E3E42;
            font-weight: bold;
        }

        /* Status Bar */
        QStatusBar {
            background-color: #007ACC;
            color: white;
        }
        QStatusBar QLabel {
            color: white;
        }

        /* GroupBox */
        QGroupBox {
            border: 1px solid #3E3E42;
            border-radius: 6px;
            margin-top: 24px;
            font-weight: bold;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 0 5px;
            left: 10px;
        }
        
        /* List Widget */
        QListWidget {
            background-color: #1E1E1E;
            border: 1px solid #3E3E42;
            border-radius: 4px;
        }
        QListWidget::item {
            padding: 4px;
        }
        QListWidget::item:selected {
            background-color: #007ACC;
        }
        
        /* ScrollBar */
        QScrollBar:vertical {
            border: none;
            background: #2D2D2D;
            width: 10px;
            margin: 0px;
        }
        QScrollBar::handle:vertical {
            background: #555;
            min-height: 20px;
            border-radius: 5px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
    )";
    
    // 应用到全局 Application
    if (qApp) {
        qApp->setStyleSheet(style);
    }
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

    // 1. 先初始化识别应用（包含特征库）
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

    // 2. 初始化服务（传递识别应用的特征库指针）
    // 关键修复：确保 UserService 使用与 FaceRecognitionApp 相同的 FeatureLibrary 实例
    user_service_ = std::make_unique<service::UserService>(
        db_manager_,
        &recognition_app_->get_feature_library()  // 传递特征库指针
    );
    attendance_service_ = std::make_unique<service::AttendanceService>(db_manager_);

    // 3. 设置考勤服务（用于检查是否已签到）
    recognition_app_->set_attendance_service(attendance_service_.get());

    // 设置识别回调
    recognition_app_->set_recognition_callback([this](const RecognitionResult& result) {
        // 记录考勤（先记录，再发送信号）
        bool is_new_attendance = false;
        if (attendance_service_) {
            int record_id = attendance_service_->record_attendance(result.user_id,
                                                  result.user_name,
                                                  result.similarity);
            is_new_attendance = (record_id > 0);  // 如果返回 > 0，说明是新签到
        }

        // 发送识别结果信号（包含是否新签到的信息）
        emit on_recognition_result(result.user_id,
                                   QString::fromStdString(result.user_name),
                                   result.similarity,
                                   is_new_attendance);

        // 如果是新签到，显示提示
        if (is_new_attendance) {
            QMetaObject::invokeMethod(this, [this, name = result.user_name]() {
                attendance_status_label_->setText(QString("✓ %1 签到成功").arg(QString::fromStdString(name)));
                attendance_status_label_->setVisible(true);

                // 3秒后隐藏提示
                QTimer::singleShot(3000, this, [this]() {
                    attendance_status_label_->setVisible(false);
                });
            }, Qt::QueuedConnection);
        }
    });
    
    spdlog::info("System initialized successfully");

    // 4. 初始加载用户列表
    load_users();

    return true;
}

void MainWindow::load_users() {
    if (!user_service_) {
        return;
    }

    // 从数据库重新加载用户列表
    auto users = user_service_->get_all_users(-1);  // -1 表示加载所有状态的用户

    // 更新用户表格
    if (user_table_) {
        user_table_->setRowCount(0);  // 清空现有行

        for (const auto& user : users) {
            int row = user_table_->rowCount();
            user_table_->insertRow(row);

            user_table_->setItem(row, 0, new QTableWidgetItem(QString::number(user.user_id)));
            user_table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(user.user_name)));
            user_table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(user.employee_id)));
            user_table_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(user.department)));
            user_table_->setItem(row, 4, new QTableWidgetItem(user.status == 1 ? "启用" : "禁用"));

            // 获取特征数量
            int feature_count = user_service_->get_feature_count(user.user_id);
            user_table_->setItem(row, 5, new QTableWidgetItem(QString::number(feature_count)));
        }

        spdlog::info("User list refreshed: {} users loaded", users.size());
    }
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

    settings_menu->addSeparator();

    QAction* theme_action = settings_menu->addAction("切换主题(&T)");
    connect(theme_action, &QAction::triggered, this, &MainWindow::on_action_toggle_theme);

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

    toolbar->addSeparator();

    // 主题切换
    QAction* theme_action = toolbar->addAction("🌓 切换主题");
    connect(theme_action, &QAction::triggered, this, &MainWindow::on_action_toggle_theme);
}

void MainWindow::create_status_bar() {
    status_label_ = new QLabel("就绪");
    statusBar()->addWidget(status_label_, 1);

    fps_label_ = new QLabel("FPS: 0");
    statusBar()->addPermanentWidget(fps_label_);

    recognition_label_ = new QLabel("未识别");
    statusBar()->addPermanentWidget(recognition_label_);

    // 签到状态提示标签（初始隐藏）
    attendance_status_label_ = new QLabel("");
    attendance_status_label_->setStyleSheet("QLabel { color: white; background-color: green; padding: 5px; font-weight: bold; }");
    attendance_status_label_->setVisible(false);
    statusBar()->addPermanentWidget(attendance_status_label_);
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
    if (is_running_ || !recognition_app_) {
        return;
    }

    is_running_ = true;
    status_label_->setText("运行中");

    // 设置帧回调（使用 Qt 信号槽机制确保线程安全）
    recognition_app_->set_frame_callback([this](const cv::Mat& frame, const std::vector<RecognitionResult>& results) {
        // 使用 QMetaObject::invokeMethod 确保在主线程中调用槽函数
        QMetaObject::invokeMethod(this, "on_frame_ready", Qt::QueuedConnection,
                                 Q_ARG(cv::Mat, frame),
                                 Q_ARG(std::vector<RecognitionResult>, results));
    });

    // 启动后台识别线程
    recognition_thread_ = std::thread([this]() {
        spdlog::info("Recognition thread started");
        recognition_app_->run();
        spdlog::info("Recognition thread stopped");
    });

    spdlog::info("Recognition started in background thread");
}

void MainWindow::stop_recognition() {
    if (!is_running_) {
        return;
    }

    is_running_ = false;
    status_label_->setText("已停止");

    // 停止识别应用
    if (recognition_app_) {
        recognition_app_->stop();
    }

    // 等待后台线程结束
    if (recognition_thread_.joinable()) {
        recognition_thread_.join();
    }

    spdlog::info("Recognition stopped");
}

void MainWindow::on_frame_ready(const cv::Mat& frame, const std::vector<RecognitionResult>& results) {
    if (!is_running_) {
        return;
    }

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

        // 检查是否已签到（5分钟内）
        fr.is_duplicate = false;
        if (attendance_service_ && result.user_id > 0) {
            fr.is_duplicate = attendance_service_->is_duplicate_check(result.user_id, 300);
        }

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

void MainWindow::update_status() {
    fps_label_->setText(QString("FPS: %1").arg(fps_, 0, 'f', 1));
}

void MainWindow::on_recognition_result(int user_id, const QString& name, float similarity, bool is_new_attendance) {
    recognition_label_->setText(QString("识别: %1 (%2)").arg(name).arg(similarity, 0, 'f', 2));

    // 只有新签到时才更新考勤表格
    if (is_new_attendance) {
        int row = attendance_table_->rowCount();
        attendance_table_->insertRow(row);
        attendance_table_->setItem(row, 0, new QTableWidgetItem(name));
        attendance_table_->setItem(row, 1, new QTableWidgetItem(
            QDateTime::currentDateTime().toString("hh:mm:ss")));
        attendance_table_->setItem(row, 2, new QTableWidgetItem("签到"));
        attendance_table_->setItem(row, 3, new QTableWidgetItem(
            QString::number(similarity, 'f', 2)));

        spdlog::info("New attendance recorded: {} (ID: {}, similarity: {:.2f})",
                     name.toStdString(), user_id, similarity);
    } else {
        spdlog::debug("Recognition (duplicate): {} (ID: {}, similarity: {:.2f})",
                      name.toStdString(), user_id, similarity);
    }
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
    // 记录识别线程是否正在运行
    bool was_running = is_running_;

    // 如果识别线程正在运行，先暂停（避免摄像头资源冲突）
    if (was_running) {
        spdlog::info("Pausing recognition for face registration");
        stop_recognition();

        // 等待线程完全停止（确保摄像头资源释放）
        QThread::msleep(200);
    }

    // 创建并显示注册对话框
    if (!registration_dialog_) {
        registration_dialog_ = new FaceRegistrationDialog(
            recognition_app_.get(), user_service_.get(), this);
    }

    int result = registration_dialog_->exec();

    // 如果之前识别线程在运行，恢复运行
    if (was_running) {
        spdlog::info("Resuming recognition after face registration");

        // 短暂延迟，确保对话框资源完全释放
        QThread::msleep(200);

        start_recognition();
    }

    // 如果注册成功，刷新用户列表
    if (result == QDialog::Accepted) {
        load_users();
    }
}

void MainWindow::on_action_query_attendance() {
    // 每次创建新窗口，避免窗口关闭问题
    AttendanceQueryWidget* query_widget = new AttendanceQueryWidget(
        attendance_service_.get(), nullptr);  // parent 设为 nullptr
    query_widget->setAttribute(Qt::WA_DeleteOnClose);  // 关闭时自动删除
    query_widget->setWindowFlags(Qt::Window);  // 设置为独立窗口
    query_widget->setWindowTitle("考勤查询");
    query_widget->resize(900, 600);
    query_widget->show();
}

void MainWindow::on_action_user_management() {
    if (!user_service_) {
        QMessageBox::warning(this, "警告", "用户服务未初始化");
        return;
    }

    UserManagementWidget* user_mgmt = new UserManagementWidget(user_service_.get(), nullptr);  // parent 设为 nullptr
    user_mgmt->setAttribute(Qt::WA_DeleteOnClose);  // 关闭时自动删除
    user_mgmt->setWindowFlags(Qt::Window);  // 设置为独立窗口
    user_mgmt->setWindowTitle("用户管理");
    user_mgmt->resize(800, 600);

    // 连接信号槽：当用户管理窗口数据变更时，刷新主窗口的用户列表
    connect(user_mgmt, &UserManagementWidget::data_changed, this, &MainWindow::load_users);

    user_mgmt->show();
}

void MainWindow::on_action_about() {
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::on_action_toggle_theme() {
    is_dark_theme_ = !is_dark_theme_;

    if (is_dark_theme_) {
        // 切换到深色主题
        load_stylesheet();
        spdlog::info("Switched to dark theme");
    } else {
        // 切换到浅色主题（清除样式表）
        if (qApp) {
            qApp->setStyleSheet("");
        }
        spdlog::info("Switched to light theme");
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // 确认退出
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "确认退出",
        "确定要退出人脸识别考勤系统吗？",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        spdlog::info("User confirmed exit, stopping recognition...");
        stop_recognition();
        event->accept();
    } else {
        event->ignore();
    }
}

