#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>
namespace zima::workspace {
bool commit_boundary_surface(Workspace&,const kernel::OcctKernel&,const std::string&,document::HistoryContainer);
}
