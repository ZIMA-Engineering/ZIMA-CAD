#include <zima/drawing/view_breaks.hpp>
#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_annotation_operations.hpp>
#include <algorithm>
#include <zima/document/dimension_layout_json.hpp>
namespace zima::command_host {
namespace {
using Reference=drawing::ModelAnnotationReference;
void invalid(){throw workspace::DrawingOperationError("invalid_arguments","Invalid drawing annotation arguments.");}
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
Json annotation_details(const drawing::ModelAnnotation& item) {
    Json result={{"reference",reference_json(item.source)},{"kind",item.kind==drawing::ModelAnnotationKind::Dimension?"dimension":item.kind==drawing::ModelAnnotationKind::Axis?"axis":"construction"},
        {"visible",item.visible},{"unresolved",item.unresolved},{"text",item.text},{"value",item.value},{"curve_count",item.curves.size()},
        {"model_layout",document::dimension_layout_json(item.model_layout)},
        {"view_layout",item.view_layout?document::dimension_layout_json(*item.view_layout):Json(nullptr)}};
    const auto layout=item.view_layout.value_or(item.model_layout);
    result["layout"]=document::dimension_layout_json(layout);
    result["style"]=item.model_dimension?document::dimension_text_style_json(layout.text_style.value_or(kernel::dimension_text_style(*item.model_dimension))):Json(nullptr);
    result["editable"]=item.kind==drawing::ModelAnnotationKind::Dimension&&item.model_dimension.has_value();
    result["dimension_kind"]=item.model_dimension?Json(item.dimension_kind==kernel::ViewerDimensionKind::Angular?"angular":
        item.dimension_kind==kernel::ViewerDimensionKind::Radius?"radius":item.dimension_kind==kernel::ViewerDimensionKind::Diameter?"diameter":"linear"):Json(nullptr);
    return result;
}
kernel::DimensionLayout patched_annotation_layout(const drawing::ModelAnnotation& item,const Json& args) {
    const auto initial=item.view_layout.value_or(item.model_layout);auto layout=document::dimension_layout_json(initial);
    if(args.contains("layout"))for(const auto& [key,value]:args.at("layout").items()) {
        if(!layout.contains(key)||key=="text_style")invalid();
        if(key=="arrows_reversed"||key=="radius_center_line_hidden"){if(!value.is_boolean())invalid();}
        else if(key=="plane_quarter_turns"){if(!value.is_number_integer()||value<0||value>3)invalid();}
        else if(!(key=="envelope_offset"&&value.is_null())&&(!value.is_number()||!std::isfinite(value.get<double>())))invalid();
        layout[key]=value;
    }
    if(args.contains("style")&&!args.at("style").empty()) {
        if(!item.model_dimension)throw workspace::DrawingOperationError("unsupported_annotation","The annotation has no stored model dimension presentation.");
        auto style=document::dimension_text_style_json(initial.text_style.value_or(kernel::dimension_text_style(*item.model_dimension)));
        for(const auto& [key,value]:args.at("style").items()) {
            if(!style.contains(key)||(key=="decimals"?!value.is_number_integer():!value.is_string()))invalid();
            if(key=="decimals"&&(value<0||value>12))invalid();style[key]=value;
        }
        layout["text_style"]=std::move(style);
    }
    try{return document::dimension_layout_from_json(layout);}
    catch(const std::invalid_argument&){throw workspace::DrawingOperationError("invalid_arguments","Invalid model annotation layout.");}
}

}
void Host::register_drawing_annotation_commands() {
    using Type=commands::ArgumentType;
    for(const bool edit:{false,true}) {
        std::vector<commands::Argument> parameters={{"view",true},{"reference",true,Type::Object},{"document",false}};
        if(edit){parameters.push_back({"layout",false,Type::Object});parameters.push_back({"style",false,Type::Object});}
        dispatcher_.add({edit?"drawing.annotation.set":"drawing.annotation.get",
            edit?tr("Edit the local presentation of a stored model dimension through shared Drawing Properties."):
                 tr("Read an exact model annotation and its local presentation without loading sources."),parameters,edit},[this,edit](const Json& args) {
            if(edit){const auto checked=target(args);if(!checked.ok)return checked;}
            const auto id=args.value("document",workspace_.active_document_id());auto* state=workspace_.open_drawing(id);
            if(!state)return Result::failure("unsupported_document",tr("Drawing commands require an open Drawing."));
            try {
                const auto view=args.at("view").get<std::string>();const auto source=reference(args.at("reference"));
                bool changed=false;
                if(edit) {
                    if(!args.contains("layout")&&!args.contains("style"))invalid();
                    auto next=state->document();
                    const auto layout=patched_annotation_layout(workspace::drawing_annotation(next,view,source),args);
                    changed=workspace::set_drawing_annotation_layout(next,view,source,layout);
                    if(changed){state->commit(std::move(next));change_=Change{ChangeKind::Model,id};}
                }
                auto result=annotation_details(workspace::drawing_annotation(state->document(),view,source));
                result["document"]=id;result["view"]=view;result["revision"]=state->revision();if(edit)result["changed"]=changed;
                return Result::success(std::move(result));
            }catch(const workspace::DrawingOperationError& error){return Result::failure(error.code,tr(error.what()));}
             catch(const std::exception& error){return Result::failure("drawing_failed",tr(error.what()));}
        });
    }
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
                        {"visible",item.visible},{"hidden_by_break",drawing::break_annotation_hidden(view,item)},{"unresolved",item.unresolved},{"text",item.text},{"value",item.value},{"curve_count",item.curves.size()}});
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
