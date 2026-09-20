#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/native_documents.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <zima/workspace/drawing_view_operations.hpp>
#include <cmath>
#include <zima/workspace/drawing_annotation_operations.hpp>
#include <algorithm>
namespace zima::workspace {
void set_drawing_model_dimension(Workspace& live,const kernel::OcctKernel& kernel,
    drawing::DrawingDocument& drawing,const std::filesystem::path& path,const std::string& view_id,
    const drawing::ModelAnnotationReference& reference,double value) {
    const auto& annotation=drawing_annotation(drawing,view_id,reference);
    if(!std::isfinite(value)||annotation.unresolved||!annotation.visible||!annotation.model_dimension ||
        !annotation.model_dimension->driving||annotation.model_dimension->locked||
        !annotation.model_dimension->display_text_override.empty())
        throw std::invalid_argument("Tato kóta není přímo editovatelná.");
    // Work on an isolated Workspace so failed calculation/projection cannot leave
    // the source changed while the Drawing still displays the previous value.
    auto staged=live;
    const auto ensure_open=[&](const std::string& id,std::filesystem::path source) {
        if(staged.find(id))return;
        if(source.empty())throw std::invalid_argument("Zdrojový dokument kóty není dostupný.");
        auto prepared=read_native_document(source,native_source_resolver(staged));
        if(prepared.id()!=id)throw std::invalid_argument("Zdroj kóty patří jinému dokumentu.");
        static_cast<void>(insert_native_document(staged,std::move(prepared)));
    };
    const auto* view=drawing.find_view(view_id);
    auto source_path=view->source_path;
    if(source_path.is_relative()&&!path.empty())source_path=path.parent_path()/source_path;
    auto source_id=view->source_document_id;
    ensure_open(source_id,source_path);
    for(const auto& occurrence:assembly::InstancePath::decode(reference.instance_path).occurrence_ids) {
        const auto* owner=staged.open_assembly(source_id);
        if(!owner)throw std::invalid_argument("Výskyt zdrojového modelu již není dostupný.");
        const auto* component=owner->session.document().find_occurrence(occurrence);
        if(!component||component->derived_copy)throw std::invalid_argument("Tento výskyt nemá upravitelný zdroj.");
        source_path=component->source_path;
        if(source_path.is_relative()&&!owner->path.empty())source_path=owner->path.parent_path()/source_path;
        source_id=component->source_document_id;
        ensure_open(source_id,source_path);
    }
    if(source_id!=reference.document_id)throw std::invalid_argument("Výskyt neodpovídá zdroji kóty.");
    const document::FamilyColumn binding{"dimension",reference.owner_id,reference.semantic_id};
    // Recheck current source locks/driving state, rather than trusting a cached label.
    const auto available=family_references(staged,reference.document_id);
    if(!std::ranges::any_of(available,[&](const auto& r){return r.binding==binding;}))
        throw std::invalid_argument("Kóta je odvozená, zamčená nebo již není dostupná.");
    if(auto* part=staged.open_part(reference.document_id)) {
        auto next=part->session.document();
        if(!assign_driving_dimension(next,binding,value))throw std::invalid_argument("Kótu nelze změnit.");
        PartCalculationPolicy policy;policy.reject_errors=true;
        commit_part_parameter_edit(*part,kernel,std::move(next),reference.owner_id,
            reference.semantic_id.starts_with("parameter:"),policy);
    } else if(auto* assembly=staged.open_assembly(reference.document_id)) {
        auto next=assembly->session.document();
        if(!assign_driving_dimension(next,binding,value))throw std::invalid_argument("Kótu nelze změnit.");
        calculate_resolved_assembly_cuts(kernel,next);
        assembly->session.commit(std::move(next));
    } else throw std::invalid_argument("Zdroj kóty není model.");
    staged.refresh_source_geometry();
    auto projected=drawing;
    static_cast<void>(regenerate_drawing_views(projected,&staged,path));
    staged.drawing_edited_sources[drawing.document_id].insert(reference.document_id);
    live=std::move(staged);
    drawing=std::move(projected);
}

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
