#pragma once
#include <zima/document/sheet_form_definition.hpp>

namespace zima::document {
// Authored dimensions in the inserted definition's current frame.
// Uses persisted Sketch data only; never solves or calculates geometry.
[[nodiscard]] std::vector<kernel::ViewerDimension> sheet_form_internal_dimensions(
    const HistoryContainer&);
[[nodiscard]] bool sheet_form_internal_dimension_editable(const HistoryContainer&,const std::string&);
// Mutates only a private native definition, using ordinary Sketch dimension edits.
[[nodiscard]] bool edit_sheet_form_internal_dimension(PartDocument&,const std::string&,double);
[[nodiscard]] std::optional<double> sheet_form_internal_dimension_value(const PartDocument&,const std::string&);
inline bool is_sheet_form_internal_dimension(const std::string& key) {
    return key.starts_with("form:internal:");
}
} // namespace zima::document
