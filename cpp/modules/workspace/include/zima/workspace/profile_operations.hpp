#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <stdexcept>

namespace zima::workspace {
class ProfileOperationError : public std::runtime_error {
public:
    ProfileOperationError(const char* code, const char* message) : std::runtime_error(message), code(code) {}
    const char* code;
};
enum class ProfileEditMode { Create, Replace, TransformSketch };
void validate_profile_definition(const document::HistoryContainer&);
// Assembly cutters affect immediate Part occurrences, never their source files.
void commit_assembly_profile(Workspace&, const kernel::OcctKernel&, const std::string& document_id,
    document::HistoryContainer, std::vector<std::string> targets, ProfileEditMode,
    const std::optional<sketcher::Sketch>& owned_sketch = {});
void normalize_owned_profile_front_references(std::vector<document::ConstructionReference>&);
[[nodiscard]] std::string revolution_axis_segment_id(const sketcher::Sketch&, const std::string& configured_id = {});
[[nodiscard]] document::HistoryContainer profile_from_sketch(const document::PartDocument&,
    const std::string& sketch_id, document::FeatureKind);
[[nodiscard]] document::HistoryContainer profile_from_sketch(const assembly::AssemblyDocument&,
    const std::string& sketch_id, document::FeatureKind);
// Read one original target in an existing container or Body frame. Selection
// uses this query; commit additionally validates source order and ownership.
[[nodiscard]] document::ExtrusionParameters::EndTarget resolve_profile_end_target(
    const document::PartDocument&,const std::vector<kernel::BodyResult>&,
    const std::string& frame_owner,const document::ExtrusionParameters::EndTarget&);
[[nodiscard]] document::ExtrusionParameters::EndTarget prepare_profile_end_target(
    const document::PartDocument&,const std::vector<kernel::BodyResult>&,
    const document::HistoryContainer&,const document::ExtrusionParameters::EndTarget&);
// Refresh only successful original references; calculation errors retain their
// last useful target geometry and the original identity needed for repair.
bool refresh_profile_end_targets(document::PartDocument&,const std::vector<kernel::BodyResult>&);

// Same explicit OK transaction for GUI and CLI. A profile owns its Sketch;
// numerical equality alone cannot prove that its geometry is unchanged.
void commit_profile(Workspace&, const kernel::OcctKernel&, const std::string& document_id,
    document::HistoryContainer, ProfileEditMode, const std::optional<sketcher::Sketch>& owned_sketch = {});
} // namespace zima::workspace
