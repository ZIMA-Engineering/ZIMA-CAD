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
#include <QHBoxLayout>

#include <algorithm>

namespace zima::ui {

PropertiesSubWindow::PropertiesSubWindow(const QString& title, QWidget* parent)
    : QDialog(parent) {
    setWindowFlags(Qt::SubWindow | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    setModal(false);
    setObjectName("zimaPropertiesSubWindow");
    setProperty("zimaPropertiesSubWindow", true);
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);
    setStyleSheet(
        "QDialog[zimaPropertiesSubWindow=\"true\"] { background: palette(window);"
        " border: 1px solid #5b6065; border-radius: 5px; }");
    setWindowTitle(title);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 6, 8, 8);
    title_bar_ = new QWidget(this);
    title_bar_->setObjectName("propertiesTitleBar");
    title_bar_->setFixedHeight(34);
    title_bar_->setCursor(Qt::SizeAllCursor);
    title_bar_->installEventFilter(this);
    title_bar_->setStyleSheet(
        "QWidget#propertiesTitleBar { background: palette(midlight);"
        " border: 1px solid palette(mid); border-radius: 4px; }");
    auto* title_layout = new QHBoxLayout(title_bar_);
    title_layout->setContentsMargins(10, 2, 4, 2);
    title_label_ = new QLabel(title, title_bar_);
    auto title_font = title_label_->font();
    title_font.setBold(true);
    title_label_->setFont(title_font);
    title_layout->addWidget(title_label_, 1);
    auto* close = new QPushButton(QStringLiteral("×"), title_bar_);
    close->setObjectName("propertiesCloseButton");
    close->setFixedSize(27, 26);
    close->setToolTip(tr("Zrušit"));
    close->setStyleSheet(
        "QPushButton { border:none; border-radius:4px; font-weight:700; }"
        "QPushButton:hover { background:#b83232; color:white; }");
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    title_layout->addWidget(close);
    outer->addWidget(title_bar_);

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

void PropertiesSubWindow::set_centered_on_show(bool centered) {
    centered_on_show_ = centered;
}

void PropertiesSubWindow::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    adjustSize();
    if (parentWidget() != nullptr) {
        const int margin = 12;
        if (centered_on_show_) {
            move(std::max(0, (parentWidget()->width() - width()) / 2),
                 std::max(0, (parentWidget()->height() - height()) / 2));
        } else {
            move(std::max(margin, parentWidget()->width() - width() - margin), margin);
        }
    }
    QTimer::singleShot(0, this, [this] { keep_inside_parent(); raise(); });
}

void PropertiesSubWindow::moveEvent(QMoveEvent* event) {
    QDialog::moveEvent(event);
    keep_inside_parent();
}

bool PropertiesSubWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == title_bar_) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                title_drag_origin_ = mouse->globalPosition();
                title_drag_window_origin_ = pos();
                title_drag_active_ = true;
                mouse->accept();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && title_drag_active_) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->buttons().testFlag(Qt::LeftButton)) {
                const QPointF delta = mouse->globalPosition() - title_drag_origin_;
                move(title_drag_window_origin_ + QPoint(
                    static_cast<int>(delta.x()), static_cast<int>(delta.y())));
                mouse->accept();
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                title_drag_active_ = false;
                mouse->accept();
                return true;
            }
        }
    }
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
