#include <zima/ui/properties_subwindow.hpp>

#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>
#include <exception>

namespace zima::ui {

PropertiesSubWindow::PropertiesSubWindow(const QString& title, QWidget* parent)
    : QDialog(parent) {
    setWindowFlags(Qt::SubWindow | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    setModal(false);
    setSizeGripEnabled(true);
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
    submit_error_ = new QLabel(this);
    submit_error_->setObjectName("propertiesSubmitError");
    submit_error_->setWordWrap(true);
    submit_error_->setStyleSheet("color:#ed7777;font-weight:700;");
    submit_error_->hide();
    outer->addWidget(submit_error_);
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                    Qt::Horizontal, this);
    for (auto* abstract_button : buttons_->buttons()) {
        if (auto* button = qobject_cast<QPushButton*>(abstract_button)) {
            button->setAutoDefault(false);
            button->setDefault(false);
        }
    }
    connect(buttons_, &QDialogButtonBox::accepted, this, [this] {
        submit_error_->hide();
        try {
            if (submit()) accept();
        } catch (const std::exception& error) {
            submit_error_->setText(QString::fromUtf8(error.what()));
            submit_error_->show();
        } catch (...) {
            submit_error_->setText(tr("Operaci nelze dokončit."));
            submit_error_->show();
        }
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

void PropertiesSubWindow::set_initial_size(const QSize& size) {
    initial_size_ = size;
}

void PropertiesSubWindow::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (initial_size_.isValid()) {
        QSize target = initial_size_.expandedTo(minimumSizeHint());
        if (parentWidget() != nullptr) {
            target.setWidth(std::min(target.width(), parentWidget()->width()));
            target.setHeight(std::min(target.height(), parentWidget()->height()));
        }
        resize(target);
    } else {
        adjustSize();
    }
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

void PropertiesSubWindow::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if (correcting_size_ || parentWidget() == nullptr) return;
    const QSize bounded(
        std::min(width(), parentWidget()->width()),
        std::min(height(), parentWidget()->height()));
    if (bounded != size()) {
        correcting_size_ = true;
        resize(bounded);
        correcting_size_ = false;
    }
    keep_inside_parent();
}

bool PropertiesSubWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == title_bar_) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            const QPoint dialog_local = mapFromGlobal(
                mouse->globalPosition().toPoint());
            if (mouse->button() == Qt::LeftButton &&
                dialog_local.x() > 8 && dialog_local.x() < width() - 8 &&
                dialog_local.y() > 8) {
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
    auto* watched_widget = qobject_cast<QWidget*>(watched);
    const bool inside_dialog = watched_widget == this ||
        (watched_widget != nullptr && isAncestorOf(watched_widget));
    if (isVisible() && inside_dialog &&
        (event->type() == QEvent::MouseMove ||
         event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::MouseButtonRelease)) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        const QPoint local = mapFromGlobal(mouse->globalPosition().toPoint());
        constexpr int margin = 8;
        Qt::Edges edges;
        if (local.x() <= margin) edges |= Qt::LeftEdge;
        if (local.x() >= width() - margin) edges |= Qt::RightEdge;
        if (local.y() <= margin) edges |= Qt::TopEdge;
        if (local.y() >= height() - margin) edges |= Qt::BottomEdge;
        if (event->type() == QEvent::MouseButtonPress &&
            mouse->button() == Qt::LeftButton && edges != Qt::Edges{}) {
            resize_edges_ = edges;
            resize_drag_origin_ = mouse->globalPosition();
            resize_drag_geometry_ = geometry();
            mouse->accept();
            return true;
        }
        if (event->type() == QEvent::MouseMove && resize_edges_ != Qt::Edges{} &&
            mouse->buttons().testFlag(Qt::LeftButton)) {
            const QPoint delta = (mouse->globalPosition() - resize_drag_origin_).toPoint();
            QRect next = resize_drag_geometry_;
            if (resize_edges_.testFlag(Qt::LeftEdge)) next.setLeft(next.left() + delta.x());
            if (resize_edges_.testFlag(Qt::RightEdge)) next.setRight(next.right() + delta.x());
            if (resize_edges_.testFlag(Qt::TopEdge)) next.setTop(next.top() + delta.y());
            if (resize_edges_.testFlag(Qt::BottomEdge)) next.setBottom(next.bottom() + delta.y());
            const QSize minimum = minimumSizeHint().expandedTo(minimumSize());
            if (next.width() < minimum.width()) {
                if (resize_edges_.testFlag(Qt::LeftEdge)) next.setLeft(next.right() - minimum.width() + 1);
                else next.setRight(next.left() + minimum.width() - 1);
            }
            if (next.height() < minimum.height()) {
                if (resize_edges_.testFlag(Qt::TopEdge)) next.setTop(next.bottom() - minimum.height() + 1);
                else next.setBottom(next.top() + minimum.height() - 1);
            }
            if (parentWidget() != nullptr) next = next.intersected(parentWidget()->rect());
            setGeometry(next);
            mouse->accept();
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease &&
            mouse->button() == Qt::LeftButton && resize_edges_ != Qt::Edges{}) {
            resize_edges_ = {};
            mouse->accept();
            return true;
        }
        if (event->type() == QEvent::MouseMove && mouse->buttons() == Qt::NoButton) {
            const bool horizontal = edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge);
            const bool vertical = edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge);
            if (horizontal && vertical) {
                setCursor((edges.testFlag(Qt::LeftEdge) == edges.testFlag(Qt::TopEdge))
                    ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
            } else if (horizontal) setCursor(Qt::SizeHorCursor);
            else if (vertical) setCursor(Qt::SizeVerCursor);
            else unsetCursor();
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
