/**
 * @file main_window.h
 * @brief 主窗口类定义
 * @author CL
 * @date 2025-11-20
 *
 * 人脸识别考勤系统的主窗口，包含菜单栏、工具栏、视频显示区域、
 * 用户列表和考勤记录等组件。
 */

#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QLabel>
#include <QStatusBar>
#include <QMenuBar>
#include <QToolBar>
#include <QDockWidget>
#include <QTableWidget>
#include <QPushButton>
#include <memory>

#include "app/face_recognition_app.h"
#include "database/database_manager.h"
#include "service/user_service.h"
#include "service/attendance_service.h"

// 前向声明
class VideoDisplayWidget;
class FaceRegistrationDialog;
class AttendanceQueryWidget;

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

    // 启动/停止识别
    void start_recognition();
    void stop_recognition();

signals:
    // 信号
    void recognition_result_signal(int user_id, const QString& name, float similarity);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // 菜单栏操作
    void on_action_open_camera();
    void on_action_close_camera();
    void on_action_settings();
    void on_action_exit();
    
    // 工具栏操作
    void on_action_register_face();
    void on_action_query_attendance();
    void on_action_user_management();
    
    // 帮助菜单
    void on_action_about();
    
    // 定时器更新
    void update_frame();
    void update_status();
    
    // 识别回调
    void on_recognition_result(int user_id, const QString& name, float similarity, bool is_new_attendance);

private:
    // UI 初始化
    void setup_ui();
    void create_menus();
    void create_toolbars();
    void create_status_bar();
    void create_dock_widgets();
    
    // 系统组件
    std::unique_ptr<FaceRecognitionApp> recognition_app_;
    db::DatabaseManager* db_manager_;
    std::unique_ptr<service::UserService> user_service_;
    std::unique_ptr<service::AttendanceService> attendance_service_;
    
    // UI 组件
    VideoDisplayWidget* video_widget_;
    QDockWidget* user_list_dock_;
    QDockWidget* attendance_dock_;
    QTableWidget* user_table_;
    QTableWidget* attendance_table_;
    
    // 状态栏组件
    QLabel* status_label_;
    QLabel* fps_label_;
    QLabel* recognition_label_;
    QLabel* attendance_status_label_;  // 签到状态提示标签
    
    // 定时器
    QTimer* frame_timer_;
    QTimer* status_timer_;
    
    // 对话框
    FaceRegistrationDialog* registration_dialog_;
    // 注意：AttendanceQueryWidget 和 UserManagementWidget 每次创建新窗口，不需要成员变量
    
    // 系统状态
    bool is_running_;
    int frame_count_;
    double fps_;
    std::chrono::steady_clock::time_point last_fps_time_;
    
    // 配置
    std::string retinaface_model_;
    std::string facenet_model_;
    std::string camera_source_;
    int camera_id_;
    std::string db_path_;
};

#endif // MAIN_WINDOW_H

