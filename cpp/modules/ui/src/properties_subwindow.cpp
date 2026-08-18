#include <zima/ui/properties_subwindow.hpp>

#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace zima::ui {

PropertiesSubWindow::PropertiesSubWindow(const QString& title, QWidget* parent)
    : QDialog(parent) {
    setWindowFlags(Qt::SubWindow | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    setModal(false);
    setObjectName("zimaPropertiesSubWindow");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "QDialog#zimaPropertiesSubWindow { background: palette(window);"
        " border: 1px solid palette(mid); border-radius: 5px; }");
    setWindowTitle(title);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 6, 8, 8);
    title_label_ = new QLabel(title, this);
    auto title_font = title_label_->font();
    title_font.setBold(true);
    title_label_->setFont(title_font);
    title_label_->setMinimumHeight(28);
    title_label_->setStyleSheet(
        "background: palette(midlight); border: 1px solid palette(mid);"
        " border-radius: 4px; padding-left: 8px;");
    outer->addWidget(title_label_);

    content_layout_ = new QVBoxLayout;
    outer->addLayout(content_layout_);
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                    Qt::Horizontal, this);
    for (auto* abstract_button : buttons_->buttons()) {
        if (auto* button = qobject_cast<QPushButton*>(abstract_button)) {
            button->setAutoDefault(false);
            button->setDefault(false);
        }
    }
    connect(buttons_, &QDialogButtonBox::accepted, this, [this] {
        if (submit()) accept();
    });
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons_);
    qApp->installEventFilter(this);
}

PropertiesSubWindow::~PropertiesSubWindow() {
    if (qApp != nullptr) qApp->removeEventFilter(this);
}

QVBoxLayout* PropertiesSubWindow::content_layout() const { return content_layout_; }
QDialogButtonBox* PropertiesSubWindow::buttons() const { return buttons_; }

void PropertiesSubWindow::set_internal_title(const QString& title) {
    setWindowTitle(title);
    title_label_->setText(title);
}

void PropertiesSubWindow::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    adjustSize();
    if (parentWidget() != nullptr) {
        const int margin = 12;
        move(std::max(margin, parentWidget()->width() - width() - margin), margin);
    }
    QTimer::singleShot(0, this, [this] { keep_inside_parent(); raise(); });
}

void PropertiesSubWindow::moveEvent(QMoveEvent* event) {
    QDialog::moveEvent(event);
    keep_inside_parent();
}

bool PropertiesSubWindow::eventFilter(QObject*, QEvent* event) {
    if (isVisible() && event->type() == QEvent::MouseButtonDblClick) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::MiddleButton) {
            if (auto* ok = buttons_->button(QDialogButtonBox::Ok);
                ok != nullptr && ok->isEnabled()) {
                ok->click();
                return true;
            }
        }
    }
    return false;
}

void PropertiesSubWindow::keep_inside_parent() {
    if (correcting_position_ || parentWidget() == nullptr) return;
    correcting_position_ = true;
    const int maximum_x = std::max(0, parentWidget()->width() - width());
    const int maximum_y = std::max(0, parentWidget()->height() - height());
    const QPoint bounded(std::clamp(x(), 0, maximum_x), std::clamp(y(), 0, maximum_y));
    if (bounded != pos()) move(bounded);
    correcting_position_ = false;
}

}  // namespace zima::ui
