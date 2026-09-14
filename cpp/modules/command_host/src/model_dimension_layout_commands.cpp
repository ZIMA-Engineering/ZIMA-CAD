#include <zima/command_host/host.hpp>
#include <zima/workspace/model_dimension_layout_operations.hpp>
#include <zima/document/dimension_layout_json.hpp>
namespace zima::command_host {
namespace {
using Error=workspace::ModelDimensionLayoutError;
void invalid(){throw Error("invalid_arguments","Invalid model dimension property arguments.");}
kernel::EdgeReference reference(const Json& args,const workspace::Workspace& live,const std::string& id) {
    const auto& row=args.at("reference");
    if(!row.contains("owner")||!row.contains("key"))invalid();
    for(const auto& [key,value]:row.items())if((key!="owner"&&key!="key"&&key!="instance_path")||!value.is_string())invalid();
    return {row.at("owner").get<std::string>(),row.at("key").get<std::string>(),row.value("instance_path",id==live.active_document_id()?live.active_occurrence_path():std::string{})};
}
Json data(const workspace::Workspace& live,const std::string& id,const kernel::EdgeReference& ref) {
    const auto value=workspace::read_model_dimension_layout(live,id,ref);
    return {{"document",id},{"reference",{{"owner",ref.owner_id},{"key",ref.semantic_key},{"instance_path",ref.instance_path}}},
        {"owner_name",value.parameter.owner_name},{"has_override",value.stored_layout.has_value()},
        {"layout",document::dimension_layout_json(value.stored_layout.value_or(workspace::default_model_dimension_layout()))},
        {"body_calculated",false},{"revision",live.open_part(id)?live.open_part(id)->session.revision():live.open_assembly(id)->session.revision()}};
}
kernel::DimensionLayout parse_layout(const Json& value) {
    for(const auto& [key,item]:value.items()) {
        if(key=="text_style") {
            if(!item.is_null()) {
                if(!item.is_object()||item.size()!=9||!item.contains("decimals")||!item["decimals"].is_number_integer()||item["decimals"]<0||item["decimals"]>12)invalid();
                for(const auto& [name,text]:item.items())if(name!="decimals"&&!text.is_string())invalid();
            }
        } else if(key=="plane_quarter_turns") {if(!item.is_number_integer()||item<0||item>3)invalid();}
        else if(key=="arrows_reversed"||key=="radius_center_line_hidden") {if(!item.is_boolean())invalid();}
        else if(key=="envelope_offset") {if(!item.is_null()&&!item.is_number())invalid();}
        else if(key=="radius_rotation_degrees"||key=="text_along"||key=="text_outward"||key=="line_offset") {if(!item.is_number())invalid();}
        else invalid();
    }
    try{return document::dimension_layout_from_json(value);}catch(const std::exception&){invalid();}
    throw std::logic_error("Unreachable dimension layout parser");
}
}
void Host::register_model_dimension_layout_commands() {
    using Type=commands::ArgumentType;
    dispatcher_.add({"dimension.layout.list",tr("List native model dimension identities without generating view geometry."),
        {{"owner",false},{"limit",false,Type::Integer},{"document",false}},false},[this](const Json& args) {
        try {
            const auto id=args.value("document",workspace_.active_document_id());const auto limit=args.value("limit",2000LL);if(limit<1||limit>10000)invalid();
            const auto parameters=workspace::model_dimension_parameters(workspace_,id);const auto owner=args.value("owner",std::string{});
            const auto path=id==workspace_.active_document_id()?workspace_.active_occurrence_path():std::string{};
            Json items=Json::array();std::size_t total=0;
            for(const auto& p:parameters)if(owner.empty()||p.owner_id==owner){++total;if(items.size()<static_cast<std::size_t>(limit))
                items.push_back({{"reference",{{"owner",p.owner_id},{"key",p.semantic_key},{"instance_path",path}}},{"owner_name",p.owner_name}});}
            return Result::success({{"document",id},{"items",std::move(items)},{"total",total},{"body_calculated",false}});
        }catch(const Error& e){return Result::failure(e.code,tr(e.what()));}
    });
    dispatcher_.add({"dimension.layout.get",tr("Read stored 3D dimension layout and its complete text override."),
        {{"reference",true,Type::Object},{"document",false}},false},[this](const Json& args) {
        try{const auto id=args.value("document",workspace_.active_document_id());return Result::success(data(workspace_,id,reference(args,workspace_,id)));}
        catch(const Error& e){return Result::failure(e.code,tr(e.what()));}
    });
    dispatcher_.add({"dimension.layout.set",tr("Set or reset 3D dimension presentation through shared Properties without calculating a body."),
        {{"reference",true,Type::Object},{"layout",false,Type::Object},{"reset",false,Type::Boolean},{"document",false}},true},[this](const Json& args) {
        const auto checked=target(args);if(!checked.ok)return checked;
        if(interaction().template_document)return Result::failure("unsupported_document",tr("Model dimension properties require an open Part or Assembly."));
        try {
            if(args.contains("layout")==args.contains("reset")||(args.contains("reset")&&!args["reset"].get<bool>()))invalid();
            const auto id=workspace_.active_document_id();const auto ref=reference(args,workspace_,id);
            const auto layout=args.contains("layout")?std::optional<kernel::DimensionLayout>{parse_layout(args["layout"])}:std::nullopt;
            const auto changed=workspace::set_model_dimension_layout(workspace_,id,ref,layout);
            if(changed)change_=Change{ChangeKind::Model,id,true};auto result=data(workspace_,id,ref);result["changed"]=changed;return Result::success(std::move(result));
        }catch(const Error& e){return Result::failure(e.code,tr(e.what()));}
         catch(const std::exception& e){return Result::failure("dimension_layout_failed",tr(e.what()));}
    });
}
}
