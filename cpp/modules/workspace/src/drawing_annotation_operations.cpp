#include <zima/workspace/drawing_annotation_operations.hpp>
#include <algorithm>
namespace zima::workspace {
const drawing::ModelAnnotation& drawing_annotation(const drawing::DrawingDocument& document,
    const std::string& view_id,const drawing::ModelAnnotationReference& reference) {
    const auto* view=document.find_view(view_id);
    if(!view)throw DrawingOperationError("view_not_found","The drawing view does not exist.");
    const drawing::ModelAnnotation* found=nullptr;
    for(const auto& item:view->model_annotations)if(item.source==reference) {
        if(found)throw DrawingOperationError("ambiguous_reference","The drawing annotation reference is ambiguous.");
        found=&item;
    }
    if(!found)throw DrawingOperationError("annotation_not_found","The drawing annotation does not exist.");
    return *found;
}
bool set_drawing_annotation_layout(drawing::DrawingDocument& document,const std::string& view_id,
    const drawing::ModelAnnotationReference& reference,const kernel::DimensionLayout& layout) {
    const auto& original=drawing_annotation(document,view_id,reference);
    if(original.kind!=drawing::ModelAnnotationKind::Dimension||!original.model_dimension)
        throw DrawingOperationError("unsupported_annotation","The annotation has no stored model dimension presentation.");
    try {kernel::validate_dimension_layout(layout);}
    catch(const std::invalid_argument&) {throw DrawingOperationError("invalid_arguments","Invalid model annotation layout.");}
    if(layout.text_style) {
        const auto& style=*layout.text_style;
        if(style.decimals<0||style.decimals>12||(style.tolerance_mode!=""&&style.tolerance_mode!="symmetric"&&
            style.tolerance_mode!="single_deviation"&&style.tolerance_mode!="deviations"))
            throw DrawingOperationError("invalid_arguments","Invalid model annotation layout.");
    }
    if(layout==original.view_layout.value_or(original.model_layout)&&original.paper_handles.empty())return false;
    auto value=original;value.view_layout=layout;value.paper_handles.clear();
    auto* view=document.find_view(view_id);
    value=drawing::project_model_annotation(*view,std::move(value));
    const auto target=std::ranges::find(view->model_annotations,reference,&drawing::ModelAnnotation::source);
    *target=std::move(value);
    return true;
}
bool set_drawing_annotation_visibility(drawing::DrawingDocument& document,const std::vector<AnnotationVisibility>& values) {
    std::set<std::pair<std::string,drawing::ModelAnnotationReference>> seen;
    std::vector<std::pair<drawing::ModelAnnotation*,bool>> changes;
    for(const auto& value:values) {
        if(!seen.emplace(value.view,value.reference).second)
            throw DrawingOperationError("invalid_arguments","An annotation may be specified only once per view.");
        auto* view=document.find_view(value.view);
        if(!view)throw DrawingOperationError("view_not_found","The drawing view does not exist.");
        auto* target=static_cast<drawing::ModelAnnotation*>(nullptr);
        for(auto& item:view->model_annotations)if(item.source==value.reference) {
            if(target)throw DrawingOperationError("ambiguous_reference","The drawing annotation reference is ambiguous.");
            target=&item;
        }
        if(!target)throw DrawingOperationError("annotation_not_found","The drawing annotation does not exist.");
        if(target->visible==value.visible)continue;
        if(target->unresolved)throw DrawingOperationError("unresolved_reference","An unresolved drawing annotation cannot change visibility.");
        changes.emplace_back(target,value.visible);
    }
    for(const auto& [item,visible]:changes)item->visible=visible;
    return !changes.empty();
}
bool show_erase_drawing_annotations(drawing::DrawingDocument& document,const std::vector<ShowEraseRequest>& requests) {
    std::set<std::string> views;std::vector<AnnotationVisibility> changes;
    for(const auto& request:requests) {
        if(!views.insert(request.view).second)throw DrawingOperationError("invalid_arguments","Each Show/Erase batch may include a view only once.");
        const auto* view=document.find_view(request.view);
        if(!view)throw DrawingOperationError("view_not_found","The drawing view does not exist.");
        if((request.mode!=drawing::ShowEraseMode::Show&&request.mode!=drawing::ShowEraseMode::Erase)||
           (request.selection!=drawing::ShowEraseSelection::KeepSelected&&request.selection!=drawing::ShowEraseSelection::RemoveSelected))
            throw DrawingOperationError("invalid_arguments","Invalid Show/Erase mode or selection.");
        for(const auto kind:request.kinds)if(kind<drawing::ModelAnnotationKind::Dimension||kind>drawing::ModelAnnotationKind::Construction)
            throw DrawingOperationError("invalid_arguments","Invalid drawing annotation kind.");
        drawing::ShowEraseSession session(*view);const auto offered=session.candidates(request.mode,request.kinds);
        for(const auto& reference:request.selected)if(std::ranges::find(offered,reference)==offered.end())
            throw DrawingOperationError("reference_not_offered","The selected annotation is not offered by Show/Erase.");
        for(const auto& item:session.preview(request.mode,request.selection,request.kinds,request.selected))
            changes.push_back({request.view,item.source,item.visible});
    }
    return set_drawing_annotation_visibility(document,changes);
}
}
