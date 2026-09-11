#pragma once
#include <QPushButton>
#include <QPainter>
#include <QPaintEvent>

namespace zima::app {
class TabCloseButton final : public QPushButton {
public:
    explicit TabCloseButton(QWidget* parent) : QPushButton(parent) {}
protected:
    void paintEvent(QPaintEvent* event) override {
        QPushButton::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::RoundCap));
        const QPointF center(width() / 2.0, height() / 2.0);
        constexpr qreal radius = 3.5;
        painter.drawLine(center + QPointF(-radius, -radius), center + QPointF(radius, radius));
        painter.drawLine(center + QPointF(-radius, radius), center + QPointF(radius, -radius));
    }
};
} // namespace zima::app
