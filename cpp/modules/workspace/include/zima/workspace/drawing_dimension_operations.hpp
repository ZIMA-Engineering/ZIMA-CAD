#pragma once
#include <zima/drawing/drawing_document.hpp>
namespace zima::workspace {
// Remove only the requested measured dimension. Number allocation belongs to
// the document and is retained by its normal history transaction.
bool erase_drawing_dimension(drawing::DrawingSheet&, const std::string& id);
}
