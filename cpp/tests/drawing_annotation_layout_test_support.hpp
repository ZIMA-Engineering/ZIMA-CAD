#pragma once
#include <zima/drawing/model_annotations.hpp>
#include <zima/workspace/drawing_view_operations.hpp>
namespace annotation_layout_test {
inline auto snapshot(const zima::drawing::DrawingDocument& doc) {
    std::map<std::pair<std::string,std::string>,std::vector<zima::drawing::ModelAnnotation>> result;
    for(const auto& sheet:doc.sheets)for(const auto& view:sheet.views)result.emplace(std::pair{sheet.id,view.id},view.model_annotations);
    return result;
}
inline zima::drawing::DrawingDocument fixture() {
    using namespace zima;auto doc=drawing::DrawingDocument::create_default();
    auto view=drawing::DrawingDocument::create_view("unavailable-source","missing.prtz",{});
    view.camera={{1,0,0},{0,1,0},{0,0,-1}};
    zima::workspace::validate_drawing_view(view);
    kernel::ViewerDimension dimension;dimension.reference={"profile","dimension:length",{}};
    dimension.witness_first={0,0,0};dimension.witness_second={10,0,0};dimension.line_first={0,4,0};dimension.line_second={10,4,0};
    dimension.plane_normal={0,0,1};dimension.value=10;dimension.unit_suffix="mm";
    drawing::ModelAnnotation item;item.source={"unavailable-source","profile","dimension:length","first"};
    item.visible=true;item.model_dimension=dimension;item.model_envelope.include({0,0,0});item.model_envelope.include({10,8,0});
    item=drawing::project_model_annotation(view,item);auto other=item;other.source.instance_path="second";
    auto missing=item;missing.source.semantic_id="dimension:missing";missing.unresolved=true;
    auto axis=item;axis.source.semantic_id="axis:primary";axis.kind=drawing::ModelAnnotationKind::Axis;axis.model_dimension.reset();
    view.model_annotations={item,other,missing,axis};auto second=view;
    second.id=drawing::DrawingDocument::create_view("unavailable-source","missing.prtz",{}).id;
    doc.sheets.front().views={view,second};return doc;
}
}
