#pragma once
#include <zima/workspace/model_calculation.hpp>

namespace zima::workspace {
[[nodiscard]] bool commit_line_bend(Workspace&,const kernel::OcctKernel&,
    const std::string&,document::HistoryContainer,sketcher::Sketch);
}
