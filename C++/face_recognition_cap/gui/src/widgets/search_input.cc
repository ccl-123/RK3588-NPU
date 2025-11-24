#include "widgets/search_input.h"

#include "utils/svg_icon_manager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>

SearchInput::SearchInput(QWidget* parent)
    : QWidget(parent)
    , line_edit_(new QLineEdit(this))
    , icon_label_(new QLabel(this))
    , debounce_timer_(new QTimer(this)) {
    setObjectName("SearchInput");
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    const QIcon search_icon = SvgIconManager::icon(":/icons/actions/search.svg", QSize(16, 16), QColor("#8c8c8c"));
    if (!search_icon.isNull()) {
        icon_label_->setPixmap(search_icon.pixmap(16, 16));
    } else {
        icon_label_->setText(QStringLiteral("🔍"));
    }
    icon_label_->setFixedSize(32, 32);
    icon_label_->setAlignment(Qt::AlignCenter);

    layout->addWidget(icon_label_);
    layout->addWidget(line_edit_);

    line_edit_->setPlaceholderText(tr("Search"));

    debounce_timer_->setInterval(300);
    debounce_timer_->setSingleShot(true);

    connect(line_edit_, &QLineEdit::textChanged, this, &SearchInput::onTextChanged);
    connect(debounce_timer_, &QTimer::timeout, this, &SearchInput::emitSearch);
}

void SearchInput::setPlaceholderText(const QString& text) {
    line_edit_->setPlaceholderText(text);
}

void SearchInput::setDebounceInterval(int ms) {
    debounce_timer_->setInterval(ms);
}

QString SearchInput::text() const {
    return line_edit_->text();
}

void SearchInput::onTextChanged(const QString&) {
    debounce_timer_->start();
}

void SearchInput::emitSearch() {
    emit searchTriggered(line_edit_->text());
}

