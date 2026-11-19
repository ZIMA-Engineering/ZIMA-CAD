#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>
namespace zima::workspace {
enum class LoftFailurePolicy { Reject, KeepDefinition };
bool commit_loft(Workspace&,const kernel::OcctKernel&,const std::string& document_id,document::HistoryContainer,
    LoftFailurePolicy=LoftFailurePolicy::Reject);
}
