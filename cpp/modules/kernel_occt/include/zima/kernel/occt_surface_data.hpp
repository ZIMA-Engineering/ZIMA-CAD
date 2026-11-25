#pragma once
#include <zima/kernel/exact_surface.hpp>
#include <TopoDS_Face.hxx>

namespace zima::kernel {
// Calculation-only adapter. The document and UI consume the returned native
// mathematical packet, never this adapter or a live OCCT face.
[[nodiscard]] std::shared_ptr<const ExactSurfaceGeometry> capture_exact_surface_geometry(const TopoDS_Face&);
}
