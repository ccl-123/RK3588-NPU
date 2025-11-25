#pragma once

#include <QWidget>

class CardWidget;
class QLabel;
class QProgressBar;
class ModernTableView;
class StatusTag;
class VideoDisplayWidget;
class QNetworkAccessManager;
class QNetworkReply;

/**
 * @brief RecognitionPage 人脸识别主页面。
 */
class RecognitionPage : public QWidget {
    Q_OBJECT
public:
    explicit RecognitionPage(QWidget* parent = nullptr);

    VideoDisplayWidget* videoWidget() const;
    ModernTableView* attendanceTable() const;
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
    
    // 状态栏更新方法
    void updateClock(const QString& time);
    void updateDate(const QString& date);
    void updateFaceCount(int count);
    void updateDetectionStatus(const QString& status, int progress = -1);
    
    // 信息栏更新方法（天气 + 考勤统计）
    void updateWeather(const QString& weather, const QString& temp);
    void updateAttendanceStats(int checkin_count, int checkout_count, int late_count, int early_leave_count);
    void updateCheckMode(bool is_checkout_mode);
    
    // 刷新天气
    void refreshWeather();

signals:
    void startRecognitionRequested();
    void stopRecognitionRequested();
    void registerFaceRequested();

private slots:
    void onWeatherReplyFinished(QNetworkReply* reply);

private:
    QString weatherCodeToString(int code);  // 天气代码转中文
    
    CardWidget* createVideoCard();
    QWidget* createInfoBar();       // 新增：信息栏（天气 + 考勤统计）
    QWidget* createStatusBar();
    CardWidget* createStatusCard();
    CardWidget* createAttendanceCard();

    VideoDisplayWidget* video_widget_;
    ModernTableView* attendance_table_;
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
    
    // 信息栏组件（天气 + 考勤统计）
    QLabel* weather_label_;
    QLabel* temp_label_;
    QLabel* checkin_count_label_;
    QLabel* checkout_count_label_;
    QLabel* late_count_label_;
    QLabel* check_mode_label_;
    
    // 网络请求（天气）
    QNetworkAccessManager* network_manager_;
};

