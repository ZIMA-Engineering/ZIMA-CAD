#pragma once
#include <zima/workspace/model_calculation.hpp>
namespace zima::workspace {
[[nodiscard]] bool commit_sheet_transition(Workspace&,const kernel::OcctKernel&,const std::string&,document::HistoryContainer);
}
