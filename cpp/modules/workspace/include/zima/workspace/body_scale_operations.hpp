#pragma once
#include <zima/workspace/body_operations.hpp>

namespace zima::workspace {
[[nodiscard]] BodyGraphEdit prepare_body_scale_edit(const document::PartDocument& document,
    const std::string& id={});
[[nodiscard]] bool commit_body_scale(Workspace& workspace,const kernel::OcctKernel& kernel,
    const BodyGraphEdit& edit,document::BodyHistory value);
} // namespace zima::workspace
