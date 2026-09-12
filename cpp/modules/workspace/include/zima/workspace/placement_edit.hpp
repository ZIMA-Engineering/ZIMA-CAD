#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <string_view>

namespace zima::workspace {
[[nodiscard]] bool placement_angle_uses_reference_correction(
    const std::vector<document::ConstructionReference>&,
    const kernel::ViewerReferenceGeometry&, const kernel::Vec3& origin, std::size_t axis);
// Mutates only the caller's draft. Keys are x/y/z, rotation_x/y/z or
// reference_offset:N (the Nth populated positional reference). A false result
// means unavailable/locked, not a partially applied edit. Values are mm/degrees.
[[nodiscard]] bool assign_placement_dimension(document::Placement&,
    const kernel::ViewerReferenceGeometry&, std::string_view key, double value);
[[nodiscard]] bool assign_placement_dimension(document::ConstructionObject&,
    const kernel::ViewerReferenceGeometry&, std::string_view key, double value);
// Existing inline Part parameter transaction: owned profile offset, resolved
// references, body calculation and final exact ShaftThread fingerprints.
void commit_part_parameter_edit(PartState&, const kernel::OcctKernel&,
    document::PartDocument draft, const std::string& owner, bool parameter,
    const PartCalculationPolicy& = {});
// Existing Construction Properties OK / inline transaction. No body calculation.
enum class ConstructionEditMode { Create, Replace };
[[nodiscard]] bool commit_construction(Workspace&, const std::string& document,
    document::ConstructionObject, ConstructionEditMode);
struct PlacementInfo {
    std::string kind, coordinate_system, coordinate_owner, body;
    document::Placement placement;
};
class PlacementEditError : public std::runtime_error {
public:
    const char* code;
    PlacementEditError(const char* code, const char* message) : std::runtime_error(message), code(code) {}
};
[[nodiscard]] PlacementInfo read_placement(const Workspace&, const std::string& document,
    const std::string& object);
[[nodiscard]] kernel::ViewerReferenceGeometry placement_edit_geometry(
    const Workspace&, const std::string& document, const std::string& object);
using PlacementValuePatch = std::map<std::string, double>;
[[nodiscard]] bool set_placement_values(Workspace&, const kernel::OcctKernel&,
    const std::string& document, const std::string& object, const PlacementValuePatch&);
} // namespace zima::workspace
