#pragma once

#include <QPushButton>

namespace zima::app {

// Sketch entry is white; hover and active states follow the shared GUI contract.
inline void style_sketch_button(QPushButton* button) {
    button->setStyleSheet(
        "QPushButton{background:#FFFFFF;color:#102027;font-weight:700;"
        "padding:9px 18px;border-radius:4px}"
        "QPushButton:checked,QPushButton[zimaCommandActive=\"true\"]{background:#00D1FF;color:#102027}"
        "QPushButton:hover:enabled,QPushButton:checked:hover:enabled,QPushButton[zimaCommandActive=\"true\"]:hover:enabled{background:#4DD811;color:#102027}"
        "QPushButton:disabled{background:#394139;color:#899189}");
}

} // namespace zima::app
