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
#include "gui/about_dialog.h"
#include "themes/theme_manager.h"
#include "ui/attendance_page.h"
#include "ui/recognition_page.h"
#include "ui/settings_page.h"
#include "ui/user_management_page.h"
#include "utils/stack_router.h"
#include "utils/audio_manager.h"
#include "utils/config_manager.h"
#include "widgets/modern_table_view.h"
#include "widgets/side_menu.h"
#include "widgets/title_bar.h"
#include "widgets/toast_notification.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QCloseEvent>
#include <QTimer>
#include <QThread>
#include <QStackedWidget>
#include <QDateTime>
#include <QDate>
#include <QTableWidgetItem>
#include <QMenu>
#include <ctime>
#include <spdlog/spdlog.h>

// 注册 Qt 元类型（用于跨线程信号槽）
Q_DECLARE_METATYPE(cv::Mat)
Q_DECLARE_METATYPE(std::vector<RecognitionResult>)

// 定义 static constexpr 成员变量（C++17之前需要类外定义）
constexpr int MainWindow::USER_DETECTION_TIMEOUT_MS;
constexpr int MainWindow::DUPLICATE_CHECK_COOLDOWN_MS;
constexpr int MainWindow::STRANGER_AUDIO_COOLDOWN_MS;
constexpr int MainWindow::STRANGER_CONFIRM_DURATION_MS;
constexpr int MainWindow::STRANGER_DETECTION_TIMEOUT_MS;


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
    , news_service_(nullptr)
    , status_label_(nullptr)
    , fps_label_(nullptr)
    , recognition_label_(nullptr)
    , attendance_status_label_(nullptr)
    , user_name_label_(nullptr)
    , user_id_label_(nullptr)
    , user_dept_label_(nullptr)
    , user_similarity_label_(nullptr)
    , check_type_label_(nullptr)
    , status_timer_(nullptr)
    , registration_dialog_(nullptr)
    , is_running_(false)
    , frame_count_(0)
    , fps_(0.0)
    , camera_id_(0)
    , is_dark_theme_(false)  // 默认使用浅色主题
    , current_date_(QDate::currentDate())  // 初始化当前日期（用于跨日检测）
    , user_detection_{false, 0, "", 0.0f, std::chrono::steady_clock::now(), std::chrono::steady_clock::now(), false}
    , user_confirm_duration_ms_(1000)  // 默认1秒，从配置加载
    , stranger_detection_{false, std::chrono::steady_clock::now(), std::chrono::steady_clock::now()}
{
    // 初始化音频冷却时间（设置为10秒前，确保首次播放不会被阻止）
    auto init_time = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    last_audio_play_times_[AudioType::AlreadyCheckedIn] = init_time;
    last_audio_play_times_[AudioType::AlreadyCheckedOut] = init_time;
    last_audio_play_times_[AudioType::StrangerDetected] = init_time;
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

    // 清理动态分配的对话框
    if (registration_dialog_) {
        delete registration_dialog_;
        registration_dialog_ = nullptr;
    }

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
    
    // 初始化数据库
    db_manager_ = &db::DatabaseManager::instance();
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
    
    // 从配置文件加载识别阈值
    config.facenet_threshold = ConfigManager::instance()->getRecognitionThreshold();
    spdlog::info("Loaded recognition threshold from config: {:.2f}", config.facenet_threshold);

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

    // 4. 初始化音频管理器（从配置文件加载）
    AudioManager::instance()->setEnabled(ConfigManager::instance()->isAudioEnabled());
    AudioManager::instance()->setVolume(ConfigManager::instance()->getAudioVolume());
    QString audioDevice = ConfigManager::instance()->getAudioDevice();
    if (!audioDevice.isEmpty()) {
        AudioManager::instance()->setAudioDevice(audioDevice);
    }
    spdlog::info("AudioManager initialized from config");
    
    // 5. 配置考勤服务的工作时间规则（从配置文件加载）
    if (attendance_service_) {
        attendance_service_->set_work_schedule(
            ConfigManager::instance()->getWorkStartTime().toStdString(),
            ConfigManager::instance()->getWorkEndTime().toStdString(),
            ConfigManager::instance()->getLateThreshold(),
            ConfigManager::instance()->getEarlyLeaveThreshold(),
            ConfigManager::instance()->isAllowMultipleCheckin(),
            ConfigManager::instance()->getDuplicateCheckInterval()
        );
        spdlog::info("Attendance work schedule configured from settings");
    }

    // 6. 初始化热点服务（用于标题栏滚动）
    news_service_ = NewsService::instance();
    if (news_service_) {
        connect(news_service_, &NewsService::headlinesUpdated, this, [this](const QStringList& list) {
            if (title_bar_) {
                title_bar_->setHeadlines(list);
            }
        });
        connect(news_service_, &NewsService::errorOccurred, this, [](const QString& msg) {
            spdlog::warn("NewsService error: {}", msg.toStdString());
        });
    }

    // 设置识别回调（带连续确认机制，防止误识别导致错误签到）
    recognition_app_->set_recognition_callback([this](const RecognitionResult& result) {
        auto now = std::chrono::steady_clock::now();
        
        // 检查是否是陌生人
        bool is_stranger = (result.user_id == 0) || (result.user_name == "stranger");
        
        // ==================== 陌生人检测（基于持续时间 2秒） ====================
        if (is_stranger) {
            // 重置用户检测状态
            if (user_detection_.is_detecting) {
                spdlog::trace("Switched from user to stranger, reset user detection");
                user_detection_.is_detecting = false;
            }
            
            // 检查是否已经在检测陌生人
            if (stranger_detection_.is_detecting) {
                // 检查距离上次检测是否超时
                auto since_last = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - stranger_detection_.last_seen).count();
                
                if (since_last > STRANGER_DETECTION_TIMEOUT_MS) {
                    // 超时了，重新开始检测
                    stranger_detection_.first_seen = now;
                    stranger_detection_.last_seen = now;
                    spdlog::trace("Stranger detection restarted (timeout after {}ms)", since_last);
                } else {
                    // 没超时，更新最后检测时间
                    stranger_detection_.last_seen = now;
                    
                    // 检查是否已经持续检测了 2 秒
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - stranger_detection_.first_seen).count();
            
                    spdlog::trace("Stranger detection continued (duration: {}ms / {}ms)", 
                                  duration, STRANGER_CONFIRM_DURATION_MS);
            
                    if (duration >= STRANGER_CONFIRM_DURATION_MS) {
                        // 持续检测到陌生人超过 2 秒，播放提示音
                if (checkAudioCooldown(AudioType::StrangerDetected, STRANGER_AUDIO_COOLDOWN_MS)) {
                    AudioManager::instance()->playSound(AudioType::StrangerDetected);
                    updateAudioPlayTime(AudioType::StrangerDetected);
                            spdlog::info("Stranger confirmed after {}ms, played audio", duration);
                }
                
                        // 重置陌生人检测状态
                        stranger_detection_.is_detecting = false;
                    }
                }
            } else {
                // 第一次检测到陌生人，开始计时
                stranger_detection_.is_detecting = true;
                stranger_detection_.first_seen = now;
                stranger_detection_.last_seen = now;
                spdlog::trace("Stranger detection started");
            }
            return;
        }
        
        // ==================== 已注册用户识别（基于持续时间 1秒） ====================
        // 识别到已注册用户，重置陌生人检测状态
        if (stranger_detection_.is_detecting) {
            stranger_detection_.is_detecting = false;
                last_audio_play_times_[AudioType::StrangerDetected] = 
                    std::chrono::steady_clock::now() - std::chrono::seconds(20);
            spdlog::trace("Switched from stranger to user, reset stranger detection");
            }
            
        // 检查是否是同一个人的连续识别
        bool is_same_person = user_detection_.is_detecting && 
                             (result.user_id == user_detection_.user_id) && 
                             (result.user_id > 0);
        
        if (!is_same_person) {
            // 不是同一个人，开始新的检测
            user_detection_.is_detecting = true;
            user_detection_.user_id = result.user_id;
            user_detection_.user_name = result.user_name;
            user_detection_.max_similarity = result.similarity;
            user_detection_.first_seen = now;
            user_detection_.last_seen = now;
            user_detection_.attendance_recorded = false;
            
            // 第一次识别时更新显示，但不签到
            QMetaObject::invokeMethod(this, "on_recognition_result", Qt::QueuedConnection,
                                      Q_ARG(int, result.user_id),
                                      Q_ARG(QString, QString::fromStdString(result.user_name)),
                                      Q_ARG(float, result.similarity),
                                      Q_ARG(bool, false),
                                      Q_ARG(int, 1));
            
            spdlog::trace("User detection started: {} (similarity: {:.2f})", 
                          result.user_name, result.similarity);
            return;
        }
        
        // 是同一个人，检查是否超时
        auto since_last = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - user_detection_.last_seen).count();
        
        if (since_last > USER_DETECTION_TIMEOUT_MS) {
            // 超时了，重新开始检测
            user_detection_.first_seen = now;
            user_detection_.last_seen = now;
            user_detection_.max_similarity = result.similarity;
            user_detection_.attendance_recorded = false;
            spdlog::trace("User detection restarted (timeout after {}ms)", since_last);
            // 继续执行下面的 UI 更新，不要 return
        } else {
            // 没超时，更新检测状态
            user_detection_.last_seen = now;
            user_detection_.max_similarity = std::max(user_detection_.max_similarity, result.similarity);
        }
        
        // 计算检测持续时间
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - user_detection_.first_seen).count();
        
        spdlog::trace("User detection continued: {} (duration: {}ms / {}ms)", 
                      result.user_name, duration, user_confirm_duration_ms_);
        
        // 更新显示（使用实时相似度，不触发签到）
        QMetaObject::invokeMethod(this, "on_recognition_result", Qt::QueuedConnection,
                                  Q_ARG(int, result.user_id),
                                  Q_ARG(QString, QString::fromStdString(result.user_name)),
                                  Q_ARG(float, result.similarity),  // 使用实时值
                                  Q_ARG(bool, false),
                                  Q_ARG(int, 1));
        
        // 只有达到确认时长且未记录过考勤才真正签到
        if (duration >= user_confirm_duration_ms_ && !user_detection_.attendance_recorded) {
            bool is_new_attendance = false;
            
            // 重要：在 record_attendance 之前确定 check_type
            // 因为 record_attendance 会在数据库中插入记录，导致之后 auto_determine_check_type 返回下一次的类型
            std::time_t current_time = std::time(nullptr);
            int check_type = attendance_service_ ? 
                attendance_service_->auto_determine_check_type(result.user_id, current_time) : 1;
            
            // 记录考勤
            if (attendance_service_) {
                int record_id = attendance_service_->record_attendance(
                    result.user_id,
                    result.user_name,
                    user_detection_.max_similarity);
                is_new_attendance = (record_id > 0);
            }
            
            // 标记本次检测已记录考勤
            user_detection_.attendance_recorded = true;
            
            const char* type_str = (check_type == 2) ? "签退" : "签到";
            spdlog::info("User confirmed after {}ms: {} (similarity: {:.2f}, type: {}, is_new: {})",
                        duration, result.user_name, user_detection_.max_similarity, type_str, is_new_attendance);

            // 如果是新考勤记录（签到或签退），显示提示
            if (is_new_attendance) {
                // 根据类型播放不同音频
                if (check_type == 2) {  // CHECK_OUT
                    AudioManager::instance()->playSound(AudioType::CheckOutSuccess);
                } else {  // CHECK_IN
                    AudioManager::instance()->playSound(AudioType::CheckInSuccess);
                }
                
                // 在主线程更新 UI
                QMetaObject::invokeMethod(this, "on_recognition_result", Qt::QueuedConnection,
                                          Q_ARG(int, result.user_id),
                                          Q_ARG(QString, QString::fromStdString(result.user_name)),
                                          Q_ARG(float, user_detection_.max_similarity),
                                          Q_ARG(bool, true),
                                          Q_ARG(int, check_type));
            } else {
                // 重复打卡 - 使用独立的冷却机制避免频繁播放
                // 根据打卡类型选择对应的音频类型（签到和签退分别冷却）
                AudioType audio_type = (check_type == 2) ? 
                    AudioType::AlreadyCheckedOut : AudioType::AlreadyCheckedIn;
                
                // 检查该类型音频的独立冷却时间
                if (checkAudioCooldown(audio_type, DUPLICATE_CHECK_COOLDOWN_MS)) {
                    AudioManager::instance()->playSound(audio_type);
                    updateAudioPlayTime(audio_type);
                    
                    spdlog::debug("Played duplicate {} audio", type_str);
                }
            }
        }
    });
    
    // 从配置加载用户识别确认时间
    user_confirm_duration_ms_ = ConfigManager::instance()->getUserConfirmDuration();
    spdlog::info("Loaded user confirm duration from config: {}ms", user_confirm_duration_ms_);
    
    spdlog::info("System initialized successfully");

    // 6. 设置页面服务
    if (attendance_page_) {
        attendance_page_->setAttendanceService(attendance_service_.get());
    }
    if (user_page_) {
        user_page_->setUserService(user_service_.get());
    }

    // 7. 初始加载用户列表
    load_users();
    
    // 8. 加载今日考勤记录到右侧表格
    load_today_attendance();
    
    // 9. 初始刷新天气和每日一句
    if (recognition_page_) {
        recognition_page_->refreshWeather();
        recognition_page_->refreshDailySentence();
    }

    // 10. 初始拉取热点标题
    if (news_service_) {
        news_service_->requestHeadlines();
    }

    return true;
}

void MainWindow::apply_recognition_settings(float threshold) {
    if (recognition_app_) {
        recognition_app_->set_recognition_threshold(threshold);
        spdlog::info("Applied recognition threshold: {:.2f}", threshold);
    }
}

void MainWindow::apply_user_confirm_duration(int duration_ms) {
    // 限制范围 100-2000 ms
    user_confirm_duration_ms_ = std::max(100, std::min(2000, duration_ms));
    spdlog::info("Applied user confirm duration: {}ms ({:.1f}s)", 
                 user_confirm_duration_ms_, user_confirm_duration_ms_ / 1000.0);
}

bool MainWindow::checkAudioCooldown(AudioType audio_type, int cooldown_ms) {
    auto now = std::chrono::steady_clock::now();
    
    // 如果该音频类型从未播放过，可以播放
    if (last_audio_play_times_.find(audio_type) == last_audio_play_times_.end()) {
        return true;
    }
    
    // 检查距离上次播放的时间
    auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_audio_play_times_[audio_type]).count();
    
    if (time_since_last >= cooldown_ms) {
        return true;  // 冷却时间已过，可以播放
    } else {
        spdlog::debug("Audio cooldown active for type {}, {}ms remaining",
                     static_cast<int>(audio_type), cooldown_ms - time_since_last);
        return false;  // 还在冷却中
    }
}

void MainWindow::updateAudioPlayTime(AudioType audio_type) {
    last_audio_play_times_[audio_type] = std::chrono::steady_clock::now();
    spdlog::debug("Updated audio play time for type {}", static_cast<int>(audio_type));
}


void MainWindow::load_today_attendance() {
    if (!attendance_service_ || !attendance_table_) {
        spdlog::warn("Cannot load today attendance: service or table is null");
        return;
    }
    
    // 清空表格
    attendance_table_->setRowCount(0);
    
    // 查询今日考勤记录
    QDate today = QDate::currentDate();
    QString date_str = today.toString("yyyy-MM-dd");
    auto records = attendance_service_->query_records_by_date(date_str.toStdString());
    
    spdlog::info("Loading today's attendance: {} records found for {}", 
                 records.size(), date_str.toStdString());
    
    // 倒序添加（最新的在上面）
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        const auto& record = *it;
        
        int row = attendance_table_->rowCount();
        attendance_table_->insertRow(row);
        
        attendance_table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(record.user_name)));
        
        // 格式化时间为 MM-dd HH:mm:ss（带日期，便于确认是否是今天的记录）
        std::tm* tm_info = std::localtime(&record.check_time);
        char time_str[18];
        strftime(time_str, sizeof(time_str), "%m-%d %H:%M:%S", tm_info);
        attendance_table_->setItem(row, 1, new QTableWidgetItem(QString::fromUtf8(time_str)));
        
        // 显示打卡类型
        QString type_text = (record.check_type == 2) ? tr("签退") : tr("签到");
        attendance_table_->setItem(row, 2, new QTableWidgetItem(type_text));
        
        attendance_table_->setItem(row, 3, new QTableWidgetItem(
            QString::number(record.similarity, 'f', 2)));
    }
    
    spdlog::info("Today's attendance table updated: {} rows", attendance_table_->rowCount());
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
    user_name_label_ = recognition_page_->userNameLabel();
    user_id_label_ = recognition_page_->userIdLabel();
    user_dept_label_ = recognition_page_->userDeptLabel();
    user_similarity_label_ = recognition_page_->userSimilarityLabel();
    check_type_label_ = recognition_page_->checkTypeLabel();
    user_table_ = user_page_->table();
}

void MainWindow::setup_navigation() {
    if (!side_menu_ || !router_) {
        return;
    }

    // 使用新 SVG 图标系统
    QList<SideMenu::Item> items = {
        {"recognition", tr("实时画面"), ":/icons/navigation/home.svg"},
        {"attendance", tr("考勤记录"), ":/icons/navigation/calendar.svg"},
        {"users", tr("用户管理"), ":/icons/navigation/users.svg"},
        {"settings", tr("系统设置"), ":/icons/navigation/settings.svg"}
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
        connect(user_page_, &UserManagementPage::registerFaceRequested,
                this, &MainWindow::on_action_register_face);
    }

    if (settings_page_) {
        connect(settings_page_, &SettingsPage::themeToggleRequested,
                this, &MainWindow::on_action_toggle_theme);
        
        // 连接设置变更信号，实时应用识别阈值和考勤规则
        connect(settings_page_, &SettingsPage::settingsChanged,
                this, [this]() {
            if (settings_page_) {
                // 应用识别阈值
                float threshold = settings_page_->getRecognitionThreshold();
                apply_recognition_settings(threshold);
                
                // 应用用户识别确认时间
                int confirm_duration = settings_page_->getUserConfirmDuration();
                apply_user_confirm_duration(confirm_duration);
                
                // 应用考勤工作时间规则
                if (attendance_service_) {
                    attendance_service_->set_work_schedule(
                        ConfigManager::instance()->getWorkStartTime().toStdString(),
                        ConfigManager::instance()->getWorkEndTime().toStdString(),
                        ConfigManager::instance()->getLateThreshold(),
                        ConfigManager::instance()->getEarlyLeaveThreshold(),
                        ConfigManager::instance()->isAllowMultipleCheckin(),
                        ConfigManager::instance()->getDuplicateCheckInterval()
                    );
                    spdlog::info("Attendance work schedule updated from settings");
                }
            }
        });
        
        // 连接天气设置变更信号，刷新天气
        connect(settings_page_, &SettingsPage::weatherSettingsChanged,
                this, [this]() {
            if (recognition_page_) {
                spdlog::info("Weather settings changed, refreshing weather...");
                // 重置位置缓存，让它重新获取
                recognition_page_->resetLocationCache();
                recognition_page_->refreshWeather();
            }
        });
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
    
    // 重置用户检测状态
    user_detection_ = {false, 0, "", 0.0f, std::chrono::steady_clock::now(), std::chrono::steady_clock::now(), false};
    
    // 重置陌生人检测状态
    stranger_detection_ = {false, std::chrono::steady_clock::now(), std::chrono::steady_clock::now()};
    
    // 重置状态栏
    if (recognition_page_) {
        recognition_page_->updateFaceCount(0);
        recognition_page_->updateDetectionStatus(tr("已停止"), -1);
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

        // 检查是否已打卡（使用配置的间隔时间）并判断打卡类型
        fr.is_duplicate = false;
        fr.check_type = 1;  // 默认签到
        if (attendance_service_ && result.user_id > 0) {
            int dup_interval = ConfigManager::instance()->getDuplicateCheckInterval();
            fr.is_duplicate = attendance_service_->is_duplicate_check(result.user_id, dup_interval);
            // 自动判断打卡类型
            std::time_t current_time = std::time(nullptr);
            fr.check_type = attendance_service_->auto_determine_check_type(result.user_id, current_time);
        }

        face_results.push_back(fr);
    }
    if (video_widget_) {
    video_widget_->set_face_results(face_results);
    }
    
    // 更新状态栏的人脸检测数量
    if (recognition_page_) {
        recognition_page_->updateFaceCount(static_cast<int>(results.size()));
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

    // 更新状态栏的时钟和日期
    if (recognition_page_) {
        QDateTime current_datetime = QDateTime::currentDateTime();
        QDate today = current_datetime.date();
        
        // 跨日检测：如果日期变化，自动刷新签到表格
        if (today != current_date_) {
            spdlog::info("Date changed from {} to {}, refreshing attendance table",
                        current_date_.toString("yyyy-MM-dd").toStdString(),
                        today.toString("yyyy-MM-dd").toStdString());
            current_date_ = today;
            load_today_attendance();  // 重新加载今日签到记录
        }
        
        // 更新时钟 (HH:mm:ss)
        recognition_page_->updateClock(current_datetime.toString("HH:mm:ss"));
        
        // 更新日期 (yyyy年MM月dd日 周x)
        QString weekday;
        switch (today.dayOfWeek()) {
            case 1: weekday = tr("周一"); break;
            case 2: weekday = tr("周二"); break;
            case 3: weekday = tr("周三"); break;
            case 4: weekday = tr("周四"); break;
            case 5: weekday = tr("周五"); break;
            case 6: weekday = tr("周六"); break;
            case 7: weekday = tr("周日"); break;
        }
        recognition_page_->updateDate(current_datetime.toString("yyyy年MM月dd日 ") + weekday);
        
        // 更新签到/签退模式（基于当前时间）
        std::time_t current_time = std::time(nullptr);
        bool is_checkout_mode = false;
        if (attendance_service_) {
            // 使用虚拟用户ID 0 来判断当前时段
            int mode = attendance_service_->auto_determine_check_type(0, current_time);
            is_checkout_mode = (mode == 2);
        }
        recognition_page_->updateCheckMode(is_checkout_mode);
        
        // 每10秒更新一次考勤统计（减少数据库查询）
        static int stats_update_counter = 0;
        if (++stats_update_counter >= 10) {
            stats_update_counter = 0;
            
            if (attendance_service_) {
                // 获取今天的日期
                std::time_t now = std::time(nullptr);
                std::tm* tm_now = std::localtime(&now);
                char date_buf[16];
                std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", tm_now);
                
                auto stats = attendance_service_->get_statistics(std::string(date_buf));
                recognition_page_->updateAttendanceStats(
                    stats.check_in_count,
                    stats.check_out_count,
                    stats.late_count,
                    stats.early_leave_count
                );
            }
        }
        
        // 每10分钟刷新一次天气
        static int weather_update_counter = 0;
        if (++weather_update_counter >= Config::UI::WEATHER_REFRESH_INTERVAL_SEC) {
            weather_update_counter = 0;
            recognition_page_->refreshWeather();
        }
        
        // 每1分钟刷新一次每日一句
        static int sentence_update_counter = 0;
        if (++sentence_update_counter >= Config::UI::SENTENCE_REFRESH_INTERVAL_SEC) {
            sentence_update_counter = 0;
            recognition_page_->refreshDailySentence();
        }

        // 定期刷新热点标题
        static int news_update_counter = 0;
        if (++news_update_counter >= Config::UI::NEWS_REFRESH_INTERVAL_SEC) {
            news_update_counter = 0;
            if (news_service_) {
                news_service_->requestHeadlines();
            }
        }
    }
}

void MainWindow::on_recognition_result(int user_id, const QString& name, float similarity, bool is_new_attendance, int check_type) {
    spdlog::trace("on_recognition_result called: user_id={}, name={}, is_new={}, check_type={}", 
                  user_id, name.toStdString(), is_new_attendance, check_type);
    
    if (recognition_label_) {
        recognition_label_->setText(QString("识别: %1 (%2)").arg(name).arg(similarity, 0, 'f', 2));
    }

    // 更新状态栏的识别状态
    if (recognition_page_) {
        if (is_new_attendance) {
            // 识别成功并完成签到/签退
            QString status_text = (check_type == 2) ? tr("✓ 签退成功") : tr("✓ 签到成功");
            recognition_page_->updateDetectionStatus(status_text, -1);  // -1 隐藏进度条
        } else if (user_id > 0) {
            // 正在识别用户中
            if (user_detection_.is_detecting && user_detection_.user_id == user_id) {
                auto now = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - user_detection_.first_seen).count();
                int progress = std::min(100, static_cast<int>(duration * 100 / user_confirm_duration_ms_));
                recognition_page_->updateDetectionStatus(tr("识别中: %1").arg(name), progress);
            }
        }
    }

    // 更新用户信息面板（现代化卡片布局）- 轻量更新，只更新文字
    if (user_name_label_) {
        user_name_label_->setText(name);
    }
    
    if (user_similarity_label_) {
        user_similarity_label_->setText(QString("%1%").arg(QString::number(similarity * 100, 'f', 1)));
    }
    
    // 更新打卡类型标签 - 使用传入的 check_type，避免每次查询数据库
    if (check_type_label_) {
        QString type_text = (check_type == 2) ? tr("签退") : tr("签到");
        check_type_label_->setText(type_text);
        
        if (is_new_attendance) {
            check_type_label_->setStyleSheet("color: #52c41a; font-size: 18px; font-weight: bold; background: transparent;");
        } else {
            check_type_label_->setStyleSheet("color: #8c8c8c; font-size: 18px; font-weight: 500; background: transparent;");
        }
    }
    
    // 只在用户ID变化时获取用户详细信息（减少数据库查询）
    static int last_displayed_user_id = -1;
    if (user_id > 0 && user_id != last_displayed_user_id) {
        last_displayed_user_id = user_id;
        
        if (user_service_) {
            db::UserInfo user_info;
            if (user_service_->get_user(user_id, user_info)) {
                if (user_id_label_) {
                    user_id_label_->setText(QString("工号: %1").arg(QString::fromStdString(user_info.employee_id)));
                }
                if (user_dept_label_) {
                    user_dept_label_->setText(QString("部门: %1").arg(QString::fromStdString(user_info.department)));
                }
            }
        }
    } else if (user_id <= 0) {
        last_displayed_user_id = -1;
        if (user_id_label_) {
            user_id_label_->setText(tr("工号: --"));
        }
        if (user_dept_label_) {
            user_dept_label_->setText(tr("部门: --"));
        }
    }

    // 更新考勤表格和状态标签
    if (is_new_attendance) {
        spdlog::info("Processing new attendance: user_id={}, name={}, check_type={}", 
                     user_id, name.toStdString(), check_type);
        
        // 更新状态标签（签到绿色/签退蓝色，5秒后隐藏）
        if (attendance_status_label_) {
            QString msg = (check_type == 2) ? tr("✓ 签退成功") : tr("✓ 签到成功");
            attendance_status_label_->setText(msg);
            
            // 设置样式属性，区分签到/签退颜色
            attendance_status_label_->setProperty("checkType", (check_type == 2) ? "checkout" : "checkin");
            attendance_status_label_->style()->unpolish(attendance_status_label_);
            attendance_status_label_->style()->polish(attendance_status_label_);
            
            attendance_status_label_->setVisible(true);

            // 5秒后隐藏提示（容器固定宽度，不会导致布局抖动）
            QTimer::singleShot(5000, this, [this]() {
                if (attendance_status_label_) {
                    attendance_status_label_->setVisible(false);
                }
            });
        }
        
        // 更新今日签到表格（最新的在上面）
        if (attendance_table_) {
        spdlog::trace("Updating attendance_table: row count before = {}", attendance_table_->rowCount());
            
            attendance_table_->insertRow(0);  // 插入到第0行
            attendance_table_->setItem(0, 0, new QTableWidgetItem(name));
            // 时间格式：MM-dd HH:mm:ss（带日期，便于确认是否是今天的记录）
            attendance_table_->setItem(0, 1, new QTableWidgetItem(
                QDateTime::currentDateTime().toString("MM-dd HH:mm:ss")));
            
            // 根据打卡类型显示不同文字
            QString type_text = (check_type == 2) ? tr("签退") : tr("签到");
            attendance_table_->setItem(0, 2, new QTableWidgetItem(type_text));
            
            attendance_table_->setItem(0, 3, new QTableWidgetItem(
                QString::number(similarity, 'f', 2)));
            
            spdlog::trace("Attendance_table updated: row count after = {}", attendance_table_->rowCount());
            spdlog::info("Added to attendance table: {} (type: {}, ID: {}, similarity: {:.2f})",
                        name.toStdString(), type_text.toStdString(), user_id, similarity);
        } else {
            spdlog::error("attendance_table_ is nullptr!");
        }
    } else {
        spdlog::trace("Recognition (duplicate): {} (ID: {}, similarity: {:.2f})",
                      name.toStdString(), user_id, similarity);
    }
}

void MainWindow::on_action_open_camera() {
    start_recognition();
}

void MainWindow::on_action_close_camera() {
    stop_recognition();
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
        
        // 清空音频队列，避免注册时还在播放陌生人提示音
        AudioManager::instance()->clearQueue();
        spdlog::debug("Cleared audio queue before face registration");
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

        // 清空音频队列，避免注册音频影响后续识别播报
        // （用户已通过对话框知道注册结果，不需要等待音频播放完毕）
        AudioManager::instance()->clearQueue();
        spdlog::debug("Cleared audio queue before resuming recognition");

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
    // 连接人脸注册请求信号
    connect(user_mgmt, &UserManagementWidget::register_face_requested, this, &MainWindow::on_action_register_face);

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

