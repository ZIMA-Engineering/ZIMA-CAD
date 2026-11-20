#pragma once

#include <BRepTools_History.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <array>
#include <optional>

namespace zima::kernel::detail {
struct SectionedNormalOffset {
    TopoDS_Shape shape;
    Handle(BRepTools_History) history;
};

// A bounded fallback for a sectioned solid with two authored planar caps.
// Its side faces remain exact normal-offset surfaces. Only their intersection
// curves are fitted, and the completed B-Rep must pass the exact native checks.
// Unsupported topology and failed calculations return no result.
std::optional<SectionedNormalOffset> sectioned_normal_offset(
    const TopoDS_Shape& source, const std::array<TopoDS_Face, 2>& caps,
    double distance, const std::array<double, 2>& cap_distances,
    double tolerance);
}
