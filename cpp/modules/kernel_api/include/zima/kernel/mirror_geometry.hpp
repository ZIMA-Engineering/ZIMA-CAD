#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <cmath>
#include <charconv>
#include <stdexcept>

namespace zima::kernel {
inline MirrorPlane normalized_mirror_plane(MirrorPlane plane) {
    for(double v:{plane.point.x,plane.point.y,plane.point.z,plane.normal.x,plane.normal.y,plane.normal.z})
        if(!std::isfinite(v))throw std::invalid_argument("Rovina zrcadla musí být konečná.");
    const double length=std::hypot(plane.normal.x,plane.normal.y,plane.normal.z);
    if(length<1e-12)throw std::invalid_argument("Rovina zrcadla nemá normálu.");
    plane.normal={plane.normal.x/length,plane.normal.y/length,plane.normal.z/length};return plane;
}
inline Vec3 mirrored_vector(Vec3 v,const MirrorPlane& plane) {
    const auto n=plane.normal;const double d=2*(v.x*n.x+v.y*n.y+v.z*n.z);
    return {v.x-d*n.x,v.y-d*n.y,v.z-d*n.z};
}
inline Vec3 mirrored_point(Vec3 p,const MirrorPlane& plane) {
    const auto v=mirrored_vector({p.x-plane.point.x,p.y-plane.point.y,p.z-plane.point.z},plane);
    return {v.x+plane.point.x,v.y+plane.point.y,v.z+plane.point.z};
}
// A derived identity stores its exact original owner/key. It is defined from
// ZIMA data before calculation, independently of kernel topology traversal.
inline std::string mirror_source_key(const std::string& owner,const std::string& key) {
    return "mirror:from:"+std::to_string(owner.size())+":"+owner+key;
}
inline std::optional<EdgeReference> mirror_parent_reference(const std::string& key) {
    constexpr std::string_view prefix="mirror:from:";
    if(!key.starts_with(prefix))return std::nullopt;
    const auto end=key.find(':',prefix.size());if(end==std::string::npos)return std::nullopt;
    std::size_t size{};const auto parsed=std::from_chars(key.data()+prefix.size(),key.data()+end,size);
    if(parsed.ec!=std::errc{}||parsed.ptr!=key.data()+end||size>=key.size()-end-1)return std::nullopt;
    return EdgeReference{key.substr(end+1,size),key.substr(end+1+size),{}};
}
// Whole-container display ownership is deliberately not topology ancestry.
// Boolean outputs may have no source face on their display tessellation; the
// marker offers only the container and never a selectable Face reference.
inline void mark_copy_display(ViewerMesh& mesh,const std::string& owner) {
    if(owner.empty())return;
    for(auto& ref:mesh.triangle_references)if(!ref.valid())ref={owner,"container:display",ref.instance_path};
}
inline ViewerMesh mirrored_viewer_mesh(ViewerMesh mesh,MirrorPlane plane,const std::string& owner={}) {
    plane=normalized_mirror_plane(plane);
    const auto point=[&](Vec3& p){p=mirrored_point(p,plane);};
    const auto vector=[&](Vec3& p){p=mirrored_vector(p,plane);};
    const auto reference=[&](auto& ref){if(owner.empty()||!ref.valid())return;
        if(ref.semantic_key!="container:display")ref.semantic_key=mirror_source_key(ref.owner_id,ref.semantic_key);ref.owner_id=owner;ref.instance_path.clear();};
    const auto face=[&](FaceReference& ref){reference(ref);if(!ref.surface)return;
        auto surface=std::make_shared<SurfaceGeometry>(*ref.surface);point(surface->origin);vector(surface->axis);vector(surface->radial);
        ref.surface=std::move(surface);};
    const auto geometry=[&](auto& value){
        for(auto& p:value.vertices)point(p);
        for(std::size_t i=0;i+2<value.triangles.size();i+=3)std::swap(value.triangles[i+1],value.triangles[i+2]);
        for(auto& ref:value.triangle_references)face(ref);
        for(auto& edge:value.edges){for(auto& p:edge.points)point(p);if(edge.exact_spline)for(auto& p:edge.exact_spline->poles)point(p);reference(edge.reference);
            if(!owner.empty()){edge.display_owner_id=owner;edge.edge_treatment_owner_ids.clear();}
            for(auto& row:edge.edge_treatment_side_directions)for(auto& v:row)vector(v);
            for(auto& ref:edge.edge_treatment_side_references)face(ref);
            for(auto& ref:edge.edge_treatment_endpoint_references)reference(ref);}
        for(auto& p:value.points){point(p.position);reference(p.reference);}
        for(auto& axis:value.axes){point(axis.point);vector(axis.direction);reference(axis.reference);}
    };
    geometry(mesh);geometry(mesh.original_references);mark_copy_display(mesh,owner);
    std::map<ObjectEnvelopeKey,ModelEnvelope> frames;
    for(auto [key,frame]:mesh.annotation_frames){point(frame.origin);for(auto& axis:frame.axes)vector(axis);
        if(owner.empty())frames[key]=frame;
        else if(key.first.empty()&&key.second.empty())frames[{owner,{}}]=frame;
    }mesh.annotation_frames=std::move(frames);

    // Source dimensions are inspected at their owner, never duplicated here.
    mesh.dimensions.clear();mesh.constraint_markers.clear();return mesh;
}
} // namespace zima::kernel
