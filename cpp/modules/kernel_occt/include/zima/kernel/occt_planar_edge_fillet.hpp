#pragma once

#include <zima/kernel/geometry_kernel.hpp>

#include <BRepTools_History.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <NCollection_List.hxx>

#include <cstddef>
#include <span>
#include <vector>

namespace zima::kernel {

struct OcctPlanarEdgeFilletMetrics {
    std::size_t requested_edges{};
    std::size_t cutter_solids{};
    std::size_t generated_fillet_faces{};
    double preparation_ms{};
    double boolean_ms{};
    double validation_ms{};
};

// TopoDS-independent exact definition of one constant-radius planar Fillet
// removal domain. This is derived in-memory experiment data, not native-file
// persistence. The circular sector at source_start is swept along
// axis_direction beyond both source endpoints by axial_extension.
struct PlanarFilletAnalyticRemovalDomain {
    Vec3 source_start,source_end;
    Vec3 axis_direction;
    Vec3 first_inward_direction,second_inward_direction;
    Vec3 first_tangent_at_start,second_tangent_at_start;
    Vec3 center_at_start;
    double radius{};
    double axial_extension{};
};

// Experimental exact builder for convex, straight plane/plane edges. It
// constructs the circular removal domains directly and applies all of them in
// one Boolean. The ordinary Fillet path and native persistence remain
// unchanged until geometry and identity equivalence are proved.
struct OcctPlanarEdgeFilletResult {
    TopoDS_Shape shape;
    occ::handle<BRepTools_History> history;
    // Exact analytic removal domains are retained for primary-region
    // experiments. They are derived in-memory geometry, never persistence.
    std::vector<TopoDS_Shape> removal_domains;
    std::vector<PlanarFilletAnalyticRemovalDomain> analytic_removal_domains;
    std::vector<std::vector<TopoDS_Face>> fillet_faces;
    std::vector<TopoDS_Edge> source_edges;
    std::vector<NCollection_List<TopoDS_Shape>> generated_from_edges;
    OcctPlanarEdgeFilletMetrics metrics;

    [[nodiscard]] const TopoDS_Shape& Shape() const noexcept {return shape;}
    [[nodiscard]] const NCollection_List<TopoDS_Shape>& Modified(
        const TopoDS_Shape&) const;
    [[nodiscard]] const NCollection_List<TopoDS_Shape>& Generated(
        const TopoDS_Shape&) const;
    [[nodiscard]] bool IsDeleted(const TopoDS_Shape&) const;
};

[[nodiscard]] OcctPlanarEdgeFilletResult occt_planar_edge_fillet(
    const TopoDS_Shape& input,
    std::span<const TopoDS_Edge> edges,
    double radius,
    double tolerance);

} // namespace zima::kernel
