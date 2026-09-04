#pragma once

#include "placement_reference_dialog.hpp"

#include <zima/document/part_document.hpp>
#include <zima/ui/container_placement_section.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <array>
#include <functional>
#include <memory>
#include <set>
#include <string_view>
#include <QString>

class QAction;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QToolButton;
class QTreeWidget;
class QWidget;

namespace zima::ui {
class ReferenceCellItem;
}  // namespace zima::ui

namespace zima::app {

class PrimitivePropertiesDialog final : public zima::ui::PropertiesSubWindow,
                                        public PlacementReferenceDialog {
public:
    using LegacyCommitCallback =
        std::function<void(zima::document::HistoryContainer)>;
    using CommitCallback = std::function<void(
        zima::document::HistoryContainer, std::vector<std::string>)>;
    using AssemblyTarget = std::pair<std::string, std::string>;
    using ReferenceRequestCallback = std::function<void(std::size_t)>;

    PrimitivePropertiesDialog(
        const zima::document::HistoryContainer& initial,
        bool edit_mode,
        bool allow_subtract,
        CommitCallback commit,
        QWidget* parent,
        std::vector<AssemblyTarget> assembly_targets = {},
        std::vector<std::string> selected_targets = {},
        bool assembly_cut_mode = false);
    PrimitivePropertiesDialog(
        const zima::document::HistoryContainer& initial,
        bool edit_mode,
        bool allow_subtract,
        LegacyCommitCallback commit,
        QWidget* parent);
    void set_extrusion_target(
        zima::kernel::FaceReference reference, zima::kernel::Vec3 origin,
        zima::kernel::Vec3 normal, std::string label = {});
    void set_extrusion_surface_target(
        zima::kernel::FaceReference reference,
        std::vector<zima::kernel::Vec3> triangles, std::string label = {});
    void set_extrusion_target_request(std::function<void()> callback);
    void set_extrusion_target_cancel(std::function<void()> callback);
    void finish_extrusion_target_entry();
    void set_profile_pick_request(std::function<void(bool)> callback);
    void set_edit_sketch_callback(
        std::function<void(zima::document::HistoryContainer)> callback);
    // The numeric fields may be unchanged after returning from an owned
    // Sketch, while the profile geometry itself is newly created/edited.
    // In that case OK must still invoke the explicit body calculation.
    void set_commit_required(bool required);
    void set_preview_callback(
        std::function<void(const zima::document::HistoryContainer&)> callback);
    [[nodiscard]] double profile_plane_offset() const;
    [[nodiscard]] double forward_extent_length() const;
    [[nodiscard]] double reverse_extent_length() const;
    [[nodiscard]] zima::document::ProfileExtentMode profile_extent_mode() const;
    [[nodiscard]] bool extrusion_direction_reversed() const;
    void set_profile_offset_and_forward_length(double offset, double length);
    void set_forward_extent_length(double length);
    void set_forward_extent_and_direction(double length, bool reversed);
    void set_reverse_extent_length(double length);
    void set_reverse_extent_and_direction(double length, bool reversed);
    void add_edge_reference(const zima::kernel::EdgeReference& edge);
    void set_edge_references(std::vector<zima::kernel::EdgeReference> edges);
    using EdgeGroup = std::vector<zima::kernel::EdgeReference>;
    void set_edge_groups(std::vector<EdgeGroup> groups);
    void set_edge_route_start_vertices(
        std::vector<zima::kernel::VertexReference> vertices);
    void set_edge_group_callbacks(
        std::function<void(std::size_t, std::optional<std::size_t>)> remove,
        std::function<void(std::size_t)> restore);
    void set_shell_faces(std::vector<zima::kernel::FaceReference> faces);
    void set_shell_face_callbacks(
        std::function<void(std::size_t)> remove,
        std::function<void()> request_selection);
    void set_shell_face_selection_active(bool active);

    // Universal container placement (position + FRONT/TOP orientation
    // references), reusing the same reference/DOF contract as
    // ConstructionPropertiesDialog: index < 3 selects a position reference
    // row, index >= 3 selects the FRONT (3) / TOP (4) orientation reference.
    void set_reference_request_callback(ReferenceRequestCallback callback);
    bool set_reference(std::size_t index,
        zima::document::ConstructionReference reference,
        const QString& label);
    [[nodiscard]] const std::string& container_id() const;
    [[nodiscard]] bool owns_reference_owner(const std::string& owner_id) const;
    [[nodiscard]] std::vector<zima::document::ConstructionReference>
        references_without(std::size_t index) const;
    [[nodiscard]] std::size_t first_empty_position_index() const;
    void set_active_reference_index(
        std::optional<std::size_t> index) override;
    void set_reference_inspected(
        std::size_t index, bool inspected) override;
    void clear_reference_highlights() override;
    void set_origin_selection_mode_callback(std::function<void(bool)> callback) override;
    void set_origin_selection_mode_active(bool active) override;
    void set_remaining_translation_dof(int dof);
    void set_remaining_rotation_dof(int dof);
    void set_rotation_constraint_state(
        const zima::document::OrientationConstraintState& state) override;
    void set_translation_constraint_state(
        const zima::document::PointConstraintState& state,
        const zima::kernel::Vec3& solution);
    void set_orientation_base_rotation(
        const zima::kernel::Vec3& rotation, bool constrained);
    void set_resolved_rotation(
        const zima::kernel::Vec3& rotation, bool valid = true) override;
    bool set_inline_parameter_value(
        std::string_view key, double value) override;

    // Toggle-highlight-on-click for populated reference rows, matching
    // ConstructionPropertiesDialog and Python's `_reference_cell_clicked`.
    using ReferenceHighlightsChangedCallback = std::function<void()>;
    void set_reference_highlights_changed_callback(
        ReferenceHighlightsChangedCallback callback);
    [[nodiscard]] std::set<std::string> highlighted_reference_owner_ids() const;
    [[nodiscard]] std::vector<zima::document::ConstructionReference>
        highlighted_reference_entries() const;

protected:
    bool submit() override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    zima::document::HistoryContainer initial_;
    bool edit_mode_{};
    bool commit_required_{};
    std::optional<zima::document::HistoryContainer> accepted_baseline_;
    std::vector<std::string> accepted_target_baseline_;
    CommitCallback commit_;
    QLineEdit* name_{};
    QComboBox* operation_{};
    QPushButton* add_operation_button_{};
    QPushButton* subtract_operation_button_{};
    QDoubleSpinBox* length_{};
    QDoubleSpinBox* width_{};
    QDoubleSpinBox* height_{};
    QDoubleSpinBox* radius_{};
    QDoubleSpinBox* top_radius_{};
    QDoubleSpinBox* top_offset_{};
    QComboBox* hole_type_{};
    QDoubleSpinBox* hole_diameter_{};
    QComboBox* hole_bore_end_{};
    QDoubleSpinBox* hole_bore_length_{};
    QDoubleSpinBox* hole_entrance_chamfer_{};
    QCheckBox* hole_drill_point_{};
    QDoubleSpinBox* hole_drill_angle_{};
    QCheckBox* hole_exit_chamfer_enabled_{};
    QDoubleSpinBox* hole_exit_chamfer_{};
    QCheckBox* hole_thread_enabled_{};
    QDoubleSpinBox* hole_thread_nominal_diameter_{};
    QDoubleSpinBox* hole_thread_pitch_{};
    QComboBox* hole_thread_end_{};
    QDoubleSpinBox* hole_thread_length_{};
    QCheckBox* hole_left_hand_{};
    QComboBox* extrusion_direction_{};
    QComboBox* extrusion_extent_{};
    QLabel* extrusion_target_{};
    QLineEdit* profile_source_{};
    QPushButton* profile_pick_button_{};
    QPushButton* profile_reset_button_{};
    QPushButton* own_sketch_button_{};
    QLineEdit* profile_status_{};
    QDoubleSpinBox* profile_plane_offset_{};
    QComboBox* result_type_{};
    QPushButton* result_type_switch_button_{};
    QDoubleSpinBox* thin_thickness_{};
    QComboBox* thin_mode_{};
    QPushButton* thin_mode_switch_button_{};
    QWidget* thin_thickness_row_{};
    QWidget* thin_mode_row_{};
    QComboBox* extent_mode_{};
    QPushButton* extent_switch_button_{};
    QPushButton* direction_flip_button_{};
    QDoubleSpinBox* forward_length_{};
    QDoubleSpinBox* reverse_length_{};
    QComboBox* forward_end_condition_{};
    QComboBox* reverse_end_condition_{};
    QLineEdit* forward_end_target_{};
    QLineEdit* reverse_end_target_{};
    QAction* forward_end_target_clear_action_{};
    QAction* reverse_end_target_clear_action_{};
    QToolButton* forward_end_targets_button_{};
    QToolButton* reverse_end_targets_button_{};
    QWidget* reverse_end_row_{};
    std::array<std::array<double, 2>, 3> revolution_extent_values_{{
        {{360.0, 360.0}}, {{45.0, 45.0}}, {{45.0, 45.0}}}};
    int revolution_previous_extent_index_{};
    std::string active_end_target_side_{"forward"};
    bool forward_end_target_highlighted_{};
    bool reverse_end_target_highlighted_{};
    bool forward_end_target_pick_active_{};
    bool reverse_end_target_pick_active_{};
    std::function<void()> extrusion_target_request_;
    std::function<void()> extrusion_target_cancel_;
    std::function<void(bool)> profile_pick_request_;
    std::function<void(zima::document::HistoryContainer)> edit_sketch_;
    std::function<void(const zima::document::HistoryContainer&)> preview_;
    QComboBox* treatment_type_{};
    QLabel* treatment_primary_label_{};
    QLabel* treatment_secondary_label_{};
    QLabel* treatment_angle_label_{};
    QDoubleSpinBox* treatment_primary_{};
    QDoubleSpinBox* treatment_secondary_{};
    QDoubleSpinBox* treatment_angle_{};
    QPushButton* treatment_flip_{};
    QPushButton* treatment_reverse_{};
    QTreeWidget* edge_list_{};
    QPushButton* remove_edge_button_{};
    QPushButton* restore_route_button_{};
    std::vector<EdgeGroup> edge_groups_;
    std::function<void(std::size_t, std::optional<std::size_t>)> remove_edge_;
    std::function<void(std::size_t)> restore_route_;
    QDoubleSpinBox* shell_thickness_{};
    QListWidget* shell_face_list_{};
    QPushButton* remove_shell_face_button_{};
    std::vector<zima::kernel::FaceReference> shell_faces_;
    std::function<void(std::size_t)> remove_shell_face_;
    std::function<void()> request_shell_face_selection_;
    std::array<QDoubleSpinBox*, 3> translation_{};
    std::array<QDoubleSpinBox*, 3> rotation_{};
    std::unique_ptr<zima::ui::ContainerPlacementSection> placement_;
    QLabel* reference_status_{};
    QLabel* dof_label_{};
    ReferenceRequestCallback reference_request_;
    int remaining_translation_dof_{3};
    int remaining_rotation_dof_{3};
    QLabel* error_{};
    QListWidget* assembly_targets_{};
    ReferenceHighlightsChangedCallback reference_highlights_changed_;
    [[nodiscard]] zima::document::HistoryContainer values() const;
    void request_extrusion_target(const std::string& side);
    void clear_extrusion_target(const std::string& side);
    void toggle_extrusion_target_highlight(const std::string& side);
    void refresh_extrusion_target_styles();
    void notify_preview();
    void refresh_edge_treatment_fields();
};

}  // namespace zima::app
