#include <zima/workspace/drawing_view_operations.hpp>
#include <zima/command_host/host.hpp>
#include <zima/workspace/document_operations.hpp>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <stdexcept>

namespace zima::command_host {
namespace {
std::string path_text(const std::filesystem::path& path){const auto text=path.generic_u8string();return {text.begin(),text.end()};}
std::string trim(std::string text){
    const auto space=[](unsigned char c){return std::isspace(c)!=0;};
    const auto first=std::find_if_not(text.begin(),text.end(),space);
    if(first==text.end())return {};
    return {first,std::find_if_not(text.rbegin(),text.rend(),space).base()};
}
}
Json documents(const workspace::Workspace& workspace) {
    Json result=Json::array();
    for(const auto& state:workspace.documents())std::visit([&](const auto& value) {
        using State=std::decay_t<decltype(value)>;
        const auto& document=[&]() -> const auto& {
            if constexpr(std::is_same_v<State,workspace::DrawingState>)return value.document();
            else return value.session.document();
        }();
        Json row={{"id",document.document_id},{"name",document.name},{"path",path_text(value.path)},
            {"active",document.document_id==workspace.active_document_id()},
            {"displayed",document.document_id==workspace.displayed_document_id()}};
        if constexpr(std::is_same_v<State,workspace::DrawingState>) {row["type"]="drawing";row["dirty"]=value.is_dirty();row["revision"]=value.revision();}
        else {row["type"]=std::is_same_v<State,workspace::PartState>?"part":"assembly";
            row["dirty"]=value.session.is_dirty();row["revision"]=value.session.revision();}
        row["needs_save"]=workspace::document_needs_save(workspace,document.document_id);
        result.push_back(std::move(row));
    },state);
    return result;
}
Host::Host(workspace::Workspace& workspace,const kernel::OcctKernel& kernel,
    std::filesystem::path& directory,Options options)
    :workspace_(workspace),kernel_(kernel),directory_(directory),options_(std::move(options)){register_commands();}
Interaction Host::interaction() const{return options_.interaction?options_.interaction():Interaction{};}
std::string Host::tr(const char* text) const{return options_.translate?options_.translate(text):std::string(text);}
void Host::io(std::function<void()> task) const{if(options_.run_io)options_.run_io(std::move(task));else task();}
void Host::activate(const std::string& id){workspace_.activate(id);workspace_.display_top_level(id);}
Result Host::target(const Json& args) const {
    if(workspace_.active_document_id().empty())return Result::failure("no_document",tr("Není otevřený dokument."));
    if(args.contains("document")&&args["document"]!=workspace_.active_document_id())
        return Result::failure("document_changed",tr("Požadovaný dokument není aktivní."));
    return Result::success();
}
Result Host::run(const std::function<Result()>& execute){
    if(executing_)return Result::failure("busy",tr("Jiný příkaz právě probíhá."));
    struct Running {bool& flag;explicit Running(bool& flag):flag(flag){flag=true;}~Running(){flag=false;}} running(executing_);
    change_.reset();return execute();
}
Result Host::execute_text(std::string_view text){return run([&]{return dispatcher_.execute_text(text);});}
Result Host::execute(const Json& request){return run([&]{return dispatcher_.execute(request);});}
void Host::register_commands(){
    register_primitive_commands();
    register_document_commands();
    register_body_commands();
    register_history_commands();
    register_reference_commands();
    register_construction_commands();
    register_placement_commands();
    register_sketch_commands();
    register_sketch_curve_commands();
    register_sketch_relation_commands();
    register_sketch_dimension_commands();
    register_sketch_reference_commands();
    register_sketch_text_commands();
    register_sketch_spline_commands();
    register_import_commands();
    register_export_commands();
    register_metadata_commands();
    register_engineering_metadata_commands();
    register_component_commands();
    register_drawing_commands();
    register_drawing_annotation_commands();
    register_drawing_dimension_commands();
    register_drawing_title_commands();
    dispatcher_.set_guard([this](const commands::Command& command){
        if(!command.changes_state)return Result::success();
        const auto state=interaction();
        if(state.editing)return Result::failure("editing_in_progress",tr("Nejprve dokončete nebo zrušte otevřenou editaci."));
        if(workspace_.active_document_id()!=workspace_.displayed_document_id()||!state.active_occurrence.empty())
            return Result::failure("active_occurrence",tr("Nejprve ukončete aktivaci komponenty v sestavě."));
        return Result::success();
    });
    dispatcher_.add({"help",tr("Seznam příkazů a jejich argumentů."),{},false},[this](const Json&){return Result::success(dispatcher_.catalog());});
    dispatcher_.add({"documents",tr("Otevřené dokumenty a jejich identifikátory."),{},false},[this](const Json&){return Result::success(documents(workspace_));});
    dispatcher_.add({"context",tr("Aktivní dokument, výskyt a potvrzený výběr."),{},false},[this](const Json&){
        const auto state=interaction();
        const auto time=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        return Result::success({{"active_document",workspace_.active_document_id()},
            {"displayed_document",workspace_.displayed_document_id()},{"active_occurrence",state.active_occurrence},
            {"active_sketch",state.active_sketch},{"working_directory",path_text(directory_)},
            {"captured_at_unix_ms",time},{"selection",state.selection},{"hover",state.hover},
            {"camera",state.camera},{"pointer",state.pointer}});
    });
    dispatcher_.add({"tree",tr("Strom zobrazeného dokumentu bez výpočtu geometrie."),{{"document",false}},false},[this](const Json& args){
        return Result::success(model_tree(workspace_,args.value("document",workspace_.displayed_document_id())));
    });
    dispatcher_.add({"open",tr("Otevřít nativní dokument: open cesta."),{{"path",true}},true},[this](const Json& args){
        auto path=std::filesystem::u8path(args["path"].get<std::string>());
        if(path.is_relative())path=directory_/path;
        path=std::filesystem::absolute(path).lexically_normal();
        try{static_cast<void>(workspace::native_document_type(path));}
        catch(const std::invalid_argument&){return Result::failure("unsupported_format",tr("Konzole otevírá soubory prtz, asmz a drwz."));}
        if(options_.progress)options_.progress(Activity::Read,path);
        std::string id;
        if(const auto opened=workspace_.document_id_for_path(path))id=*opened;
        else {
            std::optional<workspace::PreparedNativeDocument> prepared;
            io([&]{prepared=workspace::read_native_document(path);});
            if(!prepared)throw std::runtime_error("I/O runner did not complete native reading");
            id=workspace::insert_native_document(workspace_,std::move(*prepared));
        }
        activate(id);directory_=path.parent_path();change_=Change{ChangeKind::Open,id};
        return Result::success(documents(workspace_));
    });
    dispatcher_.add({"new",tr("Nový dokument: new part|assembly|drawing název."),{{"type",true},{"name",true}},true},[this](const Json& args){
        const auto kind=args["type"].get<std::string>();
        if(kind!="part"&&kind!="assembly"&&kind!="drawing")return Result::failure("invalid_arguments","Expected part, assembly or drawing.");
        const auto name=trim(args["name"].get<std::string>());
        if(name.empty()||name=="."||name==".."||name.find_first_of("\\/:*?\"<>|")!=std::string::npos||name.back()=='.')
            return Result::failure("invalid_name",tr("Zadejte název bez cesty a přípony."));
        const auto suffix=kind=="part"?".prtz":kind=="assembly"?".asmz":".drwz";
        const auto path=directory_/std::filesystem::u8path(name+suffix);
        const auto settings=options_.settings?options_.settings():Settings{};
        auto prepared=workspace::prepare_new_native_document(workspace::native_document_type(path),name,path,settings.templates,settings.units);
        const auto id=workspace::insert_native_document(workspace_,std::move(prepared));
        activate(id);change_=Change{ChangeKind::New,id};return Result::success(documents(workspace_));
    });
    dispatcher_.add({"save",tr("Uložit aktivní dokument; volitelně ověřit jeho ID."),{{"document",false}},true},[this](const Json& args){
        auto check=target(args);if(!check.ok)return check;
        const auto id=workspace_.active_document_id();const auto* state=workspace_.find(id);
        const auto path=std::visit([](const auto& value){return value.path;},*state);
        if(path.empty()||interaction().template_document)
            return Result::failure("path_required",tr("Nejprve určete cestu dokumentu pomocí Uložit jako."));
        if(options_.progress)options_.progress(Activity::Write,path);
        auto job=workspace::prepare_document_save(workspace_,id,path);
        std::optional<workspace::SavedDocument> saved;
        io([&]{saved=job.write();});
        if(!saved)throw std::runtime_error("I/O runner did not complete native writing");
        if(!workspace::complete_document_save(workspace_,*saved))
            throw std::runtime_error(tr("Uložený dokument byl mezitím zavřen nebo změnil cestu."));
        directory_=path.parent_path();change_=Change{ChangeKind::Save,id};
        return Result::success(documents(workspace_));
    });
    dispatcher_.add({"regenerate",tr("Výslovně regenerovat aktivní model."),{{"document",false}},true},[this](const Json& args){
        auto check=target(args);if(!check.ok)return check;
        const auto id=workspace_.active_document_id();
        if(interaction().template_document)
            return Result::failure("unsupported_document",tr("Tento příkaz podporuje Part a Assembly."));
        if(auto* drawing=workspace_.open_drawing(id)) {
            try {
                auto next=drawing->document();const auto count=workspace::regenerate_drawing_views(next,&workspace_,drawing->path);
                if(count){drawing->commit(std::move(next));change_=Change{ChangeKind::Regenerate,id,true};}
                return Result::success(documents(workspace_));
            }catch(const workspace::DrawingOperationError& e){return Result::failure(e.code,tr(e.what()));}
             catch(const std::exception& e){return Result::failure("calculation_failed",tr(e.what()));}
        }
        change_=Change{ChangeKind::Regenerate,id};
        try{
            if(workspace_.open_part(id))static_cast<void>(workspace::regenerate_part(workspace_,kernel_,id));
            else workspace::regenerate_assembly(workspace_,kernel_,id);
        }catch(const std::exception& error){return Result::failure("calculation_failed",error.what());}
        if(const auto* part=workspace_.open_part(id))if(!part->session.calculated_boundaries().empty()){
            const auto& errors=part->session.calculated_boundaries().back().calculation_errors;
            if(!errors.empty())return Result{false,"calculation_errors",tr("Některé prvky nebyly vypočteny."),errors};
        }
        return Result::success(documents(workspace_));
    });
    for(bool redo:{false,true})dispatcher_.add({redo?"redo":"undo",redo?tr("Znovu provést změnu."):tr("Vrátit změnu."),{{"document",false}},true},[this,redo](const Json& args){
        auto check=target(args);if(!check.ok)return check;
        const auto direction=redo?workspace::HistoryDirection::Redo:workspace::HistoryDirection::Undo;
        if(!workspace::step_document_history(workspace_,workspace_.active_document_id(),direction))
            return Result::failure("nothing_to_undo_redo",tr("Žádná změna není k dispozici."));
        change_=Change{ChangeKind::History,workspace_.active_document_id()};return Result::success(documents(workspace_));
    });
    dispatcher_.add({"fit",tr("Přizpůsobit model velikosti pohledu."),{},true},[this](const Json& args){
        auto check=target(args);if(!check.ok)return check;
        if(workspace_.open_drawing(workspace_.displayed_document_id()))return Result::failure("unsupported_document",tr("Tento příkaz podporuje Part a Assembly."));
        if(!options_.fit)return Result::failure("view_unavailable","This host has no model View.");
        options_.fit();return Result::success();
    });
}
} // namespace zima::command_host
