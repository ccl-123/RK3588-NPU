/**
 * @file face_registration_dialog.h
 * @brief 人脸注册对话框类定义
 * @author CL
 * @date 2025-11-20
 *
 * 提供图形化的人脸注册界面，支持用户信息输入、人脸采集、
 * 质量检测和特征提取。
 */

#ifndef FACE_REGISTRATION_DIALOG_H
#define FACE_REGISTRATION_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QTimer>
#include <QProgressBar>
#include <opencv2/opencv.hpp>
#include <vector>
#include <memory>

#include "app/face_recognition_app.h"
#include "service/user_service.h"

/**
 * @brief 人脸注册对话框
 * 
 * 功能：
 * - 用户信息输入
 * - 实时人脸采集
 * - 人脸质量检测
 * - 多角度采集（3-5张）
 */
class FaceRegistrationDialog : public QDialog {
    Q_OBJECT

public:
    explicit FaceRegistrationDialog(FaceRecognitionApp* app,
                                   service::UserService* user_service,
                                   QWidget* parent = nullptr);
    ~FaceRegistrationDialog();

private slots:
    // 按钮操作
    void on_capture_clicked();
    void on_delete_clicked();
    void on_register_clicked();
    void on_cancel_clicked();
    
    // 定时器更新
    void update_preview();
    
    // 列表选择
    void on_face_selected(int index);

private:
    // UI 初始化
    void setup_ui();
    
    // 人脸质量检测
    bool check_face_quality(const cv::Mat& face_image, std::string& hint);
    
    // 提取人脸特征
    bool extract_feature(const cv::Mat& face_image, std::vector<float>& feature);
    
    // 用户信息输入
    QLineEdit* name_edit_;
    QLineEdit* employee_id_edit_;
    QComboBox* department_combo_;
    
    // 人脸采集
    QLabel* preview_label_;
    QPushButton* capture_btn_;
    QPushButton* delete_btn_;
    QListWidget* captured_faces_list_;
    QProgressBar* progress_bar_;
    
    // 质量提示
    QLabel* quality_hint_label_;
    
    // 操作按钮
    QPushButton* register_btn_;
    QPushButton* cancel_btn_;
    
    // 定时器
    QTimer* preview_timer_;
    
    // 系统组件
    FaceRecognitionApp* recognition_app_;
    service::UserService* user_service_;
    
    // 采集的人脸数据
    std::vector<cv::Mat> captured_faces_;
    std::vector<std::vector<float>> captured_features_;

    // 当前预览帧
    cv::Mat current_frame_;
    
    // 配置
    static constexpr int MIN_FACES = 3;
    static constexpr int MAX_FACES = 5;
    static constexpr float QUALITY_THRESHOLD = 0.7f;
};

#endif // FACE_REGISTRATION_DIALOG_H

