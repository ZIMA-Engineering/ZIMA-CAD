#pragma once
#include <zima/document/metadata.hpp>
#include <zima/document/part_document.hpp>

namespace zima::document {
[[nodiscard]] kernel::LineBendRequest line_bend_request(
    const HistoryContainer&,const sketcher::Sketch&,
    const SheetMetalDefaults&);
[[nodiscard]] kernel::SheetMaterialDefinition line_bend_material_definition(
    const HistoryContainer&,const sketcher::Sketch&,
    const SheetMetalDefaults&);
}
