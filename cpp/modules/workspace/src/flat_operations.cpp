#include <zima/workspace/flat_operations.hpp>
#include <zima/workspace/origin_display_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/document/flat.hpp>
#include <zima/document/sketch_placement.hpp>
#include <algorithm>

namespace zima::workspace {
bool initialize_attached_flat_profile(sketcher::Sketch& sketch) {
    if(!sketch.points.empty()||!sketch.segments.empty()||!sketch.circles.empty()||
       !sketch.arcs.empty()||!sketch.ellipses.empty()||!sketch.elliptical_arcs.empty()||
       !sketch.bsplines.empty()||!sketch.texts.empty())return false;
    const auto endpoint=[&](const char* suffix) {
        return std::ranges::find(sketch.external_references,sketch.id+suffix,&sketcher::SketchExternalReference::id);
    };
    auto first=endpoint(":flat-attachment:first"),last=endpoint(":flat-attachment:last");
    if(first==sketch.external_references.end()||last==sketch.external_references.end()||
       first->broken||last->broken||first->cached_points.size()!=1||last->cached_points.size()!=1)return false;
    if(first->cached_points.front()[0]>last->cached_points.front()[0])std::swap(first,last);
    const auto a=first->cached_points.front(),b=last->cached_points.front();
    if(std::abs(a[1]-b[1])>1e-8||b[0]-a[0]<=1e-8)return false;
    auto draft=sketch;
    const auto base=draft.add_segment(a[0],a[1],b[0],b[1],0);
    const auto right=draft.add_segment(b[0],b[1],b[0],b[1]+50);
    const auto top=draft.add_segment(b[0],b[1]+50,a[0],a[1]+50);
    const auto left=draft.add_segment(a[0],a[1]+50,a[0],a[1]);
    const auto segment=[&](const auto& id){return *std::ranges::find(draft.segments,id,&sketcher::SketchSegment::id);};
    const auto bottom=segment(base),r=segment(right),t=segment(top),l=segment(left);
    const auto contact=[&](const auto& point,const auto& support) {
        if(!std::ranges::any_of(draft.constraints,[&](const auto& c){return !c.suppressed&&
            c.kind==sketcher::ConstraintKind::PointReference&&c.first_point_id==point&&c.second_point_id==support;}))
            static_cast<void>(draft.add_point_reference_constraint(point,support));
    };
    contact(bottom.first_point_id,first->id);contact(bottom.second_point_id,last->id);
    static_cast<void>(draft.add_point_pair_constraint(r.first_point_id,r.second_point_id,sketcher::ConstraintKind::Vertical));
    static_cast<void>(draft.add_point_pair_constraint(t.first_point_id,t.second_point_id,sketcher::ConstraintKind::Horizontal));
    static_cast<void>(draft.add_point_pair_constraint(l.first_point_id,l.second_point_id,sketcher::ConstraintKind::Vertical));
    // The two external contacts already determine the base direction; an
    // additional horizontal equation there would be redundant.
    auto height=draft.create_point_dimension(r.first_point_id,r.second_point_id,sketcher::DimensionKind::DistanceY);
    height.placement=std::array<double,2>{b[0]+10,b[1]+25};
    draft.apply_dimension(std::move(height));
    sketch=std::move(draft);return true;
}

bool commit_flat(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,
    document::HistoryContainer feature,sketcher::Sketch sketch) {
    auto* state=live.open_part(id);
    if(!state)throw std::invalid_argument("Flat is available only in a Part.");
    const auto& before=state->session.document();const auto* existing=before.find_container(feature.id);
    const auto* body=existing?before.body_owner_for_object(feature.id):before.body_history.find(before.body_history.active_body_id());
    if(body&&body->derived_copy)throw std::invalid_argument("A derived Body cannot be edited directly.");
    if(body&&body->scope.id!=before.body_history.active_body_id())throw std::invalid_argument("Activate the Body owning this Flat.");
    const auto old=std::ranges::find(before.sketches,sketch.id,&sketcher::Sketch::id);
    if(existing) {
        if(existing->feature_kind!=document::FeatureKind::Flat||existing->feature_id!=feature.feature_id||
            existing->feature_parent_id!=feature.feature_parent_id||existing->container_origin!=feature.container_origin||
            existing->flat.sketch_id!=feature.flat.sketch_id||old==before.sketches.end()||old->owner_container_id!=feature.id)
            throw std::invalid_argument("Flat editing must preserve its container and Sketch identities.");
        // OK is an explicit calculation request even when parameters match.
    } else if(feature.id.empty()||feature.feature_id.empty()||old!=before.sketches.end())
        throw std::invalid_argument("A new Flat must own a new container and Sketch.");
    document::validate_native_metadata_text(feature.name);
    if(feature.name.empty())throw std::invalid_argument("Specify a Flat name.");
    static_cast<void>(document::flat_request(feature,sketch,document::sheet_metal_defaults(before)));
    document::normalize_container_front_references(feature.placement.references);
    if(old!=before.sketches.end() && old->serialized()==sketch.serialized())
        if(const auto changed=commit_origin_display_only(live,id,feature))return *changed;
    const auto container=feature.id;auto next=before;
    if(existing) {
        *next.find_container(container)=std::move(feature);
        *std::ranges::find(next.sketches,sketch.id,&sketcher::Sketch::id)=std::move(sketch);
    } else {
        next.insert_history_entry(document::PartHistoryKind::Feature,container);
        next.history.push_back(std::move(feature));next.sketches.push_back(std::move(sketch));
    }
    auto policy=feature_definition_calculation_policy(before,state->session.calculated_boundaries(),container);
    if(existing){policy.edited_document_id=id;policy.edited_history_limit=before.history_index(container);}
    auto references=construction_reference_source_geometry(state->session.calculated_boundaries());
    append_reference_geometry(references,next.origin_viewer_mesh().original_references);
    append_reference_geometry(references,next.construction_viewer_mesh().original_references);
    next.resolve_constructions(std::move(references));
    auto calculated=calculate_part_with_resolved_references(kernel,next,&state->session.calculated_boundaries(),policy);
    if(!next.find_container(container)->placement.reference_valid)throw std::invalid_argument("Flat placement references cannot be resolved.");
    if(next.serialized()==before.serialized()) {
        state->session.update_calculated_boundaries(std::move(calculated));return false;
    }
    commit_part_document(live,id,std::move(next),std::move(calculated));return true;
}
}
