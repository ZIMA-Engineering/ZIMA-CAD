#pragma once
#include <zima/workspace/placement_edit.hpp>
namespace zima::workspace {
struct PlacementReferenceRemovalResult {
    bool changed{};
    bool body_calculated{};
};
// Remove an existing row without looking up the source being removed. Uses the
// same pending-row operation and final domain transactions as Properties OK.
[[nodiscard]] PlacementReferenceRemovalResult remove_placement_reference(
    Workspace&, const kernel::OcctKernel&, const std::string& document,
    const std::string& object, std::size_t index);
}
