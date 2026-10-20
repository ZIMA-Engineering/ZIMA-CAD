#pragma once

#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QTextDocument>
#include <zima/viewer/view_theme.hpp>
#include <algorithm>
#include <cmath>
#include <functional>

namespace zima::app {

// Keep the established status channel, with synchronous presentation feedback.
// No polling, viewer packet publication or model evaluation is involved.
class CommandStatusLabel final : public QLabel {
public:
    using QLabel::QLabel;
    std::function<void(const QString&)> message_changed;
    void setText(const QString& message) {
        if (message == text()) return;
        QLabel::setText(message);
        if (message_changed) message_changed(message);
    }
};

// UI-only child of the active canvas: excluded from GL/export geometry and
// transparent to all mouse input. It deliberately paints no background panel.
class CommandGuidance final : public QWidget {
public:
    explicit CommandGuidance(QWidget* view) : QWidget(view) {
        setObjectName("vladimirGuidanceOverlay");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        document_.setDocumentMargin(0);
        set_view(view);
    }
    void set_view(QWidget* view) {
        if (view_ == view) return;
        if (view_ != view) {
            if (view_) view_->removeEventFilter(this);
            view_ = view;
            setParent(view);
            if (view_) view_->installEventFilter(this);
        }
        place();
    }
    void set_guidance_enabled(bool enabled) {
        enabled_ = enabled;
        place();
    }
    void set_message(const QString& message) {
        if (message_ == message) return;
        message_ = message;
        setProperty("guidanceText", message);
        setAccessibleName(QStringLiteral("Vladimír"));
        setAccessibleDescription(message);
        rebuild_text();
    }
    [[nodiscard]] const QString& message() const { return message_; }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == view_ && (event->type() == QEvent::Resize ||
            event->type() == QEvent::Show)) place();
        return QWidget::eventFilter(watched, event);
    }
    void changeEvent(QEvent* event) override {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange)
            rebuild_text();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        document_.drawContents(&painter);
    }
private:
    void rebuild_text() {
        document_.setDefaultFont(font());
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        const auto text_color = zima::viewer::view_theme(palette()).foreground.name();
        document_.setHtml(QStringLiteral("<span style=\"color:%1\"><b>Vladimír:</b></span> "
            "<span style=\"color:%2\">%3</span>")
            .arg(dark ? "#4dd811" : "#28730a", text_color,
                 message_.toHtmlEscaped().replace('\n', QStringLiteral("<br>"))));
        place();
        update();
    }
    void place() {
        if (!view_ || !enabled_ || message_.isEmpty() || view_->width() <= 32 || view_->height() <= 32) {
            hide();
            return;
        }
        const int available = std::max(1, view_->width() - 24);
        document_.setTextWidth(std::min(620, available));
        setGeometry(12, 12, std::min(620, available),
            std::min(view_->height() - 24, static_cast<int>(std::ceil(document_.size().height()))));
        show();
        raise();
    }
    QPointer<QWidget> view_;
    QTextDocument document_;
    QString message_;
    bool enabled_{true};
};

} // namespace zima::app
