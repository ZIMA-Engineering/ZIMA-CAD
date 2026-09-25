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
// Axis termination consumes the same persisted target geometry as profiles.
// It is independent of container placement and never invokes the solid kernel.
bool resolve_axis_extents(ConstructionObject& object, const kernel::ViewerReferenceGeometry& geometry) {
    if(object.kind!=ConstructionKind::Axis || object.definition==ConstructionDefinition::CylinderAxis)return true;
    const auto sub=[](kernel::Vec3 a,kernel::Vec3 b){return kernel::Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
    const auto dot=[](kernel::Vec3 a,kernel::Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto cross=[](kernel::Vec3 a,kernel::Vec3 b){return kernel::Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    const auto direction=sub(object.axis_point(1),object.origin);
    auto resolved=object.axis_ends;
    for(std::size_t i=0;i<(object.axis_extent_mode==AxisExtentMode::TwoSides?2u:1u);++i) {
        auto& end=resolved[i];if(!end.up_to)continue;
        if(end.target.owner_id==object.entity_id || end.target.owner_id==object.container_origin.id)return false;
        ExtrusionParameters::EndTarget requested;
        requested.reference={end.target.owner_id,end.target.semantic_key,end.target.instance_path};
        requested.kind=EndTargetKind::Face;
        for(const auto& p:geometry.points)if(p.reference.owner_id==end.target.owner_id &&
            p.reference.semantic_key==end.target.semantic_key && p.reference.instance_path==end.target.instance_path)
            requested.kind=EndTargetKind::Point;
        const auto target=resolve_profile_target(requested,geometry);if(!target)return false;
        const kernel::Vec3 ray{i?-direction.x:direction.x,i?-direction.y:direction.y,i?-direction.z:direction.z};
        double distance=std::numeric_limits<double>::infinity();
        if(target->kind==EndTargetKind::Point)distance=dot(sub(target->fallback_origin,object.origin),ray);
        else if(target->kind==EndTargetKind::Plane) {
            const double denominator=dot(target->fallback_normal,ray);
            if(std::abs(denominator)<1e-10)return false;
            distance=dot(sub(target->fallback_origin,object.origin),target->fallback_normal)/denominator;
        } else {
            // Trimmed non-planar faces use the captured profile-target triangles.
            for(std::size_t j=0;j+2<target->fallback_triangles.size();j+=3) {
                const auto a=target->fallback_triangles[j];
                const auto u=sub(target->fallback_triangles[j+1],a),v=sub(target->fallback_triangles[j+2],a);
                const auto h=cross(ray,v);const double det=dot(u,h);if(std::abs(det)<1e-12)continue;
                const auto delta=sub(object.origin,a);const double b=dot(delta,h)/det;
                const auto q=cross(delta,u);const double c=dot(ray,q)/det;
                if(b < -1e-9 || c < -1e-9 || b+c > 1+1e-9)continue;
                const double d=dot(v,q)/det;if(d>1e-8)distance=std::min(distance,d);
            }
        }
        if(!std::isfinite(distance)||distance<=1e-8)return false;
        end.resolved_length=distance;
    }
    object.axis_ends=std::move(resolved);return true;
}
} // namespace zima::document
