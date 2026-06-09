/**
 * @file about_dialog.cc
 * @brief 关于对话框类实现
 * @author CL
 * @date 2025-11-20
 */

#include "gui/about_dialog.h"

#include "widgets/card_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QTextStream>
#include <QSysInfo>
#include <QVariant>

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8("关于人脸识别考勤系统"));
    if (auto* screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        resize(qMax(360, qMin(640, available.width() - 48)),
               qMax(320, qMin(520, available.height() - 48)));
    } else {
        resize(640, 520);
    }

    setup_ui();
}

AboutDialog::~AboutDialog() {
}

void AboutDialog::setup_ui() {
    auto main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(24, 24, 24, 24);
    main_layout->setSpacing(24);

    auto hero_card = new CardWidget(this);
    hero_card->setTitle(tr("人脸识别考勤系统"));
    hero_card->setSubtitle(get_version_info());
    hero_card->setVariant("default");

    auto hero_layout = new QVBoxLayout(hero_card->bodyContainer());
    hero_layout->setContentsMargins(0, 0, 0, 0);
    hero_layout->setSpacing(12);

    version_label_ = new QLabel(get_version_info(), hero_card);
    version_label_->setAlignment(Qt::AlignLeft);
    version_label_->setObjectName("Caption");
    hero_layout->addWidget(version_label_);

    auto info_card = new CardWidget(this);
    info_card->setTitle(tr("系统信息"));
    info_card->setSubtitle(tr("平台、技术栈与性能指标"));

    info_browser_ = new QTextBrowser(info_card);
    info_browser_->setOpenExternalLinks(true);
    info_browser_->setHtml(get_system_info());

    auto info_layout = new QVBoxLayout(info_card->bodyContainer());
    info_layout->setContentsMargins(0, 0, 0, 0);
    info_layout->addWidget(info_browser_);

    auto buttons = new QHBoxLayout();
    buttons->addStretch();
    close_btn_ = new QPushButton(tr("关闭"), this);
    close_btn_->setMinimumWidth(120);
    close_btn_->setProperty("primary", QVariant(true));
    connect(close_btn_, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(close_btn_);

    main_layout->addWidget(hero_card);
    main_layout->addWidget(info_card, 1);
    main_layout->addLayout(buttons);
}

QString AboutDialog::get_version_info() {
    return QString::fromUtf8("版本 2.1.0 (2025-12-18)");
}

QString AboutDialog::get_system_info() {
    QString info;
    QTextStream stream(&info);
    
    stream << "<html><head><meta charset='UTF-8'></head><body style='font-family: Arial, sans-serif;'>";
    
    // 系统信息
    stream << QString::fromUtf8("<h3>📊 系统信息</h3>");
    stream << "<table cellpadding='5'>";
    stream << QString::fromUtf8("<tr><td><b>平台:</b></td><td>Rockchip RK3588 (ARM64)</td></tr>");
    stream << QString::fromUtf8("<tr><td><b>操作系统:</b></td><td>") << QSysInfo::prettyProductName() << "</td></tr>";
    stream << QString::fromUtf8("<tr><td><b>内核:</b></td><td>") << QSysInfo::kernelVersion() << "</td></tr>";
    stream << QString::fromUtf8("<tr><td><b>Qt 版本:</b></td><td>") << qVersion() << "</td></tr>";
    stream << "</table>";
    
    // 技术栈
    stream << QString::fromUtf8("<h3>🔧 技术栈</h3>");
    stream << "<table cellpadding='5'>";
    stream << QString::fromUtf8("<tr><td><b>AI 加速:</b></td><td>RKNN NPU 2.3.0</td></tr>");
    stream << QString::fromUtf8("<tr><td><b>人脸检测:</b></td><td>YOLOv8-face</td></tr>");
    stream << QString::fromUtf8("<tr><td><b>特征提取:</b></td><td>MobileFaceNet (512维)</td></tr>");
    stream << QString::fromUtf8("<tr><td><b>计算机视觉:</b></td><td>OpenCV 4.5.4</td></tr>");
    stream << QString::fromUtf8("<tr><td><b>数据库:</b></td><td>SQLite 3.37.2</td></tr>");
    stream << QString::fromUtf8("<tr><td><b>日志库:</b></td><td>spdlog 1.12.0</td></tr>");
    stream << "</table>";
    
    // 性能指标
    stream << QString::fromUtf8("<h3>⚡ 性能指标</h3>");
    stream << "<table cellpadding='5'>";
    stream << QString::fromUtf8("<tr><td><b>识别帧率:</b></td><td>45-65 FPS</td></tr>");
    stream << QString::fromUtf8("<tr><td><b>识别延迟:</b></td><td>&lt;80ms</td></tr>");
    stream << QString::fromUtf8("<tr><td><b>识别准确率:</b></td><td>&gt;95% (良好光照)</td></tr>");
    stream << QString::fromUtf8("<tr><td><b>内存占用:</b></td><td>~200MB</td></tr>");
    stream << "</table>";
    
    // 功能特性
    stream << QString::fromUtf8("<h3>✨ 功能特性</h3>");
    stream << "<ul>";
    stream << QString::fromUtf8("<li>✅ 实时人脸检测和识别</li>");
    stream << QString::fromUtf8("<li>✅ 自动考勤记录（签到/签退）</li>");
    stream << QString::fromUtf8("<li>✅ 用户信息管理</li>");
    stream << QString::fromUtf8("<li>✅ 考勤记录查询和导出</li>");
    stream << QString::fromUtf8("<li>✅ 人脸质量检测</li>");
    stream << QString::fromUtf8("<li>✅ 数据库持久化存储</li>");
    stream << "</ul>";
    
    // 开发者信息
    stream << QString::fromUtf8("<h3>👥 开发者</h3>");
    stream << QString::fromUtf8("<p><b>开发:</b> CL<br>");
    stream << QString::fromUtf8("<b>文档:</b> Documentation Team<br>");
    stream << QString::fromUtf8("<b>测试:</b> QA Team</p>");
    
    // 许可证
    stream << QString::fromUtf8("<h3>📝 许可证</h3>");
    stream << QString::fromUtf8("<p>Copyright © 2025. All rights reserved.<br>");
    stream << QString::fromUtf8("本软件为专有软件，未经授权不得复制、分发或修改。</p>");
    
    // 联系方式
    stream << QString::fromUtf8("<h3>📞 联系方式</h3>");
    stream << QString::fromUtf8("<p><b>技术支持:</b> support@example.com<br>");
    stream << QString::fromUtf8("<b>问题反馈:</b> issues@example.com<br>");
    stream << QString::fromUtf8("<b>官方网站:</b> <a href='https://example.com'>https://example.com</a></p>");
    
    stream << "</body></html>";
    
    return info;
}

QString AboutDialog::get_license_info() {
    return QString::fromUtf8(
        "Copyright © 2025. All rights reserved.\n\n"
        "本软件为专有软件，未经授权不得复制、分发或修改。\n\n"
        "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, "
        "EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF "
        "MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT."
    );
}
