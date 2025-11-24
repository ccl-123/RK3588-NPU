#pragma once

#include <QLabel>

class StatusTag : public QLabel {
    Q_OBJECT
public:
    enum class Type {
        Default,
        Success,
        Warning,
        Error,
        Processing
    };

    explicit StatusTag(QWidget* parent = nullptr);

    void setType(Type type);
    Type type() const;

private:
    void updateAppearance();

    Type type_ = Type::Default;
};

