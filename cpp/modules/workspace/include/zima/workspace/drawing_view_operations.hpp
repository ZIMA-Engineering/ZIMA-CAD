#pragma once
#include <zima/workspace/drawing_operations.hpp>
namespace zima::workspace {
class DrawingProjection;
void validate_drawing_view(const drawing::DrawingView&);
drawing::Point2 projection_placement(drawing::ProjectionDirection,double distance);
// Edits only the Drawing draft. Pending source hatch metadata is committed by
// the GUI caller after all projections succeed; ordinary commands use source data.
void edit_drawing_view(drawing::DrawingDocument&,const std::string& sheet,
    drawing::DrawingView,bool creating,DrawingProjection&,bool pending_hatch=false);
// Whole-document draft operations: failure leaves the caller unchanged.
std::size_t regenerate_drawing_views(drawing::DrawingDocument&,const Workspace*,const std::filesystem::path& drawing_path);
std::vector<std::string> delete_drawing_view(drawing::DrawingDocument&,const std::string&);
}
