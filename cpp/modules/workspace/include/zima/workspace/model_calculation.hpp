#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <optional>
#include <set>
#include <functional>

namespace zima::workspace {

// Owner-thread scope for dispatching only calculation on private candidate data.
// The runner waits for completion; Workspace commits stay on their caller thread.
// Native/CLI callers retain synchronous execution without an installed runner.
class CalculationExecutionScope final {
public:
    using Runner = std::function<void(std::function<void()>)>;
    explicit CalculationExecutionScope(Runner runner);
    ~CalculationExecutionScope();
    CalculationExecutionScope(const CalculationExecutionScope&) = delete;
    CalculationExecutionScope& operator=(const CalculationExecutionScope&) = delete;
private:
    Runner previous_;
};

// Runtime validation of an explicit calculation. Recovery is the default;
// feature editing can reject errors only at its rollback boundary.
struct PartCalculationPolicy {
    bool reject_errors{};
    std::string edited_document_id;
    std::optional<std::size_t> edited_history_limit;
};

// Validated new definitions and already failed features retain their native
// history entry even when geometry cannot be calculated. Editing a working
// feature retains its established atomic rejection contract.
[[nodiscard]] PartCalculationPolicy feature_definition_calculation_policy(
    const document::PartDocument& before,
    const std::vector<kernel::BodyResult>& previous,
    const std::string& container_id);

[[nodiscard]] std::vector<kernel::BodyResult> calculate_part(
    const kernel::OcctKernel& kernel, const document::PartDocument& document,
    const std::vector<kernel::BodyResult>* previous = nullptr,
    const PartCalculationPolicy& policy = {});
[[nodiscard]] std::vector<kernel::BodyResult> calculate_part_with_resolved_references(
    const kernel::OcctKernel& kernel, document::PartDocument& document,
    const std::vector<kernel::BodyResult>* previous = nullptr,
    const PartCalculationPolicy& policy = {});
// Explicit interactive calculation also resolves Assembly-owned cutter references.
// Workspace's dependency refresh continues using its existing cached-cut path.
void calculate_resolved_assembly_cuts(
    const kernel::OcctKernel& kernel, assembly::AssemblyDocument& document);

struct PartRegenerationResult { bool references_changed{}; };
// Synchronous operations on the Workspace owner thread. No activation or View work.
[[nodiscard]] PartRegenerationResult regenerate_part(
    Workspace& workspace, const kernel::OcctKernel& kernel,
    const std::string& document_id, const PartCalculationPolicy& policy = {});
void regenerate_assembly(Workspace& workspace, const kernel::OcctKernel& kernel,
    const std::string& document_id, const PartCalculationPolicy& policy = {});

void append_reference_geometry(
    zima::kernel::ViewerReferenceGeometry& target,
    zima::kernel::ViewerReferenceGeometry source);

// An optional explicit Body scopes a new, uncommitted Sketch to its insertion
// cursor. Persisted Sketches always retain their own earlier-history boundary.
// An explicitly identified new Section Sketch may reference the complete Part.
std::set<std::string> sketch_external_reference_source_owners(
    const zima::document::PartDocument& document,
    const std::string& sketch_id, const std::string& draft_body_id = {},
    bool draft_section = false);

zima::kernel::ViewerReferenceGeometry sketch_external_reference_source_geometry(
    const zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries);

zima::kernel::ViewerReferenceGeometry construction_reference_source_geometry(
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries);

zima::kernel::ViewerReferenceGeometry part_construction_dimension_geometry(
    const zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries);

bool refresh_sketch_external_references(
    zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries);

bool prune_missing_drill_point_references(
    zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& boundaries);

bool refresh_assembly_sketch_external_references(
    zima::assembly::AssemblyDocument& document);

} // namespace zima::workspace
