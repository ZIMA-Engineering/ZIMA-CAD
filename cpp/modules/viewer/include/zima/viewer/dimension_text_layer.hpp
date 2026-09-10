#pragma once
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <span>
#include <algorithm>

namespace zima::viewer {
struct DimensionTextLabel {
    QString text;
    QPointF baseline;
    double angle{};
    QFont font;
    QColor color;
};
inline QRectF dimension_text_box(const QFont& font, const QString& text, double padding) {
    const QFontMetricsF metrics(font);
    auto bounds = metrics.tightBoundingRect(text);
    bounds.setLeft(std::min(0., bounds.left()));
    bounds.setRight(std::max(metrics.horizontalAdvance(text), bounds.right()));
    return bounds.adjusted(-padding, -padding, padding, padding);
}
// Stable input order is the annotation order: each later label masks earlier
// labels as well as all geometry painted before this final text layer.
template<class PaintBackground>
void paint_dimension_text_layer(QPainter& painter, std::span<const DimensionTextLabel> labels,
                                double padding, PaintBackground background) {
    for(const auto& label:labels) {
        if(label.text.isEmpty())continue;
        QTransform transform;
        transform.translate(label.baseline.x(),label.baseline.y());
        transform.rotate(label.angle);
        QPainterPath mask;
        mask.addRect(dimension_text_box(label.font,label.text,padding));
        painter.save();
        background(painter,transform.map(mask));
        painter.setTransform(transform,true);
        painter.setFont(label.font);
        painter.setPen(label.color);
        painter.drawText(QPointF{},label.text);
        painter.restore();
    }
}
} // namespace zima::viewer
