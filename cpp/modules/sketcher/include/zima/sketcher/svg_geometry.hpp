#pragma once
#include <zima/sketcher/template_image.hpp>
#include <zima/kernel/geometry_kernel.hpp>
#include <vector>
namespace zima::sketcher {
struct SvgFill { std::string source_key; std::vector<kernel::BSplineGeometry> boundaries; bool even_odd{}; };
// Filled paths remain exact polynomial/rational curves. Unsupported painting
// is rejected explicitly instead of silently changing the imported artwork.
[[nodiscard]] std::vector<SvgFill> exact_svg_fills(const TemplateImage&);
// Call after validating disjoint simple boundaries through the native profile
// path. Removes boundaries that do not separate filled/unfilled nonzero regions.
[[nodiscard]] std::vector<kernel::BSplineGeometry> nonzero_fill_boundaries(const std::vector<kernel::BSplineGeometry>&);
}
