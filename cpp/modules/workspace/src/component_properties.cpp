#include <zima/workspace/component_properties.hpp>
#include <zima/document/metadata.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
namespace zima::workspace {
namespace {
using Error=ComponentOperationError;
std::array<double,6> coordinates(const assembly::ComponentPlacement& p){return {p.x,p.y,p.z,p.rotation_x,p.rotation_y,p.rotation_z};}
void assign(assembly::PartOccurrence& target,const ComponentProperties& value) {
    target.name=value.name;target.placement=value.placement;target.placement_references=value.references;
    target.value_locks=value.value_locks;target.visible=value.visible;target.suppressed=value.suppressed;target.grounded=value.grounded;
}
void validate_values(const ComponentEdit& edit,const ComponentProperties& value) {
    document::validate_native_metadata_text(value.name);
    if(value.name.empty()||std::ranges::all_of(value.name,[](unsigned char c){return std::isspace(c)!=0;}))
        throw Error("invalid_arguments","Specify a nonempty object name.");
    for(double n:coordinates(value.placement))if(!std::isfinite(n))throw Error("invalid_arguments","Placement value must be finite.");
    if(edit.derived&&(value.placement!=edit.initial.placement||value.references!=edit.initial.references||value.grounded!=edit.initial.grounded))
        throw Error("read_only_copy","Edit derived component placement through Mirror or Pattern properties.");
    if(value.references.size()>3)throw Error("invalid_arguments","A component accepts at most three placement reference rows.");
    for(std::size_t i=0;i<value.references.size();++i) {
        const auto& row=value.references[i];
        if(i<edit.initial.references.size()&&row==edit.initial.references[i])continue;
        const bool angular=row.mate_type==assembly::MateKind::PlaneAngle;
        const bool planar=angular||row.mate_type==assembly::MateKind::PlaneCoincident;
        if(!planar&&row.mate_type!=assembly::MateKind::AxisCoincident&&row.mate_type!=assembly::MateKind::PointCoincident)
            throw Error("invalid_arguments","Unknown component mate type.");
        const double limit=angular?180:1e9;
        for(auto n:{std::optional<double>(row.offset),row.lower_limit,row.upper_limit})
            if(n&&(!std::isfinite(*n)||std::abs(*n)>limit))throw Error("invalid_arguments","Component mate value is outside the supported range.");
        if((row.lower_limit&&row.offset<*row.lower_limit)||(row.upper_limit&&row.offset>*row.upper_limit))
            throw Error("invalid_arguments","Aktuální hodnota musí ležet v zadaných mezích.");
        if(!planar&&row.offset!=0)
            throw Error("invalid_arguments","Axis and point coincidence require a zero distance.");
    }
}
void validate_references(const assembly::AssemblyDocument& doc,const ComponentEdit& edit,const ComponentProperties& value) {
    using namespace assembly;
    for(std::size_t i=0;i<value.references.size();++i) {
        const auto& row=value.references[i];
        if(i<edit.initial.references.size()&&row==edit.initial.references[i])continue;
        const auto& moving=row.component_reference.instance_path.occurrence_ids;
        const auto& target=row.target_reference.instance_path.occurrence_ids;
        if(moving.empty()||moving.front()!=edit.occurrence_id||(!target.empty()&&target.front()==edit.occurrence_id))
            throw Error("invalid_reference","Place only the owning component against an independent target.");
        const auto valid=[&](const MateReference& ref){
            if(row.mate_type==MateKind::PointCoincident)return ref.kind==MateReferenceKind::Point&&doc.resolve_point(ref).status==MateStatus::Valid;
            if(row.mate_type==MateKind::AxisCoincident)return ref.kind==MateReferenceKind::Axis&&doc.resolve_axis(ref).status==MateStatus::Valid;
            return ref.kind==MateReferenceKind::Face&&doc.resolve_plane(ref).status==MateStatus::Valid;
        };
        if(!valid(row.component_reference)||!valid(row.target_reference))
            throw Error("missing_reference","Select existing original geometry for both component mate references.");
    }
    if(value.references==edit.initial.references)return;
    DependencyGraph graph;
    for(const auto& dependency:doc.dependencies)graph.add_dependency(dependency.dependent_occurrence_id,dependency.prerequisite_occurrence_id);
    for(const auto& component:doc.components)for(const auto& row:component.placement_references) {
        const auto& target=row.target_reference.instance_path.occurrence_ids;
        if(!target.empty())graph.add_dependency(component.occurrence_id,target.front());
    }
}
}
ComponentProperties component_properties(const assembly::PartOccurrence& value) {
    return {value.name,value.placement,value.placement_references,value.value_locks,value.visible,value.suppressed,value.grounded};
}
ComponentEdit prepare_component_edit(const Workspace& live,const std::string& id,const std::string& occurrence) {
    const auto* state=live.open_assembly(id);
    if(!state)throw Error("unsupported_document","Component commands require an open Assembly.");
    const auto* value=state->session.document().find_occurrence(occurrence);
    if(!value)throw Error("occurrence_not_found","The requested component occurrence does not exist.");
    return {id,occurrence,state->session.revision(),component_properties(*value),value->derived_copy.has_value()};
}
void assign_component_coordinates(const Workspace& live,const ComponentEdit& edit,ComponentProperties& value,const std::map<std::string,double>& patch) {
    if(patch.empty())throw Error("invalid_arguments","Specify at least one placement parameter.");
    if(edit.derived)throw Error("read_only_copy","Edit derived component placement through Mirror or Pattern properties.");
    const std::array<std::string,6> keys{"x_mm","y_mm","z_mm","rotation_x_deg","rotation_y_deg","rotation_z_deg"};
    const std::array<std::string,6> locks{"placement:x","placement:y","placement:z","placement:rotation_x","placement:rotation_y","placement:rotation_z"};
    auto doc=live.open_assembly(edit.document_id)->session.document();assign(*doc.find_occurrence(edit.occurrence_id),value);
    const auto freedom=doc.component_constraint_state(edit.occurrence_id);auto numbers=coordinates(value.placement);
    for(const auto& [key,number]:patch) {
        const auto found=std::ranges::find(keys,key);
        if(found==keys.end())throw Error("unknown_parameter","Unknown component placement parameter.");
        const auto index=static_cast<std::size_t>(found-keys.begin());
        if(!std::isfinite(number)||std::abs(number)>(index<3?1e6:180))throw Error("invalid_arguments","Component placement value is outside the supported range.");
        if(number==numbers[index])continue;
        if(value.value_locks.contains(locks[index]))throw Error("value_locked","Unlock the dimension before changing it.");
        if(!freedom.coordinate_free[index])throw Error("constrained_coordinate","This component coordinate is fixed by grounding or its placement references.");
        numbers[index]=number;
    }
    value.placement={numbers[0],numbers[1],numbers[2],numbers[3],numbers[4],numbers[5]};
}
bool commit_component_properties(Workspace& live,const ComponentEdit& edit,const ComponentProperties& value) {
    auto* state=live.open_assembly(edit.document_id);
    if(!state||state->session.revision()!=edit.revision)throw Error("document_changed","The document changed while component properties were open.");
    validate_values(edit,value);
    if(value==edit.initial)return false;
    const bool solve=value.placement!=edit.initial.placement||value.references!=edit.initial.references||value.grounded!=edit.initial.grounded||value.suppressed!=edit.initial.suppressed;
    if(solve)live.refresh_source_geometry();
    auto next=state->session.document();assign(*next.find_occurrence(edit.occurrence_id),value);
    validate_references(next,edit,value);
    if(solve)next.calculate_placement_references();
    state->session.commit(std::move(next));return true;
}
} // namespace zima::workspace
