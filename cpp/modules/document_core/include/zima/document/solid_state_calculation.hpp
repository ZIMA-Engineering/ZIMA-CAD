#pragma once
#include <zima/document/part_document.hpp>
#include <zima/document/solid_state_reference_view.hpp>
#include <zima/document/solid_state_face_transfers.hpp>

namespace zima::document {
// Resolve a state's earlier dependencies in an isolated document. The public
// history keeps the frames valid at their original boundaries; only the state's
// kernel replay receives these target-frame primitives.
inline std::vector<kernel::HistoryOperation> solid_state_calculation_operations(
        const document::PartDocument& document,const std::vector<kernel::BodyResult>* calculated,bool recover_errors=true) {
    auto operations=document.kernel_operations(false,recover_errors);
    if(!calculated||calculated->empty()||calculated->back().solid_state_reference_views.empty())return operations;
    const auto authored=calculated->back().mesh.original_references;
    for(auto& state:operations) {
        if(state.suppressed||!std::holds_alternative<kernel::SolidStateRequest>(state.primitive))continue;
        const auto* body=document.body_history.find(state.body.id);
        if(!body)continue;
        state.solid_state_face_transfers=solid_state_face_transfers(document,*body,state.owner_id,authored,operations);
        std::set<std::string> carried;
        for(const auto& transfer:state.solid_state_face_transfers)carried.insert(transfer.owner_id);
        const auto packet=calculated->back().solid_state_reference_views.find(state.owner_id);
        if(packet==calculated->back().solid_state_reference_views.end())continue;
        std::set<document::HistoryReferenceView::Owner> owners;
        const auto own=[&](const auto& ref){owners.insert({ref.owner_id,ref.instance_path});};
        for(const auto& ref:packet->second->triangle_references)own(ref);
        for(const auto& item:packet->second->edges)own(item.reference);
        for(const auto& item:packet->second->points)own(item.reference);
        for(const auto& item:packet->second->axes)own(item.reference);
        document::HistoryReferenceViews views;
        std::set<std::string> prefix;
        for(const auto& entry:body->entries) {
            if(entry.id==state.owner_id)break;
            prefix.insert(entry.id);
            // Material-carried joins keep their authored placement. Do not
            // also resolve the same descendant through a target reference view.
            if(carried.contains(entry.id))continue;
            const std::vector<document::ConstructionReference>* refs=nullptr;
            if(const auto* feature=document.find_container(entry.id);feature&&!feature->suppressed)
                refs=&feature->placement.references;
            else if(const auto* object=document.find_construction(entry.id);object&&!object->suppressed)
                refs=&object->references;
            if(refs&&std::ranges::any_of(*refs,[&](const auto& ref){return owners.contains({ref.owner_id,ref.instance_path});}))
                views.emplace(entry.id,document::solid_state_reference_view(authored,*packet->second,owners,*refs));
        }
        if(views.empty())continue;
        auto transient=document;
        transient.resolve_constructions(authored,views);
        const auto resolved=transient.kernel_operations(false,recover_errors);
        std::vector<kernel::HistoryOperation> replacements;
        for(auto candidate:resolved) {
            if(!prefix.contains(candidate.owner_id)||carried.contains(candidate.owner_id)||candidate.suppressed||
               std::holds_alternative<kernel::SolidStateRequest>(candidate.primitive))continue;
            const auto original=std::ranges::find_if(operations,[&](const auto& item){return item.owner_id==candidate.owner_id;});
            if(original==operations.end())throw std::invalid_argument("History reference view is invalid.");
            if(kernel::history_fingerprint({candidate},1)!=kernel::history_fingerprint({*original},1)) {
                candidate.body={};
                replacements.push_back(std::move(candidate));
            }
        }
        if(!replacements.empty())state.solid_state_placements=
            std::make_shared<const std::vector<kernel::HistoryOperation>>(std::move(replacements));
    }
    return operations;
}


} // namespace zima::document
