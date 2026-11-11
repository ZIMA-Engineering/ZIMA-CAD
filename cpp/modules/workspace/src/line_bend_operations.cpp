#include <zima/workspace/line_bend_operations.hpp>
#include <zima/workspace/origin_display_operations.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/document/line_bend.hpp>
#include <zima/document/sketch_placement.hpp>
#include <algorithm>
#include <ranges>

namespace zima::workspace {
bool commit_line_bend(Workspace& live,const kernel::OcctKernel& kernel,
        const std::string& id,document::HistoryContainer feature,
        sketcher::Sketch sketch) {
    auto* state=live.open_part(id);
    if(!state)throw std::invalid_argument(
        "Line Bend is available only in a Part.");
    const auto& before=state->session.document();
    const auto* existing=before.find_container(feature.id);
    const auto* body=existing?before.body_owner_for_object(feature.id):
        before.body_history.find(before.body_history.active_body_id());
    if(body&&body->derived_copy)throw std::invalid_argument(
        "A derived Body cannot be edited directly.");
    if(body&&body->scope.id!=before.body_history.active_body_id())
        throw std::invalid_argument(
            "Activate the Body owning this Line Bend.");
    if(!calculated_operation_input(state->session,
            existing?feature.id:std::string{}))
        throw std::invalid_argument(
            "Line Bend requires a calculated sheet input body.");
    const auto old=std::ranges::find(before.sketches,sketch.id,
        &sketcher::Sketch::id);
    if(existing) {
        if(existing->feature_kind!=document::FeatureKind::LineBend||
                existing->feature_id!=feature.feature_id||
                existing->feature_parent_id!=feature.feature_parent_id||
                existing->container_origin!=feature.container_origin||
                existing->line_bend.sketch_id!=feature.line_bend.sketch_id||
                old==before.sketches.end()||
                old->owner_container_id!=feature.id)
            throw std::invalid_argument(
                "Line Bend editing must preserve its container and Sketch identities.");
    } else if(feature.id.empty()||feature.feature_id.empty()||
            old!=before.sketches.end())
        throw std::invalid_argument(
            "A new Line Bend must own a new container and Sketch.");
    if(feature.feature_kind!=document::FeatureKind::LineBend||
            feature.line_bend.sketch_id!=sketch.id||
            sketch.owner_container_id!=feature.id)
        throw std::invalid_argument("Invalid Line Bend Sketch ownership.");
    document::validate_native_metadata_text(feature.name);
    if(feature.name.empty())throw std::invalid_argument(
        "Specify a Line Bend name.");
    const auto ordinary_count=std::ranges::count_if(sketch.segments,
        [](const auto& segment){return !segment.construction;});
    if(ordinary_count!=1)
        throw std::invalid_argument(
            "Line Bend requires exactly one ordinary straight Sketch segment.");
    feature.line_bend.segment_id=std::ranges::find_if(sketch.segments,
        [](const auto& segment){return !segment.construction;})->id;
    static_cast<void>(document::line_bend_request(feature,sketch,
        document::sheet_metal_defaults(before)));
    document::normalize_container_front_references(feature.placement.references);
    if(!state->session.calculated_boundaries().empty()&&
            old!=before.sketches.end()&&
            old->serialized()==sketch.serialized())
        if(const auto changed=commit_origin_display_only(live,id,feature))
            return *changed;
    const auto container=feature.id;auto next=before;
    if(existing) {
        *next.find_container(container)=std::move(feature);
        *std::ranges::find(next.sketches,sketch.id,
            &sketcher::Sketch::id)=std::move(sketch);
    } else {
        next.insert_history_entry(document::PartHistoryKind::Feature,
            container);
        next.history.push_back(std::move(feature));
        next.sketches.push_back(std::move(sketch));
    }
    auto policy=feature_definition_calculation_policy(before,
        state->session.calculated_boundaries(),container);
    if(existing) {
        policy.edited_document_id=id;
        policy.edited_history_limit=before.history_index(container);
    }
    kernel::ViewerReferenceGeometry references;
    if(const auto* input=calculated_operation_input(state->session,
            existing?container:std::string{}))
        references=input->mesh.original_references;
    append_reference_geometry(references,
        next.origin_viewer_mesh().original_references);
    append_reference_geometry(references,
        next.construction_viewer_mesh().original_references);
    next.resolve_constructions(std::move(references));
    auto calculated=calculate_part_with_resolved_references(kernel,next,
        &state->session.calculated_boundaries(),policy);
    if(!next.find_container(container)->placement.reference_valid)
        throw std::invalid_argument(
            "Line Bend placement references cannot be resolved.");
    for(const auto& boundary:calculated)
        if(const auto failed=boundary.calculation_errors.find(container);
                failed!=boundary.calculation_errors.end())
            throw std::runtime_error(failed->second);
    if(next.serialized()==before.serialized()) {
        state->session.update_calculated_boundaries(std::move(calculated));
        return false;
    }
    commit_part_document(live,id,std::move(next),std::move(calculated));
    return true;
}
}
