#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <set>
namespace zima::kernel {
inline constexpr const char* surface_result_color = "#F2D34F";
inline bool has_surface_results(const ViewerMesh& mesh) {
    const auto contains=[](const auto& geometry) {
        return std::ranges::any_of(geometry.triangle_references,[](const auto& r){return r.surface_result;}) ||
            std::ranges::any_of(geometry.edges,[](const auto& e){return e.surface_result;}) ||
            std::ranges::any_of(geometry.points,[](const auto& p){return p.surface_result;});
    };
    return contains(mesh)||contains(mesh.original_references);
}
// Filter calculated viewer data only. Hidden surfaces also leave the common picker.
inline void hide_surface_results(ViewerMesh& mesh) {
    if(!has_surface_results(mesh))return;
    std::set<ObjectEnvelopeKey> owners;
    for(const auto& r:mesh.original_references.triangle_references)
        if(r.surface_result)owners.emplace(r.owner_id,r.instance_path);
    const auto filter=[&](auto& geometry) {
        std::vector<std::uint32_t> triangles;
        std::vector<FaceReference> references;
        for(std::size_t i=0;i<geometry.triangles.size()/3;++i) {
            const auto r=i<geometry.triangle_references.size()?geometry.triangle_references[i]:FaceReference{};
            if(r.surface_result)continue;
            references.push_back(r);
            triangles.insert(triangles.end(),geometry.triangles.begin()+3*i,geometry.triangles.begin()+3*i+3);
        }
        std::vector<Vec3> vertices;std::map<std::uint32_t,std::uint32_t> indices;
        for(auto& index:triangles) {
            auto [it,inserted]=indices.emplace(index,static_cast<std::uint32_t>(vertices.size()));
            if(inserted)vertices.push_back(geometry.vertices.at(index));
            index=it->second;
        }
        geometry.vertices=std::move(vertices);geometry.triangles=std::move(triangles);geometry.triangle_references=std::move(references);
        std::erase_if(geometry.edges,[](const auto& e){return e.surface_result;});
        std::erase_if(geometry.points,[](const auto& p){return p.surface_result;});
        std::erase_if(geometry.axes,[&](const auto& a){return owners.contains({a.reference.owner_id,a.reference.instance_path});});
    };
    filter(mesh);filter(mesh.original_references);
    std::erase_if(mesh.dimensions,[&](const auto& d){return owners.contains({d.reference.owner_id,d.reference.instance_path});});
}
}
