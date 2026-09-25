#pragma once
#include <zima/drawing/drawing_document.hpp>
namespace zima::drawing {
void validate_symbol_contacts(const DrawingSheet&);
// Resolves only cached original projected geometry. Missing references retain
// their last paper pose, including after the owning view is removed.
void refresh_symbol_contacts(DrawingSheet&);
// Translate cached paper presentation with its view, without projecting geometry.
void translate_symbol_contacts(DrawingSheet&,const std::string& view_id,Point2 delta);
bool move_symbol_contact(DrawingSheet&,const std::string&,Point2 paper_point);
bool move_symbol_contact(symbols::Placement&,const DrawingView&,SymbolContact&,Point2 paper_point);
void attach_symbol_to_view(symbols::Placement&,const DrawingView&,const DimensionAttachment&);
}
