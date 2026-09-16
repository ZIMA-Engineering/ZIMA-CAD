#pragma once
#include <zima/document/part_document.hpp>
#include <zima/document/metadata.hpp>

namespace zima::document {
[[nodiscard]] BendParameters resolved_bend_parameters(const HistoryContainer&, const SheetMetalDefaults&);
[[nodiscard]] kernel::FeatureGroupRequest bend_request(const HistoryContainer&, const sketcher::Sketch&, const SheetMetalDefaults&);
[[nodiscard]] kernel::ViewerMesh bend_preview(const HistoryContainer&, const sketcher::Sketch&, const SheetMetalDefaults&);
}
