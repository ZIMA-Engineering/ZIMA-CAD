#pragma once
#include <zima/sketcher/sketch.hpp>

namespace zima::sketcher {
// Deterministic outlines of the bundled OSIFONT, independent of Qt/system fonts.
// Lengths are millimetres; Bezier chord error is at most min(0.01 mm, 0.1% height).
// Only contours change; failure preserves the input.
void rebuild_text_contours(SketchText&,bool y_up=false);
}
