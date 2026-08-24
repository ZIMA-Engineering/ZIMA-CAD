#pragma once

#include <zima/document/part_document.hpp>
#include <zima/ui/container_placement_section.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <array>
#include <functional>
#include <memory>
#include <set>
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
        CommitCallback commit, QWidget* parent);
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
    void set_remaining_translation_dof(int dof);
    void set_remaining_rotation_dof(int dof);
    void set_translation_constraint_state(
        const zima::document::PointConstraintState& state,
        const zima::kernel::Vec3& solution);
    // Refreshes the "Absolutní" rotation column from the base rotation
    // implied by orientation-driving references (front/top), disabling it
    // while such references are present; matches Python's
    // `_sync_rotation_absolute_fields`. When `has_orientation_references` is
    // false, the absolute column stays editable (there is no base to show).
    void set_orientation_base_rotation(
        const zima::kernel::Vec3& base_rotation, bool has_orientation_references);

    // Owner ids of the references (from both the placement- and
    // orientation-reference tables) currently toggled cyan/azure in the
    // viewer, matching Python's `highlighted_reference_keys` /
    // `_orientation_highlighted_reference_keys`. Recomputed after every
    // toggle; the caller wires this into MeshView's highlight setters.
    using ReferenceHighlightsChangedCallback = std::function<void()>;
    void set_reference_highlights_changed_callback(
        ReferenceHighlightsChangedCallback callback);
    [[nodiscard]] std::set<std::string> highlighted_reference_owner_ids() const;

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
    [[nodiscard]] zima::document::ConstructionObject current_value() const;
    void notify_preview();
};

}  // namespace zima::app
