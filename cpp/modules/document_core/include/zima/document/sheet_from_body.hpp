#pragma once
#include <zima/document/part_document.hpp>
#include <algorithm>

namespace zima::document {
// RIP definitions keep ordinary native Sketch features before their conversion
// in calculation order. Membership owns their editing and Tree lifecycle only
// while the conversion has associated sheet children or is awaiting repair.
inline const HistoryContainer* sheet_from_body_rip_owner(
        const PartDocument& document,const std::string& object) {
    const auto sketch=std::ranges::find_if(document.sketches,[&](const auto& value) {
        return value.id==object||value.owner_container_id==object;
    });
    if(sketch==document.sketches.end())return nullptr;
    const auto* feature=document.find_container(sketch->owner_container_id);
    if(!feature||!(feature->feature_kind==FeatureKind::Sketch||
            (feature->feature_kind==FeatureKind::Feature&&feature->feature.type==FeatureType::Sketch)))return nullptr;
    const auto* body=document.body_owner_for_object(feature->id);
    for(const auto& parent:document.history) {
        if(parent.feature_kind!=FeatureKind::SheetFromBody||document.body_owner_for_object(parent.id)!=body||
                (!(parent.sheet_from_body.child_bindings.empty()&&parent.sheet_from_body.source_face.reference.valid())&&
                 std::ranges::none_of(parent.sheet_from_body.child_bindings,[](const auto& binding){return !binding.detached;})))continue;
        if(std::ranges::find(parent.sheet_from_body.rip_sketches,sketch->id)!=parent.sheet_from_body.rip_sketches.end())return &parent;
    }
    return nullptr;
}
}
