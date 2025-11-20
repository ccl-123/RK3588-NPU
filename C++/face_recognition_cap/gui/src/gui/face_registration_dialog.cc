/**
 * @file face_registration_dialog.cc
 * @brief 人脸注册对话框类实现
 * @author CL
 * @date 2025-11-20
 */

#include "gui/face_registration_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QTimer>
#include <QPainter>
#include <spdlog/spdlog.h>

// 静态常量定义
constexpr int FaceRegistrationDialog::MIN_FACES;
constexpr int FaceRegistrationDialog::MAX_FACES;
constexpr float FaceRegistrationDialog::QUALITY_THRESHOLD;

FaceRegistrationDialog::FaceRegistrationDialog(FaceRecognitionApp* app,
                                             service::UserService* user_service,
                                             QWidget* parent)
    : QDialog(parent)
    , recognition_app_(app)
    , user_service_(user_service)
{
    setup_ui();
    
    // 初始化定时器
    preview_timer_ = new QTimer(this);
    connect(preview_timer_, &QTimer::timeout, this, &FaceRegistrationDialog::update_preview);
    preview_timer_->start(33);  // 约30 FPS
}

FaceRegistrationDialog::~FaceRegistrationDialog() {
    preview_timer_->stop();
}

void FaceRegistrationDialog::setup_ui() {
    setWindowTitle("人脸注册");
    resize(800, 600);
    
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    
    // 用户信息组
    QGroupBox* info_group = new QGroupBox("用户信息");
    QFormLayout* info_layout = new QFormLayout();
    
    name_edit_ = new QLineEdit();
    employee_id_edit_ = new QLineEdit();
    department_combo_ = new QComboBox();
    department_combo_->addItems({"技术部", "市场部", "研发部", "行政部", "财务部"});
    
    info_layout->addRow("姓名*:", name_edit_);
    info_layout->addRow("工号:", employee_id_edit_);
    info_layout->addRow("部门:", department_combo_);
    
    info_group->setLayout(info_layout);
    main_layout->addWidget(info_group);
    
    // 人脸采集组
    QGroupBox* capture_group = new QGroupBox("人脸采集");
    QHBoxLayout* capture_layout = new QHBoxLayout();
    
    // 预览区域
    QVBoxLayout* preview_layout = new QVBoxLayout();
    preview_label_ = new QLabel();
    preview_label_->setMinimumSize(400, 300);
    preview_label_->setStyleSheet("border: 1px solid gray;");
    preview_label_->setAlignment(Qt::AlignCenter);
    
    quality_hint_label_ = new QLabel("请正视摄像头");
    quality_hint_label_->setAlignment(Qt::AlignCenter);
    quality_hint_label_->setStyleSheet("color: blue; font-size: 14px;");
    
    preview_layout->addWidget(preview_label_);
    preview_layout->addWidget(quality_hint_label_);
    
    // 采集列表和按钮
    QVBoxLayout* list_layout = new QVBoxLayout();
    
    captured_faces_list_ = new QListWidget();
    captured_faces_list_->setMaximumWidth(200);
    connect(captured_faces_list_, &QListWidget::currentRowChanged, 
            this, &FaceRegistrationDialog::on_face_selected);
    
    capture_btn_ = new QPushButton("采集人脸");
    connect(capture_btn_, &QPushButton::clicked, this, &FaceRegistrationDialog::on_capture_clicked);
    
    delete_btn_ = new QPushButton("删除");
    delete_btn_->setEnabled(false);
    connect(delete_btn_, &QPushButton::clicked, this, &FaceRegistrationDialog::on_delete_clicked);
    
    progress_bar_ = new QProgressBar();
    progress_bar_->setRange(0, MAX_FACES);
    progress_bar_->setValue(0);
    
    list_layout->addWidget(new QLabel("已采集:"));
    list_layout->addWidget(captured_faces_list_);
    list_layout->addWidget(progress_bar_);
    list_layout->addWidget(capture_btn_);
    list_layout->addWidget(delete_btn_);
    list_layout->addStretch();
    
    capture_layout->addLayout(preview_layout);
    capture_layout->addLayout(list_layout);
    
    capture_group->setLayout(capture_layout);
    main_layout->addWidget(capture_group);
    
    // 操作按钮
    QHBoxLayout* button_layout = new QHBoxLayout();
    button_layout->addStretch();
    
    register_btn_ = new QPushButton("注册");
    register_btn_->setEnabled(false);
    connect(register_btn_, &QPushButton::clicked, this, &FaceRegistrationDialog::on_register_clicked);
    
    cancel_btn_ = new QPushButton("取消");
    connect(cancel_btn_, &QPushButton::clicked, this, &FaceRegistrationDialog::on_cancel_clicked);
    
    button_layout->addWidget(register_btn_);
    button_layout->addWidget(cancel_btn_);
    
    main_layout->addLayout(button_layout);
}

void FaceRegistrationDialog::update_preview() {
    if (!recognition_app_) {
        return;
    }

    // 获取当前帧
    cv::Mat frame;
    if (!recognition_app_->get_current_frame(frame)) {
        return;
    }

    current_frame_ = frame.clone();

    // 转换为 QImage 并显示
    cv::Mat rgb_frame;
    cv::cvtColor(frame, rgb_frame, cv::COLOR_BGR2RGB);

    QImage qimg(rgb_frame.data, rgb_frame.cols, rgb_frame.rows,
                rgb_frame.step, QImage::Format_RGB888);

    // 缩放到预览标签大小
    QPixmap pixmap = QPixmap::fromImage(qimg).scaled(
        preview_label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    preview_label_->setPixmap(pixmap);
}

void FaceRegistrationDialog::on_capture_clicked() {
    if (current_frame_.empty()) {
        QMessageBox::warning(this, "警告", "无法获取摄像头画面");
        return;
    }

    if (captured_faces_.size() >= MAX_FACES) {
        QMessageBox::information(this, "提示",
            QString("已达到最大采集数量 %1").arg(MAX_FACES));
        return;
    }

    if (!recognition_app_) {
        QMessageBox::critical(this, "错误", "识别引擎未初始化");
        return;
    }

    // 检测人脸
    std::vector<cv::Rect> face_boxes;
    std::vector<std::vector<cv::Point2f>> landmarks;

    int face_count = recognition_app_->detect_faces(current_frame_, face_boxes, landmarks);

    if (face_count == 0) {
        QMessageBox::warning(this, "警告", "未检测到人脸，请调整位置和光线");
        return;
    }

    if (face_count > 1) {
        QMessageBox::warning(this, "警告",
            QString("检测到 %1 张人脸，请确保画面中只有一个人").arg(face_count));
        return;
    }

    // 裁剪人脸区域
    cv::Rect face_box = face_boxes[0];

    // 扩展人脸框（增加 20% 边距）
    int margin_x = static_cast<int>(face_box.width * 0.2);
    int margin_y = static_cast<int>(face_box.height * 0.2);

    face_box.x = std::max(0, face_box.x - margin_x);
    face_box.y = std::max(0, face_box.y - margin_y);
    face_box.width = std::min(current_frame_.cols - face_box.x, face_box.width + 2 * margin_x);
    face_box.height = std::min(current_frame_.rows - face_box.y, face_box.height + 2 * margin_y);

    cv::Mat face_img = current_frame_(face_box).clone();

    // 检查人脸质量
    std::string hint;
    if (!check_face_quality(face_img, hint)) {
        QMessageBox::warning(this, "警告",
            QString("人脸质量不佳: %1").arg(QString::fromStdString(hint)));
        return;
    }

    // 提取特征（使用真实的 FaceNet 推理）
    std::vector<float> feature;
    cv::Rect detected_box;
    if (!recognition_app_->extract_feature_from_frame(current_frame_, feature, &detected_box)) {
        QMessageBox::warning(this, "警告", "特征提取失败，请重试");
        return;
    }

    // 保存人脸和特征
    captured_faces_.push_back(face_img);
    captured_features_.push_back(feature);

    // 更新列表
    QString item_text = QString("人脸 %1 (质量: %2)").arg(captured_faces_.size()).arg(QString::fromStdString(hint));
    captured_faces_list_->addItem(item_text);

    // 更新进度条
    progress_bar_->setValue(captured_faces_.size());

    // 更新注册按钮状态
    register_btn_->setEnabled(captured_faces_.size() >= MIN_FACES);

    spdlog::info("Captured face {}/{}, feature size: {}",
                captured_faces_.size(), MAX_FACES, feature.size());
}

void FaceRegistrationDialog::on_delete_clicked() {
    int current_row = captured_faces_list_->currentRow();
    if (current_row >= 0 && current_row < static_cast<int>(captured_faces_.size())) {
        captured_faces_.erase(captured_faces_.begin() + current_row);
        captured_features_.erase(captured_features_.begin() + current_row);
        delete captured_faces_list_->takeItem(current_row);
        progress_bar_->setValue(captured_faces_.size());

        register_btn_->setEnabled(captured_faces_.size() >= MIN_FACES);
    }
}

void FaceRegistrationDialog::on_register_clicked() {
    QString name = name_edit_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "警告", "请输入姓名");
        return;
    }

    if (captured_faces_.size() < MIN_FACES) {
        QMessageBox::warning(this, "警告",
            QString("至少需要采集 %1 张人脸照片").arg(MIN_FACES));
        return;
    }

    if (!user_service_) {
        QMessageBox::critical(this, "错误", "用户服务未初始化");
        return;
    }

    // 创建用户
    service::RegistrationResult result = user_service_->register_user(
        name.toStdString(),
        employee_id_edit_->text().toStdString(),
        department_combo_->currentText().toStdString()
    );

    if (!result.success) {
        QMessageBox::critical(this, "错误",
            QString("创建用户失败: %1").arg(QString::fromStdString(result.message)));
        return;
    }

    int user_id = result.user_id;

    spdlog::info("Created user: {} (ID: {})", name.toStdString(), user_id);

    // 保存人脸特征
    int success_count = 0;
    for (size_t i = 0; i < captured_features_.size(); i++) {
        if (user_service_->add_face_feature(user_id, captured_features_[i])) {
            success_count++;
        }
    }

    if (success_count == 0) {
        QMessageBox::critical(this, "错误", "保存人脸特征失败");
        user_service_->delete_user(user_id);  // 回滚
        return;
    }

    spdlog::info("Saved {} face features for user {}", success_count, user_id);

    // 重新加载特征库
    int loaded_count = user_service_->reload_feature_library();
    if (loaded_count > 0) {
        spdlog::info("Reloaded feature library: {} features", loaded_count);
    } else {
        spdlog::warn("Failed to reload feature library");
    }

    QMessageBox::information(this, "成功",
        QString("注册成功！\n用户: %1\n特征数: %2")
        .arg(name)
        .arg(success_count));

    accept();
}

void FaceRegistrationDialog::on_cancel_clicked() {
    reject();
}

void FaceRegistrationDialog::on_face_selected(int index) {
    delete_btn_->setEnabled(index >= 0);
}

bool FaceRegistrationDialog::check_face_quality(const cv::Mat& face_image, std::string& hint) {
    // 简单的质量检测：检查图像大小和亮度
    if (face_image.empty()) {
        hint = "图像为空";
        return false;
    }

    if (face_image.cols < 50 || face_image.rows < 50) {
        hint = "图像太小";
        return false;
    }

    // 检查亮度
    cv::Mat gray;
    if (face_image.channels() == 3) {
        cv::cvtColor(face_image, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = face_image;
    }

    cv::Scalar mean_val = cv::mean(gray);
    double brightness = mean_val[0];

    if (brightness < 50) {
        hint = "光线太暗";
        return false;
    }

    if (brightness > 200) {
        hint = "光线太亮";
        return false;
    }

    hint = "质量良好";
    return true;
}

bool FaceRegistrationDialog::extract_feature(const cv::Mat& face_image, std::vector<float>& feature) {
    if (!recognition_app_) {
        spdlog::error("Recognition app is null");
        return false;
    }

    // 使用真实的特征提取（包含检测、对齐、推理）
    bool success = recognition_app_->extract_feature_from_frame(face_image, feature);

    if (!success) {
        spdlog::error("Failed to extract feature from face image");
        return false;
    }

    spdlog::info("Successfully extracted feature, size: {}", feature.size());
    return true;
}

