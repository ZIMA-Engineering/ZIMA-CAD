#pragma once
#include <QColor>
#include <QVector4D>

namespace zima::interaction {
inline const QColor hover{"#4DD811"};
inline const QColor selected{"#00D1FF"};
inline const QColor subtract{"#FF0000"};
inline const QColor surface{"#FFD400"};
inline const QColor construction{"#FF8C00"};
inline const QColor axis{"#AD6E2E"};
inline QVector4D rgba(const QColor& color) {
    return {float(color.redF()),float(color.greenF()),float(color.blueF()),1.F};
}
} // namespace zima::interaction
