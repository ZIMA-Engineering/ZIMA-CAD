#include "component_command_support.hpp"
#include <zima/workspace/component_properties.hpp>
#include <algorithm>
#include <cmath>
namespace zima::command_host {
namespace {
using Error=workspace::ComponentOperationError;
void keys(const Json& value,std::initializer_list<const char*> allowed) {
    if(!value.is_object())throw Error("invalid_arguments","Expected an object of component properties.");
    for(const auto& [key,field]:value.items())if(std::ranges::none_of(allowed,[&](const char* name){return key==name;}))
        throw Error("invalid_arguments","Unknown component property field.");
}
double number(const Json& value) {
    if(!value.is_number()||!std::isfinite(value.get<double>()))throw Error("invalid_arguments","Component dimensions must be finite JSON numbers.");
    return value.get<double>();
}
bool boolean(const Json& value) {
    if(!value.is_boolean())throw Error("invalid_arguments","Component flags must be JSON booleans.");
    return value.get<bool>();
}
assembly::MateReference reference(const Json& value,assembly::MateReferenceKind kind) {
    keys(value,{"owner","key","instance_path","kind"});
    for(const auto* key:{"owner","key","instance_path"})if(!value.contains(key)||!value.at(key).is_string())
        throw Error("invalid_arguments","A component reference requires owner, key and an explicit instance_path.");
    if(kind==assembly::MateReferenceKind::Axis && value.value("kind",std::string{})=="cylinder_face")kind=assembly::MateReferenceKind::CylinderFace;
    const auto expected=kind==assembly::MateReferenceKind::CylinderFace?"cylinder_face":kind==assembly::MateReferenceKind::Face?"face":kind==assembly::MateReferenceKind::Axis?"axis":"point";
    if(value.contains("kind")&&value.at("kind")!=expected)throw Error("invalid_reference","The reference kind does not match the component mate type.");
    assembly::MateReference result{kind,assembly::InstancePath::decode(value.at("instance_path").get<std::string>()),value.at("owner"),value.at("key")};
    if(result.owner_id.empty()||result.semantic_key.empty())throw Error("invalid_reference","A component reference requires owner, key and an explicit instance_path.");
    return result;
}
std::vector<assembly::ComponentPlacementReference> references(const Json& values,const workspace::ComponentProperties& before) {
    using namespace assembly;
    if(values.size()>3)throw Error("invalid_arguments","A component accepts at most three placement reference rows.");
    std::vector<ComponentPlacementReference> result;
    for(const auto& value:values) {
        keys(value,{"kind","component","target","offset","flip","locked","lower_limit","upper_limit"});
        if(!value.contains("kind")||!value.contains("component")||!value.contains("target"))
            throw Error("invalid_arguments","Each placement row requires kind, component and target.");
        ComponentPlacementReference row;
        if(value.at("kind")=="plane_coincident")row.mate_type=MateKind::PlaneCoincident;
        else if(value.at("kind")=="plane_angle")row.mate_type=MateKind::PlaneAngle;
        else if(value.at("kind")=="axis_coincident")row.mate_type=MateKind::AxisCoincident;
        else if(value.at("kind")=="point_coincident")row.mate_type=MateKind::PointCoincident;
        else throw Error("invalid_arguments","Unknown component mate type.");
        const auto kind=row.mate_type==MateKind::AxisCoincident?MateReferenceKind::Axis:row.mate_type==MateKind::PointCoincident?MateReferenceKind::Point:MateReferenceKind::Face;
        row.offset_locked=kind!=MateReferenceKind::Face;
        row.component_reference=reference(value.at("component"),kind);row.target_reference=reference(value.at("target"),kind);
        const auto same=[&](const auto& old){return old.mate_type==row.mate_type&&old.component_reference==row.component_reference&&old.target_reference==row.target_reference;};
        const auto stored=std::ranges::find_if(before.references,same);
        if(stored!=before.references.end())row=*stored;
        if(value.contains("offset"))row.offset=number(value.at("offset"));
        if(value.contains("flip"))row.flip=boolean(value.at("flip"));
        if(value.contains("locked"))row.offset_locked=boolean(value.at("locked"));
        if(value.contains("lower_limit"))row.lower_limit=value.at("lower_limit").is_null()?std::nullopt:std::optional<double>(number(value.at("lower_limit")));
        if(value.contains("upper_limit"))row.upper_limit=value.at("upper_limit").is_null()?std::nullopt:std::optional<double>(number(value.at("upper_limit")));
        if(stored!=before.references.end()&&stored->offset_locked&&row.offset_locked&&stored->offset!=row.offset)
            throw Error("value_locked","Unlock the dimension before changing it.");
        result.push_back(std::move(row));
    }
    return result;
}
}
void Host::register_component_property_commands() {
    using Type=commands::ArgumentType;
    dispatcher_.add({"component.set",tr("Edit an owned component's properties and placement references through the shared Properties transaction."),
        {{"instance_path",true},{"name",false},{"visible",false,Type::Boolean},{"suppressed",false,Type::Boolean},{"grounded",false,Type::Boolean},
         {"placement",false,Type::Object},{"placement_references",false,Type::Array},{"document",false}},true},[this](const Json& args) {
        const auto check=target(args);if(!check.ok)return check;
        if(interaction().template_document)return Result::failure("unsupported_document",tr("Component commands require an open Assembly."));
        try {
            if(args.size()==1+(args.contains("document")?1:0))throw Error("invalid_arguments","Specify at least one component property.");
            const auto path=assembly::InstancePath::decode(args.at("instance_path").get<std::string>());
            if(path.occurrence_ids.size()!=1)throw Error("unsupported_context","Edit a component only in its immediate owning Assembly.");
            const auto id=workspace_.active_document_id();const auto edit=workspace::prepare_component_edit(workspace_,id,path.occurrence_ids.front());auto value=edit.initial;
            if(args.contains("name"))value.name=args.at("name").get<std::string>();
            if(args.contains("visible"))value.visible=args.at("visible").get<bool>();
            if(args.contains("suppressed"))value.suppressed=args.at("suppressed").get<bool>();
            if(args.contains("grounded"))value.grounded=args.at("grounded").get<bool>();
            if(args.contains("placement_references"))value.references=references(args.at("placement_references"),edit.initial);
            if(args.contains("placement")) {
                std::map<std::string,double> patch;for(const auto& [key,n]:args.at("placement").items())patch.emplace(key,number(n));
                workspace::assign_component_coordinates(workspace_,edit,value,patch);
            }
            const bool changed=workspace::commit_component_properties(workspace_,edit,value);
            const auto* state=workspace_.open_assembly(id);auto result=component_details(state->session.document(),path);
            result["document"]=id;result["revision"]=state->session.revision();result["changed"]=changed;
            if(changed)change_=Change{ChangeKind::Model,id};
            return Result::success(std::move(result));
        }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
         catch(const std::exception& error){return Result::failure("component_properties_rejected",tr(error.what()));}
    });
}
} // namespace zima::command_host
