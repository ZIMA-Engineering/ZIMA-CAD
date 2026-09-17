#pragma once
#include <zima/document/part_document.hpp>
#include <zima/document/metadata.hpp>

namespace zima::document {
[[nodiscard]] std::vector<ConstructionReference> flat_sheet_references(
    const kernel::ViewerEdge&, const kernel::VertexReference& start = {});
void update_flat_sheet_attachment(HistoryContainer&, sketcher::Sketch&,
    const kernel::ViewerReferenceGeometry&, const std::string& document_id);
[[nodiscard]] double flat_thickness(const HistoryContainer&, const SheetMetalDefaults&);
[[nodiscard]] kernel::ExtrusionRequest flat_request(const HistoryContainer&, const sketcher::Sketch&, const SheetMetalDefaults&);
[[nodiscard]] kernel::ViewerMesh flat_preview(const HistoryContainer&, const sketcher::Sketch&, const SheetMetalDefaults&);
}
