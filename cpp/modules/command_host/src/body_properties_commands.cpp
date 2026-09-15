#include <zima/command_host/host.hpp>
#include <zima/workspace/body_properties_edits.hpp>
namespace zima::command_host {
namespace {
commands::Json details(const document::BodyProperties& row) {
    using Json=commands::Json;auto result=Json::parse(document::serialize_body_properties({row})).at(0);
    result["object"]=row.id;result["body_calculated"]=false;
    result["mass_kg"]=row.integrals&&row.density_kg_mm3?Json(row.volume * *row.density_kg_mm3):Json(nullptr);
    result["inertia_kg_mm2"]=nullptr;
    if(row.integrals&&row.density_kg_mm3) {
        auto t=kernel::inertia_rotate(row.integrals->inertia,kernel::inertia_frame(row.rotation_degrees),true);
        for(auto& x:t)x*=*row.density_kg_mm3;result["inertia_kg_mm2"]=t;
    }return result;
}
}
void Host::register_body_properties_commands() {
    using Json=commands::Json;using Result=commands::Result;using Type=commands::ArgumentType;
    for(bool create:{true,false}) {
        std::vector<commands::Argument> arguments{{"name",false},{"rotation_degrees",false,Type::Array},{"visible",false,Type::Boolean},{"document",false}};
        if(!create)arguments.insert(arguments.begin(),{"object",true});
        dispatcher_.add({create?"body_properties.create":"body_properties.set",create?
            tr("Create body properties at the current history position."):tr("Edit the name, visibility and axes of body properties."),arguments,true},[this,create](const Json& args) {
            const auto checked=target(args);if(!checked.ok)return checked;
            if(interaction().template_document)return Result::failure("unsupported_document",tr("Body properties require an open Part."));
            try {
                const auto id=workspace_.active_document_id();
                const auto edit=workspace::prepare_body_properties_edit(workspace_,id,create?std::string{}:args.at("object").get<std::string>(),tr("Vlastnosti tělesa"));
                auto row=edit.initial;if(args.contains("name"))row.name=args.at("name").get<std::string>();
                if(args.contains("visible"))row.visible=args.at("visible").get<bool>();
                if(args.contains("rotation_degrees")) {
                    const auto& v=args.at("rotation_degrees");if(v.size()!=3)throw std::invalid_argument("Invalid body properties vector.");
                    row.rotation_degrees={v.at(0).get<double>(),v.at(1).get<double>(),v.at(2).get<double>()};
                }
                const bool changed=workspace::commit_body_properties(workspace_,edit,std::move(row));
                const auto& session=workspace_.open_part(id)->session;
                auto data=details(*std::ranges::find(session.document().body_properties,edit.initial.id,&document::BodyProperties::id));
                data["document"]=id;data["revision"]=session.revision();data["changed"]=changed;
                if(changed)change_=Change{ChangeKind::Model,id,true};return Result::success(std::move(data));
            }catch(const workspace::MeasurementOperationError& e){return Result::failure(e.code,tr(e.what()));}
             catch(const std::exception& e){return Result::failure("body_properties_rejected",tr(e.what()));}
        });
    }
    dispatcher_.add({"body_properties.delete",tr("Remove body properties from history."),{{"object",true},{"document",false}},true},[this](const Json& args) {
        const auto checked=target(args);if(!checked.ok)return checked;
        if(interaction().template_document)return Result::failure("unsupported_document",tr("Body properties require an open Part."));
        try {
            const auto id=workspace_.active_document_id();workspace::remove_body_properties(workspace_,id,args.at("object").get<std::string>());
            change_=Change{ChangeKind::Model,id,true};return Result::success({{"document",id},{"changed",true},{"body_calculated",false}});
        }catch(const workspace::MeasurementOperationError& e){return Result::failure(e.code,tr(e.what()));}
         catch(const std::exception& e){return Result::failure("body_properties_rejected",tr(e.what()));}
    });
    for(bool list:{true,false}) {
        std::vector<commands::Argument> args{{"document",false}};if(!list)args.insert(args.begin(),{"object",true});
        dispatcher_.add({list?"body_properties.list":"body_properties.get",list?
            tr("List historical body properties."):tr("Read cached body properties and central inertia in the selected axes."),args,false},[this,list](const Json& args) {
            try {
                const auto id=args.value("document",workspace_.active_document_id());const auto* part=workspace_.open_part(id);
                if(!part)return Result::failure("unsupported_document",tr("Body properties require an open Part."));
                const auto& rows=part->session.document().body_properties;
                if(list){auto items=Json::array();for(const auto& row:rows)items.push_back(details(row));return Result::success({{"document",id},{"items",items},{"total",rows.size()}});}
                const auto it=std::ranges::find(rows,args.at("object").get<std::string>(),&document::BodyProperties::id);
                if(it==rows.end())return Result::failure("body_properties_not_found",tr("The body properties feature does not exist."));
                auto data=details(*it);data["document"]=id;return Result::success(std::move(data));
            }catch(const std::exception& e){return Result::failure("body_properties_rejected",tr(e.what()));}
        });
    }
}
}
