#pragma once
#include <QLineF>
#include <QPointF>
#include <QSizeF>
#include <cmath>

namespace zima::viewer {
// Render an infinite support near the viewport, even when its defining points
// project far outside it after zooming or panning. This is presentation only.
inline QLineF screen_infinite_line(const QPointF& first, const QPointF& second,
                                 const QSizeF& viewport) {
    const QPointF delta = second - first;
    const double length = std::hypot(delta.x(), delta.y());
    if (!std::isfinite(length) || length <= 1.0e-6) return {};
    const QPointF unit = delta / length;
    const QPointF center(viewport.width() * 0.5, viewport.height() * 0.5);
    const QPointF offset = center - first;
    const QPointF anchor = first + unit * (offset.x() * unit.x() + offset.y() * unit.y());
    const double extent = 2.0 * std::hypot(viewport.width(), viewport.height());
    return {anchor - unit * extent, anchor + unit * extent};
}
} // namespace zima::viewer
