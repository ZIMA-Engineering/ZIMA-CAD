#pragma once

// Private implementation support for the split workspace sources.
// Do not include this header from public APIs or model/kernel modules.
#include "tab_close_button.hpp"
#include <zima/document/body_origin_attachment.hpp>
#include <zima/workspace/native_documents.hpp>
#include "sketch_offset_dialog.hpp"
#include <zima/sketcher/curve_geometry.hpp>
#include "import_options_dialog.hpp"
#include <zima/document/object_annotation_frames.hpp>
#include "dimension_properties_fields.hpp"
#include <QSaveFile>
#include "appearance_dialog.hpp"
#include "measurement_dialog.hpp"
#include <zima/interchange/model_import.hpp>
#include <zima/document/feature_sketches.hpp>
#include "section_properties_dialog.hpp"
#include <zima/drawing/drawing_template.hpp>
#include "derived_copy_dialog.hpp"
#include "shaft_thread_dialog.hpp"
#include "body_properties_dialog.hpp"
#include "shaft_thread_preview.hpp"
#include "sketch_constraints_dialog.hpp"
#include "history_reorder_policy.hpp"
#include "history_tree_widget.hpp"
#include "reference_tree_policy.hpp"
#include "edge_treatment_tree_policy.hpp"
#include "opening_tree_policy.hpp"
#include "assembly_workspace_window.hpp"
#include "reference_display.hpp"
#include <zima/ui/container_placement_section.hpp>
#include <zima/assembly/physical_properties.hpp>
#include "helical_sweep_dialog.hpp"
#include "sweep2d_dialog.hpp"
#include <zima/document/helical_geometry.hpp>
#include <zima/document/precision.hpp>
#include "component_properties_dialog.hpp"
#include "primitive_properties_dialog.hpp"
#include "construction_properties_dialog.hpp"
#include "orientation_dialog.hpp"
#include "sketch_properties_dialog.hpp"
#include "sketch_bspline_properties_dialog.hpp"
#include "sketch_text_properties_dialog.hpp"
#include "sketch_dimension_properties_dialog.hpp"
#include "drawing_window.hpp"
#include "document_tools_dialogs.hpp"
#include "construction_reference_candidate_policy.hpp"
#include "extrusion_dimension_policy.hpp"
#include "opening_dimension_policy.hpp"
#include "thread_catalog.hpp"
#include "file_dialog.hpp"
#include "global_settings_dialog.hpp"
#include "resource_icon.hpp"

#include <zima/viewer/mesh_view.hpp>
#include <zima/interchange/interchange.hpp>
#include <zima/interchange/dxf.hpp>
#include <zima/interchange/step.hpp>
#include <zima/interchange/step_model.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QAbstractItemView>
#include <QBrush>
#include <QComboBox>
#include <QColor>
#include <QCursor>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFont>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPointer>
#include <QMenuBar>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QKeySequence>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QTextStream>
#include <QTimer>
#include <QPixmap>
#include <QPainter>
#include <QDebug>
#include <QPushButton>
#include <QProxyStyle>
#include <QProgressBar>
#include <QProcess>
#include <QRadioButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QStyleOptionToolButton>
#include <QStyleOptionProgressBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <future>
#include <iterator>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <type_traits>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

namespace zima::app::workspace_detail {

// Tangency expresses a less visually obvious intent than point coincidence
// or H/V alignment.  Give it a wider screen-space capture band while keeping
// all stored geometry exact.  The conversion through MeshView makes this
// independent of model zoom.
constexpr double sketch_tangent_intent_pixels = 8.0;
constexpr double sketch_circle_tangent_contact_pixels = 18.0;
constexpr double visible_parameter_dimension_epsilon = 1.0e-9;

void append_nonzero_parameter_dimensions(
    std::vector<zima::kernel::ViewerDimension>& target,
    std::vector<zima::kernel::ViewerDimension> source);

template <typename Function>
auto run_background_task(Function&& function) {
    using Result = std::invoke_result_t<std::decay_t<Function>>;
    auto future = std::async(
        std::launch::async, std::forward<Function>(function));
    using namespace std::chrono_literals;
    while (future.wait_for(0ms) != std::future_status::ready) {
        QApplication::processEvents(
            QEventLoop::ExcludeUserInputEvents, 16);
        std::this_thread::sleep_for(4ms);
    }
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 1);
    if constexpr (std::is_void_v<Result>) {
        future.get();
    } else {
        return future.get();
    }
}

std::optional<std::array<double, 2>> sketch_point_reference_position(
    const zima::sketcher::Sketch& sketch, const std::string& reference_id);

// An axis/construction reference for the universal dimension tool: either a
// global sketch axis, or a native segment explicitly drawn as construction
// (centerline) geometry. Distinguishing this from an ordinary real line lets
// the "line, axis, line" click sequence offer a symmetric dimension.
bool sketch_axis_like_reference(
    const zima::sketcher::Sketch& sketch, const std::string& id);

std::optional<std::array<double, 2>> exact_circle_tangent_contact(
    const zima::sketcher::Sketch& sketch,
    const std::array<double, 2>& start,
    const std::string& circle_id,
    const std::array<double, 2>& hint);

int document_decimal_places(const auto& document) noexcept {
    const auto found = document.document_precision.find("decimal_places");
    if (found == document.document_precision.end()) return 3;
    try {
        return std::clamp(std::stoi(found->second), 0, 12);
    } catch (...) {
        return 3;
    }
}

double rounded_to_decimal_places(double value, int decimal_places);

void normalize_owned_profile_front_references(
        std::vector<zima::document::ConstructionReference>& references,
        bool preserve_front_through_origin_triad = false);

zima::kernel::Vec3 euler_degrees_from_frame_columns(
        const zima::kernel::Vec3& x_axis,
        const zima::kernel::Vec3& y_axis,
        const zima::kernel::Vec3& z_axis);

// One common local-to-world frame for every parameter dimension owned by a
// HistoryContainer. Dimension definitions stay in feature-local coordinates;
// points and witness directions pass through this exact final placement frame
// on every preview refresh. This keeps dimensions and preview geometry in the
// same plane for arbitrary reference changes, FRONT/BACK, quarter turns and
// manual corrections.
struct ContainerDimensionFrame {
    zima::kernel::Vec3 origin;
    std::array<std::array<double, 3>, 3> rotation;

    [[nodiscard]] zima::kernel::Vec3 inverse_vector(const zima::kernel::Vec3& v) const {
        return {rotation[0][0]*v.x+rotation[1][0]*v.y+rotation[2][0]*v.z,
            rotation[0][1]*v.x+rotation[1][1]*v.y+rotation[2][1]*v.z,
            rotation[0][2]*v.x+rotation[1][2]*v.y+rotation[2][2]*v.z};
    }
    [[nodiscard]] zima::kernel::Vec3 inverse_point(const zima::kernel::Vec3& v) const {
        return inverse_vector({v.x-origin.x,v.y-origin.y,v.z-origin.z});
    }

    [[nodiscard]] zima::kernel::Vec3 vector(
            const zima::kernel::Vec3& value) const {
        return {
            rotation[0][0] * value.x + rotation[0][1] * value.y +
                rotation[0][2] * value.z,
            rotation[1][0] * value.x + rotation[1][1] * value.y +
                rotation[1][2] * value.z,
            rotation[2][0] * value.x + rotation[2][1] * value.y +
                rotation[2][2] * value.z};
    }

    [[nodiscard]] zima::kernel::Vec3 point(
            const zima::kernel::Vec3& value) const {
        const auto transformed = vector(value);
        return {origin.x + transformed.x, origin.y + transformed.y,
            origin.z + transformed.z};
    }
};

ContainerDimensionFrame container_dimension_frame(
        const zima::document::Placement& placement);

// Builds the precise per-entity key set for every currently-toggled
// highlighted reference row. Each ConstructionReference already carries a
// (owner_id, semantic_key, instance_path) triple that uniquely identifies
// one specific entity -- e.g. one exact Origin plane/axis/point
// ("origin:plane:xz" vs "origin:plane:xy"), or one exact face of a body
// ("plocha 6" vs "plocha 3") -- even though many sibling entities share the
// same owner_id (the Origin's own three planes and three axes, or every
// face of the same body). Matching by owner_id alone (as
// highlighted_reference_owner_ids() does, kept only for whole-container
// references that have no such siblings) would wrongly highlight every one
// of those siblings at once instead of just the single referenced entity.
std::set<zima::viewer::EdgeKey> highlighted_reference_edge_keys(
    const auto& dialog) {
    std::set<zima::viewer::EdgeKey> keys;
    for (const auto& reference : dialog.highlighted_reference_entries()) {
        keys.insert(zima::viewer::EdgeKey{
            reference.owner_id, reference.semantic_key, reference.instance_path});
        // A standalone Plane persists its semantic entity as "plane", while
        // its only visible/pickable geometry is the rectangular "border".
        // They are one object, so reference highlighting must address that
        // border without broad owner-only matching.
        if (reference.semantic_key == "plane") {
            keys.insert(zima::viewer::EdgeKey{
                reference.owner_id, "border", reference.instance_path});
        }
    }
    return keys;
}

// Every primitive solid shares the universal container placement UI
// (position/orientation reference tables) wired in PrimitivePropertiesDialog.
bool supports_placement_reference_picking(zima::document::FeatureKind kind);

bool sketch_visible_outside_sketcher(
    const zima::document::PartDocument& document,
    const zima::sketcher::Sketch& sketch);

// A picked reference supports an editable offset when it is a planar
// surface: either a solid/sketch Face, or a construction/origin datum Plane
// (Container candidate whose semantic_key resolves to "plane"), matching
// Python's `_reference_supports_offset()` (also true for EntityKind.PLANE).
bool candidate_supports_offset(const zima::viewer::ViewerCandidate& candidate);

// A picked reference is a directional (planar/linear) candidate when it is a
// Face, Edge or Axis -- including the persisted origin plane/axis overlays,
// whose semantic keys are "origin:plane:*"/"origin:axis:*" -- matching
// Python's `is_orientation_candidate` test in `_add_reference()`.  Such a
// reference simultaneously fixes both position AND, unlike a bare vertex,
// part of the object's orientation.
bool candidate_drives_rotation(const zima::viewer::ViewerCandidate& candidate);

bool placement_angle_uses_reference_correction(
        const std::vector<zima::document::ConstructionReference>& references,
        const zima::kernel::ViewerReferenceGeometry& geometry,
        const zima::kernel::Vec3& origin, std::size_t axis_index);

// Whether the classic history-order shortcut ("1st point = origin, 2nd =
// axis direction" for an Axis; "1st point = origin, 2nd/3rd = plane-defining
// points" for a Plane) already has every point it needs -- used to stop
// offering further reference rows once the container's direction/normal is
// fully determined this way, since the generic rotation-DOF count cannot
// see it (a bare point/vertex is never marked orientation-driving).
bool construction_shortcut_satisfied(zima::document::ConstructionKind kind,
    const std::vector<zima::document::ConstructionReference>& references,
    const zima::kernel::ViewerReferenceGeometry& geometry);

// Assigns the next unused orientation role ("normal" first, then "up") to a
// newly accepted Point position reference that drives rotation, matching
// Python's `_default_orientation_role()`/`_ensure_automatic_orientation_roles()`.
// A Point container has no dedicated orientation-reference table: the same
// position reference simultaneously participates in placement (equations
// solved by `resolve_construction`) and, once marked, in the rotation-DOF
// count via `orientation_constraint_remaining_dof(..., marked_only=true)`.
void assign_automatic_orientation_role(
    zima::document::ConstructionReference& reference,
    const std::vector<zima::document::ConstructionReference>& existing);


zima::workspace::NativeTemplateSettings native_template_settings(
    const ApplicationSettings& settings);

zima::document::PartDocument new_part_from_template(
    const ApplicationSettings& settings);

zima::assembly::AssemblyDocument new_assembly_from_template(
    const ApplicationSettings& settings);

void append_reference_geometry(
    zima::kernel::ViewerReferenceGeometry& target,
    zima::kernel::ViewerReferenceGeometry source);

void append_mesh(zima::kernel::ViewerMesh& target, zima::kernel::ViewerMesh source);

zima::kernel::ViewerMesh local_origin_display_mesh(
        const zima::kernel::ViewerReferenceGeometry& geometry,
        const std::set<std::string>& visible_origin_ids);

void keep_only_inactive_sketch_profile(
    zima::kernel::ViewerMesh& mesh,
    const std::string& display_owner_id);

void remove_sketch_computation_points(zima::kernel::ViewerMesh& mesh);

QTreeWidgetItem* add_origin_tree_item(QTreeWidgetItem* parent,
    const std::string& document_id, bool assembly,
    const zima::assembly::InstancePath& instance_path = {});

QTreeWidgetItem* add_construction_origin_tree_item(QTreeWidgetItem* parent,
    const zima::document::ContainerOrigin& container_origin,
    const std::string& container_name,
    const zima::assembly::InstancePath& instance_path = {},
    bool point_kind_container = false);

QString feature_icon_name(zima::document::FeatureKind kind);

void add_construction_tree_children(QTreeWidgetItem* parent,
    const zima::document::ConstructionObject& object,
    const zima::assembly::InstancePath& instance_path);

void add_history_container_tree_children(QTreeWidgetItem* parent,
    const zima::document::HistoryContainer& container,
    const zima::assembly::InstancePath& instance_path = {},
    const zima::sketcher::Sketch* owned_sketch = nullptr,
    bool assembly_owned = false);

void add_construction_tree_children(QTreeWidgetItem* parent,
    const zima::document::ConstructionObject& object,
    const zima::assembly::InstancePath& instance_path = {});

using SketchPosition = std::array<double, 2>;

std::optional<std::string> sketch_text_id_from_key(const std::string& key);

std::string revolution_axis_segment_id(
    const zima::sketcher::Sketch& sketch,
    const std::string& configured_id = {});

struct RevolutionCueFrame {
    zima::kernel::Vec3 center;
    zima::kernel::Vec3 axis;
    zima::kernel::Vec3 radial;
};

std::optional<RevolutionCueFrame> revolution_cue_frame(
    const zima::sketcher::Sketch& sketch, const std::string& axis_segment_id);

zima::kernel::ViewerMesh local_container_context_mesh(
        const zima::document::PartDocument& document,
        const std::set<std::string>& visible_origin_ids,
        bool show_axes, bool show_planes);

std::optional<std::string> sketch_external_reference_id_from_key(
    const std::string& key);

std::optional<std::string> sketch_keypoint_curve_id(
    const std::string& key);

std::optional<std::pair<std::string,std::string>> common_tangent_supports(
    const zima::sketcher::Sketch& sketch, const std::string& first_support,
    const std::string& second_support, const std::array<double,2>& first,
    const std::array<double,2>& second, double tolerance);

std::string apply_sketch_point_snap(zima::sketcher::Sketch& sketch,
    const std::string& point_id, const std::string& support_id,
    const std::optional<zima::sketcher::ConstraintKind>& kind);

QString sketch_constraint_label(zima::sketcher::ConstraintKind kind);

QIcon sketch_constraint_tree_icon(zima::sketcher::ConstraintKind kind);

QString sketch_dimension_label(const zima::sketcher::SketchDimension& dimension);

std::vector<zima::kernel::ViewerEdge> sketch_text_preview_edges(
    const zima::sketcher::Sketch& sketch,
    const zima::sketcher::SketchText& text);

std::set<std::string> sketch_external_reference_source_owners(
    const zima::document::PartDocument& document,
    const std::string& sketch_id);

zima::kernel::ViewerReferenceGeometry sketch_external_reference_source_geometry(
    const zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries);

zima::kernel::ViewerReferenceGeometry construction_reference_source_geometry(
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries);

zima::kernel::ViewerReferenceGeometry part_construction_dimension_geometry(
    const zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries);

bool refresh_sketch_external_references(
    zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries);

bool prune_missing_drill_point_references(
    zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& boundaries);

bool refresh_assembly_sketch_external_references(
    zima::assembly::AssemblyDocument& document);

void populate_external_reference_cache(
    const zima::sketcher::Sketch& sketch,
    zima::sketcher::SketchExternalReference& reference,
    const zima::kernel::ViewerReferenceGeometry& source);

std::optional<SketchPosition> projected_ellipse_minor(
    const SketchPosition& center, const SketchPosition& major,
    const SketchPosition& cursor);

struct ProjectedEllipsePosition {
    SketchPosition position;
    double parameter{};
};

std::optional<ProjectedEllipsePosition> projected_ellipse_position(
    const SketchPosition& center, const SketchPosition& major,
    const SketchPosition& minor, const SketchPosition& cursor);

zima::kernel::ViewerEdge ellipse_preview_edge(
    const zima::sketcher::Sketch& sketch, const SketchPosition& center,
    const SketchPosition& major, const SketchPosition& minor);

zima::document::ConstructionObject sweep_display_path(
    const zima::document::HistoryContainer& container);

double edge_preview_length(const zima::kernel::Vec3& value);

zima::kernel::Vec3 edge_preview_difference(
    const zima::kernel::Vec3& left, const zima::kernel::Vec3& right);

double edge_preview_dot(
    const zima::kernel::Vec3& left, const zima::kernel::Vec3& right);

std::optional<zima::kernel::Vec3> edge_preview_normalized(
    const zima::kernel::Vec3& value);

zima::kernel::Vec3 edge_preview_offset(const zima::kernel::Vec3& point,
    const zima::kernel::Vec3& direction, double distance);

bool edge_preview_same_point(
    const zima::kernel::Vec3& first, const zima::kernel::Vec3& second);

bool viewer_edges_same_geometry(
    const zima::kernel::ViewerEdge& first,
    const zima::kernel::ViewerEdge& second);

std::size_t restore_surviving_edge_references_after_history_delete(
    zima::document::PartDocument& document,
    const std::string& deleted_owner,
    const zima::kernel::BodyResult& input_before_deleted,
    const std::vector<zima::kernel::BodyResult>& old_boundaries);

struct EdgeTreatmentPreviewPath {
    std::vector<zima::kernel::ViewerEdge> edges;
};

struct EdgeTreatmentPreviewGeometry {
    std::vector<zima::kernel::ViewerEdge> edges;
    std::vector<zima::kernel::ViewerDimension> dimensions;
};

bool edge_preview_has_face_guides(const zima::kernel::ViewerEdge& edge);

void edge_preview_reverse(zima::kernel::ViewerEdge& edge);

std::vector<EdgeTreatmentPreviewPath> ordered_edge_treatment_preview_paths(
    std::vector<zima::kernel::ViewerEdge> edges);

zima::kernel::Vec3 edge_preview_blend_direction(
    const zima::kernel::Vec3& first, const zima::kernel::Vec3& second,
    double ratio);

struct EdgeTreatmentSection {
    zima::kernel::ViewerEdge wire;
    zima::kernel::Vec3 first_tangent;
    zima::kernel::Vec3 second_tangent;
    std::optional<zima::kernel::Vec3> radius_center;
    std::optional<zima::kernel::Vec3> radius_rim;
    zima::kernel::Vec3 plane_normal;
    double first_distance{};
    double second_distance{};
};

std::optional<EdgeTreatmentSection> edge_treatment_section(
    const zima::kernel::Vec3& point,
    const zima::kernel::Vec3& raw_first_direction,
    const zima::kernel::Vec3& raw_second_direction,
    const zima::document::EdgeTreatmentParameters& parameters,
    bool fillet, double radius);

void edge_preview_append_corner(std::vector<zima::kernel::Vec3>& rail,
    const zima::kernel::Vec3& corner,
    const zima::kernel::Vec3& target);

EdgeTreatmentPreviewGeometry edge_treatment_preview_wire(
    const std::vector<std::vector<zima::kernel::ViewerEdge>>& groups,
    const zima::document::EdgeTreatmentParameters& parameters,
    zima::document::FeatureKind kind, const std::string& owner_id);
std::pair<double, double> camera_angles_for_view_direction(const zima::kernel::Vec3& direction);
double camera_roll_for_direction(const zima::kernel::Vec3& view_direction, const zima::kernel::Vec3& world_direction, double target_angle_degrees);

} // namespace zima::app::workspace_detail
