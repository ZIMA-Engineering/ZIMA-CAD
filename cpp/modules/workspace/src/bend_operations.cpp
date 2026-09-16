#include <zima/workspace/bend_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/sketch_placement.hpp>
#include <algorithm>

namespace zima::workspace {
bool commit_bend(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,
    document::HistoryContainer feature,sketcher::Sketch sketch) {
    auto* state=live.open_part(id);
    if(!state)throw std::invalid_argument("Bend is available only in a Part.");
    const auto& before=state->session.document();const auto* existing=before.find_container(feature.id);
    document::prepare_bend_sketches(feature,sketch,document::sheet_metal_defaults(before));
    const auto* body=existing?before.body_owner_for_object(feature.id):before.body_history.find(before.body_history.active_body_id());
    if(body&&body->derived_copy)throw std::invalid_argument("A derived Body cannot be edited directly.");
    if(body&&body->scope.id!=before.body_history.active_body_id())throw std::invalid_argument("Activate the Body owning this Bend.");
    const auto old=std::ranges::find(before.sketches,sketch.id,&sketcher::Sketch::id);
    if(existing) {
        if(existing->feature_kind!=document::FeatureKind::Bend||existing->feature_id!=feature.feature_id||
            existing->feature_parent_id!=feature.feature_parent_id||existing->container_origin!=feature.container_origin||
            existing->bend.sketch_id!=feature.bend.sketch_id||old==before.sketches.end()||old->owner_container_id!=feature.id)
            throw std::invalid_argument("Bend editing must preserve its container and Sketch identities.");
        for(std::size_t stage=0;stage<2;++stage) {
            const auto previous=sketcher::Sketch::from_serialized(existing->bend.auxiliary_sketches[stage]);
            const auto pending=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[stage]);
            if(previous.id!=pending.id||pending.owner_container_id!=feature.id)
                throw std::invalid_argument("Bend editing must preserve its auxiliary Sketch identities.");
        }
        if(*existing==feature&&old->serialized()==sketch.serialized())return false;
    } else if(feature.id.empty()||feature.feature_id.empty()||old!=before.sketches.end())
        throw std::invalid_argument("A new Bend must own a new container and Sketch.");
    document::validate_native_metadata_text(feature.name);
    if(feature.name.empty())throw std::invalid_argument("Specify a Bend name.");
    static_cast<void>(document::bend_request(feature,sketch,document::sheet_metal_defaults(before)));
    document::normalize_container_front_references(feature.placement.references);
    const auto container=feature.id;auto next=before;
    if(existing) {
        *next.find_container(container)=std::move(feature);
        *std::ranges::find(next.sketches,sketch.id,&sketcher::Sketch::id)=std::move(sketch);
    } else {
        next.insert_history_entry(document::PartHistoryKind::Feature,container);
        next.history.push_back(std::move(feature));next.sketches.push_back(std::move(sketch));
    }
    PartCalculationPolicy policy;policy.reject_errors=true;
    if(existing){policy.edited_document_id=id;policy.edited_history_limit=before.history_index(container);}
    auto references=construction_reference_source_geometry(state->session.calculated_boundaries());
    append_reference_geometry(references,next.origin_viewer_mesh().original_references);
    append_reference_geometry(references,next.construction_viewer_mesh().original_references);
    next.resolve_constructions(std::move(references));
    auto calculated=calculate_part_with_resolved_references(kernel,next,&state->session.calculated_boundaries(),policy);
    if(!next.find_container(container)->placement.reference_valid)throw std::invalid_argument("Bend placement references cannot be resolved.");
    commit_part_document(live,id,std::move(next),std::move(calculated));return true;
}
}
