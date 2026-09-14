#pragma once
#include <zima/workspace/drawing_operations.hpp>
#include <zima/drawing/model_annotations.hpp>
namespace zima::workspace {
struct AnnotationVisibility {
    std::string view;
    drawing::ModelAnnotationReference reference;
    bool visible{};
};
struct ShowEraseRequest {
    std::string view;
    drawing::ShowEraseMode mode{drawing::ShowEraseMode::Show};
    drawing::ShowEraseSelection selection{drawing::ShowEraseSelection::KeepSelected};
    std::set<drawing::ModelAnnotationKind> kinds;
    std::set<drawing::ModelAnnotationReference> selected;
};
// Validate the complete batch before assigning any visibility. Source data,
// geometry, layouts and unrelated occurrences remain unchanged.
bool set_drawing_annotation_visibility(drawing::DrawingDocument&,const std::vector<AnnotationVisibility>&);
bool show_erase_drawing_annotations(drawing::DrawingDocument&,const std::vector<ShowEraseRequest>&);
// Exact stored occurrence; no source document loading or body calculation.
const drawing::ModelAnnotation& drawing_annotation(const drawing::DrawingDocument&,
    const std::string& view,const drawing::ModelAnnotationReference&);
// Shared Properties commit: validate/project a private annotation before replacing it.
bool set_drawing_annotation_layout(drawing::DrawingDocument&,const std::string& view,
    const drawing::ModelAnnotationReference&,const kernel::DimensionLayout&);

}
