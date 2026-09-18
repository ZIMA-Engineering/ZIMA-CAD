#include <zima/workspace/sheet_state_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/document/precision.hpp>
#include <set>
namespace zima::workspace {
namespace {
kernel::sheet_material::History sheet_state_history(const document::PartDocument& document,const std::string& edited) {
    std::set<std::string> owners;
    if(!document.body_history.bodies().empty()) {
        const auto* body=edited.empty()?document.body_history.find(document.body_history.active_body_id()):document.body_owner_for_object(edited);
        if(!body)return {};
        const auto limit=edited.empty()?body->cursor:document.body_history.rollback_before(edited).entry_count;
        for(std::size_t i=0;i<std::min(limit,body->entries.size());++i)owners.insert(body->entries[i].id);
    }else {
        const auto limit=edited.empty()?document.effective_history_cursor():document.history_index(edited).value_or(0);
        if(document.history_order.empty()) {
            for(std::size_t i=0;i<std::min(limit,document.history.size());++i)owners.insert(document.history[i].id);
        }else for(std::size_t i=0;i<std::min(limit,document.history_order.size());++i)owners.insert(document.history_order[i].id);
    }
    auto operations=document.kernel_operations(true,true);
    std::erase_if(operations,[&](const auto& operation){return !owners.contains(operation.owner_id);});
    return kernel::sheet_material::regions_before(operations,operations.size());
}
}
std::vector<kernel::SheetMaterialDefinition> sheet_state_regions(const document::PartDocument& document,const std::string& edited) {
    return sheet_state_history(document,edited).regions;
}
bool commit_sheet_state(Workspace& live,const kernel::OcctKernel& kernel,
        const std::string& id,document::HistoryContainer feature) {
    auto* state=live.open_part(id);
    if(!state||!document::is_sheet_state(feature.feature_kind))throw std::invalid_argument("Sheet state requires an open Part.");
    const auto& before=state->session.document();const auto* existing=before.find_container(feature.id);
    if(existing&&(existing->feature_kind!=feature.feature_kind||existing->feature_id!=feature.feature_id||
        existing->feature_parent_id!=feature.feature_parent_id||existing->container_origin!=feature.container_origin))
        throw std::invalid_argument("Sheet state editing must preserve feature identity.");
    const auto* body=existing?before.body_owner_for_object(feature.id):before.body_history.find(before.body_history.active_body_id());
    if(body&&(body->derived_copy||body->scope.id!=before.body_history.active_body_id()))throw std::invalid_argument("Activate an editable Body before changing its sheet state.");
    if(!calculated_operation_input(state->session,existing?feature.id:std::string{}))throw std::invalid_argument("Sheet state requires a calculated input body.");
    document::validate_native_metadata_text(feature.name);
    if(feature.name.empty()||feature.id.empty()||feature.feature_id.empty())throw std::invalid_argument("Specify a sheet state name and identity.");
    auto regions=sheet_state_history(before,existing?feature.id:std::string{});
    static_cast<void>(kernel::sheet_material::change(regions,{feature.feature_kind==document::FeatureKind::Unbend,
        feature.sheet_state.all,feature.sheet_state.owners,document::sheet_cut_tolerance(before.document_precision)}));
    auto next=before;const auto owner=feature.id;
    if(existing)*next.find_container(owner)=std::move(feature);
    else {next.insert_history_entry(document::PartHistoryKind::Feature,owner);next.history.push_back(std::move(feature));}
    PartCalculationPolicy policy;policy.reject_errors=true;
    if(existing){policy.edited_document_id=id;policy.edited_history_limit=before.history_index(owner);}
    auto calculated=calculate_part_with_resolved_references(kernel,next,&state->session.calculated_boundaries(),policy);
    if(next.serialized()==before.serialized()){state->session.update_calculated_boundaries(std::move(calculated));return false;}
    commit_part_document(live,id,std::move(next),std::move(calculated));return true;
}
}
