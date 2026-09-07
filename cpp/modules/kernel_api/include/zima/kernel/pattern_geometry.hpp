#pragma once
#include <zima/kernel/mirror_geometry.hpp>
#include <numbers>
namespace zima::kernel {
inline PatternRequest validated_pattern(PatternRequest p) {
    if(p.count<2||p.count>1000)throw std::invalid_argument("Počet výskytů Pole musí být 2 až 1000.");
    for(double v:{p.spacing,p.angle_degrees,p.origin.x,p.origin.y,p.origin.z,p.axis.x,p.axis.y,p.axis.z,p.direction.x,p.direction.y,p.direction.z})
        if(!std::isfinite(v))throw std::invalid_argument("Parametry Pole musí být konečné.");
    p.axis=normalized_mirror_plane({{},p.axis}).normal;p.direction=normalized_mirror_plane({{},p.direction}).normal;
    if(p.circular) {
        if(p.full_circle)p.angle_degrees=360.0/p.count;
        if(std::abs(p.angle_degrees)<1e-9||std::abs(p.angle_degrees)*(p.count-1)>=360-1e-9)
            throw std::invalid_argument("Úhel Pole musí rozlišovat výskyty v jedné otáčce.");
    } else if(std::abs(p.spacing)<1e-9)throw std::invalid_argument("Rozteč Pole nesmí být nulová.");
    return p;
}
inline std::string pattern_copy_id(unsigned index){return "copy-"+std::to_string(index);}
inline Vec3 pattern_vector(Vec3 v,const PatternRequest& p,unsigned index) {
    if(!p.circular)return v;
    const double a=p.angle_degrees*index*std::numbers::pi/180.0,c=std::cos(a),s=std::sin(a);
    const auto n=p.axis;const double d=n.x*v.x+n.y*v.y+n.z*v.z;
    return {v.x*c+(n.y*v.z-n.z*v.y)*s+n.x*d*(1-c),
        v.y*c+(n.z*v.x-n.x*v.z)*s+n.y*d*(1-c),v.z*c+(n.x*v.y-n.y*v.x)*s+n.z*d*(1-c)};
}
inline Vec3 pattern_point(Vec3 v,const PatternRequest& p,unsigned index) {
    if(!p.circular)return {v.x+p.direction.x*p.spacing*index,v.y+p.direction.y*p.spacing*index,v.z+p.direction.z*p.spacing*index};
    v=pattern_vector({v.x-p.origin.x,v.y-p.origin.y,v.z-p.origin.z},p,index);
    return {v.x+p.origin.x,v.y+p.origin.y,v.z+p.origin.z};
}
inline ViewerMesh pattern_copy_mesh(ViewerMesh mesh,PatternRequest p,unsigned index,const std::string& owner={},bool occurrence=false) {
    p=validated_pattern(p);mark_copy_display(mesh,owner);if(index==0||index>=p.count)throw std::invalid_argument("Invalid Pattern occurrence index");
    const auto ref=[&](auto& r){if(!r.valid())return;
        if(occurrence){const auto copy=pattern_copy_id(index);r.instance_path=std::to_string(copy.size())+":"+copy+r.instance_path;}
        else if(!owner.empty()){if(r.semantic_key!="container:display")r.semantic_key="pattern:"+pattern_copy_id(index)+":"+mirror_source_key(r.owner_id,r.semantic_key);r.owner_id=owner;r.instance_path.clear();}};
    const auto face=[&](FaceReference& r){ref(r);if(!r.surface)return;auto surface=std::make_shared<SurfaceGeometry>(*r.surface);
        surface->origin=pattern_point(surface->origin,p,index);surface->axis=pattern_vector(surface->axis,p,index);surface->radial=pattern_vector(surface->radial,p,index);r.surface=std::move(surface);};
    const auto geometry=[&](auto& g){for(auto& v:g.vertices)v=pattern_point(v,p,index);for(auto& r:g.triangle_references)face(r);
        for(auto& e:g.edges){for(auto& v:e.points)v=pattern_point(v,p,index);ref(e.reference);
            if(!owner.empty()&&!occurrence){e.display_owner_id=owner;e.edge_treatment_owner_ids.clear();}
            for(auto& side:e.edge_treatment_side_directions)for(auto& v:side)v=pattern_vector(v,p,index);
            for(auto& r:e.edge_treatment_side_references)face(r);for(auto& r:e.edge_treatment_endpoint_references)ref(r);}
        for(auto& v:g.points){v.position=pattern_point(v.position,p,index);ref(v.reference);}
        for(auto& a:g.axes){a.point=pattern_point(a.point,p,index);a.direction=pattern_vector(a.direction,p,index);ref(a.reference);}};
    geometry(mesh);geometry(mesh.original_references);mesh.dimensions.clear();mesh.constraint_markers.clear();return mesh;
}
} // namespace zima::kernel
