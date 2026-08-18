#pragma once

#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>

#include <QMainWindow>

#include <optional>
#include <array>
#include <vector>

class QAction;
class QDialog;
class QLabel;
class QKeyEvent;
class QTabBar;
class QTreeWidget;
class QTreeWidgetItem;

namespace zima::viewer { class MeshView; struct ViewerCandidate; }

namespace zima::app {

class AssemblyWorkspaceWindow final : public QMainWindow {
public:
    AssemblyWorkspaceWindow();

private:
    zima::workspace::Workspace workspace_;
    zima::kernel::OcctKernel kernel_;
    QTabBar* tabs_{};
    QTreeWidget* tree_{};
    zima::viewer::MeshView* viewer_{};
    QLabel* state_{};
    QAction* insert_action_{};
    QAction* regenerate_action_{};
    QAction* save_action_{};
    QAction* undo_action_{};
    QAction* redo_action_{};
    QAction* box_action_{};
    QAction* cylinder_action_{};
    QAction* extrusion_action_{};
    QAction* revolution_action_{};
    QAction* sketch_action_{};
    QAction* sketch_segment_action_{};
    QAction* sketch_rectangle_action_{};
    QAction* sketch_circle_action_{};
    QAction* sketch_arc_action_{};
    QAction* sketch_ellipse_action_{};
    QAction* sketch_bspline_action_{};
    QAction* sketch_horizontal_action_{};
    QAction* sketch_vertical_action_{};
    QAction* sketch_coincident_action_{};
    QAction* sketch_parallel_action_{};
    QAction* sketch_perpendicular_action_{};
    QAction* sketch_equal_length_action_{};
    QAction* sketch_dimension_action_{};
    QAction* sketch_dimension_x_action_{};
    QAction* sketch_dimension_y_action_{};
    QAction* sketch_angle_dimension_action_{};
    QAction* sketch_radius_dimension_action_{};
    QAction* sketch_diameter_dimension_action_{};
    QAction* sketch_ellipse_major_dimension_action_{};
    QAction* sketch_ellipse_minor_dimension_action_{};
    QAction* sketch_ellipse_rotation_dimension_action_{};
    QAction* sketch_fix_point_action_{};
    QAction* regenerate_part_action_{};
    QAction* plane_mate_action_{};
    QAction* axis_mate_action_{};
    QDialog* properties_dialog_{};
    struct PartRollbackContext {
        std::string part_document_id;
        std::string instance_path;
        std::size_t history_limit{};
    };
    std::optional<PartRollbackContext> part_rollback_;
    std::string active_occurrence_path_;
    std::optional<zima::assembly::MateReference> pending_mate_reference_;
    bool mate_selection_active_{};
    std::string active_sketch_id_;
    std::string selected_sketch_segment_id_;
    std::string selected_sketch_circle_id_;
    std::string selected_sketch_arc_id_;
    std::string selected_sketch_ellipse_id_;
    std::string selected_sketch_bspline_id_;
    std::string selected_sketch_point_id_;
    std::optional<std::array<double, 2>> pending_segment_start_;
    bool sketch_segment_active_{};
    bool sketch_rectangle_active_{};
    std::optional<std::array<double, 2>> pending_rectangle_corner_;
    bool sketch_circle_active_{};
    std::optional<std::array<double, 2>> pending_circle_center_;
    bool sketch_arc_active_{};
    std::optional<std::array<double, 2>> pending_arc_center_;
    std::optional<std::array<double, 2>> pending_arc_start_;
    bool sketch_ellipse_active_{};
    std::optional<std::array<double, 2>> pending_ellipse_center_;
    std::optional<std::array<double, 2>> pending_ellipse_major_;
    bool sketch_bspline_active_{};
    std::vector<std::array<double, 2>> pending_bspline_points_;
    bool sketch_coincident_active_{};
    std::string pending_coincident_point_id_;
    bool sketch_segment_pair_active_{};
    std::string pending_pair_segment_id_;
    zima::sketcher::ConstraintKind pending_pair_kind_{
        zima::sketcher::ConstraintKind::Parallel};
    bool preserve_view_on_refresh_{};
    std::optional<zima::document::PartDocument> sketch_drag_document_;
    std::string sketch_drag_point_id_;
    bool sketch_drag_changed_{};
    zima::assembly::MateKind pending_mate_kind_{
        zima::assembly::MateKind::PlaneCoincident};

    void create_layout();
    void create_actions();
    void new_assembly();
    void new_part();
    void open_part();
    void open_assembly();
    void insert_active_component();
    void regenerate_assembly();
    void start_plane_mate();
    void start_axis_mate();
    void accept_mate_reference(const zima::viewer::ViewerCandidate& candidate);
    [[nodiscard]] std::optional<zima::assembly::MateReference>
        local_mate_reference(const zima::viewer::ViewerCandidate& candidate) const;
    void save_active_assembly();
    void save_active_document();
    void show_primitive_properties(
        zima::document::FeatureKind feature_kind,
        const std::string& container_id = {});
    void show_sketch_properties(const std::string& sketch_id = {});
    void show_sketch_bspline_properties(
        const std::string& sketch_id, const std::string& bspline_id);
    void start_sketch_segment();
    void cancel_sketch_segment();
    void start_sketch_rectangle();
    void cancel_sketch_rectangle();
    void start_sketch_circle();
    void cancel_sketch_circle();
    void start_sketch_arc();
    void cancel_sketch_arc();
    void start_sketch_ellipse();
    void cancel_sketch_ellipse();
    void start_sketch_bspline();
    void cancel_sketch_bspline();
    bool finish_sketch_bspline();
    bool accept_sketch_segment_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void preview_sketch_segment_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    bool accept_sketch_rectangle_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void preview_sketch_rectangle_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    bool accept_sketch_circle_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void preview_sketch_circle_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    bool accept_sketch_arc_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void preview_sketch_arc_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    bool accept_sketch_ellipse_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void preview_sketch_ellipse_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    bool accept_sketch_bspline_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void preview_sketch_bspline_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void constrain_selected_segment(zima::sketcher::ConstraintKind kind);
    void start_sketch_coincident();
    void cancel_sketch_coincident();
    void accept_sketch_coincident_point(
        const zima::viewer::ViewerCandidate& candidate);
    void start_sketch_segment_pair(zima::sketcher::ConstraintKind kind);
    void accept_sketch_segment_pair(
        const zima::viewer::ViewerCandidate& candidate);
    void toggle_selected_sketch_point_fixed();
    [[nodiscard]] bool begin_sketch_point_drag(
        const zima::viewer::ViewerCandidate& candidate);
    void update_sketch_point_drag(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void end_sketch_point_drag();
    [[nodiscard]] bool delete_selected_sketch_geometry();
    void show_sketch_dimension_properties(
        const std::string& sketch_id, const std::string& dimension_id = {},
        zima::sketcher::DimensionKind creation_kind =
            zima::sketcher::DimensionKind::Distance);
    void regenerate_active_part();
    void undo();
    void redo();
    [[nodiscard]] std::vector<zima::kernel::BodyResult> calculate_part(
        const zima::document::PartDocument& document) const;
    void refresh_tabs();
    void refresh_scene();
    void add_assembly_tree_children(
        QTreeWidgetItem* parent,
        const std::string& assembly_document_id,
        const zima::assembly::InstancePath& parent_path,
        bool ancestor_suppressed = false);
    void add_snapshot_tree_children(
        QTreeWidgetItem* parent,
        const std::vector<zima::assembly::OccurrenceSnapshot>& snapshots,
        const std::string& owner_assembly_document_id,
        const zima::assembly::InstancePath& parent_path,
        bool ancestor_suppressed = false);
    void add_mate_tree_children(
        QTreeWidgetItem* parent,
        const std::string& owner_assembly_document_id);
    void select_occurrence(const std::string& instance_path);
    void select_container(const std::string& container_id);
    void show_component_properties(const std::string& instance_path);
    void show_component_context_menu(
        const std::string& instance_path, const QPoint& global_position);
    void show_mate_properties(
        const std::string& assembly_document_id, const std::string& mate_id);
    void show_mate_context_menu(
        const std::string& assembly_document_id,
        const std::string& mate_id,
        const QPoint& global_position);
    [[nodiscard]] std::optional<std::string> resolve_active_occurrence(
        const std::string& part_document_id) const;

protected:
    void keyPressEvent(QKeyEvent* event) override;
};

}  // namespace zima::app
