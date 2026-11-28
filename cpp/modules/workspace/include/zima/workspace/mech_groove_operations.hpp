#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>

namespace zima::workspace {
[[nodiscard]] bool refresh_mech_grooves(document::PartDocument&,
    const kernel::ViewerReferenceGeometry&,const document::HistoryReferenceViews&);
[[nodiscard]] bool commit_mech_groove(Workspace&,const kernel::OcctKernel&,
    const std::string& document_id,document::HistoryContainer);
}
