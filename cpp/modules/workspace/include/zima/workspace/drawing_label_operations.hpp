#pragma once
#include <zima/workspace/drawing_operations.hpp>
namespace zima::workspace {
enum class DrawingLabel { Caption, Section };
// Paper millimetres relative to the view origin, right/up. Null restores automatic placement.
bool set_drawing_label_position(drawing::DrawingView&,DrawingLabel,std::optional<drawing::Point2>);
const document::SectionDefinition& drawing_section_marker(const drawing::DrawingView&,const std::string&);
// minimum_mm is supplied by section_trace_layout, as it is for the GUI drag session.
// Only stored presentation is changed; no source loading or geometry calculation.
bool set_drawing_section_end(drawing::DrawingView&,const std::string&,std::size_t end,double offset_mm,double minimum_mm);
bool reset_drawing_section_ends(drawing::DrawingView&,const std::string&);
}
