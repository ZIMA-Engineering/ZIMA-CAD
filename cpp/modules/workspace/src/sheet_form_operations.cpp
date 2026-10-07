#include <zima/workspace/sheet_form_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/document/sheet_form_definition.hpp>
#include <zima/document/metadata.hpp>
namespace zima::workspace {
bool commit_sheet_form(Workspace& live,const kernel::OcctKernel& kernel,
        const std::string& id,document::HistoryContainer feature) {
    auto* state=live.open_part(id);
    if(!state||feature.feature_kind!=document::FeatureKind::SheetForm)
        throw std::invalid_argument("FORM requires an open Part.");
    const auto& before=state->session.document();const auto* existing=before.find_container(feature.id);
    if(existing&&(existing->feature_kind!=feature.feature_kind||existing->feature_id!=feature.feature_id||
        existing->feature_parent_id!=feature.feature_parent_id||existing->container_origin!=feature.container_origin))
        throw std::invalid_argument("FORM editing must preserve feature identity.");
    const auto* body=existing?before.body_owner_for_object(feature.id):before.body_history.find(before.body_history.active_body_id());
    if(!body||body->derived_copy||body->scope.id!=before.body_history.active_body_id())
        throw std::invalid_argument("Activate an editable Body before inserting FORM.");
    document::validate_native_metadata_text(feature.name);
    if(feature.name.empty())throw std::invalid_argument("Specify a FORM feature name.");
    document::validate_sheet_form_parameters(feature.sheet_form);
    if(existing&&*existing==feature)return false;
    if(!feature.sheet_form.support.valid()||!feature.sheet_form.support.instance_path.empty())
        throw std::invalid_argument("Select a planar outer sheet face for FORM.");
    const auto* support=before.body_owner_for_object(feature.sheet_form.support.owner_id);
    const auto boundary=existing?before.history_index(feature.id):before.effective_history_cursor();
    if(!support||support->scope.id!=body->scope.id||
        before.history_index(feature.sheet_form.support.owner_id)>=boundary)
        throw std::invalid_argument("Select a preceding sheet face in the active Body.");
    // Definition replacement is pending until the whole candidate commits.
    // Complete native role validation consumes the persisted packet, not OCCT.
    static_cast<void>(document::stored_sheet_form_definition(feature.sheet_form));
    auto next=before;const auto owner=feature.id;
    if(existing)*next.find_container(owner)=std::move(feature);
    else {next.insert_history_entry(document::PartHistoryKind::Feature,owner);next.history.push_back(std::move(feature));}
    auto policy=feature_definition_calculation_policy(before,state->session.calculated_boundaries(),owner);
    if(existing){policy.edited_document_id=id;policy.edited_history_limit=before.history_index(owner);}
    auto calculated=calculate_part_with_resolved_references(kernel,next,&state->session.calculated_boundaries(),policy);
    if(next.serialized()==before.serialized()){state->session.update_calculated_boundaries(std::move(calculated));return false;}
    commit_part_document(live,id,std::move(next),std::move(calculated));return true;
}
}
