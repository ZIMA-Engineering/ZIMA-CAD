#include <zima/command_host/host.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/profile_reference_operations.hpp>
#include <zima/workspace/sketch_operations.hpp>
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
const Feature& profile(const workspace::Workspace& live, const std::string& document, const std::string& id, Kind kind) {
    const Feature* value = nullptr;
    if (const auto* state = live.open_part(document)) value = state->session.document().find_container(id);
    else if (const auto* state = live.open_assembly(document)) {
        const auto* cut = state->session.document().find_cut(id);
        if (cut) value = &cut->definition;
    } else throw Error("unsupported_document", "Profile operations require an open Part or Assembly.");
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
Json details(const workspace::Workspace& live, const std::string& id, const Feature& value) {
    const bool extrusion=value.feature_kind==Kind::Extrusion;
    const auto* part=live.open_part(id);const auto* assembly=live.open_assembly(id);
    const auto* owner=part?part->session.document().body_owner_for_object(value.id):nullptr;
    const auto result_type=extrusion?value.extrusion.result_type:value.revolution.result_type;
    const auto thin_mode=extrusion?value.extrusion.thin_mode:value.revolution.thin_mode;
    const auto direction=extrusion?value.extrusion.direction:value.revolution.direction;
    Json result={{"document",id},{"container",value.id},{"feature",value.feature_id},
        {"sketch",extrusion?value.extrusion.sketch_id:value.revolution.sketch_id},{"body",owner?owner->scope.id:std::string{}},
        {"name",value.name},{"kind",extrusion?"extrusion":"revolution"},{"revision",part?part->session.revision():assembly->session.revision()},
        {"combine",value.combine_mode==document::CombineMode::Add?"add":"subtract"},
        {"profile_source",(extrusion?value.extrusion.profile_source:value.revolution.profile_source)==document::ProfileSource::Internal?"internal":"external"},
        {"profile_offset_mm",extrusion?value.extrusion.profile_plane_offset:value.revolution.profile_plane_offset},
        {"result_type",result_type==document::ProfileResultType::Solid?"solid":result_type==document::ProfileResultType::Surface?"surface":"thin"},
        {"thin_thickness_mm",extrusion?value.extrusion.thin_thickness:value.revolution.thin_thickness},
        {"thin_mode",thin_mode==document::ThinMode::OneSide?"one_side":thin_mode==document::ThinMode::OtherSide?"other_side":"symmetric"},
        {"extent",extent(extrusion?value.extrusion.extent_mode:value.revolution.extent_mode)},
        {"direction",direction==document::ExtrusionDirection::Reverse?"reverse":"forward"},
        {"value_locks",value.value_locks},{"reference_valid",value.placement.reference_valid}};
    const auto& sketch_id=extrusion?value.extrusion.sketch_id:value.revolution.sketch_id;
    const auto& sketches=part?part->session.document().sketches:assembly->session.document().sketches;
    const auto sketch=std::ranges::find(sketches,sketch_id,&sketcher::Sketch::id);
    if(sketch!=sketches.end()) {
        result["profile_plane_auto"]=sketch->plane_auto;
        result["profile_plane"]=sketch->plane==sketcher::SketchPlane::XY?"XY":sketch->plane==sketcher::SketchPlane::XZ?"XZ":"YZ";
    }
    if(extrusion) result.update({{"sheet_cut",value.extrusion.sheet_cut},{"sheet_cut_clearance",value.extrusion.sheet_cut_clearance},{"length_forward_mm",value.extrusion.length_forward},{"length_reverse_mm",value.extrusion.length_reverse},
        {"end_forward",condition(value.extrusion.end_condition_forward)},{"end_reverse",condition(value.extrusion.end_condition_reverse)},
        {"targets_forward",targets(value.extrusion.end_targets_forward)},{"targets_reverse",targets(value.extrusion.end_targets_reverse)}});
    else result.update({{"angle_degrees",value.revolution.angle_degrees},{"angle_reverse_degrees",value.revolution.angle_reverse},{"axis",value.revolution.axis_segment_id},
        {"sheet_metal",value.revolution.sheet_metal},{"sheet_attachment",value.revolution.sheet_attachment},{"thickness_override",value.revolution.thickness_override}});
    if (assembly) {
        result["targets"] = assembly->session.document().find_cut(value.id)->target_occurrence_ids;
        result["suppressed"] = value.suppressed;
    }
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
    if(extrusion&&args.contains("sheet_cut")) {
        value.extrusion.sheet_cut=args.at("sheet_cut");
        if(value.extrusion.sheet_cut) {
            value.combine_mode=document::CombineMode::Subtract;
            value.extrusion.result_type=document::ProfileResultType::Solid;
        }
    }
    if(extrusion&&args.contains("sheet_cut_clearance"))
        value.extrusion.sheet_cut_clearance=args.at("sheet_cut_clearance");
    if(!extrusion) {
        if(args.contains("sheet_metal")) {
            value.revolution.sheet_metal=args.at("sheet_metal");
            if(value.revolution.sheet_metal)value.revolution.result_type=document::ProfileResultType::Thin;
        }
        if(value.revolution.sheet_metal&&args.contains("thin_thickness_mm"))value.revolution.thickness_override=true;
        if(args.contains("thickness_override"))value.revolution.thickness_override=args.at("thickness_override");
    }
    if(args.contains("name")) {const auto name=args.at("name").get<std::string>();document::validate_native_metadata_text(name);
        if(name.empty()||std::ranges::all_of(name,[](unsigned char c){return std::isspace(c)!=0;}))throw Error("invalid_arguments","Specify a nonempty object name.");value.name=name;}
    if(args.contains("combine"))value.combine_mode=choose("combine",{"add","subtract"})=="add"?document::CombineMode::Add:document::CombineMode::Subtract;
    auto& result_type=extrusion?value.extrusion.result_type:value.revolution.result_type;
    if(args.contains("result_type")){const auto type=choose("result_type",{"solid","thin","surface"});result_type=type=="solid"?document::ProfileResultType::Solid:type=="surface"?document::ProfileResultType::Surface:document::ProfileResultType::Thin;}
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
        const auto set_targets=[&](const char* key,auto& targets) {
            if(!args.contains(key))return;
            const auto& input=args.at(key);
            if(input.size()>1)throw Error("invalid_arguments","Each extrusion end accepts one target reference.");
            targets.clear();
            for(const auto& item:input) {
                if(!item.is_object() || !item.contains("owner") || !item.contains("key"))throw Error("invalid_arguments","A target reference requires owner and key.");
                for(const auto& [name,v]:item.items())if((name!="owner"&&name!="key"&&name!="instance_path"&&name!="kind"&&name!="label") || !v.is_string())
                    throw Error("invalid_arguments","Target reference fields must be supported text fields.");
                const auto kind=item.value("kind",std::string("face"));
                if(kind!="face"&&kind!="plane")throw Error("invalid_reference","Extrusion end references require a plane or an original face.");
                document::ExtrusionParameters::EndTarget target;
                target.kind=document::EndTargetKind::Face;
                target.reference={item.at("owner").get<std::string>(),item.at("key").get<std::string>(),item.value("instance_path",std::string{})};
                target.label=item.value("label",target.reference.owner_id+" / "+target.reference.semantic_key);
                document::validate_native_metadata_text(target.label);targets.push_back(std::move(target));
            }
        };
        set_targets("targets_forward",value.extrusion.end_targets_forward);set_targets("targets_reverse",value.extrusion.end_targets_reverse);
        value.extrusion.height=extent_mode==document::ProfileExtentMode::OneSide?forward:forward+reverse;
    } else if(args.contains("axis")) {
        const auto id=args.at("axis").get<std::string>();const auto* part=live.open_part(document);
        const auto& sketches=part?part->session.document().sketches:live.open_assembly(document)->session.document().sketches;
        const auto sketch=std::ranges::find(sketches,value.revolution.sketch_id,&sketcher::Sketch::id);
        if(sketch==sketches.end()||std::ranges::none_of(sketch->segments,[&](const auto& s){return s.id==id&&s.centerline&&s.construction;}))
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
    dispatcher_.add({"assembly.cut.list",tr("List Assembly profile cuts without calculation."),{{"document",false}},false},[this](const Json& args){
        const auto id=args.value("document",workspace_.active_document_id());const auto* state=workspace_.open_assembly(id);
        if(!state)return Result::failure("unsupported_document",tr("Cut operations require an open Assembly."));
        auto items=Json::array();for(const auto& cut:state->session.document().cuts)items.push_back(details(workspace_,id,cut.definition));
        return Result::success({{"document",id},{"items",std::move(items)},{"total",state->session.document().cuts.size()}});
    });
    for(const auto kind:{Kind::Extrusion,Kind::Revolution}) {
        const bool extrusion=kind==Kind::Extrusion;const std::string prefix=extrusion?"extrusion":"revolution";
        dispatcher_.add({prefix+".get",tr("Read an Extrusion or Revolution and its owned profile without calculation."),{{"container",true},{"document",false}},false},[this,kind](const Json& args){
            try{const auto id=args.value("document",workspace_.active_document_id());const auto& value=profile(workspace_,id,args.at("container").get<std::string>(),kind);return Result::success(details(workspace_,id,value));}
            catch(const Error& e){return Result::failure(e.code,tr(e.what()));}
        });
        dispatcher_.add({prefix+".reference.set",tr("Assign an original reference through the supported shared placement and feature transactions."),
            {{"container",true},{"index",true,Type::Integer},{"reference",true,Type::Object},{"offset_mm",false,Type::Number},
             {"flip",false,Type::Boolean},{"derive_orientation",false,Type::Boolean},{"document",false}},true},[this,kind](const Json& args) {
            const auto checked=target(args);if(!checked.ok)return checked;
            if(interaction().template_document)return Result::failure("unsupported_document",tr("Profile operations require an open Part or Assembly."));
            try {
                const auto& ref=args.at("reference");
                const auto invalid=[](){throw Error("invalid_arguments","Specify owner, key and an optional instance_path for the placement reference.");};
                for(const auto& [key,item]:ref.items())if((key!="owner"&&key!="key"&&key!="instance_path")||!item.is_string())invalid();
                if(!ref.contains("owner")||!ref.contains("key")||args.at("index")<0||args.at("index")>4)invalid();
                const auto id=workspace_.active_document_id(),container=args.at("container").get<std::string>();static_cast<void>(profile(workspace_,id,container,kind));
                document::ConstructionReference source;source.owner_id=ref.at("owner");source.semantic_key=ref.at("key");source.instance_path=ref.value("instance_path",std::string{});
                source.offset=args.value("offset_mm",0.0);source.flip=args.value("flip",false);
                const auto changed=workspace::set_profile_reference(workspace_,kernel_,id,container,args.at("index").get<std::size_t>(),std::move(source),args.value("derive_orientation",true));
                if(changed)change_=Change{ChangeKind::Model,id};auto result=details(workspace_,id,profile(workspace_,id,container,kind));result["changed"]=changed;return Result::success(std::move(result));
            }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
             catch(const workspace::PlacementEditError& error){return Result::failure(error.code,tr(error.what()));}
             catch(const std::exception& error){return Result::failure("profile_rejected",tr(error.what()));}
        });
        dispatcher_.add({prefix+".sketch.edit",tr("Edit an owned profile Sketch and calculate its feature in one atomic batch."),
            {{"container",true},{"operations",true,Type::Array},{"document",false}},true},[this,kind](const Json& args) {
            const auto checked=target(args);if(!checked.ok)return checked;
            if(interaction().template_document)return Result::failure("unsupported_document",tr("Profile operations require an open Part or Assembly."));
            const auto& operations=args.at("operations");
            if(operations.empty()||operations.size()>1000)return Result::failure("invalid_arguments",tr("A profile Sketch batch requires 1 to 1000 operations."));
            try {
                const auto id=workspace_.active_document_id(),container=args.at("container").get<std::string>();
                auto value=profile(workspace_,id,container,kind);
                auto* part=workspace_.open_part(id);
                if(part) {
                    const auto* body=part->session.document().body_owner_for_object(container);
                    if(body&&body->derived_copy)throw Error("read_only_body","A derived Body cannot be edited directly.");
                    if(body&&body->scope.id!=part->session.document().body_history.active_body_id())throw Error("inactive_body","Activate the owning Body before editing its profile.");
                }
                const auto sketch_id=kind==Kind::Extrusion?value.extrusion.sketch_id:value.revolution.sketch_id;
                auto draft=workspace::document_sketch(workspace_,id,sketch_id);
                const auto before=draft.serialized();const auto batch=sketch_draft_dispatcher(draft);auto results=Json::array();
                for(std::size_t i=0;i<operations.size();++i) {
                    auto result=batch.execute(operations[i]);
                    if(!result.ok){result.data={{"operation_index",i}};return result;}
                    results.push_back(std::move(result.data));
                }
                const bool changed=before!=draft.serialized();
                if(changed) {
                    if(part) workspace::commit_profile(workspace_,kernel_,id,std::move(value),workspace::ProfileEditMode::Replace,draft);
                    else {
                        const auto targets=workspace_.open_assembly(id)->session.document().find_cut(container)->target_occurrence_ids;
                        workspace::commit_assembly_profile(workspace_,kernel_,id,std::move(value),targets,workspace::ProfileEditMode::Replace,draft);
                    }
                    change_=Change{ChangeKind::Model,id,true};
                }
                auto result=details(workspace_,id,profile(workspace_,id,container,kind));
                result["changed"]=changed;result["body_calculated"]=changed;result["results"]=std::move(results);
                return Result::success(std::move(result));
            } catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
              catch(const workspace::SketchOperationError& error){return Result::failure(error.code,tr(error.what()));}
              catch(const std::exception& error){return Result::failure("profile_rejected",tr(error.what()));}
        });
        for(const bool create:{true,false}) {
            std::vector<commands::Argument> fields{{create?"sketch":"container",true},{"name",false},{"combine",false},
                {"result_type",false},{"thin_thickness_mm",false,Type::Number},{"thin_mode",false},{"extent",false},{"direction",false},
                {extrusion?"length_forward_mm":"angle_degrees",false,Type::Number},{extrusion?"length_reverse_mm":"angle_reverse_degrees",false,Type::Number},
                {"profile_offset_mm",false,Type::Number},{"profile_plane",false},{"placement",false,Type::Object},{"targets",false,Type::Array},{"document",false}};
            if(extrusion){fields.push_back({"end_forward",false});fields.push_back({"end_reverse",false});fields.push_back({"targets_forward",false,Type::Array});fields.push_back({"targets_reverse",false,Type::Array});}else fields.push_back({"axis",false});
            if(extrusion){fields.push_back({"sheet_cut",false,Type::Boolean});fields.push_back({"sheet_cut_clearance",false,Type::Boolean});}
            else {fields.push_back({"sheet_metal",false,Type::Boolean});fields.push_back({"thickness_override",false,Type::Boolean});}
            dispatcher_.add({prefix+(create?".create":".set"),create?tr("Convert a standalone Sketch to an Extrusion or Revolution in one transaction."):tr("Edit and calculate a profile feature through the shared Properties transaction."),std::move(fields),true},
                [this,create,kind](const Json& args){
                    const auto check=target(args);if(!check.ok)return check;
                    try {
                        const auto id=workspace_.active_document_id();auto* state=workspace_.open_part(id);
                        auto* assembly=workspace_.open_assembly(id);
                        if((!state&&!assembly)||interaction().template_document)throw Error("unsupported_document","Profile operations require an open Part or Assembly.");
                        auto value=create?(state?workspace::profile_from_sketch(state->session.document(),args.at("sketch").get<std::string>(),kind)
                                                :workspace::profile_from_sketch(assembly->session.document(),args.at("sketch").get<std::string>(),kind))
                                         :profile(workspace_,id,args.at("container").get<std::string>(),kind);
                        if(state) {
                            const auto* body=state->session.document().body_owner_for_object(value.id);
                            if(body&&body->scope.id!=state->session.document().body_history.active_body_id())throw Error("inactive_body","Activate the owning Body before editing its profile.");
                            if(args.contains("targets"))throw Error("invalid_arguments","Occurrence targets are available only in an Assembly.");
                        }
                        properties(value,args,workspace_,id);const auto container=value.id;
                        std::optional<sketcher::Sketch> pending_sketch;
                        if(args.contains("profile_plane")) {
                            const auto plane=args.at("profile_plane").get<std::string>();
                            if(plane!="AUTO"&&plane!="XY"&&plane!="XZ"&&plane!="YZ")throw Error("invalid_arguments","Profile plane must be AUTO, XY, XZ or YZ.");
                            pending_sketch=workspace::document_sketch(workspace_,id,kind==Kind::Extrusion?value.extrusion.sketch_id:value.revolution.sketch_id);
                            pending_sketch->plane_auto=plane=="AUTO";
                            if(!pending_sketch->plane_auto)pending_sketch->plane=plane=="XY"?sketcher::SketchPlane::XY:plane=="XZ"?sketcher::SketchPlane::XZ:sketcher::SketchPlane::YZ;
                        }
                        if(assembly) {
                            std::vector<std::string> selected;
                            if(args.contains("targets")) {
                                for(const auto& target:args.at("targets")) {
                                    if(!target.is_string()||target.get<std::string>().empty())throw Error("invalid_arguments","Cut targets must be nonempty occurrence identities.");
                                    selected.push_back(target.get<std::string>());
                                }
                            } else if(!create) selected=assembly->session.document().find_cut(container)->target_occurrence_ids;
                            else for(const auto& item:assembly->session.document().components)
                                if(!item.suppressed&&!item.derived_copy&&item.source_kind==zima::assembly::ComponentSourceKind::Part)selected.push_back(item.occurrence_id);
                            workspace::commit_assembly_profile(workspace_,kernel_,id,std::move(value),std::move(selected),
                                create?workspace::ProfileEditMode::TransformSketch:workspace::ProfileEditMode::Replace,pending_sketch);
                        } else workspace::commit_profile(workspace_,kernel_,id,std::move(value),create?workspace::ProfileEditMode::TransformSketch:workspace::ProfileEditMode::Replace,pending_sketch);
                        change_=Change{ChangeKind::Model,id};auto result=details(workspace_,id,profile(workspace_,id,container,kind));result["changed"]=true;return Result::success(std::move(result));
                    } catch(const Error& e){return Result::failure(e.code,tr(e.what()));}
                      catch(const workspace::PlacementEditError& e){return Result::failure(e.code,tr(e.what()));}
                      catch(const std::exception& e){return Result::failure("profile_rejected",tr(e.what()));}
                });
        }
    }
}
} // namespace zima::command_host
