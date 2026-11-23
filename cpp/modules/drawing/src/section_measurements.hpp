#pragma once
#include <zima/drawing/drawing_document.hpp>
namespace zima::drawing::detail {
// Drawing-only supports; never publish generated cut rims as 3D references.
void capture_section_measurements(DrawingView&, const kernel::ViewerMesh&,
    document::SectionResult&, const document::SectionDefinition&);
}
