#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_annotation_operations.hpp>
#include <algorithm>
namespace zima::command_host {
namespace {
using Reference=drawing::ModelAnnotationReference;
void invalid(){throw workspace::DrawingOperationError("invalid_arguments","Invalid Show/Erase arguments.");}
void keys(const Json& value,const std::set<std::string>& allowed) {
    if(!value.is_object())invalid();for(const auto& [key,item]:value.items())if(!allowed.contains(key))invalid();
}
std::string string(const Json& object,const char* key,const char* fallback=nullptr) {
    if(!object.contains(key)){if(fallback)return fallback;invalid();}
    if(!object[key].is_string())invalid();return object[key].get<std::string>();
}
drawing::ModelAnnotationKind kind(const std::string& value) {
    if(value=="dimension")return drawing::ModelAnnotationKind::Dimension;
    if(value=="axis")return drawing::ModelAnnotationKind::Axis;
    if(value=="construction")return drawing::ModelAnnotationKind::Construction;
    invalid();return {};
}
Reference reference(const Json& value) {
    keys(value,{"source_document","owner","key","instance_path"});
    Reference ref{string(value,"source_document"),string(value,"owner"),string(value,"key"),string(value,"instance_path")};
    if(ref.document_id.empty()||ref.owner_id.empty()||ref.semantic_id.empty())invalid();return ref;
}
Json reference_json(const Reference& value){return {{"source_document",value.document_id},{"owner",value.owner_id},{"key",value.semantic_id},{"instance_path",value.instance_path}};}
workspace::ShowEraseRequest request(const Json& value) {
    keys(value,{"view","mode","selection","kinds","selected"});workspace::ShowEraseRequest result;
    result.view=string(value,"view");const auto mode=string(value,"mode","show"),selection=string(value,"selection","keep_selected");
    if(mode!="show"&&mode!="erase")invalid();result.mode=mode=="show"?drawing::ShowEraseMode::Show:drawing::ShowEraseMode::Erase;
    if(selection!="keep_selected"&&selection!="remove_selected")invalid();result.selection=selection=="keep_selected"?drawing::ShowEraseSelection::KeepSelected:drawing::ShowEraseSelection::RemoveSelected;
    if(value.contains("kinds")) {
        if(!value["kinds"].is_array())invalid();for(const auto& item:value["kinds"]){if(!item.is_string()||!result.kinds.insert(kind(item.get<std::string>())).second)invalid();}
    }else result.kinds={drawing::ModelAnnotationKind::Dimension,drawing::ModelAnnotationKind::Axis,drawing::ModelAnnotationKind::Construction};
    if(!value.contains("selected")||!value["selected"].is_array())invalid();
    for(const auto& item:value["selected"])if(!result.selected.insert(reference(item)).second)invalid();
    return result;
}
}
void Host::register_drawing_annotation_commands() {
    using Type=commands::ArgumentType;
    dispatcher_.add({"drawing.annotation.list",tr("List stored model annotations and Show/Erase candidates without source loading."),{{"view",false},{"kind",false},{"mode",false},{"limit",false,Type::Integer},{"document",false}},false},[this](const Json& args){
        const auto id=args.value("document",workspace_.active_document_id());const auto* state=workspace_.open_drawing(id);
        if(!state)return Result::failure("unsupported_document",tr("Drawing commands require an open Drawing."));
        try {
            const auto selected=args.value("view",std::string{}),filter=args.value("kind",std::string("all")),mode=args.value("mode",std::string("all"));const auto limit=args.value("limit",2000.0);
            if(limit<1||limit>10000)return Result::failure("invalid_arguments",tr("Drawing query limit must be from 1 to 10000."));
            if(mode!="all"&&mode!="show"&&mode!="erase")invalid();if(filter!="all")static_cast<void>(kind(filter));
            if(!selected.empty()&&!state->document().find_view(selected))throw workspace::DrawingOperationError("view_not_found","The drawing view does not exist.");
            Json rows=Json::array();std::size_t total=0;
            const std::set kinds=filter=="all"?std::set{drawing::ModelAnnotationKind::Dimension,drawing::ModelAnnotationKind::Axis,drawing::ModelAnnotationKind::Construction}:std::set{kind(filter)};
            for(const auto& sheet:state->document().sheets)for(const auto& view:sheet.views)if(selected.empty()||view.id==selected) {
                std::set<Reference> offered;
                if(mode!="all"){const auto items=drawing::show_erase_candidates(view.model_annotations,mode=="show"?drawing::ShowEraseMode::Show:drawing::ShowEraseMode::Erase,kinds);offered={items.begin(),items.end()};}
                for(const auto& item:view.model_annotations)if(kinds.contains(item.kind)&&(mode=="all"||offered.contains(item.source))) {
                    ++total;if(rows.size()>=static_cast<std::size_t>(limit))continue;
                    rows.push_back({{"view",view.id},{"sheet",sheet.id},{"reference",reference_json(item.source)},
                        {"kind",item.kind==drawing::ModelAnnotationKind::Dimension?"dimension":item.kind==drawing::ModelAnnotationKind::Axis?"axis":"construction"},
                        {"visible",item.visible},{"unresolved",item.unresolved},{"text",item.text},{"value",item.value},{"curve_count",item.curves.size()}});
                }
            }
            return Result::success({{"document",id},{"revision",state->revision()},{"items",std::move(rows)},{"total",total}});
        }catch(const workspace::DrawingOperationError& e){return Result::failure(e.code,tr(e.what()));}
         catch(const std::exception& e){return Result::failure("drawing_failed",tr(e.what()));}
    });
    dispatcher_.add({"drawing.annotation.show_erase",tr("Apply shared Show/Erase selections to one or more drawing views."),{{"views",true,Type::Array},{"document",false}},true},[this](const Json& args){
        const auto checked=target(args);if(!checked.ok)return checked;
        const auto id=args.value("document",workspace_.active_document_id());auto* state=workspace_.open_drawing(id);
        if(!state)return Result::failure("unsupported_document",tr("Drawing commands require an open Drawing."));
        try {
            if(args["views"].empty()||args["views"].size()>1000)invalid();std::vector<workspace::ShowEraseRequest> requests;
            for(const auto& value:args["views"])requests.push_back(request(value));
            auto next=state->document();const bool changed=workspace::show_erase_drawing_annotations(next,requests);
            if(changed){state->commit(std::move(next));change_=Change{ChangeKind::Model,id,true};}
            return Result::success({{"document",id},{"revision",state->revision()},{"changed",changed},{"views",requests.size()}});
        }catch(const workspace::DrawingOperationError& e){return Result::failure(e.code,tr(e.what()));}
         catch(const std::exception& e){return Result::failure("drawing_failed",tr(e.what()));}
    });
}
}
