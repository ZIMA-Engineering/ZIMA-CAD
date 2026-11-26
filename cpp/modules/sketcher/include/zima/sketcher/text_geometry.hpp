#pragma once
#include <zima/sketcher/sketch.hpp>

namespace zima::sketcher {
// Deterministic outlines from the embedded font or bundled OSIFONT.
// The authored font bytes make geometry independent of installed system fonts.
// Lengths are millimetres; Bezier chord error is at most min(0.01 mm, 0.1% height).
// Only contours change; failure preserves the input.
void rebuild_text_contours(SketchText&,bool y_up=false);
// Exact Bezier boundaries for the embedded font. Display polygons are not
// consumed by modeling. The bounded cache compares every actual text input.
[[nodiscard]] std::vector<kernel::BSplineGeometry> exact_text_curves(const SketchText&);
[[nodiscard]] std::string embedded_font_name(const EmbeddedFont&);
}
