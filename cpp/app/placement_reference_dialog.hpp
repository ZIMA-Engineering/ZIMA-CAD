#pragma once

#include <zima/document/part_document.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <QString>

namespace zima::app {

// Common viewer-facing contract for every dialog that owns the shared
// ContainerPlacementSection. It lets Plane, Sketch and body containers use
// one picker, one ordered candidate list and one Tree bulk-fill path.
class PlacementReferenceDialog {
public:
    virtual ~PlacementReferenceDialog() = default;
    [[nodiscard]] virtual std::vector<zima::document::ConstructionReference>
        references_without(std::size_t index) const = 0;
    [[nodiscard]] virtual bool owns_reference_owner(
        const std::string& owner_id) const = 0;
    virtual bool set_reference(std::size_t index,
        zima::document::ConstructionReference reference,
        const QString& label) = 0;
    [[nodiscard]] virtual std::size_t first_empty_position_index() const = 0;
    virtual void set_active_reference_index(
        std::optional<std::size_t> index) = 0;
    virtual void set_reference_inspected(
        std::size_t index, bool inspected) = 0;
    virtual void clear_reference_highlights() = 0;
    virtual void set_translation_constraint_state(
        const zima::document::PointConstraintState& state,
        const zima::kernel::Vec3& solution) = 0;
    virtual void set_remaining_rotation_dof(int dof) = 0;
    virtual void set_rotation_constraint_state(
        const zima::document::OrientationConstraintState& state) = 0;
    virtual void set_orientation_base_rotation(
        const zima::kernel::Vec3& rotation, bool constrained) = 0;
    virtual void set_resolved_rotation(
        const zima::kernel::Vec3& rotation, bool valid = true) = 0;
    // Inline View dimensions edit the same pending widgets as the dialog.
    virtual bool set_inline_parameter_value(
        std::string_view key, double value) = 0;
};

}  // namespace zima::app
