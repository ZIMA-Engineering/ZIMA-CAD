#pragma once

#include <QPushButton>

namespace zima::app {

// Highlight the shared Sketch entry point in feature properties.
inline void style_sketch_button(QPushButton* button) {
    button->setMinimumHeight(36);
    button->setStyleSheet(QStringLiteral(
        "QPushButton:enabled {background:#00D1FF;color:#102027;"
        "border:1px solid #008DAA;border-radius:2px;padding:4px;}"
        "QPushButton:enabled:hover {border-color:#102027;}"
        "QPushButton:enabled:pressed {background:#00B9E3;}"
        "QPushButton:enabled:focus {border:2px solid #102027;}"));
}

} // namespace zima::app
