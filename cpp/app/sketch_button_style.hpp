#pragma once

#include <QPushButton>

namespace zima::app {

// Keep the shared entry-button sizing; Qt owns interaction feedback.
inline void style_sketch_button(QPushButton* button) {
    button->setMinimumHeight(36);
}

} // namespace zima::app
