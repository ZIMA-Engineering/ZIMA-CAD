#pragma once

#include <zima/kernel/occt_planar_edge_fillet.hpp>
#include <BRepTools_History.hxx>
#include <NCollection_List.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <span>
#include <vector>

namespace zima::kernel {

// Exact straight plane/plane Chamfer. The profile is swept between square end
// planes; mixed subtractive/additive edges are applied in authored order.
struct OcctPlanarEdgeChamferResult {
    TopoDS_Shape shape;
    occ::handle<BRepTools_History> history;
    std::vector<std::vector<TopoDS_Face>> chamfer_faces;
    std::vector<TopoDS_Edge> source_edges;
    std::vector<NCollection_List<TopoDS_Shape>> generated_from_edges;

    [[nodiscard]] const TopoDS_Shape& Shape() const noexcept {return shape;}
    [[nodiscard]] const NCollection_List<TopoDS_Shape>& Modified(
        const TopoDS_Shape&) const;
    [[nodiscard]] const NCollection_List<TopoDS_Shape>& Generated(
        const TopoDS_Shape&) const;
    [[nodiscard]] bool IsDeleted(const TopoDS_Shape&) const;
};

enum class OcctPlanarEdgeChamferMode {
    EqualDistance,
    TwoDistances,
    DistanceAngle
};

struct OcctPlanarEdgeChamferSelection {
    TopoDS_Edge edge;
    // A x B and A + angle measure A on this exact adjacent face. A null face
    // is intentional only for the symmetric A x A mode.
    TopoDS_Face distance_a_face;
};

[[nodiscard]] OcctPlanarEdgeChamferResult occt_planar_edge_chamfer(
    const TopoDS_Shape& input,
    std::span<const OcctPlanarEdgeChamferSelection> selections,
    OcctPlanarEdgeChamferMode mode,
    double distance_a,
    double distance_b_or_angle,
    double tolerance,
    std::span<const OcctPlanarEdgeEndFaces> end_faces = {});

[[nodiscard]] OcctPlanarEdgeChamferResult occt_planar_edge_chamfer(
    const TopoDS_Shape& input,
    std::span<const TopoDS_Edge> edges,
    double distance,
    double tolerance,
    std::span<const OcctPlanarEdgeEndFaces> end_faces = {});

} // namespace zima::kernel
