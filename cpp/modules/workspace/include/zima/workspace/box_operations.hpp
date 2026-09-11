#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <stdexcept>

namespace zima::workspace {
enum class BoxEditMode { Create, Replace };
class BoxOperationError : public std::runtime_error {
public:
    BoxOperationError(const char* code, const char* message)
        : std::runtime_error(message), code(code) {}
    const char* code;
};
// One explicit OK transaction for GUI and command hosts. Placement solving uses
// the existing pre-edit reference universe; no document is committed on failure.
// Replace preserves identity. A complete properties edit may also change locks.
// Returns false for an unchanged container (no calculation or Undo entry).
[[nodiscard]] bool commit_box(Workspace&, const kernel::OcctKernel&,
    const std::string& document_id, document::HistoryContainer, BoxEditMode);
// Parameter-only commands cannot override the locks in the saved model.
struct BoxDimensionPatch {
    std::optional<double> length, width, height;
};
[[nodiscard]] bool set_box_dimensions(Workspace&, const kernel::OcctKernel&,
    const std::string& document_id, const std::string& container_id, const BoxDimensionPatch&);
} // namespace zima::workspace
