#pragma once
#include <zima/kernel/appearance.hpp>
#include <zima/kernel/mirror_geometry.hpp>
#include <algorithm>

namespace zima::kernel {
// Copies retain exact source ancestry in their face keys. Resolve appearance
// from that identity without copying authored metadata or calculating geometry.
// An explicit style on the copy takes precedence over source inheritance.
template<class FaceStyle,class OwnerStyle>
const SurfaceStyle* copied_face_style(std::string owner,std::string key,
        FaceStyle face_style,OwnerStyle owner_style) {
    for(;;) {
        if(const auto* style=face_style(owner,key))return style;
        if(const auto* style=owner_style(owner))return style;
        if(key.starts_with("pattern:")) {
            const auto separator=key.find(':',8);
            if(separator==std::string::npos)return nullptr;
            key.erase(0,separator+1);
        }
        const auto parent=mirror_parent_reference(key);
        if(!parent)return nullptr;
        owner=parent->owner_id;key=parent->semantic_key;
    }
}
inline const SurfaceStyle* copied_face_style(const Appearance& appearance,
        const FaceReference& reference) {
    return copied_face_style(reference.owner_id,reference.semantic_key,
        [&](const auto& owner,const auto& key)->const SurfaceStyle* {
            const auto face=owner+"::"+key;
            for(auto group=appearance.groups.rbegin();group!=appearance.groups.rend();++group)
                if(std::ranges::find(group->faces,face)!=group->faces.end())return &group->style;
            return nullptr;
        },[&](const auto& owner)->const SurfaceStyle* {
            const auto found=appearance.owner_bodies.find(owner);
            if(found!=appearance.owner_bodies.end())
                if(const auto body=appearance.bodies.find(found->second);body!=appearance.bodies.end())return &body->second;
            return nullptr;
        });
}
} // namespace zima::kernel
