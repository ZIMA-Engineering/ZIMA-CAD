#include "workspace_internal.hpp"

namespace zima::app {

zima::workspace::PartCalculationPolicy AssemblyWorkspaceWindow::part_calculation_policy() const {
    zima::workspace::PartCalculationPolicy policy;
    policy.reject_errors = properties_dialog_ != nullptr;
    if (part_rollback_) {
        policy.edited_document_id = part_rollback_->part_document_id;
        policy.edited_history_limit = part_rollback_->history_limit;
    }
    return policy;
}

std::optional<zima::document::HistoryRollbackBoundary>
AssemblyWorkspaceWindow::part_history_rollback_boundary(
        const std::string& container_id) {
    auto* part=workspace_.open_part(workspace_.active_document_id());
    if(!part)return std::nullopt;
    static_cast<void>(workspace::materialize_part_history_input_boundary(
        workspace_,kernel_,part->session.document().document_id,container_id));
    return part->session.rollback_boundary(container_id);
}

std::vector<zima::kernel::BodyResult> AssemblyWorkspaceWindow::calculate_part(
    const zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>* previous) const {
    return workspace::calculate_part(kernel_, document, previous, part_calculation_policy());
}

std::vector<zima::kernel::BodyResult>
AssemblyWorkspaceWindow::calculate_part_with_resolved_references(
    zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>* previous) const {
    return workspace::calculate_part_with_resolved_references(
        kernel_, document, previous, part_calculation_policy());
}

} // namespace zima::app
