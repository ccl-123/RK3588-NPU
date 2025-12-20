#pragma once

#include <QWidget>
#include "services/weather_service.h"
#include "widgets/attendance_list_widget.h"

class CardWidget;
class QLabel;
class QProgressBar;
class StatusTag;
class VideoDisplayWidget;

/**
 * @brief RecognitionPage 人脸识别主页面。
 */
class RecognitionPage : public QWidget {
    Q_OBJECT
public:
    explicit RecognitionPage(QWidget* parent = nullptr);

    VideoDisplayWidget* videoWidget() const;
    AttendanceListWidget* attendanceList() const;
    QLabel* statusLabel() const;
    QLabel* fpsLabel() const;
    QLabel* recognitionLabel() const;
    QLabel* attendanceStatusLabel() const;
    
    // 用户信息面板的 label
    QLabel* userNameLabel() const;
    QLabel* userIdLabel() const;
    QLabel* userDeptLabel() const;
    QLabel* userSimilarityLabel() const;
    QLabel* checkTypeLabel() const;
    
    // 状态栏相关
    QLabel* clockLabel() const;
    QLabel* dateLabel() const;
    QLabel* faceCountLabel() const;
    QLabel* detectionStatusLabel() const;
    QProgressBar* detectionProgressBar() const;
    void updateHolidayStatus(const QString& status, const QString& countdown);
    
    // 状态栏更新方法
    void updateClock(const QString& time);
    void updateDate(const QString& date);
    void updateFaceCount(int count);
    void updateDetectionStatus(const QString& status, int progress = -1);
    
    // 信息栏更新方法（天气 + 考勤统计）
    void updateWeather(const QString& city, const QString& temp);
    void updateWeatherDesc(const QString& desc);
    void updateAttendanceStats(int checkin_count, int checkout_count, int late_count, int early_leave_count);
    void updateCheckMode(bool is_checkout_mode);
    
    // 刷新天气
    void refreshWeather();
    void resetLocationCache();  // 重置位置缓存（设置变更后需要调用）
    
    // 刷新每日一句
    void refreshDailySentence();

signals:
    void startRecognitionRequested();
    void stopRecognitionRequested();
    void registerFaceRequested();
    void refreshAttendanceRequested();

private:
    CardWidget* createVideoCard();
    QWidget* createInfoBar();       // 新增：信息栏（天气 + 考勤统计）
    QWidget* createStatusBar();
    CardWidget* createStatusCard();
    CardWidget* createAttendanceCard();

    VideoDisplayWidget* video_widget_;
    AttendanceListWidget* attendance_list_;
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
    
    // 状态栏组件
    QLabel* clock_label_;
    QLabel* date_label_;
    QLabel* face_count_label_;
    QLabel* detection_status_label_;
    QProgressBar* detection_progress_bar_;
    QLabel* holiday_status_label_;
    QLabel* holiday_countdown_label_;
    
    // 信息栏组件（天气 + 考勤统计）
    QLabel* weather_label_;      // 城市名
    QLabel* temp_label_;         // 温度
    QLabel* weather_desc_;       // 天气描述
    QLabel* checkin_count_label_;
    QLabel* checkout_count_label_;
    QLabel* late_count_label_;
    QLabel* check_mode_label_;
    QLabel* attendance_date_label_;
    
    // AQI 和 UV 显示标签
    QLabel* aqi_label_;
    QLabel* uv_label_;
    
    // 一言（每次随机）
    QLabel* sentence_en_label_;   // 一言句子
    QLabel* sentence_cn_label_;   // 一言来源
    
    // 服务引用
    WeatherService* weather_service_;
    
    // 位置信息缓存
    QString current_city_;      // 当前城市名
    double current_lat_;        // 纬度
    double current_lon_;        // 经度
    bool location_fetched_;     // 是否已获取位置
};
