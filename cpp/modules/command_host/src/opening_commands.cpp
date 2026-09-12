#include <zima/command_host/host.hpp>
#include <zima/document/thread_catalog.hpp>
#include <stdexcept>
#include <zima/workspace/opening_operations.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <zima/document/metadata.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>

namespace zima::command_host {
namespace {
using Feature=document::HistoryContainer;
using Error=workspace::OpeningOperationError;
const Feature& opening(const workspace::PartState* state,const std::string& id) {
    if(!state)throw Error("unsupported_document","Opening operations require an open Part.");
    const auto* value=state->session.document().find_container(id);
    if(!value)throw Error("container_not_found","The requested container does not exist.");
    if(value->feature_kind!=document::FeatureKind::Thread)throw Error("wrong_feature","This container is not an opening.");
    return *value;
}
const char* standard_name(document::ThreadStandard standard) {
    return standard==document::ThreadStandard::Metric?"metric":standard==document::ThreadStandard::Whitworth?"whitworth":"pipe";
}
const char* end_name(document::EndCondition end) {
    return end==document::EndCondition::Length?"length":end==document::EndCondition::UpTo?"up_to":"through_all";
}
Json opening_details(const workspace::PartState& state,const Feature& feature) {
    const auto& p=feature.thread;const auto& doc=state.session.document();const auto* body=doc.body_owner_for_object(feature.id);
    Json result={{"document",doc.document_id},{"container",feature.id},{"feature",feature.feature_id},
        {"body",body?body->scope.id:std::string{}},{"name",feature.name},{"type",p.enabled?standard_name(p.standard):"plain"},
        {"designation",p.designation},{"nominal_diameter_mm",p.nominal_diameter},{"pitch_mm",p.pitch},
        {"bore_diameter_mm",p.profile_diameter},{"custom_bore_diameter",p.custom_profile_diameter},
        {"bore_length_mm",p.bore_length},{"thread_length_mm",p.length_forward},
        {"bore_end",end_name(p.end_condition_forward)},{"thread_end",end_name(p.length_end_condition)},
        {"chamfer_enabled",p.chamfer_enabled},{"chamfer_depth_mm",p.chamfer_depth},{"chamfer_angle_degrees",p.chamfer_angle_degrees},
        {"drill_point_enabled",feature.hole.drill_point_enabled},{"drill_point_angle_degrees",feature.hole.drill_point_angle_degrees},
        {"runout_pitch_factor",p.runout_pitch_factor},{"left_handed",p.left_hand},
        {"direction",p.direction==document::ExtrusionDirection::Reverse?"reverse":"forward"},
        {"value_locks",feature.value_locks},{"reference_valid",feature.placement.reference_valid},{"revision",state.session.revision()}};
    const auto targets=[](const auto& values) {auto result=Json::array();for(const auto& target:values)
        result.push_back({{"owner",target.reference.owner_id},{"key",target.reference.semantic_key},{"instance_path",target.reference.instance_path},
            {"kind",target.kind==document::EndTargetKind::Plane?"plane":target.kind==document::EndTargetKind::Point?"point":"face"}});return result;};
    result["bore_targets"]=targets(p.end_targets_forward);result["thread_targets"]=targets(p.length_end_targets);
    return result;
}
void opening_properties(Feature& value,const Json& args,const workspace::Workspace& live,const std::string& document_id,bool create) {
    const auto previous=value;auto& p=value.thread;
    const auto choose=[&](const char* key,std::initializer_list<const char*> choices) {
        const auto text=args.at(key).get<std::string>();
        if(std::ranges::none_of(choices,[&](const auto* item){return text==item;}))throw Error("invalid_arguments","Unknown opening parameter option.");return text;
    };
    if(args.contains("name")) {
        const auto name=args.at("name").get<std::string>();document::validate_native_metadata_text(name);
        if(name.empty()||std::ranges::all_of(name,[](unsigned char c){return std::isspace(c)!=0;}))throw Error("invalid_arguments","Specify a nonempty object name.");value.name=name;
    }
    if(args.contains("type")) {
        const auto type=choose("type",{"plain","metric","whitworth","pipe"});p.enabled=type!="plain";
        if(p.enabled)p.standard=type=="metric"?document::ThreadStandard::Metric:type=="whitworth"?document::ThreadStandard::Whitworth:document::ThreadStandard::Pipe;
    }
    if(args.contains("custom_bore_diameter"))p.custom_profile_diameter=args.at("custom_bore_diameter").get<bool>();
    const auto number=[&](const char* key,double& field) {if(args.contains(key))field=args.at(key).get<double>();};
    const auto flag=[&](const char* key,bool& field) {if(args.contains(key))field=args.at(key).get<bool>();};
    number("bore_length_mm",p.bore_length);number("thread_length_mm",p.length_forward);
    number("chamfer_depth_mm",p.chamfer_depth);number("chamfer_angle_degrees",p.chamfer_angle_degrees);
    number("drill_point_angle_degrees",value.hole.drill_point_angle_degrees);number("runout_pitch_factor",p.runout_pitch_factor);
    flag("chamfer_enabled",p.chamfer_enabled);flag("drill_point_enabled",value.hole.drill_point_enabled);flag("left_handed",p.left_hand);
    if(args.contains("direction"))p.direction=choose("direction",{"forward","reverse"})=="forward"?document::ExtrusionDirection::Forward:document::ExtrusionDirection::Reverse;
    if(args.contains("bore_end")) {
        const auto end=choose("bore_end",{"length","up_to","through_all"});
        p.end_condition_forward=end=="length"?document::EndCondition::Length:end=="up_to"?document::EndCondition::UpTo:document::EndCondition::ThroughAll;
        if(p.end_condition_forward!=document::EndCondition::Length&&!args.contains("drill_point_enabled"))value.hole.drill_point_enabled=false;
    }
    if(args.contains("thread_end"))p.length_end_condition=choose("thread_end",{"length","up_to"})=="length"?document::EndCondition::Length:document::EndCondition::UpTo;
    if(args.contains("nominal_diameter_mm")) {
        if(p.enabled)throw Error("invalid_arguments","Select a catalog size for a threaded opening; nominal diameter is editable for a plain opening.");
        number("nominal_diameter_mm",p.nominal_diameter);
    }
    const bool select_size=create||args.contains("designation")||args.contains("type")||args.contains("custom_bore_diameter");
    if(!p.enabled&&args.contains("designation"))throw Error("invalid_arguments","A plain opening has no thread catalog size.");
    if(p.enabled&&select_size) {
        const auto& catalog=document::thread_catalog(standard_name(p.standard));
        const auto designation=args.value("designation",p.designation);
        auto selected=std::ranges::find(catalog,designation,&document::ThreadCatalogSize::designation);
        if(selected==catalog.end()) {
            if(args.contains("designation"))throw Error("invalid_arguments","The requested thread designation is not in this catalog.");
            selected=std::ranges::min_element(catalog,[&](const auto& a,const auto& b){return std::abs(a.nominal_diameter-p.nominal_diameter)<std::abs(b.nominal_diameter-p.nominal_diameter);});
        }
        if(selected==catalog.end())throw Error("invalid_arguments","The thread catalog is empty.");
        document::select_opening_thread_size(value,p.standard,*selected);
        // An explicit final depth wins over catalog defaults and is validated.
        number("bore_length_mm",p.bore_length);
    }
    if(args.contains("bore_diameter_mm")) {
        if(!p.custom_profile_diameter)throw Error("invalid_arguments","Enable a custom bore diameter before changing it.");
        number("bore_diameter_mm",p.profile_diameter);
    }
    if(args.contains("placement")) {
        const auto& patch=args.at("placement");if(patch.empty())throw Error("invalid_arguments","Specify at least one placement parameter.");
        const auto* state=live.open_part(document_id);
        const auto owner=create?state->session.document().body_history.active_body_id():value.id;
        const auto geometry=workspace::placement_edit_geometry(live,document_id,owner);
        for(const auto& [key,number]:patch.items()) {
            if(!number.is_number()||!std::isfinite(number.get<double>()))throw Error("invalid_arguments","Placement parameters must be finite JSON numbers.");
            if(!workspace::assign_placement_dimension(value.placement,geometry,key,number.get<double>()))throw Error("parameter_not_editable","The placement parameter is unknown, constrained or locked.");
        }
    }
    const auto old=workspace::opening_dimensions(previous),now=workspace::opening_dimensions(value);
    for(std::size_t i=0;i<old.size();++i)if(old[i].second!=now[i].second&&previous.value_locks.contains(old[i].first))
        throw Error("value_locked","Unlock the dimension before changing it.");
}
} // namespace
void Host::register_opening_commands() {
    using Type=commands::ArgumentType;
    dispatcher_.add({"thread.catalog",tr("Read the shared thread catalog; all dimensions are millimetres."),
        {{"standard",true},{"designation",false},{"offset",false,Type::Integer},{"limit",false,Type::Integer}},false},[this](const Json& args) {
        try {
            const auto& catalog=document::thread_catalog(args.at("standard").get<std::string>());
            const auto offset=args.value("offset",0LL),limit=args.value("limit",100LL);
            if(offset<0||offset>100000000||limit<1||limit>1000)
                return Result::failure("invalid_arguments",tr("Catalog offset must be nonnegative and limit must be between 1 and 1000."));
            const auto designation=args.value("designation",std::string{});
            auto items=Json::array();std::size_t total=0;
            for(const auto& size:catalog) {
                if(!designation.empty()&&designation!=size.designation)continue;
                if(total++<static_cast<std::size_t>(offset)||items.size()>=static_cast<std::size_t>(limit))continue;
                items.push_back({{"designation",size.designation},{"nominal_diameter_mm",size.nominal_diameter},
                    {"pitch_mm",size.pitch},{"internal_root_diameter_mm",size.internal_root_diameter},
                    {"external_root_diameter_mm",size.external_root_diameter},{"preferred",size.preferred}});
            }
            const auto next=static_cast<std::size_t>(offset)+items.size();
            return Result::success({{"standard",args.at("standard")},{"items",std::move(items)},{"total",total},
                {"more",next<total},{"next_offset",next}});
        } catch(const std::invalid_argument& error){return Result::failure("invalid_arguments",tr(error.what()));}
    });
    dispatcher_.add({"opening.get",tr("Read opening and internal thread properties without calculating geometry."),
        {{"container",true},{"document",false}},false},[this](const Json& args) {
        try {const auto* state=workspace_.open_part(args.value("document",workspace_.active_document_id()));
            const auto& feature=opening(state,args.at("container"));
            return Result::success(opening_details(*state,feature));
        }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
    });
    for(const bool create:{true,false}) {
        std::vector<commands::Argument> arguments;
        if(!create)arguments.push_back({"container",true});
        for(const auto* key:{"name","type","designation","direction","bore_end","thread_end"})arguments.push_back({key,false});
        for(const auto* key:{"nominal_diameter_mm","bore_diameter_mm","bore_length_mm","thread_length_mm","chamfer_depth_mm",
            "chamfer_angle_degrees","drill_point_angle_degrees","runout_pitch_factor"})arguments.push_back({key,false,Type::Number});
        for(const auto* key:{"custom_bore_diameter","chamfer_enabled","drill_point_enabled","left_handed"})arguments.push_back({key,false,Type::Boolean});
        arguments.push_back({"placement",false,Type::Object});arguments.push_back({"document",false});
        dispatcher_.add({create?"opening.create":"opening.set",create?tr("Create a plain or threaded opening in the active Body.")
            :tr("Change opening properties with the same transaction as Properties OK."),std::move(arguments),true},[this,create](const Json& args) {
            const auto check=target(args);if(!check.ok)return check;const auto id=workspace_.active_document_id();
            try {
                auto* state=workspace_.open_part(id);
                if(!state||interaction().template_document)throw Error("unsupported_document","Opening operations require an open Part.");
                if(!create&&args.size()==1+(args.contains("document")?1:0))throw Error("invalid_arguments","Specify at least one opening parameter.");
                auto value=create?document::PartDocument::create_thread_container():opening(state,args.at("container"));
                if(create)value.name=tr("Otvor");
                const auto* body=create?state->session.document().body_history.find(state->session.document().body_history.active_body_id()):state->session.document().body_owner_for_object(value.id);
                if(body&&body->scope.id!=state->session.document().body_history.active_body_id())throw Error("inactive_body","Activate the owning Body before editing its opening.");
                opening_properties(value,args,workspace_,id,create);const auto container=value.id;
                const bool changed=workspace::commit_opening(workspace_,kernel_,id,std::move(value),create?workspace::OpeningEditMode::Create:workspace::OpeningEditMode::Replace);
                if(changed)change_=Change{ChangeKind::Model,id};auto data=opening_details(*state,opening(state,container));data["changed"]=changed;return Result::success(std::move(data));
            }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
             catch(const workspace::PlacementEditError& error){return Result::failure(error.code,tr(error.what()));}
             catch(const std::exception& error){return Result::failure("opening_rejected",tr(error.what()));}
        });
    }
}
} // namespace zima::command_host
