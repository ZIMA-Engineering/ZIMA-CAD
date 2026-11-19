#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <optional>

namespace zima::kernel {

// A sheet attachment datum belongs to the exact, possibly trimmed, native
// edge. Its identity does not contain coordinates, mesh indices or an OCCT
// enumeration. A later length change therefore moves the same midpoint.
[[nodiscard]] inline VertexReference sheet_edge_midpoint_reference(
        const EdgeReference& edge) {
    return {edge.owner_id,"sheet-midpoint:edge:"+
        std::to_string(edge.semantic_key.size())+":"+edge.semantic_key,
        edge.instance_path};
}

[[nodiscard]] inline std::optional<ViewerPoint> sheet_edge_midpoint(
        const ViewerEdge& edge) {
    if(!edge.reference.valid()||sheet_edge_role(edge)!=SheetEdgeRole::Boundary||
       edge.points.size()<2||edge.edge_treatment_endpoint_references.size()!=2)
        return {};
    const auto a=edge.points.front(),b=edge.points.back();
    const Vec3 d{b.x-a.x,b.y-a.y,b.z-a.z};
    const double length=std::hypot(d.x,d.y,d.z);
    if(length<.001)return {};
    const auto straight=[&](const auto& points) {
        return std::ranges::all_of(points,[&](const auto& p) {
            const Vec3 v{p.x-a.x,p.y-a.y,p.z-a.z};
            return std::hypot(v.y*d.z-v.z*d.y,v.z*d.x-v.x*d.z,
                v.x*d.y-v.y*d.x)<=1e-7*length;
        });
    };
    if(!straight(edge.points)||(edge.exact_spline&&
            !straight(edge.exact_spline->poles)))return {};
    ViewerPoint result{{(a.x+b.x)*.5,(a.y+b.y)*.5,(a.z+b.z)*.5},
        sheet_edge_midpoint_reference(edge.reference)};
    result.always_visible=false;
    result.surface_result=edge.surface_result;
    return result;
}

template<class Geometry>
[[nodiscard]] std::optional<ViewerPoint> find_sheet_edge_midpoint(
        const Geometry& geometry,const VertexReference& reference) {
    if(!reference.semantic_key.starts_with("sheet-midpoint:edge:"))return {};
    for(const auto& edge:geometry.edges)
        if(sheet_edge_midpoint_reference(edge.reference)==reference)
            return sheet_edge_midpoint(edge);
    return {};
}

inline void append_sheet_edge_midpoints(ViewerReferenceGeometry& geometry) {
    for(const auto& edge:geometry.edges)if(const auto point=sheet_edge_midpoint(edge))
        if(std::ranges::none_of(geometry.points,[&](const auto& existing) {
                return existing.reference==point->reference;
            }))geometry.points.push_back(*point);
}
}
