#pragma once
#include <zima/document/part_document.hpp>
#include <zima/document/metadata.hpp>
#include <zima/kernel/sheet_material.hpp>

namespace zima::document {
[[nodiscard]] kernel::SheetMaterialDefinition sheet_material_definition(const HistoryContainer&,
    const sketcher::Sketch&,const kernel::PrimitiveRequest&,const SheetMetalDefaults&);
[[nodiscard]] inline bool is_sheet_state(FeatureKind kind) {
    return kind==FeatureKind::Unbend||kind==FeatureKind::BendBack;
}
}
