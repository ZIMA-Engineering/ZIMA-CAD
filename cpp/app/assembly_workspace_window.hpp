#pragma once

#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/sketcher/sketch_trim.hpp>
#include <zima/viewer/picking.hpp>
#include "application_settings.hpp"

#include <QMainWindow>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <array>
#include <utility>
#include <vector>

class QAction;
class QActionGroup;
class QComboBox;
class QDialog;
class QLabel;
class QKeyEvent;
class QMenu;
class QString;
class QTabBar;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;
class QStackedWidget;
class QSplitter;

namespace zima::viewer { class MeshView; struct ViewerCandidate; }

namespace zima::app {

class PrimitivePropertiesDialog;
class ConstructionPropertiesDialog;
class DrawingWindow;
class SketchTextPropertiesDialog;

class AssemblyWorkspaceWindow final : public QMainWindow {
public:
    AssemblyWorkspaceWindow();
    [[nodiscard]] bool open_document_path(const QString& path);
    void show_tree_item_properties(QTreeWidgetItem* item);

private:
    enum class ApplicationMode {
        Modeling,
        Assembly,
        SheetMetal,
        Surface,
        Piping,
        Drawing,
    };

    zima::workspace::Workspace workspace_;
    zima::kernel::OcctKernel kernel_;
    ApplicationSettings application_settings_;
    std::filesystem::path working_directory_{std::filesystem::current_path()};
    QTabBar* tabs_{};
    QTreeWidget* tree_{};
    zima::viewer::MeshView* viewer_{};
    QStackedWidget* workspace_stack_{};
    QSplitter* document_splitter_{};
    QWidget* model_workspace_{};
    DrawingWindow* drawing_workspace_{};
    QLabel* state_{};
    QToolBar* main_toolbar_{};
    QToolBar* view_toolbar_{};
    QToolBar* tools_toolbar_{};
    QComboBox* standard_view_combo_{};
    QComboBox* selection_filter_combo_{};
    QAction* new_document_action_{};
    QAction* open_document_action_{};
    QAction* close_document_action_{};
    QAction* insert_action_{};
    QMenu* insert_menu_{};
    QAction* regenerate_document_action_{};
    QAction* regenerate_action_{};
    QAction* save_action_{};
    QAction* save_as_action_{};
    QAction* rename_document_action_{};
    QMenu* delete_file_menu_{};
    QAction* delete_current_file_action_{};
    QAction* delete_all_versions_action_{};
    QAction* delete_old_versions_action_{};
    QAction* delete_old_versions_keep_latest_action_{};
    QMenu* delete_working_directory_menu_{};
    QAction* delete_working_directory_old_versions_action_{};
    QAction* delete_working_directory_keep_latest_action_{};
    QAction* working_directory_action_{};
    QAction* undo_action_{};
    QAction* redo_action_{};
    QAction* fit_view_action_{};
    QAction* selection_action_{};
    QAction* wire_action_{};
    QAction* hidden_edges_action_{};
    QAction* no_hidden_edges_action_{};
    QAction* shaded_edges_action_{};
    QAction* shaded_action_{};
    QAction* show_origins_action_{};
    QAction* show_points_action_{};
    QAction* show_axes_action_{};
    QAction* show_planes_action_{};
    QAction* show_sketches_action_{};
    QMenu* colors_menu_{};
    QActionGroup* display_mode_group_{};
    QActionGroup* application_group_{};
    std::array<QAction*, 6> application_actions_{};
    ApplicationMode active_application_{ApplicationMode::Modeling};
    QAction* box_action_{};
    QAction* cylinder_action_{};
    QAction* sphere_action_{};
    QAction* cone_action_{};
    QAction* pyramid_action_{};
    QAction* wedge_action_{};
    QAction* construction_point_action_{};
    QAction* construction_axis_action_{};
    QAction* construction_plane_action_{};
    QAction* extrusion_action_{};
    QAction* revolution_action_{};
    QAction* fillet_action_{};
    QAction* chamfer_action_{};
    QAction* sketch_action_{};
    QAction* sketch_normal_view_action_{};
    QAction* sketch_external_reference_action_{};
    QAction* sketch_point_action_{};
    QAction* sketch_construction_action_{};
    QAction* sketch_segment_action_{};
    QAction* sketch_polyline_action_{};
    QAction* sketch_rectangle_action_{};
    QAction* sketch_polygon_action_{};
    QMenu* sketch_polygon_menu_{};
    QAction* sketch_trim_action_{};
    QAction* sketch_mirror_action_{};
    QAction* sketch_circle_action_{};
    QAction* sketch_arc_action_{};
    QAction* sketch_ellipse_action_{};
    QAction* sketch_elliptical_arc_action_{};
    QAction* sketch_bspline_action_{};
    QAction* sketch_text_action_{};
    QAction* sketch_constraints_action_{};
    QMenu* sketch_constraints_menu_{};
    QAction* sketch_horizontal_action_{};
    QAction* sketch_vertical_action_{};
    QAction* sketch_coincident_action_{};
    QAction* sketch_midpoint_action_{};
    QAction* sketch_symmetric_action_{};
    QAction* sketch_concentric_action_{};
    QAction* sketch_tangent_action_{};
    QAction* sketch_parallel_action_{};
    QAction* sketch_perpendicular_action_{};
    QAction* sketch_equal_length_action_{};
    QAction* sketch_dimensions_action_{};
    QMenu* sketch_dimensions_menu_{};
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
    QAction* finish_sketch_action_{};
    QAction* regenerate_part_action_{};
    QAction* plane_mate_action_{};
    QAction* axis_mate_action_{};
    QAction* point_mate_action_{};
    QAction* angle_mate_action_{};
    QAction* plane_angle_mate_action_{};
    QAction* parameters_action_{};
    QAction* material_action_{};
    QAction* relations_action_{};
    QAction* family_table_action_{};
    QAction* file_settings_action_{};
    QAction* export_action_{};
    QAction* settings_action_{};
    QMenu* standard_views_menu_{};
    QDialog* properties_dialog_{};
    QDialog* global_settings_dialog_{};
    struct PartRollbackContext {
        std::string part_document_id;
        std::string instance_path;
        std::size_t history_limit{};
        std::optional<zima::kernel::BodyResult> input_body;
    };
    std::optional<PartRollbackContext> part_rollback_;
    struct AssemblyCutRollbackContext {
        std::string assembly_document_id;
        std::string cut_id;
        std::size_t cut_index{};
        std::map<std::string, zima::kernel::BodyResult> input_component_bodies;
    };
    std::optional<AssemblyCutRollbackContext> assembly_cut_rollback_;
    std::string active_occurrence_path_;
    std::optional<zima::assembly::MateReference> pending_mate_reference_;
    bool mate_selection_active_{};
    std::optional<zima::document::FeatureKind> edge_treatment_selection_;
    std::vector<zima::kernel::EdgeReference> pending_edge_treatment_edges_;
    PrimitivePropertiesDialog* extrusion_target_dialog_{};
    ConstructionPropertiesDialog* construction_reference_dialog_{};
    std::optional<zima::kernel::ViewerMesh> construction_preview_mesh_;
    std::string construction_dimension_object_id_;
    std::optional<std::size_t> pending_construction_reference_index_;
    int construction_translation_dof_{3};
    std::string active_sketch_id_;
    std::string selected_sketch_id_;
    std::string selected_sketch_segment_id_;
    std::string selected_sketch_circle_id_;
    std::string selected_sketch_arc_id_;
    std::string selected_sketch_ellipse_id_;
    std::string selected_sketch_elliptical_arc_id_;
    std::string selected_sketch_bspline_id_;
    std::string selected_sketch_text_id_;
    std::string selected_sketch_external_reference_id_;
    std::string selected_sketch_point_id_;
    bool sketch_point_active_{};
    std::optional<std::array<double, 2>> pending_segment_start_;
    bool sketch_segment_active_{};
    bool sketch_segment_construction_{};
    bool sketch_polyline_active_{};
    bool sketch_rectangle_active_{};
    std::optional<std::array<double, 2>> pending_rectangle_corner_;
    bool sketch_polygon_active_{};
    unsigned sketch_polygon_sides_{6};
    std::optional<std::array<double, 2>> pending_polygon_center_;
    bool sketch_trim_active_{};
    bool sketch_trim_changed_{};
    std::optional<zima::sketcher::Sketch> sketch_trim_preview_;
    std::vector<zima::sketcher::SketchTrimPiece> sketch_trim_topology_;
    std::vector<std::array<double, 2>> sketch_trim_path_;
    std::optional<std::size_t> sketch_trim_pressed_piece_;
    bool sketch_mirror_active_{};
    bool sketch_mirror_selecting_sources_{};
    std::vector<std::string> pending_mirror_geometry_ids_;
    std::string pending_mirror_axis_id_;
    bool sketch_circle_active_{};
    std::optional<std::array<double, 2>> pending_circle_center_;
    bool sketch_arc_active_{};
    std::optional<std::array<double, 2>> pending_arc_center_;
    std::optional<std::array<double, 2>> pending_arc_start_;
    bool sketch_ellipse_active_{};
    std::optional<std::array<double, 2>> pending_ellipse_center_;
    std::optional<std::array<double, 2>> pending_ellipse_major_;
    bool sketch_elliptical_arc_active_{};
    std::optional<std::array<double, 2>> pending_elliptical_arc_center_;
    std::optional<std::array<double, 2>> pending_elliptical_arc_major_;
    std::optional<std::array<double, 2>> pending_elliptical_arc_minor_;
    std::optional<std::array<double, 2>> pending_elliptical_arc_start_;
    bool pending_elliptical_arc_reversed_{};
    bool sketch_bspline_active_{};
    std::vector<std::array<double, 2>> pending_bspline_points_;
    bool sketch_text_active_{};
    std::string editing_sketch_text_id_;
    SketchTextPropertiesDialog* sketch_text_dialog_{};
    bool sketch_external_reference_active_{};
    bool sketch_coincident_active_{};
    std::string pending_coincident_point_id_;
    bool sketch_midpoint_active_{};
    std::string pending_midpoint_point_id_;
    bool sketch_symmetric_active_{};
    std::vector<std::string> pending_symmetric_point_ids_;
    bool sketch_concentric_active_{};
    std::string pending_concentric_geometry_id_;
    bool sketch_tangent_active_{};
    std::string pending_tangent_geometry_id_;
    bool pending_tangent_reference_is_segment_{};
    bool pending_tangent_reference_supports_curve_pair_{};
    bool sketch_segment_pair_active_{};
    std::string pending_pair_geometry_id_;
    bool pending_pair_reference_is_circular_{};
    zima::sketcher::ConstraintKind pending_pair_kind_{
        zima::sketcher::ConstraintKind::Parallel};
    bool sketch_point_dimension_active_{};
    std::string pending_point_dimension_first_id_;
    zima::sketcher::DimensionKind pending_point_dimension_kind_{
        zima::sketcher::DimensionKind::Distance};
    bool preserve_view_on_refresh_{};
    std::map<std::string, std::array<float, 7>> document_camera_states_;
    std::optional<zima::document::PartDocument> sketch_drag_document_;
    std::optional<zima::assembly::AssemblyDocument> assembly_sketch_drag_document_;
    std::string sketch_drag_point_id_;
    bool sketch_drag_changed_{};
    std::optional<zima::assembly::AssemblyDocument> assembly_drag_document_;
    std::string assembly_drag_document_id_;
    std::string assembly_drag_mate_id_;
    zima::kernel::Vec3 assembly_drag_axis_point_;
    zima::kernel::Vec3 assembly_drag_axis_direction_;
    bool assembly_drag_angular_{};
    bool assembly_drag_changed_{};
    zima::assembly::MateKind pending_mate_kind_{
        zima::assembly::MateKind::PlaneCoincident};

    void create_layout();
    void create_actions();
    void new_document();
    [[nodiscard]] QString create_document(
        const QString& document_type, const QString& file_stem);
    void new_assembly();
    void new_part();
    void new_drawing();
    void close_document(int tab_index = -1);
    void open_document();
    void rebuild_insert_menu();
    [[nodiscard]] bool has_insertable_component() const;
    void insert_component(const std::string& source_document_id);
    void rebuild_application_toolbar();
    void update_application_actions();
    void set_active_application(ApplicationMode mode);
    void update_document_area_visibility();
    void regenerate_active_document();
    [[nodiscard]] const zima::sketcher::Sketch* active_sketch() const;
    bool mutate_active_sketch(
        const std::function<void(zima::sketcher::Sketch&)>& mutation);
    void edit_document_parameters();
    void edit_material();
    void edit_relations();
    void edit_family_table();
    void edit_file_settings();
    void regenerate_assembly();
    void start_plane_mate();
    void start_axis_mate();
    void start_point_mate();
    void start_angle_mate();
    void start_plane_angle_mate();
    void set_mate_candidate_filter();
    void start_edge_treatment(zima::document::FeatureKind kind);
    void accept_edge_treatment(const zima::viewer::ViewerCandidate& candidate);
    [[nodiscard]] bool finish_edge_treatment_selection();
    void accept_extrusion_target(const zima::viewer::ViewerCandidate& candidate);
    void accept_mate_reference(const zima::viewer::ViewerCandidate& candidate);
    [[nodiscard]] std::optional<zima::assembly::MateReference>
        local_mate_reference(const zima::viewer::ViewerCandidate& candidate) const;
    void save_active_assembly();
    void save_active_document();
    void save_active_document_as();
    void set_working_directory();
    void open_new_window();
    void show_global_settings();
    void show_about();
    void import_file();
    void import_step_into_assembly(const std::filesystem::path& path);
    void export_file();
    void show_primitive_properties(
        zima::document::FeatureKind feature_kind,
        const std::string& container_id = {});
    void show_construction_properties(
        zima::document::ConstructionKind kind, const std::string& object_id = {});
    void start_construction_reference_selection(std::size_t index);
    void accept_construction_reference(
        const zima::viewer::ViewerCandidate& candidate);
    [[nodiscard]] bool accept_construction_tree_reference(
        const QTreeWidgetItem* item);
    void show_sketch_properties(const std::string& sketch_id = {});
    void show_sketch_bspline_properties(
        const std::string& sketch_id, const std::string& bspline_id);
    void show_sketch_text_properties(
        const std::string& sketch_id, const std::string& text_id = {});
    void set_sketch_external_reference_mode(bool enabled);
    void accept_sketch_external_reference(
        const zima::viewer::ViewerCandidate& candidate);
    void align_active_sketch_view();
    void finish_active_sketch();
    void start_sketch_point();
    void start_sketch_segment(bool construction = false);
    void start_sketch_polyline();
    void cancel_sketch_segment();
    void start_sketch_rectangle();
    void cancel_sketch_rectangle();
    void start_sketch_polygon(unsigned sides);
    void cancel_sketch_polygon();
    void start_sketch_trim();
    void cancel_sketch_trim();
    [[nodiscard]] bool begin_sketch_trim_gesture(
        const std::optional<zima::viewer::ViewerCandidate>& candidate,
        const zima::kernel::Vec3& origin,
        const zima::kernel::Vec3& direction);
    void update_sketch_trim_gesture(
        const zima::kernel::Vec3& origin,
        const zima::kernel::Vec3& direction);
    void end_sketch_trim_gesture();
    [[nodiscard]] bool finish_sketch_trim();
    void start_sketch_mirror();
    void cancel_sketch_mirror();
    void accept_sketch_mirror_source(
        const zima::viewer::ViewerCandidate& candidate);
    void accept_sketch_mirror_axis(
        const zima::viewer::ViewerCandidate& candidate);
    [[nodiscard]] bool finish_sketch_mirror();
    void start_sketch_circle();
    void cancel_sketch_circle();
    void start_sketch_arc();
    void cancel_sketch_arc();
    void start_sketch_ellipse();
    void cancel_sketch_ellipse();
    void start_sketch_elliptical_arc();
    void cancel_sketch_elliptical_arc();
    void start_sketch_bspline();
    void cancel_sketch_bspline();
    bool finish_sketch_bspline();
    bool finish_sketch_polyline();
    bool accept_sketch_point_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    bool accept_sketch_external_snap(
        const zima::viewer::ViewerCandidate& candidate);
    [[nodiscard]] std::optional<std::pair<zima::kernel::Vec3,
        zima::kernel::Vec3>> sketch_external_snap_ray(
            const zima::viewer::ViewerCandidate& candidate) const;
    bool accept_sketch_segment_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void preview_sketch_segment_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    bool accept_sketch_rectangle_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void preview_sketch_rectangle_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    bool accept_sketch_polygon_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void preview_sketch_polygon_ray(
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
    bool accept_sketch_elliptical_arc_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void preview_sketch_elliptical_arc_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    bool accept_sketch_bspline_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void preview_sketch_bspline_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    bool accept_sketch_text_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void constrain_selected_segment(zima::sketcher::ConstraintKind kind);
    void start_sketch_coincident();
    void cancel_sketch_coincident();
    void accept_sketch_coincident_point(
        const zima::viewer::ViewerCandidate& candidate);
    void start_sketch_midpoint();
    void cancel_sketch_midpoint();
    void accept_sketch_midpoint_selection(
        const zima::viewer::ViewerCandidate& candidate);
    void start_sketch_symmetric();
    void set_sketch_symmetric_axis_contract();
    void accept_sketch_symmetric_selection(
        const zima::viewer::ViewerCandidate& candidate);
    void start_sketch_concentric();
    void set_sketch_concentric_contract();
    void accept_sketch_concentric_selection(
        const zima::viewer::ViewerCandidate& candidate);
    void start_sketch_tangent();
    void set_sketch_tangent_contract();
    void accept_sketch_tangent_selection(
        const zima::viewer::ViewerCandidate& candidate);
    void start_sketch_segment_pair(zima::sketcher::ConstraintKind kind);
    void set_sketch_pair_contract();
    void accept_sketch_segment_pair(
        const zima::viewer::ViewerCandidate& candidate);
    void start_sketch_point_dimension(zima::sketcher::DimensionKind kind);
    void accept_sketch_point_dimension(
        const zima::viewer::ViewerCandidate& candidate);
    void toggle_selected_sketch_point_fixed();
    [[nodiscard]] bool begin_sketch_point_drag(
        const zima::viewer::ViewerCandidate& candidate);
    void update_sketch_point_drag(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void end_sketch_point_drag();
    [[nodiscard]] bool begin_assembly_mate_drag(
        const zima::viewer::ViewerCandidate& candidate);
    void update_assembly_mate_drag(
        const zima::kernel::Vec3& ray_origin,
        const zima::kernel::Vec3& ray_direction);
    void end_assembly_mate_drag();
    [[nodiscard]] bool delete_selected_sketch_geometry();
    void remove_sketch_relation(
        const std::string& sketch_id, const std::string& relation_id,
        bool dimension);
    void toggle_part_container_suppressed(const std::string& container_id);
    void move_part_container(const std::string& container_id, int direction);
    void delete_part_object(const std::string& object_id, const QString& kind);
    void show_sketch_dimension_properties(
        const std::string& sketch_id, const std::string& dimension_id = {},
        zima::sketcher::DimensionKind creation_kind =
            zima::sketcher::DimensionKind::Distance,
        const std::string& first_point_id = {},
        const std::string& second_point_id = {});
    void regenerate_active_part();
    void undo();
    void redo();
    [[nodiscard]] std::vector<zima::kernel::BodyResult> calculate_part(
        const zima::document::PartDocument& document) const;
    void calculate_assembly_cuts(
        zima::assembly::AssemblyDocument& document) const;
    void refresh_tabs();
    void refresh_scene();
    void add_part_tree_children(
        QTreeWidgetItem* parent,
        const zima::document::PartDocument& document);
    void populate_sketch_tree(const zima::sketcher::Sketch& sketch);
    [[nodiscard]] std::pair<zima::kernel::Vec3, zima::kernel::Vec3>
    active_part_local_ray(
        const zima::kernel::Vec3& origin,
        const zima::kernel::Vec3& direction) const;
    [[nodiscard]] std::pair<zima::kernel::Vec3, zima::kernel::Vec3>
    active_assembly_local_ray(
        const zima::kernel::Vec3& origin,
        const zima::kernel::Vec3& direction) const;
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
