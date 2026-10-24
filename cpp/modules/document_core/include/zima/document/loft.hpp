#pragma once
#include <zima/document/part_document.hpp>
namespace zima::document {
[[nodiscard]] HistoryContainer loft_sketch_editor_carrier(const HistoryContainer&);
void resolve_loft_sketches(HistoryContainer&,const kernel::ViewerReferenceGeometry&);
[[nodiscard]] HistoryContainer create_loft();
void validate_loft(const HistoryContainer&);
[[nodiscard]] kernel::LoftRequest loft_request(const PartDocument&,const HistoryContainer&);
[[nodiscard]] kernel::ViewerMesh loft_definition_mesh(const PartDocument&,const HistoryContainer&);
}
