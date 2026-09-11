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

void AssemblyWorkspaceWindow::calculate_assembly_cuts(
    zima::assembly::AssemblyDocument& document) const {
    workspace::calculate_resolved_assembly_cuts(kernel_, document);
}

} // namespace zima::app
