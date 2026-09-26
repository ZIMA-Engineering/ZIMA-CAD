#pragma once
#include <QPalette>
#include <QColor>

namespace zima::viewer {
struct ViewTheme {
    QColor bottom;
    QColor top;
    QColor foreground;
};
inline ViewTheme view_theme(const QPalette& palette) {
    if (palette.color(QPalette::Window).lightness() < 128)
        return {QColor(23,27,33), QColor(59,70,84), QColor(Qt::white)};
    return {QColor(220,225,230), QColor(246,247,249), QColor(35,40,46)};
}
} // namespace zima::viewer
