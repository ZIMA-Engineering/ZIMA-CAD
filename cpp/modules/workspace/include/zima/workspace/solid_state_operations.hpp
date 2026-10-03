#pragma once
#include <zima/workspace/model_calculation.hpp>

namespace zima::workspace {
// Native history inspection only: opening a dialog never invokes OCCT.
[[nodiscard]] std::vector<std::string> solid_state_sources(
    const document::PartDocument&,bool restore,const std::string& edited={});
[[nodiscard]] bool commit_solid_state(Workspace&,const kernel::OcctKernel&,
    const std::string& document_id,document::HistoryContainer);
}
