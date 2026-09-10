#pragma once
#include <zima/kernel/mirror_geometry.hpp>
#include <numbers>
namespace zima::kernel {
inline std::pair<unsigned,unsigned> pattern_direction_counts(const LinearPatternDirection& d) {
    if (d.local_axis < 0) return {0,0};
    if (d.count < 2 || d.count > 1000 || d.reverse_count > 999)
        throw std::invalid_argument("Počet ve směru Pole musí být 2 až 1000.");
    switch (d.distribution) {
    case PatternDistribution::Forward: return {d.count-1,0};
    case PatternDistribution::Reverse: return {0,d.count-1};
    case PatternDistribution::Both: return {d.count-1,d.reverse_count};
    case PatternDistribution::Symmetric:
        if (d.count % 2 == 0) throw std::invalid_argument("Symetrické Pole potřebuje lichý počet včetně zdroje (3, 5, 7…).");
        return {(d.count-1)/2,(d.count-1)/2};
    }
    throw std::invalid_argument("Neplatný směr Pole.");
}
inline unsigned pattern_instance_count(const PatternRequest& p) {
    if (p.circular) return p.count;
    unsigned total=1;
    for (const auto& d : p.linear) {
        const auto [forward,reverse]=pattern_direction_counts(d);
        const auto count=1+forward+reverse;
        if (count > 1000 || total > 1000/count) throw std::invalid_argument("Pole může mít nejvýše 1000 výskytů celkem.");
        total*=count;
    }
    return total;
}
inline PatternRequest validated_pattern(PatternRequest p) {
    p.count=pattern_instance_count(p);
    if(p.count<2||p.count>1000)throw std::invalid_argument("Vyberte alespoň jeden směr; Pole může mít 2 až 1000 výskytů.");
    for(double v:{p.angle_degrees,p.origin.x,p.origin.y,p.origin.z,p.axis.x,p.axis.y,p.axis.z})
        if(!std::isfinite(v))throw std::invalid_argument("Parametry Pole musí být konečné.");
    p.axis=normalized_mirror_plane({{},p.axis}).normal;
    if(p.circular) {
        if(p.full_circle)p.angle_degrees=360.0/p.count;
        if(std::abs(p.angle_degrees)<1e-9||std::abs(p.angle_degrees)*(p.count-1)>=360-1e-9)
            throw std::invalid_argument("Úhel Pole musí rozlišovat výskyty v jedné otáčce.");
    } else {
        std::array<bool,3> used{};
        for(auto& d:p.linear) {
            if(d.local_axis==-1)continue;
            if(d.local_axis<0||d.local_axis>2||used[d.local_axis])throw std::invalid_argument("Vyberte různé osy X, Y nebo Z počátku Pole.");
            used[d.local_axis]=true;
            if(!std::isfinite(d.spacing)||d.spacing<1e-9)throw std::invalid_argument("Rozteč Pole musí být kladná.");
            d.direction=normalized_mirror_plane({{},d.direction}).normal;
        }
    }
    return p;
}
inline Vec3 pattern_translation(const PatternRequest& p,unsigned index) {
    Vec3 result;
    for(const auto& d:p.linear) {
        const auto [forward,reverse]=pattern_direction_counts(d);
        const auto count=1+forward+reverse,slot=index%count;index/=count;
        const int step=slot<=forward?static_cast<int>(slot):-static_cast<int>(slot-forward);
        result.x+=d.direction.x*d.spacing*step;result.y+=d.direction.y*d.spacing*step;result.z+=d.direction.z*d.spacing*step;
    }
    return result;
}
inline std::string pattern_copy_id(const PatternRequest& p,unsigned index) {
    if(p.circular)return "copy-"+std::to_string(index);
    std::array<int,3> coordinate{};
    for(const auto& d:p.linear){const auto [forward,reverse]=pattern_direction_counts(d);const auto count=1+forward+reverse,slot=index%count;index/=count;
        if(d.local_axis>=0&&d.local_axis<3)coordinate[d.local_axis]=slot<=forward?static_cast<int>(slot):-static_cast<int>(slot-forward);}
    return "copy-x"+std::to_string(coordinate[0])+"-y"+std::to_string(coordinate[1])+"-z"+std::to_string(coordinate[2]);
}
inline Vec3 pattern_vector(Vec3 v,const PatternRequest& p,unsigned index) {
    if(!p.circular)return v;
    const double a=p.angle_degrees*index*std::numbers::pi/180.0,c=std::cos(a),s=std::sin(a);
    const auto n=p.axis;const double d=n.x*v.x+n.y*v.y+n.z*v.z;
    return {v.x*c+(n.y*v.z-n.z*v.y)*s+n.x*d*(1-c),
        v.y*c+(n.z*v.x-n.x*v.z)*s+n.y*d*(1-c),v.z*c+(n.x*v.y-n.y*v.x)*s+n.z*d*(1-c)};
}
inline Vec3 pattern_point(Vec3 v,const PatternRequest& p,unsigned index) {
    if(!p.circular){const auto t=pattern_translation(p,index);return {v.x+t.x,v.y+t.y,v.z+t.z};}
    v=pattern_vector({v.x-p.origin.x,v.y-p.origin.y,v.z-p.origin.z},p,index);
    return {v.x+p.origin.x,v.y+p.origin.y,v.z+p.origin.z};
}
inline ViewerMesh pattern_copy_mesh(ViewerMesh mesh,PatternRequest p,unsigned index,const std::string& owner={},bool occurrence=false) {
    p=validated_pattern(p);mark_copy_display(mesh,owner);if(index==0||index>=p.count)throw std::invalid_argument("Invalid Pattern occurrence index");
    const auto ref=[&](auto& r){if(!r.valid())return;
        if(occurrence){const auto copy=pattern_copy_id(p,index);r.instance_path=std::to_string(copy.size())+":"+copy+r.instance_path;}
        else if(!owner.empty()){if(r.semantic_key!="container:display")r.semantic_key="pattern:"+pattern_copy_id(p,index)+":"+mirror_source_key(r.owner_id,r.semantic_key);r.owner_id=owner;r.instance_path.clear();}};
    const auto face=[&](FaceReference& r){ref(r);if(!r.surface)return;auto surface=std::make_shared<SurfaceGeometry>(*r.surface);
        surface->origin=pattern_point(surface->origin,p,index);surface->axis=pattern_vector(surface->axis,p,index);surface->radial=pattern_vector(surface->radial,p,index);r.surface=std::move(surface);};
    const auto geometry=[&](auto& g){for(auto& v:g.vertices)v=pattern_point(v,p,index);for(auto& r:g.triangle_references)face(r);
        for(auto& e:g.edges){for(auto& v:e.points)v=pattern_point(v,p,index);ref(e.reference);
            if(!owner.empty()&&!occurrence){e.display_owner_id=owner;e.edge_treatment_owner_ids.clear();}
            for(auto& side:e.edge_treatment_side_directions)for(auto& v:side)v=pattern_vector(v,p,index);
            for(auto& r:e.edge_treatment_side_references)face(r);for(auto& r:e.edge_treatment_endpoint_references)ref(r);}
        for(auto& v:g.points){v.position=pattern_point(v.position,p,index);ref(v.reference);}
        for(auto& a:g.axes){a.point=pattern_point(a.point,p,index);a.direction=pattern_vector(a.direction,p,index);ref(a.reference);}};
    std::map<ObjectEnvelopeKey,ModelEnvelope> frames;
    for(auto [key,frame]:mesh.annotation_frames){frame.origin=pattern_point(frame.origin,p,index);for(auto& axis:frame.axes)axis=pattern_vector(axis,p,index);
        if(occurrence){auto copy=pattern_copy_id(p,index);frames[{key.first,std::to_string(copy.size())+":"+copy+key.second}]=frame;}
        else if(owner.empty())frames[key]=frame;
        else if(key.first.empty()&&key.second.empty())frames[{owner,{}}]=frame;
    }mesh.annotation_frames=std::move(frames);
    geometry(mesh);geometry(mesh.original_references);mesh.dimensions.clear();mesh.constraint_markers.clear();return mesh;
}
} // namespace zima::kernel
