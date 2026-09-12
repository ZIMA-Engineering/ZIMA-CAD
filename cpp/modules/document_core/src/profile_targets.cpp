#include <zima/document/profile_targets.hpp>
#include <cmath>
#include <stdexcept>

namespace zima::document {
bool profile_target_is_datum(const kernel::FaceReference& reference) {
    return reference.semantic_key=="plane" || reference.semantic_key.starts_with("origin:plane:");
}
std::optional<ExtrusionParameters::EndTarget> resolve_profile_target(
    const ExtrusionParameters::EndTarget& requested,const kernel::ViewerReferenceGeometry& source) {
    auto result=requested;result.fallback_triangles.clear();
    const auto matches=[&](const auto& ref){return ref.owner_id==requested.reference.owner_id &&
        ref.semantic_key==requested.reference.semantic_key && ref.instance_path==requested.reference.instance_path;};
    if(requested.kind==EndTargetKind::Point) {
        for(const auto& point:source.points)if(matches(point.reference)) {
            result.fallback_origin=point.position;return result;
        }
        return std::nullopt;
    }
    bool analytic_curved=false,unknown_surface=false;
    for(std::size_t i=0;i<source.triangle_references.size();++i) {
        const auto& ref=source.triangle_references[i];if(!matches(ref))continue;
        analytic_curved=analytic_curved || (ref.surface && ref.surface->kind!=kernel::SurfaceGeometry::Kind::Plane);
        unknown_surface=unknown_surface || !ref.surface;
        if(i*3+2>=source.triangles.size())throw std::runtime_error("The persisted target face geometry is incomplete.");
        for(std::size_t j=0;j<3;++j) {
            const auto index=source.triangles[i*3+j];
            if(index>=source.vertices.size())throw std::runtime_error("The persisted target face geometry is incomplete.");
            const auto point=source.vertices[index];
            if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z))throw std::runtime_error("The persisted target face geometry is invalid.");
            result.fallback_triangles.push_back(point);
        }
    }
    if(result.fallback_triangles.empty())return std::nullopt;
    result.kind=EndTargetKind::Face;
    bool have_plane=false;
    for(std::size_t i=0;i<result.fallback_triangles.size();i+=3) {
        const auto a=result.fallback_triangles[i],b=result.fallback_triangles[i+1],c=result.fallback_triangles[i+2];
        const kernel::Vec3 u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z};
        kernel::Vec3 n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
        const double length=std::hypot(n.x,n.y,n.z);if(length<=1e-12)continue;
        result.fallback_origin=a;result.fallback_normal={n.x/length,n.y/length,n.z/length};have_plane=true;break;
    }
    if(!have_plane)throw std::runtime_error("The target face has no usable geometry.");
    // A coarse rendering of a curved face may contain only coplanar vertices.
    // Only persisted plane metadata or a native datum establishes planarity.
    bool planar=!analytic_curved && (!unknown_surface || profile_target_is_datum(requested.reference));
    const auto p=result.fallback_origin,n=result.fallback_normal;
    for(const auto v:result.fallback_triangles)if(std::abs((v.x-p.x)*n.x+(v.y-p.y)*n.y+(v.z-p.z)*n.z)>1e-6){planar=false;break;}
    if(planar){result.kind=EndTargetKind::Plane;result.fallback_triangles.clear();}
    return result;
}
} // namespace zima::document
