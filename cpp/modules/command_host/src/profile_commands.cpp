#include <zima/command_host/host.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <zima/document/metadata.hpp>
#include <cmath>
#include <cctype>
#include <algorithm>

namespace zima::command_host {
namespace {
using Feature = document::HistoryContainer;
using Kind = document::FeatureKind;
using Error = workspace::ProfileOperationError;
const Feature& profile(const workspace::PartState* state, const std::string& id, Kind kind) {
    if (!state) throw Error("unsupported_document", "Profile operations require an open Part.");
    const auto* value = state->session.document().find_container(id);
    if (!value) throw Error("container_not_found", "The requested container does not exist.");
    if (value->feature_kind != kind) throw Error("wrong_feature", "The requested profile type does not match the container.");
    return *value;
}
const char* extent(document::ProfileExtentMode mode) {
    return mode == document::ProfileExtentMode::OneSide ? "one_side" : mode == document::ProfileExtentMode::TwoSides ? "two_sides" : "symmetric";
}
const char* condition(document::EndCondition mode) {
    return mode == document::EndCondition::Length ? "length" : mode == document::EndCondition::UpTo ? "up_to" : "through_all";
}
Json targets(const std::vector<document::ExtrusionParameters::EndTarget>& values) {
    auto result=Json::array();
    for (const auto& target:values) result.push_back({{"kind",target.kind==document::EndTargetKind::Point?"point":target.kind==document::EndTargetKind::Plane?"plane":"face"},
        {"owner",target.reference.owner_id},{"key",target.reference.semantic_key},{"instance_path",target.reference.instance_path},{"label",target.label}});
    return result;
}
Json details(const workspace::PartState& state, const Feature& value) {
    const bool extrusion=value.feature_kind==Kind::Extrusion;
    const auto& doc=state.session.document();const auto* owner=doc.body_owner_for_object(value.id);
    const auto result_type=extrusion?value.extrusion.result_type:value.revolution.result_type;
    const auto thin_mode=extrusion?value.extrusion.thin_mode:value.revolution.thin_mode;
    const auto direction=extrusion?value.extrusion.direction:value.revolution.direction;
    Json result={{"document",doc.document_id},{"container",value.id},{"feature",value.feature_id},
        {"sketch",extrusion?value.extrusion.sketch_id:value.revolution.sketch_id},{"body",owner?owner->scope.id:std::string{}},
        {"name",value.name},{"kind",extrusion?"extrusion":"revolution"},{"revision",state.session.revision()},
        {"combine",value.combine_mode==document::CombineMode::Add?"add":"subtract"},
        {"profile_source",(extrusion?value.extrusion.profile_source:value.revolution.profile_source)==document::ProfileSource::Internal?"internal":"external"},
        {"profile_offset_mm",extrusion?value.extrusion.profile_plane_offset:value.revolution.profile_plane_offset},
        {"result_type",result_type==document::ProfileResultType::Solid?"solid":"thin"},
        {"thin_thickness_mm",extrusion?value.extrusion.thin_thickness:value.revolution.thin_thickness},
        {"thin_mode",thin_mode==document::ThinMode::OneSide?"one_side":thin_mode==document::ThinMode::OtherSide?"other_side":"symmetric"},
        {"extent",extent(extrusion?value.extrusion.extent_mode:value.revolution.extent_mode)},
        {"direction",direction==document::ExtrusionDirection::Reverse?"reverse":"forward"},
        {"value_locks",value.value_locks},{"reference_valid",value.placement.reference_valid}};
    if(extrusion) result.update({{"length_forward_mm",value.extrusion.length_forward},{"length_reverse_mm",value.extrusion.length_reverse},
        {"end_forward",condition(value.extrusion.end_condition_forward)},{"end_reverse",condition(value.extrusion.end_condition_reverse)},
        {"targets_forward",targets(value.extrusion.end_targets_forward)},{"targets_reverse",targets(value.extrusion.end_targets_reverse)}});
    else result.update({{"angle_degrees",value.revolution.angle_degrees},{"angle_reverse_degrees",value.revolution.angle_reverse},{"axis",value.revolution.axis_segment_id}});
    return result;
}
void properties(Feature& value, const Json& args, const workspace::Workspace& live, const std::string& document) {
    const bool extrusion=value.feature_kind==Kind::Extrusion;
    const auto choose=[&](const char* key, std::initializer_list<const char*> choices) {
        const auto text=args.at(key).get<std::string>();
        if(std::ranges::none_of(choices,[&](const auto* choice){return text==choice;}))
            throw Error("invalid_arguments","Unknown profile parameter option.");
        return text;
    };
    const auto number=[&](const char* key, double& field) {
        if(!args.contains(key))return;
        const auto v=args.at(key).get<double>();if(!std::isfinite(v))throw Error("invalid_arguments","Profile dimensions must be finite JSON numbers.");field=v;
    };
    const auto previous=value;
    if(args.contains("name")) {const auto name=args.at("name").get<std::string>();document::validate_native_metadata_text(name);
        if(name.empty()||std::ranges::all_of(name,[](unsigned char c){return std::isspace(c)!=0;}))throw Error("invalid_arguments","Specify a nonempty object name.");value.name=name;}
    if(args.contains("combine"))value.combine_mode=choose("combine",{"add","subtract"})=="add"?document::CombineMode::Add:document::CombineMode::Subtract;
    auto& result_type=extrusion?value.extrusion.result_type:value.revolution.result_type;
    if(args.contains("result_type"))result_type=choose("result_type",{"solid","thin"})=="solid"?document::ProfileResultType::Solid:document::ProfileResultType::Thin;
    auto& thin_mode=extrusion?value.extrusion.thin_mode:value.revolution.thin_mode;
    if(args.contains("thin_mode")){const auto mode=choose("thin_mode",{"one_side","other_side","symmetric"});thin_mode=mode=="one_side"?document::ThinMode::OneSide:mode=="other_side"?document::ThinMode::OtherSide:document::ThinMode::Symmetric;}
    auto& extent_mode=extrusion?value.extrusion.extent_mode:value.revolution.extent_mode;
    if(args.contains("extent")){const auto mode=choose("extent",{"one_side","two_sides","symmetric"});extent_mode=mode=="one_side"?document::ProfileExtentMode::OneSide:mode=="two_sides"?document::ProfileExtentMode::TwoSides:document::ProfileExtentMode::Symmetric;}
    auto& direction=extrusion?value.extrusion.direction:value.revolution.direction;
    if(args.contains("direction"))direction=choose("direction",{"forward","reverse"})=="forward"?document::ExtrusionDirection::Forward:document::ExtrusionDirection::Reverse;
    number("profile_offset_mm",extrusion?value.extrusion.profile_plane_offset:value.revolution.profile_plane_offset);
    number("thin_thickness_mm",extrusion?value.extrusion.thin_thickness:value.revolution.thin_thickness);
    auto& forward=extrusion?value.extrusion.length_forward:value.revolution.angle_degrees;
    auto& reverse=extrusion?value.extrusion.length_reverse:value.revolution.angle_reverse;
    number(extrusion?"length_forward_mm":"angle_degrees",forward);
    number(extrusion?"length_reverse_mm":"angle_reverse_degrees",reverse);
    if(extent_mode==document::ProfileExtentMode::Symmetric) {
        if(args.contains(extrusion?"length_reverse_mm":"angle_reverse_degrees") && reverse!=forward)
            throw Error("invalid_arguments","Symmetric profile extents must have equal forward and reverse values.");
        reverse=forward;
    }
    if(extrusion) {
        const auto set_end=[&](const char* key, document::EndCondition& field) {if(!args.contains(key))return;
            const auto mode=choose(key,{"length","up_to","through_all"});field=mode=="length"?document::EndCondition::Length:mode=="up_to"?document::EndCondition::UpTo:document::EndCondition::ThroughAll;};
        set_end("end_forward",value.extrusion.end_condition_forward);set_end("end_reverse",value.extrusion.end_condition_reverse);
        value.extrusion.height=extent_mode==document::ProfileExtentMode::OneSide?forward:forward+reverse;
    } else if(args.contains("axis")) {
        const auto id=args.at("axis").get<std::string>();const auto* part=live.open_part(document);
        const auto sketch=std::ranges::find(part->session.document().sketches,value.revolution.sketch_id,&sketcher::Sketch::id);
        if(sketch==part->session.document().sketches.end()||std::ranges::none_of(sketch->segments,[&](const auto& s){return s.id==id&&s.centerline&&s.construction;}))
            throw Error("invalid_reference","The revolution axis must identify a construction centerline in its own Sketch.");
        value.revolution.axis_segment_id=id;
    }
    const auto lock=[&](const char* key,double before,double after){if(before!=after&&value.value_locks.contains(key))throw Error("value_locked","Unlock the dimension before changing it.");};
    lock("profile_offset",extrusion?previous.extrusion.profile_plane_offset:previous.revolution.profile_plane_offset,extrusion?value.extrusion.profile_plane_offset:value.revolution.profile_plane_offset);
    lock("thin_thickness",extrusion?previous.extrusion.thin_thickness:previous.revolution.thin_thickness,extrusion?value.extrusion.thin_thickness:value.revolution.thin_thickness);
    lock("length_reverse",extrusion?previous.extrusion.length_reverse:previous.revolution.angle_reverse,reverse);
    lock(extrusion?"length_forward":"angle",extrusion?previous.extrusion.length_forward:previous.revolution.angle_degrees,forward);
    if(args.contains("placement")) {
        if(args.at("placement").empty())throw Error("invalid_arguments","Specify at least one placement parameter.");
        const auto geometry=workspace::placement_edit_geometry(live,document,value.id);
        for(const auto& [key,v]:args.at("placement").items()) {
            if(!v.is_number()||!std::isfinite(v.get<double>()))throw Error("invalid_arguments","Placement parameters must be finite JSON numbers.");
            if(!workspace::assign_placement_dimension(value.placement,geometry,key,v.get<double>()))
                throw Error("parameter_not_editable","The placement parameter is unknown, constrained or locked.");
        }
    }
}
}
void Host::register_profile_commands() {
    using Type=commands::ArgumentType;
    for(const auto kind:{Kind::Extrusion,Kind::Revolution}) {
        const bool extrusion=kind==Kind::Extrusion;const std::string prefix=extrusion?"extrusion":"revolution";
        dispatcher_.add({prefix+".get",tr("Read an Extrusion or Revolution and its owned profile without calculation."),{{"container",true},{"document",false}},false},[this,kind](const Json& args){
            try{const auto* state=workspace_.open_part(args.value("document",workspace_.active_document_id()));const auto& value=profile(state,args.at("container").get<std::string>(),kind);return Result::success(details(*state,value));}
            catch(const Error& e){return Result::failure(e.code,tr(e.what()));}
        });
        for(const bool create:{true,false}) {
            std::vector<commands::Argument> fields{{create?"sketch":"container",true},{"name",false},{"combine",false},
                {"result_type",false},{"thin_thickness_mm",false,Type::Number},{"thin_mode",false},{"extent",false},{"direction",false},
                {extrusion?"length_forward_mm":"angle_degrees",false,Type::Number},{extrusion?"length_reverse_mm":"angle_reverse_degrees",false,Type::Number},
                {"profile_offset_mm",false,Type::Number},{"placement",false,Type::Object},{"document",false}};
            if(extrusion){fields.push_back({"end_forward",false});fields.push_back({"end_reverse",false});}else fields.push_back({"axis",false});
            dispatcher_.add({prefix+(create?".create":".set"),create?tr("Convert a standalone Sketch to an Extrusion or Revolution in one transaction."):tr("Edit and calculate a profile feature through the shared Properties transaction."),std::move(fields),true},
                [this,create,kind](const Json& args){
                    const auto check=target(args);if(!check.ok)return check;
                    try {
                        const auto id=workspace_.active_document_id();auto* state=workspace_.open_part(id);
                        if(!state||interaction().template_document)throw Error("unsupported_document","Profile operations require an open Part.");
                        auto value=create?workspace::profile_from_sketch(state->session.document(),args.at("sketch").get<std::string>(),kind)
                            :profile(state,args.at("container").get<std::string>(),kind);
                        const auto* body=state->session.document().body_owner_for_object(value.id);
                        if(body&&body->scope.id!=state->session.document().body_history.active_body_id())throw Error("inactive_body","Activate the owning Body before editing its profile.");
                        properties(value,args,workspace_,id);const auto container=value.id;
                        workspace::commit_profile(workspace_,kernel_,id,std::move(value),create?workspace::ProfileEditMode::TransformSketch:workspace::ProfileEditMode::Replace);
                        change_=Change{ChangeKind::Model,id};auto result=details(*state,profile(state,container,kind));result["changed"]=true;return Result::success(std::move(result));
                    } catch(const Error& e){return Result::failure(e.code,tr(e.what()));}
                      catch(const workspace::PlacementEditError& e){return Result::failure(e.code,tr(e.what()));}
                      catch(const std::exception& e){return Result::failure("profile_rejected",tr(e.what()));}
                });
        }
    }
}
} // namespace zima::command_host
