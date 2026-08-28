#pragma once

#include <zima/document/part_document.hpp>
#include <zima/ui/container_placement_section.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <array>
#include <functional>
#include <memory>
#include <set>
#include <string_view>
#include <QString>

class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace zima::ui {
class ReferenceCellItem;
}  // namespace zima::ui

namespace zima::app {

class ConstructionPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using CommitCallback = std::function<void(zima::document::ConstructionObject)>;

    ConstructionPropertiesDialog(
        const zima::document::ConstructionObject& initial, bool edit_mode,
        CommitCallback commit, QWidget* parent, int decimal_places = 3);
    using ReferenceRequestCallback = std::function<void(std::size_t)>;
    using PreviewCallback = std::function<void(zima::document::ConstructionObject)>;
    void set_reference_request_callback(ReferenceRequestCallback callback);
    void set_preview_callback(PreviewCallback callback);
    bool set_reference(std::size_t index,
        zima::document::ConstructionReference reference,
        const QString& label,
        zima::document::ConstructionDefinition definition);
    bool set_reference(std::size_t index,
        zima::document::ConstructionReference reference,
        const QString& label);
    [[nodiscard]] zima::document::ConstructionDefinition current_definition() const;
    [[nodiscard]] zima::document::ConstructionKind construction_kind() const;
    [[nodiscard]] const std::string& construction_id() const;
    [[nodiscard]] bool owns_reference_owner(const std::string& owner_id) const;
    [[nodiscard]] std::vector<zima::document::ConstructionReference>
        references_without(std::size_t index) const;
    // Position row index (0..2) of the first still-empty placement
    // reference row, or 3 if all three are already filled. Used by the
    // "Počátek" bulk-fill so it always targets the real first empty row(s)
    // instead of assuming the currently-armed index starts at 0 -- a 2nd
    // bulk-fill attempt (e.g. after deleting and re-entering a reference)
    // may arm a row other than 0.
    [[nodiscard]] std::size_t first_empty_position_index() const;
    // Populated position references only (empty "hole" rows left by a
    // deleted reference are skipped, in row order). Use this instead of
    // relying on the raw reference count whenever the true entered-reference
    // list is needed (e.g. persisting/counting), since rows 0-2 are fixed,
    // pre-existing slots that no longer shrink/shift when one is deleted.
    [[nodiscard]] std::vector<zima::document::ConstructionReference>
        populated_references() const;
    void set_remaining_translation_dof(int dof);
    void set_remaining_rotation_dof(int dof);
    void set_translation_constraint_state(
        const zima::document::PointConstraintState& state,
        const zima::kernel::Vec3& solution);
    // Applies an inline View-dimension edit to the same live widget that
    // owns the pending Point/Axis/Plane value. This deliberately does not
    // commit the document; OK/Cancel remain the transaction boundary.
    bool set_inline_parameter_value(std::string_view key, double value);
    // Refreshes the "Absolutní" rotation column from the base rotation
    // implied by orientation-driving references (front/top), disabling it
    // while such references are present; matches Python's
    // `_sync_rotation_absolute_fields`. When `has_orientation_references` is
    // false, the absolute column stays editable (there is no base to show).
    void set_orientation_base_rotation(
        const zima::kernel::Vec3& base_rotation, bool has_orientation_references);
    // Reflects the Plane container's auto-inherited orientation (see
    // resolve_construction()'s `orientation_inherited_from_reference`) in
    // the FRONT/TOP table: locks it and labels it with the position
    // reference the orientation came from, instead of leaving it looking
    // like an empty, pickable pair of rows.
    void set_orientation_inherited_from_reference(bool inherited);

    // Owner ids of the references (from both the placement- and
    // orientation-reference tables) currently toggled cyan/azure in the
    // viewer, matching Python's `highlighted_reference_keys` /
    // `_orientation_highlighted_reference_keys`. Recomputed after every
    // toggle; the caller wires this into MeshView's highlight setters.
    using ReferenceHighlightsChangedCallback = std::function<void()>;
    void set_reference_highlights_changed_callback(
        ReferenceHighlightsChangedCallback callback);
    [[nodiscard]] std::set<std::string> highlighted_reference_owner_ids() const;
    [[nodiscard]] std::vector<zima::document::ConstructionReference>
        highlighted_reference_entries() const;

protected:
    bool submit() override;

private:
    zima::document::ConstructionObject initial_;
    CommitCallback commit_;
    QLineEdit* name_{};
    std::array<QDoubleSpinBox*, 3> origin_{};
    // "Absolutní" rotation column: shows the base rotation implied by
    // orientation-driving references (read-only, disabled) when such
    // references exist; otherwise it is the plain editable rotation.
    std::array<QDoubleSpinBox*, 3> rotation_{};
    // "Korekce" rotation column: always-editable manual correction applied
    // on top of the base rotation (zero when there is no base rotation),
    // matching Python's rotation_offset_x/y/z fields.
    std::array<QDoubleSpinBox*, 3> rotation_offset_{};
    QComboBox* direction_combo_{};
    QComboBox* base_plane_combo_{};
    QDoubleSpinBox* display_size_{};
    QDoubleSpinBox* offset_{};
    QComboBox* definition_{};
    std::unique_ptr<zima::ui::ContainerPlacementSection> placement_;
    QLabel* reference_status_{};
    QLabel* dof_label_{};
    ReferenceRequestCallback reference_request_;
    PreviewCallback preview_;
    QLabel* error_{};
    int remaining_translation_dof_{3};
    int remaining_rotation_dof_{3};
    bool has_orientation_base_rotation_{false};
    bool updating_rotation_fields_{false};
    ReferenceHighlightsChangedCallback reference_highlights_changed_;
    void refresh_definition_fields();
    void refresh_offset_enabled_state();
    [[nodiscard]] zima::document::ConstructionObject current_value() const;
    void notify_preview();
};

}  // namespace zima::app
