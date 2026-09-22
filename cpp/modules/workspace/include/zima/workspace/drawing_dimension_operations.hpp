#pragma once
#include <zima/drawing/drawing_document.hpp>
namespace zima::workspace {
// Remove only the requested measured dimension. Number allocation belongs to
// the document and is retained by its normal history transaction.
// Validate and resolve before changing the owning sheet; no source loading.
bool edit_drawing_dimension(drawing::DrawingDocument&,const std::string& sheet,
    drawing::DrawingDimension value,bool creating);
bool erase_drawing_dimension(drawing::DrawingSheet&, const std::string& id);
void synchronize_dimension_chain(drawing::DrawingSheet&,const drawing::DrawingDimension&);
bool commit_drawing_chain(drawing::DrawingDocument&,const std::string& sheet,
    drawing::DrawingDimension value,bool creating);
bool free_alignment_dimension(const drawing::DrawingSheet&,const std::string& id);
bool can_align_drawing_dimensions(const drawing::DrawingSheet&,const std::vector<std::string>& ids);
bool align_drawing_dimensions(drawing::DrawingSheet&,const std::vector<std::string>& ids);
}
