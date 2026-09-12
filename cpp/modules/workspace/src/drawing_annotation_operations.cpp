#include <zima/workspace/drawing_annotation_operations.hpp>
#include <algorithm>
namespace zima::workspace {
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
