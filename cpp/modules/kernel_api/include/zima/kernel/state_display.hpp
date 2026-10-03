#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <zima/kernel/solid_state_ancestry.hpp>

namespace zima::kernel {
// State topology remains owned by the state feature. Ordinary selection and
// recolouring follow its persisted ancestry to the authored container instead.
// This also applies when displaying an already calculated native packet.
inline void associate_solid_state_display(ViewerMesh& mesh) {
    std::map<std::string_view,std::string> owners;
    const auto owner=[&](const auto& reference) -> std::string {
        if(!reference.semantic_key.starts_with("solid-state:parent:"))return {};
        if(const auto found=owners.find(reference.semantic_key);found!=owners.end())return found->second;
        std::string result,key=reference.semantic_key;
        while(const auto parent=solid_state_parent(key)) {
            result=parent->first;key=parent->second;
        }
        owners.emplace(reference.semantic_key,result);return result;
    };
    for(auto& face:mesh.triangle_references)
        if(auto source=owner(face);!source.empty())face.display_owner_id=std::move(source);
    for(auto& edge:mesh.edges)
        if(auto source=owner(edge.reference);!source.empty())edge.display_owner_id=std::move(source);
    for(auto& point:mesh.points)
        if(auto source=owner(point.reference);!source.empty())point.display_owner_id=std::move(source);
}
} // namespace zima::kernel
