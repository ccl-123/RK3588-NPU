#pragma once

#include <QWidget>

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent = nullptr);

signals:
    void openSettingsDialogRequested();
};

