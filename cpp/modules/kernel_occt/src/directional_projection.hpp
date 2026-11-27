#pragma once

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <BRepTools_History.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <string>
#include <vector>
#include <optional>

namespace zima::kernel::detail {

// Certify the exact native support, including every positive-weight spline
// pole at roundoff precision. Rendering triangles never establish planarity.
std::optional<gp_Pln> exact_planar_support(const TopoDS_Face&,double tolerance);

struct ProjectionCurve {
    TopoDS_Edge edge;
    std::string source_key;
    std::string first_point_key, last_point_key;
};
struct ProjectionTarget {
    TopoDS_Face face;
    std::string target_key;
    std::vector<ProjectionCurve> boundaries;
    // The caller supplies an exact analytic support with its natural domain.
    // This is transient calculation state, never a persisted face identity.
    bool extended_support{};
};
struct ProjectedCurve {
    TopoDS_Edge edge;
    std::string source_key;
    // More than one parent is possible on a shared physical face boundary.
    std::vector<std::string> target_keys;
    // Runtime classification only. Parameters are never persistent identity.
    double source_first{}, source_last{};
    std::string semantic_key, first_point_key, last_point_key;
};
struct ProjectionGap {
    std::string source_key;
    double source_first{}, source_last{};
};
struct DirectionalProjection {
    std::vector<ProjectedCurve> curves;
    std::vector<ProjectionGap> gaps;
    bool ambiguous{};
};

// Explicit native calculation. The returned curves retain their native 3D
// representation and target pcurves. No tessellation participates in modeling.
// Each source interval uses the first nonnegative ray hit over the whole target
// set. With both_directions, the direction must be normal to the Sketch and the
// nearest hit on either side wins. Equal-distance opposite hits are ambiguous.
// Missing/ambiguous intervals remain diagnostics, never fallback geometry.
DirectionalProjection project_directionally(
    const std::vector<ProjectionCurve>& sources,
    const std::vector<ProjectionTarget>& targets,
    const gp_Pln& sketch_plane, const gp_Dir& direction, double tolerance,
    bool both_directions = false);

// Bind the calculated curves to authored parents. Target boundaries carry
// existing native edge identities; numerical parameters locate those parents
// but never form part of a persistent key. Ambiguous naming is an error.
void name_projected_curves(DirectionalProjection&,
    const std::vector<ProjectionCurve>& sources,
    const std::vector<ProjectionTarget>& targets,
    const gp_Pln& sketch_plane, const gp_Dir& direction, double tolerance);

struct ProjectionPartition {
    TopoDS_Shape shape;
    // Actual target descendants; the caller binds native boundary ancestry
    // before publishing any persistent region reference.
    std::vector<ProjectionTarget> regions;
    Handle(BRepTools_History) history;
};
// Certify native 3D/pcurve correspondence after partition or local boundary
// unification, repairing only the parameter curve within the requested budget.
void certify_projection_face(const TopoDS_Face&, double tolerance,
    bool bound_disposable_copy = false);
// A null input partitions only the target surfaces. Otherwise split the actual
// input body in place, preserving all material and unaffected topology.
ProjectionPartition partition_projection(const TopoDS_Shape& input,
    const std::vector<ProjectionTarget>& targets,
    const DirectionalProjection& projection, double tolerance);

}
