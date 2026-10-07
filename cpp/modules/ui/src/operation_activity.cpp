#include <zima/ui/operation_activity.hpp>

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QWidget>
#include <algorithm>

namespace zima::ui {
namespace {
thread_local int input_blocks{};
class ActivityInputGate final : public QObject {
public:
    explicit ActivityInputGate(QObject* parent) : QObject(parent) {}
protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (input_blocks == 0) return false;
        switch (event->type()) {
        case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick: case QEvent::MouseMove:
        case QEvent::KeyPress: case QEvent::KeyRelease: case QEvent::Wheel:
        case QEvent::Shortcut: case QEvent::ShortcutOverride: return true;
        case QEvent::Close: event->ignore(); return true;
        default: return false;
        }
    }
};
class ActivityIndicator final : public QWidget {
public:
    explicit ActivityIndicator(QWidget* parent, bool compact = false) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(compact ? 0 : 18, compact ? 0 : 14,
                               compact ? 6 : 18, compact ? 0 : 14);
        row->setSpacing(10);
        spinner = new QWidget(this);
        spinner->setFixedSize(compact ? 20 : 26, compact ? 20 : 26);
        spinner->installEventFilter(this);
        row->addWidget(spinner);
        label = new QLabel(QObject::tr("Pracuji…"), this);
        label->setWordWrap(!compact);
        label->setMaximumWidth(compact ? 180 : 320);
        row->addWidget(label);
        if (!compact) {
            setObjectName("operationActivityOverlay");
            setStyleSheet("QWidget#operationActivityOverlay { background:#243448;"
                          "border:1px solid #54718e;border-radius:10px; }"
                          "QLabel { color:#edf5ff;font-weight:600; }");
        }
    }
    int angle{};
    QWidget* spinner{};
    QLabel* label{};
protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (object == spinner && event->type() == QEvent::Paint) {
            QPainter painter(spinner);
            painter.setRenderHint(QPainter::Antialiasing);
            QPen track(QColor("#54718e"), 2.5);
            painter.setPen(track);
            const auto circle = QRectF(spinner->rect()).adjusted(3, 3, -3, -3);
            painter.drawEllipse(circle);
            QPen arc(QColor("#00d1ff"), 2.5, Qt::SolidLine, Qt::RoundCap);
            painter.setPen(arc);
            painter.drawArc(circle, -angle * 16, 105 * 16);
            return true;
        }
        return QWidget::eventFilter(object, event);
    }
};
}

OperationActivity::OperationActivity(QWidget* window, QWidget* view)
    : QObject(window), window_(window), view_(view) {
    setObjectName("operationActivityController");
    overlay_ = new ActivityIndicator(window);
    overlay_->hide();
    appearance_timer_ = new QTimer(this);
    appearance_timer_->setSingleShot(true);
    appearance_timer_->setInterval(250);
    animation_timer_ = new QTimer(this);
    animation_timer_->setInterval(40);
    connect(appearance_timer_, &QTimer::timeout, this, [this] {
        if (!active()) return;
        refresh();
        place_overlay();
        overlay_->show();
        overlay_->raise();
        animation_timer_->start();
        refresh();
    });
    connect(animation_timer_, &QTimer::timeout, this, [this] {
        angle_ = (angle_ + 16) % 360;
        const auto animate = [this](QWidget* widget) {
            if (auto* indicator = dynamic_cast<ActivityIndicator*>(widget)) {
                indicator->angle = angle_;
                indicator->setProperty("activityAngle", angle_);
                indicator->spinner->update();
            }
        };
        animate(overlay_);
        for (const auto& frame : frames_) if (frame.indicator) animate(frame.indicator);
    });
    if (window_) window_->installEventFilter(this);
    if (view_) view_->installEventFilter(this);
}

OperationActivity::~OperationActivity() {
    if (active() && finished) finished();
    delete overlay_;
}

OperationActivity::Token OperationActivity::begin(const QString& message, QWidget* indicator) {
    const bool first = !active();
    const auto token = ++next_token_;
    frames_.push_back({token, message, indicator});
    if (first) {
        angle_ = 0;
        try { if (started) started(); }
        catch (...) { frames_.pop_back(); if (finished) finished(); throw; }
        appearance_timer_->start();
    }
    refresh();
    return token;
}

bool OperationActivity::update_message(Token token, const QString& message) {
    const auto found = std::find_if(frames_.begin(), frames_.end(),
        [token](const auto& frame) { return frame.token == token; });
    if (found == frames_.end()) return false;
    found->message = message;
    refresh();
    return true;
}

void OperationActivity::end(Token token) {
    const auto found = std::find_if(frames_.begin(), frames_.end(),
        [token](const auto& frame) { return frame.token == token; });
    if (found == frames_.end()) return;
    // Retire unfinished children too when their owning operation exits.
    for (auto it = found; it != frames_.end(); ++it) if (it->indicator) it->indicator->hide();
    frames_.erase(found, frames_.end());
    if (!active()) {
        appearance_timer_->stop();
        animation_timer_->stop();
        overlay_->hide();
        if (finished) finished();
    } else refresh();
}

void OperationActivity::refresh() {
    if (!active()) return;
    auto* indicator = static_cast<ActivityIndicator*>(overlay_);
    indicator->label->setText(frames_.back().message);
    for (const auto& frame : frames_) if (frame.indicator)
        frame.indicator->setVisible(overlay_->isVisible());
    if (overlay_->isVisible()) { place_overlay(); overlay_->raise(); }
}

void OperationActivity::place_overlay() {
    if (!window_ || !view_) return;
    overlay_->adjustSize();
    const auto area = QRect(view_->mapTo(window_, QPoint{}), view_->size());
    overlay_->move(area.center() - overlay_->rect().center());
}

QWidget* OperationActivity::overlay() const { return overlay_; }
void OperationActivity::set_view(QWidget* view) {
    if (view_ == view) return;
    if (view_) view_->removeEventFilter(this);
    view_ = view;
    if (view_) view_->installEventFilter(this);
    if (active()) place_overlay();
}

OperationActivity* OperationActivity::find(QWidget* child) {
    for (auto* parent = child; parent; parent = parent->parentWidget())
        for (auto* object : parent->children())
            if (auto* activity = dynamic_cast<OperationActivity*>(object)) return activity;
    return nullptr;
}

QWidget* OperationActivity::create_confirmation_indicator(QWidget* parent) {
    auto* widget = new ActivityIndicator(parent, true);
    widget->setObjectName("operationConfirmationActivity");
    widget->hide();
    return widget;
}

OperationActivity::Scope::Scope(OperationActivity* activity, const QString& message, QWidget* indicator)
    : activity_(activity), token_(activity ? activity->begin(message, indicator) : 0) {}
OperationActivity::Scope::~Scope() { if (activity_) activity_->end(token_); }
OperationActivity::InputBlock::InputBlock() {
    ++input_blocks;
    static QPointer<ActivityInputGate> gate;
    if (qApp) {
        if (!gate) gate = new ActivityInputGate(qApp);
        // Run before numeric-input and dialog filters, which may themselves
        // consume a key and edit pending data before widget event delivery.
        qApp->removeEventFilter(gate);
        qApp->installEventFilter(gate);
    }
}
OperationActivity::InputBlock::~InputBlock() { --input_blocks; }

bool OperationActivity::eventFilter(QObject* watched, QEvent* event) {
    if ((watched == window_ || watched == view_) &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Move) && active()) place_overlay();
    return QObject::eventFilter(watched, event);
}
} // namespace zima::ui
