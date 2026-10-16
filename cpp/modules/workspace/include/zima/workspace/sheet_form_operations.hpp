#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>
namespace zima::workspace {
[[nodiscard]] bool commit_sheet_form(Workspace&,const kernel::OcctKernel&,
    const std::string& document,document::HistoryContainer);
[[nodiscard]] bool set_sheet_form_dimension(Workspace&,const kernel::OcctKernel&,
    const std::string& document,const std::string& feature,const std::string& dimension,double value);
}
