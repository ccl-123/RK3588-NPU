#pragma once

#include <QWidget>

class CardWidget;
class QLabel;
class ModernTableView;
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
    ModernTableView* attendanceTable() const;
    QLabel* statusLabel() const;
    QLabel* fpsLabel() const;
    QLabel* recognitionLabel() const;
    QLabel* attendanceStatusLabel() const;

signals:
    void startRecognitionRequested();
    void stopRecognitionRequested();
    void registerFaceRequested();

private:
    CardWidget* createVideoCard();
    CardWidget* createStatusCard();
    CardWidget* createAttendanceCard();

    VideoDisplayWidget* video_widget_;
    ModernTableView* attendance_table_;
    QLabel* status_label_;
    QLabel* fps_label_;
    QLabel* recognition_label_;
    QLabel* attendance_status_label_;
};

