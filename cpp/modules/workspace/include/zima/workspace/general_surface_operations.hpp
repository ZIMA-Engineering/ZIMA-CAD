#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>
namespace zima::workspace {
bool commit_general_surface(Workspace&,const kernel::OcctKernel&,const std::string& document_id,document::HistoryContainer);
}
