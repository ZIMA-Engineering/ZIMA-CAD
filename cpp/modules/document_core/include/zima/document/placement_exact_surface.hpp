#pragma once
#include <zima/document/placement_surface.hpp>
#include <zima/kernel/exact_surface.hpp>
#include <limits>
#include <tuple>
#include <unordered_set>

namespace zima::document::placement_exact {
using kernel::Vec3;
inline Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline double square(Vec3 a){return a.x*a.x+a.y*a.y+a.z*a.z;}
inline bool matches(const ConstructionReference& ref,const kernel::FaceReference& face) {
    return face.owner_id==ref.owner_id&&face.semantic_key==ref.semantic_key&&face.instance_path==ref.instance_path;
}
inline bool is_surface(const ConstructionReference& ref,const kernel::ViewerReferenceGeometry& geometry) {
    if(ref.use_axis||placement_surface(ref,geometry))return false;
    return std::ranges::any_of(geometry.triangle_references,[&](const auto& face){
        return matches(ref,face)&&face.exact_surface;
    });
}
// An authored face can have multiple current trimmed patches. Visit each
// immutable mathematical patch once, independently of display triangle count.
template<class F> void visit(const ConstructionReference& ref,
        const kernel::ViewerReferenceGeometry& geometry,F&& function) {
    std::unordered_set<const kernel::ExactSurfaceGeometry*> visited;
    for(const auto& face:geometry.triangle_references)
        if(matches(ref,face)&&face.exact_surface&&visited.insert(face.exact_surface.get()).second)
            function(*face.exact_surface);
}
inline std::optional<PlacementSurfacePoint> project(const ConstructionReference& ref,
        const kernel::ViewerReferenceGeometry& geometry,Vec3 seed) {
    std::optional<PlacementSurfacePoint> best;
    double distance=std::numeric_limits<double>::infinity();
    visit(ref,geometry,[&](const kernel::ExactSurfaceGeometry& patch) {
        const auto solved=kernel::solve_exact_surface_contact(patch,seed,ref.offset);
        for(const auto& contact:solved.contacts) {
            const double d=square(sub(contact.point,seed));
            if(d<distance||(d==distance&&best&&std::tie(contact.normal.x,contact.normal.y,contact.normal.z)<
                    std::tie(best->normal.x,best->normal.y,best->normal.z))) {
                const auto n=contact.normal;
                best=PlacementSurfacePoint{{contact.point.x-ref.offset*n.x,
                    contact.point.y-ref.offset*n.y,contact.point.z-ref.offset*n.z},n};
                distance=d;
            }
        }
    });
    return best;
}
} // namespace zima::document::placement_exact
