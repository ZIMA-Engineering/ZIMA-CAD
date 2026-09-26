#pragma once
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QPainter>
#include <QMenu>
#include <QKeyEvent>
#include <QPointer>
#include <QStyleOptionToolButton>
#include <QTimer>
#include <QToolButton>

namespace zima::app {
// One event-driven guard for the last hovered command, not a per-button
// application filter or polling timer. Toolbar replacement / opening a child
// window can deprive a button of Leave or release; the next pointer movement
// must retire that visual state even when the button itself receives no event.
class CommandButtonHoverTracker final : public QObject {
public:
    static CommandButtonHoverTracker& instance() {
        static QPointer<CommandButtonHoverTracker> tracker;
        if (!tracker) tracker = new CommandButtonHoverTracker;
        return *tracker;
    }
    void watch(QToolButton* button) {
        if (button_ != button) reconcile();
        button_ = button;
    }
    void reconcile() {
        if (!button_) return;
        const auto* hit = QApplication::widgetAt(QCursor::pos());
        bool changed = false;
        if (hit != button_ && !(hit && button_->isAncestorOf(hit)) &&
            button_->testAttribute(Qt::WA_UnderMouse)) {
            button_->setAttribute(Qt::WA_UnderMouse, false);
            changed = true;
        }
        if (button_->isDown() && keyboard_button_ != button_ && QApplication::mouseButtons() == Qt::NoButton &&
            !(button_->menu() && button_->menu()->isVisible())) {
            button_->setDown(false);
            changed = true;
        }
        if (changed) button_->update();
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (!key->isAutoRepeat() && (key->key() == Qt::Key_Space ||
                key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
                if (event->type() == QEvent::KeyPress)
                    keyboard_button_ = qobject_cast<QToolButton*>(watched);
                else if (keyboard_button_ == watched) keyboard_button_.clear();
            }
        }
        // Do not consume or alter MouseButtonRelease before QAbstractButton
        // dispatches clicked(). The button's deferred callback covers release.
        if (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove ||
            event->type() == QEvent::Enter || event->type() == QEvent::WindowDeactivate)
            reconcile();
        return false;
    }
private:
    CommandButtonHoverTracker() : QObject(qApp) { qApp->installEventFilter(this); }
    QPointer<QToolButton> button_;
    QPointer<QToolButton> keyboard_button_;
};

// Stylesheets may bypass CE_ToolButtonLabel in a proxy style. Paint the
// command label explicitly while retaining the real toolbar QAction.
class LeftAlignedCommandLabel final : public QObject {
public:
    explicit LeftAlignedCommandLabel(QToolButton* button) : QObject(button), button_(button) {
        button->installEventFilter(this);
        static_cast<void>(CommandButtonHoverTracker::instance());
    }
protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type()==QEvent::Leave || event->type()==QEvent::HoverLeave ||
            event->type()==QEvent::Hide) {
            button_->setAttribute(Qt::WA_UnderMouse, false);
            button_->update();
        }
        if (event->type()==QEvent::MouseButtonRelease || event->type()==QEvent::FocusOut) {
            // Commands can open a dialog or rebuild their toolbar before Qt
            // delivers Leave. Reconcile after the action without polling.
            QTimer::singleShot(0, button_, [button=QPointer<QToolButton>(button_)] {
                if (!button) return;
                CommandButtonHoverTracker::instance().reconcile();
                const auto* hit=QApplication::widgetAt(QCursor::pos());
                if (hit!=button && !(hit && button->isAncestorOf(hit)))
                    button->setAttribute(Qt::WA_UnderMouse, false);
                button->update();
            });
        }
        if (event->type()!=QEvent::Paint) return false;
        QStyleOptionToolButton option;
        option.initFrom(button_);
        const auto* hit=QApplication::widgetAt(QCursor::pos());
        if (hit==button_ || (hit && button_->isAncestorOf(hit)))
            CommandButtonHoverTracker::instance().watch(button_);
        if (hit!=button_ && !(hit && button_->isAncestorOf(hit)))
            option.state &= ~QStyle::State_MouseOver;
        option.iconSize=button_->iconSize();
        option.toolButtonStyle=Qt::ToolButtonTextBesideIcon;
        if (button_->isDown()) option.state|=QStyle::State_Sunken;
        if (button_->isChecked() || button_->property("zimaCommandActive").toBool()) option.state|=QStyle::State_On;
        if (button_->autoRaise()) option.state|=QStyle::State_AutoRaise;
        if (button_->menu()) option.features|=QStyleOptionToolButton::HasMenu;
        QPainter painter(button_);
        button_->style()->drawComplexControl(QStyle::CC_ToolButton,&option,&painter,button_);
        const int extent=button_->iconSize().width();
        const QRect icon_rect(6,(button_->height()-extent)/2,extent,extent);
        const auto icon=button_->icon();
        if (!icon.isNull()) icon.paint(&painter,icon_rect,Qt::AlignCenter,
            button_->isEnabled()?QIcon::Normal:QIcon::Disabled,
            button_->isChecked()?QIcon::On:QIcon::Off);
        const int left=icon.isNull()?6:icon_rect.right()+5;
        auto palette=button_->palette();
        button_->style()->drawItemText(&painter,button_->rect().adjusted(left,0,-16,0),
            Qt::AlignLeft|Qt::AlignVCenter|Qt::TextShowMnemonic,
            palette,button_->isEnabled(),button_->text(),QPalette::ButtonText);
        return true;
    }
private:
    QToolButton* button_;
};

} // namespace zima::app
