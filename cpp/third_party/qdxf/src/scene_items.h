/*
  ZIMA-libqdxf
  http://www.zima-engineering.cz/projekty/ZIMA-libqdxf

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SCENE_ITEMS_H
#define SCENE_ITEMS_H

#include <QGraphicsItem>
#include "../libdxfrw/src/libdxfrw.h"
#include <QDebug>
#include <QFontMetricsF>
#include <QPainter>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsTextItem>
#include <QPainterPath>
#include <QTextDocument>
#include <QTransform>


class SceneArc : public QAbstractGraphicsShapeItem
{
public:
    SceneArc(DRW_Arc a)
    {
        arc = a;
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/, QWidget * /* widget */)
    {
        painter->setPen(pen());
        if(arc.isccw == 0)
            qDebug() << "not ccw";

        //qDebug() << "painting arc";
        int startAngle = -arc.staangle*(180*16/M_PI);
        int spanAngle;

        if(arc.endangle > arc.staangle)
            spanAngle = (arc.endangle-arc.staangle)*(180*16/M_PI);
        else
            spanAngle = (2*M_PI-arc.staangle+arc.endangle)*(180*16/M_PI);

        if(spanAngle > 0)
            spanAngle *= -1;

        /*qDebug() << "startangle:" << startAngle/16;
        qDebug() << "spanangle:" << spanAngle/16;

        qDebug() << "dxf: startangle" << arc.staangle*(180/M_PI);
        qDebug() << "dxf: endangle" << arc.endangle*(180/M_PI);*/

        painter->drawArc( QRectF(arc.basePoint.x-arc.radious, arc.basePoint.y-arc.radious, arc.radious*2, arc.radious*2), startAngle, spanAngle);
    }

    QRectF boundingRect() const
    {
        return QRectF(arc.basePoint.x-arc.radious, arc.basePoint.y-arc.radious, arc.radious*2, arc.radious*2);
    }

private:
    DRW_Arc arc;
};

class SceneText : public QGraphicsTextItem
{
public:
    enum HorizontalAlignment {
        AlignLeft,
        AlignCenter,
        AlignRight
    };

    enum VerticalAlignment {
        AlignBaseline,
        AlignTop,
        AlignMiddle,
        AlignBottom
    };

    enum ScaleMode {
        ScaleFromTextHeight,
        ScaleWidthToTarget,
        ScaleUniformToTarget
    };

    SceneText(const QString &text, const QFont &font, const QColor &color,
              double height, double widthScale, double angle,
              const QPointF &anchorPoint,
              HorizontalAlignment horizontalAlignment,
              VerticalAlignment verticalAlignment,
              bool richText = false,
              double maxWidth = 0.0,
              ScaleMode scaleMode = ScaleFromTextHeight,
              double targetWidth = 0.0,
              QGraphicsItem *parent = 0) :
        QGraphicsTextItem(parent)
    {
        QFont scaledFont = font;
        if (scaledFont.family().isEmpty())
            scaledFont.setFamily(QStringLiteral("Arial"));

        scaledFont.setPointSizeF(100.0);
        setFont(scaledFont);
        setDefaultTextColor(color);
        document()->setDocumentMargin(0);
        const QFontMetricsF metrics(scaledFont);
        const double naturalHeight = qMax(1.0, metrics.height());
        const double textHeight = height > 0.0 ? height : 1.0;
        double yScale = textHeight / naturalHeight;
        double xScale = yScale * (widthScale > 0.0 ? widthScale : 1.0);

        if (richText)
            setHtml(text);
        else
            setPlainText(text);

        if (maxWidth > 0.0 && xScale > 0.0)
            setTextWidth(maxWidth / xScale);
        else
        {
            setTextWidth(-1);
            adjustSize();
        }

        const QRectF bounds = boundingRect();
        if (targetWidth > 0.0 && bounds.width() > 0.0)
        {
            const double fittedScale = targetWidth / bounds.width();
            if (scaleMode == ScaleUniformToTarget)
            {
                xScale = fittedScale;
                yScale = fittedScale;
            }
            else if (scaleMode == ScaleWidthToTarget)
            {
                xScale = fittedScale;
            }
        }

        const QPointF localAnchor = anchorFor(bounds, metrics,
                                              horizontalAlignment,
                                              verticalAlignment);

        QTransform itemTransform;
        itemTransform.rotate(angle);
        itemTransform.scale(xScale, -yScale);
        setTransform(itemTransform);
        setPos(anchorPoint - itemTransform.map(localAnchor));
    }

private:
    static QPointF anchorFor(const QRectF &bounds, const QFontMetricsF &metrics,
                             HorizontalAlignment horizontalAlignment,
                             VerticalAlignment verticalAlignment)
    {
        double x = bounds.left();
        if (horizontalAlignment == AlignCenter)
            x = bounds.center().x();
        else if (horizontalAlignment == AlignRight)
            x = bounds.right();

        double y = metrics.ascent();
        if (verticalAlignment == AlignTop)
            y = bounds.top();
        else if (verticalAlignment == AlignMiddle)
            y = bounds.center().y();
        else if (verticalAlignment == AlignBottom)
            y = bounds.bottom();

        return QPointF(x, y);
    }
};

class ScenePlainText : public QGraphicsPathItem
{
public:
    ScenePlainText(const QString &text, const QFont &font, const QColor &color,
                   double height, double widthScale, double angle,
                   const QPointF &anchorPoint,
                   SceneText::HorizontalAlignment horizontalAlignment,
                   SceneText::VerticalAlignment verticalAlignment,
                   SceneText::ScaleMode scaleMode = SceneText::ScaleFromTextHeight,
                   double targetWidth = 0.0,
                   QGraphicsItem *parent = 0) :
        QGraphicsPathItem(parent)
    {
        QFont scaledFont = font;
        if (scaledFont.family().isEmpty())
            scaledFont.setFamily(QStringLiteral("Arial"));

        scaledFont.setPointSizeF(100.0);

        QPainterPath textPath;
        textPath.addText(0.0, 0.0, scaledFont, text);
        setPath(textPath);
        setBrush(color);
        setPen(Qt::NoPen);

        const QRectF bounds = textPath.boundingRect();
        const double naturalHeight = qMax(1.0, bounds.height());
        const double naturalWidth = qMax(1.0, bounds.width());
        const double textHeight = height > 0.0 ? height : 1.0;
        double yScale = textHeight / naturalHeight;
        double xScale = yScale * (widthScale > 0.0 ? widthScale : 1.0);

        if (targetWidth > 0.0)
        {
            const double fittedScale = targetWidth / naturalWidth;
            if (scaleMode == SceneText::ScaleUniformToTarget)
            {
                xScale = fittedScale;
                yScale = fittedScale;
            }
            else if (scaleMode == SceneText::ScaleWidthToTarget)
            {
                xScale = fittedScale;
            }
        }

        const QPointF localAnchor = anchorFor(bounds, horizontalAlignment,
                                              verticalAlignment);
        QTransform itemTransform;
        itemTransform.rotate(angle);
        itemTransform.scale(xScale, -yScale);
        setTransform(itemTransform);
        setPos(anchorPoint - itemTransform.map(localAnchor));
    }

private:
    static QPointF anchorFor(const QRectF &bounds,
                             SceneText::HorizontalAlignment horizontalAlignment,
                             SceneText::VerticalAlignment verticalAlignment)
    {
        double x = bounds.left();
        if (horizontalAlignment == SceneText::AlignCenter)
            x = bounds.center().x();
        else if (horizontalAlignment == SceneText::AlignRight)
            x = bounds.right();

        double y = 0.0;
        if (verticalAlignment == SceneText::AlignTop)
            y = bounds.top();
        else if (verticalAlignment == SceneText::AlignMiddle)
            y = bounds.center().y();
        else if (verticalAlignment == SceneText::AlignBottom)
            y = bounds.bottom();

        return QPointF(x, y);
    }
};

#endif // SCENE_ITEMS_H
