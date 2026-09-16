#pragma once
#include <zima/document/part_document.hpp>
#include <zima/document/metadata.hpp>

namespace zima::document {
[[nodiscard]] double flat_thickness(const HistoryContainer&, const SheetMetalDefaults&);
[[nodiscard]] kernel::ExtrusionRequest flat_request(const HistoryContainer&, const sketcher::Sketch&, const SheetMetalDefaults&);
[[nodiscard]] kernel::ViewerMesh flat_preview(const HistoryContainer&, const sketcher::Sketch&, const SheetMetalDefaults&);
}
