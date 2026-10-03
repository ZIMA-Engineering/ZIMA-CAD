#pragma once
#include <zima/document/part_document.hpp>
#include <zima/document/solid_state_reference_view.hpp>

namespace zima::document {
// Read-only native history inspection. A state's evaluation packet becomes
// available only after that row, and only to explicitly dependent placements.
inline HistoryReferenceViews solid_state_reference_views(const PartDocument& document,
        const kernel::BodyResult& calculated,const kernel::ViewerReferenceGeometry& authored,
        const std::string& stop_before={}) {
    HistoryReferenceViews result;
    if(calculated.solid_state_reference_views.empty()&&std::ranges::none_of(document.history,
        [](const auto& feature){return !feature.suppressed&&is_solid_state(feature.feature_kind);}))return result;
    HistoryReferenceView active;
    const auto bind=[&](const std::string& entry,const std::vector<ConstructionReference>& references) {
        if(!stop_before.empty()&&entry!=stop_before)return;
        if(entry!=stop_before&&std::ranges::none_of(references,[&](const auto& ref){return active.owners.contains({ref.owner_id,ref.instance_path});}))return;
        // An editor must also open with a missing stored reference so it can be repaired.
        result.emplace(entry,solid_state_reference_view(authored,active.geometry,active.owners,
            entry==stop_before?std::span<const ConstructionReference>{}:std::span<const ConstructionReference>{references}));
    };
    for(const auto& body_id:document.body_history.order()) {
        const auto* body=document.body_history.find(body_id);if(!body||body->suppressed)continue;
        bind(body_id,body->scope.placement.references);
        if(body_id==stop_before)return result;
        for(const auto& entry:body->entries) {
            if(const auto* feature=document.find_container(entry.id)) {
                if(entry.id==stop_before){bind(entry.id,feature->placement.references);return result;}
                if(feature->suppressed)continue;
                bind(entry.id,feature->placement.references);
                if(is_solid_state(feature->feature_kind)) {
                    const auto found=calculated.solid_state_reference_views.find(entry.id);
                    if(found==calculated.solid_state_reference_views.end()) {
                        if(calculated.calculation_errors.contains(entry.id))continue; // Failed operation has no changed frame.
                        throw std::invalid_argument("History reference view is invalid.");
                    }
                    HistoryReferenceView next;next.geometry=*found->second;
                    const auto owner=[&](const auto& ref){next.owners.insert({ref.owner_id,ref.instance_path});};
                    for(const auto& ref:next.geometry.triangle_references)owner(ref);
                    for(const auto& edge:next.geometry.edges)owner(edge.reference);
                    for(const auto& point:next.geometry.points)owner(point.reference);
                    for(const auto& axis:next.geometry.axes)owner(axis.reference);
                    history_reference_detail::retain(active.geometry,next.owners,false);
                    history_reference_detail::append(active.geometry,next.geometry);
                    active.owners.insert(next.owners.begin(),next.owners.end());
                }
            } else if(const auto* object=document.find_construction(entry.id)) {
                if(entry.id==stop_before){bind(entry.id,object->references);return result;}
                if(!object->suppressed)bind(entry.id,object->references);
            }
        }
    }
    return result;
}

// Cached reference input for an existing editor. Stop before its history row:
// later states may be absent from a rollback packet and must not affect it.
// This is solver/inspection data, never an additional selectable topology owner.
inline kernel::ViewerReferenceGeometry solid_state_editor_reference_geometry(
        const PartDocument& document,const kernel::BodyResult& calculated,
        const std::string& entry,kernel::ViewerReferenceGeometry geometry) {
    if(!document.find_container(entry)&&!document.find_construction(entry)&&!document.body_history.find(entry))return geometry;
    const auto views=solid_state_reference_views(document,calculated,geometry,entry);
    if(const auto view=views.find(entry);view!=views.end()) {
        history_reference_detail::retain(geometry,view->second.owners,false);
        history_reference_detail::append(geometry,view->second.geometry);
    }
    return geometry;
}
}
