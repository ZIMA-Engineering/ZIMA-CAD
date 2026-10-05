#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>
namespace zima::workspace {
[[nodiscard]] bool commit_surface_thicken(Workspace&,const kernel::OcctKernel&,const std::string&,document::HistoryContainer);
}
