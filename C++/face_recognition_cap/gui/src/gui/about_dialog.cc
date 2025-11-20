/**
 * @file about_dialog.cc
 * @brief 关于对话框类实现
 * @author CL
 * @date 2025-11-20
 */

#include "gui/about_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QFile>
#include <QTextStream>
#include <QSysInfo>

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("关于人脸识别考勤系统");
    setFixedSize(600, 500);
    
    setup_ui();
}

AboutDialog::~AboutDialog() {
}

void AboutDialog::setup_ui() {
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(20);
    main_layout->setContentsMargins(30, 30, 30, 30);
    
    // Logo 和标题
    title_label_ = new QLabel("人脸识别考勤系统", this);
    QFont title_font = title_label_->font();
    title_font.setPointSize(20);
    title_font.setBold(true);
    title_label_->setFont(title_font);
    title_label_->setAlignment(Qt::AlignCenter);
    main_layout->addWidget(title_label_);
    
    // 版本信息
    version_label_ = new QLabel(get_version_info(), this);
    QFont version_font = version_label_->font();
    version_font.setPointSize(12);
    version_label_->setFont(version_font);
    version_label_->setAlignment(Qt::AlignCenter);
    version_label_->setStyleSheet("color: #666;");
    main_layout->addWidget(version_label_);
    
    // 分隔线
    QFrame* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    main_layout->addWidget(line);
    
    // 详细信息
    info_browser_ = new QTextBrowser(this);
    info_browser_->setOpenExternalLinks(true);
    info_browser_->setHtml(get_system_info());
    main_layout->addWidget(info_browser_);
    
    // 关闭按钮
    QHBoxLayout* button_layout = new QHBoxLayout();
    button_layout->addStretch();
    
    close_btn_ = new QPushButton("关闭", this);
    close_btn_->setMinimumWidth(100);
    connect(close_btn_, &QPushButton::clicked, this, &QDialog::accept);
    button_layout->addWidget(close_btn_);
    
    main_layout->addLayout(button_layout);
}

QString AboutDialog::get_version_info() {
    return QString("版本 1.0.0 (2025-11-20)");
}

QString AboutDialog::get_system_info() {
    QString info;
    QTextStream stream(&info);
    
    stream << "<html><body style='font-family: Arial, sans-serif;'>";
    
    // 系统信息
    stream << "<h3>📊 系统信息</h3>";
    stream << "<table cellpadding='5'>";
    stream << "<tr><td><b>平台:</b></td><td>Rockchip RK3588 (ARM64)</td></tr>";
    stream << "<tr><td><b>操作系统:</b></td><td>" << QSysInfo::prettyProductName() << "</td></tr>";
    stream << "<tr><td><b>内核:</b></td><td>" << QSysInfo::kernelVersion() << "</td></tr>";
    stream << "<tr><td><b>Qt 版本:</b></td><td>" << qVersion() << "</td></tr>";
    stream << "</table>";
    
    // 技术栈
    stream << "<h3>🔧 技术栈</h3>";
    stream << "<table cellpadding='5'>";
    stream << "<tr><td><b>AI 加速:</b></td><td>RKNN NPU 2.3.0</td></tr>";
    stream << "<tr><td><b>人脸检测:</b></td><td>RetinaFace</td></tr>";
    stream << "<tr><td><b>特征提取:</b></td><td>MobileFaceNet (512维)</td></tr>";
    stream << "<tr><td><b>计算机视觉:</b></td><td>OpenCV 4.5.4</td></tr>";
    stream << "<tr><td><b>数据库:</b></td><td>SQLite 3.37.2</td></tr>";
    stream << "<tr><td><b>日志库:</b></td><td>spdlog 1.12.0</td></tr>";
    stream << "</table>";
    
    // 性能指标
    stream << "<h3>⚡ 性能指标</h3>";
    stream << "<table cellpadding='5'>";
    stream << "<tr><td><b>识别帧率:</b></td><td>45-65 FPS (GUI) / 60-73 FPS (CLI)</td></tr>";
    stream << "<tr><td><b>识别延迟:</b></td><td>&lt;80ms (GUI) / &lt;50ms (CLI)</td></tr>";
    stream << "<tr><td><b>识别准确率:</b></td><td>&gt;95% (良好光照)</td></tr>";
    stream << "<tr><td><b>内存占用:</b></td><td>~200MB (GUI) / ~150MB (CLI)</td></tr>";
    stream << "</table>";
    
    // 功能特性
    stream << "<h3>✨ 功能特性</h3>";
    stream << "<ul>";
    stream << "<li>✅ 实时人脸检测和识别</li>";
    stream << "<li>✅ 自动考勤记录（签到/签退）</li>";
    stream << "<li>✅ 用户信息管理</li>";
    stream << "<li>✅ 考勤记录查询和导出</li>";
    stream << "<li>✅ 人脸质量检测</li>";
    stream << "<li>✅ 数据库持久化存储</li>";
    stream << "</ul>";
    
    // 开发者信息
    stream << "<h3>👥 开发者</h3>";
    stream << "<p><b>开发:</b> CL<br>";
    stream << "<b>文档:</b> Documentation Team<br>";
    stream << "<b>测试:</b> QA Team</p>";
    
    // 许可证
    stream << "<h3>📝 许可证</h3>";
    stream << "<p>Copyright © 2025. All rights reserved.<br>";
    stream << "本软件为专有软件，未经授权不得复制、分发或修改。</p>";
    
    // 联系方式
    stream << "<h3>📞 联系方式</h3>";
    stream << "<p><b>技术支持:</b> support@example.com<br>";
    stream << "<b>问题反馈:</b> issues@example.com<br>";
    stream << "<b>官方网站:</b> <a href='https://example.com'>https://example.com</a></p>";
    
    stream << "</body></html>";
    
    return info;
}

QString AboutDialog::get_license_info() {
    return QString(
        "Copyright © 2025. All rights reserved.\n\n"
        "本软件为专有软件，未经授权不得复制、分发或修改。\n\n"
        "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, "
        "EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF "
        "MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT."
    );
}

