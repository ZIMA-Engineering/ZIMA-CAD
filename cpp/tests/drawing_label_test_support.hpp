#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <zima/workspace/drawing_view_operations.hpp>
namespace drawing_label_test {
inline zima::drawing::DrawingDocument fixture() {
    using namespace zima;auto doc=drawing::DrawingDocument::create_default();
    auto view=drawing::DrawingDocument::create_view("unavailable-source","missing-label-source.prtz",{},drawing::ViewOrientation::Top);
    view.name="Caption";view.show_caption=true;view.x=75;view.y=90;view.scale=2;view.use_sheet_scale=false;
    drawing::ProjectedEdge edge;edge.points={{-5,-5},{5,-5},{5,5},{-5,5},{-5,-5}};view.projected_edges={edge};
    auto marker=document::create_section();static_cast<void>(marker.sketch.add_segment(-1,0,1,0));view.section_markers={marker};
    workspace::validate_drawing_view(view);
    auto other=view;other.id=drawing::DrawingDocument::create_view("unavailable-source",{},{}).id;other.x=140;
    doc.sheets.front().views={view,other};return doc;
}
}
