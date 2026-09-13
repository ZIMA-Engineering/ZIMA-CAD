#include "opening_target_input.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/hole_operations.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <zima/document/metadata.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
namespace zima::command_host {
namespace {
using Error=workspace::HoleOperationError;
using Feature=document::HistoryContainer;
const Feature& hole(const workspace::PartState* state,const std::string& id) {
    if(!state)throw Error("unsupported_document","Hole operations require an open Part.");
    const auto* value=state->session.document().find_container(id);
    if(!value)throw Error("container_not_found","The requested container does not exist.");
    if(value->feature_kind!=document::FeatureKind::Hole)throw Error("wrong_feature","This container is not a native Hole.");
    return *value;
}
const char* end_name(document::EndCondition end) {
    return end==document::EndCondition::Length?"length":end==document::EndCondition::ThroughAll?"through_all":"up_to";
}
Json details(const workspace::PartState& state,const Feature& f) {
    const auto& h=f.hole;const auto& doc=state.session.document();const auto* body=doc.body_owner_for_object(f.id);
    Json result={{"document",doc.document_id},{"container",f.id},{"feature",f.feature_id},{"name",f.name},
        {"body",body?body->scope.id:std::string{}},{"type",h.type==document::HoleType::Plain?"plain":h.type==document::HoleType::MetricThread?"metric":h.type==document::HoleType::PipeThread?"pipe":"whitworth"},
        {"diameter_mm",h.diameter},{"bore_length_mm",h.bore_length},{"bore_end",end_name(h.bore_end_condition)},
        {"entrance_chamfer_mm",h.entrance_chamfer},{"exit_chamfer_mm",h.exit_chamfer},{"exit_chamfer_enabled",h.exit_chamfer_enabled},
        {"drill_point_enabled",h.drill_point_enabled},{"drill_point_angle_degrees",h.drill_point_angle_degrees},
        {"thread_enabled",h.thread_enabled},{"thread_diameter_mm",h.thread_nominal_diameter},{"thread_pitch_mm",h.thread_pitch},
        {"thread_length_mm",h.thread_length},{"thread_end",end_name(h.thread_end_condition)},{"left_handed",h.left_hand_thread},
        {"bore_sketch",h.sketch_id},{"bore_circle",h.circle_id},{"chamfer_sketch",h.chamfer_sketch_id},{"tip_sketch",h.tip_sketch_id},
        {"value_locks",f.value_locks},{"reference_valid",f.placement.reference_valid},{"revision",state.session.revision()}};
    result["bore_targets"]=Json::array();
    for(const auto& target:h.bore_end_targets)result["bore_targets"].push_back({{"owner",target.reference.owner_id},
        {"key",target.reference.semantic_key},{"instance_path",target.reference.instance_path},
        {"kind",target.kind==document::EndTargetKind::Plane?"plane":target.kind==document::EndTargetKind::Point?"point":"face"}});
    return result;
}
void properties(Feature& feature,const Json& args,const workspace::Workspace& live,const std::string& id,bool create) {
    const auto previous=feature;auto& h=feature.hole;
    if(args.contains("name")) {
        const auto name=args.at("name").get<std::string>();document::validate_native_metadata_text(name);
        if(name.empty()||std::ranges::all_of(name,[](unsigned char c){return std::isspace(c)!=0;}))throw Error("invalid_arguments","Specify a nonempty object name.");
        feature.name=name;
    }
    if(args.contains("type")) {
        const auto type=args.at("type").get<std::string>();
        if(type!="plain"&&type!="metric"&&type!="pipe"&&type!="whitworth")throw Error("invalid_arguments","Unknown Hole type.");
        h.type=type=="plain"?document::HoleType::Plain:type=="metric"?document::HoleType::MetricThread:type=="pipe"?document::HoleType::PipeThread:document::HoleType::WhitworthThread;
    }
    const auto number=[&](const char* key,double& field){if(args.contains(key))field=args.at(key).get<double>();};
    const auto flag=[&](const char* key,bool& field){if(args.contains(key))field=args.at(key).get<bool>();};
    number("diameter_mm",h.diameter);number("bore_length_mm",h.bore_length);number("entrance_chamfer_mm",h.entrance_chamfer);
    number("exit_chamfer_mm",h.exit_chamfer);number("drill_point_angle_degrees",h.drill_point_angle_degrees);
    number("thread_diameter_mm",h.thread_nominal_diameter);number("thread_pitch_mm",h.thread_pitch);number("thread_length_mm",h.thread_length);
    flag("exit_chamfer_enabled",h.exit_chamfer_enabled);flag("drill_point_enabled",h.drill_point_enabled);
    flag("thread_enabled",h.thread_enabled);flag("left_handed",h.left_hand_thread);
    const auto end=[&](const char* key,document::EndCondition& field) {
        if(!args.contains(key))return;const auto text=args.at(key).get<std::string>();
        const bool bore=std::string_view(key)=="bore_end";
        if(text!="length"&&text!="through_all"&&(!bore||text!="up_to"))
            throw Error("unsupported_end_condition",bore?"Unknown Hole end condition.":"Native Hole thread endings support length and through_all.");
        field=text=="length"?document::EndCondition::Length:text=="up_to"?document::EndCondition::UpTo:document::EndCondition::ThroughAll;
    };
    end("bore_end",h.bore_end_condition);end("thread_end",h.thread_end_condition);
    if(args.contains("bore_targets"))h.bore_end_targets=opening_target_input<Error>(args.at("bore_targets"));
    // Same effective states as the Hole properties dialog.
    if(h.type!=document::HoleType::Plain)h.thread_enabled=true;
    if(h.drill_point_enabled)h.exit_chamfer_enabled=false;
    if(h.thread_end_condition==document::EndCondition::ThroughAll)h.thread_length=h.bore_length;
    if(args.contains("placement")) {
        const auto& patch=args.at("placement");if(patch.empty())throw Error("invalid_arguments","Specify at least one placement parameter.");
        const auto* state=live.open_part(id);
        const auto geometry=workspace::placement_edit_geometry(live,id,create?state->session.document().body_history.active_body_id():feature.id);
        for(const auto& [key,number]:patch.items()) {
            if(!number.is_number()||!std::isfinite(number.get<double>()))throw Error("invalid_arguments","Placement parameters must be finite JSON numbers.");
            if(!workspace::assign_placement_dimension(feature.placement,geometry,key,number.get<double>()))throw Error("parameter_not_editable","The placement parameter is unknown, constrained or locked.");
        }
    }
    const auto old=workspace::hole_dimensions(previous),now=workspace::hole_dimensions(feature);
    for(std::size_t i=0;i<old.size();++i)if(old[i].second!=now[i].second&&previous.value_locks.contains(old[i].first))
        throw Error("value_locked","Unlock the dimension before changing it.");
}
}
void Host::register_hole_commands() {
    using Type=commands::ArgumentType;
    dispatcher_.add({"hole.get",tr("Read native Hole properties and owned profile identities without calculating geometry."),
        {{"container",true},{"document",false}},false},[this](const Json& args) {
        try {const auto* state=workspace_.open_part(args.value("document",workspace_.active_document_id()));
            const auto& feature=hole(state,args.at("container"));
            return Result::success(details(*state,feature));
        }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
    });
    for(const bool create:{true,false}) {
        std::vector<commands::Argument> arguments;
        if(!create)arguments.push_back({"container",true});
        for(const auto* key:{"name","type","bore_end","thread_end"})arguments.push_back({key,false});
        for(const auto* key:{"diameter_mm","bore_length_mm","entrance_chamfer_mm","exit_chamfer_mm","drill_point_angle_degrees",
                "thread_diameter_mm","thread_pitch_mm","thread_length_mm"})arguments.push_back({key,false,Type::Number});
        for(const auto* key:{"thread_enabled","drill_point_enabled","exit_chamfer_enabled","left_handed"})arguments.push_back({key,false,Type::Boolean});
        arguments.push_back({"bore_targets",false,Type::Array});
        arguments.push_back({"placement",false,Type::Object});arguments.push_back({"document",false});
        dispatcher_.add({create?"hole.create":"hole.set",create?tr("Create a native Hole with owned bore, chamfer and tip Sketches."):
            tr("Change native Hole dimensions with the same transaction as Properties OK."),std::move(arguments),true},[this,create](const Json& args) {
            const auto check=target(args);if(!check.ok)return check;const auto id=workspace_.active_document_id();
            try {
                const auto* state=workspace_.open_part(id);
                if(!state||interaction().template_document)throw Error("unsupported_document","Hole operations require an open Part.");
                if(!create&&args.size()==1+(args.contains("document")?1:0))throw Error("invalid_arguments","Specify at least one Hole parameter.");
                auto feature=create?document::PartDocument::create_hole_container():hole(state,args.at("container"));
                if(create)feature.name=tr("Otvor");
                properties(feature,args,workspace_,id,create);const auto container=feature.id;
                const bool changed=workspace::commit_hole(workspace_,kernel_,id,std::move(feature),create?workspace::HoleEditMode::Create:workspace::HoleEditMode::Replace);
                if(changed)change_=Change{ChangeKind::Model,id};
                const auto* committed=workspace_.open_part(id);
                auto result=details(*committed,hole(committed,container));result["changed"]=changed;return Result::success(std::move(result));
            }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
             catch(const workspace::PlacementEditError& error){return Result::failure(error.code,tr(error.what()));}
             catch(const std::exception& error){return Result::failure("hole_rejected",tr(error.what()));}
        });
    }
}
}
