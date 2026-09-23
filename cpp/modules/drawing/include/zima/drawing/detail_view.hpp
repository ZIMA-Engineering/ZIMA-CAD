#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <zima/drawing/view_crop.hpp>
namespace zima::drawing {
// An enlarged presentation of the parent's calculated content. No new body
// calculation, independent visibility inference or topology naming is involved.
inline void refresh_detail_view(DrawingView& detail,const DrawingView& parent) {
    if(!detail.detail_view||!detail.crop||detail.parent_view_id!=parent.id)
        throw std::invalid_argument("Invalid detail view parent or boundary.");
    validate_view_crop(detail);
    detail.source_document_id=parent.source_document_id;detail.source_path=parent.source_path;
    detail.camera=parent.camera;detail.orientation=parent.orientation;
    detail.projection_direction=ProjectionDirection::None;
    detail.display_style=parent.display_style;detail.hidden_edge_style=parent.hidden_edge_style;
    detail.tangent_edge_style=parent.tangent_edge_style;detail.show_thread_leadins=parent.show_thread_leadins;
    detail.projected_edges=parent.projected_edges;detail.projected_triangles=parent.projected_triangles;
    detail.output_source=parent.output_source;
    detail.measurement_geometry=parent.measurement_geometry;
    detail.model_annotations=parent.model_annotations;
    detail.section_id=parent.section_id;detail.section_snapshot=parent.section_snapshot;
    detail.section_display_reversed=parent.section_display_reversed;
    detail.section_hatch_crops=parent.section_hatch_crops;
    detail.hidden_hatch_components=parent.hidden_hatch_components;
    detail.section_markers.clear();detail.show_section_label=false;
    detail.breaks=parent.breaks;
    for(auto& item:detail.breaks)item.gap*=detail.scale/parent.scale;
    detail.inherited_crops=parent.inherited_crops;
    if(parent.crop)detail.inherited_crops.push_back(*parent.crop);
}
inline std::string next_detail_name(const DrawingDocument& document) {
    std::set<std::string> used;for(const auto& sheet:document.sheets)for(const auto& view:sheet.views)used.insert(view.name);
    for(unsigned i=0;;++i) {
        auto name=std::string(1,"XYZ"[i%3])+(i<3?std::string{}:std::to_string(i/3));
        if(!used.contains(name))return name;
    }
}
}
