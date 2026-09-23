#include <zima/workspace/sheet_transition_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/document/sheet_transition.hpp>
#include <zima/document/metadata.hpp>
#include <zima/workspace/feature_reference_input.hpp>
namespace zima::workspace {
bool commit_sheet_transition(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,document::HistoryContainer feature) {
    auto* state=live.open_part(id);if(!state||feature.feature_kind!=document::FeatureKind::SheetTransition)throw std::invalid_argument("Sheet transition requires an open Part.");
    const auto& before=state->session.document();const auto* existing=before.find_container(feature.id);
    if(existing&&(existing->feature_kind!=feature.feature_kind||existing->feature_id!=feature.feature_id||existing->feature_parent_id!=feature.feature_parent_id||existing->container_origin!=feature.container_origin))throw std::invalid_argument("Sheet transition editing must preserve feature identity.");
    const auto* body=existing?before.body_owner_for_object(feature.id):before.body_history.find(before.body_history.active_body_id());
    if(!body||body->derived_copy||body->scope.id!=before.body_history.active_body_id())throw std::invalid_argument("Activate an editable Body before creating a sheet transition.");
    document::validate_native_metadata_text(feature.name);if(feature.name.empty())throw std::invalid_argument("Specify a sheet transition name.");
    auto next=before;const auto owner=feature.id;
    if(existing)*next.find_container(owner)=std::move(feature);else {next.insert_history_entry(document::PartHistoryKind::Feature,owner);next.history.push_back(std::move(feature));}
    auto references=construction_reference_source_geometry(state->session.calculated_boundaries());
    append_reference_geometry(references,next.origin_viewer_mesh().original_references);
    append_reference_geometry(references,next.construction_viewer_mesh().original_references);
    next.resolve_constructions(references);document::reframe_sheet_transition(*next.find_container(owner));
    PartCalculationPolicy policy;policy.reject_errors=true;if(existing){policy.edited_document_id=id;policy.edited_history_limit=before.history_index(owner);}
    auto calculated=calculate_part_with_resolved_references(kernel,next,&state->session.calculated_boundaries(),policy);
    if(next.serialized()==before.serialized()){state->session.update_calculated_boundaries(std::move(calculated));return false;}
    commit_part_document(live,id,std::move(next),std::move(calculated));return true;
}
}
