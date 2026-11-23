#pragma once
#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/solid_state_calculation.hpp>
#include <zima/kernel/solid_state_history.hpp>
#include <zima/kernel/solid_straightening.hpp>

namespace zima::workspace {
inline std::vector<kernel::HistoryOperation> group_solid_state_cached_operations(
        const document::PartDocument& doc,const std::vector<kernel::BodyResult>* previous,bool recover_errors=true) {
    return document::solid_state_calculation_operations(doc,previous,recover_errors);
}
// Combined profile sides use the ordinary reference solver in an isolated
// history view. This preserves mixed datum/face constraints and limit-driven
// cuts; no source placement or authored reference is rewritten.
inline std::vector<kernel::HistoryOperation> group_solid_state_operations(
        const kernel::OcctKernel& kernel,const document::PartDocument& doc,
        const std::vector<kernel::BodyResult>* previous) {
    auto operations=doc.kernel_operations(false,true);
    const bool grouped=std::ranges::any_of(operations,[](const auto& op) {
        return std::holds_alternative<kernel::FeatureGroupRequest>(op.primitive)&&kernel::solid_state_candidate(op);
    });
    if(!grouped)return document::solid_state_calculation_operations(
        doc,previous,std::move(operations));
    if(!std::ranges::any_of(operations,[](const auto& op) {
        return !op.suppressed&&std::holds_alternative<kernel::SolidStateRequest>(op.primitive);
    }))return operations;
    std::vector<kernel::BodyResult> bootstrap;
    if(!previous||previous->empty()) {
        // State-owned references require their persisted ancestry inventory too.
        bootstrap=kernel.evaluate_history_recovering(operations,{});previous=&bootstrap;
        if(previous->empty())return operations;
    }
    const auto& authored_geometry=previous->back().mesh.original_references;
    const auto& errors=previous->back().calculation_errors;
    std::vector<kernel::HistoryOperation> authored;
    std::vector<kernel::SolidStateChange> changes;
    for(auto& operation:operations) {
        const auto* state=std::get_if<kernel::SolidStateRequest>(&operation.primitive);
        if(!state){authored.push_back(operation);continue;}
        for(auto& change:changes) {
            const auto& prior=*std::ranges::find(operations,change.owner_id,&kernel::HistoryOperation::owner_id);
            change.suppressed=document::solid_state_preparation_suppressed(prior,operation.owner_id,errors);
        }
        changes.push_back({authored.size(),operation.owner_id,operation.body.id,
            state->restore,state->all,operation.suppressed,state->coefficient,state->owners});
        if(operation.suppressed)continue;
        try {
        std::set<std::string> carried;
        if(const auto* body=doc.body_history.find(operation.body.id)) {
            operation.solid_state_face_transfers=document::solid_state_face_transfers(
                doc,*body,operation.owner_id,authored_geometry,operations,true,errors);
            for(const auto& transfer:operation.solid_state_face_transfers)carried.insert(transfer.owner_id);
        }
        const auto targets=kernel::solid_states_before(authored,changes,authored.size());
        std::set<document::HistoryReferenceView::Owner> changed;
        std::vector<kernel::HistoryOperation> replay,replacements;
        std::vector<kernel::BodyResult> calculated;
        auto transient=doc;
        for(const auto& original:authored) {
            if(original.body.id!=operation.body.id)continue;
            const auto* feature=doc.find_container(original.owner_id);
            const auto* object=doc.find_construction(original.owner_id);
            const auto* refs=feature?&feature->placement.references:object?&object->references:nullptr;
            const bool dependent=refs&&std::ranges::any_of(*refs,[&](const auto& ref) {
                return changed.contains({ref.owner_id,ref.instance_path})||changed.contains(document::solid_state_source_owner(ref));
            });
            // A referenced cut on the unchanged end still has a complete
            // placement definition. Resolve it in this replay rather than
            // asking the material cutter-transfer heuristic to move it with
            // an unrelated straightened source.
            const bool referenced_cut=!original.suppressed&&original.operation==kernel::BooleanOperation::Subtract&&refs&&!refs->empty();
            auto candidate=original;
            if(carried.contains(original.owner_id)) {
                // Use the same full end-frame transport as the final state,
                // so later references see the carried input during this pass.
                std::vector<kernel::HistoryOperation> prefix;
                for(const auto& item:operations) {
                    prefix.push_back(item);
                    if(std::holds_alternative<kernel::SolidStateRequest>(item.primitive))
                        prefix.back().suppressed=document::solid_state_preparation_suppressed(item,operation.owner_id,errors);
                    if(item.owner_id==original.owner_id)break;
                }
                auto boundary=operation;
                std::erase_if(boundary.solid_state_face_transfers,[&](const auto& transfer) {
                    return std::ranges::none_of(prefix,[&](const auto& item){return item.owner_id==transfer.owner_id;});
                });
                if(!replacements.empty())boundary.solid_state_placements=
                    std::make_shared<const std::vector<kernel::HistoryOperation>>(replacements);
                prefix.push_back(std::move(boundary));
                candidate=kernel.prepare_solid_state_replay(prefix).back();
            } else if((dependent||referenced_cut)&&!calculated.empty()) {
                auto current=document::solid_state_alias_geometry(authored_geometry,calculated.back().mesh.original_references,changed);
                document::HistoryReferenceViews views{{original.owner_id,
                    document::solid_state_reference_view(authored_geometry,current.geometry,current.owners,*refs)}};
                transient.resolve_constructions(authored_geometry,views);
                const auto resolved=transient.kernel_operations(false,false);
                const auto found=std::ranges::find(resolved,original.owner_id,&kernel::HistoryOperation::owner_id);
                if(found==resolved.end())throw std::invalid_argument("History reference view is invalid.");
                candidate=*found;
            }
            if(dependent||referenced_cut) {
                if(!carried.contains(original.owner_id))replacements.push_back(candidate);
                changed.insert({original.owner_id,{}});
                if(feature)changed.insert({feature->container_origin.id,{}});
            }
            if(const auto target=targets.find(original.owner_id);target!=targets.end()&&target->second.straight&&!carried.contains(original.owner_id)) {
                candidate.primitive=std::visit([&](const auto& input)->kernel::PrimitiveRequest {
                    using T=std::decay_t<decltype(input)>;
                    if constexpr(std::is_same_v<T,kernel::RevolutionRequest>||std::is_same_v<T,kernel::Sweep3DRequest>||
                                 std::is_same_v<T,kernel::FeatureGroupRequest>)
                        return kernel.prepare_straightening(input,target->second.coefficient).primitive;
                    else throw std::invalid_argument("Solid state replay cannot yet preserve this operation.");
                },candidate.primitive);
                changed.insert({original.owner_id,{}});
            }
            if(targets.contains(original.owner_id))changed.insert({original.owner_id,{}});
            replay.push_back(std::move(candidate));
            calculated=kernel.evaluate_history_incremental(replay,calculated);
        }
        if(!replacements.empty())operation.solid_state_placements=
            std::make_shared<const std::vector<kernel::HistoryOperation>>(std::move(replacements));
        } catch(const std::exception& error) {
            // Preparation failures belong to this history container just like
            // kernel failures. The ordinary recovering calculation preserves
            // its input and lets unchanged second OK retain the definition.
            operation.input_error=error.what();
            operation.solid_state_face_transfers.clear();operation.solid_state_placements.reset();
        }
    }
    return operations;
}
}
