#pragma once
#include <zima/document/placement_types.hpp>
#include <zima/kernel/geometry_kernel.hpp>
#include <algorithm>
#include <cmath>

namespace zima::document {
inline const kernel::SurfaceGeometry* placement_surface(
        const ConstructionReference& ref, const kernel::ViewerReferenceGeometry& geometry) {
    const auto found=std::ranges::find_if(geometry.triangle_references,[&](const auto& face) {
        return face.owner_id==ref.owner_id && face.semantic_key==ref.semantic_key &&
            face.instance_path==ref.instance_path && face.surface;
    });
    return found==geometry.triangle_references.end()?nullptr:found->surface.get();
}
inline bool placement_surface_has_axis(const kernel::SurfaceGeometry& surface) {
    return surface.kind==kernel::SurfaceGeometry::Kind::Cylinder ||
        surface.kind==kernel::SurfaceGeometry::Kind::Cone;
}
struct PlacementSurfacePoint { kernel::Vec3 point, normal; };
// Evaluate the exact supporting surface, independently of trimming and mesh
// density. Cone apexes have no unique normal and cannot orient a container.
inline std::optional<PlacementSurfacePoint> project_placement_surface(
        const kernel::SurfaceGeometry& surface, kernel::Vec3 point) {
    using kernel::Vec3;
    const auto dot=[](Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto add=[](Vec3 a,Vec3 b,double t){return Vec3{a.x+t*b.x,a.y+t*b.y,a.z+t*b.z};};
    Vec3 delta{point.x-surface.origin.x,point.y-surface.origin.y,point.z-surface.origin.z};
    const double axial=dot(delta,surface.axis);
    if(surface.kind==kernel::SurfaceGeometry::Kind::Plane) {
        return PlacementSurfacePoint{add(point,surface.axis,-axial),
            add({},surface.axis,surface.reversed?-1:1)};
    }
    if(!placement_surface_has_axis(surface))return {};
    Vec3 radial=add(delta,surface.axis,-axial);
    const double distance=std::sqrt(dot(radial,radial));
    radial=distance>1e-12?add({},radial,1/distance):surface.radial;
    double station=axial,radius=surface.radius;
    Vec3 normal=radial;
    if(surface.kind==kernel::SurfaceGeometry::Kind::Cone) {
        const double slope=std::tan(surface.semi_angle);
        station=(axial+slope*(distance-radius))/(1+slope*slope);
        radius+=station*slope;
        if(radius<=1e-10)return {};
        normal=add(radial,surface.axis,-slope);
        normal=add({},normal,1/std::sqrt(1+slope*slope));
    }
    if(radius<=0)return {};
    if(surface.reversed)normal=add({},normal,-1);
    return PlacementSurfacePoint{add(add(surface.origin,surface.axis,station),radial,radius),normal};
}
} // namespace zima::document
