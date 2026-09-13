#include <zima/command_host/host.hpp>
#include <zima/workspace/shell_operations.hpp>
#include <zima/document/metadata.hpp>
#include <algorithm>
#include <cctype>
namespace zima::command_host {
namespace {
using Error=workspace::ShellOperationError;
const document::HistoryContainer& shell(const workspace::PartState* state,const std::string& id) {
    if(!state)throw Error("unsupported_document","Shell operations require an open Part.");
    const auto* feature=state->session.document().find_container(id);
    if(!feature)throw Error("container_not_found","The requested container does not exist.");
    if(feature->feature_kind!=document::FeatureKind::Shell)throw Error("wrong_feature","This container is not a Shell.");
    return *feature;
}
Json reference(const kernel::FaceReference& face) {
    return {{"owner",face.owner_id},{"key",face.semantic_key},{"instance_path",face.instance_path}};
}
Json details(const workspace::PartState& state,const document::HistoryContainer& feature) {
    const auto& doc=state.session.document();const auto* body=doc.body_owner_for_object(feature.id);
    auto faces=Json::array();for(const auto& face:feature.shell.removed_faces)faces.push_back(reference(face));
    return {{"document",doc.document_id},{"container",feature.id},{"feature",feature.feature_id},{"name",feature.name},
        {"body",body?body->scope.id:std::string{}},{"thickness_mm",feature.shell.thickness},{"faces",std::move(faces)},
        {"value_locks",feature.value_locks},{"revision",state.session.revision()}};
}
std::size_t page(const Json& args,const char* key,std::size_t fallback,std::size_t maximum,bool positive=false) {
    if(!args.contains(key))return fallback;
    if(args.at(key)<0||args.at(key)>maximum||(positive&&args.at(key)==0))
        throw Error("invalid_arguments","Shell face pagination is outside the supported range.");
    return args.at(key).get<std::size_t>();
}
}
void Host::register_shell_commands() {
    using Type=commands::ArgumentType;
    dispatcher_.add({"shell.get",tr("Read Shell thickness and opening faces without calculation."),
        {{"container",true},{"document",false}},false},[this](const Json& args) {
            try {const auto* state=workspace_.open_part(args.value("document",workspace_.active_document_id()));
                const auto& feature=shell(state,args.at("container"));return Result::success(details(*state,feature));
            }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
        });
    dispatcher_.add({"shell.faces",tr("List available faces of the calculated Shell input body."),
        {{"container",false},{"owner",false},{"offset",false,Type::Integer},{"limit",false,Type::Integer},{"document",false}},false},[this](const Json& args) {
            try {
                const auto id=args.value("document",workspace_.active_document_id());const auto* state=workspace_.open_part(id);
                if(!state)throw Error("unsupported_document","Shell operations require an open Part.");
                const auto container=args.value("container",std::string{});if(args.contains("container"))static_cast<void>(shell(state,container));
                const auto offset=page(args,"offset",0,100000000),limit=page(args,"limit",500,5000,true);
                const auto input=workspace::shell_input_faces(*state,container);auto items=Json::array();std::size_t total{};
                for(const auto& face:input) {
                    if(args.contains("owner")&&args.at("owner")!=face.owner_id)continue;
                    if(total>=offset&&items.size()<limit)items.push_back(reference(face));++total;
                }
                const bool more=offset<total&&items.size()<total-offset;
                return Result::success({{"document",id},{"container",container},{"revision",state->session.revision()},
                    {"items",std::move(items)},{"total",total},{"offset",offset},{"limit",limit},{"has_more",more}});
            }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
        });
    for(const bool create:{true,false}) {
        std::vector<commands::Argument> args;if(!create)args.push_back({"container",true});
        args.push_back({"thickness_mm",false,Type::Number});args.push_back({"faces",false,Type::Array});
        args.push_back({"name",false});args.push_back({"document",false});
        dispatcher_.add({create?"shell.create":"shell.set",create?tr("Create an inward Shell with optional opening faces."):
            tr("Change Shell thickness and opening faces."),std::move(args),true},[this,create](const Json& args) {
            const auto checked=target(args);if(!checked.ok)return checked;const auto id=workspace_.active_document_id();
            try {
                auto* state=workspace_.open_part(id);
                if(!state||interaction().template_document)throw Error("unsupported_document","Shell operations require an open Part.");
                if(!create&&args.size()==1+(args.contains("document")?1:0))throw Error("invalid_arguments","Specify at least one Shell parameter.");
                auto feature=create?document::PartDocument::create_shell_container():shell(state,args.at("container"));
                if(create)feature.name=tr("Shell");
                const auto* body=create?state->session.document().body_history.find(state->session.document().body_history.active_body_id()):state->session.document().body_owner_for_object(feature.id);
                if(body&&body->scope.id!=state->session.document().body_history.active_body_id())throw Error("inactive_body","Activate the owning Body before editing its Shell.");
                if(args.contains("name")) {
                    feature.name=args.at("name").get<std::string>();document::validate_native_metadata_text(feature.name);
                    if(feature.name.empty()||std::ranges::all_of(feature.name,[](unsigned char c){return std::isspace(c)!=0;}))
                        throw Error("invalid_arguments","Specify a nonempty object name.");
                }
                if(args.contains("thickness_mm"))feature.shell.thickness=args.at("thickness_mm").get<double>();
                if(args.contains("faces")) {
                    if(args.at("faces").size()>10000)throw Error("invalid_arguments","Too many Shell opening faces.");
                    feature.shell.removed_faces.clear();
                    for(const auto& input:args.at("faces")) {
                        if(!input.is_object()||!input.contains("owner")||!input.contains("key"))throw Error("invalid_arguments","A target reference requires owner and key.");
                        for(const auto& [key,value]:input.items())if((key!="owner"&&key!="key"&&key!="instance_path")||!value.is_string())
                            throw Error("invalid_arguments","Reference fields must be supported text fields.");
                        feature.shell.removed_faces.push_back({input.at("owner"),input.at("key"),input.value("instance_path",std::string{})});
                    }
                }
                const auto container=feature.id;
                const bool changed=workspace::commit_shell(workspace_,kernel_,id,std::move(feature),create?workspace::ShellEditMode::Create:workspace::ShellEditMode::Replace);
                if(changed)change_=Change{ChangeKind::Model,id};auto data=details(*state,shell(state,container));data["changed"]=changed;return Result::success(std::move(data));
            }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
             catch(const std::exception& error){return Result::failure("shell_rejected",tr(error.what()));}
        });
    }
}
} // namespace zima::command_host
