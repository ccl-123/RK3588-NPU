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
#include "gui_utils/stack_router.h"
#include "gui_utils/audio_manager.h"
#include "gui_utils/config_manager.h"
#include "widgets/modern_table_view.h"
#include "widgets/side_menu.h"
#include "widgets/title_bar.h"
#include "widgets/toast_notification.h"
#include "widgets/attendance_list_widget.h"
#include "app/local_llm_thread.h"
#include "gui_services/local_ai_analysis_service.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QCloseEvent>
#include <QTimer>
#include <QThread>
#include <QStackedWidget>
#include <QScreen>
#include <QResizeEvent>
#include <QWindow>
#include <QDateTime>
#include <QDate>
#include <QTableWidgetItem>
#include <QMenu>
#include <QStringList>
#include <QtConcurrent>
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

QRect available_geometry_for(const QWidget* widget) {
    QScreen* screen = nullptr;
    if (widget && widget->windowHandle()) {
        screen = widget->windowHandle()->screen();
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    return screen ? screen->availableGeometry() : QRect(0, 0, 1440, 900);
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
    , status_timer_(nullptr)
    , camera_pause_timer_(nullptr)
    , registration_dialog_(nullptr)
    , is_running_(false)
    , recognition_paused_for_llm_(false)
    , npu_fps_(0.0)
    , camera_fps_(0.0)
    , rknn_release_watcher_(nullptr)
    , rknn_reload_watcher_(nullptr)
    , current_date_(QDate::currentDate())  // 初始化当前日期（用于跨日检测）
    , last_displayed_user_id_(-1)
    , user_confirm_duration_ms_(1000)  // 默认1秒，从配置加载
    , stranger_detection_{false, std::chrono::steady_clock::now(), std::chrono::steady_clock::now()}
    , camera_id_(0)
{
    // 注册 Qt 元类型（必须在使用前注册）
    qRegisterMetaType<cv::Mat>("cv::Mat");
    qRegisterMetaType<std::vector<RecognitionResult>>("std::vector<RecognitionResult>");

    // 初始化视觉模型异步切换 Watcher；NPU 资源所有权由 NpuResourceManager 统一仲裁。
    rknn_release_watcher_ = new QFutureWatcher<bool>(this);
    rknn_reload_watcher_ = new QFutureWatcher<bool>(this);
    connect(rknn_release_watcher_, &QFutureWatcher<bool>::finished,
            this, [this]() { on_rknn_models_released(rknn_release_watcher_->result()); });
    connect(rknn_reload_watcher_, &QFutureWatcher<bool>::finished,
            this, [this]() { on_rknn_models_reloaded(rknn_reload_watcher_->result()); });

    auto* local_llm = LocalLLMThread::instance();
    connect(local_llm, &LocalLLMThread::modelReleased, this, [this]() {
        waiting_for_llm_release_.store(false, std::memory_order_release);
        if (closing_.load(std::memory_order_acquire)) {
            return;
        }

        if (current_route_key_ == "recognition" &&
            recognition_app_ &&
            !recognition_app_->are_models_loaded() &&
            !rknn_switching_.load(std::memory_order_acquire)) {
            spdlog::info("Local LLM released, reloading RKNN models for recognition");
            reload_rknn_models_async();
        }
    });

    setup_ui();

    // 初始化定时器（只保留状态更新定时器）
    status_timer_ = new QTimer(this);
    connect(status_timer_, &QTimer::timeout, this, &MainWindow::update_status);
    status_timer_->start(1000);  // 每秒更新一次状态

    // 快速切页时延迟暂停摄像头，避免 V4L2 设备被频繁 close/open
    camera_pause_timer_ = new QTimer(this);
    camera_pause_timer_->setSingleShot(true);
    connect(camera_pause_timer_, &QTimer::timeout, this, [this]() {
        if (closing_.load(std::memory_order_acquire) ||
            current_route_key_ != "dashboard" ||
            !dashboard_page_ ||
            !dashboard_page_->isLocalBackendEnabled() ||
            is_running_.load(std::memory_order_acquire) ||
            !recognition_app_) {
            return;
        }

        if (!recognition_app_->pause_camera()) {
            spdlog::warn("Failed to pause camera for LLM");
        }
    });

    last_fps_time_ = std::chrono::steady_clock::now();

    spdlog::info("MainWindow initialized");
}


MainWindow::~MainWindow() {
    closing_.store(true, std::memory_order_release);
    stop_recognition();

    // 等待异步视觉模型切换完成，避免悬空指针
    if (rknn_release_watcher_ && rknn_release_watcher_->isRunning()) {
        spdlog::info("Waiting for async RKNN release to complete...");
        rknn_release_watcher_->waitForFinished();
    }
    if (rknn_reload_watcher_ && rknn_reload_watcher_->isRunning()) {
        spdlog::info("Waiting for async RKNN reload to complete...");
        rknn_reload_watcher_->waitForFinished();
    }

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
                        if (AudioManager::instance()->playSoundWithCooldown(
                                AudioType::StrangerDetected, STRANGER_AUDIO_COOLDOWN_MS)) {
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
            AudioManager::instance()->resetCooldown(AudioType::StrangerDetected);
            spdlog::trace("Switched from stranger to user, reset stranger detection");
        }
            
        auto& user_detection = user_detections_[result.user_id];
        bool is_same_person = user_detection.is_detecting && 
                             (result.user_id == user_detection.user_id) && 
                             (result.user_id > 0);
        
        if (!is_same_person) {
            // 不是同一个人，开始新的检测
            user_detection.is_detecting = true;
            user_detection.user_id = result.user_id;
            user_detection.user_name = result.user_name;
            user_detection.max_similarity = result.similarity;
            user_detection.first_seen = now;
            user_detection.last_seen = now;
            user_detection.attendance_recorded = false;
            
            spdlog::trace("User detection started: {} (similarity: {:.2f})", 
                          result.user_name, result.similarity);
            return;
        }
        
        // 是同一个人，检查是否超时
        auto since_last = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - user_detection.last_seen).count();
        
        if (since_last > USER_DETECTION_TIMEOUT_MS) {
            // 超时了，重新开始检测
            user_detection.first_seen = now;
            user_detection.last_seen = now;
            user_detection.max_similarity = result.similarity;
            user_detection.attendance_recorded = false;
            spdlog::trace("User detection restarted (timeout after {}ms)", since_last);
            // 继续执行下面的 UI 更新，不要 return
        } else {
            // 没超时，更新检测状态
            user_detection.last_seen = now;
            user_detection.max_similarity = std::max(user_detection.max_similarity, result.similarity);
        }
        
        // 计算检测持续时间
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - user_detection.first_seen).count();
        
        const int confirm_duration_ms = user_confirm_duration_ms_.load(std::memory_order_acquire);
        spdlog::trace("User detection continued: {} (duration: {}ms / {}ms)", 
                      result.user_name, duration, confirm_duration_ms);
        
        // 只有达到确认时长且未记录过考勤才真正签到
        if (duration >= confirm_duration_ms && !user_detection.attendance_recorded) {
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
                    user_detection.max_similarity,
                    "",
                    check_type);
                is_new_attendance = (record_id > 0);
            }
            
            // 标记本次检测已记录考勤
            user_detection.attendance_recorded = true;
            
            const char* type_str = (check_type == 2) ? "签退" : "签到";
            spdlog::info("User confirmed after {}ms: {} (similarity: {:.2f}, type: {}, is_new: {})",
                        duration, result.user_name, user_detection.max_similarity, type_str, is_new_attendance);

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
                                          Q_ARG(float, user_detection.max_similarity),
                                          Q_ARG(bool, true),
                                          Q_ARG(int, check_type),
                                          Q_ARG(int, attendance_status));
            } else {
                // 重复打卡 - 使用独立的冷却机制避免频繁播放
                // 根据打卡类型选择对应的音频类型（签到和签退分别冷却）
                AudioType audio_type = (check_type == 2) ? 
                    AudioType::AlreadyCheckedOut : AudioType::AlreadyCheckedIn;
                
                // 检查该类型音频的独立冷却时间
                if (AudioManager::instance()->playSoundWithCooldown(
                        audio_type, DUPLICATE_CHECK_COOLDOWN_MS)) {
                    spdlog::debug("Played duplicate {} audio", type_str);
                }
            }
        }
    });
    
    // 从配置加载用户识别确认时间
    apply_user_confirm_duration(ConfigManager::instance()->getUserConfirmDuration());
    
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

    if (recognition_page_) {
        recognition_page_->setSystemStatus(tr("初始化中..."));
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
                    if (recognition_page_) {
                        recognition_page_->setSystemStatus(tr("初始化失败"));
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

                    if (recognition_page_) {
                        recognition_page_->setSystemStatus(tr("就绪 (摄像头未连接)"));
                    }

                    // 显示友好提示，但不阻止应用启动
                    QMessageBox::warning(this, tr("摄像头未连接"),
                        tr("摄像头初始化失败：\n%1\n\n"
                           "应用已启动，但摄像头功能不可用。\n"
                           "您可以在【设置】页面重新选择摄像头设备。").arg(camera_error));

                    // 不启动识别，因为没有摄像头
                    return;
                }

                if (recognition_page_) {
                    recognition_page_->setSystemStatus(tr("就绪"));
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
    const int clamped = std::max(100, std::min(2000, duration_ms));
    user_confirm_duration_ms_.store(clamped, std::memory_order_release);
    const int current = user_confirm_duration_ms_.load(std::memory_order_acquire);
    spdlog::info("Applied user confirm duration: {}ms ({:.1f}s)", 
                 current, current / 1000.0);
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
    apply_initial_window_geometry();

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
    update_chrome_compact_mode();
    // 主题由 ThemeManager 在 main_gui.cc 中初始化，不再在此调用 apply_theme()

    connect(title_bar_, &TitleBar::requestMinimize, this, &MainWindow::showMinimized);
    connect(title_bar_, &TitleBar::requestMaximize, this, &MainWindow::on_action_toggle_maximize);
    connect(title_bar_, &TitleBar::requestClose, this, &MainWindow::close);
    connect(title_bar_, &TitleBar::requestToggleTheme, this, &MainWindow::on_action_toggle_theme);
}

void MainWindow::apply_initial_window_geometry() {
    const QRect available = available_geometry_for(this);
    if (!available.isValid() || available.isEmpty()) {
        resize(1440, 900);
        return;
    }

    const bool small_screen = available.width() <= 1024 || available.height() <= 600;
    const QSize preferred(1440, 900);
    const int margin = small_screen ? 0 : 48;
    const QSize max_size(qMax(480, available.width() - margin),
                         qMax(360, available.height() - margin));
    QSize target = small_screen ? available.size() : preferred.boundedTo(max_size);

    const QSize min_size(qMin(640, available.width()), qMin(420, available.height()));
    target = target.expandedTo(min_size).boundedTo(available.size());
    setMinimumSize(min_size);
    resize(target);
    move(available.center() - rect().center());
}

void MainWindow::update_chrome_compact_mode() {
    const QRect available = available_geometry_for(this);
    const bool compact = width() <= 1100 ||
                         available.width() <= 1024 ||
                         available.height() <= 600;
    if (compact_chrome_ == compact) {
        return;
    }

    compact_chrome_ = compact;
    const int side_width = compact_chrome_ ? 72 : 220;
    if (side_menu_) {
        side_menu_->setCompactMode(compact_chrome_);
    }
    if (title_bar_) {
        title_bar_->setSideBarWidth(side_width);
    }
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

    // ==================== 页面驱动的模型生命周期编排 ====================
    // NPU 资源所有权由 NpuResourceManager 集中仲裁；MainWindow 只负责
    // 页面切换时异步触发视觉模型加载/卸载和本地 LLM 初始化/释放。
    // =========================================================
    connect(router_, &UiRouter::routeChanged, this, [this](const QString& key, QWidget*) {
        on_route_changed(key);
    });

    router_->navigateTo("recognition");
    side_menu_->setActiveKey("recognition");
}

void MainWindow::on_route_changed(const QString& key) {
    current_route_key_ = key;
    if (key == "recognition") {
        handle_recognition_route();
    } else if (key == "dashboard") {
        handle_dashboard_route();
    } else if (key == "settings") {
        handle_settings_route();
    }

    update_route_breadcrumb(key);
}

void MainWindow::handle_recognition_route() {
    if (camera_pause_timer_ && camera_pause_timer_->isActive()) {
        camera_pause_timer_->stop();
        spdlog::info("Cancelled delayed camera pause while returning to recognition");
    }

    if (is_running_.load(std::memory_order_acquire) && !recognition_paused_for_llm_) {
        return;
    }

    // 进入识别页面时确保摄像头已恢复（不自动启动识别）
    bool camera_ready = true;
    if (recognition_app_ && !is_running_.load(std::memory_order_acquire)) {
        camera_ready = recognition_app_->resume_camera();
        if (!camera_ready) {
            spdlog::error("Failed to resume camera for recognition");
            handle_camera_runtime_failure(
                QString::fromStdString(recognition_app_->get_camera_error()),
                false);
            return;
        }
    }

    auto local_llm = LocalLLMThread::instance();
    if ((local_llm->isModelReady() || local_llm->isInitInProgress()) &&
        !waiting_for_llm_release_.exchange(true, std::memory_order_acq_rel)) {
        local_llm->releaseModelAsync();
        spdlog::info("LLM model release requested for face recognition");
        return;
    }

    if (rknn_switching_.load()) {
        spdlog::info("RKNN switching in progress, wait for completion");
        return;
    }

    if (camera_ready && recognition_app_ && !recognition_app_->are_models_loaded()) {
        reload_rknn_models_async();
    } else if (camera_ready && recognition_app_ && recognition_app_->are_models_loaded() && recognition_paused_for_llm_) {
        recognition_paused_for_llm_ = false;
        start_recognition();
        spdlog::info("Recognition resumed (models already loaded)");
    }
}

void MainWindow::handle_dashboard_route() {
    const bool use_local_llm = dashboard_page_ && dashboard_page_->isLocalBackendEnabled();
    if (!use_local_llm) {
        if (camera_pause_timer_ && camera_pause_timer_->isActive()) {
            camera_pause_timer_->stop();
        }
        spdlog::info("Dashboard cloud backend active, keeping recognition pipeline running");
        return;
    }

    // 只有本地 LLM 需要独占 NPU，进入本地模式时才暂停识别和摄像头。
    if (is_running_.load(std::memory_order_acquire)) {
        recognition_paused_for_llm_ = true;
        stop_recognition();
        spdlog::info("Recognition paused for local LLM");
    }

    if (recognition_app_ && camera_pause_timer_ && !camera_pause_timer_->isActive()) {
        camera_pause_timer_->start(CAMERA_PAUSE_DELAY_MS);
        spdlog::info("Scheduled delayed camera pause for dashboard");
    }

    if (dashboard_page_ && dashboard_page_->isLocalBackendEnabled()) {
        maybe_start_local_llm();
    }
}

void MainWindow::handle_settings_route() {
    if (settings_page_) {
        settings_page_->activate();
    }
}

void MainWindow::update_route_breadcrumb(const QString& key) {
    if (!title_bar_) {
        return;
    }

    QString breadcrumb;
    if (key == "recognition") {
        breadcrumb = tr("实时画面");
    } else if (key == "dashboard") {
        breadcrumb = tr("智能看板");
    } else if (key == "attendance") {
        breadcrumb = tr("考勤记录");
    } else if (key == "users") {
        breadcrumb = tr("用户管理");
    } else if (key == "settings") {
        breadcrumb = tr("系统设置");
    }

    title_bar_->setBreadcrumb({breadcrumb});
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

    if (dashboard_page_) {
        connect(dashboard_page_, &DashboardPage::backendPreferenceChanged,
                this, &MainWindow::on_dashboard_backend_preference_changed);
    }
}

void MainWindow::start_recognition() {
    if (!recognition_app_) {
        return;
    }
    if (!recognition_app_->are_models_loaded()) {
        spdlog::warn("Cannot start recognition: models not loaded");
        if (recognition_page_) {
            recognition_page_->setSystemStatus(tr("模型未加载"));
            recognition_page_->setRecognitionRunning(false);
            recognition_page_->updateDetectionStatus(tr("模型未加载"), -1);
        }
        return;
    }
    if (!recognition_app_->is_camera_initialized()) {
        handle_camera_runtime_failure(
            QString::fromStdString(recognition_app_->get_camera_error()),
            false);
        return;
    }
    if (is_running_.load(std::memory_order_acquire)) {
        if (recognition_page_) {
            recognition_page_->setRecognitionRunning(true);
            recognition_page_->setSystemStatus(tr("运行中"));
        }
        return;
    }

    user_detections_.clear();
    stranger_detection_ = {false, std::chrono::steady_clock::now(), std::chrono::steady_clock::now()};
    is_running_.store(true, std::memory_order_release);
    if (recognition_page_) {
        recognition_page_->setSystemStatus(tr("运行中"));
        recognition_page_->setRecognitionRunning(true);
        recognition_page_->updateDetectionStatus(tr("等待识别"), 0);
    }

    // 设置帧回调（使用 Qt 信号槽机制确保线程安全）
    recognition_app_->set_frame_callback([this](const cv::Mat& frame, const std::vector<RecognitionResult>& results) {
        if (closing_.load(std::memory_order_acquire) || !is_running_.load(std::memory_order_acquire)) {
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
        int ret = recognition_app_->run();
        spdlog::info("Recognition thread stopped with code {}", ret);

        if (ret != 0 &&
            !closing_.load(std::memory_order_acquire) &&
            recognition_app_ &&
            !recognition_app_->get_camera_error().empty()) {
            const QString error_message = QString::fromStdString(recognition_app_->get_camera_error());
            QMetaObject::invokeMethod(
                this,
                [this, error_message]() {
                    handle_camera_runtime_failure(error_message, true);
                },
                Qt::QueuedConnection);
        }
    });

    spdlog::info("Recognition started in background thread");
}

void MainWindow::drain_latest_frame() {
    if (closing_.load(std::memory_order_acquire) || !is_running_.load(std::memory_order_acquire)) {
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

    if (closing_.load(std::memory_order_acquire) || !is_running_.load(std::memory_order_acquire)) {
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
    if (!is_running_.load(std::memory_order_acquire)) {
        if (recognition_page_) {
            recognition_page_->setSystemStatus(tr("已停止"));
            recognition_page_->setRecognitionRunning(false);
            recognition_page_->updateDetectionStatus(tr("已停止"), -1);
        }
        return;
    }

    is_running_.store(false, std::memory_order_release);
    if (recognition_page_) {
        recognition_page_->setSystemStatus(tr("已停止"));
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
    
    // 重置状态栏
    if (recognition_page_) {
        recognition_page_->updateFaceCount(0);
        recognition_page_->updateDetectionStatus(tr("已停止"), -1);
    }
    last_displayed_user_id_ = -1;

    spdlog::info("Recognition stopped");
}

void MainWindow::handle_camera_runtime_failure(const QString& error_message, bool should_offer_restart) {
    if (closing_.load(std::memory_order_acquire)) {
        return;
    }

    const bool was_running = is_running_.load(std::memory_order_acquire);
    if (should_offer_restart && was_running) {
        restart_recognition_after_camera_recovery_ = true;
    }

    if (was_running) {
        stop_recognition();
    }

    camera_fps_ = 0.0;
    if (video_widget_) {
        video_widget_->set_camera_fps(0.0);
    }

    if (recognition_page_) {
        recognition_page_->setSystemStatus(tr("摄像头已断开"));
        recognition_page_->setRecognitionRunning(false);
        recognition_page_->updateDetectionStatus(tr("摄像头已断开"), -1);
    }

    if (side_menu_) {
        side_menu_->setActiveKey("settings");
    } else if (router_) {
        router_->navigateTo("settings");
    }

    QMessageBox::warning(
        this,
        tr("摄像头已断开"),
        tr("运行中的摄像头连接已中断：\n%1\n\n已切换到【设置】页面，请重新选择摄像头并点击【应用】完成初始化。")
            .arg(error_message.isEmpty() ? tr("设备不可用") : error_message));
}

void MainWindow::on_frame_ready(const cv::Mat& frame, const std::vector<RecognitionResult>& results) {
    if (!is_running_.load(std::memory_order_acquire)) {
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
    const RecognitionResult* single_recognized_result = nullptr;
    int single_recognized_check_type = 1;
    int recognized_count = 0;
    QStringList recognized_names;
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

        if (result.user_id > 0) {
            recognized_count++;
            recognized_names << QString::fromStdString(result.user_name);
            single_recognized_result = &result;
            single_recognized_check_type = fr.check_type;
        }

        face_results.push_back(fr);
    }
    if (video_widget_) {
    video_widget_->set_face_results(face_results);
    }
    
    // 更新状态栏的人脸检测数量
    if (recognition_page_) {
        recognition_page_->updateFaceCount(static_cast<int>(results.size()));

        if (recognized_count == 0) {
            if (results.empty()) {
                recognition_page_->setRecognitionSummary(tr("等待识别"));
                recognition_page_->updateDetectionStatus(tr("等待识别"), 0);
                last_displayed_user_id_ = -1;
                recognition_page_->setUserName(tr("未识别"));
                recognition_page_->setUserSimilarity(-1.0f);
                recognition_page_->resetUserMeta();
            } else {
                recognition_page_->setRecognitionSummary(
                    tr("检测到 %1 张人脸，未识别").arg(results.size()));
                recognition_page_->updateDetectionStatus(tr("未识别用户"), -1);
                last_displayed_user_id_ = -1;
                recognition_page_->setUserName(tr("未识别"));
                recognition_page_->setUserSimilarity(-1.0f);
                recognition_page_->resetUserMeta();
            }
        } else if (recognized_count == 1 && single_recognized_result) {
            on_recognition_result(single_recognized_result->user_id,
                                  QString::fromStdString(single_recognized_result->user_name),
                                  single_recognized_result->similarity,
                                  false,
                                  single_recognized_check_type,
                                  1);
        } else {
            recognition_page_->setRecognitionSummary(
                tr("识别到 %1 位用户: %2").arg(recognized_count).arg(recognized_names.join("、")));
            recognition_page_->updateDetectionStatus(tr("多人识别中: %1 位").arg(recognized_count), -1);

            if (last_displayed_user_id_ != -2) {
                last_displayed_user_id_ = -2;
                recognition_page_->setUserName(tr("多人识别"));
                recognition_page_->setUserSimilarity(-1.0f);
                recognition_page_->resetUserMeta();
            }
        }
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
    if (recognition_page_) {
        recognition_page_->setFpsText(QString("FPS: %1").arg(camera_fps_, 0, 'f', 1));
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
                std::tm tm_now;
                localtime_r(&now, &tm_now);
                char date_buf[16];
                std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_now);
                
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
    if (!is_running_.load(std::memory_order_acquire)) {
        return;
    }
    spdlog::trace("on_recognition_result called: user_id={}, name={}, is_new={}, check_type={}, status={}", 
                  user_id, name.toStdString(), is_new_attendance, check_type, status);
    
    if (recognition_page_) {
        recognition_page_->setRecognitionSummary(
            QString("识别: %1 (%2)").arg(name).arg(similarity, 0, 'f', 2));
    }

    // 更新状态栏的识别状态
    if (recognition_page_) {
        if (is_new_attendance) {
            // 识别成功并完成签到/签退
            QString status_text = (check_type == 2) ? tr("✓ 签退成功") : tr("✓ 签到成功");
            recognition_page_->updateDetectionStatus(status_text, -1);  // -1 隐藏进度条
        } else if (user_id > 0) {
            // 正在识别用户中
            recognition_page_->updateDetectionStatus(tr("识别中: %1").arg(name), -1);
        }
    }

    const bool keep_multi_user_card = is_new_attendance && last_displayed_user_id_ == -2;
    if (!keep_multi_user_card && recognition_page_) {
        recognition_page_->setUserName(name);
        recognition_page_->setUserSimilarity(similarity);
    }
    
    // 只在用户ID变化时获取用户详细信息（减少数据库查询）
    if (!keep_multi_user_card && user_id > 0 && user_id != last_displayed_user_id_) {
        last_displayed_user_id_ = user_id;
        
        if (user_service_) {
            db::UserInfo user_info;
            if (user_service_->get_user(user_id, user_info)) {
                if (recognition_page_) {
                    recognition_page_->setUserMeta(
                        QString::fromStdString(user_info.employee_id),
                        QString::fromStdString(user_info.department));

                    QString photo_path = resolve_photo_path(user_info.photo_path);
                    if (!photo_path.isEmpty() && QFileInfo::exists(photo_path)) {
                        QPixmap avatar(photo_path);
                        int avatar_size = recognition_page_->avatarDisplaySize();
                        recognition_page_->setUserAvatar(make_circular_pixmap(avatar, avatar_size));
                    } else {
                        recognition_page_->setUserAvatar(QPixmap());
                    }
                }
            }
        }
    } else if (!keep_multi_user_card && user_id <= 0) {
        last_displayed_user_id_ = -1;
        if (recognition_page_) {
            recognition_page_->resetUserMeta();
        }
    }

    // 更新考勤表格和状态标签
    if (is_new_attendance) {
        spdlog::info("Processing new attendance: user_id={}, name={}, check_type={}, status={}", 
                     user_id, name.toStdString(), check_type, status);
        
        // 更新状态标签（签到绿色/签退蓝色，5秒后隐藏）
        if (recognition_page_) {
            QString msg = (check_type == 2) ? tr("✓ 签退成功") : tr("✓ 签到成功");
            recognition_page_->showAttendanceStatus(msg, check_type == 2);
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
    bool was_running = is_running_.load(std::memory_order_acquire);
    if (was_running) {
        spdlog::info("Stopping recognition to reinitialize camera");
        stop_recognition();
        // stop_recognition 内部会停止定时器，reinitialize_camera 会 join 线程
        // 无需额外 sleep
    }

    // 重新初始化摄像头（内部会处理线程同步）
    std::string device_number = std::to_string(deviceId);
    bool success = recognition_app_->reinitialize_camera(device_number);
    const bool should_restart = was_running || restart_recognition_after_camera_recovery_;

    if (success) {
        spdlog::info("Camera reinitialized successfully: /dev/video{}", deviceId);
        restart_recognition_after_camera_recovery_ = false;
        QMessageBox::information(this, tr("成功"),
            tr("摄像头已切换到 /dev/video%1").arg(deviceId));

        // 如果之前在运行，重新启动识别
        if (should_restart) {
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
    bool was_running = is_running_.load(std::memory_order_acquire);

    // 确保检测流水线在运行（注册复用检测结果）
    if (!was_running) {
        if (recognition_app_) {
            recognition_app_->set_recognition_mode(RecognitionMode::Registration);
        }
        start_recognition();
    } else if (recognition_app_) {
        recognition_app_->set_recognition_mode(RecognitionMode::Registration);
    }

    // 清空音频队列，避免注册时还在播放陌生人提示音
    AudioManager::instance()->clearQueue();
    spdlog::debug("Cleared audio queue before face registration");

    // 创建并显示注册对话框
    if (!registration_dialog_) {
        registration_dialog_ = new FaceRegistrationDialog(
            recognition_app_.get(), user_service_.get(), this);
    }

    int result = registration_dialog_->exec();

    if (recognition_app_) {
        recognition_app_->set_recognition_mode(RecognitionMode::Recognition);
    }

    // 清空音频队列，避免注册音频影响后续识别播报
    AudioManager::instance()->clearQueue();
    spdlog::debug("Cleared audio queue after face registration");

    if (!was_running) {
        stop_recognition();
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
    ThemeManager::instance()->toggleTheme();
    spdlog::info("Theme toggled: {}", ThemeManager::instance()->isDarkMode() ? "dark" : "light");
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
    QTimer::singleShot(0, this, [this]() {
        update_chrome_compact_mode();
        if (title_bar_) {
            title_bar_->updateMaximizeIcon();
        }
    });
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);

    // 窗口状态变化时更新 TitleBar 的最大化图标
    if (event->type() == QEvent::WindowStateChange) {
        update_chrome_compact_mode();
        if (title_bar_) {
            title_bar_->updateMaximizeIcon();
        }
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    update_chrome_compact_mode();
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
        restart_recognition_after_camera_recovery_ = false;
        stop_recognition();
        event->accept();
    } else {
        event->ignore();
    }
}

// ==================== 视觉模型异步切换实现 ====================

void MainWindow::release_rknn_models_async() {
    if (rknn_switching_.load()) {
        spdlog::warn("RKNN model switching already in progress, skipping release request");
        return;
    }

    if (!recognition_app_ || !recognition_app_->are_models_loaded()) {
        spdlog::info("RKNN models already released, skipping");
        on_rknn_models_released(true);  // 直接触发完成回调
        return;
    }

    if (is_running_.load(std::memory_order_acquire)) {
        spdlog::info("Stopping recognition before RKNN model release");
        stop_recognition();
    }

    rknn_switching_.store(true);
    spdlog::info("Starting async RKNN model release...");

    // 使用 QtConcurrent 在后台线程执行模型释放
    QFuture<bool> future = QtConcurrent::run([this]() -> bool {
        if (recognition_app_) {
            return recognition_app_->release_models();
        }
        return false;
    });
    rknn_release_watcher_->setFuture(future);
}

void MainWindow::reload_rknn_models_async() {
    if (rknn_switching_.load()) {
        spdlog::warn("RKNN model switching already in progress, skipping reload request");
        return;
    }

    if (!recognition_app_) {
        spdlog::error("Cannot reload RKNN models: recognition_app_ is null");
        return;
    }

    if (recognition_app_->are_models_loaded()) {
        spdlog::info("RKNN models already loaded, skipping");
        on_rknn_models_reloaded(true);  // 直接触发完成回调
        return;
    }

    if (is_running_.load(std::memory_order_acquire)) {
        spdlog::info("Stopping recognition before RKNN model reload");
        stop_recognition();
    }

    rknn_switching_.store(true);
    spdlog::info("Starting async RKNN model reload...");

    // 使用 QtConcurrent 在后台线程执行模型加载
    QFuture<bool> future = QtConcurrent::run([this]() -> bool {
        if (recognition_app_) {
            return recognition_app_->reload_models();
        }
        return false;
    });
    rknn_reload_watcher_->setFuture(future);
}

void MainWindow::on_rknn_models_released(bool success) {
    rknn_switching_.store(false);
    if (!success) {
        spdlog::error("Failed to release RKNN models (async callback)");
        return;
    }

    spdlog::info("RKNN models released (async callback)");

    if (current_route_key_ == "dashboard" && dashboard_page_ && dashboard_page_->isLocalBackendEnabled()) {
        maybe_start_local_llm();
        return;
    }

    if (current_route_key_ == "recognition" &&
        recognition_app_ &&
        !recognition_app_->are_models_loaded()) {
        spdlog::info("RKNN released while recognition route active, reloading models");
        reload_rknn_models_async();
    }
}

void MainWindow::on_rknn_models_reloaded(bool success) {
    rknn_switching_.store(false);

    if (success) {
        spdlog::info("RKNN models reloaded successfully (async callback)");

        // 模型加载成功后，如果是从 LLM 模式返回，启动识别
        if (current_route_key_ == "recognition" && recognition_paused_for_llm_) {
            recognition_paused_for_llm_ = false;

            // 摄像头已在路由切换时恢复，直接启动识别
            start_recognition();
            spdlog::info("Recognition resumed after async RKNN reload");
        }
    } else {
        spdlog::error("Failed to reload RKNN models (async callback)");
        recognition_paused_for_llm_ = false;  // 即使失败也要重置标志
        ToastNotification::showMessage(this, tr("错误"), tr("人脸模型加载失败"),
                                        ToastNotification::Level::Error);
    }
}

void MainWindow::on_dashboard_backend_preference_changed(bool use_local, const QString& model_path) {
    pending_local_llm_model_path_ = model_path;

    if (!use_local) {
        if (camera_pause_timer_ && camera_pause_timer_->isActive()) {
            camera_pause_timer_->stop();
        }

        auto* local_llm = LocalLLMThread::instance();
        if ((local_llm->isModelReady() || local_llm->isInitInProgress()) &&
            !waiting_for_llm_release_.exchange(true, std::memory_order_acq_rel)) {
            spdlog::info("Releasing local LLM after backend switched to cloud");
            local_llm->releaseModelAsync();
        }
        return;
    }

    if (current_route_key_ != "dashboard") {
        return;
    }

    handle_dashboard_route();
}

void MainWindow::maybe_start_local_llm() {
    if (current_route_key_ != "dashboard" || pending_local_llm_model_path_.isEmpty()) {
        return;
    }

    auto* local_llm = LocalLLMThread::instance();
    if (local_llm->isModelReady() || local_llm->isInitInProgress()) {
        spdlog::info("Local LLM already ready or initializing");
        return;
    }

    if (!recognition_app_) {
        return;
    }

    if (rknn_switching_.load(std::memory_order_acquire)) {
        spdlog::info("RKNN model switch in progress, wait before init local LLM");
        return;
    }

    if (recognition_app_->are_models_loaded()) {
        spdlog::info("Releasing RKNN models before initializing local LLM");
        release_rknn_models_async();
        return;
    }

    spdlog::info("Initializing local LLM after RKNN release");
    LocalAiAnalysisService::instance()->initializeLocalLLM(pending_local_llm_model_path_);
}
