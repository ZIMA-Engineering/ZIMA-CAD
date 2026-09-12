#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <stdexcept>

namespace zima::workspace {
class SweepOperationError : public std::runtime_error {
public:
    SweepOperationError(const char* code, const char* message)
        : std::runtime_error(message), code(code) {}
    const char* code;
};
enum class SweepEditMode { Create, Replace };
// One explicit calculation/commit for 2D Sweep, 3D Sweep/Loft and Helical Sweep.
// The supplied definition owns its embedded Sketches and path Point identities.
void commit_sweep(Workspace&, const kernel::OcctKernel&, const std::string& document_id,
    document::HistoryContainer, SweepEditMode);
} // namespace zima::workspace
