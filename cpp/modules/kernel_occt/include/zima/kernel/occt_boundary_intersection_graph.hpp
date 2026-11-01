#pragma once

#include <zima/kernel/geometry_kernel.hpp>

#include <Geom2d_Curve.hxx>
#include <Geom_Curve.hxx>
#include <TopoDS_Face.hxx>

#include <cstddef>
#include <span>
#include <vector>

namespace zima::kernel {

// In-memory OCCT boundary-graph input. This is derived calculation state and
// is deliberately not part of the native document format.
struct OcctBoundaryFace {
    TopoDS_Face shape;
    // Oldest contributing reference first. Aliases remain attached so later
    // topology decisions never replace the oldest coplanar identity.
    std::vector<FaceReference> references;
    std::size_t history_index{};
};

enum class OcctBoundaryCurveKind {
    Transverse,
    Coplanar
};

struct OcctBoundaryCurve {
    occ::handle<Geom_Curve> curve;
    occ::handle<Geom2d_Curve> first_pcurve;
    occ::handle<Geom2d_Curve> second_pcurve;
    double first_parameter{};
    double last_parameter{};
    double tolerance{};
    OcctBoundaryCurveKind kind{OcctBoundaryCurveKind::Transverse};
    TopoDS_Face first_face;
    TopoDS_Face second_face;
    std::vector<FaceReference> first_parents;
    std::vector<FaceReference> second_parents;
    std::size_t first_history_index{};
    std::size_t second_history_index{};
};

struct OcctBoundaryIntersectionMetrics {
    std::size_t possible_pairs{};
    std::size_t candidate_pairs{};
    std::size_t transverse_surface_curves{};
    std::size_t tangent_pairs{};
    std::size_t retained_transverse_intervals{};
    std::size_t retained_coplanar_intervals{};
    double bounds_ms{};
    double intersection_ms{};
    double trim_ms{};
    double coplanar_ms{};
};

struct OcctBoundaryIntersectionGraph {
    std::vector<OcctBoundaryCurve> curves;
    OcctBoundaryIntersectionMetrics metrics;
};

// Intersects two authored face sets without constructing a Boolean result
// body. Transverse supporting-surface intersections are trimmed against both
// face domains. Coplanar overlap boundaries are retained as a distinct case.
[[nodiscard]] OcctBoundaryIntersectionGraph
occt_boundary_intersection_graph(
    std::span<const OcctBoundaryFace> first,
    std::span<const OcctBoundaryFace> second,
    double tolerance);

} // namespace zima::kernel
