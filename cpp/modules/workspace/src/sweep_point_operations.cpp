#include <zima/workspace/sweep_point_operations.hpp>
#include <zima/workspace/construction_reference_operations.hpp>
#include <zima/workspace/feature_reference_input.hpp>
#include <algorithm>
#include <cmath>

namespace zima::workspace {
bool set_sweep_point_reference(Workspace& live,const kernel::OcctKernel& kernel,
    const std::string& id,const std::string& object,std::size_t index,
    document::ConstructionReference reference,bool derive_orientation) {
    const auto reject=[](const char* code,const char* message){throw PlacementEditError(code,message);};
    if(index>4||reference.owner_id.empty()||reference.semantic_key.empty()||
        !reference.instance_path.empty()||!std::isfinite(reference.offset))
        reject("invalid_arguments","A feature reference requires a local source, a slot from 0 to 4 and a finite offset.");
    const auto* state=live.open_part(id);
    if(!state)reject("unsupported_document","Sweep operations require an open Part.");
    const auto& before=state->session.document();
    const auto feature=std::ranges::find_if(before.history,[&](const auto& value){
        return value.feature_kind==document::FeatureKind::Sweep3D&&
            std::ranges::any_of(value.sweep3d.path.curve_points,[&](const auto& point){return point.id==object;});
    });
    if(feature==before.history.end())
        reject("construction_not_found","This command requires an independent construction container or its curve point.");
    if(const auto* body=before.body_owner_for_object(feature->id)){
        if(body->derived_copy)reject("read_only_body","A derived Body cannot be edited directly.");
        if(body->scope.id!=before.body_history.active_body_id())
            reject("inactive_body","Activate the owning Body before editing its history.");
    }
    const auto& path=feature->sweep3d.path;
    const auto found=std::ranges::find(path.curve_points,object,&document::ConstructionObject::id);
    const bool parent_origin=reference.owner_id==path.container_origin.id&&reference.semantic_key.starts_with("origin:");
    const bool earlier_point=std::any_of(path.curve_points.begin(),found,[&](const auto& point){
        return reference.owner_id==point.container_origin.id&&
            (reference.semantic_key=="point"||reference.semantic_key.starts_with("origin:"));
    });
    if(!parent_origin&&!earlier_point)
        validate_part_feature_reference_source(before,feature->id,reference);

    // Reuse native reference data in the owning Body's frame. A transient Curve
    // carrier supplies the path's frame and its persisted child datum geometry,
    // as in Point Properties; it is never inserted into the document.
    auto geometry=placement_edit_geometry(live,id,feature->id);
    auto frame=path;
    frame.parent_construction_id.clear();frame.references.clear();
    frame.origin={feature->placement.x,feature->placement.y,feature->placement.z};
    frame.entity_origin=frame.origin;
    frame.rotation={feature->placement.rotation_x,feature->placement.rotation_y,feature->placement.rotation_z};
    frame.absolute_rotation=frame.rotation;
    frame.rotation_offset_x=frame.rotation_offset_y=frame.rotation_offset_z=0;
    frame.orientation_back=false;frame.orientation_quarter_turns=0;
    frame.definition=document::ConstructionDefinition::Absolute;
    document::PartDocument carrier;carrier.constructions.push_back(std::move(frame));
    geometry=carrier.construction_reference_geometry_for(object,std::move(geometry));
    auto point=prepare_construction_reference(*found,geometry,index,std::move(reference),derive_orientation);
    if(point==*found)return false;
    auto draft=*feature;
    *std::ranges::find(draft.sweep3d.path.curve_points,object,&document::ConstructionObject::id)=std::move(point);
    commit_sweep(live,kernel,id,std::move(draft),SweepEditMode::Replace);
    return true;
}
}
