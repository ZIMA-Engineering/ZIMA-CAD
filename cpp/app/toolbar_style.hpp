#pragma once

#include <QString>

namespace zima::app {

// All toolbars use native separator actions with this shared appearance.
// Dimensions are logical pixels: a 1 px line, 4 px end insets and equal
// 4 px gaps on either side. Qt adapts the orientation and display scaling.
inline QString toolbar_separator_style() {
    return QStringLiteral(
        "QToolBar::separator { background:#00D1FF; border:none; }"
        "QToolBar::separator:horizontal { width:1px; min-width:1px; max-width:1px; margin:4px; }"
        "QToolBar::separator:vertical { height:1px; min-height:1px; max-height:1px; margin:4px; }");
}

} // namespace zima::app
