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

enum class EdgeFilletPathKind { Linear, Circular };

// TopoDS-independent exact definition of one constant-radius ZIMA Fillet
// domain. This is derived in-memory calculation data, not native-file
// persistence. A circular sector is swept by translation or exact revolution.
struct PlanarFilletAnalyticRemovalDomain {
    Vec3 source_start,source_end;
    Vec3 axis_direction;
    Vec3 first_inward_direction,second_inward_direction;
    Vec3 first_tangent_at_start,second_tangent_at_start;
    Vec3 center_at_start;
    double radius{};
    double axial_extension{};
    EdgeFilletPathKind path_kind{EdgeFilletPathKind::Linear};
    Vec3 revolution_axis_point,revolution_axis_direction;
    double path_radius{};
    double sweep_angle_radians{};
};

enum class OcctPlanarEdgeFilletEndMode {
    Perpendicular,
    AutomaticBodyEnvelope
};

// Exact builder for straight plane/plane edges. It constructs the circular
// treatment domains directly, batches consecutive edges with the same
// material sense and applies mixed outer/inner edges in authored order.
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

// Faces are already resolved against the current input Body. A null face is
// the explicit perpendicular end. Non-null faces must be planar and contain
// the matching curve-parameter endpoint of the selected edge.
struct OcctPlanarEdgeEndFaces {
    TopoDS_Face first;
    TopoDS_Face last;
};

[[nodiscard]] OcctPlanarEdgeFilletResult occt_planar_edge_fillet(
    const TopoDS_Shape& input,
    std::span<const TopoDS_Edge> edges,
    double radius,
    double tolerance,
    std::span<const OcctPlanarEdgeEndFaces> end_faces = {},
    OcctPlanarEdgeFilletEndMode end_mode =
        OcctPlanarEdgeFilletEndMode::Perpendicular,
    std::span<const std::size_t> route_edge_counts = {});

} // namespace zima::kernel
