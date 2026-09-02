#pragma once

#include "placement_reference_dialog.hpp"

#include <zima/sketcher/sketch.hpp>
#include <zima/document/part_document.hpp>
#include <zima/ui/container_placement_section.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <utility>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace zima::app {

class SketchPropertiesDialog final : public zima::ui::PropertiesSubWindow,
                                     public PlacementReferenceDialog {
public:
    using CommitCallback = std::function<void(
        zima::sketcher::Sketch, zima::document::Placement, bool)>;

    // Identifies one candidate Plane construction container the Sketch may
    // be placed on instead of one of the three fixed XY/XZ/YZ planes --
    // `owner_id` is the container's entity_id (matches
    // Sketch::plane_reference_owner_id / ConstructionReference::owner_id
    // convention), `label` its display name.
    struct PlaneOption {
        std::string owner_id;
        QString label;
    };

    SketchPropertiesDialog(
        zima::sketcher::Sketch initial,
        zima::document::Placement initial_placement, bool edit_mode,
        std::vector<PlaneOption> plane_options,
        CommitCallback commit, QWidget* parent);
    using ReferenceRequestCallback = std::function<void(std::size_t)>;
    using HighlightsChangedCallback = std::function<void()>;
    using PreviewCallback = std::function<void(
        const zima::sketcher::Sketch&, const zima::document::Placement&)>;
    void set_reference_request_callback(ReferenceRequestCallback callback);
    void set_reference_highlights_changed_callback(
        HighlightsChangedCallback callback);
    void set_reference_geometry(
        zima::kernel::ViewerReferenceGeometry geometry);
    void set_preview_callback(PreviewCallback callback);
    [[nodiscard]] std::vector<zima::document::ConstructionReference>
        highlighted_reference_entries() const;
    [[nodiscard]] std::vector<zima::document::ConstructionReference>
        references_without(std::size_t index) const override;
    [[nodiscard]] bool owns_reference_owner(
        const std::string& owner_id) const override;
    bool set_reference(std::size_t index,
        zima::document::ConstructionReference reference,
        const QString& label) override;
    [[nodiscard]] std::size_t first_empty_position_index() const override;
    void set_active_reference_index(
        std::optional<std::size_t> index) override;
    void set_reference_inspected(
        std::size_t index, bool inspected) override;
    void clear_reference_highlights() override;
    void set_translation_constraint_state(
        const zima::document::PointConstraintState& state,
        const zima::kernel::Vec3& solution) override;
    void set_remaining_rotation_dof(int dof) override;
    void set_orientation_base_rotation(
        const zima::kernel::Vec3& rotation, bool constrained) override;
    bool set_inline_parameter_value(
        std::string_view key, double value) override;

protected:
    bool submit() override;

private:
    zima::sketcher::Sketch initial_;
    zima::document::Placement initial_placement_;
    std::vector<PlaneOption> plane_options_;
    CommitCallback commit_;
    QLineEdit* name_{};
    QComboBox* plane_{};
    QDoubleSpinBox* offset_{};
    QComboBox* plane_reference_{};
    QPushButton* sketch_button_{};
    QLabel* error_{};
    std::unique_ptr<zima::ui::ContainerPlacementSection> placement_;
    zima::kernel::ViewerReferenceGeometry reference_geometry_;
    PreviewCallback preview_;
    bool enter_sketch_after_commit_{};

    void update_plane_fields_enabled();
    void refresh_resolved_placement();
    void notify_preview();
    [[nodiscard]] std::pair<zima::sketcher::Sketch,
        zima::document::Placement> current_values() const;
};

}  // namespace zima::app
