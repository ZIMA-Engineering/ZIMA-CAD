#include <zima/document/profile_targets.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/document/file_path.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>

namespace zima::document {
bool ExternalProfileTarget::operator==(const ExternalProfileTarget& other) const {
    return document_id==other.document_id&&source_file==other.source_file&&
        context_assembly_id==other.context_assembly_id&&dependent_instance_path==other.dependent_instance_path&&
        source_instance_path==other.source_instance_path&&source_to_part==other.source_to_part&&source_to_body==other.source_to_body&&
        (source_face.shares_with(other.source_face)||serialize_body_result(source_face,false,false)==serialize_body_result(other.source_face,false,false));
}
nlohmann::json serialize_external_profile_target(const ExternalProfileTarget& value) {
    const auto placements=[](const auto& values) {
        auto rows=nlohmann::json::array();for(const auto& p:values)rows.push_back({
            {"translation",{p.translation.x,p.translation.y,p.translation.z}},
            {"rotation_degrees",{p.rotation_degrees.x,p.rotation_degrees.y,p.rotation_degrees.z}}});return rows;
    };
    return {{"document",value.document_id},{"source_file",path_to_utf8(value.source_file)},{"assembly",value.context_assembly_id},
        {"dependent_instance_path",value.dependent_instance_path},{"source_instance_path",value.source_instance_path},
        {"source_face",serialize_body_result(value.source_face,false,false)},
        {"source_to_part",placements(value.source_to_part)},{"source_to_body",placements(value.source_to_body)}};
}
ExternalProfileTarget load_external_profile_target(const nlohmann::json& value) {
    ExternalProfileTarget result;
    result.document_id=value.at("document");result.source_file=std::filesystem::u8path(value.at("source_file").get<std::string>());result.context_assembly_id=value.at("assembly");
    result.dependent_instance_path=value.at("dependent_instance_path");result.source_instance_path=value.at("source_instance_path");
    if(result.document_id.empty()||result.source_file.empty()||result.context_assembly_id.empty()||
        result.dependent_instance_path.empty()||result.source_instance_path.empty())throw std::invalid_argument("Invalid Feature definition.");
    const auto placements=[](const nlohmann::json& rows) {
        std::vector<kernel::KernelRigidPlacement> values;
        if(!rows.is_array()||rows.size()>1024)throw std::invalid_argument("Invalid Feature definition.");
        const auto vector=[](const auto& v) {
            if(!v.is_array()||v.size()!=3)throw std::invalid_argument("Invalid Feature definition.");
            kernel::Vec3 p{v.at(0),v.at(1),v.at(2)};
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))throw std::invalid_argument("Invalid Feature definition.");return p;
        };
        for(const auto& row:rows)values.push_back({vector(row.at("translation")),vector(row.at("rotation_degrees"))});return values;
    };
    if(value.contains("source_face"))result.source_face=load_body_result(value.at("source_face"));
    if(value.contains("source_to_part"))result.source_to_part=placements(value.at("source_to_part"));
    if(value.contains("source_to_body"))result.source_to_body=placements(value.at("source_to_body"));
    return result;
}
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
std::optional<double> axis_target_distance(kernel::Vec3 origin,kernel::Vec3 ray,
        const ExtrusionParameters::EndTarget& target) {
    const auto sub=[](kernel::Vec3 a,kernel::Vec3 b){return kernel::Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
    const auto dot=[](kernel::Vec3 a,kernel::Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto cross=[](kernel::Vec3 a,kernel::Vec3 b){return kernel::Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    double distance=std::numeric_limits<double>::infinity();
    if(target.kind==EndTargetKind::Point)distance=dot(sub(target.fallback_origin,origin),ray);
    else if(target.kind==EndTargetKind::Plane) {
        const double denominator=dot(target.fallback_normal,ray);
        if(std::abs(denominator)<1e-10)return std::nullopt;
        distance=dot(sub(target.fallback_origin,origin),target.fallback_normal)/denominator;
    } else {
        for(std::size_t j=0;j+2<target.fallback_triangles.size();j+=3) {
            const auto a=target.fallback_triangles[j];
            const auto u=sub(target.fallback_triangles[j+1],a),v=sub(target.fallback_triangles[j+2],a);
            const auto h=cross(ray,v);const double det=dot(u,h);if(std::abs(det)<1e-12)continue;
            const auto delta=sub(origin,a);const double b=dot(delta,h)/det;
            const auto q=cross(delta,u);const double c=dot(ray,q)/det;
            if(b < -1e-9 || c < -1e-9 || b+c > 1+1e-9)continue;
            const double d=dot(v,q)/det;if(d>1e-8)distance=std::min(distance,d);
        }
    }
    if(!std::isfinite(distance)||distance<=1e-8)return std::nullopt;
    return distance;
}
// Axis termination consumes the same persisted target geometry as profiles.
// It is independent of container placement and never invokes the solid kernel.
bool resolve_axis_extents(ConstructionObject& object, const kernel::ViewerReferenceGeometry& geometry) {
    if(object.kind!=ConstructionKind::Axis)return true;
    const auto sub=[](kernel::Vec3 a,kernel::Vec3 b){return kernel::Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
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
        const auto distance=axis_target_distance(object.origin,ray,*target);
        if(!distance)return false;
        end.resolved_length=*distance;
    }
    object.axis_ends=std::move(resolved);return true;
}
} // namespace zima::document
