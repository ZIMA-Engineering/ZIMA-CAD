#pragma once
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QCoreApplication>
#include <QVariant>
#include <span>
#include <algorithm>
#include <zima/viewer/dimension_presentation.hpp>

namespace zima::viewer {
// This display-only representation never enters a document or dimension value.
// Only generated deviation text is split; user text overrides stay literal.
inline QString dimension_render_text(const kernel::DimensionTextStyle& style, QString text) {
    if(!QCoreApplication::instance() || !QCoreApplication::instance()->property("zimaStackedTolerances").toBool()
        || style.tolerance_mode!="deviations" || !style.text_override.empty())return text;
    const auto upper=QString::fromStdString(kernel::dimension_decimal_text(style.upper_tolerance));
    const auto lower=QString::fromStdString(kernel::dimension_decimal_text(style.lower_tolerance));
    const auto tail=" +"+upper+" /-"+lower;
    if(!text.endsWith(tail))return text;
    text.chop(tail.size());
    const auto deviation=[](QString value,QChar sign) {
        bool ok=false;const auto number=QString(value).replace(',','.').toDouble(&ok);
        if(ok&&number==0)return QStringLiteral("0");
        if(value.startsWith('+')||value.startsWith('-'))return value;
        return sign+value;
    };
    return text+QChar(0x1f)+deviation(upper,'+')+QChar(0x1f)+deviation(lower,'-');
}
inline QString dimension_render_text(const kernel::ViewerDimension& d,QString text) {
    return d.source_text_style?dimension_render_text(*d.source_text_style,std::move(text)):text;
}
struct DimensionTextRun {QString text;QPointF baseline;};
inline std::vector<DimensionTextRun> dimension_text_runs(const QFont& font,const QString& text) {
    const auto parts=text.split(QChar(0x1f));
    if(parts.size()!=3)return {{text,{}}};
    const QFontMetricsF metrics(font);
    const auto decimal=[&](const QString& value){auto index=value.indexOf(',');return metrics.horizontalAdvance(index<0?value:value.left(index));};
    const double left=metrics.horizontalAdvance(parts[0]+" "),align=std::max(decimal(parts[1]),decimal(parts[2]));
    return {{parts[0],{}},{parts[1],{left+align-decimal(parts[1]),-metrics.height()}},{parts[2],{left+align-decimal(parts[2]),0}}};
}
inline double dimension_text_width(const QFont& font,const QString& text) {
    const QFontMetricsF metrics(font);double width=0;
    for(const auto& run:dimension_text_runs(font,text))width=std::max(width,run.baseline.x()+metrics.horizontalAdvance(run.text));
    return width;
}
struct DimensionTextLabel {
    QString text;
    QPointF baseline;
    double angle{};
    QFont font;
    QColor color;
};
inline QRectF dimension_text_box(const QFont& font, const QString& text, double padding) {
    const QFontMetricsF metrics(font);
    QRectF bounds;
    for(const auto& run:dimension_text_runs(font,text))bounds=bounds.united(metrics.tightBoundingRect(run.text).translated(run.baseline));
    bounds.setLeft(std::min(0., bounds.left()));
    bounds.setRight(std::max(dimension_text_width(font,text), bounds.right()));
    return bounds.adjusted(-padding, -padding, padding, padding);
}
// Keep the opaque label background clear of its own dimension stroke,
// including descenders and font rounding at different paper zoom levels.
inline double dimension_text_clearance(const QFont& font, const QString& text,
                                       double padding, double stroke_width, double minimum_gap) {
    return std::max(minimum_gap, dimension_text_box(font,text,padding).bottom()+stroke_width*.5+padding*.5);
}
// Painting, text picking and grip layout must use the same font and clearance.
template<class Project>
DimensionPresentation dimension_text_presentation(const kernel::ViewerDimension& dimension,
    Project project, const QFont& font, const QString& text, double padding,
    double stroke_width, double arrow=10, double minimum_gap=3, bool angular_leaders=false) {
    return dimension_presentation(dimension,project,dimension_text_width(font,text),arrow,
        dimension_text_clearance(font,text,padding,stroke_width,minimum_gap),angular_leaders);
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
        for(const auto& run:dimension_text_runs(label.font,label.text))painter.drawText(run.baseline,run.text);
        painter.restore();
    }
}
} // namespace zima::viewer
