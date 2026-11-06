#pragma once

#include <zima/workspace/model_calculation.hpp>

namespace zima::workspace {

struct PreparedSheetProfile {
    document::PartDocument document;
    // Child IDs follow the authored contour, independently of executable Body
    // history order (first Bend, start branch, end branch).
    std::vector<std::string> logical_children;
};

// Pure document preparation used by GUI/CLI commands and native tests. The
// supplied reference packet is used only to resolve the ordinary shared
// placement of the Profile's one owned Sketch.
[[nodiscard]] PreparedSheetProfile prepare_sheet_profile(
    const document::PartDocument&, document::HistoryContainer,
    sketcher::Sketch, kernel::ViewerReferenceGeometry references = {});

// Reconcile edits made through the ordinary first Flat/Bend editor back into
// the parent route, then regenerate the stable child group. Non-master
// generated children are edited through the parent Profile Sketch instead.
[[nodiscard]] PreparedSheetProfile prepare_sheet_profile_master_edit(
    const document::PartDocument&, document::HistoryContainer,
    sketcher::Sketch, kernel::ViewerReferenceGeometry references = {});

[[nodiscard]] bool commit_sheet_profile(
    Workspace&, const kernel::OcctKernel&, const std::string& document_id,
    document::HistoryContainer, sketcher::Sketch);

[[nodiscard]] bool commit_sheet_profile_master_edit(
    Workspace&, const kernel::OcctKernel&, const std::string& document_id,
    document::HistoryContainer, sketcher::Sketch);

// Remove only the non-geometric parent link. Ordinary generated Flats/Bends
// and their owned Sketches retain identity and executable order in one Undo
// transaction.
[[nodiscard]] bool explode_sheet_profile(
    Workspace&, const kernel::OcctKernel&, const std::string& document_id,
    const std::string& profile_id);

} // namespace zima::workspace
