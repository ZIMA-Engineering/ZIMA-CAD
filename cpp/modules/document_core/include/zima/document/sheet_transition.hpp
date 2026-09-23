#pragma once
#include <zima/document/part_document.hpp>
namespace zima::document {
[[nodiscard]] HistoryContainer create_sheet_transition();
void reframe_sheet_transition(HistoryContainer&);
[[nodiscard]] kernel::ViewerMesh sheet_transition_preview(const HistoryContainer&);
[[nodiscard]] kernel::ViewerReferenceGeometry sheet_transition_end_references(const HistoryContainer&);
[[nodiscard]] ContainerOrigin sheet_transition_end_origin(const HistoryContainer&);
[[nodiscard]] kernel::HistoryOperation sheet_transition_operation(const PartDocument&,const HistoryContainer&);
}
