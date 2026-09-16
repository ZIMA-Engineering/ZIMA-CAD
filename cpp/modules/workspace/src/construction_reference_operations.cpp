#include <zima/workspace/construction_reference_operations.hpp>
#include <zima/document/placement_reference_assignment.hpp>
#include <algorithm>
#include <cmath>
namespace zima::workspace {
namespace {
using Object=document::ConstructionObject;using Ref=document::ConstructionReference;
void reject(const char* code,const char* text){throw PlacementEditError(code,text);}
bool owns(const Object& object,const std::string& owner) {
    if(owner==object.id||owner==object.entity_id||owner==object.container_origin.id)return true;
    return std::ranges::any_of(object.curve_points,[&](const auto& child){return owns(child,owner);});
}
std::vector<Ref> combined(const std::vector<Ref>& position,const std::vector<Ref>& orientation,std::optional<std::size_t> skip={}) {
    std::vector<Ref> result;
    for(std::size_t i=0;i<position.size();++i)if(skip!=i&&(!position[i].owner_id.empty()||!position[i].semantic_key.empty()))result.push_back(position[i]);
    for(std::size_t i=0;i<orientation.size();++i)if(skip!=i+3&&(!orientation[i].owner_id.empty()||!orientation[i].semantic_key.empty()))result.push_back(orientation[i]);
    return result;
}
}
document::ConstructionObject prepare_construction_reference(Object value,
    const kernel::ViewerReferenceGeometry& geometry,std::size_t index,Ref reference,bool derive_orientation) {
    if(index>4||reference.owner_id.empty()||reference.semantic_key.empty()||!std::isfinite(reference.offset))
        reject("invalid_arguments","A construction reference requires a source, a slot from 0 to 4 and a finite offset.");
    const auto matches=[&](const auto& item){return item.reference.owner_id==reference.owner_id&&item.reference.semantic_key==reference.semantic_key&&item.reference.instance_path==reference.instance_path;};
    const bool point=std::ranges::any_of(geometry.points,matches),axis=std::ranges::any_of(geometry.axes,matches),edge=std::ranges::any_of(geometry.edges,matches);
    const bool face=std::ranges::any_of(geometry.triangle_references,[&](const auto& item){return item.owner_id==reference.owner_id&&item.semantic_key==reference.semantic_key&&item.instance_path==reference.instance_path;});
    if(!point&&!axis&&!edge&&!face)reject("reference_not_found","The original construction reference is unavailable.");
    if((index>=3||!face)&&reference.offset!=0)reject("parameter_not_editable","The placement parameter is unknown, constrained or locked.");
    reference.supports_offset=face;reference.orientation_role="none";reference.orientation_only=false;reference.orientation_drives_rotation=false;reference.measured_offset.reset();
    std::vector<Ref> position,orientation(2);std::array<bool,3> empty_locks{};
    for(const auto& ref:value.references) {
        if(ref.orientation_only&&ref.orientation_role!="direction") {
            const bool second=ref.orientation_role=="top"||ref.orientation_role=="bottom"||ref.orientation_role=="left"||ref.orientation_role=="right";
            orientation[second?1:0]=ref;
        }else position.push_back(ref);
    }
    if(position.size()>3)reject("invalid_placement","The construction has too many position reference rows.");
    const auto baseline=combined(position,orientation,index);
    const auto translation=document::point_constraint_remaining_dof(baseline,geometry,value.origin);
    const auto rotation=document::orientation_constraint_remaining_dof(baseline,geometry,true,value.origin);
    if(index<3&&translation==0&&rotation>0) {
        reference.orientation_drives_rotation=true;reference.orientation_role="direction";reference.orientation_only=true;reference.supports_offset=false;
    }
    // The construction dialog leaves position rows independent of FRONT/TOP.
    // Planar references are mirrored by the approved common assignment helper.
    const bool first_plane=value.kind==document::ConstructionKind::Plane&&index==0&&reference.supports_offset;
    reference.measured_offset=document::measure_placement_reference_offset(reference,geometry,value.origin);
    const auto assigned=document::assign_placement_reference({position,orientation,empty_locks},true,index,std::move(reference),derive_orientation);
    using Error=document::PlacementReferenceError;
    if(assigned.error==Error::Duplicate)reject("duplicate_reference","The same placement reference cannot be assigned twice.");
    if(assigned.error==Error::MissingMeasuredOffset)reject("invalid_reference","The current distance from the placement reference cannot be measured.");
    if(assigned.error!=Error::None)reject("invalid_arguments","The placement reference slot is unavailable.");
    value.references=combined(position,orientation);
    for(auto& ref:value.references)ref.measured_offset.reset();
    if(index<3)value.definition=document::ConstructionDefinition::PointReference;
    if(first_plane&&value.base_plane_auto)value.base_plane=document::LocalDatumPlane::XZ;
    if(!document::resolve_construction(value,geometry))reject("invalid_reference","The proposed construction references cannot be resolved.");
    return value;
}
bool set_construction_reference(Workspace& live,const std::string& id,const std::string& object_id,
    std::size_t index,Ref reference,bool derive_orientation) {
    if(index>4||reference.owner_id.empty()||reference.semantic_key.empty()||!std::isfinite(reference.offset))
        reject("invalid_arguments","A construction reference requires a source, a slot from 0 to 4 and a finite offset.");
    const auto* part=live.open_part(id);const auto* assembly=live.open_assembly(id);
    if(!part&&!assembly)reject("unsupported_document","Construction operations require an open Part or Assembly.");
    const auto& roots=part?part->session.document().constructions:assembly->session.document().constructions;
    const auto root=std::ranges::find_if(roots,[&](const auto& candidate) {
        return candidate.id==object_id||std::ranges::any_of(candidate.curve_points,[&](const auto& point){return point.id==object_id;});
    });
    if(root==roots.end())reject("construction_not_found","This command requires an independent construction container or its curve point.");
    const auto child=std::ranges::find(root->curve_points,object_id,&Object::id);
    const auto* found=root->id==object_id?&*root:&*child;
    auto value=*found;
    if(part) {
        if(!reference.instance_path.empty())reject("invalid_reference","A Part construction requires a local original reference.");
        if(const auto* body=part->session.document().body_owner_for_object(object_id)) {
            if(body->derived_copy)reject("read_only_body","A derived Body cannot be edited directly.");
            if(body->scope.id!=part->session.document().body_history.active_body_id())reject("inactive_body","Activate the owning Body before editing a construction.");
        }
    }
    if(reference.instance_path.empty()) {
        // A child may use the owning Curve's frame and already defined points,
        // but never its resulting edge or a later/self point. Those depend on it.
        const bool earlier_point=child!=root->curve_points.end()&&std::any_of(root->curve_points.begin(),child,
            [&](const auto& point){return reference.owner_id==point.container_origin.id&&(reference.semantic_key=="point"||reference.semantic_key.starts_with("origin:"));});
        const bool parent_origin=child!=root->curve_points.end()&&reference.owner_id==root->container_origin.id&&reference.semantic_key.starts_with("origin:");
        for(auto it=root;it!=roots.end();++it)if(owns(*it,reference.owner_id)&&!(it==root&&(earlier_point||parent_origin)))
            reject("reference_not_available","A construction cannot reference itself or a later construction.");
        if(part) {
            const auto& doc=part->session.document();
            if(doc.find_container(reference.owner_id)) {
                const auto source=std::ranges::find_if(doc.history_order,[&](const auto& entry){return entry.id==reference.owner_id;});
                const auto target=std::ranges::find_if(doc.history_order,[&](const auto& entry){return entry.id==root->id;});
                if(source==doc.history_order.end()||target==doc.history_order.end()||source>=target)
                    reject("reference_not_available","A construction reference must precede it in model history.");
            }
        }
    }
    const auto geometry=placement_edit_geometry(live,id,object_id);
    value=prepare_construction_reference(std::move(value),geometry,index,std::move(reference),derive_orientation);
    if(value==*found)return false;
    return commit_construction(live,id,std::move(value),ConstructionEditMode::Replace);
}
}
