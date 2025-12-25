/**
 * @file main_window.cc
 * @brief 主窗口类实现
 * @author CL
 * @date 2025-11-20
 */

#include "gui/main_window.h"
#include "gui/video_display_widget.h"
#include "gui/face_registration_dialog.h"
#include "gui/about_dialog.h"
#include "themes/theme_manager.h"
#include "ui/attendance_page.h"
#include "ui/dashboard_page.h"
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
#include "widgets/attendance_list_widget.h"
#include "app/local_llm_thread.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QCloseEvent>
#include <QTimer>
#include <QThread>
#include <QStackedWidget>
#include <QDateTime>
#include <QDate>
#include <QCoreApplication>
#include <QTableWidgetItem>
#include <QMenu>
#include <ctime>
#include <spdlog/spdlog.h>

namespace {
struct InitPayload {
    std::unique_ptr<FaceRecognitionApp> recognition_app;
};

QString resolve_photo_path(const std::string& raw_path) {
    if (raw_path.empty()) {
        return {};
    }
    QString path = QString::fromStdString(raw_path);
    QFileInfo info(path);
    if (info.isAbsolute()) {
        return path;
    }
    QString base_dir = QCoreApplication::applicationDirPath();
    return QDir(base_dir).filePath(path);
}

QPixmap make_circular_pixmap(const QPixmap& source, int size) {
    if (source.isNull() || size <= 0) {
        return QPixmap();
    }

    QPixmap scaled = source.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap output(size, size);
    output.fill(Qt::transparent);

    QPainter painter(&output);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addEllipse(0, 0, size, size);
    painter.setClipPath(path);

    QRect target(0, 0, size, size);
    QRect source_rect((scaled.width() - size) / 2, (scaled.height() - size) / 2, size, size);
    painter.drawPixmap(target, scaled, source_rect);
    painter.setPen(QPen(QColor(255, 255, 255, 80), 1));
    painter.drawEllipse(0, 0, size - 1, size - 1);
    return output;
}
}  // namespace

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
    , attendance_list_(nullptr)
    , side_menu_(nullptr)
    , title_bar_(nullptr)
    , content_stack_(nullptr)
    , router_(nullptr)
    , recognition_page_(nullptr)
    , dashboard_page_(nullptr)
    , attendance_page_(nullptr)
    , user_page_(nullptr)
    , settings_page_(nullptr)
    , holiday_service_(nullptr)
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
    , recognition_paused_for_llm_(false)
    , npu_fps_(0.0)
    , camera_fps_(0.0)
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
    closing_.store(true, std::memory_order_release);
    stop_recognition();

    if (init_thread_.joinable()) {
        init_thread_.join();
    }

    // 清理动态分配的对话框
    if (registration_dialog_) {
        delete registration_dialog_;
        registration_dialog_ = nullptr;
    }

    spdlog::info("MainWindow destroyed");
}

bool MainWindow::finish_initialization_after_core() {
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

    // 7. 初始化节假日服务
    holiday_service_ = HolidayService::instance();
    if (holiday_service_ && recognition_page_) {
        connect(holiday_service_, &HolidayService::holidayStatusUpdated,
                recognition_page_, &RecognitionPage::updateHolidayStatus);
        connect(holiday_service_, &HolidayService::errorOccurred, this, [](const QString& msg) {
            spdlog::warn("HolidayService error: {}", msg.toStdString());
        });
        holiday_service_->requestTodayAndNext();
    }

    // 设置识别回调（带连续确认机制，防止误识别导致错误签到）
    recognition_app_->set_recognition_callback([this](const RecognitionResult& result) {
        if (closing_.load(std::memory_order_acquire)) {
            return;
        }
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
                                      Q_ARG(int, 1), // check_type 默认为1
                                      Q_ARG(int, 1)); // status 默认为1
            
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
            
            int attendance_status = 1;
            if (attendance_service_) {
                attendance_status = attendance_service_->determine_status(current_time, check_type);
            }

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
                                          Q_ARG(int, check_type),
                                          Q_ARG(int, attendance_status));
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
        attendance_page_->setUserService(user_service_.get());
    }
    if (dashboard_page_) {
        dashboard_page_->setAttendanceService(attendance_service_.get());
        dashboard_page_->setUserService(user_service_.get());
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

void MainWindow::initialize_async(const std::string& retinaface_model,
                                 const std::string& facenet_model,
                                 const std::string& camera_source,
                                 int camera_id,
                                 const std::string& db_path) {
    if (closing_.load(std::memory_order_acquire)) {
        return;
    }

    bool expected = false;
    if (!init_in_progress_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        spdlog::warn("System initialization already in progress, ignored");
        return;
    }

    retinaface_model_ = retinaface_model;
    facenet_model_ = facenet_model;
    camera_source_ = camera_source;
    camera_id_ = camera_id;

    if (status_label_) {
        status_label_->setText(tr("初始化中..."));
    }

    const float recognition_threshold = ConfigManager::instance()->getRecognitionThreshold();

    if (init_thread_.joinable()) {
        init_thread_.join();
    }

    init_thread_ = std::thread([this,
                                retinaface_model,
                                facenet_model,
                                camera_source,
                                camera_id,
                                db_path,
                                recognition_threshold]() {
        auto payload = std::make_shared<InitPayload>();
        payload->recognition_app = std::make_unique<FaceRecognitionApp>();

        bool ok = true;
        QString error_message;

        // 1) 初始化数据库（重）
        db::DatabaseManager* db_manager = &db::DatabaseManager::instance();
        if (!db_manager->initialize(db_path)) {
            ok = false;
            error_message = tr("数据库初始化失败");
        }

        // 2) 初始化识别应用（模型 + 特征库 + 摄像头等，重）
        if (ok) {
            AppConfig config;
            config.retinaface_model_path = retinaface_model;
            config.facenet_model_path = facenet_model;
            config.camera_type = camera_source;
            config.device_number = std::to_string(camera_id);
            config.use_database = true;
            config.database_path = db_path;
            config.facenet_threshold = recognition_threshold;

            spdlog::info("Loaded recognition threshold from config: {:.2f}", config.facenet_threshold);

            if (payload->recognition_app->initialize(config) != 0) {
                ok = false;
                error_message = tr("人脸识别系统初始化失败");
            }
        }

        QMetaObject::invokeMethod(
            this,
            [this, payload, ok, error_message]() {
                init_in_progress_.store(false, std::memory_order_release);

                if (closing_.load(std::memory_order_acquire)) {
                    return;
                }

                if (!ok) {
                    if (status_label_) {
                        status_label_->setText(tr("初始化失败"));
                    }
                    QMessageBox::critical(this, tr("错误"), error_message);
                    spdlog::error("System initialization failed: {}", error_message.toStdString());
                    QCoreApplication::exit(-1);
                    return;
                }

                // 完成：将重资源挂到主线程对象上（避免跨线程访问 UI 成员）
                db_manager_ = &db::DatabaseManager::instance();
                recognition_app_ = std::move(payload->recognition_app);

                if (!finish_initialization_after_core()) {
                    QMessageBox::critical(this, tr("错误"), tr("系统初始化失败"));
                    spdlog::error("finish_initialization_after_core failed");
                    QCoreApplication::exit(-1);
                    return;
                }

                // 检查摄像头状态
                if (!recognition_app_->is_camera_initialized()) {
                    QString camera_error = QString::fromStdString(recognition_app_->get_camera_error());
                    spdlog::warn("Camera not initialized: {}", camera_error.toStdString());

                    if (status_label_) {
                        status_label_->setText(tr("就绪 (摄像头未连接)"));
                    }

                    // 显示友好提示，但不阻止应用启动
                    QMessageBox::warning(this, tr("摄像头未连接"),
                        tr("摄像头初始化失败：\n%1\n\n"
                           "应用已启动，但摄像头功能不可用。\n"
                           "您可以在【设置】页面重新选择摄像头设备。").arg(camera_error));

                    // 不启动识别，因为没有摄像头
                    return;
                }

                if (status_label_) {
                    status_label_->setText(tr("就绪"));
                }

                // 自动启动识别（初始化完成后，且摄像头可用）
                start_recognition();
            },
            Qt::QueuedConnection);
    });
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

    return finish_initialization_after_core();
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
    if (!attendance_service_ || !attendance_list_) {
        return;
    }

    // 获取今日所有考勤记录
    std::string today_str = QDate::currentDate().toString("yyyy-MM-dd").toStdString();
    auto records = attendance_service_->query_records_by_date(today_str);
    
    // 清空列表
    attendance_list_->clear();
    
    // 遍历记录添加到列表
    // 数据库返回通常是按时间顺序（早->晚），addRecord 是插入到顶部
    // 所以最终列表显示是：晚（顶）-> 早（底），符合 Feed 流习惯
    for (const auto& record : records) {
        AttendanceItem item;
        item.user_id = record.user_id;
        item.name = QString::fromStdString(record.user_name);
        
        // 获取用户详情（部门）
        if (user_service_) {
            db::UserInfo user_info;
            if (user_service_->get_user(record.user_id, user_info)) {
                item.department = QString::fromStdString(user_info.department);
                item.avatar_path = resolve_photo_path(user_info.photo_path);
            }
        }
        
        item.time = QDateTime::fromTime_t(record.check_time);
        item.check_type = record.check_type;
        item.status = record.status; // 从数据库记录获取考勤状态
        item.similarity = record.similarity;
        // item.is_stranger 已从 AttendanceItem 结构体中移除
        
        attendance_list_->addRecord(item);
    }
    
    spdlog::info("Today's attendance list refreshed: {} records", records.size());

    if (dashboard_page_) {
        dashboard_page_->refreshData();
    }
}


void MainWindow::load_users() {
    if (user_page_) {
        user_page_->load_users();
    }
    if (dashboard_page_) {
        dashboard_page_->refreshData();
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
    connect(title_bar_, &TitleBar::requestMaximize, this, &MainWindow::on_action_toggle_maximize);
    connect(title_bar_, &TitleBar::requestClose, this, &MainWindow::close);
    connect(title_bar_, &TitleBar::requestToggleTheme, this, &MainWindow::on_action_toggle_theme);
}

void MainWindow::setup_pages() {
    if (!content_stack_) {
        return;
    }

    router_ = new UiRouter(content_stack_, this);

    recognition_page_ = new RecognitionPage(content_stack_);
    dashboard_page_ = new DashboardPage(content_stack_);
    attendance_page_ = new AttendancePage(content_stack_);
    user_page_ = new UserManagementPage(content_stack_);
    settings_page_ = new SettingsPage(content_stack_);

    router_->registerPage("recognition", recognition_page_);
    router_->registerPage("dashboard", dashboard_page_);
    router_->registerPage("attendance", attendance_page_);
    router_->registerPage("users", user_page_);
    router_->registerPage("settings", settings_page_);

    video_widget_ = recognition_page_->videoWidget();
    attendance_list_ = recognition_page_->attendanceList();
    status_label_ = recognition_page_->statusLabel();
    fps_label_ = recognition_page_->fpsLabel();
    recognition_label_ = recognition_page_->recognitionLabel();
    attendance_status_label_ = recognition_page_->attendanceStatusLabel();
    user_name_label_ = recognition_page_->userNameLabel();
    user_id_label_ = recognition_page_->userIdLabel();
    user_dept_label_ = recognition_page_->userDeptLabel();
    user_similarity_label_ = recognition_page_->userSimilarityLabel();
    check_type_label_ = recognition_page_->checkTypeLabel();
    avatar_label_ = recognition_page_->avatarLabel();
    user_table_ = user_page_->table();
}

void MainWindow::setup_navigation() {
    if (!side_menu_ || !router_) {
        return;
    }

    // 使用新 SVG 图标系统
    QList<SideMenu::Item> items = {
        {"recognition", tr("实时画面"), ":/icons/navigation/home.svg"},
        {"dashboard", tr("智能看板"), ":/icons/navigation/dashboard.svg"},
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

    // ==================== NPU 资源切换逻辑 ====================
    // RK3588 的 NPU 被 RKNN (人脸检测/识别) 和 RKLLM (语言模型) 共享
    // 两者不能同时高效运行，必须完全互斥使用：
    //   - 进入 Dashboard (智能看板): 释放 RKNN → 让 RKLLM 获得全部 NPU 资源
    //   - 回到 Recognition (实时画面): 释放 RKLLM → 让 RKNN 获得全部 NPU 资源
    // =========================================================
    connect(router_, &UiRouter::routeChanged, this, [this](const QString& key, QWidget*) {
        QString breadcrumb;
        if (key == "recognition") {
            breadcrumb = tr("实时画面");
            
            // === 回到实时识别页面：释放 RKLLM → 加载 RKNN ===
            if (recognition_paused_for_llm_) {
                recognition_paused_for_llm_ = false;
                
                // 步骤1: 异步释放 RKLLM 模型（释放 NPU 给人脸识别）
                auto local_llm = LocalLLMThread::instance();
                if (local_llm->isModelReady()) {
                    local_llm->releaseModelAsync();
                    spdlog::info("LLM model release requested for face recognition");
                }
                
                // 步骤2: 重新加载 RKNN 模型和工作线程
                if (recognition_app_ && !recognition_app_->are_models_loaded()) {
                    if (recognition_app_->reload_models()) {
                        spdlog::info("RKNN models reloaded after LLM usage");
                    } else {
                        spdlog::error("Failed to reload RKNN models!");
                    }
                }
                
                // 步骤3: 启动人脸识别
                start_recognition();
                spdlog::info("Recognition resumed (back to recognition page)");
            }
        } else if (key == "dashboard") {
            breadcrumb = tr("智能看板");
            
            // === 进入智能看板页面：释放 RKNN → 为 RKLLM 腾出 NPU ===
            if (is_running_) {
                recognition_paused_for_llm_ = true;
                stop_recognition();
                spdlog::info("Recognition paused (entering dashboard for LLM)");
            }
            
            // 释放 RKNN 模型（包括停止工作线程），彻底释放 NPU 资源
            if (recognition_app_ && recognition_app_->are_models_loaded()) {
                if (recognition_app_->release_models()) {
                    spdlog::info("RKNN models released for LLM performance boost");
                }
            }
        } else if (key == "attendance") {
            breadcrumb = tr("考勤记录");
        } else if (key == "users") {
            breadcrumb = tr("用户管理");
        } else if (key == "settings") {
            breadcrumb = tr("系统设置");
            if (settings_page_) {
                settings_page_->activate();
            }
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
        connect(recognition_page_, &RecognitionPage::refreshAttendanceRequested,
                this, &MainWindow::load_today_attendance);
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

        // 连接摄像头设置变更信号，重新初始化摄像头
        connect(settings_page_, &SettingsPage::cameraSettingsChanged,
                this, &MainWindow::apply_camera_settings);
    }
}

void MainWindow::apply_theme() {
    ThemeManager::apply(is_dark_theme_ ? ThemeManager::Theme::Dark
                                       : ThemeManager::Theme::Light);
}

void MainWindow::start_recognition() {
    if (!recognition_app_) {
        return;
    }
    if (is_running_) {
        if (recognition_page_) {
            recognition_page_->setRecognitionRunning(true);
        }
        if (status_label_) {
            status_label_->setText(tr("运行中"));
        }
        return;
    }

    is_running_ = true;
    if (status_label_) {
        status_label_->setText("运行中");
    }
    if (recognition_page_) {
        recognition_page_->setRecognitionRunning(true);
    }

    // 设置帧回调（使用 Qt 信号槽机制确保线程安全）
    recognition_app_->set_frame_callback([this](const cv::Mat& frame, const std::vector<RecognitionResult>& results) {
        if (closing_.load(std::memory_order_acquire) || !is_running_) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(latest_frame_mutex_);
            latest_frame_ = frame;          // cv::Mat 轻量复制（引用计数）
            latest_results_ = results;      // 复制结果（避免 Qt 事件队列堆积导致多份拷贝）
            latest_frame_seq_.fetch_add(1, std::memory_order_release);
        }

        bool expected = false;
        if (ui_update_scheduled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            QMetaObject::invokeMethod(this, "drain_latest_frame", Qt::QueuedConnection);
        }
    });

    // 启动后台识别线程
    recognition_thread_ = std::thread([this]() {
        spdlog::info("Recognition thread started");
        recognition_app_->run();
        spdlog::info("Recognition thread stopped");
    });

    spdlog::info("Recognition started in background thread");
}

void MainWindow::drain_latest_frame() {
    if (closing_.load(std::memory_order_acquire) || !is_running_) {
        ui_update_scheduled_.store(false, std::memory_order_release);
        return;
    }

    cv::Mat frame;
    std::vector<RecognitionResult> results;
    uint64_t drained_seq = 0;
    {
        std::lock_guard<std::mutex> lock(latest_frame_mutex_);
        frame = latest_frame_;
        results = std::move(latest_results_);
        drained_seq = latest_frame_seq_.load(std::memory_order_acquire);
    }

    if (!frame.empty()) {
        on_frame_ready(frame, results);
    }

    ui_update_scheduled_.store(false, std::memory_order_release);

    if (closing_.load(std::memory_order_acquire) || !is_running_) {
        return;
    }

    if (latest_frame_seq_.load(std::memory_order_acquire) != drained_seq) {
        bool expected = false;
        if (ui_update_scheduled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            QMetaObject::invokeMethod(this, "drain_latest_frame", Qt::QueuedConnection);
        }
    }
}

void MainWindow::stop_recognition() {
    if (!is_running_) {
        if (recognition_page_) {
            recognition_page_->setRecognitionRunning(false);
        }
        if (status_label_) {
            status_label_->setText(tr("已停止"));
        }
        return;
    }

    is_running_ = false;
    if (status_label_) {
        status_label_->setText("已停止");
    }
    if (recognition_page_) {
        recognition_page_->setRecognitionRunning(false);
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
    face_results.reserve(results.size());
    const int dup_interval = ConfigManager::instance()->getDuplicateCheckInterval();
    const std::time_t current_time = std::time(nullptr);
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
            fr.is_duplicate = attendance_service_->is_duplicate_check(result.user_id, dup_interval);
            // 自动判断打卡类型
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

    // 每秒更新一次 FPS
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fps_time_);
    if (duration.count() >= 1000) {
        if (recognition_app_) {
            // 1. NPU 帧率：从 PerformanceMonitor 获取（YOLO 检测能力，约 50+ FPS）
            npu_fps_ = recognition_app_->get_npu_fps();
            
            // 2. 摄像头采集帧率：从摄像头采集线程获取真实帧率（约 30 FPS）
            camera_fps_ = recognition_app_->get_camera_fps();
        }
        
        // 3. 显示帧率：由 VideoDisplayWidget 在 paintEvent 中自行计算
        
        last_fps_time_ = now;
        
        if (video_widget_) {
            video_widget_->set_npu_fps(npu_fps_);
            video_widget_->set_camera_fps(camera_fps_);
        }
    }
}

void MainWindow::update_status() {
    if (fps_label_) {
    fps_label_->setText(QString("FPS: %1").arg(camera_fps_, 0, 'f', 1));
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
            if (holiday_service_) {
                holiday_service_->requestTodayAndNext();
            }
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

void MainWindow::on_recognition_result(int user_id, const QString& name, float similarity, bool is_new_attendance, int check_type, int status) {
    spdlog::trace("on_recognition_result called: user_id={}, name={}, is_new={}, check_type={}, status={}", 
                  user_id, name.toStdString(), is_new_attendance, check_type, status);
    
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
                if (avatar_label_) {
                    QString photo_path = resolve_photo_path(user_info.photo_path);
                    if (!photo_path.isEmpty() && QFileInfo::exists(photo_path)) {
                        QPixmap avatar(photo_path);
                        QPixmap rounded = make_circular_pixmap(avatar, avatar_label_->width());
                        avatar_label_->setPixmap(rounded);
                        avatar_label_->setText("");
                    } else {
                        avatar_label_->setPixmap(QPixmap());
                        avatar_label_->setText("◉");
                    }
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
        if (avatar_label_) {
            avatar_label_->setPixmap(QPixmap());
            avatar_label_->setText("◉");
        }
    }

    // 更新考勤表格和状态标签
    if (is_new_attendance) {
        spdlog::info("Processing new attendance: user_id={}, name={}, check_type={}, status={}", 
                     user_id, name.toStdString(), check_type, status);
        
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
        
        // 更新今日签到列表（最新的在上面）
        // 只有注册用户才添加到考勤列表
        if (attendance_list_ && user_id > 0) { 
            AttendanceItem item;
            item.user_id = user_id;
            item.name = name;
            item.time = QDateTime::currentDateTime();
            item.check_type = check_type;
            item.status = status; // 传入状态
            item.similarity = similarity;
            // item.is_stranger 不再需要，因为陌生人不会进入列表
            
            // 获取用户详情（部门）
            if (user_service_) {
                db::UserInfo user_info;
                if (user_service_->get_user(user_id, user_info)) {
                    item.department = QString::fromStdString(user_info.department);
                    item.avatar_path = resolve_photo_path(user_info.photo_path);
                }
            }
            
            attendance_list_->addRecord(item);
            
            spdlog::info("Added to attendance feed: {} (type: {}, status: {}, ID: {}, similarity: {:.2f})",
                        name.toStdString(), check_type, status, user_id, similarity);
        } else if (user_id <= 0) {
            spdlog::trace("Stranger detected, not added to attendance feed.");
        } else {
            spdlog::error("attendance_list_ is nullptr!");
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

void MainWindow::apply_camera_settings(int deviceId) {
    if (!recognition_app_) {
        QMessageBox::warning(this, tr("错误"), tr("识别系统未初始化"));
        return;
    }

    spdlog::info("Applying camera settings: device ID = {}", deviceId);

    // 如果识别正在运行，先停止
    bool was_running = is_running_;
    if (was_running) {
        spdlog::info("Stopping recognition to reinitialize camera");
        stop_recognition();
        // stop_recognition 内部会停止定时器，reinitialize_camera 会 join 线程
        // 无需额外 sleep
    }

    // 重新初始化摄像头（内部会处理线程同步）
    std::string device_number = std::to_string(deviceId);
    bool success = recognition_app_->reinitialize_camera(device_number);

    if (success) {
        spdlog::info("Camera reinitialized successfully: /dev/video{}", deviceId);
        QMessageBox::information(this, tr("成功"),
            tr("摄像头已切换到 /dev/video%1").arg(deviceId));

        // 如果之前在运行，重新启动识别
        if (was_running) {
            spdlog::info("Restarting recognition with new camera");
            start_recognition();
        }
    } else {
        std::string error = recognition_app_->get_camera_error();
        spdlog::error("Failed to reinitialize camera: {}", error);
        QMessageBox::critical(this, tr("错误"),
            tr("摄像头初始化失败：\n%1\n\n请检查设备连接或选择其他摄像头。")
            .arg(QString::fromStdString(error)));
    }
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
        QThread::msleep(50);

        start_recognition();
    }

    // 如果注册成功，刷新用户列表
    if (result == QDialog::Accepted) {
        load_users();
    }
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

void MainWindow::on_action_toggle_maximize() {
    if (isMaximized() || isFullScreen()) {
        showNormal();
        spdlog::info("Window restored to normal");
    } else {
        showMaximized();
        spdlog::info("Window maximized");
    }
    // 更新 TitleBar 的最大化图标
    if (title_bar_) {
        title_bar_->updateMaximizeIcon();
    }
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);

    // 窗口状态变化时更新 TitleBar 的最大化图标
    if (event->type() == QEvent::WindowStateChange) {
        if (title_bar_) {
            title_bar_->updateMaximizeIcon();
        }
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
        closing_.store(true, std::memory_order_release);
        stop_recognition();
        event->accept();
    } else {
        event->ignore();
    }
}
