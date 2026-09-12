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
enum class SweepEditMode { Create, Replace, AdoptSources, ReplaceAdoptSources };
struct SweepProfileSource {
    std::string sketch_id;
    std::string point_id;
    bool incoming{};
    std::string correspondence_start_point_id;
};
[[nodiscard]] document::Sweep3DProfile sweep_profile_from_source(const document::PartDocument&,
    const std::string& feature_id, const SweepProfileSource&);
// Transfer the definitions of standalone native inputs into a new owned feature.
// The caller obtains path_placement through the existing placement query.
[[nodiscard]] document::HistoryContainer sweep3d_from_sources(const document::PartDocument&,
    const std::string& path_id, const document::Placement& path_placement,
    const std::vector<SweepProfileSource>&);
// One explicit calculation/commit for 2D Sweep, 3D Sweep/Loft and Helical Sweep.
// The supplied definition owns its embedded Sketches and path Point identities.
void commit_sweep(Workspace&, const kernel::OcctKernel&, const std::string& document_id,
    document::HistoryContainer, SweepEditMode);
} // namespace zima::workspace
