#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <TopoDS_Shape.hxx>
#include <BRepTools_History.hxx>

namespace zima::kernel {
// Kernel-only materialization, for explicit geometry calculation/export.
[[nodiscard]] TopoDS_Shape read_calculated_body_shape(const BodyResult& body);

using CalculatedTopologyReference = std::variant<VertexReference,EdgeReference,FaceReference>;

struct CalculatedBodyTopology {
    TopoDS_Shape shape;
    std::vector<std::vector<TopoDS_Shape>> referenced_shapes;
};

// Resolve exact members of a single archived material result for explicit
// downstream calculation. Each output row follows its requested reference.
[[nodiscard]] CalculatedBodyTopology read_calculated_body_topology(
    const BodyResult& body,const std::vector<CalculatedTopologyReference>& references);

struct ProjectedMeshCurve {
    TopoDS_Shape edge,face;
};

// Exact authored geometry at its placed plane, wholly contained in the material.
// Only explicit FEM validation/meshing materializes these private shapes.
[[nodiscard]] std::vector<TopoDS_Shape> planar_calculated_mesh_region(
    const TopoDS_Shape& body,const ProjectionRequest& request);
struct ProjectedMeshRegion {
    TopoDS_Shape body;
    std::vector<TopoDS_Shape> faces;
    std::vector<ProjectedMeshCurve> curves;
    Handle(BRepTools_History) history;
    std::vector<std::string> region_keys;
};

[[nodiscard]] ProjectedMeshRegion project_calculated_mesh_region(
    const TopoDS_Shape& body,const ProjectionRequest& request,
    const std::vector<TopoDS_Shape>& targets,const std::vector<Vec3>& surface_seeds,
    const std::vector<std::string>& expected_region_keys={});
}
