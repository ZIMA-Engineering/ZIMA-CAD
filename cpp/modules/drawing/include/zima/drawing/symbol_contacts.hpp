#pragma once
#include <zima/drawing/drawing_document.hpp>
namespace zima::drawing {
void validate_symbol_contacts(const DrawingSheet&);
// Resolves only cached original projected geometry. Missing references retain
// their last paper pose, including after the owning view is removed.
void refresh_symbol_contacts(DrawingSheet&);
void attach_symbol_to_view(symbols::Placement&,const DrawingView&,const DimensionAttachment&);
}
