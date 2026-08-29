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
class QColor;
class QDialog;
class QLabel;
class QKeyEvent;
class QLineEdit;
class QMenu;
class QString;
class QTabBar;
class QToolBar;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QStackedWidget;
class QSplitter;

namespace zima::viewer { class MeshView; struct ViewerCandidate; }

namespace zima::app {

class PrimitivePropertiesDialog;
class PlacementReferenceDialog;
class ConstructionPropertiesDialog;
class ComponentPropertiesDialog;
class OrientationDialog;
class DrawingWindow;
class SketchTextPropertiesDialog;

class AssemblyWorkspaceWindow final : public QMainWindow {
public:
    explicit AssemblyWorkspaceWindow(
        const QString& working_directory = {});
    ~AssemblyWorkspaceWindow() override;
    [[nodiscard]] bool open_document_path(const QString& path);
    void show_tree_item_properties(QTreeWidgetItem* item);
    void focus_parameter_dimension_field(const std::string& semantic_key);
    void edit_dimension_inline(const zima::viewer::ViewerCandidate& candidate);
    // Exposed for regression coverage of nested Assembly occurrence
    // activation through the real window (no context-menu interaction).
    [[nodiscard]] bool activate_occurrence_for_test(const std::string& instance_path);
    void deactivate_active_occurrence_for_test();
    [[nodiscard]] const std::string& active_occurrence_path_for_test() const {
        return active_occurrence_path_;
    }

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
    QToolButton* document_kind_button_{};
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
    QLineEdit* inline_dimension_edit_{};
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
    std::array<QAction*, 8> body_color_preset_actions_{};
    QAction* custom_body_color_action_{};
    QAction* reset_body_color_action_{};
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
    QAction* sketch_flip_view_action_{};
    QAction* sketch_rotate_view_action_{};
    QAction* sketch_external_reference_action_{};
    QAction* sketch_external_profile_action_{};
    QAction* sketch_point_action_{};
    QAction* sketch_construction_action_{};
    QAction* sketch_segment_action_{};
    QAction* sketch_polyline_action_{};
    QAction* sketch_rectangle_action_{};
    QAction* sketch_polygon_action_{};
    QMenu* sketch_polygon_menu_{};
    QAction* sketch_trim_action_{};
    QAction* sketch_corner_fillet_action_{};
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
    QAction* sketch_point_line_dimension_action_{};
    QAction* sketch_symmetric_dimension_action_{};
    QAction* sketch_three_point_angle_dimension_action_{};
    QAction* sketch_angle_dimension_action_{};
    QAction* sketch_parallel_distance_dimension_action_{};
    QAction* sketch_radius_dimension_action_{};
    QAction* sketch_diameter_dimension_action_{};
    QAction* sketch_ellipse_major_dimension_action_{};
    QAction* sketch_ellipse_minor_dimension_action_{};
    QAction* sketch_ellipse_rotation_dimension_action_{};
    QAction* sketch_fix_point_action_{};
    QAction* finish_sketch_action_{};
    QAction* regenerate_part_action_{};
    QAction* parameters_action_{};
    QAction* material_action_{};
    QAction* relations_action_{};
    QAction* family_table_action_{};
    QAction* file_settings_action_{};
    QAction* export_action_{};
    QAction* settings_action_{};
    QMenu* standard_views_menu_{};
    QDialog* properties_dialog_{};
    std::string properties_dialog_instance_path_;
    QDialog* rename_document_dialog_{};
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
    // A newly invoked Extrusion/Revolution owns this transient draft while
    // its internal Sketch is being edited. OCCT is not called until the
    // reopened feature Properties dialog is confirmed with OK.
    std::optional<zima::document::HistoryContainer> pending_profile_feature_;
    std::string active_occurrence_path_;
    // "Pohled kolmo" (normal_view_action in Python): while active the viewer
    // is restricted to Face candidates; on selection the camera is rotated
    // to be perpendicular to the picked face and the mode is exited.
    bool normal_view_selection_active_{};
    zima::app::OrientationDialog* orientation_dialog_{};
    std::array<float, 8> orientation_dialog_original_camera_{};
    std::map<std::string, zima::viewer::ViewerCandidate>
        orientation_reference_candidates_;
    std::size_t pending_orientation_reference_index_{};
    std::optional<zima::document::FeatureKind> edge_treatment_selection_;
    PrimitivePropertiesDialog* edge_treatment_dialog_{};
    std::vector<zima::kernel::EdgeReference> pending_edge_treatment_edges_;
    std::vector<std::vector<zima::kernel::EdgeReference>>
        pending_edge_treatment_groups_;
    std::vector<zima::kernel::EdgeReference> pending_edge_treatment_seeds_;
    PrimitivePropertiesDialog* extrusion_target_dialog_{};
    ConstructionPropertiesDialog* construction_reference_dialog_{};
    std::optional<zima::kernel::ViewerMesh> construction_preview_mesh_;
    std::optional<zima::kernel::ViewerMesh> primitive_origin_preview_mesh_;
    std::string sketch_properties_preview_id_;
    zima::kernel::ViewerReferenceGeometry construction_reference_geometry_;
    std::string construction_dimension_object_id_;
    bool refreshing_scene_{};
    std::optional<zima::document::HistoryContainer>
        parameter_dimension_preview_;
    std::optional<zima::document::ConstructionObject>
        construction_parameter_preview_;
    std::optional<std::size_t> pending_construction_reference_index_;
    int construction_translation_dof_{3};
    int construction_rotation_dof_{3};
    // Universal container placement (Box PoC): mirrors the
    // construction_reference_* state above but for HistoryContainer.placement
    // reference picking in PrimitivePropertiesDialog.
    PlacementReferenceDialog* primitive_reference_dialog_{};
    zima::kernel::ViewerReferenceGeometry primitive_reference_geometry_;
    std::optional<std::size_t> pending_primitive_reference_index_;
    int primitive_translation_dof_{3};
    std::string active_sketch_id_;
    std::string sketch_view_state_id_;
    bool sketch_view_back_{};
    int sketch_view_quarter_turns_{};
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
    std::set<std::string> selected_sketch_geometry_ids_;
    bool sketch_point_active_{};
    std::optional<std::array<double, 2>> pending_segment_start_;
    std::string pending_sketch_snap_geometry_id_;
    std::optional<zima::sketcher::ConstraintKind> pending_sketch_snap_kind_;
    std::string pending_segment_start_snap_geometry_id_;
    std::optional<zima::sketcher::ConstraintKind>
        pending_segment_start_snap_kind_;
    std::size_t sketch_segment_inference_cycle_{};
    std::size_t sketch_segment_inference_variant_count_{1};
    bool sketch_inference_cycle_refresh_{};
    bool sketch_skip_candidate_snap_{};
    bool sketch_segment_active_{};
    bool sketch_segment_construction_{};
    bool sketch_polyline_active_{};
    bool sketch_polyline_arc_mode_{};
    std::string pending_polyline_tangent_geometry_id_;
    bool sketch_rectangle_active_{};
    bool sketch_rectangle_axis_selecting_{};
    std::string pending_rectangle_axis_id_;
    std::optional<std::array<double, 2>> pending_rectangle_corner_;
    bool sketch_polygon_active_{};
    unsigned sketch_polygon_sides_{6};
    std::optional<std::array<double, 2>> pending_polygon_center_;
    bool sketch_trim_active_{};
    bool sketch_corner_fillet_active_{};
    std::string pending_corner_fillet_segment_id_;
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
    bool sketch_arc_clockwise_{};
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
    bool sketch_external_profile_active_{};
    bool sketch_coincident_active_{};
    std::string pending_coincident_point_id_;
    zima::sketcher::ConstraintKind pending_point_pair_constraint_kind_{
        zima::sketcher::ConstraintKind::Coincident};
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
    std::string pending_point_dimension_second_id_;
    std::string pending_point_dimension_vertex_id_;
    std::optional<std::array<double, 2>> pending_point_dimension_cursor_;
    zima::sketcher::DimensionKind pending_point_dimension_kind_{
        zima::sketcher::DimensionKind::Distance};
    bool sketch_line_pair_dimension_active_{};
    std::string pending_line_dimension_reference_id_;
    zima::sketcher::DimensionKind pending_line_dimension_kind_{
        zima::sketcher::DimensionKind::DistanceLine};
    std::optional<zima::sketcher::SketchDimension> pending_sketch_dimension_;
    bool preserve_view_on_refresh_{};
    std::map<std::string, std::array<float, 8>> document_camera_states_;
    std::optional<zima::document::PartDocument> sketch_drag_document_;
    std::optional<zima::assembly::AssemblyDocument> assembly_sketch_drag_document_;
    std::string sketch_drag_point_id_;
    std::string sketch_drag_dimension_id_;
    bool sketch_drag_changed_{};
    // Drag-to-adjust for an embedded placement_references row's dimension
    // overlay, addressed by owning occurrence_id + row index rather than a
    // top-level mate_id, matching PartOccurrence::placement_references'
    // storage.
    std::optional<zima::assembly::AssemblyDocument> placement_reference_drag_document_;
    std::string placement_reference_drag_document_id_;
    std::string placement_reference_drag_occurrence_id_;
    std::size_t placement_reference_drag_index_{};
    zima::kernel::Vec3 placement_reference_drag_axis_point_;
    zima::kernel::Vec3 placement_reference_drag_axis_direction_;
    bool placement_reference_drag_angular_{};
    bool placement_reference_drag_changed_{};
    // Free-component drag (matches Python's `_on_insertion_origin_dragged`):
    // active only while `properties_dialog_` is a ComponentPropertiesDialog
    // editing the dragged occurrence. Translates the occurrence's
    // coordinate-system origin along the camera view plane.
    std::optional<zima::assembly::AssemblyDocument> component_drag_document_;
    std::string component_drag_document_id_;
    std::string component_drag_occurrence_id_;
    zima::kernel::Vec3 component_drag_plane_point_;
    zima::kernel::Vec3 component_drag_plane_normal_;
    zima::kernel::Vec3 component_drag_start_hit_;
    zima::kernel::Vec3 component_drag_start_local_origin_;
    bool component_drag_changed_{};
    // Embedded placement-reference picking state for ComponentPropertiesDialog
    // (mirrors pending_construction_reference_index_/construction_reference_dialog_
    // above, but a row has two independently pickable cells).
    ComponentPropertiesDialog* component_placement_dialog_{};
    std::optional<std::size_t> pending_component_placement_index_;
    bool pending_component_placement_component_side_{};
    std::string component_placement_assembly_document_id_;
    std::string component_placement_occurrence_id_;

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
    void insert_component_from_file();
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
    void start_edge_treatment(zima::document::FeatureKind kind);
    void accept_edge_treatment(const zima::viewer::ViewerCandidate& candidate);
    void refresh_edge_treatment_selection_ui();
    void remove_edge_treatment_member(
        std::size_t group, std::optional<std::size_t> member);
    void restore_edge_treatment_route(std::size_t group);
    [[nodiscard]] bool finish_edge_treatment_selection();
    void accept_extrusion_target(const zima::viewer::ViewerCandidate& candidate);
    void finish_extrusion_target_selection();
    void begin_normal_view_selection();
    void accept_normal_view_reference(const zima::viewer::ViewerCandidate& candidate);
    void show_orientation_dialog();
    void accept_orientation_reference(const zima::viewer::ViewerCandidate& candidate);
    void save_active_assembly();
    void save_active_document();
    void save_active_document_as();
    void rename_document_file();
    void delete_current_document_file();
    void delete_all_file_versions();
    void delete_old_file_versions();
    void delete_old_file_versions_keep_latest();
    void delete_working_directory_old_versions();
    void delete_working_directory_old_versions_keep_latest();
    void refresh_delete_file_actions();
    [[nodiscard]] std::optional<std::filesystem::path> active_document_file_path() const;
    [[nodiscard]] static std::vector<std::filesystem::path> document_archive_paths(
        const std::filesystem::path& file_path);
    [[nodiscard]] static std::map<std::filesystem::path, std::vector<std::filesystem::path>>
        working_directory_archive_groups(const std::filesystem::path& directory);
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
    void start_primitive_reference_selection(std::size_t index);
    void accept_primitive_reference(
        const zima::viewer::ViewerCandidate& candidate);
    // Embedded component placement-reference picking (ComponentPropertiesDialog):
    // mirrors start_construction_reference_selection()/accept_construction_reference()
    // but resolves candidates against MateReferenceKind (Face/Axis/Point)
    // instead of ConstructionReference's OCCT-origin-driven contract, and
    // accepts BOTH a component-side and a target-side pick per row.
    void start_component_placement_reference_selection(
        std::size_t index, bool component_side);
    void accept_component_placement_reference(
        const zima::viewer::ViewerCandidate& candidate);
    [[nodiscard]] bool accept_construction_tree_reference(
        const QTreeWidgetItem* item);
    [[nodiscard]] bool accept_primitive_tree_reference(
        const QTreeWidgetItem* item);
    [[nodiscard]] bool accept_component_placement_tree_reference(
        const QTreeWidgetItem* item);
    // Accepts one already-extracted "origin-reference" leaf (Point/Axis/
    // Plane child of a document/construction Origin node) by value instead
    // of by QTreeWidgetItem pointer. accept_construction_reference()
    // synchronously triggers refresh_scene(), which calls tree_->clear()
    // and destroys every QTreeWidgetItem -- a caller that still needs to
    // read sibling tree items afterwards (e.g. the whole-Origin bulk-accept
    // loop in accept_construction_tree_reference()) must capture their data
    // into plain values *before* accepting any one of them, then feed those
    // values through this helper instead of re-reading a QTreeWidgetItem
    // that may already have been deleted.
    [[nodiscard]] bool accept_origin_reference_value(
        const QString& owner_id, const QString& instance_path,
        const QString& semantic_key);
    void show_sketch_properties(const std::string& sketch_id = {});
    void show_sketch_bspline_properties(
        const std::string& sketch_id, const std::string& bspline_id);
    void show_sketch_text_properties(
        const std::string& sketch_id, const std::string& text_id = {});
    void set_sketch_external_reference_mode(bool enabled);
    void accept_sketch_external_reference(
        const zima::viewer::ViewerCandidate& candidate);
    void align_active_sketch_view(bool fit_view = true);
    void flip_active_sketch_view();
    void rotate_active_sketch_view();
    void finish_active_sketch();
    void set_sketch_placement_selection_contract();
    void start_sketch_point();
    void start_sketch_segment(bool construction = false);
    void start_sketch_polyline();
    void cancel_sketch_segment();
    bool cancel_current_sketch_step(bool right_click_behavior = false);
    bool confirm_current_sketch_step();
    bool finish_current_sketch_tool();
    void start_sketch_rectangle();
    void cancel_sketch_rectangle();
    void start_sketch_polygon(unsigned sides);
    void cancel_sketch_polygon();
    void start_sketch_trim();
    void start_sketch_corner_fillet();
    void accept_sketch_corner_fillet_segment(
        const zima::viewer::ViewerCandidate& candidate);
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
        const zima::viewer::ViewerCandidate& candidate,
        const zima::kernel::Vec3& origin,
        const zima::kernel::Vec3& direction);
    struct SketchCandidateSnap {
        zima::kernel::Vec3 origin;
        zima::kernel::Vec3 direction;
        std::string support_geometry_id;
        std::optional<zima::sketcher::ConstraintKind> relation;
    };
    [[nodiscard]] std::optional<SketchCandidateSnap> sketch_candidate_snap_ray(
        const zima::viewer::ViewerCandidate& candidate,
        const zima::kernel::Vec3& origin,
        const zima::kernel::Vec3& direction) const;
    bool accept_sketch_segment_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    struct SketchSegmentInference {
        std::array<double, 2> position;
        std::optional<zima::sketcher::ConstraintKind> kind;
        std::string reference_point_id;
        std::string equal_length_reference_id;
        std::string symmetry_axis_id;
        std::string tangent_reference_id;
        std::string perpendicular_reference_id;
        std::string parallel_reference_id;
        std::string midpoint_line_reference_id;
        std::size_t variant_count{1};
    };
    [[nodiscard]] SketchSegmentInference inferred_sketch_segment_end(
        const std::array<double, 2>& position) const;
    void preview_sketch_segment_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    bool accept_sketch_rectangle_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void preview_sketch_rectangle_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void accept_sketch_rectangle_axis(
        const zima::viewer::ViewerCandidate& candidate);
    bool accept_sketch_polygon_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void preview_sketch_polygon_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    bool accept_sketch_circle_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void preview_sketch_circle_ray(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    [[nodiscard]] std::optional<std::pair<double, std::string>>
        inferred_sketch_circle_radius(
            const std::array<double, 2>& rim_position) const;
    [[nodiscard]] std::optional<std::pair<double, std::string>>
        inferred_sketch_circle_tangent(
            const std::array<double, 2>& rim_position) const;
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
    void start_sketch_coincident(
        zima::sketcher::ConstraintKind kind =
            zima::sketcher::ConstraintKind::Coincident);
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
    [[nodiscard]] bool accept_sketch_dimension_placement_ray(
        const zima::kernel::Vec3& origin,
        const zima::kernel::Vec3& direction);
    void finish_pending_linear_dimension(
        const std::array<double, 2>& cursor);
    void start_sketch_line_pair_dimension(zima::sketcher::DimensionKind kind);
    void accept_sketch_line_pair_dimension(
        const zima::viewer::ViewerCandidate& candidate);
    void toggle_selected_sketch_point_fixed();
    [[nodiscard]] bool begin_sketch_point_drag(
        const zima::viewer::ViewerCandidate& candidate);
    void update_sketch_point_drag(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void end_sketch_point_drag();
    [[nodiscard]] bool begin_sketch_dimension_drag(
        const zima::viewer::ViewerCandidate& candidate);
    void update_sketch_dimension_drag(
        const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction);
    void end_sketch_dimension_drag();
    [[nodiscard]] bool begin_placement_reference_drag(
        const zima::viewer::ViewerCandidate& candidate);
    void update_placement_reference_drag(
        const zima::kernel::Vec3& ray_origin,
        const zima::kernel::Vec3& ray_direction);
    void end_placement_reference_drag();
    [[nodiscard]] bool begin_component_drag(
        const zima::viewer::ViewerCandidate& candidate,
        const zima::kernel::Vec3& ray_origin,
        const zima::kernel::Vec3& ray_direction);
    void update_component_drag(
        const zima::kernel::Vec3& ray_origin,
        const zima::kernel::Vec3& ray_direction);
    void end_component_drag();
    void clear_selected_sketch_geometry();
    [[nodiscard]] bool delete_selected_sketch_geometry();
    void set_active_sketch_geometry_construction(
        const std::string& geometry_id, bool construction);
    void remove_sketch_relation(
        const std::string& sketch_id, const std::string& relation_id,
        bool dimension);
    void toggle_part_container_suppressed(const std::string& container_id);
    void move_part_container(const std::string& container_id, int direction);
    void delete_part_object(const std::string& object_id, const QString& kind,
        bool ask_confirmation = true);
    void show_sketch_dimension_properties(
        const std::string& sketch_id, const std::string& dimension_id = {},
        zima::sketcher::DimensionKind creation_kind =
            zima::sketcher::DimensionKind::Distance,
        const std::string& first_point_id = {},
        const std::string& second_point_id = {},
        const std::string& first_geometry_id = {},
        const std::string& second_geometry_id = {},
        std::optional<std::array<double, 2>> placement = std::nullopt);
    void regenerate_active_part();
    void undo();
    void redo();
    [[nodiscard]] std::vector<zima::kernel::BodyResult> calculate_part(
        const zima::document::PartDocument& document,
        const std::vector<zima::kernel::BodyResult>* previous = nullptr) const;
    void calculate_assembly_cuts(
        zima::assembly::AssemblyDocument& document) const;
    void refresh_tabs();
    void refresh_scene();
    void apply_body_color(const QColor& color);
    void reset_body_color();
    void show_body_color_dialog();
    void update_body_color_actions();
    void update_viewer_body_colors();
    [[nodiscard]] std::optional<std::string> selected_occurrence_path() const;
    [[nodiscard]] QColor selected_body_color() const;
    void update_document_kind_button();
    void navigate_document_kind();
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
    void select_occurrence(const std::string& instance_path);
    void select_container(const std::string& container_id);
    void show_component_properties(const std::string& instance_path);
    void show_component_context_menu(
        const std::string& instance_path, const QPoint& global_position);
    [[nodiscard]] std::optional<std::string> resolve_active_occurrence(
        const std::string& part_document_id) const;

protected:
    void keyPressEvent(QKeyEvent* event) override;
};

}  // namespace zima::app
