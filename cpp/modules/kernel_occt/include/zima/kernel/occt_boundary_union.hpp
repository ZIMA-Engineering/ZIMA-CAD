#pragma once

#include <zima/kernel/occt_boundary_intersection_graph.hpp>

#include <TopoDS_Shape.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace zima::kernel {

// One authored solid participating in an in-memory Add boundary calculation.
// The shape and its faces are derived calculation state, never document data.
struct OcctBoundarySolid {
    TopoDS_Shape shape;
    std::vector<OcctBoundaryFace> faces;
    struct Edge {
        TopoDS_Edge shape;
        std::vector<EdgeReference> references;
        std::size_t history_index{};
    };
    struct Vertex {
        TopoDS_Vertex shape;
        std::vector<VertexReference> references;
        std::size_t history_index{};
    };
    // Optional while the boundary algorithm is evaluated in isolation.
    // Production callers provide the complete authored curve/point ancestry.
    std::vector<Edge> edges;
    std::vector<Vertex> vertices;
};

// One signed, derived material region. Positive regions contribute material;
// negative regions remove it. This is an in-memory calculation input and does
// not change or extend the native document format.
struct OcctBoundaryRegion {
    OcctBoundarySolid boundary;
    bool adds_material{true};
};

struct OcctBoundaryUnionFace {
    TopoDS_Face shape;
    // Oldest contributing reference first. The remaining entries are aliases
    // retained for durable downstream reference resolution.
    std::vector<FaceReference> references;
    std::size_t history_index{};
};

struct OcctBoundaryUnionEdge {
    TopoDS_Edge shape;
    // Empty only for a newly created intersection edge. Existing and split
    // authored edges retain every parent alias here.
    std::vector<EdgeReference> references;
    std::size_t history_index{};
};

struct OcctBoundaryUnionVertex {
    TopoDS_Vertex shape;
    // Empty only for a newly created intersection point.
    std::vector<VertexReference> references;
    std::size_t history_index{};
};

struct OcctBoundaryUnionMetrics {
    std::size_t source_faces{};
    std::size_t intersection_curves{};
    std::size_t split_faces{};
    std::size_t split_patches{};
    std::size_t boundary_curves_skipped{};
    std::size_t retained_patches{};
    std::size_t discarded_internal_patches{};
    std::size_t containment_queries{};
    std::size_t ray_fallbacks{};
    std::size_t sewed_shells{};
    std::size_t free_edges{};
    std::size_t multiple_edges{};
    std::size_t protected_identity_edges{};
    std::size_t unprotected_faces{};
    std::size_t unprotected_edges{};
    double graph_ms{};
    double split_ms{};
    double classify_ms{};
    double sew_ms{};
    double unify_ms{};
};

struct OcctBoundaryUnion {
    TopoDS_Shape shape;
    std::vector<OcctBoundaryUnionFace> faces;
    std::vector<OcctBoundaryUnionEdge> edges;
    std::vector<OcctBoundaryUnionVertex> vertices;
    std::vector<OcctBoundaryCurve> curves;
    OcctBoundaryUnionMetrics metrics;
};

// Canonical direct identities for one completed boundary. Parent aliases in
// OcctBoundaryUnion remain calculation ancestry; these entries provide the
// single unambiguous identity used by a downstream topology binding.
template <typename Shape,typename Reference>
struct OcctBoundaryNamedTopologyEntry {
    Shape shape;
    Reference reference;
    std::size_t history_index{};
};

struct OcctBoundaryNamedTopology {
    std::vector<OcctBoundaryNamedTopologyEntry<TopoDS_Face,FaceReference>>
        faces;
    std::vector<OcctBoundaryNamedTopologyEntry<TopoDS_Edge,EdgeReference>>
        edges;
    std::vector<OcctBoundaryNamedTopologyEntry<TopoDS_Vertex,VertexReference>>
        vertices;
};

struct OcctResolvedBooleanEdge {
    TopoDS_Edge shape;
    EdgeReference reference;
};

// Builds the exact exterior boundary of an Add block from authored solids.
// OCCT supplies surface intersections and local face splitting; ZIMA owns
// parent age, aliases, region classification and same-domain protection.
[[nodiscard]] OcctBoundaryUnion occt_boundary_union(
    std::span<const OcctBoundarySolid> solids,
    double tolerance);

// Builds the boundary of (union of positive regions) minus (union of negative
// regions). OCCT is used only for pairwise surface intersections and local face
// splitting; material-side classification and retained ancestry remain owned
// by ZIMA. Arbitrarily interleaved history semantics are intentionally outside
// this experimental primitive.
[[nodiscard]] OcctBoundaryUnion occt_boundary_composition(
    std::span<const OcctBoundaryRegion> regions,
    double tolerance);

// Names split fragments and new intersections directly from authored parent
// ancestry and graph incidence. It does not replay or depend on intermediate
// OCCT Boolean results. history_owners is indexed like OcctBoundaryFace's
// history_index and supplies ownership for a newly derived identity.
[[nodiscard]] OcctBoundaryNamedTopology occt_boundary_named_topology(
    const OcctBoundaryUnion& boundary,
    std::span<const std::string> history_owners,
    double tolerance);

// Names a signed-composition boundary in a namespace distinct from ordinary
// Add topology. Parent inheritance remains unchanged; only newly split or
// intersecting objects receive composition-owned identities.
[[nodiscard]] OcctBoundaryNamedTopology
occt_boundary_composition_named_topology(
    const OcctBoundaryUnion& boundary,
    std::span<const std::string> history_owners,
    double tolerance);

// Resolves current Add-intersection edges from their persisted parent-face
// identities. The lookup uses only the current authored source faces and the
// current calculated boundary; it never compares against a cached historical
// body. Endpoint support ancestry disambiguates separate intervals of one
// supporting-surface intersection.
[[nodiscard]] std::vector<OcctResolvedBooleanEdge>
occt_resolve_add_intersection_edges(
    const TopoDS_Shape& boundary,
    std::span<const OcctBoundaryFace> source_faces,
    std::span<const EdgeReference> requested,
    double tolerance);

// Resolves a generated Fillet/Chamfer edge from its persisted adjacent-face
// and endpoint-support ancestry. This is the same exact incidence lookup used
// for Add intersections; the selected-edge ancestry remains part of identity
// but is not replaced by geometric proximity during lookup.
[[nodiscard]] std::vector<OcctResolvedBooleanEdge>
occt_resolve_edge_treatment_edges(
    const TopoDS_Shape& boundary,
    std::span<const OcctBoundaryFace> source_faces,
    std::span<const EdgeReference> requested,
    double tolerance);

} // namespace zima::kernel
