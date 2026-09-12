#pragma once
#include <zima/document/part_document.hpp>

namespace zima::document {
// The existing source-Sketch to new-Sweep frame conversion. The caller first
// normalizes FRONT/TOP using the existing owned-profile reference helper.
// No reference solving, geometry calculation, or live-document mutation.
void adopt_sweep_sketch_frame(HistoryContainer&, zima::sketcher::Sketch&);
} // namespace zima::document
