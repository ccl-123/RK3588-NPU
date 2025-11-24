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
#include "themes/theme_manager.h"
#include "ui/attendance_page.h"
#include "ui/recognition_page.h"
#include "ui/settings_page.h"
#include "ui/user_management_page.h"
#include "utils/stack_router.h"
#include "widgets/modern_table_view.h"
#include "widgets/side_menu.h"
#include "widgets/title_bar.h"
#include "widgets/toast_notification.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QCloseEvent>
#include <QTimer>
#include <QThread>
#include <QStackedWidget>
#include <QAction>
#include <QIcon>
#include <QDateTime>
#include <QTableWidgetItem>
#include <QList>
#include <QMenu>
#include <QStatusBar>
#include <spdlog/spdlog.h>

// 注册 Qt 元类型（用于跨线程信号槽）
Q_DECLARE_METATYPE(cv::Mat)
Q_DECLARE_METATYPE(std::vector<RecognitionResult>)

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , db_manager_(nullptr)
    , video_widget_(nullptr)
    , user_table_(nullptr)
    , attendance_table_(nullptr)
    , side_menu_(nullptr)
    , title_bar_(nullptr)
    , content_stack_(nullptr)
    , router_(nullptr)
    , recognition_page_(nullptr)
    , attendance_page_(nullptr)
    , user_page_(nullptr)
    , settings_page_(nullptr)
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
                if (!attendance_status_label_) {
                    return;
                }
                attendance_status_label_->setText(QString("✓ %1 签到成功").arg(QString::fromStdString(name)));
                attendance_status_label_->setVisible(true);

                // 3秒后隐藏提示
                QTimer::singleShot(3000, this, [this]() {
                    if (attendance_status_label_) {
                    attendance_status_label_->setVisible(false);
                    }
                });
            }, Qt::QueuedConnection);
        }
    });
    
    spdlog::info("System initialized successfully");

    // 4. 设置页面服务
    if (attendance_page_) {
        attendance_page_->setAttendanceService(attendance_service_.get());
    }
    if (user_page_) {
        user_page_->setUserService(user_service_.get());
    }

    // 5. 初始加载用户列表
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
    resize(1440, 900);

    QWidget* host = new QWidget(this);
    setCentralWidget(host);

    auto root_layout = new QVBoxLayout(host);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    title_bar_ = new TitleBar(this);
    title_bar_->setTitle(tr("人脸识别考勤系统"));
    root_layout->addWidget(title_bar_);

    auto user_menu = new QMenu(title_bar_);
    user_menu->addAction(tr("关于"), this, &MainWindow::on_action_about);
    user_menu->addSeparator();
    user_menu->addAction(tr("退出"), this, &MainWindow::on_action_exit);
    title_bar_->setUserMenu(user_menu);

    auto body = new QWidget(host);
    auto body_layout = new QHBoxLayout(body);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(0);
    root_layout->addWidget(body, 1);

    side_menu_ = new SideMenu(body);
    body_layout->addWidget(side_menu_);

    content_stack_ = new QStackedWidget(body);
    body_layout->addWidget(content_stack_, 1);

    setup_pages();
    setup_navigation();
    connect_page_signals();
    apply_theme();

    connect(title_bar_, &TitleBar::requestMinimize, this, &MainWindow::showMinimized);
    connect(title_bar_, &TitleBar::requestClose, this, &MainWindow::close);
    connect(title_bar_, &TitleBar::requestToggleTheme, this, &MainWindow::on_action_toggle_theme);

    if (statusBar()) {
        statusBar()->hide();
    }
}

void MainWindow::setup_pages() {
    if (!content_stack_) {
        return;
    }

    router_ = new UiRouter(content_stack_, this);

    recognition_page_ = new RecognitionPage(content_stack_);
    attendance_page_ = new AttendancePage(content_stack_);
    user_page_ = new UserManagementPage(content_stack_);
    settings_page_ = new SettingsPage(content_stack_);

    router_->registerPage("recognition", recognition_page_);
    router_->registerPage("attendance", attendance_page_);
    router_->registerPage("users", user_page_);
    router_->registerPage("settings", settings_page_);

    video_widget_ = recognition_page_->videoWidget();
    attendance_table_ = recognition_page_->attendanceTable();
    status_label_ = recognition_page_->statusLabel();
    fps_label_ = recognition_page_->fpsLabel();
    recognition_label_ = recognition_page_->recognitionLabel();
    attendance_status_label_ = recognition_page_->attendanceStatusLabel();
    user_table_ = user_page_->table();
}

void MainWindow::setup_navigation() {
    if (!side_menu_ || !router_) {
        return;
    }

    QList<SideMenu::Item> items = {
        {"recognition", tr("实时画面"), ""},
        {"attendance", tr("考勤记录"), ""},
        {"users", tr("用户管理"), ""},
        {"settings", tr("系统设置"), ""}
    };
    side_menu_->setItems(items);

    connect(side_menu_, &SideMenu::routeChanged, this, [this](const QString& key) {
        if (router_) {
            router_->navigateTo(key);
}
    });

    connect(router_, &UiRouter::routeChanged, this, [this](const QString& key, QWidget*) {
        QString breadcrumb;
        if (key == "recognition") {
            breadcrumb = tr("实时画面");
        } else if (key == "attendance") {
            breadcrumb = tr("考勤记录");
        } else if (key == "users") {
            breadcrumb = tr("用户管理");
        } else if (key == "settings") {
            breadcrumb = tr("系统设置");
        }
        if (title_bar_) {
            title_bar_->setBreadcrumb({breadcrumb});
        }
    });

    router_->navigateTo("recognition");
    side_menu_->setActiveKey("recognition");
}

void MainWindow::connect_page_signals() {
    if (recognition_page_) {
        connect(recognition_page_, &RecognitionPage::startRecognitionRequested,
                this, &MainWindow::on_action_open_camera);
        connect(recognition_page_, &RecognitionPage::stopRecognitionRequested,
                this, &MainWindow::on_action_close_camera);
        connect(recognition_page_, &RecognitionPage::registerFaceRequested,
                this, &MainWindow::on_action_register_face);
    }

    if (user_page_) {
        connect(user_page_, &UserManagementPage::dataChanged,
                this, &MainWindow::load_users);
    }

    if (settings_page_) {
        connect(settings_page_, &SettingsPage::themeToggleRequested,
                this, &MainWindow::on_action_toggle_theme);
    }
}

void MainWindow::apply_theme() {
    ThemeManager::apply(is_dark_theme_ ? ThemeManager::Theme::Dark
                                       : ThemeManager::Theme::Light);
}

void MainWindow::start_recognition() {
    if (is_running_ || !recognition_app_) {
        return;
    }

    is_running_ = true;
    if (status_label_) {
    status_label_->setText("运行中");
    }

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
    if (status_label_) {
    status_label_->setText("已停止");
    }

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
    if (video_widget_) {
    video_widget_->update_frame(frame);
    }

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
    if (video_widget_) {
    video_widget_->set_face_results(face_results);
    }

    frame_count_++;

    // 计算 FPS
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fps_time_);
    if (duration.count() >= 1000) {
        fps_ = frame_count_ * 1000.0 / duration.count();
        frame_count_ = 0;
        last_fps_time_ = now;
        if (video_widget_) {
        video_widget_->set_fps(fps_);
        }
    }
}

void MainWindow::update_status() {
    if (fps_label_) {
    fps_label_->setText(QString("FPS: %1").arg(fps_, 0, 'f', 1));
    }
}

void MainWindow::on_recognition_result(int user_id, const QString& name, float similarity, bool is_new_attendance) {
    recognition_label_->setText(QString("识别: %1 (%2)").arg(name).arg(similarity, 0, 'f', 2));

    // 只有新签到时才更新考勤表格
    if (is_new_attendance && attendance_table_) {
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
    apply_theme();
    spdlog::info("Theme toggled: {}", is_dark_theme_ ? "dark" : "light");
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

