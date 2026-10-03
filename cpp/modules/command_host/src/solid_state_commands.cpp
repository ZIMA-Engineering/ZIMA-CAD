#include <zima/command_host/host.hpp>
#include <zima/workspace/solid_state_operations.hpp>

namespace zima::command_host {
void Host::register_solid_state_commands() {
    using Type=commands::ArgumentType;
    for(bool restore:{false,true}) {
        const std::string prefix=restore?"restore_shape":"straighten";
        dispatcher_.add({prefix+".get",tr("Read solid state properties without calculation."),
            {{"container",true},{"document",false}},false},[this,restore](const Json& args) {
            try {
                const auto id=args.value("document",workspace_.active_document_id());
                const auto* part=workspace_.open_part(id);
                if(!part)throw std::invalid_argument("Solid state requires an open Part.");
                const auto* feature=part->session.document().find_container(args.at("container"));
                if(!feature||feature->feature_kind!=(restore?document::FeatureKind::RestoreShape:document::FeatureKind::Straighten))
                    throw std::invalid_argument("Solid state container does not exist.");
                return Result::success({{"document",id},{"container",feature->id},{"name",feature->name},
                    {"all",feature->solid_state.all},{"owners",feature->solid_state.owners},
                    {"coefficient",feature->solid_state.coefficient}});
            }catch(const std::exception& error){return Result::failure("solid_state_rejected",tr(error.what()));}
        });
        for(bool create:{true,false}) {
            std::vector<commands::Argument> arguments{{"container",!create},{"all",false,Type::Boolean},
                {"owners",false,Type::Array},{"name",false},{"document",false}};
            if(!restore)arguments.push_back({"coefficient",false,Type::Number});
            dispatcher_.add({prefix+(create?".create":".set"),tr("Change the solid state at this history boundary while retaining supported modifications."),
                arguments,true},[this,restore,create](const Json& args) {
                if(auto result=target(args);!result.ok)return result;
                try {
                    const auto id=workspace_.active_document_id();const auto* part=workspace_.open_part(id);
                    if(!part)throw std::invalid_argument("Solid state requires an open Part.");
                    const auto* stored=create?nullptr:part->session.document().find_container(args.at("container"));
                    const auto kind=restore?document::FeatureKind::RestoreShape:document::FeatureKind::Straighten;
                    if(!create&&(!stored||stored->feature_kind!=kind))
                        throw std::invalid_argument("Solid state container does not exist.");
                    auto feature=stored?*stored:document::PartDocument::create_solid_state_container(restore);
                    feature.name=args.value("name",stored?stored->name:tr(restore?"Restore shape":"Straighten"));
                    if(args.contains("owners"))feature.solid_state.owners=args.at("owners").get<std::vector<std::string>>();
                    feature.solid_state.all=args.value("all",args.contains("owners")?false:feature.solid_state.all);
                    if(!restore)feature.solid_state.coefficient=args.value("coefficient",feature.solid_state.coefficient);
                    const auto owner=feature.id;
                    const bool changed=workspace::commit_solid_state(workspace_,kernel_,id,std::move(feature));
                    if(changed)change_=Change{ChangeKind::Model,id};
                    return Result::success({{"document",id},{"container",owner},{"changed",changed}});
                }catch(const std::exception& error){return Result::failure("solid_state_rejected",tr(error.what()));}
            });
        }
    }
}
}
