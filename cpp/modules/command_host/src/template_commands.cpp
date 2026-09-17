#include <zima/command_host/host.hpp>
#include <zima/workspace/template_operations.hpp>
#include <zima/workspace/sketch_operations.hpp>
namespace zima::command_host {
namespace {
std::string path_text(const std::filesystem::path& path){const auto bytes=path.generic_u8string();return {bytes.begin(),bytes.end()};}
std::filesystem::path path(const Json& args,const std::filesystem::path& directory){auto result=std::filesystem::u8path(args.at("path").get<std::string>());return std::filesystem::absolute(result.is_relative()?directory/result:result).lexically_normal();}
Json data(const workspace::Workspace& live,const std::string& id) {
    const auto& sketch=workspace::drawing_template_sketch(live,id);const auto* state=live.open_part(id);const auto& meta=*sketch.drawing_template;
    return {{"document",id},{"sketch",sketch.id},{"name",sketch.name},{"kind",meta.kind},{"path",path_text(state->path)},
        {"revision",state->session.revision()},{"points",sketch.points.size()},{"segments",sketch.segments.size()},{"texts",sketch.texts.size()},
        {"images",meta.images.size()},{"repeat_regions",meta.repeat_regions.size()},{"sections",meta.sections},{"field_ids",meta.field_ids},{"pens",meta.pens}};
}
}
void Host::register_template_commands() {
    using Type=commands::ArgumentType;
    const auto add=[this](commands::Command declaration,std::function<Result(const Json&)> operation){
        dispatcher_.add(std::move(declaration),[this,operation=std::move(operation)](const Json& args){try{return operation(args);}
            catch(const workspace::TemplateOperationError& error){return Result::failure(error.code,tr(error.what()));}
            catch(const workspace::SketchOperationError& error){return Result::failure(error.code,tr(error.what()));}
            catch(const std::exception& error){return Result::failure("template_rejected",tr(error.what()));}});
    };
    add({"template.get",tr("Read drawing template metadata without calculation."),{{"document",false}},false},[this](const Json& args){return Result::success(data(workspace_,args.value("document",workspace_.active_document_id())));});
    add({"template.new",tr("Create a drawing_format or title_block template."),{{"kind",true},{"name",true}},true},[this](const Json& args){
        const auto kind=args.at("kind").get<std::string>(),name=args.at("name").get<std::string>();
        if(kind!="title_block"&&kind!="drawing_format")return Result::failure("invalid_arguments",tr("Template kind must be drawing_format or title_block."));
        const auto id=workspace::create_drawing_template(workspace_,kind=="title_block",name,directory_/std::filesystem::u8path(name+(kind=="title_block"?".tblz":".frmz")));
        activate(id);change_=Change{ChangeKind::New,id};return Result::success(data(workspace_,id));
    });
    add({"template.open",tr("Open a native drawing template while preserving any open edits."),{{"path",true}},true},[this](const Json& args){
        const auto target=path(args,directory_);if(options_.progress)options_.progress(Activity::Read,target);
        const auto id=workspace::open_drawing_template(workspace_,target);activate(id);change_directory(target.parent_path());change_=Change{ChangeKind::Open,id};return Result::success(data(workspace_,id));
    });
    add({"template.save",tr("Save a drawing template or an independent copy."),{{"path",false},{"copy",false,Type::Boolean},{"overwrite",false,Type::Boolean},{"document",false}},true},[this](const Json& args){
        const auto checked=target(args);if(!checked.ok)return checked;const auto id=workspace_.active_document_id();
        static_cast<void>(workspace::drawing_template_sketch(workspace_,id));const auto target=args.contains("path")?path(args,directory_):workspace_.open_part(id)->path;
        if(options_.progress)options_.progress(Activity::Write,target);
        const bool copy=args.value("copy",false);workspace::save_drawing_template(workspace_,id,target,copy,args.value("overwrite",false));
        change_directory(target.parent_path());change_=Change{copy?ChangeKind::Copy:ChangeKind::Save,id};auto result=data(workspace_,id);result["paths"]=Json::array({path_text(target)});result["copy"]=copy;return Result::success(std::move(result));
    });
    add({"template.sketch.edit",tr("Edit template geometry in one atomic Sketch batch."),{{"operations",true,Type::Array},{"document",false}},true},[this](const Json& args){
        const auto checked=target(args);if(!checked.ok)return checked;const auto id=workspace_.active_document_id();const auto& operations=args.at("operations");
        if(operations.empty()||operations.size()>1000)return Result::failure("invalid_arguments",tr("A template Sketch batch requires 1 to 1000 operations."));
        auto draft=workspace::drawing_template_sketch(workspace_,id);const auto batch=sketch_draft_dispatcher(draft);auto results=Json::array();
        for(std::size_t i=0;i<operations.size();++i){
            if(operations[i].is_object()&&operations[i].contains("command")&&operations[i].at("command").is_string()&&operations[i].at("command").get<std::string>().starts_with("sketch.reference."))
                return Result{false,"invalid_reference",tr("A drawing template cannot depend on external model references."),{{"operation_index",i}}};
            auto result=batch.execute(operations[i]);if(!result.ok){result.data={{"operation_index",i}};return result;}results.push_back(std::move(result.data));
        }
        const bool changed=workspace::commit_template_sketch(workspace_,id,std::move(draft));if(changed)change_=Change{ChangeKind::Model,id,true};
        auto result=data(workspace_,id);result["changed"]=changed;result["results"]=std::move(results);result["body_calculated"]=false;return Result::success(std::move(result));
    });
}
}
