#include <zima/command_host/host.hpp>
#include <zima/workspace/named_view_operations.hpp>
namespace zima::command_host {
void Host::register_named_view_commands() {
    using Type=commands::ArgumentType;
    const auto encode=[](const document::NamedView& view){return Json::parse(document::serialize_named_views({view})).at(0);};
    const auto add=[this](commands::Command command,std::function<Json(const std::string&,const Json&)> action) {
        const bool changes=command.changes_state;
        dispatcher_.add(std::move(command),[this,changes,action](const Json& args){
            if(changes){const auto check=target(args);if(!check.ok)return check;}
            const auto id=args.value("document",workspace_.active_document_id());
            if(!workspace_.open_part(id) && !workspace_.open_assembly(id))
                return Result::failure("unsupported_document",tr("Named views require an open Part or Assembly."));
            try{
                auto result=action(id,args);result["document"]=id;result["body_calculated"]=false;
                result["revision"]=workspace_.open_part(id)?workspace_.open_part(id)->session.revision():workspace_.open_assembly(id)->session.revision();
                if(changes && result.value("changed",false))change_=Change{ChangeKind::Metadata,id,false};
                return Result::success(std::move(result));
            }catch(const std::out_of_range& e){return Result::failure("named_view_not_found",tr(e.what()));}
             catch(const std::exception& e){return Result::failure("invalid_arguments",tr(e.what()));}
        });
    };
    add({"view.named.list",tr("List saved camera views without changing the displayed camera."),{{"document",false}},false},
        [this](const auto& id,const Json&){return Json{{"views",Json::parse(document::serialize_named_views(workspace::named_views(workspace_,id)))}};});
    add({"view.named.get",tr("Read a named camera view including its rotation, zoom and pan."),{{"name",true},{"document",false}},false},
        [this,encode](const auto& id,const Json& args){return Json{{"view",encode(workspace::named_view(workspace_,id,args.at("name")))}};});
    add({"view.named.set",tr("Save or replace a complete named camera view without calculating geometry."),
        {{"name",true},{"camera",true,Type::Object},{"document",false}},true},
        [this,encode](const auto& id,const Json& args){
            auto entry=args.at("camera");
            if(entry.contains("name"))throw std::invalid_argument("Pass the view name separately from its camera.");
            entry["name"]=args.at("name");
            const auto view=document::parse_named_views(Json::array({entry}).dump()).front();
            const auto changed=workspace::set_named_view(workspace_,id,view);
            return Json{{"view",encode(view)},{"changed",changed}};
        });
    add({"view.named.delete",tr("Delete a saved camera view without changing geometry."),{{"name",true},{"document",false}},true},
        [this](const auto& id,const Json& args){return Json{{"changed",workspace::delete_named_view(workspace_,id,args.at("name"))}};});
}
}
