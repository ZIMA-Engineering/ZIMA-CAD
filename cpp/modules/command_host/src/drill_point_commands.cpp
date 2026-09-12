#include <zima/command_host/host.hpp>
#include <zima/workspace/drill_point_operations.hpp>
#include <zima/document/metadata.hpp>
#include <algorithm>
#include <cctype>

namespace zima::command_host {
namespace {
using Error=workspace::DrillPointOperationError;
const document::HistoryContainer& drill_point(const workspace::PartState* state,const std::string& id) {
    if(!state)throw Error("unsupported_document","Drill points require an open Part.");
    const auto* feature=state->session.document().find_container(id);
    if(!feature)throw Error("container_not_found","The requested container does not exist.");
    if(feature->feature_kind!=document::FeatureKind::DrillPoint)throw Error("wrong_feature","This container is not a drill point.");
    return *feature;
}
Json details(const workspace::PartState& state,const document::HistoryContainer& feature) {
    const auto& doc=state.session.document();const auto* body=doc.body_owner_for_object(feature.id);
    auto faces=Json::array();
    for(const auto& ref:feature.drill_point.bottom_faces)faces.push_back({{"owner",ref.owner_id},{"key",ref.semantic_key},{"instance_path",ref.instance_path}});
    return {{"document",doc.document_id},{"container",feature.id},{"feature",feature.feature_id},{"name",feature.name},
        {"body",body?body->scope.id:std::string{}},{"angle_degrees",feature.drill_point.included_angle_degrees},
        {"faces",std::move(faces)},{"value_locks",feature.value_locks},{"revision",state.session.revision()}};
}
}
void Host::register_drill_point_commands() {
    using Type=commands::ArgumentType;
    dispatcher_.add({"drill_point.get",tr("Read drill-point properties without calculating geometry."),
        {{"container",true},{"document",false}},false},[this](const Json& args) {
            try {const auto* state=workspace_.open_part(args.value("document",workspace_.active_document_id()));
                const auto& feature=drill_point(state,args.at("container"));return Result::success(details(*state,feature));
            }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
        });
    for(const bool create:{true,false}) {
        std::vector<commands::Argument> args;if(!create)args.push_back({"container",true});
        args.push_back({"faces",create,Type::Array});args.push_back({"angle_degrees",false,Type::Number});
        args.push_back({"name",false});args.push_back({"document",false});
        dispatcher_.add({create?"drill_point.create":"drill_point.set",create?tr("Create drill points on selected circular hole bottoms."):
            tr("Change drill-point angle and original bottom faces."),std::move(args),true},[this,create](const Json& args) {
            const auto checked=target(args);if(!checked.ok)return checked;const auto id=workspace_.active_document_id();
            try {
                auto* state=workspace_.open_part(id);
                if(!state||interaction().template_document)throw Error("unsupported_document","Drill points require an open Part.");
                if(!create&&args.size()==1+(args.contains("document")?1:0))throw Error("invalid_arguments","Specify at least one drill-point parameter.");
                auto feature=create?document::PartDocument::create_drill_point_container():drill_point(state,args.at("container"));
                if(create)feature.name=tr("Vrtací špička");
                const auto* body=create?state->session.document().body_history.find(state->session.document().body_history.active_body_id()):state->session.document().body_owner_for_object(feature.id);
                if(body&&body->scope.id!=state->session.document().body_history.active_body_id())throw Error("inactive_body","Activate the owning Body before editing its drill point.");
                if(args.contains("name")) {
                    feature.name=args.at("name").get<std::string>();document::validate_native_metadata_text(feature.name);
                    if(feature.name.empty()||std::ranges::all_of(feature.name,[](unsigned char c){return std::isspace(c)!=0;}))
                        throw Error("invalid_arguments","Specify a nonempty object name.");
                }
                if(args.contains("angle_degrees"))feature.drill_point.included_angle_degrees=args.at("angle_degrees").get<double>();
                if(args.contains("faces")) {
                    if(args.at("faces").size()>10000)throw Error("invalid_arguments","Too many drill-point references.");
                    feature.drill_point.bottom_faces.clear();
                    for(const auto& input:args.at("faces")) {
                        if(!input.is_object()||!input.contains("owner")||!input.contains("key"))throw Error("invalid_arguments","A target reference requires owner and key.");
                        for(const auto& [key,value]:input.items())if((key!="owner"&&key!="key"&&key!="instance_path")||!value.is_string())
                            throw Error("invalid_arguments","Reference fields must be supported text fields.");
                        feature.drill_point.bottom_faces.push_back({input.at("owner"),input.at("key"),input.value("instance_path",std::string{})});
                    }
                }
                const auto container=feature.id;
                const bool changed=workspace::commit_drill_point(workspace_,kernel_,id,std::move(feature),create?workspace::DrillPointEditMode::Create:workspace::DrillPointEditMode::Replace);
                if(changed)change_=Change{ChangeKind::Model,id};auto data=details(*state,drill_point(state,container));data["changed"]=changed;return Result::success(std::move(data));
            }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
             catch(const std::exception& error){return Result::failure("drill_point_rejected",tr(error.what()));}
        });
    }
}
} // namespace zima::command_host
