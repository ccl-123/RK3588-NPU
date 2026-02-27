/**
 * @file about_dialog.h
 * @brief 关于对话框类定义
 * @author CL
 * @date 2025-11-20
 *
 * 显示系统版本信息、技术栈、性能指标、开发者信息等。
 */

#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>

/**
 * @brief 关于对话框
 * 
 * 显示系统版本信息、开发者信息、许可证等
 */
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
    ~AboutDialog();

private:
    void setup_ui();
    QString get_system_info();
    QString get_version_info();
    QString get_license_info();
    
    // UI 组件
    QLabel* version_label_;
    QTextBrowser* info_browser_;
    QPushButton* close_btn_;
};


