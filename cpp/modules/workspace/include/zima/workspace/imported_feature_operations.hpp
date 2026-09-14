#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/import_operations.hpp>

namespace zima::workspace {
// Read persisted parameters only; never opens the original interchange file.
[[nodiscard]] const document::HistoryContainer& imported_feature(
    const Workspace&, const std::string& document, const std::string& container);
// Existing imported-feature Properties OK transaction, shared with CLI.
// Source payload and topology identities cannot be replaced by a property edit.
[[nodiscard]] bool commit_imported_feature(Workspace&, const kernel::OcctKernel&,
    const std::string& document, document::HistoryContainer);
[[nodiscard]] bool set_imported_feature_reference(Workspace&, const kernel::OcctKernel&,
    const std::string& document, const std::string& container, std::size_t index,
    document::ConstructionReference, bool derive_orientation = true);
}
