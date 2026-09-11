#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <optional>
#include <set>

namespace zima::workspace {

// Runtime validation of an explicit calculation. Recovery is the default;
// feature editing can reject errors only at its rollback boundary.
struct PartCalculationPolicy {
    bool reject_errors{};
    std::string edited_document_id;
    std::optional<std::size_t> edited_history_limit;
};

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

std::set<std::string> sketch_external_reference_source_owners(
    const zima::document::PartDocument& document,
    const std::string& sketch_id);

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
