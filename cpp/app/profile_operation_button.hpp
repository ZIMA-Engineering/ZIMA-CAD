#pragma once
#include <QPushButton>
#include <QPainter>
#include <QEvent>

namespace zima::app {
// Native vector-painted signs remain crisp at the current display scale.
class ProfileOperationButton final : public QPushButton {
public:
    ProfileOperationButton(bool add,const QString& text,QWidget* parent)
        : QPushButton(text,parent),add_(add) {
        connect(this,&QPushButton::toggled,this,[this]{update_icon(underMouse());});
        update_icon(false);
    }
protected:
    bool event(QEvent* event) override {
        const bool handled=QPushButton::event(event);
        if(event->type()==QEvent::Enter)update_icon(true);
        else if(event->type()==QEvent::Leave)update_icon(false);
        else if(event->type()==QEvent::EnabledChange)update_icon(underMouse());
        return handled;
    }
private:
    bool add_;
    void update_icon(bool hovered) {
        const qreal scale=devicePixelRatioF();
        QPixmap pixmap(QSize(18,18)*scale);pixmap.setDevicePixelRatio(scale);pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setPen(Qt::NoPen);
        painter.setBrush(add_?((hovered||isChecked())&&isEnabled()?QColor(Qt::black):QColor("#4DD811")):QColor("#FF0000"));
        painter.drawRect(3,8,12,3);
        if(add_)painter.drawRect(8,3,3,12);
        painter.end();setIcon(QIcon(pixmap));setIconSize({18,18});
    }
};
}
