#pragma once
#include <QPainter>
#include <QPushButton>

namespace zima::ui {
// Shared visual treatment only. Each caller retains its own close/remove action.
class CloseButton final : public QPushButton {
public:
    explicit CloseButton(QWidget* parent=nullptr) : QPushButton(parent) {
        setAttribute(Qt::WA_Hover);
        setAutoDefault(false);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        const auto fill=!isEnabled()?palette().color(QPalette::Disabled,QPalette::Mid):
            QColor(isDown()?"#9E2015":underMouse()?"#E81123":"#C42B1C");
        painter.setPen(QPen(isEnabled()?QColor("#A52216"):fill,1));
        painter.setBrush(fill);painter.drawRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5),3,3);
        if(hasFocus()) {
            painter.setPen(QPen(Qt::white,1,Qt::DotLine));painter.setBrush(Qt::NoBrush);
            painter.drawRect(rect().adjusted(3,3,-4,-4));
        }
        painter.setPen(QPen(isEnabled()?QColor(Qt::white):palette().color(QPalette::Disabled,QPalette::ButtonText),
            1.5,Qt::SolidLine,Qt::RoundCap));
        const QPointF center(width()/2.0,height()/2.0);
        constexpr qreal radius=3.5;
        painter.drawLine(center+QPointF(-radius,-radius),center+QPointF(radius,radius));
        painter.drawLine(center+QPointF(-radius,radius),center+QPointF(radius,-radius));
    }
};
}
