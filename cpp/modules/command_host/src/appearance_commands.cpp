#include <zima/command_host/host.hpp>
#include <zima/workspace/appearance_operations.hpp>
#include <zima/kernel/stable_id.hpp>
#include <algorithm>
#include <cmath>
namespace zima::command_host {
namespace {
using Error=workspace::AppearanceError;
using Edit=workspace::AppearanceEdit;
void keys(const Json& object,std::initializer_list<const char*> allowed) {
    if(!object.is_object())throw Error("invalid_arguments","Appearance fields must be JSON objects.");
    for(const auto& [key,value]:object.items())if(std::ranges::none_of(allowed,[&](const char* name){return key==name;}))
        throw Error("invalid_arguments","Unknown appearance property field.");
}
std::string text(const Json& object,const char* key,const std::string& fallback={}) {
    if(!object.contains(key))return fallback;
    if(!object.at(key).is_string())throw Error("invalid_arguments","Appearance names and identities must be text.");
    return object.at(key).get<std::string>();
}
Json encode(const kernel::SurfaceStyle& style){return {{"color",style.color},{"roughness",style.roughness},{"metallic",style.metallic}};}
kernel::SurfaceStyle style(const Json& input,kernel::SurfaceStyle value) {
    keys(input,{"color","roughness","metallic"});
    if(input.empty())throw Error("invalid_arguments","Specify at least one surface style property.");
    if(input.contains("color"))value.color=text(input,"color");
    for(const auto* key:{"roughness","metallic"})if(input.contains(key)) {
        if(!input.at(key).is_number()||!std::isfinite(input.at(key).get<double>()))throw Error("invalid_arguments","Surface style values must be finite JSON numbers.");
        (std::string_view(key)=="roughness"?value.roughness:value.metallic)=input.at(key).get<double>();
    }
    kernel::Appearance check;check.body=value;document::validate_appearance(check);return value;
}
kernel::SurfaceStyle current_style(const kernel::Appearance& value,const std::string& body) {
    const auto found=value.bodies.find(body);return body.empty()||found==value.bodies.end()?value.body:found->second;
}
Edit prepare(const workspace::Workspace& live,const Json& args) {
    std::string occurrence;
    if(args.contains("instance_path")) {
        const auto path=assembly::InstancePath::decode(args.at("instance_path").get<std::string>());
        if(path.occurrence_ids.size()!=1)throw Error("unsupported_context","Edit a component only in its immediate owning Assembly.");
        occurrence=path.occurrence_ids.front();
    }
    return workspace::prepare_appearance_edit(live,args.value("document",live.active_document_id()),occurrence,
        args.contains("body")?std::optional<std::string>(args.at("body").get<std::string>()):std::nullopt);
}
Json details(const workspace::Workspace& live,const Edit& edit) {
    Json groups=Json::array();
    for(const auto& group:edit.initial.groups)if(group.body_id==edit.body_id) {
        auto faces=Json::array();for(const auto& face:group.faces) {
            const auto split=face.find("::");faces.push_back({{"owner",face.substr(0,split)},{"key",split==std::string::npos?std::string{}:face.substr(split+2)}});
        }
        groups.push_back({{"id",group.id},{"name",group.name},{"style",encode(group.style)},{"faces",std::move(faces)}});
    }
    bool inherited=false;
    if(!edit.occurrence_id.empty()) {
        const auto* component=live.open_assembly(edit.document_id)->session.document().find_occurrence(edit.occurrence_id);
        inherited=!component->appearance_override&&!component->body_color_override;
    }
    return {{"document",edit.document_id},{"instance_path",edit.occurrence_id.empty()?std::string{}:assembly::InstancePath{}.child(edit.occurrence_id).encoded()},
        {"body",edit.body_id},{"style",encode(current_style(edit.initial,edit.body_id))},{"groups",std::move(groups)},
        {"body_override",!edit.body_id.empty()&&edit.initial.bodies.contains(edit.body_id)},{"inherited",inherited},{"revision",edit.revision}};
}
void groups(kernel::Appearance& value,const Edit& edit,const Json& input) {
    if(input.size()>4096)throw Error("invalid_arguments","Too many appearance groups.");
    std::vector<kernel::SurfaceGroup> replacement;
    for(const auto& row:input) {
        keys(row,{"id","name","style","faces"});const auto id=text(row,"id",kernel::make_stable_id());
        const auto found=std::ranges::find(edit.initial.groups,id,&kernel::SurfaceGroup::id);
        kernel::SurfaceGroup group;
        if(found!=edit.initial.groups.end()) {
            if(found->body_id!=edit.body_id)throw Error("wrong_appearance_scope","Change appearance only in the requested body scope.");group=*found;
        } else {group.id=id;group.body_id=edit.body_id;group.style=current_style(value,edit.body_id);}
        group.name=text(row,"name",group.name);
        if(row.contains("style"))group.style=style(row.at("style"),group.style);
        if(row.contains("faces")) {
            const auto& faces=row.at("faces");if(!faces.is_array()||faces.size()>100000)throw Error("invalid_arguments","Appearance faces must be a bounded array of references.");
            group.faces.clear();
            for(const auto& face:faces) {
                keys(face,{"owner","key"});const auto owner=text(face,"owner"),key=text(face,"key");
                if(owner.empty()||key.empty()||owner.find("::")!=std::string::npos)throw Error("invalid_reference","Select a persisted result face in this appearance scope.");
                group.faces.push_back(owner+"::"+key);
            }
        }
        replacement.push_back(std::move(group));
    }
    std::erase_if(value.groups,[&](const auto& group){return group.body_id==edit.body_id;});
    value.groups.insert(value.groups.end(),replacement.begin(),replacement.end());
}
}
void Host::register_appearance_commands() {
    using Type=commands::ArgumentType;
    const std::vector<commands::Argument> scope{{"instance_path",false},{"body",false},{"document",false}};
    dispatcher_.add({"appearance.palette",tr("List built-in surface styles without changing a document."),{},false},[](const Json&) {
        auto items=Json::array();for(const auto& item:document::default_surface_palette())items.push_back({{"id",item.id},{"name",item.name},{"category",item.category},{"style",encode(item.style)}});
        return Result::success({{"items",std::move(items)}});
    });
    dispatcher_.add({"appearance.get",tr("Read the appearance of a Part body or an owned component occurrence."),scope,false},[this](const Json& args) {
        try{return Result::success(details(workspace_,prepare(workspace_,args)));}
        catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
        catch(const std::exception& error){return Result::failure("appearance_rejected",tr(error.what()));}
    });
    auto query=scope;query.push_back({"offset",false,Type::Integer});query.push_back({"limit",false,Type::Integer});
    dispatcher_.add({"appearance.faces",tr("List persisted display faces available for appearance groups without calculating geometry."),std::move(query),false},[this](const Json& args) {
        try {
            const auto edit=prepare(workspace_,args);const auto offset=args.value("offset",0LL),limit=args.value("limit",2000LL);
            if(offset<0||offset>100000000||limit<1||limit>10000)throw Error("invalid_arguments","Appearance query offset or limit is outside the supported range.");
            const auto faces=workspace::appearance_faces(workspace_,edit);auto items=Json::array();std::size_t index=0;
            for(const auto& [owner,key]:faces)if(index++>=static_cast<std::size_t>(offset)&&items.size()<static_cast<std::size_t>(limit))items.push_back({{"owner",owner},{"key",key}});
            return Result::success({{"document",edit.document_id},{"body",edit.body_id},{"items",std::move(items)},{"total",faces.size()},{"revision",edit.revision}});
        }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
         catch(const std::exception& error){return Result::failure("appearance_rejected",tr(error.what()));}
    });
    for(const bool reset:{false,true}) {
        auto arguments=scope;
        if(reset)arguments.push_back({"inherit",false,Type::Boolean});
        else {arguments.push_back({"style",false,Type::Object});arguments.push_back({"groups",false,Type::Array});}
        dispatcher_.add({reset?"appearance.reset":"appearance.set",reset?tr("Reset appearance in this body scope or restore source appearance for a whole occurrence."):
            tr("Change surface style and face groups through the shared appearance transaction."),std::move(arguments),true},[this,reset](const Json& args) {
            const auto checked=target(args);if(!checked.ok)return checked;
            if(interaction().template_document)return Result::failure("unsupported_document",tr("Appearance requires an open Part or an owned Part occurrence."));
            try {
                if(!reset&&!args.contains("style")&&!args.contains("groups"))throw Error("invalid_arguments","Specify a surface style or appearance groups.");
                const auto edit=prepare(workspace_,args);auto value=edit.initial;bool changed=false;
                if(reset&&args.value("inherit",false))changed=workspace::inherit_occurrence_appearance(workspace_,edit);
                else {
                    if(reset)workspace::reset_appearance(value,edit.body_id);
                    else {
                        if(args.contains("style")) {
                            const auto updated=style(args.at("style"),current_style(value,edit.body_id));
                            if(edit.body_id.empty())value.body=updated;else value.bodies[edit.body_id]=updated;
                        }
                        if(args.contains("groups"))groups(value,edit,args.at("groups"));
                    }
                    changed=workspace::commit_appearance(workspace_,edit,value);
                }
                auto result=details(workspace_,prepare(workspace_,args));result["changed"]=changed;
                if(changed)change_=Change{ChangeKind::Appearance,edit.document_id};return Result::success(std::move(result));
            }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
             catch(const std::exception& error){return Result::failure("appearance_rejected",tr(error.what()));}
        });
    }
}
}
