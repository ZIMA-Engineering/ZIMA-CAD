#include <zima/workspace/solid_state_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/document/metadata.hpp>
#include <algorithm>
#include <map>
#include <set>

namespace zima::workspace {
std::vector<std::string> solid_state_sources(const document::PartDocument& doc,
        bool restore,const std::string& edited) {
    const auto* body=edited.empty()?doc.body_history.find(doc.body_history.active_body_id()):doc.body_owner_for_object(edited);
    if(!body)return {};
    const auto limit=edited.empty()?body->cursor:doc.body_history.rollback_before(edited).entry_count;
    std::map<std::string,bool> sources;
    std::vector<std::string> ordered;
    for(std::size_t i=0;i<std::min(limit,body->entries.size());++i) {
        const auto* feature=doc.find_container(body->entries[i].id);
        if(!feature||feature->suppressed)continue;
        if(document::is_solid_state(feature->feature_kind)) {
            const auto& state=feature->solid_state;
            for(auto& [owner,straight]:sources)
                if(state.all||std::ranges::find(state.owners,owner)!=state.owners.end())
                    straight=feature->feature_kind==document::FeatureKind::Straighten;
        } else if(feature->combine_mode==document::CombineMode::Add&&!feature->is_surface_result()&&
                ((feature->feature_kind==document::FeatureKind::Revolution&&!feature->revolution.sheet_metal)||
                 feature->feature_kind==document::FeatureKind::Sweep2D||feature->feature_kind==document::FeatureKind::Sweep3D||
                 feature->feature_kind==document::FeatureKind::HelicalSweep||
                 (feature->feature_kind==document::FeatureKind::Feature&&
                  feature->feature.type==document::FeatureType::Modeling&&
                  (feature->feature.effective_side(0).operation==document::FeatureSideOperation::Twist||
                   feature->feature.effective_side(0).operation==document::FeatureSideOperation::Revolution||
                   feature->feature.effective_side(1).operation==document::FeatureSideOperation::Revolution)))) {
            sources.emplace(feature->id,false);ordered.push_back(feature->id);
        }
    }
    if(restore)std::erase_if(ordered,[&](const auto& owner){return !sources.at(owner);});
    return ordered;
}

bool commit_solid_state(Workspace& live,const kernel::OcctKernel& kernel,
        const std::string& id,document::HistoryContainer feature) {
    auto* state=live.open_part(id);
    if(!state||!document::is_solid_state(feature.feature_kind))
        throw std::invalid_argument("Solid state requires an open Part.");
    const auto& before=state->session.document();const auto* existing=before.find_container(feature.id);
    if(existing&&(existing->feature_kind!=feature.feature_kind||existing->feature_id!=feature.feature_id||
        existing->feature_parent_id!=feature.feature_parent_id||existing->container_origin!=feature.container_origin))
        throw std::invalid_argument("Solid state editing must preserve feature identity.");
    const auto* body=existing?before.body_owner_for_object(feature.id):before.body_history.find(before.body_history.active_body_id());
    if(!body||body->derived_copy||body->scope.id!=before.body_history.active_body_id())
        throw std::invalid_argument("Activate an editable Body before changing its solid state.");
    document::validate_native_metadata_text(feature.name);
    if(feature.name.empty()||feature.id.empty()||feature.feature_id.empty())
        throw std::invalid_argument("Specify a solid state name and identity.");
    if(feature.feature_kind==document::FeatureKind::RestoreShape)feature.solid_state.coefficient=1;
    // No geometry calculation, cache publication or Undo item for unchanged OK.
    if(existing&&*existing==feature)return false;
    if(!calculated_operation_input(state->session,existing?feature.id:std::string{}))
        throw std::invalid_argument("Solid state requires a calculated input body.");
    const auto available=solid_state_sources(before,feature.feature_kind==document::FeatureKind::RestoreShape,
        existing?feature.id:std::string{});
    if(available.empty()||(!feature.solid_state.all&&feature.solid_state.owners.empty()))
        throw std::invalid_argument("Select at least one eligible solid element.");
    if(!feature.solid_state.all)for(const auto& owner:feature.solid_state.owners)
        if(std::ranges::find(available,owner)==available.end())
            throw std::invalid_argument("A selected solid element is unavailable.");
    auto next=before;const auto owner=feature.id;
    if(existing)*next.find_container(owner)=std::move(feature);
    else {next.insert_history_entry(document::PartHistoryKind::Feature,owner);next.history.push_back(std::move(feature));}
    // Serialization validates the complete native definition before calculation.
    static_cast<void>(next.serialized());
    PartCalculationPolicy policy;policy.reject_errors=true;
    auto calculated=calculate_part_with_resolved_references(kernel,next,&state->session.calculated_boundaries(),policy);
    commit_part_document(live,id,std::move(next),std::move(calculated));
    return true;
}
}
