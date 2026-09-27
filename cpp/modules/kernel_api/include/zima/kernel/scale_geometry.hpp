#pragma once
#include <zima/kernel/mirror_geometry.hpp>

namespace zima::kernel {
inline void validate_body_scale(double factor,Vec3 center) {
    if(!std::isfinite(factor)||factor<=0||!std::isfinite(center.x)||
        !std::isfinite(center.y)||!std::isfinite(center.z))
        throw std::invalid_argument("Body scale must be positive and its center finite.");
}
inline Vec3 scaled_body_point(Vec3 point,double factor,Vec3 center) {
    return {center.x+factor*(point.x-center.x),center.y+factor*(point.y-center.y),
        center.z+factor*(point.z-center.z)};
}
inline std::string scale_source_key(const std::string& owner,const std::string& key) {
    return "scale:from:"+std::to_string(owner.size())+":"+owner+key;
}
inline std::optional<EdgeReference> scale_parent_reference(const std::string& key) {
    constexpr std::string_view prefix="scale:from:";
    if(!key.starts_with(prefix))return std::nullopt;
    const auto end=key.find(':',prefix.size());if(end==std::string::npos)return std::nullopt;
    std::size_t size{};const auto parsed=std::from_chars(key.data()+prefix.size(),key.data()+end,size);
    if(parsed.ec!=std::errc{}||parsed.ptr!=key.data()+end||size>=key.size()-end-1)return std::nullopt;
    return EdgeReference{key.substr(end+1,size),key.substr(end+1+size),{}};
}
inline ViewerMesh scaled_body_mesh(ViewerMesh mesh,double factor,Vec3 center,const std::string& owner,std::string_view derivation="scale") {
    validate_body_scale(factor,center);
    const auto point=[&](Vec3& p){p=scaled_body_point(p,factor,center);};
    const auto reference=[&](auto& ref){
        if(!ref.valid())return;
        if(ref.semantic_key!="container:display")ref.semantic_key=std::string(derivation)+":from:"+std::to_string(ref.owner_id.size())+":"+ref.owner_id+ref.semantic_key;
        ref.owner_id=owner;ref.instance_path.clear();
    };
    const auto face=[&](FaceReference& ref){
        reference(ref);ref.display_owner_id=owner;
        if(ref.measured_area)*ref.measured_area*=factor*factor;
        ref.sheet_thickness*=factor;
        if(!ref.sheet_owner.empty())ref.sheet_owner=std::string(derivation)+":from:"+std::to_string(ref.sheet_owner.size())+":"+ref.sheet_owner+"sheet";
        if(!ref.surface)return;
        auto surface=std::make_shared<SurfaceGeometry>(*ref.surface);
        point(surface->origin);surface->radius*=factor;
        surface->axial_min*=factor;surface->axial_max*=factor;ref.surface=std::move(surface);
    };
    const auto geometry=[&](auto& value){
        for(auto& p:value.vertices)point(p);
        for(auto& ref:value.triangle_references)face(ref);
        for(auto& edge:value.edges){
            for(auto& p:edge.points)point(p);
            if(edge.exact_spline)for(auto& p:edge.exact_spline->poles)point(p);
            if(edge.measured_length)*edge.measured_length*=factor;
            if(edge.annotation)transform_annotation(*edge.annotation,[&](auto p){point(p);return p;});
            reference(edge.reference);edge.display_owner_id=owner;edge.edge_treatment_owner_ids.clear();
            for(auto& ref:edge.edge_treatment_side_references)face(ref);
            for(auto& ref:edge.edge_treatment_endpoint_references)reference(ref);
        }
        for(auto& p:value.points){point(p.position);reference(p.reference);}
        for(auto& axis:value.axes){point(axis.point);reference(axis.reference);}
    };
    // Positive uniform scaling preserves normals, winding and geometric sides.
    geometry(mesh);geometry(mesh.original_references);mark_copy_display(mesh,owner);
    mesh.annotation_frames.clear();mesh.dimensions.clear();mesh.constraint_markers.clear();
    return mesh;
}
} // namespace zima::kernel
