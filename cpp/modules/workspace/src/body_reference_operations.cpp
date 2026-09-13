#include <zima/workspace/body_reference_operations.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <zima/document/placement_reference_assignment.hpp>
#include <algorithm>
#include <cmath>
namespace zima::workspace {
namespace {
using Ref=document::ConstructionReference;
void reject(const char* code,const char* message){throw BodyOperationError(code,message);}
std::vector<Ref> combined(const std::vector<Ref>& position,const std::vector<Ref>& orientation,std::optional<std::size_t> skip={}) {
    std::vector<Ref> result;
    for(std::size_t i=0;i<position.size();++i)if(skip!=i&&(!position[i].owner_id.empty()||!position[i].semantic_key.empty()))result.push_back(position[i]);
    for(std::size_t i=0;i<orientation.size();++i)if(skip!=i+3&&(!orientation[i].owner_id.empty()||!orientation[i].semantic_key.empty()))result.push_back(orientation[i]);
    return result;
}
}
bool set_body_placement_reference(Workspace& live,const kernel::OcctKernel& kernel,
    const std::string& id,const std::string& body_id,std::size_t index,Ref source,bool derive_orientation) {
    if(index>4||source.owner_id.empty()||source.semantic_key.empty()||!source.instance_path.empty()||!std::isfinite(source.offset))
        reject("invalid_arguments","A Body reference requires a local source, a slot from 0 to 4 and a finite offset.");
    auto* state=live.open_part(id);if(!state)reject("unsupported_document","Body operations require an open Part.");
    const auto& before=state->session.document();const auto edit=prepare_body_edit(before,body_id);auto value=*edit.pending.find(body_id);
    if(value.derived_copy)reject("read_only_body","A derived Body cannot be edited directly.");
    const auto& order=before.body_history.order();const auto position_index=std::ranges::find(order,body_id)-order.begin();
    if(source.owner_id!=before.document_id+":origin") {
        const auto* owner=before.body_owner_for_object(source.owner_id);
        if(!owner||std::ranges::find(order,owner->scope.id)-order.begin()>=position_index)
            reject("reference_not_available","A Body reference must belong to the Part origin or an earlier Body.");
    }
    auto geometry=placement_edit_geometry(live,id,body_id);
    append_reference_geometry(geometry,before.history_origin_reference_geometry_before({}));
    const auto matches=[&](const auto& item){return item.reference.owner_id==source.owner_id&&item.reference.semantic_key==source.semantic_key&&item.reference.instance_path.empty();};
    const bool point=std::ranges::any_of(geometry.points,matches),axis=std::ranges::any_of(geometry.axes,matches),edge=std::ranges::any_of(geometry.edges,matches);
    const bool face=std::ranges::any_of(geometry.triangle_references,[&](const auto& ref){return ref.owner_id==source.owner_id&&ref.semantic_key==source.semantic_key&&ref.instance_path.empty();});
    if(!point&&!axis&&!edge&&!face)reject("reference_not_found","The original Body placement reference is unavailable.");
    if((index>=3||!face)&&source.offset!=0)
        reject("parameter_not_editable","The placement parameter is unknown, constrained or locked.");
    source.supports_offset=face;source.orientation_only=false;source.orientation_role="none";source.orientation_drives_rotation=false;source.measured_offset.reset();
    std::vector<Ref> position,orientation(2);std::array<bool,3> empty_locks{};
    for(const auto& ref:value.scope.placement.references) {
        if(ref.orientation_only&&ref.orientation_role!="direction") {
            const bool second=ref.orientation_role=="top"||ref.orientation_role=="bottom"||ref.orientation_role=="left"||ref.orientation_role=="right";
            orientation[second?1:0]=ref;
        } else position.push_back(ref);
    }
    if(position.size()>3)reject("invalid_placement","The Body has too many position reference rows.");
    auto baseline=value.scope.placement;baseline.references=combined(position,orientation,index);
    if(!document::resolve_placement(baseline,geometry))reject("invalid_placement","Existing Body placement references cannot be resolved.");
    const kernel::Vec3 origin{baseline.x,baseline.y,baseline.z};
    const auto translation_dof=document::point_constraint_remaining_dof(baseline.references,geometry);
    const auto rotation_dof=document::orientation_constraint_remaining_dof(baseline.references,geometry,true,origin);
    const bool direction=index<3&&translation_dof==0&&rotation_dof>0;
    if(index>=3||direction) {
        source.orientation_drives_rotation=true;source.orientation_role=direction?"direction":index==3?"front":"top";
        source.orientation_only=direction;if(direction)source.supports_offset=false;
    } else if(face||axis||edge) {
        std::set<std::string> used;for(const auto& ref:baseline.references)if(ref.orientation_drives_rotation)used.insert(ref.orientation_role);
        source.orientation_role=!used.contains("front")?"front":!used.contains("top")?"top":"none";
        source.orientation_drives_rotation=source.orientation_role!="none";
    }
    source.measured_offset=document::measure_placement_reference_offset(source,geometry,origin);
    const auto assigned=document::assign_placement_reference({position,orientation,empty_locks},true,index,std::move(source),derive_orientation);
    using Error=document::PlacementReferenceError;
    if(assigned.error==Error::Duplicate)reject("duplicate_reference","The same placement reference cannot be assigned twice.");
    if(assigned.error==Error::MissingMeasuredOffset)reject("invalid_reference","The current distance from the placement reference cannot be measured.");
    if(assigned.error!=Error::None)reject("invalid_arguments","The placement reference slot is unavailable.");
    value.scope.placement.references=combined(position,orientation);
    if(!document::resolve_placement(value.scope.placement,geometry))reject("invalid_reference","The proposed Body placement references cannot be resolved.");
    return commit_body_edit(live,kernel,edit,std::move(value),edit.original.active_body_id()==body_id);
}
}
