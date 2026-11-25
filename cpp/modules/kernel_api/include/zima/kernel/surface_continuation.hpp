#pragma once
#include <zima/kernel/curved_wrap.hpp>
#include <functional>

namespace zima::kernel {
// Geometric equivalence does not replace a face identity. It only permits
// following an actual shared boundary in one current Body's target domain.
inline bool same_surface_support(const SurfaceGeometry& a,const SurfaceGeometry& b,double tolerance) {
    using namespace wrap_detail;
    if(a.kind!=b.kind)return false;
    const auto axis_a=unit(a.axis),axis_b=unit(b.axis),delta=sub(b.origin,a.origin);
    const double aligned=dot(axis_a,axis_b);
    if(a.kind==SurfaceGeometry::Kind::Plane)
        return dot(scale(axis_a,a.reversed?-1.:1.),scale(axis_b,b.reversed?-1.:1.))>1-1e-12&&
            std::abs(dot(delta,axis_a))<=tolerance;
    if(a.reversed!=b.reversed)return false;
    if(a.kind==SurfaceGeometry::Kind::Sphere)
        return length(delta)<=tolerance&&std::abs(a.radius-b.radius)<=tolerance;
    if(a.kind==SurfaceGeometry::Kind::Cylinder)
        return std::abs(aligned)>1-1e-12&&length(cross(delta,axis_a))<=tolerance&&
            std::abs(a.radius-b.radius)<=tolerance;
    if(a.kind==SurfaceGeometry::Kind::Cone)
        return aligned>1-1e-12&&length(cross(delta,axis_a))<=tolerance&&
            std::abs(a.semi_angle-b.semi_angle)<=1e-12&&
            std::abs(a.radius+dot(delta,axis_a)*std::tan(a.semi_angle)-b.radius)<=tolerance;
    return false;
}

// Stable first-selection order; repeated references do not repeat work.
// The adjacency supplied by each adapter is native/persisted topology, never
// an inference that spatially close faces must be joined.
inline std::vector<std::size_t> surface_continuation_indices(
        const std::vector<FaceReference>& faces,const std::vector<std::pair<std::size_t,std::size_t>>& adjacent,
        const std::vector<FaceReference>& selected,double tolerance) {
    std::vector<std::size_t> result;
    const auto add=[&](std::size_t index) {
        if(std::ranges::find(result,index)==result.end())result.push_back(index);
    };
    for(const auto& seed:selected)for(std::size_t i=0;i<faces.size();++i)if(faces[i]==seed)add(i);
    for(std::size_t at=0;at<result.size();++at) {
        const auto current=result[at];
        const auto& a=faces[current].surface?faces[current].surface:faces[current].contour_surface;
        if(!a)continue;
        for(const auto& [first,second]:adjacent) {
            if(first!=current&&second!=current)continue;
            const auto next=first==current?second:first;
            const auto& b=faces[next].surface?faces[next].surface:faces[next].contour_surface;
            if(b&&same_surface_support(*a,*b,tolerance))add(next);
        }
    }
    return result;
}

inline std::vector<FaceReference> viewer_surface_references(const ViewerMesh& mesh,
        const std::function<bool(const FaceReference&)>& allowed={}) {
    using Key=std::tuple<std::string,std::string,std::string>;
    const auto key=[](const FaceReference& face){return Key{face.owner_id,face.semantic_key,face.instance_path};};
    std::map<Key,std::size_t> index;std::vector<FaceReference> faces;
    for(const auto& face:mesh.triangle_references)
        if((!allowed||allowed(face))&&index.try_emplace(key(face),faces.size()).second)faces.push_back(face);
    // Native display tags omit repeated analytic descriptors. Recover only
    // the exact identity's metadata; keep the current triangles and adjacency.
    // Historical reference triangles never extend the selected target domain.
    for(const auto& source:mesh.original_references.triangle_references) {
        const auto found=index.find(key(source));if(found==index.end())continue;
        auto& face=faces[found->second];
        if(!face.surface&&!face.contour_surface) {
            face.surface=source.surface;face.contour_surface=source.contour_surface;
        }
    }
    return faces;
}

inline std::vector<FaceReference> continued_viewer_surfaces(const ViewerMesh& mesh,
        const std::vector<FaceReference>& selected,double tolerance,
        const std::function<bool(const FaceReference&)>& allowed={}) {
    using Key=std::tuple<std::string,std::string,std::string>;
    const auto key=[](const FaceReference& face){return Key{face.owner_id,face.semantic_key,face.instance_path};};
    const auto faces=viewer_surface_references(mesh,allowed);
    std::map<Key,std::size_t> index;
    for(std::size_t i=0;i<faces.size();++i)index.emplace(key(faces[i]),i);
    std::vector<std::pair<std::size_t,std::size_t>> adjacent;
    std::map<std::pair<std::string,std::string>,std::size_t> groups;
    for(std::size_t i=0;i<faces.size();++i)if(faces[i].continuation_group) {
        const auto [it,inserted]=groups.try_emplace(std::pair{faces[i].instance_path,*faces[i].continuation_group},i);
        if(!inserted)adjacent.emplace_back(it->second,i);
    }
    for(const auto& edge:mesh.edges)if(edge.edge_treatment_side_references.size()==2) {
        const auto a=index.find(key(edge.edge_treatment_side_references[0]));
        const auto b=index.find(key(edge.edge_treatment_side_references[1]));
        if(a!=index.end()&&b!=index.end())adjacent.emplace_back(a->second,b->second);
    }
    auto result=selected;
    for(const auto i:surface_continuation_indices(faces,adjacent,selected,tolerance))
        if(std::ranges::find(result,faces[i])==result.end())result.push_back(faces[i]);
    return result;
}
}
