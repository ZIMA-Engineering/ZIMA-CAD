#pragma once

#include <QPushButton>

namespace zima::app {

// Green consistently denotes entry into the Sketcher, including owned sketches.
inline void style_sketch_button(QPushButton* button) {
    button->setStyleSheet(
        "QPushButton{background:#4DD811;color:#102027;font-weight:700;"
        "padding:9px 18px;border-radius:4px}"
        "QPushButton:hover{background:#65ec2c}"
        "QPushButton:disabled{background:#394139;color:#899189}");
}

} // namespace zima::app
