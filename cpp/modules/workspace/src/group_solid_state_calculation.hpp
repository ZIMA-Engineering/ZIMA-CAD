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
    if(!grouped)return document::solid_state_calculation_operations(doc,previous);
    if(!std::ranges::any_of(operations,[](const auto& op) {
        return !op.suppressed&&std::holds_alternative<kernel::SolidStateRequest>(op.primitive);
    }))return operations;
    std::vector<kernel::BodyResult> bootstrap;
    if(!previous||previous->empty()) {
        auto input=operations;
        std::erase_if(input,[](const auto& op){return std::holds_alternative<kernel::SolidStateRequest>(op.primitive);});
        bootstrap=kernel.evaluate_history_recovering(input,{});previous=&bootstrap;
        if(previous->empty())return operations;
    }
    const auto& authored_geometry=previous->back().mesh.original_references;
    std::vector<kernel::HistoryOperation> authored;
    std::vector<kernel::SolidStateChange> changes;
    for(auto& operation:operations) {
        const auto* state=std::get_if<kernel::SolidStateRequest>(&operation.primitive);
        if(!state){authored.push_back(operation);continue;}
        changes.push_back({authored.size(),operation.owner_id,operation.body.id,
            state->restore,state->all,operation.suppressed,state->coefficient,state->owners});
        if(operation.suppressed)continue;
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
                return changed.contains({ref.owner_id,ref.instance_path});
            });
            auto candidate=original;
            if(dependent&&!calculated.empty()) {
                document::HistoryReferenceViews views{{original.owner_id,
                    document::solid_state_reference_view(authored_geometry,
                        calculated.back().mesh.original_references,changed,*refs)}};
                transient.resolve_constructions(authored_geometry,views);
                const auto resolved=transient.kernel_operations(false,false);
                const auto found=std::ranges::find(resolved,original.owner_id,&kernel::HistoryOperation::owner_id);
                if(found==resolved.end())throw std::invalid_argument("History reference view is invalid.");
                candidate=*found;
            }
            if(dependent) {
                replacements.push_back(candidate);
                changed.insert({original.owner_id,{}});
                if(feature)changed.insert({feature->container_origin.id,{}});
            }
            if(const auto target=targets.find(original.owner_id);target!=targets.end()&&target->second.straight) {
                candidate.primitive=std::visit([&](const auto& input)->kernel::PrimitiveRequest {
                    using T=std::decay_t<decltype(input)>;
                    if constexpr(std::is_same_v<T,kernel::RevolutionRequest>||std::is_same_v<T,kernel::Sweep3DRequest>||
                                 std::is_same_v<T,kernel::FeatureGroupRequest>)
                        return kernel.prepare_straightening(input,target->second.coefficient).primitive;
                    else throw std::invalid_argument("Solid state replay cannot yet preserve this operation.");
                },candidate.primitive);
                changed.insert({original.owner_id,{}});
            }
            replay.push_back(std::move(candidate));
            calculated=kernel.evaluate_history_incremental(replay,calculated);
        }
        if(!replacements.empty())operation.solid_state_placements=
            std::make_shared<const std::vector<kernel::HistoryOperation>>(std::move(replacements));
    }
    return operations;
}
}
