#include <zima/workspace/fem_condition_operations.hpp>
#include <zima/workspace/calculated_body_source.hpp>
#include <zima/document/fem_presentation.hpp>
#include <zima/document/placement_reference_geometry.hpp>
#include <zima/kernel/fem_region_validation.hpp>
#include <zima/workspace/modeling_confirmation.hpp>
#include <algorithm>

namespace zima::workspace {
std::string fem_source_frame_owner(const document::BodyHistoryGraph& graph,const std::string& id) {
    const auto* body=graph.find(id);if(!body||!body->fem)return {};
    return material_source_frame_owner(graph,body->fem->source_id);
}
const document::FemCondition* find_fem_condition(const document::PartDocument& doc,const std::string& body,const std::string& id){
    const auto* b=doc.body_history.find(body);if(!b||!b->fem)return nullptr;
    const auto it=std::ranges::find(b->fem->conditions,id,&document::FemCondition::id);return it==b->fem->conditions.end()?nullptr:&*it;
}
kernel::ViewerReferenceGeometry fem_placement_geometry(const document::DocumentSession& session,const std::string& body){
    const auto source=read_fem_source(session,body);
    return material_source_placement_geometry(session.document(),source,
        session.document().body_history.find(body)->fem->source_id);
}
bool commit_fem_condition(Workspace& live,const BodyGraphEdit& edit,document::FemCondition value){
    auto* state=live.open_part(edit.document_id);if(!state)throw std::invalid_argument("The requested FEM Body does not exist.");
    const auto* body=state->session.document().body_history.find(edit.object_id);
    if(!body||!body->fem)throw std::invalid_argument("The requested FEM Body does not exist.");
    const auto* old=find_fem_condition(state->session.document(),edit.object_id,value.id);
    if(old&&*old==value&&value.calculation_error.empty()){return commit_fem(live,edit,*body);}
    document::validate_fem_condition(value);
    if(value.distance) {
        for(const auto& id:value.distance->point_ids) {
            const auto* point=find_fem_condition(state->session.document(),edit.object_id,id);
            const bool retained=old&&old->distance&&std::ranges::find(old->distance->point_ids,id)!=old->distance->point_ids.end();
            if((point&&point->kind!=document::FemConditionKind::MeasurementPoint)||(!point&&!retained))
                throw std::invalid_argument("Select measurement points from the same FEM Body.");
        }
        // A derived measurement edits metadata only; no placement or solid work.
        auto updated=*body;auto& conditions=updated.fem->conditions;
        const auto item=std::ranges::find(conditions,value.id,&document::FemCondition::id);
        if(item==conditions.end())conditions.push_back(std::move(value));else *item=std::move(value);
        return commit_fem(live,edit,std::move(updated));
    }
    value.calculation_error.clear();
    const auto validate=[&](document::FemCondition& value) {
    const auto source=read_fem_source(state->session,edit.object_id);
    const auto target_kind=value.projected_sketch?document::FemRegionKind::Surface:value.region;
    for(const auto& r:value.references)if(!document::fem_region_reference_available(r,target_kind,source->mesh)) {
        const bool retained=old&&old->region==value.region&&
            old->projected_sketch.has_value()==value.projected_sketch.has_value()&&std::ranges::find(old->references,r)!=old->references.end();
        if(!retained)throw std::invalid_argument("The condition geometry is unavailable on the FEM source.");
    }
    const auto geometry=fem_placement_geometry(state->session,edit.object_id);
    auto resolved=value.placement;
    if(document::resolve_placement(resolved,geometry))value.placement=std::move(resolved);
    else {if(!old)throw std::invalid_argument("The condition placement is unresolved.");value.placement=old->placement;value.placement.references=resolved.references;value.placement.reference_valid=false;}
    if(value.placement.reference_valid) {
        document::resolve_fem_condition_sketch_plane(value,geometry);
        document::refresh_fem_condition_sketch(value,state->session.document().document_id,source->mesh);
    }
    if(value.projected_sketch&&value.placement.reference_valid&&
            (!old||old->projected_sketch!=value.projected_sketch||old->region!=value.region||
             old->references!=value.references||old->placement!=value.placement||!old->calculation_error.empty())) {
        std::vector<kernel::FaceReference> references;
        for(const auto& ref:value.references)
            references.push_back({ref.owner_id,ref.semantic_key,ref.instance_path});
        if(value.projected_sketch->project_to_body)
            value.projected_sketch->surface_region_keys=kernel::validate_projected_mesh_region(
                source.get(),document::fem_condition_projection_request(value),references,
                value.projected_sketch->surface_seeds,value.projected_sketch->surface_region_keys);
        else kernel::validate_planar_mesh_region(source.get(),document::fem_condition_planar_request(value));
    }
    };
    if(auto* attempt=ModelingConfirmationAttempt::current();attempt&&attempt->applies(state->session.document()))
        attempt->validate(value,edit.object_id,validate);
    else validate(value);
    auto updated=*body;auto& conditions=updated.fem->conditions;
    const auto item=std::ranges::find(conditions,value.id,&document::FemCondition::id);
    if(item==conditions.end())conditions.push_back(std::move(value));else *item=std::move(value);
    return commit_fem(live,edit,std::move(updated));
}
bool erase_fem_condition(Workspace& live,const BodyGraphEdit& edit,const std::string& id){
    const auto* state=live.open_part(edit.document_id);const auto* body=state?state->session.document().body_history.find(edit.object_id):nullptr;
    if(!body||!body->fem)throw std::invalid_argument("The requested FEM Body does not exist.");
    auto value=*body;std::erase_if(value.fem->conditions,[&](const auto& c){return c.id==id;});return commit_fem(live,edit,std::move(value));
}
} // namespace zima::workspace
