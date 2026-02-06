/**
 * @file main_window.h
 * @brief 主窗口类定义
 * @author CL
 * @date 2025-11-20
 *
 * 人脸识别考勤系统的主窗口，包含菜单栏、工具栏、视频显示区域、
 * 用户列表和考勤记录等组件。
 */

#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QStackedWidget>
#include <QTimer>
#include <QDate>
#include <QFuture>
#include <QFutureWatcher>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

#include "app/face_recognition_app.h"
#include "database/database_manager.h"
#include "service/user_service.h"
#include "service/attendance_service.h"
#include "gui_utils/audio_manager.h"
#include "gui_services/holiday_service.h"
#include "gui_services/news_service.h"

// 前向声明
class VideoDisplayWidget;
class FaceRegistrationDialog;
class SideMenu;
class TitleBar;
class UiRouter;
class RecognitionPage;
class DashboardPage;
class AttendancePage;
class UserManagementPage;
class SettingsPage;
class ModernTableView;
class AttendanceListWidget;

/**
 * @brief 主窗口类
 * 
 * 功能：
 * - 视频显示和实时识别
 * - 用户管理
 * - 考勤查询
 * - 系统设置
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    // 初始化系统
    bool initialize(const std::string& retinaface_model,
                   const std::string& facenet_model,
                   const std::string& camera_source,
                   int camera_id,
                   const std::string& db_path);

    // 初始化系统（异步，不阻塞 UI 显示）
    void initialize_async(const std::string& retinaface_model,
                          const std::string& facenet_model,
                          const std::string& camera_source,
                          int camera_id,
                          const std::string& db_path);

    // 启动/停止识别
    void start_recognition();
    void stop_recognition();

signals:
    // 信号
    void recognition_result_signal(int user_id, const QString& name, float similarity);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

public slots:
    // 数据刷新槽（公开，供子窗口调用）
    void load_users();

    // 加载今日考勤记录
    void load_today_attendance();
    
    // 应用设置（由 SettingsPage 调用）
    void apply_recognition_settings(float threshold);
    void apply_user_confirm_duration(int duration_ms);
    void apply_camera_settings(int deviceId);  // 应用摄像头设置

private slots:
    void on_action_open_camera();
    void on_action_close_camera();
    void on_action_exit();
    void on_action_register_face();
    void on_action_about();
    void on_action_toggle_theme();
    void on_action_toggle_maximize();  // 切换窗口最大化/还原
    void update_status();
    void on_frame_ready(const cv::Mat& frame, const std::vector<RecognitionResult>& results);
    void on_recognition_result(int user_id, const QString& name, float similarity, bool is_new_attendance, int check_type = 1, int status = 1);
    void drain_latest_frame();

    // NPU 模型异步加载/卸载完成回调
    void on_rknn_models_released(bool success);
    void on_rknn_models_reloaded(bool success);

private:
    void setup_ui();
    void setup_navigation();
    void setup_pages();
    void connect_page_signals();
    bool finish_initialization_after_core();

    // NPU 模型异步加载/卸载
    void release_rknn_models_async();
    void reload_rknn_models_async();

    // 系统组件
    std::unique_ptr<FaceRecognitionApp> recognition_app_;
    db::DatabaseManager* db_manager_;
    std::unique_ptr<service::UserService> user_service_;
    std::unique_ptr<service::AttendanceService> attendance_service_;
    
    // UI 组件
    VideoDisplayWidget* video_widget_;
    ModernTableView* user_table_;
    AttendanceListWidget* attendance_list_;
    
    SideMenu* side_menu_;
    TitleBar* title_bar_;
    QStackedWidget* content_stack_;
    UiRouter* router_;
    RecognitionPage* recognition_page_;
    DashboardPage* dashboard_page_;
    AttendancePage* attendance_page_;
    UserManagementPage* user_page_;
    SettingsPage* settings_page_;
    HolidayService* holiday_service_;
    NewsService* news_service_;

    QLabel* status_label_;
    QLabel* fps_label_;
    QLabel* recognition_label_;
    QLabel* attendance_status_label_;
    
    // 用户信息面板的 label
    QLabel* user_name_label_;
    QLabel* user_id_label_;
    QLabel* user_dept_label_;
    QLabel* user_similarity_label_;
    QLabel* check_type_label_;
    QLabel* avatar_label_;
    
    // 定时器
    QTimer* status_timer_;

    // 后台识别线程
    std::thread recognition_thread_;
    std::thread init_thread_;

    // 对话框
    FaceRegistrationDialog* registration_dialog_;
    // 注意：AttendanceQueryWidget 和 UserManagementWidget 每次创建新窗口，不需要成员变量
    
    // 系统状态
    std::atomic<bool> is_running_;
    bool recognition_paused_for_llm_;   // 识别是否因 LLM 而暂停（用于恢复）
    double npu_fps_;                    // NPU 帧率（YOLO 检测能力，约 50+ FPS）
    double camera_fps_;                 // 摄像头采集帧率（约 30 FPS）
    std::chrono::steady_clock::time_point last_fps_time_;
    std::atomic<bool> closing_{false};
    std::atomic<bool> init_in_progress_{false};
    std::atomic<bool> rknn_switching_{false};   // NPU 模型切换中（防止重复触发）
    std::atomic<bool> pending_recognition_start_{false};  // 切换完成后需要启动识别

    // NPU 模型异步切换 Future Watcher
    QFutureWatcher<bool>* rknn_release_watcher_;
    QFutureWatcher<bool>* rknn_reload_watcher_;

    // GUI 帧更新背压：仅保留最新帧，避免 Qt 事件队列堆积导致内存上涨 / FPS 下降
    std::mutex latest_frame_mutex_;
    cv::Mat latest_frame_;
    std::vector<RecognitionResult> latest_results_;
    std::atomic<bool> ui_update_scheduled_{false};
    std::atomic<uint64_t> latest_frame_seq_{0};

    // 当前日期（用于跨日检测）
    QDate current_date_;
    
    // 用户持续识别确认机制（基于时间而非帧数）
    struct UserDetection {
        bool is_detecting;                                      // 是否正在检测用户
        int user_id;                                            // 当前检测的用户ID
        std::string user_name;                                  // 用户名
        float max_similarity;                                   // 检测期间最高相似度
        std::chrono::steady_clock::time_point first_seen;       // 第一次检测到用户的时间
        std::chrono::steady_clock::time_point last_seen;        // 最后一次检测到用户的时间
        bool attendance_recorded;                               // 本次检测是否已记录考勤
    };
    UserDetection user_detection_;
    int last_displayed_user_id_;
    std::atomic<int> user_confirm_duration_ms_;                 // 用户确认时长（可配置，默认1秒）
    static constexpr int USER_DETECTION_TIMEOUT_MS = 500;       // 用户检测超时（500ms，帧间隔容差）
    
    // 陌生人持续检测机制（基于时间而非帧数）
    struct StrangerDetection {
        bool is_detecting;                                      // 是否正在检测陌生人
        std::chrono::steady_clock::time_point first_seen;       // 第一次检测到陌生人的时间
        std::chrono::steady_clock::time_point last_seen;        // 最后一次检测到陌生人的时间
    };
    StrangerDetection stranger_detection_;
    static constexpr int STRANGER_CONFIRM_DURATION_MS = 2000;   // 陌生人确认时长（2秒）
    static constexpr int STRANGER_DETECTION_TIMEOUT_MS = 500;   // 陌生人检测超时（500ms，帧间隔容差）
    
    // 不同音频类型的冷却时间（毫秒）
    static constexpr int DUPLICATE_CHECK_COOLDOWN_MS = 10000;   // 重复签到/签退冷却（10秒）
    static constexpr int STRANGER_AUDIO_COOLDOWN_MS = 10000;    // 陌生人提示音冷却（10秒）


    // 配置
    std::string retinaface_model_;
    std::string facenet_model_;
    std::string camera_source_;
    int camera_id_;
};
