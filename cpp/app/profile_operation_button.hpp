#pragma once
#include <QPushButton>
#include "tool_button_style.hpp"
#include <QPainter>
#include <QEvent>

namespace zima::app {
// Native vector-painted signs remain crisp at the current display scale.
class ProfileOperationButton final : public QPushButton {
public:
    ProfileOperationButton(bool add,const QString& text,QWidget* parent)
        : QPushButton(text,parent),add_(add) {
        auto states=command_button_style();
        states.replace("QToolButton","QPushButton");
        states.replace("border-radius:2px","border-radius:6px");
        setStyleSheet(QStringLiteral(
            "QPushButton {border:1px solid palette(mid);border-radius:6px;padding:4px;}"
            "QPushButton:enabled {background:palette(button);color:palette(button-text);}")+states);
        setFixedHeight(44);
        update_icon();
    }
protected:
    bool event(QEvent* event) override {
        const bool handled=QPushButton::event(event);
        if(event->type()==QEvent::DevicePixelRatioChange)update_icon();
        return handled;
    }
private:
    bool add_;
    void update_icon() {
        const qreal scale=devicePixelRatioF();
        QPixmap pixmap(QSize(18,18)*scale);pixmap.setDevicePixelRatio(scale);pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setPen(Qt::NoPen);
        painter.setBrush(add_?QColor("#4DD811"):QColor("#FF0000"));
        painter.drawRect(3,8,12,3);
        if(add_)painter.drawRect(8,3,3,12);
        painter.end();setIcon(QIcon(pixmap));setIconSize({18,18});
    }
};
}
