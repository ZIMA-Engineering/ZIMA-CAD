#include <zima/viewer/mesh_view.hpp>
#include <zima/viewer/picking.hpp>

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QFontMetricsF>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPainter>
#include <QRectF>
#include <QQuaternion>
#include <QEasingCurve>
#include <QVariantAnimation>
#include <QVector3D>
#include <QVector4D>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <numbers>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace zima::viewer {

namespace {

// A new, otherwise empty metric CAD document starts with roughly 100 mm of
// vertical working area. This gives a useful physical sense of scale before
// the first body exists: a 1 mm plane offset is visible, but no longer fills
// a large part of the View.
constexpr float kEmptyDocumentViewHalfExtent = 50.0F;

double screen_segment_distance(const QPointF& point, const QPointF& first,
    const QPointF& second) {
    const QPointF delta = second - first;
    const double squared = delta.x() * delta.x() + delta.y() * delta.y();
    if (squared <= 1.0e-12) return QLineF(point, first).length();
    const QPointF relative = point - first;
    const double parameter = std::clamp(
        (relative.x() * delta.x() + relative.y() * delta.y()) / squared,
        0.0, 1.0);
    return QLineF(point, first + delta * parameter).length();
}

struct LinearDimensionLayout {
    QPointF witness_first;
    QPointF witness_second;
    QPointF line_first;
    QPointF line_second;
    QPointF along;
    QPointF first_tail;
    QPointF second_tail;
    QPointF leader_start;
    QPointF leader_end;
    QPointF text_baseline;
};

template <typename Project>
LinearDimensionLayout linear_dimension_layout(
    const zima::kernel::ViewerDimension& dimension,
    const zima::kernel::ViewerMesh& mesh, Project&& project) {
    LinearDimensionLayout result;
    result.witness_first = project(dimension.witness_first);
    result.witness_second = project(dimension.witness_second);
    result.line_first = project(dimension.line_first);
    result.line_second = project(dimension.line_second);
    QPointF direction = result.line_second - result.line_first;
    double length = std::hypot(direction.x(), direction.y());
    if (length <= 1.0e-6 &&
        dimension.kind == zima::kernel::ViewerDimensionKind::Linear) {
        const zima::kernel::Vec3 direction_tip{
            dimension.line_second.x + dimension.plane_normal.x,
            dimension.line_second.y + dimension.plane_normal.y,
            dimension.line_second.z + dimension.plane_normal.z};
        direction = project(direction_tip) - result.line_second;
        length = std::hypot(direction.x(), direction.y());
    }
    if (length <= 1.0e-6) {
        char axis_name = '\0';
        if (dimension.reference.semantic_key == "parameter:x") axis_name = 'x';
        else if (dimension.reference.semantic_key == "parameter:y") axis_name = 'y';
        else if (dimension.reference.semantic_key == "parameter:z") axis_name = 'z';
        const zima::kernel::ViewerAxis* exact_axis{};
        const zima::kernel::ViewerAxis* nearest_axis{};
        double exact_distance = std::numeric_limits<double>::infinity();
        double nearest_distance = std::numeric_limits<double>::infinity();
        for (const auto& candidate : mesh.axes) {
            if (axis_name == '\0' ||
                candidate.reference.instance_path !=
                    dimension.reference.instance_path) continue;
            const std::string suffix = std::string("axis:") + axis_name;
            if (!candidate.reference.semantic_key.ends_with(suffix)) continue;
            const double dx = candidate.point.x - dimension.witness_second.x;
            const double dy = candidate.point.y - dimension.witness_second.y;
            const double dz = candidate.point.z - dimension.witness_second.z;
            const double distance = dx * dx + dy * dy + dz * dz;
            if (candidate.reference.owner_id == dimension.reference.owner_id &&
                distance < exact_distance) {
                exact_axis = &candidate;
                exact_distance = distance;
            }
            if (distance < nearest_distance) {
                nearest_axis = &candidate;
                nearest_distance = distance;
            }
        }
        const auto* axis = exact_axis != nullptr ? exact_axis : nearest_axis;
        if (axis != nullptr) {
            const zima::kernel::Vec3 direction_tip{
                dimension.line_second.x + axis->direction.x,
                dimension.line_second.y + axis->direction.y,
                dimension.line_second.z + axis->direction.z};
            direction = project(direction_tip) - result.line_second;
            length = std::hypot(direction.x(), direction.y());
        }
    }
    if (length <= 1.0e-6) {
        const QPointF witness_direction =
            result.witness_second - result.witness_first;
        direction = {-witness_direction.y(), witness_direction.x()};
        length = std::hypot(direction.x(), direction.y());
    }
    result.along = length > 1.0e-6
        ? direction / length : QPointF{1.0, 0.0};
    result.first_tail = result.line_first - result.along * 17.0;
    result.second_tail = result.line_second + result.along * 17.0;
    result.leader_start = result.first_tail.x() > result.second_tail.x()
        ? result.first_tail : result.second_tail;
    result.leader_end = result.leader_start + QPointF(30.0, 0.0);
    result.text_baseline = result.leader_end + QPointF(4.0, 0.0);
    return result;
}

bool is_screen_constant_plane(const std::string& semantic_key) {
    return semantic_key == "border" ||
        semantic_key.starts_with("origin:plane:");
}

void draw_circular_marker(QPainter& painter, const QPointF& center,
    const QColor& color, double radius = 4.5) {
    QPolygonF polygon;
    constexpr int segments = 32;
    polygon.reserve(segments);
    for (int segment = 0; segment < segments; ++segment) {
        const double angle = 2.0 * std::numbers::pi * segment / segments;
        polygon.push_back(center + QPointF(
            std::cos(angle) * radius, std::sin(angle) * radius));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(polygon);
}

}  // namespace

struct MeshView::Impl {
    zima::kernel::ViewerMesh mesh;
    // Picking traverses this compact view on every pointer move. Build it once
    // when the scene changes instead of copying all persisted geometry for
    // every hover sample.
    zima::kernel::ViewerMesh persisted_reference_mesh;
    QOpenGLShaderProgram program;
    QOpenGLBuffer vertices{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer triangles{QOpenGLBuffer::IndexBuffer};
    QOpenGLBuffer lines{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer silhouette{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject vertex_array;
    std::vector<std::pair<GLint, GLsizei>> line_ranges;
    // Mesh edges kept in lock-step with line_ranges (same filtering as
    // upload_mesh's line_data build) purely so paintGL can resolve each
    // edge's highlight-priority colour before its individual draw call.
    std::vector<zima::kernel::ViewerEdge> line_edges;
    // Candidate internal triangulation edges eligible to become silhouettes
    // (shared by exactly two triangles of the same owning face, not already
    // a real topological edge). Rebuilt only when the mesh changes; the
    // actual visible segments are re-selected every frame from the camera
    // direction. Mirrors Python's build_silhouette_edges()/
    // silhouette_segments_from_edges() in zima_cad/viewer_data.py.
    struct SilhouetteCandidate {
        QVector3D first;
        QVector3D second;
        QVector3D normal_a;
        QVector3D normal_b;
    };
    std::vector<SilhouetteCandidate> silhouette_candidates;
    std::vector<CandidateKind> allowed_kinds{CandidateKind::Container};
    std::function<bool(const ViewerCandidate&)> candidate_filter;
    std::vector<ViewerCandidate> candidates;
    // Position at which `candidates` was last (re)computed. RMB cycling
    // reuses the list as-is instead of forcing a redundant recompute when the
    // pointer is still within picking tolerance of this position, so the
    // first RMB press after a stationary hover advances the cycle instead of
    // merely re-stabilizing an already-correct list.
    QPointF candidates_position;
    bool has_candidates_position{};
    std::size_t active_candidate{};
    std::optional<ViewerCandidate> confirmed_candidate;
    std::string selected_container_origin_id;
    std::function<void(const ViewerCandidate&)> confirmation_callback;
    std::function<void()> empty_confirmation_callback;
    std::function<void(const ViewerCandidate&)> double_confirmation_callback;
    std::function<bool(const ViewerCandidate&, const zima::kernel::Vec3&,
        const zima::kernel::Vec3&)> drag_begin_callback;
    std::function<void(const zima::kernel::Vec3&, const zima::kernel::Vec3&)>
        drag_update_callback;
    std::function<void()> drag_end_callback;
    bool drag_active{};
    std::function<void(const ViewerCandidate&, const QPoint&)> context_menu_callback;
    std::function<bool(const zima::kernel::Vec3&, const zima::kernel::Vec3&)>
        world_click_callback;
    std::function<void(const zima::kernel::Vec3&, const zima::kernel::Vec3&)>
        world_pointer_callback;
    std::function<bool(
        const std::optional<ViewerCandidate>&,
        const zima::kernel::Vec3&, const zima::kernel::Vec3&)>
        command_gesture_begin_callback;
    std::function<void(const zima::kernel::Vec3&, const zima::kernel::Vec3&)>
        command_gesture_update_callback;
    std::function<void()> command_gesture_end_callback;
    bool command_gesture_active{};
    std::function<bool()> short_middle_click_callback;
    std::function<bool()> double_middle_click_callback;
    bool sketch_box_selection_enabled{};
    std::optional<QPointF> sketch_box_start;
    std::optional<QPointF> sketch_box_end;
    std::function<void(std::vector<ViewerCandidate>, bool)>
        sketch_box_selection_callback;
    std::vector<ViewerCandidate> sketch_box_selected_candidates;
    std::function<bool()> empty_right_click_callback;
    std::function<bool(const ViewerCandidate&)>
        single_candidate_right_click_callback;
    std::function<bool(const ViewerCandidate&, std::size_t, std::size_t)>
        candidate_right_click_callback;
    bool middle_dragged{};
    bool middle_double_clicked{};
    QPoint middle_press_position;
    std::vector<zima::kernel::ViewerEdge> transient_edges;
    std::vector<zima::kernel::Vec3> transient_points;
    std::vector<std::pair<zima::kernel::Vec3, std::string>> transient_labels;
    std::optional<zima::kernel::Vec3> sketch_cursor;
    bool sketch_cursor_snapped{};
    std::string sketch_cursor_label;
    std::vector<ExtentManipulator> extent_manipulators;
    std::vector<OperationDirectionIndicator> operation_direction_indicators;
    std::optional<ExtentManipulator> dragged_extent_manipulator;
    std::string dragged_extent_key;
    std::string hovered_extent_key;
    std::function<void(const std::string&)> extent_begin_callback;
    std::function<void(const std::string&, double)> extent_update_callback;
    std::function<void()> extent_end_callback;
    std::function<zima::kernel::Vec3(const zima::kernel::Vec3&)>
        transient_point_transform;
    // Per-edge highlight priority state. Mirrors the frozenset bookkeeping in
    // zima_cad/viewer.py's MeshView (_edge_display_color/_edge_is_highlighted):
    // selected > object overlay > hovered > feature preview > color override
    // > base color. Populated via the set_*_edges/owners setters below.
    std::set<EdgeKey> edge_treatment_selection_edges;
    std::set<EdgeKey> feature_hover_edges;
    std::set<EdgeKey> feature_selected_edges;
    std::set<std::string> feature_preview_owner_ids;
    std::string active_sketch_owner_id;
    std::set<std::string> constraint_reference_owner_ids;
    std::set<EdgeKey> constraint_reference_edges;
    std::set<EdgeKey> assembly_reference_edges;
    std::set<std::string> selected_container_content_ids;
    std::set<EdgeKey> object_overlay_main_edge_keys;
    std::optional<QColor> edge_color_override;
    QColor body_surface_color{"#B9C2CC"};
    std::map<std::string, QColor> body_surface_instance_colors;
    QPoint last_pointer;
    QVector3D center;
    QPointF pan_pixels;
    float radius{1.0F};
    float view_scale{1.4F};
    float reference_view_scale{1.4F};
    // Once a baseline reference_view_scale has been established (first fit
    // of a brand-new document, or restored from a saved camera state), later
    // fit_all() calls must NOT overwrite it. reference_view_scale is the
    // fixed "screen-constant" anchor that keeps the Origin's (and every
    // container's own origin) apparent size in the View identical no matter
    // how far the camera has to zoom out to fit newly added geometry -- see
    // fit_all()'s comment for the full reasoning.
    bool reference_view_scale_initialized{false};
    QQuaternion orientation = [] {
        return QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, 0.0F) *
            QQuaternion::fromAxisAndAngle(1.0F, 0.0F, 0.0F, -45.0F) *
            QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, 215.264F);
    }();
    // Mirrors Python's animate_standard_view/animate_view_normal: a running
    // QVariantAnimation slerps orientation and lerps pan/zoom back to the
    // resting state, matching zima_cad/viewer.py's camera transition feel
    // instead of a hard jump cut.
    QVariantAnimation* camera_animation{};
    DisplayMode display_mode{DisplayMode::ShadedWithEdges};
    int dimension_decimal_places{3};
    bool show_origins{true};
    bool show_points{true};
    bool show_axes{true};
    bool show_planes{true};
    bool show_sketches{true};
    bool editing_origin_visible{};
    bool gpu_dirty{true};

    void rebuild_persisted_reference_mesh() {
        auto& target = persisted_reference_mesh;
        target = {};
        // Screen-constant datum-plane borders are rebuilt below from the
        // displayed rectangle. Do not retain their unscaled model-space
        // originals as a second pick target: that produced exactly the two
        // simultaneous hover offers visible in 01.png/02.png (the small
        // displayed rectangle plus a much larger invisible rectangle).
        std::copy_if(mesh.original_references.edges.begin(),
            mesh.original_references.edges.end(),
            std::back_inserter(target.edges), [](const auto& edge) {
                return !is_screen_constant_plane(edge.reference.semantic_key);
            });
        target.points = mesh.original_references.points;
        target.axes = mesh.original_references.axes;
        const double scale = view_scale /
            std::max(reference_view_scale, 1.0e-6F);
        for (auto& axis : target.axes) {
            if (axis.reference.semantic_key.starts_with("origin:axis:")) {
                axis.display_length *= scale;
            }
        }
        // Origin and work planes are orientation aids and therefore keep the
        // same fixed apparent LCD size. The picker consumes this exact scaled
        // rectangle too, so hover and confirmation cannot drift from it.
        for (const auto& edge : mesh.edges) {
            if (edge.reference.semantic_key != "border" &&
                !edge.reference.semantic_key.starts_with("origin:plane:")) continue;
            if (edge.points.size() < 4) continue;
            auto scaled = edge;
            const std::size_t count = edge.points.size() - 1;
            zima::kernel::Vec3 center;
            for (std::size_t index = 0; index < count; ++index) {
                center.x += edge.points[index].x;
                center.y += edge.points[index].y;
                center.z += edge.points[index].z;
            }
            center = {center.x / count, center.y / count, center.z / count};
            const double edge_scale = is_screen_constant_plane(
                edge.reference.semantic_key) ? scale : 1.0;
            for (auto& point : scaled.points) {
                point = {center.x + (point.x - center.x) * edge_scale,
                         center.y + (point.y - center.y) * edge_scale,
                         center.z + (point.z - center.z) * edge_scale};
            }
            target.edges.push_back(std::move(scaled));
        }
        // Keep persisted solid faces unchanged. Origin planes are screen-size
        // overlays; rebuild their pick triangles from the exact scaled border
        // which paintGL displays, so hover and click cannot be spatially offset.
        for (std::size_t triangle = 0;
             triangle < mesh.original_references.triangle_references.size();
             ++triangle) {
            const auto& reference =
                mesh.original_references.triangle_references[triangle];
            if (reference.semantic_key.starts_with("origin:plane:") ||
                reference.semantic_key == "plane") continue;
            if (triangle * 3 + 2 >= mesh.original_references.triangles.size()) continue;
            const auto base = static_cast<std::uint32_t>(target.vertices.size());
            for (std::size_t corner = 0; corner < 3; ++corner) {
                target.vertices.push_back(mesh.original_references.vertices[
                    mesh.original_references.triangles[triangle * 3 + corner]]);
                target.triangles.push_back(base + static_cast<std::uint32_t>(corner));
            }
            target.triangle_references.push_back(reference);
        }
        for (const auto& edge : mesh.edges) {
            if (!is_screen_constant_plane(edge.reference.semantic_key) ||
                edge.points.size() < 4) continue;
            zima::kernel::Vec3 center;
            const std::size_t count = edge.points.size() > 4
                ? edge.points.size() - 1 : edge.points.size();
            for (std::size_t index = 0; index < count; ++index) {
                center.x += edge.points[index].x;
                center.y += edge.points[index].y;
                center.z += edge.points[index].z;
            }
            center = {center.x / count, center.y / count, center.z / count};
            const auto base = static_cast<std::uint32_t>(target.vertices.size());
            for (std::size_t index = 0; index < 4; ++index) {
                const auto& point = edge.points[index];
                target.vertices.push_back({
                    center.x + (point.x - center.x) * scale,
                    center.y + (point.y - center.y) * scale,
                    center.z + (point.z - center.z) * scale});
            }
            target.triangles.insert(target.triangles.end(),
                {base, base + 1, base + 2, base, base + 2, base + 3});
            const zima::kernel::FaceReference reference{
                edge.reference.owner_id,
                edge.reference.semantic_key == "border"
                    ? std::string("plane") : edge.reference.semantic_key,
                edge.reference.instance_path};
            target.triangle_references.push_back(reference);
            target.triangle_references.push_back(reference);
        }
    }

    [[nodiscard]] QMatrix4x4 view() const {
        QMatrix4x4 result;
        const float units_per_pixel = 2.0F * view_scale /
            std::max(1, viewport_height);
        result.translate(static_cast<float>(pan_pixels.x()) * units_per_pixel,
                         static_cast<float>(-pan_pixels.y()) * units_per_pixel, 0.0F);
        result.rotate(orientation);
        result.translate(-center);
        return result;
    }

    int viewport_height{1};

    [[nodiscard]] QMatrix4x4 projection(int width, int height) const {
        const float aspect = height > 0 ? static_cast<float>(width) / height : 1.0F;
        QMatrix4x4 result;
        const float vertical = std::max(view_scale, 0.001F);
        result.ortho(-vertical * aspect, vertical * aspect, -vertical, vertical,
                     -std::max(radius * 20.0F, 100.0F),
                     std::max(radius * 20.0F, 100.0F));
        return result;
    }
};

MeshView::MeshView(QWidget* parent)
    : QOpenGLWidget(parent), impl_(std::make_unique<Impl>()) {
    setMinimumSize(500, 360);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

MeshView::~MeshView() {
    if (context() != nullptr) {
        makeCurrent();
        impl_->vertex_array.destroy();
        impl_->vertices.destroy();
        impl_->triangles.destroy();
        impl_->lines.destroy();
        impl_->silhouette.destroy();
        doneCurrent();
    }
}

void MeshView::set_mesh(zima::kernel::ViewerMesh mesh, bool fit_view) {
    impl_->mesh = std::move(mesh);
    impl_->candidates.clear();
    impl_->confirmed_candidate.reset();
    impl_->selected_container_origin_id.clear();
    impl_->gpu_dirty = true;
    if (fit_view) fit_all();
    impl_->rebuild_persisted_reference_mesh();
    update();
}

void MeshView::set_dimension_decimal_places(int decimal_places) {
    impl_->dimension_decimal_places = std::clamp(decimal_places, 0, 12);
    update();
}

int MeshView::dimension_decimal_places() const {
    return impl_->dimension_decimal_places;
}

std::array<float, 8> MeshView::camera_state() const {
    return {impl_->orientation.scalar(), impl_->orientation.x(),
            impl_->orientation.y(), impl_->orientation.z(), impl_->view_scale,
            static_cast<float>(impl_->pan_pixels.x()),
            static_cast<float>(impl_->pan_pixels.y()),
            impl_->reference_view_scale};
}

void MeshView::set_camera_state(const std::array<float, 8>& state) {
    if (!std::all_of(state.begin(), state.end(), [](float value) {
            return std::isfinite(value);
        }) || state[4] <= 0.0F) return;
    impl_->orientation = QQuaternion(state[0], state[1], state[2], state[3]).normalized();
    impl_->view_scale = state[4];
    impl_->pan_pixels = QPointF(state[5], state[6]);
    // reference_view_scale must be restored alongside view_scale so the
    // screen-constant origin axis/plane ratio (view_scale /
    // reference_view_scale) stays 1.0 across saves/undo/tab switches, not
    // just right after fit_all(). A missing/zero stored value falls back to
    // view_scale itself (equivalent to "just fit").
    impl_->reference_view_scale = state[7] > 0.0F ? state[7] : impl_->view_scale;
    impl_->reference_view_scale_initialized = true;
    impl_->candidates.clear();
    impl_->rebuild_persisted_reference_mesh();
    update();
}

void MeshView::set_selection_contract(std::vector<CandidateKind> allowed_kinds) {
    impl_->allowed_kinds = std::move(allowed_kinds);
    impl_->candidate_filter = {};
    impl_->candidates.clear();
    impl_->active_candidate = 0;
    impl_->confirmed_candidate.reset();
    impl_->selected_container_origin_id.clear();
    update();
}

void MeshView::set_active_sketch_owner(std::string owner_id) {
    if (impl_->active_sketch_owner_id == owner_id) return;
    impl_->active_sketch_owner_id = std::move(owner_id);
    impl_->candidates.clear();
    impl_->active_candidate = 0;
    impl_->confirmed_candidate.reset();
    update();
}

void MeshView::set_candidate_filter(
    std::function<bool(const ViewerCandidate&)> candidate_filter) {
    impl_->candidate_filter = std::move(candidate_filter);
    impl_->candidates.clear();
    impl_->active_candidate = 0;
    impl_->confirmed_candidate.reset();
    impl_->selected_container_origin_id.clear();
    update();
}

void MeshView::confirm_result_body() {
    if (impl_->mesh.triangles.empty()) {
        clear_selection();
        return;
    }
    // An empty-path Occurrence is never produced by ordinary hover.  It is a
    // deliberate Tree-only presentation token meaning "all displayed result
    // faces of this Part", while persisted leaf Containers remain the only
    // candidates offered under the pointer.
    impl_->candidates.clear();
    impl_->active_candidate = 0;
    impl_->confirmed_candidate = ViewerCandidate{
        CandidateKind::Occurrence, 0.0, 0, {}, {}, {},
        CandidateGeometry::Display};
    update();
}

std::optional<ViewerCandidate> MeshView::confirmed_candidate() const {
    return impl_->confirmed_candidate;
}

std::optional<ViewerCandidate> MeshView::hovered_candidate() const {
    if (impl_->confirmed_candidate || impl_->candidates.empty() ||
        impl_->active_candidate >= impl_->candidates.size()) return std::nullopt;
    return impl_->candidates[impl_->active_candidate];
}

std::vector<ViewerCandidate> MeshView::selection_candidates_at(
    const QPointF& position) const {
    if (width() <= 0 || height() <= 0) return {};
    const auto ray = ray_at(position);
    if (!ray) return {};
    const auto& [ray_origin, ray_direction] = *ray;
    // Python parity: point/topology picking uses a 9 px radius scaled by the
    // Wayland device pixel ratio. Keep this expressed through the camera's
    // world-per-pixel conversion so the visible marker and its hit area stay
    // consistent at every zoom and DPI.
    const double world_tolerance =
        world_tolerance_for_pixels(9.0 * devicePixelRatioF());
    auto candidates = ordered_viewer_candidates(
        impl_->mesh, impl_->persisted_reference_mesh,
        ray_origin, ray_direction, world_tolerance);

    // Dimensions are painted as a screen-space annotation. Their text,
    // witness lines and leaders therefore cannot be picked reliably by the
    // world-space ray test alone (and the text was not represented there at
    // all). Add those same visible parts to this one common candidate list,
    // which is subsequently consumed unchanged by hover, LMB, RMB and double
    // click.
    const QMatrix4x4 mvp = impl_->projection(width(), height()) * impl_->view();
    const auto project = [&](const zima::kernel::Vec3& point) {
        QVector4D clip = mvp * QVector4D(point.x, point.y, point.z, 1.0F);
        if (std::abs(clip.w()) > 1.0e-9F) clip /= clip.w();
        return QPointF((clip.x() + 1.0F) * width() / 2.0F,
            (1.0F - clip.y()) * height() / 2.0F);
    };
    const QFontMetricsF metrics(font());
    constexpr double hit_radius = 9.0;
    for (std::size_t index = 0; index < impl_->mesh.dimensions.size(); ++index) {
        const auto& dimension = impl_->mesh.dimensions[index];
        const QPointF witness_first = project(dimension.witness_first);
        const QPointF witness_second = project(dimension.witness_second);
        const QPointF line_first = project(dimension.line_first);
        const QPointF line_second = project(dimension.line_second);
        const QString text = QString::fromStdString(dimension.label_prefix) +
            QString::number(dimension.value, 'f',
                impl_->dimension_decimal_places) +
            QString::fromStdString(dimension.unit_suffix);
        QPointF text_anchor = line_second + QPointF(12.0, 0.0);
        std::optional<LinearDimensionLayout> linear_layout;
        if (dimension.kind == zima::kernel::ViewerDimensionKind::Linear) {
            linear_layout = linear_dimension_layout(
                dimension, impl_->mesh, project);
            text_anchor = linear_layout->text_baseline;
        } else if (dimension.kind == zima::kernel::ViewerDimensionKind::Radius ||
                   dimension.kind == zima::kernel::ViewerDimensionKind::Diameter) {
            QPointF outward = witness_second - witness_first;
            const double length = std::hypot(outward.x(), outward.y());
            if (length > 1.0e-6) {
                outward /= length;
                const QPointF tail = witness_second + outward * 17.0;
                const double side = outward.x() >= 0.0 ? 1.0 : -1.0;
                text_anchor = tail + QPointF(side * 38.0, 5.0);
                if (side < 0.0) text_anchor.rx() -= metrics.horizontalAdvance(text);
            }
        } else if (dimension.kind == zima::kernel::ViewerDimensionKind::Angular) {
            const QPointF first_vector = line_first - witness_first;
            const QPointF second_vector = line_second - witness_first;
            const double radius = std::max(22.0,
                std::hypot(first_vector.x(), first_vector.y()));
            const double start = std::atan2(first_vector.y(), first_vector.x());
            double sweep = std::abs(dimension.sweep_degrees) *
                std::numbers::pi / 180.0;
            if (first_vector.x() * second_vector.y() -
                    first_vector.y() * second_vector.x() < 0.0) sweep = -sweep;
            const double middle = start + sweep * 0.5;
            text_anchor = witness_first + QPointF(
                (radius + 12.0) * std::cos(middle),
                (radius + 12.0) * std::sin(middle));
        }
        QRectF text_bounds = metrics.boundingRect(text);
        text_bounds.moveTopLeft(text_anchor + QPointF(0.0, -metrics.ascent()));
        text_bounds.adjust(-hit_radius, -hit_radius, hit_radius, hit_radius);
        bool hit = text_bounds.contains(position) ||
            screen_segment_distance(position, witness_first, line_first) <= hit_radius ||
            screen_segment_distance(position, witness_second, line_second) <= hit_radius ||
            screen_segment_distance(position, line_first, line_second) <= hit_radius;
        if (linear_layout) {
            hit = hit ||
                screen_segment_distance(position,
                    linear_layout->line_first - linear_layout->along * 10.0,
                    linear_layout->first_tail) <= hit_radius ||
                screen_segment_distance(position,
                    linear_layout->line_second + linear_layout->along * 10.0,
                    linear_layout->second_tail) <= hit_radius ||
                screen_segment_distance(position, linear_layout->leader_start,
                    linear_layout->leader_end) <= hit_radius;
        }
        if (!hit) continue;
        std::erase_if(candidates, [index](const auto& candidate) {
            return candidate.kind == CandidateKind::Dimension &&
                candidate.geometry_index == index;
        });
        candidates.insert(candidates.begin(), ViewerCandidate{
            CandidateKind::Dimension, 0.0, index,
            dimension.reference.owner_id, dimension.reference.semantic_key,
            dimension.reference.instance_path});
    }
    if (!impl_->active_sketch_owner_id.empty()) {
        const auto sketch_owned_kind = [](CandidateKind kind) {
            return kind == CandidateKind::SketchPoint ||
                kind == CandidateKind::SketchSegment ||
                kind == CandidateKind::SketchCurve ||
                kind == CandidateKind::SketchAxis ||
                kind == CandidateKind::SketchConstraint ||
                kind == CandidateKind::SketchText ||
                kind == CandidateKind::SketchExternalReference ||
                kind == CandidateKind::SketchTrimPiece ||
                kind == CandidateKind::Dimension;
        };
        std::erase_if(candidates, [&](const auto& candidate) {
            return sketch_owned_kind(candidate.kind) &&
                candidate.owner_id != impl_->active_sketch_owner_id;
        });
    }
    return filter_candidates(candidates,
        impl_->allowed_kinds, impl_->candidate_filter);
}

std::optional<zima::kernel::ViewerEdge> MeshView::candidate_edge(
    const ViewerCandidate& candidate) const {
    const auto& edges = candidate.geometry == CandidateGeometry::OriginalReference
        ? impl_->mesh.original_references.edges : impl_->mesh.edges;
    if (candidate.geometry_index >= edges.size()) return std::nullopt;
    const auto& edge = edges[candidate.geometry_index];
    if (edge.reference.owner_id != candidate.owner_id ||
        edge.reference.semantic_key != candidate.semantic_key ||
        edge.reference.instance_path != candidate.instance_path) {
        return std::nullopt;
    }
    return edge;
}

std::optional<zima::kernel::ViewerPoint> MeshView::candidate_point(
    const ViewerCandidate& candidate) const {
    const auto& points = candidate.geometry == CandidateGeometry::OriginalReference
        ? impl_->mesh.original_references.points : impl_->mesh.points;
    if (candidate.geometry_index >= points.size()) return std::nullopt;
    const auto& point = points[candidate.geometry_index];
    if (point.reference.owner_id != candidate.owner_id ||
        point.reference.semantic_key != candidate.semantic_key ||
        point.reference.instance_path != candidate.instance_path) {
        return std::nullopt;
    }
    return point;
}

std::optional<zima::kernel::ViewerAxis> MeshView::candidate_axis(
    const ViewerCandidate& candidate) const {
    const auto& axes = candidate.geometry == CandidateGeometry::OriginalReference
        ? impl_->mesh.original_references.axes : impl_->mesh.axes;
    if (candidate.geometry_index >= axes.size()) return std::nullopt;
    const auto& axis = axes[candidate.geometry_index];
    if (axis.reference.owner_id != candidate.owner_id ||
        axis.reference.semantic_key != candidate.semantic_key ||
        axis.reference.instance_path != candidate.instance_path) {
        return std::nullopt;
    }
    return axis;
}

std::vector<zima::kernel::EdgeReference> MeshView::tangent_edge_route(
    const ViewerCandidate& seed, double angular_tolerance_degrees) const {
    if (seed.kind != CandidateKind::Edge) return {};
    const auto& edges = seed.geometry == CandidateGeometry::OriginalReference
        ? impl_->mesh.original_references.edges : impl_->mesh.edges;
    struct Endpoint { std::size_t edge{}; int end{}; };
    const double tolerance = std::max(1.0e-6,
        world_tolerance_for_pixels(0.05));
    const auto key = [tolerance](const zima::kernel::Vec3& point) {
        return std::array<long long, 3>{
            std::llround(point.x / tolerance),
            std::llround(point.y / tolerance),
            std::llround(point.z / tolerance)};
    };
    std::map<std::array<long long, 3>, std::vector<Endpoint>> endpoints;
    std::optional<std::size_t> seed_index;
    for (std::size_t index = 0; index < edges.size(); ++index) {
        const auto& edge = edges[index];
        if (edge.reference.owner_id != seed.owner_id ||
            edge.reference.instance_path != seed.instance_path ||
            edge.points.size() < 2) continue;
        endpoints[key(edge.points.front())].push_back({index, 0});
        endpoints[key(edge.points.back())].push_back({index, 1});
        if (edge.reference.semantic_key == seed.semantic_key) seed_index = index;
    }
    if (!seed_index) return {};
    const auto direction = [&](std::size_t index, int end)
        -> std::optional<zima::kernel::Vec3> {
        const auto& points = edges[index].points;
        const auto& origin = end == 0 ? points.front() : points.back();
        for (std::size_t step = 1; step < points.size(); ++step) {
            const auto& target = end == 0 ? points[step]
                                         : points[points.size() - 1 - step];
            zima::kernel::Vec3 value{target.x - origin.x,
                target.y - origin.y, target.z - origin.z};
            const double length = std::hypot(std::hypot(value.x, value.y), value.z);
            if (length > tolerance) return zima::kernel::Vec3{
                value.x / length, value.y / length, value.z / length};
        }
        return std::nullopt;
    };
    const double limit = std::cos(angular_tolerance_degrees *
        std::numbers::pi / 180.0);
    std::set<std::size_t> selected{*seed_index};
    std::vector<std::size_t> pending{*seed_index};
    while (!pending.empty()) {
        const auto current = pending.back(); pending.pop_back();
        for (int end = 0; end < 2; ++end) {
            const auto current_direction = direction(current, end);
            if (!current_direction) continue;
            const auto& point = end == 0 ? edges[current].points.front()
                                         : edges[current].points.back();
            std::vector<std::size_t> continuations;
            for (const auto candidate : endpoints[key(point)]) {
                if (candidate.edge == current || selected.contains(candidate.edge)) continue;
                const auto candidate_direction = direction(candidate.edge, candidate.end);
                if (!candidate_direction) continue;
                const double alignment = std::abs(
                    current_direction->x * candidate_direction->x +
                    current_direction->y * candidate_direction->y +
                    current_direction->z * candidate_direction->z);
                if (alignment >= limit) continuations.push_back(candidate.edge);
            }
            std::sort(continuations.begin(), continuations.end());
            continuations.erase(std::unique(continuations.begin(), continuations.end()),
                                continuations.end());
            if (continuations.size() != 1) continue;
            selected.insert(continuations.front());
            pending.push_back(continuations.front());
        }
    }
    std::vector<zima::kernel::EdgeReference> result;
    result.reserve(selected.size());
    for (const auto index : selected) result.push_back(edges[index].reference);
    return result;
}

std::optional<zima::kernel::Vec3> MeshView::candidate_face_normal(
    const ViewerCandidate& candidate) const {
    if (candidate.kind != CandidateKind::Face) return std::nullopt;
    // NOTE: a Plane's Face candidate (owner "border"/"origin:plane:<key>",
    // see ordered_viewer_candidates) carries a *border-edge* index in
    // geometry_index, not a triangle index -- it is picked through its
    // rectangular outline, never its filled interior. This helper only
    // supports true triangle-mesh Face candidates; do not call it for a
    // Plane pick. It self-checks the owner/semantic/instance match below, so
    // an accidental call safely returns nullopt in the overwhelming case,
    // but callers must not rely on that.
    const auto resolve = [&](const auto& mesh) -> std::optional<zima::kernel::Vec3> {
        const std::size_t triangle = candidate.geometry_index;
        if (triangle * 3 + 2 >= mesh.triangles.size() ||
            triangle >= mesh.triangle_references.size()) return std::nullopt;
        const auto& reference = mesh.triangle_references[triangle];
        if (reference.owner_id != candidate.owner_id ||
            reference.semantic_key != candidate.semantic_key ||
            reference.instance_path != candidate.instance_path) {
            return std::nullopt;
        }
        const auto first = mesh.triangles[triangle * 3];
        const auto second = mesh.triangles[triangle * 3 + 1];
        const auto third = mesh.triangles[triangle * 3 + 2];
        if (first >= mesh.vertices.size() || second >= mesh.vertices.size() ||
            third >= mesh.vertices.size()) return std::nullopt;
        const auto& a = mesh.vertices[first];
        const auto& b = mesh.vertices[second];
        const auto& c = mesh.vertices[third];
        const zima::kernel::Vec3 edge_one{b.x - a.x, b.y - a.y, b.z - a.z};
        const zima::kernel::Vec3 edge_two{c.x - a.x, c.y - a.y, c.z - a.z};
        zima::kernel::Vec3 normal{
            edge_one.y * edge_two.z - edge_one.z * edge_two.y,
            edge_one.z * edge_two.x - edge_one.x * edge_two.z,
            edge_one.x * edge_two.y - edge_one.y * edge_two.x};
        const double length = std::sqrt(normal.x * normal.x + normal.y * normal.y +
            normal.z * normal.z);
        if (!std::isfinite(length) || length <= 1.0e-12) return std::nullopt;
        normal.x /= length; normal.y /= length; normal.z /= length;
        return normal;
    };
    return candidate.geometry == CandidateGeometry::OriginalReference
        ? resolve(impl_->mesh.original_references) : resolve(impl_->mesh);
}

void MeshView::confirm_container(const std::string& owner_id) {
    auto candidate = container_candidate(impl_->mesh, owner_id);
    if (!candidate) {
        clear_selection();
        return;
    }
    impl_->confirmed_candidate = std::move(candidate);
    // "point"/"axis"/"plane" are the three container kinds whose own
    // defining-point marker lives at `owner_id + ":origin"` -- see
    // construction_viewer_mesh's Point/Axis/Plane marker pushes.
    impl_->selected_container_origin_id =
        (impl_->confirmed_candidate->semantic_key == "point" ||
         impl_->confirmed_candidate->semantic_key == "axis" ||
         impl_->confirmed_candidate->semantic_key == "plane")
        ? owner_id + ":origin" : std::string{};
    impl_->candidates.clear();
    update();
}

void MeshView::confirm_occurrence(const std::string& instance_path) {
    auto candidate = occurrence_candidate(impl_->mesh, instance_path);
    if (!candidate) {
        clear_selection();
        return;
    }
    impl_->confirmed_candidate = std::move(candidate);
    impl_->selected_container_origin_id.clear();
    impl_->candidates.clear();
    update();
}

void MeshView::confirm_origin(const std::string& owner_id,
    const std::string& instance_path) {
    impl_->confirmed_candidate = ViewerCandidate{CandidateKind::Container, 0.0, 0,
        owner_id, "origin", instance_path, CandidateGeometry::Display};
    impl_->selected_container_origin_id = owner_id;
    impl_->candidates.clear();
    update();
}

void MeshView::confirm_reference(const std::string& owner_id,
    const std::string& semantic_key, const std::string& instance_path,
    CandidateKind kind) {
    ViewerCandidate candidate{kind, 0.0, 0, owner_id, semantic_key,
        instance_path, CandidateGeometry::Display};
    if (kind == CandidateKind::Vertex || kind == CandidateKind::SketchPoint ||
        (kind == CandidateKind::SketchExternalReference &&
         semantic_key.starts_with("external_point:"))) {
        auto found = std::find_if(impl_->mesh.points.begin(),
            impl_->mesh.points.end(), [&](const auto& value) {
                return value.reference.owner_id == owner_id &&
                    value.reference.semantic_key == semantic_key &&
                    value.reference.instance_path == instance_path;
            });
        if (found == impl_->mesh.points.end() && semantic_key == "origin:point") {
            found = std::find_if(impl_->mesh.points.begin(),
                impl_->mesh.points.end(), [&](const auto& value) {
                    return value.reference.owner_id == owner_id &&
                        value.reference.semantic_key == "point" &&
                        value.reference.instance_path == instance_path;
                });
            if (found != impl_->mesh.points.end()) {
                candidate.semantic_key = "point";
            }
        }
        if (found == impl_->mesh.points.end()) return clear_selection();
        candidate.geometry_index = static_cast<std::size_t>(
            std::distance(impl_->mesh.points.begin(), found));
    } else if (kind == CandidateKind::SketchSegment ||
               kind == CandidateKind::SketchCurve ||
               kind == CandidateKind::SketchText ||
               kind == CandidateKind::SketchExternalReference) {
        const auto found = std::find_if(impl_->mesh.edges.begin(),
            impl_->mesh.edges.end(), [&](const auto& value) {
                return value.reference.owner_id == owner_id &&
                    (value.reference.semantic_key == semantic_key ||
                     value.reference.semantic_key.starts_with(semantic_key + ":")) &&
                    value.reference.instance_path == instance_path;
            });
        if (found == impl_->mesh.edges.end()) return clear_selection();
        candidate.semantic_key = found->reference.semantic_key;
        candidate.geometry_index = static_cast<std::size_t>(
            std::distance(impl_->mesh.edges.begin(), found));
    } else if (kind == CandidateKind::Axis) {
        const auto found = std::find_if(impl_->mesh.axes.begin(),
            impl_->mesh.axes.end(), [&](const auto& value) {
                return value.reference.owner_id == owner_id &&
                    value.reference.semantic_key == semantic_key &&
                    value.reference.instance_path == instance_path;
            });
        if (found == impl_->mesh.axes.end()) return clear_selection();
        candidate.geometry_index = static_cast<std::size_t>(
            std::distance(impl_->mesh.axes.begin(), found));
    } else if (kind == CandidateKind::Dimension) {
        const auto found = std::find_if(impl_->mesh.dimensions.begin(),
            impl_->mesh.dimensions.end(), [&](const auto& value) {
                return value.reference.owner_id == owner_id &&
                    value.reference.semantic_key == semantic_key &&
                    value.reference.instance_path == instance_path;
            });
        if (found == impl_->mesh.dimensions.end()) return clear_selection();
        candidate.geometry_index = static_cast<std::size_t>(
            std::distance(impl_->mesh.dimensions.begin(), found));
    } else if (kind == CandidateKind::SketchConstraint) {
        const auto found = std::find_if(impl_->mesh.constraint_markers.begin(),
            impl_->mesh.constraint_markers.end(), [&](const auto& value) {
                return value.reference.owner_id == owner_id &&
                    value.reference.semantic_key == semantic_key &&
                    value.reference.instance_path == instance_path;
            });
        if (found == impl_->mesh.constraint_markers.end()) return clear_selection();
        candidate.geometry_index = static_cast<std::size_t>(
            std::distance(impl_->mesh.constraint_markers.begin(), found));
    } else if (kind == CandidateKind::Plane || kind == CandidateKind::Face) {
        const auto& references = impl_->mesh.original_references.triangle_references;
        const auto found = std::find_if(references.begin(), references.end(),
            [&](const auto& value) {
                return value.owner_id == owner_id &&
                    value.semantic_key == semantic_key &&
                    value.instance_path == instance_path;
            });
        if (found == references.end()) return clear_selection();
        candidate.geometry = CandidateGeometry::OriginalReference;
        candidate.geometry_index = static_cast<std::size_t>(
            std::distance(references.begin(), found));
    } else {
        return clear_selection();
    }
    impl_->confirmed_candidate = std::move(candidate);
    impl_->selected_container_origin_id.clear();
    impl_->candidates.clear();
    impl_->active_candidate = 0;
    update();
}

void MeshView::clear_selection() {
    impl_->confirmed_candidate.reset();
    impl_->selected_container_origin_id.clear();
    impl_->candidates.clear();
    impl_->active_candidate = 0;
    impl_->sketch_box_selected_candidates.clear();
    update();
}

void MeshView::set_confirmation_callback(
    std::function<void(const ViewerCandidate&)> callback) {
    impl_->confirmation_callback = std::move(callback);
}

void MeshView::set_empty_confirmation_callback(std::function<void()> callback) {
    impl_->empty_confirmation_callback = std::move(callback);
}

void MeshView::set_context_menu_callback(
    std::function<void(const ViewerCandidate&, const QPoint&)> callback) {
    impl_->context_menu_callback = std::move(callback);
}

void MeshView::set_world_click_callback(std::function<bool(
    const zima::kernel::Vec3&, const zima::kernel::Vec3&)> callback) {
    impl_->world_click_callback = std::move(callback);
}

void MeshView::set_world_pointer_callback(std::function<void(
    const zima::kernel::Vec3&, const zima::kernel::Vec3&)> callback) {
    impl_->world_pointer_callback = std::move(callback);
}

void MeshView::set_command_gesture_callbacks(
    std::function<bool(
        const std::optional<ViewerCandidate>&,
        const zima::kernel::Vec3&, const zima::kernel::Vec3&)> begin,
    std::function<void(
        const zima::kernel::Vec3&, const zima::kernel::Vec3&)> update,
    std::function<void()> end) {
    impl_->command_gesture_begin_callback = std::move(begin);
    impl_->command_gesture_update_callback = std::move(update);
    impl_->command_gesture_end_callback = std::move(end);
}

void MeshView::set_short_middle_click_callback(std::function<bool()> callback) {
    impl_->short_middle_click_callback = std::move(callback);
}

void MeshView::set_double_middle_click_callback(std::function<bool()> callback) {
    impl_->double_middle_click_callback = std::move(callback);
}

void MeshView::set_sketch_box_selection(bool enabled,
    std::function<void(std::vector<ViewerCandidate>, bool)> callback) {
    impl_->sketch_box_selection_enabled = enabled;
    impl_->sketch_box_selection_callback = std::move(callback);
    if (!enabled) {
        impl_->sketch_box_start.reset();
        impl_->sketch_box_end.reset();
    }
    update();
}

bool MeshView::confirm_current_pointer() {
    bool handled = impl_->short_middle_click_callback &&
        impl_->short_middle_click_callback();
    if (!handled && impl_->world_click_callback) {
        const auto ray = ray_at(impl_->last_pointer);
        handled = ray && impl_->world_click_callback(ray->first, ray->second);
    }
    if (handled) {
        impl_->candidates.clear();
        update();
    }
    return handled;
}

bool MeshView::refresh_current_pointer_preview() {
    if (!impl_->world_pointer_callback) return false;
    const auto ray = ray_at(impl_->last_pointer);
    if (!ray) return false;
    impl_->world_pointer_callback(ray->first, ray->second);
    return true;
}

QPoint MeshView::last_pointer_position() const { return impl_->last_pointer; }

std::optional<double> MeshView::candidate_dimension_value(
    const ViewerCandidate& candidate) const {
    if (candidate.kind != CandidateKind::Dimension ||
        candidate.geometry_index >= impl_->mesh.dimensions.size()) return std::nullopt;
    const auto& dimension = impl_->mesh.dimensions[candidate.geometry_index];
    if (dimension.reference.owner_id != candidate.owner_id ||
        dimension.reference.semantic_key != candidate.semantic_key) return std::nullopt;
    return dimension.value;
}

std::optional<QPoint> MeshView::candidate_dimension_label_position(
    const ViewerCandidate& candidate) const {
    if (candidate.kind != CandidateKind::Dimension ||
        candidate.geometry_index >= impl_->mesh.dimensions.size() ||
        width() <= 0 || height() <= 0) return std::nullopt;
    const auto& dimension = impl_->mesh.dimensions[candidate.geometry_index];
    if (dimension.reference.owner_id != candidate.owner_id ||
        dimension.reference.semantic_key != candidate.semantic_key) return std::nullopt;
    const QMatrix4x4 mvp = impl_->projection(width(), height()) * impl_->view();
    const auto project = [&](const zima::kernel::Vec3& point) {
        QVector4D clip = mvp * QVector4D(point.x, point.y, point.z, 1.0F);
        if (std::abs(clip.w()) > 1.0e-9F) clip /= clip.w();
        return QPointF((clip.x() + 1.0F) * width() / 2.0F,
            (1.0F - clip.y()) * height() / 2.0F);
    };
    const QString text = QString::fromStdString(dimension.label_prefix) +
        QString::number(dimension.value, 'f',
                impl_->dimension_decimal_places) +
        QString::fromStdString(dimension.unit_suffix);
    const QFontMetricsF metrics(font());
    QPointF baseline;
    if (dimension.kind == zima::kernel::ViewerDimensionKind::Angular) {
        const QPointF vertex = project(dimension.witness_first);
        const QPointF first = project(dimension.line_first);
        const QPointF second = project(dimension.line_second);
        const QPointF first_vector = first - vertex;
        const QPointF second_vector = second - vertex;
        const double radius = std::max(22.0,
            std::hypot(first_vector.x(), first_vector.y()));
        const double start = std::atan2(first_vector.y(), first_vector.x());
        double sweep = std::abs(dimension.sweep_degrees) *
            std::numbers::pi / 180.0;
        if (first_vector.x() * second_vector.y() -
                first_vector.y() * second_vector.x() < 0.0) sweep = -sweep;
        const double middle = start + sweep * 0.5;
        baseline = vertex + QPointF((radius + 12.0) * std::cos(middle),
                                     (radius + 12.0) * std::sin(middle));
    } else if (dimension.kind == zima::kernel::ViewerDimensionKind::Radius ||
               dimension.kind == zima::kernel::ViewerDimensionKind::Diameter) {
        const QPointF center = project(dimension.witness_first);
        const QPointF rim = project(dimension.witness_second);
        QPointF outward = rim - center;
        const double length = std::hypot(outward.x(), outward.y());
        if (length <= 1.0e-9) return std::nullopt;
        outward /= length;
        const QPointF tail = rim + outward * 17.0;
        const double side = outward.x() >= 0.0 ? 1.0 : -1.0;
        const QPointF shoulder = tail + QPointF(side * 36.0, 0.0);
        baseline = shoulder + QPointF(
            side > 0.0 ? 2.0 : -metrics.horizontalAdvance(text) - 2.0, 5.0);
    } else {
        baseline = linear_dimension_layout(
            dimension, impl_->mesh, project).text_baseline;
    }
    return QPointF(baseline.x() + metrics.horizontalAdvance(text) * 0.5,
                   baseline.y() - (metrics.ascent() - metrics.descent()) * 0.5)
        .toPoint();
}
void MeshView::set_empty_right_click_callback(std::function<bool()> callback) {
    impl_->empty_right_click_callback = std::move(callback);
}

void MeshView::set_single_candidate_right_click_callback(
    std::function<bool(const ViewerCandidate&)> callback) {
    impl_->single_candidate_right_click_callback = std::move(callback);
}

void MeshView::set_candidate_right_click_callback(
    std::function<bool(const ViewerCandidate&, std::size_t, std::size_t)> callback) {
    impl_->candidate_right_click_callback = std::move(callback);
}

void MeshView::reset_candidate_cycle() {
    impl_->active_candidate = 0;
    update();
}

void MeshView::set_double_confirmation_callback(
    std::function<void(const ViewerCandidate&)> callback) {
    impl_->double_confirmation_callback = std::move(callback);
}

void MeshView::set_candidate_drag_callbacks(
    std::function<bool(const ViewerCandidate&, const zima::kernel::Vec3&,
        const zima::kernel::Vec3&)> begin,
    std::function<void(const zima::kernel::Vec3&, const zima::kernel::Vec3&)> update,
    std::function<void()> end) {
    impl_->drag_begin_callback = std::move(begin);
    impl_->drag_update_callback = std::move(update);
    impl_->drag_end_callback = std::move(end);
}

void MeshView::set_transient_edges(std::vector<zima::kernel::ViewerEdge> edges) {
    if (impl_->transient_point_transform) {
        for (auto& edge : edges) {
            for (auto& point : edge.points) {
                point = impl_->transient_point_transform(point);
            }
        }
    }
    impl_->transient_edges = std::move(edges);
    // Every edge submission starts a fresh preview frame. Callers that need
    // active input markers append them immediately with set_transient_points.
    impl_->transient_points.clear();
    impl_->transient_labels.clear();
    update();
}

void MeshView::set_transient_points(std::vector<zima::kernel::Vec3> points) {
    if (impl_->transient_point_transform) {
        for (auto& point : points) point = impl_->transient_point_transform(point);
    }
    impl_->transient_points = std::move(points);
    update();
}

void MeshView::set_transient_labels(
    std::vector<std::pair<zima::kernel::Vec3, std::string>> labels) {
    if (impl_->transient_point_transform) {
        for (auto& [point, label] : labels) {
            static_cast<void>(label);
            point = impl_->transient_point_transform(point);
        }
    }
    impl_->transient_labels = std::move(labels);
    update();
}

void MeshView::set_sketch_cursor(std::optional<zima::kernel::Vec3> point,
    bool snapped, std::string relation_label) {
    if (point && impl_->transient_point_transform) {
        *point = impl_->transient_point_transform(*point);
    }
    impl_->sketch_cursor = std::move(point);
    impl_->sketch_cursor_snapped = snapped;
    impl_->sketch_cursor_label = std::move(relation_label);
    update();
}

void MeshView::set_extent_manipulator(
    std::optional<ExtentManipulator> manipulator) {
    set_extent_manipulators(manipulator
        ? std::vector<ExtentManipulator>{std::move(*manipulator)}
        : std::vector<ExtentManipulator>{});
}

void MeshView::set_extent_manipulators(
    std::vector<ExtentManipulator> manipulators) {
    for (auto& value : manipulators) {
      if (impl_->transient_point_transform) {
        const auto transformed_origin =
            impl_->transient_point_transform(value.origin);
        const auto transformed_end = impl_->transient_point_transform({
            value.origin.x + value.direction.x,
            value.origin.y + value.direction.y,
            value.origin.z + value.direction.z});
        value.origin = transformed_origin;
        value.direction = {transformed_end.x - transformed_origin.x,
            transformed_end.y - transformed_origin.y,
            transformed_end.z - transformed_origin.z};
      }
    }
    impl_->extent_manipulators = std::move(manipulators);
    if (impl_->extent_manipulators.empty()) {
        impl_->hovered_extent_key.clear();
        if (impl_->dragged_extent_key.empty())
            impl_->dragged_extent_manipulator.reset();
    }
    update();
}

void MeshView::set_operation_direction_indicator(
        std::optional<OperationDirectionIndicator> indicator) {
    set_operation_direction_indicators(indicator
        ? std::vector<OperationDirectionIndicator>{std::move(*indicator)}
        : std::vector<OperationDirectionIndicator>{});
}

void MeshView::set_operation_direction_indicators(
        std::vector<OperationDirectionIndicator> indicators) {
    for (auto& value : indicators) {
      if (impl_->transient_point_transform) {
        const auto transformed_origin =
            impl_->transient_point_transform(value.origin);
        const auto transformed_direction_end =
            impl_->transient_point_transform({
                value.origin.x + value.direction.x,
                value.origin.y + value.direction.y,
                value.origin.z + value.direction.z});
        const auto transformed_radial_end =
            impl_->transient_point_transform({
                value.origin.x + value.radial.x,
                value.origin.y + value.radial.y,
                value.origin.z + value.radial.z});
        value.origin = transformed_origin;
        value.direction = {
            transformed_direction_end.x - transformed_origin.x,
            transformed_direction_end.y - transformed_origin.y,
            transformed_direction_end.z - transformed_origin.z};
        value.radial = {
            transformed_radial_end.x - transformed_origin.x,
            transformed_radial_end.y - transformed_origin.y,
            transformed_radial_end.z - transformed_origin.z};
      }
    }
    impl_->operation_direction_indicators = std::move(indicators);
    update();
}

void MeshView::set_extent_manipulator_callbacks(
    std::function<void(const std::string&)> begin,
    std::function<void(const std::string&, double)> update_callback,
    std::function<void()> end) {
    impl_->extent_begin_callback = std::move(begin);
    impl_->extent_update_callback = std::move(update_callback);
    impl_->extent_end_callback = std::move(end);
}

void MeshView::set_edge_treatment_selection_edges(std::set<EdgeKey> edges) {
    if (edges == impl_->edge_treatment_selection_edges) return;
    impl_->edge_treatment_selection_edges = std::move(edges);
    update();
}

void MeshView::set_feature_hover_edges(std::set<EdgeKey> edges) {
    if (edges == impl_->feature_hover_edges) return;
    impl_->feature_hover_edges = std::move(edges);
    update();
}

void MeshView::set_feature_selected_edges(std::set<EdgeKey> edges) {
    if (edges == impl_->feature_selected_edges) return;
    impl_->feature_selected_edges = std::move(edges);
    update();
}

void MeshView::set_feature_preview_owners(std::set<std::string> owner_ids) {
    if (owner_ids == impl_->feature_preview_owner_ids) return;
    impl_->feature_preview_owner_ids = std::move(owner_ids);
    update();
}

void MeshView::set_constraint_reference_highlights(
    std::set<std::string> owner_ids, std::set<EdgeKey> edges) {
    impl_->constraint_reference_owner_ids = std::move(owner_ids);
    impl_->constraint_reference_edges = std::move(edges);
    update();
}

void MeshView::set_assembly_reference_edges(std::set<EdgeKey> edges) {
    if (edges == impl_->assembly_reference_edges) return;
    impl_->assembly_reference_edges = std::move(edges);
    update();
}

void MeshView::set_selected_container_contents(std::set<std::string> owner_ids) {
    if (owner_ids == impl_->selected_container_content_ids) return;
    impl_->selected_container_content_ids = std::move(owner_ids);
    update();
}

void MeshView::set_object_overlay_main_edges(std::set<EdgeKey> edges) {
    if (edges == impl_->object_overlay_main_edge_keys) return;
    impl_->object_overlay_main_edge_keys = std::move(edges);
    update();
}

void MeshView::set_edge_color_override(std::optional<QColor> color) {
    impl_->edge_color_override = std::move(color);
    update();
}

void MeshView::set_body_surface_colors(
    QColor default_color, std::map<std::string, QColor> instance_colors) {
    impl_->body_surface_color = default_color.isValid()
        ? std::move(default_color) : QColor("#B9C2CC");
    impl_->body_surface_instance_colors = std::move(instance_colors);
    update();
}

void MeshView::set_transient_point_transform(
    std::function<zima::kernel::Vec3(const zima::kernel::Vec3&)> transform) {
    impl_->transient_point_transform = std::move(transform);
    // Changing the document/occurrence coordinate mapping is not a request
    // to destroy the command preview. refresh_scene() re-establishes this
    // mapping while Primitive Properties is open; clearing here made the
    // already calculated cyan Box/Cylinder/... wire disappear immediately,
    // and rollback editing triggered the same loss a second time. Preview
    // lifetime is owned explicitly by set_transient_edges({}) when the
    // dialog closes or its operation changes.
    update();
}

double MeshView::world_tolerance_for_pixels(double pixels) const {
    if (!std::isfinite(pixels) || pixels < 0.0 || height() <= 0) return 0.0;
    return 2.0 * static_cast<double>(impl_->view_scale) * pixels /
        static_cast<double>(height());
}

void MeshView::notify_confirmation() {
    if (impl_->confirmed_candidate && impl_->confirmation_callback) {
        // Confirmation handlers may immediately change the selection contract
        // or rebuild the scene. Both operations clear confirmed_candidate, so
        // never expose a reference into that optional to the callback.
        const ViewerCandidate candidate = *impl_->confirmed_candidate;
        impl_->confirmation_callback(candidate);
    }
}

void MeshView::fit_all() {
    std::vector<zima::kernel::Vec3> bounds = impl_->mesh.vertices;
    std::vector<zima::kernel::Vec3> reference_centers;
    double reference_extent = 0.5;
    for (const auto& edge : impl_->mesh.edges) {
        if (is_screen_constant_plane(edge.reference.semantic_key)) {
            const std::size_t count = edge.points.size() > 1
                ? edge.points.size() - 1 : edge.points.size();
            if (count == 0) continue;
            zima::kernel::Vec3 center;
            for (std::size_t index = 0; index < count; ++index) {
                center.x += edge.points[index].x;
                center.y += edge.points[index].y;
                center.z += edge.points[index].z;
            }
            center = {center.x / count, center.y / count, center.z / count};
            reference_centers.push_back(center);
            for (std::size_t index = 0; index < count; ++index) {
                reference_extent = std::max(reference_extent,
                    std::hypot(std::hypot(edge.points[index].x - center.x,
                                         edge.points[index].y - center.y),
                               edge.points[index].z - center.z));
            }
            continue;
        }
        bounds.insert(bounds.end(), edge.points.begin(), edge.points.end());
    }
    for (const auto& point : impl_->mesh.points) {
        const bool preview_helper =
            point.reference.semantic_key == "preview:plane-offset-point" ||
            impl_->feature_preview_owner_ids.contains(point.reference.owner_id);
        if (point.reference.semantic_key == "origin:point" || preview_helper) {
            reference_centers.push_back(point.position);
        } else {
            bounds.push_back(point.position);
        }
    }
    for (const auto& axis : impl_->mesh.axes) {
        const bool origin = axis.reference.semantic_key.starts_with("origin:axis:");
        if (origin) {
            reference_centers.push_back(axis.point);
            reference_extent = std::max(reference_extent, axis.display_length);
            continue;
        }
        const double first = origin ? 0.0 : -axis.display_length * 0.5;
        const double second = origin ? axis.display_length : axis.display_length * 0.5;
        bounds.push_back({axis.point.x + axis.direction.x * first,
                          axis.point.y + axis.direction.y * first,
                          axis.point.z + axis.direction.z * first});
        bounds.push_back({axis.point.x + axis.direction.x * second,
                          axis.point.y + axis.direction.y * second,
                          axis.point.z + axis.direction.z * second});
    }
    for (const auto& dimension : impl_->mesh.dimensions) {
        bounds.push_back(dimension.witness_first);
        bounds.push_back(dimension.witness_second);
        bounds.push_back(dimension.line_first);
        bounds.push_back(dimension.line_second);
    }
    // Pending primitive bodies are analytical transient wires until OK, so
    // they are intentionally absent from mesh.vertices/mesh.edges. They are
    // nevertheless the primary object the user needs framed while entering
    // Box/Cylinder/etc. parameters.
    for (const auto& edge : impl_->transient_edges) {
        bounds.insert(bounds.end(), edge.points.begin(), edge.points.end());
    }
    if (bounds.empty() && reference_centers.empty()) {
        impl_->center = {};
        impl_->radius = kEmptyDocumentViewHalfExtent;
        impl_->view_scale = kEmptyDocumentViewHalfExtent;
        // This is the degenerate "nothing to frame yet" case (no origin
        // geometry has even been generated into the mesh yet) -- it must
        // NOT lock in reference_view_scale_initialized, otherwise a
        // premature call here (before the real origin-only fit below ever
        // runs) would permanently freeze the screen-constant baseline at an
        // arbitrary 1.4F, unrelated to the origin's actual rendered extent.
        if (!impl_->reference_view_scale_initialized) {
            impl_->reference_view_scale = 1.4F;
        }
        impl_->pan_pixels = {};
        return;
    }
    if (bounds.empty()) {
        // No real body/user geometry yet (e.g. a brand-new empty Part): the
        // scene contains only origin axes/planes/points. Frame the view
        // around their actual rendered extent (reference_extent, tracking
        // axis display_length / plane half-diagonal) rather than around the
        // tiny cluster of near-zero center points in reference_centers,
        // which would otherwise make the fixed-length origin axes appear
        // enormous relative to an overly tight fit radius.
        zima::kernel::Vec3 center{};
        if (!reference_centers.empty()) {
            for (const auto& point : reference_centers) {
                center.x += point.x;
                center.y += point.y;
                center.z += point.z;
            }
            const double count = static_cast<double>(reference_centers.size());
            center = {center.x / count, center.y / count, center.z / count};
        }
        impl_->center = QVector3D(static_cast<float>(center.x),
                                   static_cast<float>(center.y),
                                   static_cast<float>(center.z));
        // Add headroom around the origin's own extent so the axes/planes
        // do not fill the entire viewport edge-to-edge (matches the visual
        // margin a body with real bounds gets from its own surrounding
        // whitespace once panned/zoomed by the user).
        const float reference_frame_scale = static_cast<float>(
            std::max(reference_extent, 0.5) * 2.0);
        impl_->radius = std::max(
            reference_frame_scale, kEmptyDocumentViewHalfExtent);
        impl_->view_scale = impl_->radius;
        // Only establish the screen-constant reference baseline the FIRST
        // time it is ever needed. Once set, it must survive every later
        // fit_all() -- including calls made after real geometry (an Axis,
        // Plane, body...) has been added -- so the Origin (and every
        // container's own origin) keeps an identical apparent size in the
        // View no matter how far the camera has to zoom out to frame newly
        // added, more distant geometry. Resetting it on every fit_all() call
        // (the previous behaviour) made the Origin visibly shrink each time
        // "Zobrazit vše" had to zoom out further than before.
        if (!impl_->reference_view_scale_initialized) {
            // Decouple real-world startup scale from screen-constant datum
            // size: zoom the empty world out to ~100 mm, but keep document
            // and container Origins at their established LCD size.
            impl_->reference_view_scale = reference_frame_scale;
            impl_->reference_view_scale_initialized = true;
        }
        impl_->pan_pixels = {};
        return;
    }
    zima::kernel::Vec3 minimum{
        std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max()};
    zima::kernel::Vec3 maximum{
        std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest()};
    for (const auto& point : bounds) {
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    }
    impl_->center = QVector3D(
        static_cast<float>((minimum.x + maximum.x) / 2.0),
        static_cast<float>((minimum.y + maximum.y) / 2.0),
        static_cast<float>((minimum.z + maximum.z) / 2.0));
    const QVector3D diagonal(
        static_cast<float>(maximum.x - minimum.x),
        static_cast<float>(maximum.y - minimum.y),
        static_cast<float>(maximum.z - minimum.z));
    impl_->radius = std::max(diagonal.length() / 2.0F, 0.5F);
    // A bounding sphere guarantees that every corner fits the orthographic
    // projection, but using its radius verbatim leaves the outermost corner
    // directly on the viewport edge.  Keep a modest frame around newly
    // created geometry so the complete primitive is visible immediately.
    // In a portrait/narrow viewport the horizontal half-extent is
    // view_scale * aspect, therefore compensate for aspect ratios below 1.
    constexpr float fit_margin = 1.20F;
    const float aspect = height() > 0
        ? static_cast<float>(std::max(width(), 1)) /
            static_cast<float>(height())
        : 1.0F;
    const float narrow_view_compensation = 1.0F / std::min(aspect, 1.0F);
    impl_->view_scale = bounds.size() == 1
        ? std::max(impl_->reference_view_scale,
              kEmptyDocumentViewHalfExtent)
        : impl_->radius * fit_margin * narrow_view_compensation;
    // Only establish the screen-constant reference baseline once (see the
    // identical guard/comment in the origin-only branch above). A document
    // that already has an established reference_view_scale (from an earlier
    // fit, or restored from a saved camera state) must keep it unchanged
    // here, otherwise every "Zobrazit vše" after adding new, farther-away
    // geometry would re-baseline the ratio to 1.0 and make the Origin (and
    // every container's own origin) visibly shrink.
    if (!impl_->reference_view_scale_initialized) {
        // A document may be opened for the first time with an already large
        // persisted body. The body controls only the camera fit; it must not
        // establish the LCD size of datum axes/planes. Use the datum's own
        // model extent exactly like the origin-only branch above, so opening
        // a 10 mm and a 10 m Part presents an identical Origin on screen.
        impl_->reference_view_scale = static_cast<float>(
            std::max(reference_extent, 0.5) * 2.0);
        impl_->reference_view_scale_initialized = true;
    }
    impl_->pan_pixels = {};
    update();
}

void MeshView::set_display_mode(DisplayMode mode) {
    impl_->display_mode = mode;
    update();
}

DisplayMode MeshView::display_mode() const { return impl_->display_mode; }

namespace {
// Shared camera transition helper for set_standard_view/set_view_direction,
// matching Python's animate_standard_view/animate_view_normal: slerp the
// orientation and ease pan back to zero over ANIMATION_DURATION_MS with an
// InOutCubic curve instead of a hard jump cut.
constexpr int kAnimationDurationMs = 850;
}  // namespace

void MeshView::animate_orientation_to(const QQuaternion& target) {
    if (impl_->camera_animation != nullptr) {
        impl_->camera_animation->stop();
        impl_->camera_animation = nullptr;
    }
    const QQuaternion start_orientation = impl_->orientation;
    const QPointF start_pan = impl_->pan_pixels;
    auto* animation = new QVariantAnimation(this);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setDuration(kAnimationDurationMs);
    animation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(animation, &QVariantAnimation::valueChanged, this,
        [this, start_orientation, target, start_pan](const QVariant& raw) {
            const float progress = static_cast<float>(raw.toDouble());
            impl_->orientation =
                QQuaternion::slerp(start_orientation, target, progress);
            impl_->pan_pixels = start_pan * (1.0 - progress);
            update();
        });
    connect(animation, &QVariantAnimation::finished, this, [this, animation] {
        if (impl_->camera_animation == animation) impl_->camera_animation = nullptr;
    });
    impl_->camera_animation = animation;
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void MeshView::animate_camera_state(const std::array<float, 8>& state) {
    if (!std::all_of(state.begin(), state.end(), [](float value) {
            return std::isfinite(value);
        }) || state[4] <= 0.0F) return;
    if (impl_->camera_animation != nullptr) {
        impl_->camera_animation->stop();
        impl_->camera_animation = nullptr;
    }
    const QQuaternion start_orientation = impl_->orientation;
    const QPointF start_pan = impl_->pan_pixels;
    const float start_scale = impl_->view_scale;
    const float start_reference_scale = impl_->reference_view_scale;
    const QQuaternion target_orientation =
        QQuaternion(state[0], state[1], state[2], state[3]).normalized();
    const QPointF target_pan(state[5], state[6]);
    const float target_scale = state[4];
    // reference_view_scale must be restored alongside view_scale, otherwise
    // the screen-constant origin axis/plane ratio drifts to a stale value
    // after restoring a saved "Pohledy" view.
    const float target_reference_scale = state[7] > 0.0F ? state[7] : target_scale;
    auto* animation = new QVariantAnimation(this);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setDuration(kAnimationDurationMs);
    animation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(animation, &QVariantAnimation::valueChanged, this,
        [this, start_orientation, target_orientation, start_pan, target_pan,
            start_scale, target_scale, start_reference_scale,
            target_reference_scale](const QVariant& raw) {
            const float progress = static_cast<float>(raw.toDouble());
            impl_->orientation = QQuaternion::slerp(
                start_orientation, target_orientation, progress);
            impl_->pan_pixels = start_pan +
                (target_pan - start_pan) * static_cast<double>(progress);
            impl_->view_scale = start_scale +
                (target_scale - start_scale) * progress;
            impl_->reference_view_scale = start_reference_scale +
                (target_reference_scale - start_reference_scale) * progress;
            impl_->candidates.clear();
            update();
        });
    connect(animation, &QVariantAnimation::finished, this, [this, animation] {
        if (impl_->camera_animation == animation) impl_->camera_animation = nullptr;
        impl_->rebuild_persisted_reference_mesh();
    });
    impl_->camera_animation = animation;
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}


void MeshView::set_standard_view(StandardView view) {
    float yaw{};
    float pitch{};
    switch (view) {
        case StandardView::Isometric:
            yaw = 215.264F; pitch = -45.0F;
            break;
        case StandardView::Front:
            yaw = 180.0F; pitch = -90.0F;
            break;
        case StandardView::Back:
            yaw = 0.0F; pitch = -90.0F;
            break;
        case StandardView::Left:
            yaw = -90.0F; pitch = -90.0F;
            break;
        case StandardView::Right:
            yaw = 90.0F; pitch = -90.0F;
            break;
        case StandardView::Top:
            yaw = 180.0F; pitch = 0.0F;
            break;
        case StandardView::Bottom:
            yaw = 180.0F; pitch = 180.0F;
            break;
    }
    const QQuaternion target =
        QQuaternion::fromAxisAndAngle(1.0F, 0.0F, 0.0F, pitch) *
        QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, yaw);
    impl_->candidates.clear();
    animate_orientation_to(target);
}

void MeshView::set_view_direction(const zima::kernel::Vec3& direction) {
    set_view_direction(direction, 0.0F);
}

void MeshView::set_view_direction(
    const zima::kernel::Vec3& direction, float roll_degrees) {
    const double length = std::sqrt(direction.x * direction.x +
        direction.y * direction.y + direction.z * direction.z);
    if (!std::isfinite(length) || length <= 1.0e-12) {
        throw std::invalid_argument("View direction must be finite and non-zero");
    }
    const double x = direction.x / length;
    const double y = direction.y / length;
    const double z = direction.z / length;
    const double horizontal = std::hypot(x, y);
    const float yaw = horizontal > 1.0e-12
        ? static_cast<float>(std::atan2(x, y) * 180.0 / std::numbers::pi) : 0.0F;
    const float pitch = static_cast<float>(
        std::atan2(-horizontal, -z) * 180.0 / std::numbers::pi);
    const QQuaternion target =
        QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, roll_degrees) *
        QQuaternion::fromAxisAndAngle(1.0F, 0.0F, 0.0F, pitch) *
        QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, yaw);
    impl_->candidates.clear();
    animate_orientation_to(target);
}

void MeshView::set_reference_visibility(
    ReferenceVisibility reference, bool visible) {
    switch (reference) {
        case ReferenceVisibility::Origins: impl_->show_origins = visible; break;
        case ReferenceVisibility::Points: impl_->show_points = visible; break;
        case ReferenceVisibility::Axes: impl_->show_axes = visible; break;
        case ReferenceVisibility::Planes: impl_->show_planes = visible; break;
        case ReferenceVisibility::Sketches: impl_->show_sketches = visible; break;
    }
    update();
}

bool MeshView::reference_visible(ReferenceVisibility reference) const {
    switch (reference) {
        case ReferenceVisibility::Origins: return impl_->show_origins;
        case ReferenceVisibility::Points: return impl_->show_points;
        case ReferenceVisibility::Axes: return impl_->show_axes;
        case ReferenceVisibility::Planes: return impl_->show_planes;
        case ReferenceVisibility::Sketches: return impl_->show_sketches;
    }
    return false;
}

void MeshView::set_editing_origin_visible(bool visible) {
    if (impl_->editing_origin_visible == visible) return;
    impl_->editing_origin_visible = visible;
    update();
}

void MeshView::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    // Viewer meshes are persisted/reference geometry, not an OCCT
    // presentation.  Their winding is not a visibility contract and can be
    // reversed by mirrors or occurrence transforms.  Python consequently
    // renders both sides as well.  Back-face culling here made whole faces
    // disappear from the depth pass, which in turn broke both hidden-line
    // modes and made the visible body disagree with the two-sided picker.
    glDisable(GL_CULL_FACE);
    glClearColor(0.14F, 0.16F, 0.18F, 1.0F);
    impl_->program.addShaderFromSourceCode(QOpenGLShader::Vertex,
        "#version 330 core\n"
        "layout(location=0) in vec3 position;\n"
        "layout(location=1) in vec3 normal;\n"
        "uniform mat4 mvp;\n"
        "uniform mat3 normalMatrix;\n"
        "out vec3 viewNormal;\n"
        "void main(){ viewNormal = normalize(normalMatrix * normal);"
        " gl_Position = mvp * vec4(position, 1.0); }\n");
    impl_->program.addShaderFromSourceCode(QOpenGLShader::Fragment,
        "#version 330 core\n"
        "uniform vec4 color;\n"
        "uniform int unlit;\n"
        "in vec3 viewNormal;\n"
        "out vec4 fragmentColor;\n"
        "void main(){ vec3 lightDirection = normalize(vec3(0.25, -0.35, 0.902));"
        // OCCT face orientation is persisted separately from tessellation
        // winding. Adjacent triangles of one planar ZIMA face may therefore
        // arrive with opposite winding; two-sided rendering must light both
        // identically instead of producing a fake clipping-plane pattern.
        " float diffuse = abs(dot(normalize(viewNormal), lightDirection));"
        " float brightness = unlit != 0 ? 1.0 : 0.42 + 0.58 * diffuse;"
        " fragmentColor = vec4(color.rgb * brightness, color.a); }\n");
    impl_->program.link();
    impl_->vertex_array.create();
    impl_->vertex_array.bind();
    impl_->vertices.create();
    impl_->triangles.create();
    impl_->lines.create();
    impl_->silhouette.create();
    upload_mesh();
    impl_->vertex_array.release();
}

void MeshView::resizeGL(int, int height) {
    impl_->viewport_height = std::max(1, height);
}

void MeshView::upload_mesh() {
    if (!isValid() || !impl_->gpu_dirty) return;
    // Build crease-aware vertex normals.  Corners at the same geometric
    // position are smoothed only when their triangle normals describe the
    // same continuous surface.  This keeps Box edges sharp while Sphere and
    // Cylinder side faces shade continuously instead of exposing every OCCT
    // tessellation triangle.
    using NormalPointKey = std::array<long long, 3>;
    const auto normal_point_key = [](const zima::kernel::Vec3& point) {
        constexpr double scale = 1.0e7;
        return NormalPointKey{std::llround(point.x * scale),
            std::llround(point.y * scale), std::llround(point.z * scale)};
    };
    const std::size_t triangle_count = impl_->mesh.triangles.size() / 3;
    std::vector<QVector3D> triangle_normals(triangle_count);
    std::map<NormalPointKey, std::vector<QVector3D>> normals_at_point;
    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        const auto a = impl_->mesh.triangles[triangle * 3];
        const auto b = impl_->mesh.triangles[triangle * 3 + 1];
        const auto c = impl_->mesh.triangles[triangle * 3 + 2];
        if (a >= impl_->mesh.vertices.size() || b >= impl_->mesh.vertices.size() ||
            c >= impl_->mesh.vertices.size()) continue;
        const auto& pa = impl_->mesh.vertices[a];
        const auto& pb = impl_->mesh.vertices[b];
        const auto& pc = impl_->mesh.vertices[c];
        QVector3D normal = QVector3D::crossProduct(
            QVector3D(pb.x - pa.x, pb.y - pa.y, pb.z - pa.z),
            QVector3D(pc.x - pa.x, pc.y - pa.y, pc.z - pa.z));
        if (normal.lengthSquared() > 1.0e-12F) normal.normalize();
        triangle_normals[triangle] = normal;
        for (const auto index : {a, b, c}) {
            normals_at_point[normal_point_key(impl_->mesh.vertices[index])]
                .push_back(normal);
        }
    }
    std::vector<float> vertex_data;
    vertex_data.reserve(impl_->mesh.triangles.size() * 6);
    constexpr float smooth_crease_cosine = 0.75F;
    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        const std::size_t offset = triangle * 3;
        const auto a = impl_->mesh.triangles[offset];
        const auto b = impl_->mesh.triangles[offset + 1];
        const auto c = impl_->mesh.triangles[offset + 2];
        if (a < impl_->mesh.vertices.size() && b < impl_->mesh.vertices.size() &&
            c < impl_->mesh.vertices.size()) {
            for (const auto index : {a, b, c}) {
                const auto& point = impl_->mesh.vertices[index];
                const QVector3D face_normal = triangle_normals[triangle];
                QVector3D normal;
                for (auto adjacent : normals_at_point[normal_point_key(point)]) {
                    const float alignment = QVector3D::dotProduct(
                        face_normal, adjacent);
                    if (std::abs(alignment) < smooth_crease_cosine) continue;
                    // Mirrored/reversed tessellation is not a geometric
                    // crease. Align its hemisphere before averaging.
                    if (alignment < 0.0F) adjacent = -adjacent;
                    normal += adjacent;
                }
                if (normal.lengthSquared() > 1.0e-12F) normal.normalize();
                else normal = face_normal;
                vertex_data.insert(vertex_data.end(), {
                    static_cast<float>(point.x), static_cast<float>(point.y),
                    static_cast<float>(point.z), normal.x(), normal.y(), normal.z()});
            }
        }
    }
    impl_->vertices.bind();
    impl_->vertices.allocate(vertex_data.data(),
        static_cast<int>(vertex_data.size() * sizeof(float)));
    impl_->triangles.bind();
    impl_->triangles.allocate(impl_->mesh.triangles.data(),
        static_cast<int>(impl_->mesh.triangles.size() * sizeof(std::uint32_t)));

    std::vector<float> line_data;
    impl_->line_ranges.clear();
    impl_->line_edges.clear();
    for (const auto& edge : impl_->mesh.edges) {
        // Datum/work-plane rectangles are rendered later by QPainter after
        // applying the screen-constant reference scale. Uploading their raw
        // model-space border to the GL line pass as well draws a second,
        // differently-sized rectangle that separates from the real one
        // during zoom (the small flickering plane visible in 01.png).
        if (edge.overlay || is_screen_constant_plane(edge.reference.semantic_key) ||
            edge.points.size() < 2) continue;
        const auto first = static_cast<GLint>(line_data.size() / 6);
        const auto count = static_cast<GLsizei>(edge.points.size());
        impl_->line_ranges.emplace_back(first, count);
        impl_->line_edges.push_back(edge);
        for (const auto& point : edge.points) {
            line_data.insert(line_data.end(), {
                static_cast<float>(point.x), static_cast<float>(point.y),
                static_cast<float>(point.z), 0.0F, 0.0F, 1.0F});
        }
    }
    impl_->lines.bind();
    impl_->lines.allocate(line_data.data(),
        static_cast<int>(line_data.size() * sizeof(float)));

    // Candidate silhouette edges: internal triangulation edges shared by
    // exactly two triangles of the same owning face (owner_id + semantic_key)
    // that are not already a persisted topological edge. Mirrors Python's
    // build_silhouette_edges() in zima_cad/viewer_data.py. Rebuilt only when
    // the mesh changes (gpu_dirty); the visible subset is re-selected every
    // frame from the current camera direction in paintGL.
    {
        std::set<std::pair<std::string, std::pair<std::array<double, 3>,
            std::array<double, 3>>>> topology_segments;
        const auto rounded = [](const zima::kernel::Vec3& point) {
            constexpr double scale = 1.0e7;
            return std::array<double, 3>{
                std::round(point.x * scale) / scale,
                std::round(point.y * scale) / scale,
                std::round(point.z * scale) / scale};
        };
        for (const auto& edge : impl_->mesh.edges) {
            if (edge.overlay) continue;
            for (std::size_t index = 0; index + 1 < edge.points.size(); ++index) {
                auto first = rounded(edge.points[index]);
                auto second = rounded(edge.points[index + 1]);
                if (second < first) std::swap(first, second);
                topology_segments.insert(
                    {edge.reference.owner_id, {first, second}});
            }
        }
        struct SharedRecord { QVector3D first, second, normal; };
        std::map<std::pair<std::string, std::pair<std::array<double, 3>,
            std::array<double, 3>>>, std::vector<SharedRecord>> shared;
        const auto triangle_count = impl_->mesh.triangles.size() / 3;
        for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
            if (triangle >= impl_->mesh.triangle_references.size()) continue;
            const auto& face_reference = impl_->mesh.triangle_references[triangle];
            const std::array<std::uint32_t, 3> indices{
                impl_->mesh.triangles[triangle * 3],
                impl_->mesh.triangles[triangle * 3 + 1],
                impl_->mesh.triangles[triangle * 3 + 2]};
            if (indices[0] >= impl_->mesh.vertices.size() ||
                indices[1] >= impl_->mesh.vertices.size() ||
                indices[2] >= impl_->mesh.vertices.size()) continue;
            const std::array<zima::kernel::Vec3, 3> points{
                impl_->mesh.vertices[indices[0]],
                impl_->mesh.vertices[indices[1]],
                impl_->mesh.vertices[indices[2]]};
            const QVector3D pa(static_cast<float>(points[0].x),
                static_cast<float>(points[0].y), static_cast<float>(points[0].z));
            const QVector3D pb(static_cast<float>(points[1].x),
                static_cast<float>(points[1].y), static_cast<float>(points[1].z));
            const QVector3D pc(static_cast<float>(points[2].x),
                static_cast<float>(points[2].y), static_cast<float>(points[2].z));
            QVector3D normal = QVector3D::crossProduct(pb - pa, pc - pa);
            if (normal.lengthSquared() > 1.0e-12F) normal.normalize();
            const std::pair<QVector3D, QVector3D> triangle_edges[3]{
                {pa, pb}, {pb, pc}, {pc, pa}};
            const std::string owner = face_reference.valid()
                ? face_reference.owner_id : std::string();
            const std::string face_key = owner + "|" + face_reference.semantic_key;
            for (std::size_t side = 0; side < 3; ++side) {
                auto first = rounded(points[side]);
                auto second = rounded(points[(side + 1) % 3]);
                if (second < first) std::swap(first, second);
                shared[{face_key, {first, second}}].push_back(
                    {triangle_edges[side].first, triangle_edges[side].second,
                     normal});
            }
        }
        impl_->silhouette_candidates.clear();
        for (const auto& [key, records] : shared) {
            if (records.size() != 2) continue;
            const auto separator = key.first.find('|');
            const std::string owner = separator == std::string::npos
                ? key.first : key.first.substr(0, separator);
            if (topology_segments.contains({owner, key.second})) continue;
            impl_->silhouette_candidates.push_back({records[0].first,
                records[0].second, records[0].normal, records[1].normal});
        }
    }
    impl_->gpu_dirty = false;
}

void MeshView::paintGL() {
    glClear(GL_DEPTH_BUFFER_BIT);
    // QPainter overlays and the individual display-mode passes modify GL
    // state.  Re-establish the Python viewer's common baseline every frame.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_SCISSOR_TEST);
    constexpr int background_bands = 64;
    const qreal pixel_ratio = devicePixelRatioF();
    const int framebuffer_width = std::max(
        1, static_cast<int>(std::lround(width() * pixel_ratio)));
    const int framebuffer_height = std::max(
        1, static_cast<int>(std::lround(height() * pixel_ratio)));
    for (int band = 0; band < background_bands; ++band) {
        const float factor = (static_cast<float>(band) + 0.5F) /
            static_cast<float>(background_bands);
        const auto channel = [factor](int bottom, int top) {
            return (bottom + (top - bottom) * factor) / 255.0F;
        };
        const int first_y = framebuffer_height * band / background_bands;
        const int last_y = framebuffer_height * (band + 1) / background_bands;
        glScissor(0, first_y, framebuffer_width, std::max(1, last_y - first_y));
        glClearColor(channel(23, 59), channel(27, 70), channel(33, 84), 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glDisable(GL_SCISSOR_TEST);
    impl_->vertex_array.bind();
    if (impl_->gpu_dirty) upload_mesh();
    if (!impl_->program.isLinked()) {
        impl_->vertex_array.release();
        // Reference geometry is viewer-owned 2D overlay data and must remain
        // usable even when the solid-body OpenGL shader is unavailable.  In
        // particular, a newly created empty Part has no body at all, but its
        // persisted Origin must still be visible.
        const QMatrix4x4 mvp = impl_->projection(width(), height()) * impl_->view();
        const auto project = [&](const zima::kernel::Vec3& point) {
            QVector4D clip = mvp * QVector4D(point.x, point.y, point.z, 1.0F);
            if (std::abs(clip.w()) > 1.0e-9F) clip /= clip.w();
            return QPointF((clip.x() + 1.0F) * width() / 2.0F,
                           (1.0F - clip.y()) * height() / 2.0F);
        };
        const double reference_scale = impl_->view_scale /
            std::max(impl_->reference_view_scale, 1.0e-6F);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto draw_reference_segment = [&](const QPointF& first,
                const QPointF& second, const QColor& color, double width) {
            const QLineF line(first, second);
            if (line.length() <= 1.0e-6) return;
            const QPointF normal{-line.dy() / line.length() * width * 0.5,
                                 line.dx() / line.length() * width * 0.5};
            painter.save();
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawPolygon(QPolygonF{first + normal, second + normal,
                                          second - normal, first - normal});
            painter.restore();
        };
if (impl_->show_planes) {
            painter.setPen(QPen(QColor(173, 110, 46), 1.5));
            for (const auto& edge : impl_->mesh.edges) {
                const bool origin = edge.reference.semantic_key.starts_with(
                    "origin:plane:");
                if (!origin) continue;
                // Origin planes are painted as a screen-space QPainter overlay
                // (not the depth-tested GL edge pass), so they must consult the
                // same constraint-reference highlight state as
                // edge_is_highlighted()/edge_display_color() above; otherwise
                // clicking a reference row in a placement dialog never shows a
                // visible highlight for an Origin-plane reference.
                const bool referenced =
                    impl_->constraint_reference_owner_ids.contains(edge.reference.owner_id) ||
                    impl_->constraint_reference_edges.contains(edge_key(edge.reference));
                const QColor plane_color = referenced
                    ? QColor(0, 209, 255) : QColor(173, 110, 46);
                zima::kernel::Vec3 center;
                const std::size_t corner_count = edge.points.size() > 1
                    ? edge.points.size() - 1 : edge.points.size();
                for (std::size_t index = 0; index < corner_count; ++index) {
                    const auto& point = edge.points[index];
                    center.x += point.x; center.y += point.y; center.z += point.z;
                }
                if (corner_count != 0) {
                    center.x /= corner_count; center.y /= corner_count;
                    center.z /= corner_count;
                }
                const double plane_scale = is_screen_constant_plane(
                    edge.reference.semantic_key) ? reference_scale : 1.0;
                const auto plane_point = [&](const auto& point) {
                    return zima::kernel::Vec3{
                        center.x + (point.x - center.x) * plane_scale,
                        center.y + (point.y - center.y) * plane_scale,
                        center.z + (point.z - center.z) * plane_scale};
                };
                for (std::size_t index = 1; index < edge.points.size(); ++index) {
                    draw_reference_segment(project(plane_point(edge.points[index - 1])),
                        project(plane_point(edge.points[index])), plane_color, 1.5);
                }
                if (!edge.points.empty()) {
                    painter.setPen(QPen(plane_color, 1.5));
                    painter.drawText(project(plane_point(edge.points.front())) + QPointF(6.0, -5.0),
                        QString::fromStdString(edge.reference.semantic_key.substr(
                            std::string("origin:plane:").size())).toUpper());
                }
            }
        }
        if (impl_->show_origins || impl_->show_axes) {
            for (const auto& axis : impl_->mesh.axes) {
                if (!axis.reference.semantic_key.starts_with("origin:axis:")) continue;
                const QColor color = axis.reference.semantic_key == "origin:axis:x"
                    ? QColor(232, 76, 61)
                    : axis.reference.semantic_key == "origin:axis:y"
                        ? QColor(46, 204, 112) : QColor(51, 153, 219);
                painter.setPen(QPen(color, 1.5));
                const QPointF start = project(axis.point);
                const QPointF end = project({
                    axis.point.x + axis.direction.x * axis.display_length * reference_scale,
                    axis.point.y + axis.direction.y * axis.display_length * reference_scale,
                    axis.point.z + axis.direction.z * axis.display_length * reference_scale});
                draw_reference_segment(start, end, color, 1.5);
                const QLineF line(start, end);
                if (line.length() > 1.0) {
                    const QPointF unit = (line.p2() - line.p1()) / line.length();
                    const QPointF normal{-unit.y(), unit.x()};
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(color);
                    painter.drawPolygon(QPolygonF{end, end - unit * 20.0 + normal * 3.526,
                                                  end - unit * 20.0 - normal * 3.526});
                }
                painter.setPen(QPen(color, 1.5));
                painter.drawText(end + QPointF(5.0, -4.0),
                    axis.label.empty() ? QString::fromStdString(
                        axis.reference.semantic_key.substr(
                            std::string("origin:axis:").size())).toUpper()
                    : QString::fromStdString(axis.label).toUpper());
            }
        }
        if (impl_->show_origins) {
                painter.setPen(QPen(QColor(0, 0, 0), 1.0));
                painter.setBrush(QColor(0, 0, 0));
            for (const auto& point : impl_->mesh.points) {
                if (point.reference.semantic_key != "origin:point") continue;
                const QPointF center = project(point.position);
                draw_circular_marker(painter, center, QColor(0, 0, 0));
                if (!point.label.empty()) {
                    painter.drawText(center + QPointF(8.0, -6.0),
                                     QString::fromStdString(point.label));
                }
            }
        }
        if (!impl_->mesh.constraint_markers.empty()) {
            auto marker_font = painter.font();
            marker_font.setBold(true);
            painter.setFont(marker_font);
            painter.setPen(QPen(QColor("#7CFF6B"), 2.0));
            std::map<std::pair<int, int>, int> occupied_slots;
            for (std::size_t marker_index = 0;
                 marker_index < impl_->mesh.constraint_markers.size(); ++marker_index) {
                const auto& marker = impl_->mesh.constraint_markers[marker_index];
                const QPointF anchor = project(marker.position);
                const auto key = std::pair{
                    static_cast<int>(std::lround(anchor.x())),
                    static_cast<int>(std::lround(anchor.y()))};
                const int slot = occupied_slots[key]++;
                painter.drawText(anchor + QPointF(7.0 + slot * 16.0, -7.0),
                    QString::fromStdString(marker.label));
            }
        }
        return;
    }
    impl_->program.bind();
    const QMatrix4x4 view = impl_->view();
    impl_->program.setUniformValue("mvp",
        impl_->projection(width(), height()) * view);
    impl_->program.setUniformValue("normalMatrix", view.normalMatrix());
    const auto bind_attributes = [&](QOpenGLBuffer& buffer) {
        buffer.bind();
        impl_->program.enableAttributeArray(0);
        impl_->program.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
        impl_->program.enableAttributeArray(1);
        impl_->program.setAttributeBuffer(
            1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
    };

    const auto draw_triangles = [&] {
        impl_->program.setUniformValue("unlit", 0);
        bind_attributes(impl_->vertices);
        // Keep body edges in front of their supporting surface.  Without the
        // same polygon offset used by the Python viewer, coplanar fragments
        // z-fight and the three edge display modes look nearly identical or
        // lose arbitrary edge sections depending on the driver.
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0F, 1.0F);
        const auto color_vector = [](const QColor& color) {
            return QVector4D(static_cast<float>(color.redF()),
                static_cast<float>(color.greenF()),
                static_cast<float>(color.blueF()),
                static_cast<float>(color.alphaF()));
        };
        const std::size_t triangle_count = impl_->mesh.triangles.size() / 3;
        std::size_t first{};
        while (first < triangle_count) {
            QColor color = impl_->body_surface_color;
            if (first < impl_->mesh.triangle_references.size()) {
                const auto& path =
                    impl_->mesh.triangle_references[first].instance_path;
                if (const auto found =
                        impl_->body_surface_instance_colors.find(path);
                    found != impl_->body_surface_instance_colors.end()) {
                    color = found->second;
                }
            }
            std::size_t end = first + 1;
            while (end < triangle_count) {
                QColor next = impl_->body_surface_color;
                if (end < impl_->mesh.triangle_references.size()) {
                    const auto& path =
                        impl_->mesh.triangle_references[end].instance_path;
                    if (const auto found =
                            impl_->body_surface_instance_colors.find(path);
                        found != impl_->body_surface_instance_colors.end()) {
                        next = found->second;
                    }
                }
                if (next != color) break;
                ++end;
            }
            if (color.alpha() < 255) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }
            impl_->program.setUniformValue("color", color_vector(color));
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(first * 3),
                static_cast<GLsizei>((end - first) * 3));
            if (color.alpha() < 255) glDisable(GL_BLEND);
            first = end;
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
    };
    // Resolves one final edge colour by priority, mirroring Python's
    // _edge_display_color() in zima_cad/viewer.py: selected > object overlay
    // > hovered > feature preview (line-drawing modes only) > colour
    // override > base colour (white). `hovered`/`selected` here are driven
    // by the existing single-candidate hover/confirm mechanism rather than
    // Python's separate _hovered_edge/_selected_edge fields, since C++ has
    // one shared candidate-cycling model instead of a dedicated edge cursor.
    const auto edge_is_highlighted = [&](const zima::kernel::ViewerEdge& edge,
            const std::optional<ViewerCandidate>& highlighted) {
        const auto key = edge_key(edge.reference);
        const bool preview = impl_->feature_preview_owner_ids.contains(edge.reference.owner_id) &&
            (impl_->display_mode == DisplayMode::Wire ||
             impl_->display_mode == DisplayMode::HiddenEdges ||
             impl_->display_mode == DisplayMode::NoHiddenEdges);
        const bool candidate_match = highlighted &&
            (highlighted->kind == CandidateKind::Edge) &&
            highlighted->owner_id == edge.reference.owner_id &&
            highlighted->semantic_key == edge.reference.semantic_key &&
            highlighted->instance_path == edge.reference.instance_path;
        return candidate_match ||
            impl_->edge_treatment_selection_edges.contains(key) ||
            impl_->feature_selected_edges.contains(key) ||
            impl_->feature_hover_edges.contains(key) ||
            impl_->constraint_reference_edges.contains(key) ||
            impl_->assembly_reference_edges.contains(key) ||
            impl_->object_overlay_main_edge_keys.contains(key) ||
            impl_->constraint_reference_owner_ids.contains(edge.reference.owner_id) ||
            impl_->selected_container_content_ids.contains(edge.reference.owner_id) ||
            preview;
    };
    const auto edge_display_color = [&](const zima::kernel::ViewerEdge& edge,
            const std::optional<ViewerCandidate>& highlighted,
            bool candidate_is_confirmed) {
        const auto key = edge_key(edge.reference);
        const bool candidate_match = highlighted &&
            highlighted->kind == CandidateKind::Edge &&
            highlighted->owner_id == edge.reference.owner_id &&
            highlighted->semantic_key == edge.reference.semantic_key &&
            highlighted->instance_path == edge.reference.instance_path;
        const bool selected = (candidate_match && candidate_is_confirmed) ||
            impl_->edge_treatment_selection_edges.contains(key) ||
            impl_->feature_selected_edges.contains(key) ||
            impl_->constraint_reference_edges.contains(key) ||
            impl_->assembly_reference_edges.contains(key) ||
            impl_->selected_container_content_ids.contains(edge.reference.owner_id) ||
            impl_->constraint_reference_owner_ids.contains(edge.reference.owner_id);
        if (selected) return QVector4D(0.0F, 0.82F, 1.0F, 1.0F);
        if (impl_->object_overlay_main_edge_keys.contains(key)) {
            return QVector4D(1.0F, 0.48F, 0.0F, 1.0F);
        }
        const bool hovered = (candidate_match && !candidate_is_confirmed) ||
            impl_->feature_hover_edges.contains(key);
        if (hovered) return QVector4D(1.0F, 0.48F, 0.0F, 1.0F);
        const bool preview = impl_->feature_preview_owner_ids.contains(edge.reference.owner_id) &&
            (impl_->display_mode == DisplayMode::Wire ||
             impl_->display_mode == DisplayMode::HiddenEdges ||
             impl_->display_mode == DisplayMode::NoHiddenEdges);
        if (preview) return QVector4D(0.0F, 0.82F, 1.0F, 1.0F);
        if (impl_->edge_color_override) {
            const auto& color = *impl_->edge_color_override;
            return QVector4D(static_cast<float>(color.redF()),
                static_cast<float>(color.greenF()),
                static_cast<float>(color.blueF()), 1.0F);
        }
        return QVector4D(1.0F, 1.0F, 1.0F, 1.0F);
    };
    const auto draw_lines = [&](bool force_black_if_not_highlighted = false) {
        impl_->program.setUniformValue("unlit", 1);
        bind_attributes(impl_->lines);
        std::optional<ViewerCandidate> highlighted = impl_->confirmed_candidate;
        const bool confirmed = highlighted.has_value();
        if (!highlighted && !impl_->candidates.empty()) {
            highlighted = impl_->candidates[impl_->active_candidate];
        }
        for (std::size_t index = 0; index < impl_->line_ranges.size(); ++index) {
            const auto& [first, count] = impl_->line_ranges[index];
            const auto& edge = impl_->line_edges[index];
            if (force_black_if_not_highlighted) {
                // Matches Python's hidden_edges black underlay pass: only
                // real body edges not already highlighted are dimmed here,
                // so the depth-tested colour pass afterwards can cleanly
                // overwrite their visible portions.
                if (edge_is_highlighted(edge, highlighted)) continue;
                impl_->program.setUniformValue(
                    "color", QVector4D(0.0F, 0.0F, 0.0F, 1.0F));
            } else {
                impl_->program.setUniformValue(
                    "color", edge_display_color(edge, highlighted, confirmed));
            }
            glDrawArrays(GL_LINE_STRIP, first, count);
        }
    };
    const auto depth_prepass = [&] {
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        draw_triangles();
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    };
    const auto draw_visible_lines = [&] {
        glDepthFunc(GL_LEQUAL);
        draw_lines();
        glDepthFunc(GL_LESS);
    };
    // Per-edge colour now comes from edge_display_color() by priority
    // (selected/overlay/hover/preview/override/base white), matching
    // zima_cad/viewer.py's _edge_display_color(). Display-mode switch below
    // only toggles depth handling and the hidden_edges black underlay pass.
    switch (impl_->display_mode) {
        case DisplayMode::Wire:
            glDisable(GL_DEPTH_TEST);
            draw_lines();
            glEnable(GL_DEPTH_TEST);
            break;
        case DisplayMode::HiddenEdges:
            depth_prepass();
            glDisable(GL_DEPTH_TEST);
            draw_lines(/*force_black_if_not_highlighted=*/true);
            glEnable(GL_DEPTH_TEST);
            draw_visible_lines();
            break;
        case DisplayMode::NoHiddenEdges:
            depth_prepass();
            draw_visible_lines();
            break;
        case DisplayMode::ShadedWithEdges:
            // Default body fill matches Python's ViewerMesh surface color
            // #B9C2CC (zima_cad/viewer.py:565), not a hardcoded green.
            draw_triangles();
            draw_visible_lines();
            break;
        case DisplayMode::Shaded:
            draw_triangles();
            break;
    }

    // Boundary/silhouette edges on curved surfaces (cylinders, spheres, ...)
    // that have no real sharp topology edge there. Gated the same way as
    // Python's _draw_gpu_silhouette_edges() in zima_cad/viewer.py: only in
    // the line-drawing display modes, never in plain Shaded.
    if (impl_->display_mode != DisplayMode::Shaded &&
        !impl_->silhouette_candidates.empty()) {
        const QVector3D view_direction =
            impl_->orientation.inverted().rotatedVector(QVector3D(0.0F, 0.0F, 1.0F));
        std::vector<float> silhouette_data;
        silhouette_data.reserve(impl_->silhouette_candidates.size() * 12);
        for (const auto& candidate : impl_->silhouette_candidates) {
            const float side_a = QVector3D::dotProduct(candidate.normal_a, view_direction);
            const float side_b = QVector3D::dotProduct(candidate.normal_b, view_direction);
            constexpr float epsilon = 1.0e-4F;
            if ((side_a >= -epsilon) == (side_b >= -epsilon)) continue;
            silhouette_data.insert(silhouette_data.end(), {
                candidate.first.x(), candidate.first.y(), candidate.first.z(),
                0.0F, 0.0F, 1.0F,
                candidate.second.x(), candidate.second.y(), candidate.second.z(),
                0.0F, 0.0F, 1.0F});
        }
        if (!silhouette_data.empty()) {
            impl_->silhouette.bind();
            impl_->silhouette.allocate(silhouette_data.data(),
                static_cast<int>(silhouette_data.size() * sizeof(float)));
            bind_attributes(impl_->silhouette);
            if (impl_->display_mode == DisplayMode::HiddenEdges) {
                glDisable(GL_DEPTH_TEST);
                impl_->program.setUniformValue(
                    "color", QVector4D(0.0F, 0.0F, 0.0F, 1.0F));
                glDrawArrays(GL_LINES, 0,
                    static_cast<GLsizei>(silhouette_data.size() / 6));
                glEnable(GL_DEPTH_TEST);
            }
            impl_->program.setUniformValue(
                "color", QVector4D(1.0F, 1.0F, 1.0F, 1.0F));
            glDepthFunc(GL_LEQUAL);
            glDrawArrays(GL_LINES, 0,
                static_cast<GLsizei>(silhouette_data.size() / 6));
            glDepthFunc(GL_LESS);
        }
    }

    std::optional<ViewerCandidate> highlighted = impl_->confirmed_candidate;
    QVector4D highlight_color(0.12F, 0.86F, 0.94F, 1.0F);
    if (!highlighted && !impl_->candidates.empty()) {
        highlighted = impl_->candidates[impl_->active_candidate];
        highlight_color = QVector4D(1.0F, 0.55F, 0.05F, 1.0F);
    }
    impl_->program.disableAttributeArray(0);
    impl_->program.disableAttributeArray(1);
    impl_->program.release();
    impl_->vertex_array.release();

    const bool axes_selectable = std::find(
            impl_->allowed_kinds.begin(), impl_->allowed_kinds.end(),
            CandidateKind::Axis) != impl_->allowed_kinds.end() ||
        std::find(impl_->allowed_kinds.begin(), impl_->allowed_kinds.end(),
            CandidateKind::SketchAxis) != impl_->allowed_kinds.end();
    const bool axes_visible = impl_->show_axes || impl_->show_origins ||
        impl_->editing_origin_visible || axes_selectable;
    const bool sketch_geometry_visible = impl_->show_sketches && std::any_of(
        impl_->mesh.edges.begin(), impl_->mesh.edges.end(), [](const auto& edge) {
            return edge.reference.semantic_key.starts_with("segment:") ||
                edge.reference.semantic_key.starts_with("trim_piece:") ||
                edge.reference.semantic_key.starts_with("circle:") ||
                edge.reference.semantic_key.starts_with("arc:") ||
                edge.reference.semantic_key.starts_with("ellipse:") ||
                edge.reference.semantic_key.starts_with("elliptical_arc:") ||
                edge.reference.semantic_key.starts_with("bspline:") ||
                edge.reference.semantic_key.starts_with("text:") ||
                edge.reference.semantic_key.starts_with("external_edge:") ||
                edge.reference.semantic_key.starts_with("external_axis:") ||
                edge.reference.semantic_key.starts_with("external_face:");
        });
    const bool external_points_visible = impl_->show_sketches && std::any_of(
        impl_->mesh.points.begin(), impl_->mesh.points.end(), [](const auto& point) {
            return point.reference.semantic_key.starts_with("external_point:");
        });
    const bool points_selectable = std::find(
            impl_->allowed_kinds.begin(), impl_->allowed_kinds.end(),
            CandidateKind::Vertex) != impl_->allowed_kinds.end() ||
        std::find(impl_->allowed_kinds.begin(), impl_->allowed_kinds.end(),
            CandidateKind::SketchPoint) != impl_->allowed_kinds.end();
    const bool point_containers_selectable = std::find(
            impl_->allowed_kinds.begin(), impl_->allowed_kinds.end(),
            CandidateKind::Container) != impl_->allowed_kinds.end() &&
        std::any_of(impl_->mesh.points.begin(), impl_->mesh.points.end(),
            [](const auto& point) {
                return point.reference.semantic_key == "point";
            });
    // A Point container's own marker is `always_visible` (it IS the visible
    // entity, see geometry_kernel.hpp) and must keep rendering even while a
    // different construction dialog (Axis/Plane...) narrows the active
    // selection filter to kinds that no longer include Vertex/Container --
    // otherwise an already-created Point disappears from the view the
    // moment another construction command starts picking references.
    const bool always_visible_point_present = std::any_of(
        impl_->mesh.points.begin(), impl_->mesh.points.end(),
        [](const auto& point) { return point.always_visible; });
    const bool points_visible =
        ((impl_->show_points || points_selectable || point_containers_selectable ||
          always_visible_point_present) &&
         !impl_->mesh.points.empty()) ||
        external_points_visible || impl_->show_origins ||
        impl_->editing_origin_visible;
    const bool planes_selectable = std::find(
        impl_->allowed_kinds.begin(), impl_->allowed_kinds.end(),
        CandidateKind::Plane) != impl_->allowed_kinds.end();
    const bool planes_visible = (impl_->show_planes || planes_selectable ||
        impl_->editing_origin_visible) && std::any_of(
        impl_->mesh.edges.begin(), impl_->mesh.edges.end(), [](const auto& edge) {
            return edge.reference.semantic_key == "border" ||
                edge.reference.semantic_key.starts_with("origin:plane:");
        });
    const bool dimensions_visible = !impl_->mesh.dimensions.empty();
    if (axes_visible || points_visible || planes_visible ||
        sketch_geometry_visible || dimensions_visible ||
        !impl_->transient_edges.empty() || !impl_->transient_points.empty() ||
        !impl_->transient_labels.empty() ||
        impl_->sketch_cursor.has_value() ||
        !impl_->extent_manipulators.empty() ||
        !impl_->operation_direction_indicators.empty() ||
        // Every candidate kind has an overlay below.  Restricting entry to
        // point/edge/axis candidates accidentally made Face, Container and
        // Occurrence hover completely invisible whenever datum overlays were
        // hidden.  The picker had found the Box face, but the user received
        // no orange feedback and it looked unselectable.
        highlighted.has_value()) {
        const QMatrix4x4 mvp = impl_->projection(width(), height()) * view;
        const auto project = [&](const zima::kernel::Vec3& point) {
            QVector4D clip = mvp * QVector4D(
                point.x, point.y, point.z, 1.0F);
            if (std::abs(clip.w()) > 1.0e-9F) clip /= clip.w();
            return QPointF((clip.x() + 1.0F) * width() / 2.0F,
                           (1.0F - clip.y()) * height() / 2.0F);
        };
        const double reference_scale = impl_->view_scale /
            std::max(impl_->reference_view_scale, 1.0e-6F);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto draw_reference_segment = [&](const QPointF& first,
                const QPointF& second, const QColor& color, double width) {
            const QLineF line(first, second);
            if (line.length() <= 1.0e-6) return;
            const QPointF normal{-line.dy() / line.length() * width * 0.5,
                                 line.dx() / line.length() * width * 0.5};
            painter.save();
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawPolygon(QPolygonF{first + normal, second + normal,
                                          second - normal, first - normal});
            painter.restore();
        };
        const auto screen_infinite_line = [&](const QPointF& first,
                const QPointF& second) {
            const QLineF seed(first, second);
            if (seed.length() <= 1.0e-6) return QLineF{};
            const QPointF unit = (second - first) / seed.length();
            const double extent = 2.0 * std::hypot(
                static_cast<double>(width()), static_cast<double>(height()));
            const QPointF middle = (first + second) * 0.5;
            return QLineF(middle - unit * extent, middle + unit * extent);
        };
        for (const auto& indicator : impl_->operation_direction_indicators) {
            const QColor purple("#D05CFF");
            painter.setPen(QPen(purple, 2.0, Qt::SolidLine, Qt::RoundCap));
            painter.setBrush(purple);
            const QPointF center = project(indicator.origin);
            std::vector<QPointF> path;
            if (!indicator.angular) {
                const QPointF projected_direction = project({
                    indicator.origin.x + indicator.direction.x,
                    indicator.origin.y + indicator.direction.y,
                    indicator.origin.z + indicator.direction.z}) - center;
                const double magnitude = std::hypot(
                    projected_direction.x(), projected_direction.y());
                if (magnitude > 1.0e-6) {
                    path = {center, center + projected_direction / magnitude * 62.0};
                }
            } else {
                const auto normalize = [](zima::kernel::Vec3 value) {
                    const double length = std::sqrt(value.x * value.x +
                        value.y * value.y + value.z * value.z);
                    return length > 1.0e-12
                        ? zima::kernel::Vec3{value.x / length, value.y / length,
                            value.z / length}
                        : zima::kernel::Vec3{};
                };
                const auto axis = normalize(indicator.direction);
                const auto radial_unit = normalize(indicator.radial);
                const double radial_length = std::sqrt(
                    indicator.radial.x * indicator.radial.x +
                    indicator.radial.y * indicator.radial.y +
                    indicator.radial.z * indicator.radial.z);
                const auto tangent_unit = normalize({
                    axis.y * radial_unit.z - axis.z * radial_unit.y,
                    axis.z * radial_unit.x - axis.x * radial_unit.z,
                    axis.x * radial_unit.y - axis.y * radial_unit.x});
                const double shown_angle = std::clamp(
                    std::abs(indicator.angle_degrees), 0.001, 360.0);
                constexpr int samples = 40;
                for (int sample = 0; sample <= samples; ++sample) {
                    const double radians = shown_angle *
                        static_cast<double>(sample) / samples *
                        std::numbers::pi / 180.0 *
                        (indicator.angle_degrees < 0.0 ? -1.0 : 1.0);
                    const zima::kernel::Vec3 point{
                        indicator.origin.x + radial_length *
                            (radial_unit.x * std::cos(radians) +
                             tangent_unit.x * std::sin(radians)),
                        indicator.origin.y + radial_length *
                            (radial_unit.y * std::cos(radians) +
                             tangent_unit.y * std::sin(radians)),
                        indicator.origin.z + radial_length *
                            (radial_unit.z * std::cos(radians) +
                             tangent_unit.z * std::sin(radians))};
                    path.push_back(project(point));
                }
            }
            for (std::size_t index = 1; index < path.size(); ++index)
                painter.drawLine(path[index - 1], path[index]);
            if (path.size() >= 2) {
                QPointF along = path.back() - path[path.size() - 2];
                const double magnitude = std::hypot(along.x(), along.y());
                if (magnitude > 1.0e-6) {
                    along /= magnitude;
                    const QPointF normal{-along.y(), along.x()};
                    const QPointF tip = path.back();
                    const QPointF base = tip - along * 11.0;
                    painter.drawPolygon(QPolygonF{
                        tip, base + normal * 4.0, base - normal * 4.0});
                }
            }
            painter.setBrush(Qt::NoBrush);
        }
        for (const auto& handle : impl_->extent_manipulators) {
            const double direction_length = std::sqrt(
                handle.direction.x * handle.direction.x +
                handle.direction.y * handle.direction.y +
                handle.direction.z * handle.direction.z);
            if (direction_length > 1.0e-12) {
                const zima::kernel::Vec3 direction{
                    handle.direction.x / direction_length,
                    handle.direction.y / direction_length,
                    handle.direction.z / direction_length};
                const zima::kernel::Vec3 endpoint{
                    handle.origin.x + direction.x * handle.length,
                    handle.origin.y + direction.y * handle.length,
                    handle.origin.z + direction.z * handle.length};
                const QPointF start = project(handle.origin);
                const QPointF finish = project(endpoint);
                const QColor purple("#D05CFF");
                const bool start_hot = impl_->hovered_extent_key ==
                        handle.start_key || impl_->dragged_extent_key ==
                        handle.start_key;
                const bool end_hot = impl_->hovered_extent_key ==
                        handle.end_key || impl_->dragged_extent_key ==
                        handle.end_key;
                if (!handle.point_only) {
                    painter.setPen(QPen(purple, 2.0));
                    painter.drawLine(start, finish);
                    QPointF along = finish - start;
                    const double screen_length = std::hypot(along.x(), along.y());
                    if (screen_length > 1.0e-6) {
                        along /= screen_length;
                        const QPointF normal{-along.y(), along.x()};
                        const QPointF tip = finish - along * 9.0;
                        const QPointF base = tip - along * 10.0;
                        painter.setBrush(purple);
                        painter.drawPolygon(QPolygonF{tip,
                            base + normal * 1.763269807,
                            base - normal * 1.763269807});
                    }
                }
                // Purple operation manipulators keep their semantic colour
                // even under the pointer. They are painted by QPainter above
                // the depth-tested model, so they remain visible through the
                // preview body just like the Plane offset handle.
                static_cast<void>(start_hot);
                static_cast<void>(end_hot);
                const QColor start_color = purple;
                painter.setPen(QPen(start_color, 1.0));
                painter.setBrush(start_color);
                painter.drawEllipse(start, 6.0, 6.0);
                if (!handle.point_only) {
                    painter.setPen(QPen(purple, 1.0));
                    painter.setBrush(purple);
                    painter.drawEllipse(finish, 6.0, 6.0);
                }
                painter.setBrush(Qt::NoBrush);
            }
        }
        if (sketch_geometry_visible) {
            for (const auto& edge : impl_->mesh.edges) {
                if (!edge.reference.semantic_key.starts_with("segment:") &&
                    !edge.reference.semantic_key.starts_with("trim_piece:") &&
                    !edge.reference.semantic_key.starts_with("circle:") &&
                    !edge.reference.semantic_key.starts_with("arc:") &&
                    !edge.reference.semantic_key.starts_with("ellipse:") &&
                    !edge.reference.semantic_key.starts_with("elliptical_arc:") &&
                    !edge.reference.semantic_key.starts_with("bspline:") &&
                    !edge.reference.semantic_key.starts_with("text:") &&
                    !edge.reference.semantic_key.starts_with("external_edge:") &&
                    !edge.reference.semantic_key.starts_with("external_axis:") &&
                    !edge.reference.semantic_key.starts_with("external_face:")) continue;
                const bool text = edge.reference.semantic_key.starts_with("text:");
                const bool external = edge.reference.semantic_key.starts_with(
                        "external_edge:") ||
                    edge.reference.semantic_key.starts_with("external_axis:");
                const bool external_face = edge.reference.semantic_key.starts_with(
                    "external_face:");
                const QColor text_color = edge.reference.semantic_key.ends_with(":yellow")
                    ? QColor(245, 205, 80)
                    : edge.reference.semantic_key.ends_with(":white")
                        ? QColor(255, 255, 255) : QColor(77, 216, 17);
                const QColor external_color = edge.reference.semantic_key.ends_with(
                    ":broken") ? QColor(179, 74, 60) : QColor(145, 105, 72);
                QPen edge_pen = text ? QPen(text_color, 1.8)
                    : (external || external_face)
                        ? QPen(external_color, 1.5, Qt::DashLine)
                    : edge.construction
                        ? QPen(QColor(77, 216, 17), 1.5, Qt::DashLine)
                        : QPen(QColor(255, 255, 255), 1.8);
                if (edge.dash_dot) {
                    edge_pen.setStyle(Qt::CustomDashLine);
                    edge_pen.setDashPattern({9.0, 4.0, 2.0, 4.0});
                }
                painter.setPen(edge_pen);
                if (edge.infinite && edge.points.size() >= 2) {
                    painter.drawLine(screen_infinite_line(
                        project(edge.points.front()), project(edge.points.back())));
                    continue;
                }
                for (std::size_t index = 1; index < edge.points.size(); ++index) {
                    painter.drawLine(project(edge.points[index - 1]),
                                     project(edge.points[index]));
                }
            }
        }
        if (!impl_->mesh.constraint_markers.empty()) {
            auto marker_font = painter.font();
            marker_font.setBold(true);
            painter.setFont(marker_font);
            painter.setPen(QPen(QColor("#7CFF6B"), 2.0));
            std::map<std::pair<int, int>, int> occupied_slots;
            for (std::size_t marker_index = 0;
                 marker_index < impl_->mesh.constraint_markers.size(); ++marker_index) {
                const auto& marker = impl_->mesh.constraint_markers[marker_index];
                const bool selected = highlighted &&
                    highlighted->kind == CandidateKind::SketchConstraint &&
                    highlighted->geometry_index == marker_index;
                painter.setPen(QPen(selected
                    ? (impl_->confirmed_candidate ? QColor(30, 220, 240)
                                                  : QColor(255, 145, 35))
                    : QColor("#7CFF6B"), 2.0));
                const QPointF anchor = project(marker.position);
                const auto key = std::pair{
                    static_cast<int>(std::lround(anchor.x())),
                    static_cast<int>(std::lround(anchor.y()))};
                const int slot = occupied_slots[key]++;
                painter.drawText(anchor + QPointF(7.0 + slot * 16.0, -7.0),
                    QString::fromStdString(marker.label));
            }
        }
        if (planes_visible) {
            painter.setPen(QPen(QColor(173, 110, 46), 1.5));
            for (const auto& edge : impl_->mesh.edges) {
                const bool origin = edge.reference.semantic_key.starts_with(
                    "origin:plane:");
                if (edge.reference.semantic_key != "border" && !origin) continue;
                const bool creation_preview = !origin &&
                    impl_->feature_preview_owner_ids.contains(
                        edge.reference.owner_id);
                if ((origin && !impl_->show_planes && !planes_selectable &&
                        !impl_->editing_origin_visible) ||
                    (!origin && !impl_->show_planes && !planes_selectable &&
                        !creation_preview)) continue;
                const bool exact_highlight = highlighted &&
                    ((highlighted->kind == CandidateKind::Plane &&
                      highlighted->owner_id == edge.reference.owner_id &&
                      (highlighted->semantic_key == edge.reference.semantic_key ||
                       (highlighted->semantic_key == "plane" &&
                        edge.reference.semantic_key == "border"))) ||
                     (highlighted->kind == CandidateKind::Container &&
                      highlighted->semantic_key == "plane" &&
                      highlighted->owner_id + ":entity" ==
                          edge.reference.owner_id) ||
                     // A ray landing exactly on the Plane container's own
                     // defining-point marker (see construction_viewer_mesh)
                     // resolves to a Vertex pick of that point -- or, under
                     // the default selection contract (Container-only, see
                     // MeshView::Impl::allowed_kinds), to the paired
                     // Container candidate with semantic_key=="point" that
                     // ordered_viewer_candidates derives from that same
                     // Vertex pick -- rather than to the border edges
                     // themselves. Both resolve with higher priority than
                     // Face/Container "plane" in ordered_viewer_candidates.
                     // Without these two branches, confirming/hovering that
                     // point would highlight only the dot, leaving the
                     // border its plain presentation color even though the
                     // whole container is what got selected.
                     (highlighted->kind == CandidateKind::Vertex &&
                      highlighted->semantic_key == "point" &&
                      edge.reference.owner_id.ends_with(":entity") &&
                      highlighted->owner_id == edge.reference.owner_id.substr(
                          0, edge.reference.owner_id.size() -
                              std::string_view(":entity").size()) + ":origin") ||
                     (highlighted->kind == CandidateKind::Container &&
                      highlighted->semantic_key == "point" &&
                      edge.reference.owner_id.ends_with(":entity") &&
                      highlighted->owner_id == edge.reference.owner_id.substr(
                          0, edge.reference.owner_id.size() -
                              std::string_view(":entity").size())) ||
                     // Selecting/hovering the whole Origin (e.g. clicking it
                     // in the tree) highlights every one of its own
                     // FRONT/TOP/... plane labels too, matching Python's
                     // unified _selected_object_id highlight check.
                     (origin && highlighted->kind == CandidateKind::Container &&
                      highlighted->semantic_key == "origin" &&
                      highlighted->owner_id == edge.reference.owner_id)) &&
                    highlighted->instance_path == edge.reference.instance_path;
                // Clicking a populated reference row in a placement dialog
                // (Umístění kontejneru) highlights the referenced plane
                // here too. Match ONLY the precise per-entity key (owner_id
                // + semantic_key + instance_path) -- e.g. exactly
                // "origin:plane:xz", never every sibling plane of the same
                // Origin/body owner_id, which is why owner_id alone is
                // never used for this check.
                const bool referenced = !exact_highlight &&
                    impl_->constraint_reference_edges.contains(edge_key(edge.reference));
                const QColor plane_color = exact_highlight
                    ? (impl_->confirmed_candidate ? QColor(30, 220, 240)
                                                  : QColor(255, 140, 12))
                    : (referenced || creation_preview) ? QColor(0, 209, 255)
                    : QColor(173, 110, 46);
                zima::kernel::Vec3 center;
                const std::size_t corner_count = edge.points.size() > 1
                    ? edge.points.size() - 1 : edge.points.size();
                for (std::size_t index = 0; index < corner_count; ++index) {
                    const auto& point = edge.points[index];
                    center.x += point.x; center.y += point.y; center.z += point.z;
                }
                if (corner_count != 0) {
                    center.x /= corner_count; center.y /= corner_count;
                    center.z /= corner_count;
                }
                const double plane_scale = is_screen_constant_plane(
                    edge.reference.semantic_key) ? reference_scale : 1.0;
                const auto plane_point = [&](const auto& point) {
                    return zima::kernel::Vec3{
                        center.x + (point.x - center.x) * plane_scale,
                        center.y + (point.y - center.y) * plane_scale,
                        center.z + (point.z - center.z) * plane_scale};
                };
                for (std::size_t index = 1; index < edge.points.size(); ++index) {
                    draw_reference_segment(project(plane_point(edge.points[index - 1])),
                        project(plane_point(edge.points[index])),
                        plane_color,
                        origin ? 1.5 : 1.4);
                }
                if (origin && !edge.points.empty()) {
                    painter.setPen(QPen(plane_color, 1.5));
                    const auto key = QString::fromStdString(
                        edge.reference.semantic_key.substr(
                            std::string("origin:plane:").size())).toUpper();
                    painter.drawText(project(plane_point(edge.points.front())) +
                        QPointF(6.0, -5.0), key);
                }
            }
        }
        if (!impl_->transient_edges.empty()) {
            // Orange is reserved for hover. Pending container geometry is a
            // live creation preview and therefore uses the same cyan as a
            // confirmed/modeling result, with continuous rather than dashed
            // edges.
            painter.setPen(QPen(QColor(0, 209, 255), 2.0, Qt::SolidLine,
                                Qt::RoundCap, Qt::RoundJoin));
            for (const auto& edge : impl_->transient_edges) {
                const bool inference_reference =
                    edge.reference.semantic_key == "inference:reference";
                painter.setPen(QPen(inference_reference
                        ? QColor(255, 140, 12) : QColor(0, 209, 255),
                    2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                for (std::size_t index = 1; index < edge.points.size(); ++index) {
                    painter.drawLine(project(edge.points[index - 1]), project(edge.points[index]));
                }
            }
        }
        for (const auto& point : impl_->transient_points) {
            draw_circular_marker(painter, project(point), QColor(255, 140, 12));
        }
        if (impl_->sketch_cursor) {
            const QColor cursor_color = impl_->sketch_cursor_snapped
                ? QColor(255, 140, 12) : QColor(255, 255, 255);
            const QPointF cursor = project(*impl_->sketch_cursor);
            draw_circular_marker(painter, cursor, cursor_color);
            if (!impl_->sketch_cursor_label.empty()) {
                painter.setPen(QPen(cursor_color, 1.5));
                painter.drawText(cursor + QPointF(8.0, -8.0),
                    QString::fromStdString(impl_->sketch_cursor_label));
            }
        }
        if (!impl_->transient_labels.empty()) {
            painter.setPen(QPen(QColor(255, 140, 12), 1.5));
            for (const auto& [point, label] : impl_->transient_labels) {
                painter.drawText(project(point) + QPointF(8.0, -8.0),
                    QString::fromStdString(label));
            }
        }
        if (dimensions_visible) {
            for (std::size_t index = 0; index < impl_->mesh.dimensions.size(); ++index) {
                const auto& dimension = impl_->mesh.dimensions[index];
                const bool selected = highlighted &&
                    highlighted->kind == CandidateKind::Dimension &&
                    highlighted->geometry_index == index;
                const QColor color = selected
                    ? (impl_->confirmed_candidate ? QColor(30, 220, 240)
                                                  : QColor(255, 140, 12))
                    : QColor(245, 205, 80);
                painter.setPen(QPen(color, selected ? 3.0 : 1.5));
                painter.setBrush(color);
                const QString text =
                    QString::fromStdString(dimension.label_prefix) +
                    QString::number(dimension.value, 'f',
                impl_->dimension_decimal_places) +
                    QString::fromStdString(dimension.unit_suffix);
                constexpr double arrow_length = 10.0;
                constexpr double arrow_half_width = 1.763269807;
                constexpr double tail_length = 7.0;
                const auto arrow = [&](const QPointF& tip,
                        QPointF direction) {
                    const double length = std::hypot(direction.x(), direction.y());
                    if (length <= 1.0e-9) return QPolygonF{};
                    direction /= length;
                    const QPointF normal{-direction.y(), direction.x()};
                    const QPointF base = tip - direction * arrow_length;
                    return QPolygonF{tip,
                        base + normal * arrow_half_width,
                        base - normal * arrow_half_width};
                };
                if (dimension.kind ==
                        zima::kernel::ViewerDimensionKind::Angular) {
                    const QPointF vertex = project(dimension.witness_first);
                    const auto normalize = [](zima::kernel::Vec3 value) {
                        const double length = std::hypot(
                            std::hypot(value.x, value.y), value.z);
                        return length > 1.0e-12
                            ? zima::kernel::Vec3{value.x / length,
                                value.y / length, value.z / length}
                            : zima::kernel::Vec3{};
                    };
                    const auto axis = normalize(dimension.plane_normal);
                    const zima::kernel::Vec3 radial{
                        dimension.line_first.x - dimension.witness_first.x,
                        dimension.line_first.y - dimension.witness_first.y,
                        dimension.line_first.z - dimension.witness_first.z};
                    const double radius = std::hypot(
                        std::hypot(radial.x, radial.y), radial.z);
                    const auto radial_unit = normalize(radial);
                    const auto tangent = normalize({
                        axis.y * radial_unit.z - axis.z * radial_unit.y,
                        axis.z * radial_unit.x - axis.x * radial_unit.z,
                        axis.x * radial_unit.y - axis.y * radial_unit.x});
                    const double sweep = dimension.sweep_degrees *
                        std::numbers::pi / 180.0;
                    constexpr int samples = 48;
                    QPolygonF arc;
                    arc.reserve(samples + 1);
                    for (int sample = 0; sample <= samples; ++sample) {
                        const double angle = sweep * sample / samples;
                        arc.push_back(project({
                            dimension.witness_first.x + radius *
                                (radial_unit.x * std::cos(angle) +
                                 tangent.x * std::sin(angle)),
                            dimension.witness_first.y + radius *
                                (radial_unit.y * std::cos(angle) +
                                 tangent.y * std::sin(angle)),
                            dimension.witness_first.z + radius *
                                (radial_unit.z * std::cos(angle) +
                                 tangent.z * std::sin(angle))}));
                    }
                    painter.drawLine(vertex, arc.front());
                    painter.drawLine(vertex, arc.back());
                    painter.drawPolyline(arc);
                    if (arc.size() > 2) {
                        painter.drawPolygon(arrow(arc.front(), arc[1] - arc[0]));
                        painter.drawPolygon(arrow(arc.back(),
                            arc[arc.size() - 2] - arc.back()));
                    }
                    const double middle = sweep * 0.5;
                    const auto middle_world = zima::kernel::Vec3{
                        dimension.witness_first.x + (radius + 3.0) *
                            (radial_unit.x * std::cos(middle) +
                             tangent.x * std::sin(middle)),
                        dimension.witness_first.y + (radius + 3.0) *
                            (radial_unit.y * std::cos(middle) +
                             tangent.y * std::sin(middle)),
                        dimension.witness_first.z + (radius + 3.0) *
                            (radial_unit.z * std::cos(middle) +
                             tangent.z * std::sin(middle))};
                    painter.drawText(project(middle_world), text);
                    painter.setBrush(Qt::NoBrush);
                    continue;
                }
                if (dimension.kind ==
                        zima::kernel::ViewerDimensionKind::Radius ||
                    dimension.kind ==
                        zima::kernel::ViewerDimensionKind::Diameter) {
                    const QPointF center = project(dimension.witness_first);
                    const QPointF rim = project(dimension.witness_second);
                    QPointF outward = rim - center;
                    const double length = std::hypot(outward.x(), outward.y());
                    if (length <= 1.0e-9) continue;
                    outward /= length;
                    const QPointF opposite = center - (rim - center);
                    painter.drawLine(
                        dimension.kind == zima::kernel::ViewerDimensionKind::Diameter
                            ? opposite : center, rim);
                    painter.drawPolygon(arrow(rim, -outward));
                    if (dimension.kind ==
                        zima::kernel::ViewerDimensionKind::Diameter) {
                        painter.drawPolygon(arrow(opposite, outward));
                    }
                    const QPointF tail = rim + outward *
                        (arrow_length + tail_length);
                    const double side = outward.x() >= 0.0 ? 1.0 : -1.0;
                    const QPointF shoulder = tail + QPointF(side * 36.0, 0.0);
                    painter.drawLine(rim + outward * arrow_length, tail);
                    painter.drawLine(tail, shoulder);
                    const double text_width =
                        painter.fontMetrics().horizontalAdvance(text);
                    painter.drawText(shoulder + QPointF(
                        side > 0.0 ? 2.0 : -text_width - 2.0, 5.0), text);
                    painter.setBrush(Qt::NoBrush);
                    continue;
                }
                const auto layout = linear_dimension_layout(
                    dimension, impl_->mesh, project);
                painter.drawLine(layout.witness_first, layout.line_first);
                painter.drawLine(layout.witness_second, layout.line_second);
                painter.drawLine(layout.line_first, layout.line_second);
                painter.drawPolygon(arrow(layout.line_first, layout.along));
                painter.drawPolygon(arrow(layout.line_second, -layout.along));
                painter.drawLine(
                    layout.line_first - layout.along * arrow_length,
                    layout.first_tail);
                painter.drawLine(
                    layout.line_second + layout.along * arrow_length,
                    layout.second_tail);
                painter.drawLine(layout.leader_start, layout.leader_end);
                painter.drawText(layout.text_baseline, text);
                painter.setBrush(Qt::NoBrush);
            }
        }
        if (axes_visible) {
            for (const auto& axis : impl_->mesh.axes) {
                const bool origin = axis.reference.semantic_key.starts_with(
                    "origin:axis:");
                if ((origin && !impl_->show_origins && !axes_selectable &&
                        !impl_->editing_origin_visible) ||
                    (!origin && !impl_->show_axes && !axes_selectable)) continue;
                const bool exact_highlight = highlighted &&
                    (((highlighted->kind == CandidateKind::Axis ||
                       highlighted->kind == CandidateKind::SketchAxis) &&
                      highlighted->owner_id == axis.reference.owner_id &&
                      highlighted->semantic_key == axis.reference.semantic_key) ||
                     (highlighted->kind == CandidateKind::Container &&
                      highlighted->semantic_key == "axis" &&
                      highlighted->owner_id + ":entity" ==
                          axis.reference.owner_id) ||
                     // A ray landing exactly on the Axis container's own
                     // defining-point marker (see construction_viewer_mesh)
                     // resolves to a Vertex pick of that point -- or, under
                     // the default selection contract (Container-only, see
                     // MeshView::Impl::allowed_kinds), to the paired
                     // Container candidate with semantic_key=="point" that
                     // ordered_viewer_candidates derives from that same
                     // Vertex pick -- rather than to the Axis line itself.
                     // Both resolve with higher priority than Axis/Container
                     // "axis" in ordered_viewer_candidates. Without these
                     // two branches, confirming/hovering that point would
                     // highlight only the dot, leaving the line its plain
                     // presentation color even though the whole container
                     // is what got selected.
                     (highlighted->kind == CandidateKind::Vertex &&
                      highlighted->semantic_key == "point" &&
                      axis.reference.owner_id.ends_with(":entity") &&
                      highlighted->owner_id == axis.reference.owner_id.substr(
                          0, axis.reference.owner_id.size() -
                              std::string_view(":entity").size()) + ":origin") ||
                     (highlighted->kind == CandidateKind::Container &&
                      highlighted->semantic_key == "point" &&
                      axis.reference.owner_id.ends_with(":entity") &&
                      highlighted->owner_id == axis.reference.owner_id.substr(
                          0, axis.reference.owner_id.size() -
                              std::string_view(":entity").size())) ||
                     // Selecting/hovering the whole Origin also highlights
                     // its own X/Y/Z axis lines and labels, matching
                     // Python's unified selected/hovered-object check.
                     (origin && highlighted->kind == CandidateKind::Container &&
                      highlighted->semantic_key == "origin" &&
                      highlighted->owner_id == axis.reference.owner_id)) &&
                    highlighted->instance_path == axis.reference.instance_path;
                // Match ONLY the precise per-entity key -- see the identical
                // comment on the plane block above.
                const bool referenced = !exact_highlight &&
                    impl_->constraint_reference_edges.contains(EdgeKey{
                        axis.reference.owner_id, axis.reference.semantic_key,
                        axis.reference.instance_path});
                const bool creation_preview = !origin &&
                    impl_->feature_preview_owner_ids.contains(
                        axis.reference.owner_id);
                const QColor color = exact_highlight
                    ? (impl_->confirmed_candidate ? QColor(30, 220, 240)
                                                  : QColor(255, 140, 12))
                    : (referenced || creation_preview) ? QColor(0, 209, 255)
                    : axis.reference.semantic_key == "origin:axis:x"
                        ? QColor(232, 76, 61)
                    : axis.reference.semantic_key == "origin:axis:y"
                        ? QColor(46, 204, 112)
                    : axis.reference.semantic_key == "origin:axis:z"
                        ? QColor(51, 153, 219)
                        : QColor(150, 150, 150);
                const bool sketch_axis = axis.reference.semantic_key ==
                        "sketch_axis:x" ||
                    axis.reference.semantic_key == "sketch_axis:y";
                const QColor presentation_color = !origin &&
                        !exact_highlight && !referenced && !creation_preview
                    ? QColor(173, 110, 46) : color;
                painter.setPen(QPen(presentation_color, origin ? 2.0 : 1.5,
                    Qt::SolidLine));
                const double first = origin ? 0.0 : -axis.display_length * 0.5;
                const double second = origin
                    ? axis.display_length * reference_scale
                    : axis.display_length * 0.5;
                const QPointF start = project({axis.point.x + axis.direction.x * first,
                                                axis.point.y + axis.direction.y * first,
                                                axis.point.z + axis.direction.z * first});
                const QPointF end = project({axis.point.x + axis.direction.x * second,
                                              axis.point.y + axis.direction.y * second,
                                              axis.point.z + axis.direction.z * second});
                if (origin) {
                    draw_reference_segment(start, end, presentation_color, 2.0);
                } else {
                    // A plain painter.drawLine() call silently produces no
                    // visible output in this overlay pass (leftover GL state
                    // from the solid-body shader/VAO breaks QPainter's native
                    // line primitive), whereas the filled-polygon helper
                    // already used for the Origin's own axes renders
                    // correctly.  Draw the construction axis as a dash-dot
                    // pattern of small filled quads through the same helper
                    // so it is actually visible, matching the persisted
                    // origin axes rendering technique.
                    const QLineF full_line = sketch_axis
                        ? screen_infinite_line(start, end) : QLineF(start, end);
                    const double total_length = full_line.length();
                    if (total_length > 1.0e-6) {
                        const QPointF pattern_start = full_line.p1();
                        const QPointF unit = (full_line.p2() - full_line.p1()) /
                            total_length;
                        // Keep the Python Sketcher-like dash-dot rhythm legible
                        // at normal DPI. The former 10/5/2/5 pixel pattern was
                        // visually compressed and almost looked continuous.
                        constexpr double dash = 20.0;
                        constexpr double gap = 10.0;
                        constexpr double dot = 3.0;
                        constexpr double pattern = dash + gap + dot + gap;
                        double offset = 0.0;
                        while (offset < total_length) {
                            const double dash_end = std::min(offset + dash, total_length);
                            draw_reference_segment(pattern_start + unit * offset,
                                pattern_start + unit * dash_end,
                                presentation_color, 1.5);
                            const double dot_start = std::min(offset + dash + gap, total_length);
                            const double dot_end = std::min(dot_start + dot, total_length);
                            if (dot_end > dot_start) {
                                draw_reference_segment(pattern_start + unit * dot_start,
                                    pattern_start + unit * dot_end,
                                    presentation_color, 1.5);
                            }
                            offset += pattern;
                        }
                    }
                    // Mark the axis's own origin point with a small filled
                    // dot in the same hover/select/reference-aware color as
                    // the axis line itself, matching the always-visible dot
                    // used for a standalone Point container -- this is the
                    // only way to see where a construction Axis actually
                    // starts (its dimension/length is measured from here),
                    // and it must stay visible together with the length
                    // dimension annotation, not just on hover/selection.
                    // Mark the axis's own origin point with a small filled
                    // dot, but only while this axis is hovered or selected
                    // (exact_highlight) -- matching a solid body's own
                    // origin-indicator convention -- not permanently, so it
                    // does not clutter idle/default rendering.
                    if (exact_highlight) {
                        draw_circular_marker(
                            painter, project(axis.point), presentation_color);
                    }
                }
                if (origin) {
                    const QLineF line(start, end);
                    if (line.length() > 1.0) {
                        const QPointF unit = (line.p2() - line.p1()) / line.length();
                        const QPointF normal{-unit.y(), unit.x()};
                        painter.setPen(Qt::NoPen);
                        painter.setBrush(color);
                        painter.drawPolygon(QPolygonF{
                            end, end - unit * 20.0 + normal * 3.526,
                            end - unit * 20.0 - normal * 3.526});
                    }
                    auto axis_font = painter.font();
                    axis_font.setBold(true);
                    axis_font.setPointSizeF(std::max(9.0, axis_font.pointSizeF()));
                    painter.setFont(axis_font);
                    painter.setPen(QPen(color, 1.5));
                    painter.drawText(end + QPointF(5.0, -4.0),
                        axis.label.empty()
                            ? QString::fromStdString(axis.reference.semantic_key.substr(
                                  std::string("origin:axis:").size())).toUpper()
                            : QString::fromStdString(axis.label).toUpper());
                }
            }
        }
        // Non-origin construction Point markers are drawn AFTER the plane
        // and axis overlays (rather than before them, as previously) so an
        // overlapping origin/construction axis line can never paint over
        // and hide a Point container's own marker -- the marker must
        // always stay on top, matching how the Origin's own point (drawn
        // even later, right below) is never hidden either.
        if (points_visible) {
            for (const auto& point : impl_->mesh.points) {
                // In an inactive Sketch this point is deliberately visible
                // only together with the whole-container hover/selection
                // overlay below.  Construction/profile points retain their
                // normal visibility rules.
                if (point.reference.semantic_key ==
                        "sketch:origin-marker" ||
                    point.reference.semantic_key ==
                        "container:origin-marker") continue;
                const bool external = point.reference.semantic_key.starts_with(
                        "external_point:") &&
                    point.reference.semantic_key !=
                        "external_point:sketch_origin";
                const bool origin = point.reference.semantic_key == "origin:point";
                if (origin) continue;
                if ((origin && !impl_->show_origins && !points_selectable) ||
                    (!origin && !external && !point.always_visible &&
                     !impl_->show_points &&
                     !points_selectable && !point_containers_selectable) ||
                    (external && !impl_->show_sketches)) continue;
                if (external) {
                    const QColor color = point.reference.semantic_key.ends_with(
                        ":broken") ? QColor(179, 74, 60) : QColor(145, 105, 72);
                    painter.setPen(QPen(color, 1.8));
                    const QPointF center = project(point.position);
                    painter.drawLine(center + QPointF(-4.0, -4.0),
                                     center + QPointF(4.0, 4.0));
                    painter.drawLine(center + QPointF(-4.0, 4.0),
                                     center + QPointF(4.0, -4.0));
                } else {
                    const bool replaced_by_active_point_manipulator =
                        std::any_of(impl_->extent_manipulators.begin(),
                            impl_->extent_manipulators.end(), [&](const auto& handle) {
                                return handle.point_only &&
                                    std::abs(point.position.x - handle.origin.x) <= 1.0e-9 &&
                                    std::abs(point.position.y - handle.origin.y) <= 1.0e-9 &&
                                    std::abs(point.position.z - handle.origin.z) <= 1.0e-9;
                            });
                    const bool plane_offset_preview =
                        point.reference.semantic_key ==
                            "preview:plane-offset-point";
                    const bool creation_preview =
                        impl_->feature_preview_owner_ids.contains(
                            point.reference.owner_id);
                    const bool active_sketch_point =
                        !impl_->active_sketch_owner_id.empty() &&
                        point.reference.owner_id ==
                            impl_->active_sketch_owner_id &&
                        point.reference.semantic_key.starts_with("point:");
                    const bool selected = point.reference.owner_id ==
                            impl_->selected_container_origin_id ||
                        (impl_->confirmed_candidate &&
                         (impl_->confirmed_candidate->kind == CandidateKind::Vertex ||
                          impl_->confirmed_candidate->kind ==
                              CandidateKind::SketchExternalReference) &&
                         impl_->confirmed_candidate->owner_id ==
                            point.reference.owner_id &&
                         impl_->confirmed_candidate->semantic_key ==
                            point.reference.semantic_key &&
                         impl_->confirmed_candidate->instance_path ==
                            point.reference.instance_path);
                    const bool hovered = !impl_->confirmed_candidate && highlighted &&
                        ((highlighted->kind == CandidateKind::Container &&
                          (highlighted->semantic_key == "point" ||
                           highlighted->semantic_key == "axis" ||
                           highlighted->semantic_key == "plane") &&
                          point.reference.owner_id ==
                            highlighted->owner_id + ":origin") ||
                         ((highlighted->kind == CandidateKind::Vertex ||
                           highlighted->kind ==
                               CandidateKind::SketchExternalReference) &&
                          highlighted->semantic_key == point.reference.semantic_key &&
                          highlighted->owner_id == point.reference.owner_id)) &&
                        point.reference.instance_path == highlighted->instance_path;
                    // Match ONLY the precise per-entity key -- see the
                    // identical comment on the plane block above.
                    const bool referenced = !selected && !hovered &&
                        impl_->constraint_reference_edges.contains(EdgeKey{
                            point.reference.owner_id, point.reference.semantic_key,
                            point.reference.instance_path});
                    // An Axis/Plane container's own marker
                    // (always_visible=false) stays fully invisible in the
                    // ordinary state -- the Axis line / Plane border is
                    // enough on its own; the dot only appears once one of
                    // the states above gives it a meaningful color.
                    if (!point.always_visible && !selected && !hovered && !referenced &&
                        !creation_preview) {
                        continue;
                    }
                    // A live work-plane preview is one visual object: its
                    // rectangular border and its offset-origin point use the
                    // same cyan preview colour in Plane, Sketch, Extrusion
                    // and Revolution properties. The old purple exception
                    // made the point look unrelated to its plane.
                    const QColor marker_color = active_sketch_point
                        ? QColor("#D05CFF")
                        : plane_offset_preview
                        ? QColor(0, 209, 255)
                        : selected
                        ? QColor(30, 220, 240)
                        : hovered ? QColor(255, 122, 0)
                        : (referenced || creation_preview)
                            ? QColor(0, 209, 255)
                        : point.reference.semantic_key ==
                                "external_point:sketch_origin"
                            ? QColor(173, 110, 46)
                        : point.reference.semantic_key.starts_with("point:")
                            ? point.construction
                                ? QColor(77, 216, 17)
                                : QColor(255, 255, 255)
                            : QColor(0, 0, 0);
                    painter.setPen(QPen(marker_color, 1.0));
                    painter.setBrush(marker_color);
                    const QPointF center = project(point.position);
                    // While Plane Properties owns this exact position, its
                    // purple drag handle replaces (rather than overlays) the
                    // normal cyan plane marker. The persisted point remains
                    // in viewer data and returns immediately when the dialog
                    // closes, preserving ordinary View-only editing.
                    if (!replaced_by_active_point_manipulator) {
                        draw_circular_marker(painter, center, marker_color);
                    }
                    if (!point.label.empty()) {
                        painter.setPen(QPen(replaced_by_active_point_manipulator
                                ? QColor("#D05CFF")
                                : marker_color,
                            1.0));
                        painter.drawText(center + QPointF(8.0, -6.0),
                                         QString::fromStdString(point.label));
                    }
                }
            }
        }
        if (impl_->show_origins || points_selectable ||
            impl_->editing_origin_visible) {
            for (const auto& point : impl_->mesh.points) {
                if (point.reference.semantic_key != "origin:point") continue;
                const QPointF center = project(point.position);
                // Match ONLY the precise per-entity key -- see the identical
                // comment on the plane block above.
                const bool referenced = impl_->constraint_reference_edges.contains(EdgeKey{
                    point.reference.owner_id, point.reference.semantic_key,
                    point.reference.instance_path});
                const QColor marker_color = referenced
                    ? QColor(0, 209, 255) : QColor(0, 0, 0);
                painter.setPen(QPen(marker_color, 1.0));
                painter.setBrush(marker_color);
                draw_circular_marker(painter, center, marker_color);
                if (!point.label.empty()) {
                    painter.setPen(QPen(marker_color, 1.0));
                    painter.drawText(center + QPointF(8.5, -6.5),
                                     QString::fromStdString(point.label));
                }
            }
        }
        if (highlighted) {
            const bool active_sketch_point =
                highlighted->kind == CandidateKind::SketchPoint &&
                !impl_->active_sketch_owner_id.empty() &&
                highlighted->owner_id == impl_->active_sketch_owner_id;
            const QColor color = active_sketch_point
                ? QColor("#D05CFF")
                : impl_->confirmed_candidate
                    ? QColor(30, 220, 240) : QColor(255, 140, 12);
            const bool origin_group = highlighted->kind == CandidateKind::Container &&
                highlighted->semantic_key == "origin";
            const bool sketch_container =
                highlighted->kind == CandidateKind::Container &&
                highlighted->semantic_key == "sketch";
            // Datum planes are selected through their two hidden picking
            // triangles, but hover/confirmation must present the semantic
            // plane as its rectangular border. Never expose the triangulated
            // picking surface (including its diagonal) as the highlighted
            // geometry. This applies equally to Origin planes and standalone
            // Plane construction containers.
            const bool datum_plane = highlighted->kind == CandidateKind::Plane &&
                (highlighted->semantic_key.starts_with("origin:plane:") ||
                 highlighted->semantic_key == "plane");
            if (datum_plane) {
                const auto edge = std::find_if(impl_->mesh.edges.begin(),
                    impl_->mesh.edges.end(), [&](const auto& candidate) {
                        return candidate.reference.owner_id == highlighted->owner_id &&
                            (candidate.reference.semantic_key == highlighted->semantic_key ||
                             (highlighted->semantic_key == "plane" &&
                              candidate.reference.semantic_key == "border")) &&
                            candidate.reference.instance_path == highlighted->instance_path;
                    });
                if (edge != impl_->mesh.edges.end() && edge->points.size() > 1) {
                    const std::size_t corner_count = edge->points.size() - 1;
                    zima::kernel::Vec3 center;
                    for (std::size_t index = 0; index < corner_count; ++index) {
                        center.x += edge->points[index].x;
                        center.y += edge->points[index].y;
                        center.z += edge->points[index].z;
                    }
                    center.x /= corner_count; center.y /= corner_count;
                    center.z /= corner_count;
                    const double plane_scale = is_screen_constant_plane(
                        edge->reference.semantic_key) ? reference_scale : 1.0;
                    const auto display_point = [&](const auto& point) {
                        return zima::kernel::Vec3{
                            center.x + (point.x - center.x) * plane_scale,
                            center.y + (point.y - center.y) * plane_scale,
                            center.z + (point.z - center.z) * plane_scale};
                    };
                    for (std::size_t index = 1; index < edge->points.size(); ++index) {
                        draw_reference_segment(project(display_point(edge->points[index - 1])),
                            project(display_point(edge->points[index])), color, 1.5);
                    }
                }
            }
            if (sketch_container) {
                // The whole Sketch is the exact ordinary-mode candidate.
                // Highlight its persisted ZIMA curves together, while
                // active Sketcher still highlights individual entities via
                // SketchSegment/SketchCurve candidates.
                painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
                painter.setBrush(color);
                for (const auto& edge : impl_->mesh.edges) {
                    if (edge.reference.owner_id != highlighted->owner_id ||
                        edge.reference.instance_path !=
                            highlighted->instance_path || edge.construction) continue;
                    const auto& key = edge.reference.semantic_key;
                    if (!key.starts_with("segment:") &&
                        !key.starts_with("circle:") &&
                        !key.starts_with("arc:") &&
                        !key.starts_with("ellipse:") &&
                        !key.starts_with("elliptical_arc:") &&
                        !key.starts_with("bspline:") &&
                        !key.starts_with("text:")) continue;
                    for (std::size_t index = 1; index < edge.points.size(); ++index) {
                        painter.drawLine(project(edge.points[index - 1]),
                                         project(edge.points[index]));
                    }
                }
                for (const auto& point : impl_->mesh.points) {
                    if (point.reference.owner_id == highlighted->owner_id &&
                        point.reference.instance_path ==
                            highlighted->instance_path &&
                        (point.reference.semantic_key.starts_with("point:") ||
                         point.reference.semantic_key ==
                            "sketch:origin-marker")) {
                        draw_circular_marker(
                            painter, project(point.position), color);
                    }
                }
            }
            if (highlighted->kind == CandidateKind::Container) {
                for (const auto& point : impl_->mesh.points) {
                    if (point.reference.semantic_key !=
                            "container:origin-marker" ||
                        point.reference.owner_id != highlighted->owner_id ||
                        point.reference.instance_path !=
                            highlighted->instance_path) continue;
                    painter.setPen(QPen(color, 1.0));
                    painter.setBrush(color);
                    draw_circular_marker(painter, project(point.position), color);
                }
            }
            if ((highlighted->kind == CandidateKind::Occurrence ||
                 (highlighted->kind == CandidateKind::Container && !origin_group) ||
                 highlighted->kind == CandidateKind::Face)) {
                painter.setPen(QPen(color, 1.5));
                painter.setBrush(Qt::NoBrush);
                const bool original = highlighted->geometry ==
                    CandidateGeometry::OriginalReference;
                const auto& vertices = original
                    ? impl_->mesh.original_references.vertices : impl_->mesh.vertices;
                const auto& triangles = original
                    ? impl_->mesh.original_references.triangles : impl_->mesh.triangles;
                const auto& triangle_references = original
                    ? impl_->mesh.original_references.triangle_references
                    : impl_->mesh.triangle_references;
                using RoundedPoint = std::array<long long, 3>;
                using BoundaryKey = std::tuple<std::string, RoundedPoint, RoundedPoint>;
                struct BoundarySegment {
                    zima::kernel::Vec3 first;
                    zima::kernel::Vec3 second;
                    std::size_t uses{};
                    std::array<QVector3D, 2> normals{};
                };
                std::map<BoundaryKey, BoundarySegment> boundary;
                const auto rounded = [](const zima::kernel::Vec3& point) {
                    constexpr double scale = 1.0e7;
                    return RoundedPoint{
                        std::llround(point.x * scale),
                        std::llround(point.y * scale),
                        std::llround(point.z * scale)};
                };
                for (std::size_t triangle = 0;
                     triangle < triangle_references.size(); ++triangle) {
                    const auto& reference = triangle_references[triangle];
                    // Datum and work planes have their own screen-constant
                    // rectangular highlight above. Feeding their persisted
                    // model-space picking triangles into this generic
                    // Container/Face boundary pass draws a second tiny
                    // unscaled rectangle (the "ghost Origin" in 01.png).
                    // Picking triangles remain intact; only this duplicate
                    // presentation path is suppressed.
                    if (reference.semantic_key == "plane" ||
                        reference.semantic_key.starts_with("origin:plane:")) {
                        continue;
                    }
                    const bool matches = highlighted->kind == CandidateKind::Occurrence
                        ? reference.instance_path == highlighted->instance_path
                        : highlighted->kind == CandidateKind::Container
                            ? reference.owner_id == highlighted->owner_id ||
                                ((highlighted->semantic_key == "plane" ||
                                  highlighted->semantic_key == "axis") &&
                                 reference.owner_id ==
                                    highlighted->owner_id + ":entity")
                            : reference.owner_id == highlighted->owner_id &&
                              reference.semantic_key == highlighted->semantic_key;
                    if (!matches || reference.instance_path != highlighted->instance_path ||
                        triangle * 3 + 2 >= triangles.size()) continue;
                    const auto first = triangles[triangle * 3];
                    const auto second = triangles[triangle * 3 + 1];
                    const auto third = triangles[triangle * 3 + 2];
                    if (first >= vertices.size() || second >= vertices.size() ||
                        third >= vertices.size()) continue;
                    const std::array indices{first, second, third};
                    const auto& pa = vertices[first];
                    const auto& pb = vertices[second];
                    const auto& pc = vertices[third];
                    QVector3D triangle_normal = QVector3D::crossProduct(
                        QVector3D(pb.x - pa.x, pb.y - pa.y, pb.z - pa.z),
                        QVector3D(pc.x - pa.x, pc.y - pa.y, pc.z - pa.z));
                    if (triangle_normal.lengthSquared() > 1.0e-12F) {
                        triangle_normal.normalize();
                    }
                    for (std::size_t side = 0; side < 3; ++side) {
                        const auto& a = vertices[indices[side]];
                        const auto& b = vertices[indices[(side + 1) % 3]];
                        auto key_a = rounded(a);
                        auto key_b = rounded(b);
                        if (key_b < key_a) std::swap(key_a, key_b);
                        // Count tessellation edges inside one semantic face,
                        // not across the whole Container. A real Box edge is
                        // shared by two different faces and must remain in
                        // the container/body outline; only a triangle
                        // diagonal repeated inside the same face disappears.
                        auto& segment = boundary[{
                            reference.semantic_key, key_a, key_b}];
                        if (segment.uses == 0) {
                            segment.first = a;
                            segment.second = b;
                        }
                        if (segment.uses < segment.normals.size()) {
                            segment.normals[segment.uses] = triangle_normal;
                        }
                        ++segment.uses;
                    }
                }
                // Highlight the semantic face boundary and its current-view
                // silhouette, never the complete OCCT triangulation. A
                // closed curved face such as a Sphere has no boundary at
                // all, so without the silhouette the picker did find the
                // Container but produced no visible orange feedback.
                const QVector3D view_direction = impl_->orientation.inverted()
                    .rotatedVector(QVector3D(0.0F, 0.0F, 1.0F));
                for (const auto& [key, segment] : boundary) {
                    static_cast<void>(key);
                    QVector3D second_normal = segment.normals[1];
                    if (QVector3D::dotProduct(
                            segment.normals[0], second_normal) < 0.0F) {
                        second_normal = -second_normal;
                    }
                    const bool silhouette = segment.uses == 2 &&
                        (QVector3D::dotProduct(segment.normals[0], view_direction) >= 0.0F) !=
                        (QVector3D::dotProduct(second_normal, view_direction) >= 0.0F);
                    if (segment.uses == 1 || silhouette) {
                        painter.drawLine(project(segment.first),
                                         project(segment.second));
                    }
                }
                // The empty-path Occurrence token is created only by the
                // Tree item "Těleso". Draw the persisted result topology as
                // its cyan wire in addition to the computed silhouette.
                if (highlighted->kind == CandidateKind::Occurrence &&
                    highlighted->instance_path.empty() && !original) {
                    for (const auto& edge : impl_->mesh.edges) {
                        if (edge.overlay) continue;
                        for (std::size_t index = 1; index < edge.points.size(); ++index) {
                            painter.drawLine(project(edge.points[index - 1]),
                                             project(edge.points[index]));
                        }
                    }
                }
            }
            const auto& selectable_edges =
                highlighted->geometry == CandidateGeometry::OriginalReference
                    ? impl_->mesh.original_references.edges : impl_->mesh.edges;
            const auto& selectable_points =
                highlighted->geometry == CandidateGeometry::OriginalReference
                    ? impl_->mesh.original_references.points : impl_->mesh.points;
            const auto& selectable_axes =
                highlighted->geometry == CandidateGeometry::OriginalReference
                    ? impl_->mesh.original_references.axes : impl_->mesh.axes;
            if ((highlighted->kind == CandidateKind::Edge ||
                 highlighted->kind == CandidateKind::SketchSegment ||
                 highlighted->kind == CandidateKind::SketchCurve ||
                 highlighted->kind == CandidateKind::SketchText ||
                 (highlighted->kind == CandidateKind::SketchExternalReference &&
                  (highlighted->semantic_key.starts_with("external_edge:") ||
                   highlighted->semantic_key.starts_with("external_axis:") ||
                   highlighted->semantic_key.starts_with("external_face:"))) ||
                 highlighted->kind == CandidateKind::SketchTrimPiece) &&
                highlighted->geometry_index < selectable_edges.size()) {
                painter.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap));
                if (highlighted->kind == CandidateKind::SketchText ||
                    (highlighted->kind == CandidateKind::SketchExternalReference &&
                     highlighted->semantic_key.starts_with("external_face:"))) {
                    for (const auto& edge : selectable_edges) {
                        if (edge.reference.owner_id != highlighted->owner_id ||
                            edge.reference.semantic_key != highlighted->semantic_key) continue;
                        for (std::size_t index = 1; index < edge.points.size(); ++index) {
                            painter.drawLine(project(edge.points[index - 1]),
                                             project(edge.points[index]));
                        }
                    }
                } else {
                    const auto& edge = selectable_edges[highlighted->geometry_index];
                    for (std::size_t index = 1; index < edge.points.size(); ++index) {
                        painter.drawLine(project(edge.points[index - 1]),
                                         project(edge.points[index]));
                    }
                }
            } else if ((highlighted->kind == CandidateKind::Vertex ||
                        highlighted->kind == CandidateKind::SketchPoint ||
                        (highlighted->kind == CandidateKind::SketchExternalReference &&
                         highlighted->semantic_key.starts_with("external_point:"))) &&
                       highlighted->geometry_index < selectable_points.size()) {
                painter.setPen(QPen(color, 2.0));
                painter.setBrush(color);
                painter.drawEllipse(project(
                    selectable_points[highlighted->geometry_index].position), 5.0, 5.0);
            }
            if (highlighted->kind == CandidateKind::SketchConstraint &&
                highlighted->geometry_index <
                    impl_->mesh.constraint_markers.size()) {
                const auto& marker = impl_->mesh.constraint_markers[
                    highlighted->geometry_index];
                const auto participates = [&](const std::string& semantic_key) {
                    return std::find(marker.participant_semantic_keys.begin(),
                               marker.participant_semantic_keys.end(), semantic_key) !=
                        marker.participant_semantic_keys.end();
                };
                painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap));
                painter.setBrush(color);
                for (const auto& edge : impl_->mesh.edges) {
                    if (edge.reference.owner_id != highlighted->owner_id ||
                        !participates(edge.reference.semantic_key)) continue;
                    for (std::size_t index = 1; index < edge.points.size(); ++index) {
                        painter.drawLine(project(edge.points[index - 1]),
                                         project(edge.points[index]));
                    }
                }
                for (const auto& point : impl_->mesh.points) {
                    if (point.reference.owner_id == highlighted->owner_id &&
                        participates(point.reference.semantic_key)) {
                        painter.drawEllipse(project(point.position), 5.0, 5.0);
                    }
                }
                for (const auto& axis : impl_->mesh.axes) {
                    if (axis.reference.owner_id != highlighted->owner_id ||
                        !participates(axis.reference.semantic_key)) continue;
                    const double half = axis.display_length * 0.5;
                    painter.drawLine(project({
                        axis.point.x - axis.direction.x * half,
                        axis.point.y - axis.direction.y * half,
                        axis.point.z - axis.direction.z * half}), project({
                        axis.point.x + axis.direction.x * half,
                        axis.point.y + axis.direction.y * half,
                        axis.point.z + axis.direction.z * half}));
                }
            }
            if (highlighted->kind == CandidateKind::Dimension &&
                highlighted->geometry_index < impl_->mesh.dimensions.size()) {
                const auto& dimension =
                    impl_->mesh.dimensions[highlighted->geometry_index];
                const auto participates = [&](const std::string& semantic_key) {
                    return std::find(dimension.participant_semantic_keys.begin(),
                               dimension.participant_semantic_keys.end(), semantic_key) !=
                        dimension.participant_semantic_keys.end();
                };
                painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap));
                painter.setBrush(color);
                for (const auto& edge : impl_->mesh.edges) {
                    if (edge.reference.owner_id != highlighted->owner_id ||
                        !participates(edge.reference.semantic_key)) continue;
                    for (std::size_t index = 1; index < edge.points.size(); ++index) {
                        painter.drawLine(project(edge.points[index - 1]),
                                         project(edge.points[index]));
                    }
                }
                for (const auto& point : impl_->mesh.points) {
                    if (point.reference.owner_id == highlighted->owner_id &&
                        participates(point.reference.semantic_key)) {
                        painter.drawEllipse(project(point.position), 5.0, 5.0);
                    }
                }
                for (const auto& axis : impl_->mesh.axes) {
                    if (axis.reference.owner_id != highlighted->owner_id ||
                        !participates(axis.reference.semantic_key)) continue;
                    const double half = axis.display_length * 0.5;
                    painter.drawLine(project({
                        axis.point.x - axis.direction.x * half,
                        axis.point.y - axis.direction.y * half,
                        axis.point.z - axis.direction.z * half}), project({
                        axis.point.x + axis.direction.x * half,
                        axis.point.y + axis.direction.y * half,
                        axis.point.z + axis.direction.z * half}));
                }
            }
            // Whole-Origin hover/selection is already handled by the main
            // plane/axis overlay loops above. Redrawing the same geometry a
            // second time here produced two differently antialiased copies
            // that appeared to pulse between sizes while zooming/picking.
        }
        // Point markers are the final reference layer.  Python recolours the
        // marker itself on hover/selection; drawing a later highlight on top
        // made the black Origin centre disappear at the axis intersection.
        if (impl_->show_origins || points_selectable ||
            impl_->editing_origin_visible) {
            for (const auto& point : impl_->mesh.points) {
                if (point.reference.semantic_key != "origin:point") continue;
                QColor marker_color(0, 0, 0);
                bool exact_highlight = highlighted &&
                    highlighted->owner_id == point.reference.owner_id &&
                    highlighted->instance_path == point.reference.instance_path &&
                    ((highlighted->kind == CandidateKind::Vertex &&
                      highlighted->semantic_key == point.reference.semantic_key) ||
                     (highlighted->kind == CandidateKind::Container &&
                      highlighted->semantic_key == "origin"));
                if (!exact_highlight && highlighted &&
                    highlighted->kind == CandidateKind::Container &&
                    highlighted->semantic_key == "point") {
                    const auto& highlighted_points = highlighted->geometry ==
                            CandidateGeometry::OriginalReference
                        ? impl_->mesh.original_references.points
                        : impl_->mesh.points;
                    if (highlighted->geometry_index < highlighted_points.size()) {
                        exact_highlight = QLineF(project(point.position), project(
                            highlighted_points[highlighted->geometry_index].position))
                                .length() <= 1.0;
                    }
                }
                if (exact_highlight) {
                    marker_color = impl_->confirmed_candidate
                        ? QColor(30, 220, 240) : QColor(255, 122, 0);
                }
                const QPointF center = project(point.position);
                draw_circular_marker(painter, center, marker_color);
                if (!point.label.empty()) {
                    painter.setPen(QPen(marker_color, 1.0));
                    painter.drawText(center + QPointF(8.5, -6.5),
                        QString::fromStdString(point.label));
                }
            }
        }
        painter.setPen(QPen(QColor(30, 220, 240), 2.0,
                            Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(QColor(30, 220, 240));
        for (const auto& selected : impl_->sketch_box_selected_candidates) {
            if ((selected.kind == CandidateKind::SketchSegment ||
                 selected.kind == CandidateKind::SketchCurve ||
                 selected.kind == CandidateKind::SketchText) &&
                selected.geometry_index < impl_->mesh.edges.size()) {
                const auto& edge = impl_->mesh.edges[selected.geometry_index];
                for (std::size_t index = 1; index < edge.points.size(); ++index) {
                    painter.drawLine(project(edge.points[index - 1]),
                                     project(edge.points[index]));
                }
            } else if (selected.kind == CandidateKind::SketchPoint &&
                       selected.geometry_index < impl_->mesh.points.size()) {
                painter.drawEllipse(project(
                    impl_->mesh.points[selected.geometry_index].position), 5.0, 5.0);
            }
        }
        if (impl_->sketch_box_start && impl_->sketch_box_end) {
            painter.setPen(QPen(QColor("#43B9FF"), 1.0));
            painter.setBrush(QColor(67, 185, 255, 35));
            painter.drawRect(QRectF(*impl_->sketch_box_start,
                                    *impl_->sketch_box_end).normalized());
        }
    }
}

void MeshView::update_candidates(const QPointF& position) {
    if (width() <= 0 || height() <= 0) return;
    auto next = selection_candidates_at(position);
    const bool same_order = next.size() == impl_->candidates.size() &&
        std::equal(next.begin(), next.end(), impl_->candidates.begin(),
            [](const ViewerCandidate& left, const ViewerCandidate& right) {
                return left.kind == right.kind && left.owner_id == right.owner_id &&
                    left.semantic_key == right.semantic_key &&
                    left.instance_path == right.instance_path;
            });
    impl_->candidates = std::move(next);
    impl_->candidates_position = position;
    impl_->has_candidates_position = true;
    if (!same_order || impl_->active_candidate >= impl_->candidates.size()) {
        impl_->active_candidate = 0;
    }
    update();
}

std::optional<std::pair<zima::kernel::Vec3, zima::kernel::Vec3>>
MeshView::ray_at(const QPointF& position) const {
    if (width() <= 0 || height() <= 0) return std::nullopt;
    const float x = static_cast<float>(2.0 * position.x() / width() - 1.0);
    const float y = static_cast<float>(1.0 - 2.0 * position.y() / height());
    bool invertible = false;
    const QMatrix4x4 inverse =
        (impl_->projection(width(), height()) * impl_->view()).inverted(&invertible);
    if (!invertible) return std::nullopt;
    QVector4D near_point = inverse * QVector4D(x, y, -1.0F, 1.0F);
    QVector4D far_point = inverse * QVector4D(x, y, 1.0F, 1.0F);
    near_point /= near_point.w();
    far_point /= far_point.w();
    const QVector3D direction = (far_point.toVector3D() - near_point.toVector3D()).normalized();
    return std::pair{
        zima::kernel::Vec3{near_point.x(), near_point.y(), near_point.z()},
        zima::kernel::Vec3{direction.x(), direction.y(), direction.z()}};
}

void MeshView::mousePressEvent(QMouseEvent* event) {
    impl_->last_pointer = event->position().toPoint();
    if (event->button() == Qt::LeftButton) {
      for (const auto& handle : impl_->extent_manipulators) {
        const double norm = std::sqrt(handle.direction.x * handle.direction.x +
            handle.direction.y * handle.direction.y +
            handle.direction.z * handle.direction.z);
        if (norm > 1.0e-12) {
            const zima::kernel::Vec3 direction{handle.direction.x / norm,
                handle.direction.y / norm, handle.direction.z / norm};
            const auto project = [&](const zima::kernel::Vec3& point) {
                QVector4D clip = (impl_->projection(width(), height()) * impl_->view()) *
                    QVector4D(point.x, point.y, point.z, 1.0F);
                if (std::abs(clip.w()) > 1.0e-9F) clip /= clip.w();
                return QPointF((clip.x() + 1.0F) * width() / 2.0F,
                    (1.0F - clip.y()) * height() / 2.0F);
            };
            const zima::kernel::Vec3 endpoint{handle.origin.x + direction.x * handle.length,
                handle.origin.y + direction.y * handle.length,
                handle.origin.z + direction.z * handle.length};
            const QPointF pointer = event->position();
            const double start_distance = QLineF(pointer, project(handle.origin)).length();
            const double end_distance = handle.point_only
                ? std::numeric_limits<double>::infinity()
                : QLineF(pointer, project(endpoint)).length();
            constexpr double tolerance = 12.0;
            if (std::min(start_distance, end_distance) <=
                    tolerance * devicePixelRatioF()) {
                impl_->dragged_extent_manipulator = handle;
                impl_->dragged_extent_key = start_distance <= end_distance
                    ? handle.start_key : handle.end_key;
                impl_->hovered_extent_key = impl_->dragged_extent_key;
                if (impl_->extent_begin_callback)
                    impl_->extent_begin_callback(impl_->dragged_extent_key);
                event->accept();
                update();
                return;
            }
        }
      }
    }
    if (event->button() == Qt::RightButton &&
        event->buttons().testFlag(Qt::MiddleButton)) {
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        impl_->middle_dragged = false;
        impl_->middle_press_position = event->position().toPoint();
    }
    // Hover, RMB cycling and LMB confirmation always consume the candidate
    // list produced for the click position. A preceding mouse move is not a
    // precondition for selecting or clearing a selection.
    if (event->button() == Qt::LeftButton) {
        update_candidates(event->position());
    }
    // RMB cycles the exact same ordered list used by hover and LMB.  Refresh
    // it at the click position so cycling also works immediately after a
    // command changes its selection contract, without requiring a preceding
    // mouse-move event. If the pointer is still within picking tolerance of
    // where the list was last computed (a stationary hover just before the
    // click), reuse that list unchanged instead of recomputing it: a
    // redundant recompute can otherwise re-stabilize the same order and
    // silently reset the active index, making the first RMB press appear to
    // do nothing before cycling starts working from the second press.
    if (event->button() == Qt::RightButton && !impl_->confirmed_candidate) {
        const QPointF click_position = event->position();
        const QPointF delta = impl_->has_candidates_position
            ? click_position - impl_->candidates_position : QPointF{};
        const bool cursor_stationary = impl_->has_candidates_position &&
            !impl_->candidates.empty() &&
            std::hypot(delta.x(), delta.y()) <= 9.0 * devicePixelRatioF();
        if (!cursor_stationary) {
            update_candidates(click_position);
        }
    }
    if (event->button() == Qt::LeftButton &&
        impl_->command_gesture_begin_callback) {
        const auto ray = ray_at(event->position());
        const std::optional<ViewerCandidate> candidate = impl_->candidates.empty()
            ? std::nullopt
            : std::optional{impl_->candidates[impl_->active_candidate]};
        if (ray && impl_->command_gesture_begin_callback(
                candidate, ray->first, ray->second)) {
            impl_->command_gesture_active = true;
            impl_->confirmed_candidate.reset();
            impl_->candidates.clear();
            update();
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton &&
        impl_->sketch_box_selection_enabled && impl_->candidates.empty()) {
        impl_->sketch_box_start = event->position();
        impl_->sketch_box_end = event->position();
        impl_->confirmed_candidate.reset();
        event->accept();
        update();
        return;
    }
    if (event->button() == Qt::LeftButton && impl_->world_click_callback) {
        const auto ray = ray_at(event->position());
        if (ray && impl_->world_click_callback(ray->first, ray->second)) {
            impl_->candidates.clear();
            update();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && !impl_->candidates.empty()) {
        // Keep the exact candidate consumed by this press locally. The
        // confirmation callback may rebuild the scene and consequently clear
        // the viewer's transient confirmed/candidate state before a drag is
        // started; that must not make dragging depend on callback side effects.
        const ViewerCandidate pressed_candidate =
            impl_->candidates[impl_->active_candidate];
        impl_->confirmed_candidate = pressed_candidate;
        impl_->selected_container_origin_id =
            impl_->confirmed_candidate->kind == CandidateKind::Container &&
                (impl_->confirmed_candidate->semantic_key == "point" ||
                 impl_->confirmed_candidate->semantic_key == "axis" ||
                 impl_->confirmed_candidate->semantic_key == "plane")
            ? impl_->confirmed_candidate->owner_id + ":origin"
            : std::string{};
        // Start the gesture from the same pressed candidate before notifying
        // ordinary selection listeners. Those listeners are allowed to
        // rebuild the tree or scene, but that must not invalidate a gesture
        // that was already unambiguously requested by this press.
        const auto drag_ray = ray_at(event->position());
        const bool drag_started = impl_->drag_begin_callback && drag_ray
            ? impl_->drag_begin_callback(
                  pressed_candidate, drag_ray->first, drag_ray->second)
            : false;
        notify_confirmation();
        impl_->drag_active = drag_started;
        update();
    } else if (event->button() == Qt::LeftButton) {
        clear_selection();
        if (impl_->empty_confirmation_callback) {
            impl_->empty_confirmation_callback();
        }
        event->accept();
    } else if (event->button() == Qt::RightButton) {
        if (impl_->confirmed_candidate) {
            if (impl_->context_menu_callback) {
                impl_->context_menu_callback(
                    *impl_->confirmed_candidate, event->globalPosition().toPoint());
            }
        } else if (!impl_->candidates.empty() &&
                   impl_->candidate_right_click_callback &&
                   impl_->candidate_right_click_callback(
                       impl_->candidates[impl_->active_candidate],
                       impl_->active_candidate, impl_->candidates.size())) {
            event->accept();
        } else if (impl_->candidates.size() > 1) {
            impl_->active_candidate =
                next_candidate_index(impl_->active_candidate, impl_->candidates.size());
            update();
            static_cast<void>(refresh_current_pointer_preview());
        } else if (impl_->candidates.size() == 1 &&
                   impl_->single_candidate_right_click_callback &&
                   impl_->single_candidate_right_click_callback(
                       impl_->candidates.front())) {
            event->accept();
        } else if (impl_->candidates.empty() &&
                   impl_->empty_right_click_callback &&
                   impl_->empty_right_click_callback()) {
            event->accept();
        }
    }
}

void MeshView::mouseDoubleClickEvent(QMouseEvent* event) {
    impl_->last_pointer = event->position().toPoint();
    if (event->button() == Qt::MiddleButton) {
        impl_->middle_double_clicked = true;
        if (impl_->double_middle_click_callback &&
            impl_->double_middle_click_callback()) {
            event->accept();
        }
        return;
    }
    if (event->button() != Qt::LeftButton || !impl_->double_confirmation_callback) return;
    update_candidates(event->position());
    if (!impl_->candidates.empty()) {
        impl_->double_confirmation_callback(impl_->candidates[impl_->active_candidate]);
        event->accept();
    }
}

void MeshView::mouseMoveEvent(QMouseEvent* event) {
    const QPoint current = event->position().toPoint();
    const QPoint movement = current - impl_->last_pointer;
    impl_->last_pointer = current;
    if ((event->buttons() & Qt::LeftButton) && impl_->sketch_box_start) {
        impl_->sketch_box_end = event->position();
        event->accept();
        update();
        return;
    }
    if ((event->buttons() & Qt::LeftButton) &&
        impl_->dragged_extent_manipulator &&
        !impl_->dragged_extent_key.empty()) {
        const auto& handle = *impl_->dragged_extent_manipulator;
        const double norm = std::sqrt(handle.direction.x * handle.direction.x +
            handle.direction.y * handle.direction.y +
            handle.direction.z * handle.direction.z);
        if (norm > 1.0e-12 && impl_->extent_update_callback) {
            const zima::kernel::Vec3 direction{handle.direction.x / norm,
                handle.direction.y / norm, handle.direction.z / norm};
            const auto project = [&](const zima::kernel::Vec3& point) {
                QVector4D clip = (impl_->projection(width(), height()) * impl_->view()) *
                    QVector4D(point.x, point.y, point.z, 1.0F);
                if (std::abs(clip.w()) > 1.0e-9F) clip /= clip.w();
                return QPointF((clip.x() + 1.0F) * width() / 2.0F,
                    (1.0F - clip.y()) * height() / 2.0F);
            };
            const QPointF screen_origin = project(handle.origin);
            const QPointF screen_unit = project({handle.origin.x + direction.x,
                handle.origin.y + direction.y, handle.origin.z + direction.z});
            const QPointF axis = screen_unit - screen_origin;
            const double pixels_squared = axis.x() * axis.x() + axis.y() * axis.y();
            if (pixels_squared > 1.0e-9) {
                const QPointF offset = event->position() - screen_origin;
                const double coordinate =
                    (offset.x() * axis.x() + offset.y() * axis.y()) /
                    pixels_squared;
                impl_->extent_update_callback(
                    impl_->dragged_extent_key, coordinate);
            }
        }
        event->accept();
        return;
    }
    if ((event->buttons() & Qt::LeftButton) && impl_->command_gesture_active) {
        if (impl_->command_gesture_update_callback) {
            const auto ray = ray_at(event->position());
            if (ray) impl_->command_gesture_update_callback(ray->first, ray->second);
        }
        return;
    }
    if ((event->buttons() & Qt::LeftButton) && impl_->drag_active) {
        if (impl_->drag_update_callback) {
            const auto ray = ray_at(event->position());
            if (ray) impl_->drag_update_callback(ray->first, ray->second);
        }
        return;
    }
    if (event->buttons() & Qt::MiddleButton) {
        if ((current - impl_->middle_press_position).manhattanLength() > 2) {
            impl_->middle_dragged = true;
        }
        if (event->buttons().testFlag(Qt::RightButton)) {
            impl_->pan_pixels += QPointF(movement.x(), movement.y());
        } else {
            constexpr float degrees_per_pixel = 0.22F;
            const auto horizontal = QQuaternion::fromAxisAndAngle(
                0.0F, 1.0F, 0.0F, movement.x() * degrees_per_pixel);
            const auto vertical = QQuaternion::fromAxisAndAngle(
                1.0F, 0.0F, 0.0F, movement.y() * degrees_per_pixel);
            impl_->orientation = (vertical * horizontal * impl_->orientation).normalized();
        }
        impl_->candidates.clear();
        update();
        return;
    }
    if (event->buttons() == Qt::NoButton && !impl_->confirmed_candidate) {
        std::string hovered;
        for (const auto& handle : impl_->extent_manipulators) {
            const double norm = std::sqrt(handle.direction.x * handle.direction.x +
                handle.direction.y * handle.direction.y +
                handle.direction.z * handle.direction.z);
            if (norm > 1.0e-12) {
                const zima::kernel::Vec3 direction{handle.direction.x / norm,
                    handle.direction.y / norm, handle.direction.z / norm};
                const auto project = [&](const zima::kernel::Vec3& point) {
                    QVector4D clip = (impl_->projection(width(), height()) * impl_->view()) *
                        QVector4D(point.x, point.y, point.z, 1.0F);
                    if (std::abs(clip.w()) > 1.0e-9F) clip /= clip.w();
                    return QPointF((clip.x() + 1.0F) * width() / 2.0F,
                        (1.0F - clip.y()) * height() / 2.0F);
                };
                const zima::kernel::Vec3 endpoint{
                    handle.origin.x + direction.x * handle.length,
                    handle.origin.y + direction.y * handle.length,
                    handle.origin.z + direction.z * handle.length};
                const double start_distance =
                    QLineF(event->position(), project(handle.origin)).length();
                const double end_distance = handle.point_only
                    ? std::numeric_limits<double>::infinity()
                    : QLineF(event->position(), project(endpoint)).length();
                if (std::min(start_distance, end_distance) <=
                        12.0 * devicePixelRatioF()) {
                    hovered = start_distance <= end_distance
                        ? handle.start_key : handle.end_key;
                }
            }
        }
        if (hovered != impl_->hovered_extent_key) {
            impl_->hovered_extent_key = std::move(hovered);
            update();
        }
        update_candidates(event->position());
        if (impl_->world_pointer_callback) {
            const auto ray = ray_at(event->position());
            if (ray) impl_->world_pointer_callback(ray->first, ray->second);
        }
    }
}

void MeshView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && impl_->sketch_box_start) {
        const QPointF start = *impl_->sketch_box_start;
        const QPointF end = event->position();
        const QRectF rectangle(start, end);
        const QRectF normalized = rectangle.normalized();
        const bool crossing = end.x() < start.x();
        const auto project = [&](const zima::kernel::Vec3& point) {
            QVector4D clip =
                (impl_->projection(width(), height()) * impl_->view()) *
                QVector4D(point.x, point.y, point.z, 1.0F);
            if (std::abs(clip.w()) > 1.0e-9F) clip /= clip.w();
            return QPointF((clip.x() + 1.0F) * width() / 2.0F,
                (1.0F - clip.y()) * height() / 2.0F);
        };
        const std::array borders{
            QLineF(normalized.topLeft(), normalized.topRight()),
            QLineF(normalized.topRight(), normalized.bottomRight()),
            QLineF(normalized.bottomRight(), normalized.bottomLeft()),
            QLineF(normalized.bottomLeft(), normalized.topLeft())};
        std::vector<ViewerCandidate> selected;
        if (normalized.width() >= 3.0 || normalized.height() >= 3.0) {
            for (std::size_t edge_index = 0;
                 edge_index < impl_->mesh.edges.size(); ++edge_index) {
                const auto& edge = impl_->mesh.edges[edge_index];
                if (!impl_->active_sketch_owner_id.empty() &&
                    edge.reference.owner_id !=
                        impl_->active_sketch_owner_id) continue;
                if (edge.points.empty()) continue;
                CandidateKind kind;
                if (edge.reference.semantic_key.starts_with("segment:"))
                    kind = CandidateKind::SketchSegment;
                else if (edge.reference.semantic_key.starts_with("circle:") ||
                         edge.reference.semantic_key.starts_with("arc:") ||
                         edge.reference.semantic_key.starts_with("ellipse:") ||
                         edge.reference.semantic_key.starts_with("elliptical_arc:") ||
                         edge.reference.semantic_key.starts_with("bspline:"))
                    kind = CandidateKind::SketchCurve;
                else if (edge.reference.semantic_key.starts_with("text:"))
                    kind = CandidateKind::SketchText;
                else
                    continue;
                std::vector<QPointF> screen;
                screen.reserve(edge.points.size());
                std::transform(edge.points.begin(), edge.points.end(),
                    std::back_inserter(screen), project);
                const bool contained = std::all_of(screen.begin(), screen.end(),
                    [&](const auto& point) { return normalized.contains(point); });
                bool crossed = crossing && std::any_of(screen.begin(), screen.end(),
                    [&](const auto& point) { return normalized.contains(point); });
                if (crossing && !crossed) {
                    for (std::size_t index = 1;
                         index < screen.size() && !crossed; ++index) {
                        const QLineF segment(screen[index - 1], screen[index]);
                        crossed = std::any_of(borders.begin(), borders.end(),
                            [&](const auto& border) {
                                return segment.intersects(border, nullptr) ==
                                    QLineF::BoundedIntersection;
                            });
                    }
                }
                if (contained || crossed) {
                    selected.push_back({kind, 0.0, edge_index,
                        edge.reference.owner_id,
                        edge.reference.semantic_key,
                        edge.reference.instance_path, CandidateGeometry::Display});
                }
            }
            for (std::size_t point_index = 0;
                 point_index < impl_->mesh.points.size(); ++point_index) {
                const auto& point = impl_->mesh.points[point_index];
                if (!impl_->active_sketch_owner_id.empty() &&
                    point.reference.owner_id !=
                        impl_->active_sketch_owner_id) continue;
                if (!point.reference.semantic_key.starts_with("point:")) continue;
                if (normalized.contains(project(point.position))) {
                    selected.push_back({CandidateKind::SketchPoint, 0.0, point_index,
                        point.reference.owner_id, point.reference.semantic_key,
                        point.reference.instance_path, CandidateGeometry::Display});
                }
            }
        }
        impl_->sketch_box_start.reset();
        impl_->sketch_box_end.reset();
        if (impl_->sketch_box_selection_callback) {
            if (!event->modifiers().testFlag(Qt::ShiftModifier)) {
                impl_->sketch_box_selected_candidates = selected;
            } else {
                for (const auto& candidate : selected) {
                    if (std::find(impl_->sketch_box_selected_candidates.begin(),
                                  impl_->sketch_box_selected_candidates.end(),
                                  candidate) ==
                        impl_->sketch_box_selected_candidates.end()) {
                        impl_->sketch_box_selected_candidates.push_back(candidate);
                    }
                }
            }
            impl_->sketch_box_selection_callback(std::move(selected),
                event->modifiers().testFlag(Qt::ShiftModifier));
        }
        event->accept();
        update();
        return;
    }
    if (event->button() == Qt::LeftButton &&
        !impl_->dragged_extent_key.empty()) {
        impl_->dragged_extent_key.clear();
        impl_->dragged_extent_manipulator.reset();
        if (impl_->extent_end_callback) impl_->extent_end_callback();
        event->accept();
        update();
        return;
    }
    if (event->button() == Qt::MiddleButton && impl_->middle_double_clicked) {
        impl_->middle_double_clicked = false;
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton && !impl_->middle_dragged) {
        impl_->last_pointer = event->position().toPoint();
        const bool handled = confirm_current_pointer();
        if (handled) {
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && impl_->command_gesture_active) {
        if (impl_->command_gesture_update_callback) {
            const auto ray = ray_at(event->position());
            if (ray) impl_->command_gesture_update_callback(ray->first, ray->second);
        }
        impl_->command_gesture_active = false;
        if (impl_->command_gesture_end_callback) {
            impl_->command_gesture_end_callback();
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && impl_->drag_active) {
        impl_->drag_active = false;
        if (impl_->drag_end_callback) impl_->drag_end_callback();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

void MeshView::wheelEvent(QWheelEvent* event) {
    const float steps = event->angleDelta().y() / 120.0F;
    const float old_scale = impl_->view_scale;
    impl_->view_scale *= std::pow(1.15F, steps);
    impl_->view_scale = std::clamp(
        impl_->view_scale, impl_->radius * 0.001F, impl_->radius * 1000.0F);
    if (old_scale > 0.0F) {
        const float zoom_ratio = old_scale / impl_->view_scale;
        const QPointF cursor = event->position();
        const QPointF center(width() * 0.5, height() * 0.5);
        impl_->pan_pixels = cursor - center -
            (cursor - center - impl_->pan_pixels) * zoom_ratio;
    }
    impl_->candidates.clear();
    impl_->rebuild_persisted_reference_mesh();
    update();
    event->accept();
}

}  // namespace zima::viewer
