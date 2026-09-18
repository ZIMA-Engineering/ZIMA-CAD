#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <zima/document/sheet_state.hpp>
namespace zima::workspace {
[[nodiscard]] std::vector<kernel::SheetMaterialDefinition> sheet_state_regions(
    const document::PartDocument&,const std::string& edited={});
[[nodiscard]] bool commit_sheet_state(Workspace&,const kernel::OcctKernel&,
    const std::string& document_id,document::HistoryContainer);
}
