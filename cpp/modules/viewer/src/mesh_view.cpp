#include <zima/viewer/mesh_view.hpp>
#include <zima/viewer/picking.hpp>

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPainter>
#include <QQuaternion>
#include <QEasingCurve>
#include <QVariantAnimation>
#include <QVector3D>
#include <QVector4D>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
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
    std::function<bool(const ViewerCandidate&)> drag_begin_callback;
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
    bool middle_dragged{};
    QPoint middle_press_position;
    std::vector<zima::kernel::ViewerEdge> transient_edges;
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
    std::set<std::string> constraint_reference_owner_ids;
    std::set<EdgeKey> constraint_reference_edges;
    std::set<EdgeKey> assembly_reference_edges;
    std::set<std::string> selected_container_content_ids;
    std::set<EdgeKey> object_overlay_main_edge_keys;
    std::optional<QColor> edge_color_override;
    QPoint last_pointer;
    QVector3D center;
    QPointF pan_pixels;
    float radius{1.0F};
    float view_scale{1.4F};
    float reference_view_scale{1.4F};
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
        target.edges = mesh.original_references.edges;
        target.points = mesh.original_references.points;
        target.axes = mesh.original_references.axes;
        const double scale = view_scale /
            std::max(reference_view_scale, 1.0e-6F);
        for (auto& axis : target.axes) {
            if (axis.reference.semantic_key.starts_with("origin:axis:")) {
                axis.display_length *= scale;
            }
        }
        // Keep persisted solid faces unchanged. Origin planes are screen-size
        // overlays; rebuild their pick triangles from the exact scaled border
        // which paintGL displays, so hover and click cannot be spatially offset.
        for (std::size_t triangle = 0;
             triangle < mesh.original_references.triangle_references.size();
             ++triangle) {
            const auto& reference =
                mesh.original_references.triangle_references[triangle];
            if (reference.semantic_key.starts_with("origin:plane:")) continue;
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
            if (!edge.reference.semantic_key.starts_with("origin:plane:") ||
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
                edge.reference.owner_id, edge.reference.semantic_key,
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

void MeshView::set_candidate_filter(
    std::function<bool(const ViewerCandidate&)> candidate_filter) {
    impl_->candidate_filter = std::move(candidate_filter);
    impl_->candidates.clear();
    impl_->active_candidate = 0;
    impl_->confirmed_candidate.reset();
    impl_->selected_container_origin_id.clear();
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
    return filter_candidates(ordered_viewer_candidates(
        impl_->mesh, impl_->persisted_reference_mesh,
        ray_origin, ray_direction, world_tolerance),
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

std::optional<zima::kernel::Vec3> MeshView::candidate_face_normal(
    const ViewerCandidate& candidate) const {
    if (candidate.kind != CandidateKind::Face) return std::nullopt;
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
    impl_->selected_container_origin_id =
        impl_->confirmed_candidate->semantic_key == "point"
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
    if (kind == CandidateKind::Vertex) {
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
    } else if (kind == CandidateKind::Face) {
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

void MeshView::set_double_confirmation_callback(
    std::function<void(const ViewerCandidate&)> callback) {
    impl_->double_confirmation_callback = std::move(callback);
}

void MeshView::set_candidate_drag_callbacks(
    std::function<bool(const ViewerCandidate&)> begin,
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
    update();
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

void MeshView::set_transient_point_transform(
    std::function<zima::kernel::Vec3(const zima::kernel::Vec3&)> transform) {
    impl_->transient_point_transform = std::move(transform);
    impl_->transient_edges.clear();
    update();
}

double MeshView::world_tolerance_for_pixels(double pixels) const {
    if (!std::isfinite(pixels) || pixels < 0.0 || height() <= 0) return 0.0;
    return 2.0 * static_cast<double>(impl_->view_scale) * pixels /
        static_cast<double>(height());
}

void MeshView::notify_confirmation() {
    if (impl_->confirmed_candidate && impl_->confirmation_callback) {
        impl_->confirmation_callback(*impl_->confirmed_candidate);
    }
}

void MeshView::fit_all() {
    std::vector<zima::kernel::Vec3> bounds = impl_->mesh.vertices;
    std::vector<zima::kernel::Vec3> reference_centers;
    double reference_extent = 0.5;
    for (const auto& edge : impl_->mesh.edges) {
        if (edge.reference.semantic_key.starts_with("origin:plane:")) {
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
        if (point.reference.semantic_key == "origin:point") {
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
    if (bounds.empty() && reference_centers.empty()) {
        impl_->center = {};
        impl_->radius = 1.0F;
        impl_->view_scale = 1.4F;
        impl_->reference_view_scale = 1.4F;
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
        impl_->radius = static_cast<float>(std::max(reference_extent, 0.5) * 2.0);
        impl_->view_scale = impl_->radius;
        impl_->reference_view_scale = impl_->radius;
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
    impl_->view_scale = bounds.size() == 1
        ? impl_->reference_view_scale : impl_->radius;
    // Matches Python's fit_all() resetting camera.zoom to 1.0: at fit time
    // the screen-constant reference scale (view_scale / reference_view_scale)
    // must be exactly 1, so origin axes/planes render at their true stored
    // size instead of being skewed by an unrelated reference_extent ratio.
    impl_->reference_view_scale = impl_->view_scale;
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
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
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
        "in vec3 viewNormal;\n"
        "out vec4 fragmentColor;\n"
        "void main(){ float light = 0.35 + 0.65 * abs(dot(normalize(viewNormal),"
        " normalize(vec3(0.35, 0.45, 1.0))));"
        " fragmentColor = vec4(color.rgb * light, color.a); }\n");
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
    std::vector<QVector3D> normals(impl_->mesh.vertices.size());
    for (std::size_t offset = 0; offset + 2 < impl_->mesh.triangles.size(); offset += 3) {
        const auto a = impl_->mesh.triangles[offset];
        const auto b = impl_->mesh.triangles[offset + 1];
        const auto c = impl_->mesh.triangles[offset + 2];
        if (a < impl_->mesh.vertices.size() && b < impl_->mesh.vertices.size() &&
            c < impl_->mesh.vertices.size()) {
            const auto& pa = impl_->mesh.vertices[a];
            const auto& pb = impl_->mesh.vertices[b];
            const auto& pc = impl_->mesh.vertices[c];
            const QVector3D edge_one(
                pb.x - pa.x, pb.y - pa.y, pb.z - pa.z);
            const QVector3D edge_two(
                pc.x - pa.x, pc.y - pa.y, pc.z - pa.z);
            const QVector3D normal = QVector3D::crossProduct(edge_one, edge_two);
            normals[a] += normal;
            normals[b] += normal;
            normals[c] += normal;
        }
    }
    std::vector<float> vertex_data;
    vertex_data.reserve(impl_->mesh.vertices.size() * 6);
    for (std::size_t index = 0; index < impl_->mesh.vertices.size(); ++index) {
        const auto& point = impl_->mesh.vertices[index];
        const QVector3D normal = normals[index].normalized();
        vertex_data.insert(vertex_data.end(), {
            static_cast<float>(point.x), static_cast<float>(point.y),
            static_cast<float>(point.z), normal.x(), normal.y(), normal.z()});
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
        if (edge.overlay || edge.points.size() < 2) continue;
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
                const auto plane_point = [&](const auto& point) {
                    return zima::kernel::Vec3{
                        center.x + (point.x - center.x) * reference_scale,
                        center.y + (point.y - center.y) * reference_scale,
                        center.z + (point.z - center.z) * reference_scale};
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
        bind_attributes(impl_->vertices);
        impl_->triangles.bind();
        glDrawElements(GL_TRIANGLES,
            static_cast<GLsizei>(impl_->mesh.triangles.size()),
            GL_UNSIGNED_INT, nullptr);
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
            impl_->program.setUniformValue(
                "color", QVector4D(0.7255F, 0.7608F, 0.8000F, 1.0F));
            draw_triangles();
            draw_visible_lines();
            break;
        case DisplayMode::Shaded:
            impl_->program.setUniformValue(
                "color", QVector4D(0.7255F, 0.7608F, 0.8000F, 1.0F));
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
    const bool points_visible =
        ((impl_->show_points || points_selectable || point_containers_selectable) &&
         !impl_->mesh.points.empty()) ||
        external_points_visible || impl_->show_origins ||
        impl_->editing_origin_visible;
    const bool planes_selectable = std::find(
        impl_->allowed_kinds.begin(), impl_->allowed_kinds.end(),
        CandidateKind::Face) != impl_->allowed_kinds.end();
    const bool planes_visible = (impl_->show_planes || planes_selectable ||
        impl_->editing_origin_visible) && std::any_of(
        impl_->mesh.edges.begin(), impl_->mesh.edges.end(), [](const auto& edge) {
            return edge.reference.semantic_key == "border" ||
                edge.reference.semantic_key.starts_with("origin:plane:");
        });
    const bool dimensions_visible = !impl_->mesh.dimensions.empty();
    if (axes_visible || points_visible || planes_visible ||
        sketch_geometry_visible || dimensions_visible ||
        !impl_->transient_edges.empty() ||
        (highlighted && (
            highlighted->kind == CandidateKind::Edge ||
            highlighted->kind == CandidateKind::SketchSegment ||
            highlighted->kind == CandidateKind::SketchCurve ||
            highlighted->kind == CandidateKind::SketchText ||
            highlighted->kind == CandidateKind::SketchExternalReference ||
            highlighted->kind == CandidateKind::SketchTrimPiece ||
            highlighted->kind == CandidateKind::Vertex ||
            highlighted->kind == CandidateKind::SketchPoint ||
            highlighted->kind == CandidateKind::Dimension ||
            highlighted->kind == CandidateKind::Axis ||
            highlighted->kind == CandidateKind::SketchAxis))) {
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
                        ? QColor(235, 235, 235) : QColor(77, 216, 17);
                const QColor external_color = edge.reference.semantic_key.ends_with(
                    ":broken") ? QColor(179, 74, 60) : QColor(145, 105, 72);
                painter.setPen(text ? QPen(text_color, 1.8)
                    : (external || external_face)
                        ? QPen(external_color, 1.5, Qt::DashLine)
                    : edge.construction
                        ? QPen(QColor(105, 175, 240), 1.5, Qt::DashLine)
                        : QPen(QColor(220, 220, 220), 1.8));
                for (std::size_t index = 1; index < edge.points.size(); ++index) {
                    painter.drawLine(project(edge.points[index - 1]),
                                     project(edge.points[index]));
                }
            }
        }
        if (planes_visible) {
            painter.setPen(QPen(QColor(173, 110, 46), 1.5));
            for (const auto& edge : impl_->mesh.edges) {
                const bool origin = edge.reference.semantic_key.starts_with(
                    "origin:plane:");
                if (edge.reference.semantic_key != "border" && !origin) continue;
                if ((origin && !impl_->show_planes && !planes_selectable &&
                        !impl_->editing_origin_visible) ||
                    (!origin && !impl_->show_planes && !planes_selectable)) continue;
                const bool exact_highlight = highlighted &&
                    ((highlighted->kind == CandidateKind::Face &&
                      highlighted->owner_id == edge.reference.owner_id &&
                      highlighted->semantic_key == edge.reference.semantic_key) ||
                     (highlighted->kind == CandidateKind::Container &&
                      highlighted->semantic_key == "plane" &&
                      highlighted->owner_id + ":entity" ==
                          edge.reference.owner_id)) &&
                    highlighted->instance_path == edge.reference.instance_path;
                const QColor plane_color = exact_highlight
                    ? (impl_->confirmed_candidate ? QColor(30, 220, 240)
                                                  : QColor(255, 140, 12))
                    : origin ? QColor(173, 110, 46) : QColor(90, 180, 225);
                zima::kernel::Vec3 center;
                const std::size_t corner_count = origin && edge.points.size() > 1
                    ? edge.points.size() - 1 : edge.points.size();
                for (std::size_t index = 0; index < corner_count; ++index) {
                    const auto& point = edge.points[index];
                    center.x += point.x; center.y += point.y; center.z += point.z;
                }
                if (corner_count != 0) {
                    center.x /= corner_count; center.y /= corner_count;
                    center.z /= corner_count;
                }
                const auto plane_point = [&](const auto& point) {
                    if (!origin) return point;
                    return zima::kernel::Vec3{
                        center.x + (point.x - center.x) * reference_scale,
                        center.y + (point.y - center.y) * reference_scale,
                        center.z + (point.z - center.z) * reference_scale};
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
        if (points_visible) {
            for (const auto& point : impl_->mesh.points) {
                const bool external = point.reference.semantic_key.starts_with(
                    "external_point:");
                const bool origin = point.reference.semantic_key == "origin:point";
                if (origin) continue;
                if ((origin && !impl_->show_origins && !points_selectable) ||
                    (!origin && !external && !impl_->show_points &&
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
                    const bool selected = point.reference.owner_id ==
                            impl_->selected_container_origin_id ||
                        (impl_->confirmed_candidate &&
                         impl_->confirmed_candidate->kind == CandidateKind::Vertex &&
                         impl_->confirmed_candidate->owner_id ==
                            point.reference.owner_id &&
                         impl_->confirmed_candidate->semantic_key ==
                            point.reference.semantic_key &&
                         impl_->confirmed_candidate->instance_path ==
                            point.reference.instance_path);
                    const bool hovered = !impl_->confirmed_candidate && highlighted &&
                        ((highlighted->kind == CandidateKind::Container &&
                          highlighted->semantic_key == "point" &&
                          point.reference.owner_id ==
                            highlighted->owner_id + ":origin") ||
                         (highlighted->kind == CandidateKind::Vertex &&
                          highlighted->semantic_key == point.reference.semantic_key &&
                          highlighted->owner_id == point.reference.owner_id)) &&
                        point.reference.instance_path == highlighted->instance_path;
                    const QColor marker_color = selected
                        ? QColor(30, 220, 240)
                        : hovered ? QColor(255, 122, 0) : QColor(0, 0, 0);
                    painter.setPen(QPen(marker_color, 1.0));
                    painter.setBrush(marker_color);
                    const QPointF center = project(point.position);
                    draw_circular_marker(painter, center, marker_color);
                    if (!point.label.empty()) {
                        painter.setPen(QPen(marker_color, 1.0));
                        painter.drawText(center + QPointF(8.0, -6.0),
                                         QString::fromStdString(point.label));
                    }
                }
            }
        }
        if (!impl_->transient_edges.empty()) {
            painter.setPen(QPen(QColor(255, 140, 12), 2.0, Qt::DashLine));
            for (const auto& edge : impl_->transient_edges) {
                for (std::size_t index = 1; index < edge.points.size(); ++index) {
                    painter.drawLine(project(edge.points[index - 1]), project(edge.points[index]));
                }
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
                painter.drawLine(project(dimension.witness_first), project(dimension.line_first));
                painter.drawLine(project(dimension.witness_second), project(dimension.line_second));
                painter.drawLine(project(dimension.line_first), project(dimension.line_second));
                const QPointF middle =
                    (project(dimension.line_first) + project(dimension.line_second)) * 0.5;
                painter.drawText(middle + QPointF(4.0, -4.0),
                    QString::fromStdString(dimension.label_prefix) +
                    QString::number(dimension.value, 'f', 3) +
                    QString::fromStdString(dimension.unit_suffix));
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
                    ((highlighted->kind == CandidateKind::Axis &&
                      highlighted->owner_id == axis.reference.owner_id &&
                      highlighted->semantic_key == axis.reference.semantic_key) ||
                     (highlighted->kind == CandidateKind::Container &&
                      highlighted->semantic_key == "axis" &&
                      highlighted->owner_id + ":entity" ==
                          axis.reference.owner_id)) &&
                    highlighted->instance_path == axis.reference.instance_path;
                const QColor color = exact_highlight
                    ? (impl_->confirmed_candidate ? QColor(30, 220, 240)
                                                  : QColor(255, 140, 12))
                    : axis.reference.semantic_key == "origin:axis:x"
                        ? QColor(232, 76, 61)
                    : axis.reference.semantic_key == "origin:axis:y"
                        ? QColor(46, 204, 112)
                    : axis.reference.semantic_key == "origin:axis:z"
                        ? QColor(51, 153, 219) : QColor(150, 150, 150);
                const QColor presentation_color = !origin && !exact_highlight
                    ? QColor(90, 180, 225) : color;
                painter.setPen(QPen(presentation_color, origin ? 2.0 : 1.5,
                    origin ? Qt::SolidLine : Qt::DashDotLine));
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
                    painter.drawLine(start, end);
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
        if (impl_->show_origins || points_selectable ||
            impl_->editing_origin_visible) {
            for (const auto& point : impl_->mesh.points) {
                if (point.reference.semantic_key != "origin:point") continue;
                const QPointF center = project(point.position);
                painter.setPen(QPen(QColor(0, 0, 0), 1.0));
                painter.setBrush(QColor(0, 0, 0));
                draw_circular_marker(painter, center, QColor(0, 0, 0));
                if (!point.label.empty()) {
                    painter.drawText(center + QPointF(8.5, -6.5),
                                     QString::fromStdString(point.label));
                }
            }
        }
        if (highlighted) {
            const QColor color = impl_->confirmed_candidate
                ? QColor(30, 220, 240) : QColor(255, 140, 12);
            const bool origin_group = highlighted->kind == CandidateKind::Container &&
                highlighted->semantic_key == "origin";
            const bool origin_plane = highlighted->kind == CandidateKind::Face &&
                highlighted->semantic_key.starts_with("origin:plane:");
            if (origin_plane) {
                const auto edge = std::find_if(impl_->mesh.edges.begin(),
                    impl_->mesh.edges.end(), [&](const auto& candidate) {
                        return candidate.reference.owner_id == highlighted->owner_id &&
                            candidate.reference.semantic_key == highlighted->semantic_key &&
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
                    const auto display_point = [&](const auto& point) {
                        return zima::kernel::Vec3{
                            center.x + (point.x - center.x) * reference_scale,
                            center.y + (point.y - center.y) * reference_scale,
                            center.z + (point.z - center.z) * reference_scale};
                    };
                    for (std::size_t index = 1; index < edge->points.size(); ++index) {
                        draw_reference_segment(project(display_point(edge->points[index - 1])),
                            project(display_point(edge->points[index])), color, 1.5);
                    }
                }
            }
            if ((highlighted->kind == CandidateKind::Occurrence ||
                 (highlighted->kind == CandidateKind::Container && !origin_group) ||
                 (highlighted->kind == CandidateKind::Face && !origin_plane))) {
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
                for (std::size_t triangle = 0;
                     triangle < triangle_references.size(); ++triangle) {
                    const auto& reference = triangle_references[triangle];
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
                    painter.drawPolygon(QPolygonF{
                        project(vertices[first]), project(vertices[second]),
                        project(vertices[third])});
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
                painter.setPen(QPen(color, 4.0, Qt::SolidLine, Qt::RoundCap));
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
            if (origin_group) {
                for (const auto& edge : impl_->mesh.edges) {
                    if (edge.reference.owner_id != highlighted->owner_id ||
                        edge.reference.instance_path != highlighted->instance_path ||
                        !edge.reference.semantic_key.starts_with("origin:plane:") ||
                        edge.points.size() < 2) continue;
                    const std::size_t corner_count = edge.points.size() - 1;
                    zima::kernel::Vec3 center;
                    for (std::size_t index = 0; index < corner_count; ++index) {
                        center.x += edge.points[index].x;
                        center.y += edge.points[index].y;
                        center.z += edge.points[index].z;
                    }
                    center.x /= corner_count; center.y /= corner_count;
                    center.z /= corner_count;
                    const auto display_point = [&](const auto& point) {
                        return zima::kernel::Vec3{
                            center.x + (point.x - center.x) * reference_scale,
                            center.y + (point.y - center.y) * reference_scale,
                            center.z + (point.z - center.z) * reference_scale};
                    };
                    for (std::size_t index = 1; index < edge.points.size(); ++index) {
                        draw_reference_segment(project(display_point(edge.points[index - 1])),
                            project(display_point(edge.points[index])), color, 2.5);
                    }
                }
                for (const auto& axis : impl_->mesh.axes) {
                    if (axis.reference.owner_id != highlighted->owner_id ||
                        axis.reference.instance_path != highlighted->instance_path ||
                        !axis.reference.semantic_key.starts_with("origin:axis:")) continue;
                    painter.setPen(QPen(color, 3.0, Qt::SolidLine, Qt::RoundCap));
                    painter.drawLine(project(axis.point), project({
                        axis.point.x + axis.direction.x * axis.display_length * reference_scale,
                        axis.point.y + axis.direction.y * axis.display_length * reference_scale,
                        axis.point.z + axis.direction.z * axis.display_length * reference_scale}));
                }
            }
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
    if (event->button() == Qt::LeftButton && impl_->world_click_callback) {
        const auto ray = ray_at(event->position());
        if (ray && impl_->world_click_callback(ray->first, ray->second)) {
            impl_->candidates.clear();
            update();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && !impl_->candidates.empty()) {
        impl_->confirmed_candidate = impl_->candidates[impl_->active_candidate];
        impl_->selected_container_origin_id =
            impl_->confirmed_candidate->kind == CandidateKind::Container &&
                impl_->confirmed_candidate->semantic_key == "point"
            ? impl_->confirmed_candidate->owner_id + ":origin"
            : std::string{};
        notify_confirmation();
        if (impl_->confirmed_candidate && impl_->drag_begin_callback) {
            impl_->drag_active = impl_->drag_begin_callback(*impl_->confirmed_candidate);
        }
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
        } else if (impl_->candidates.size() > 1) {
            impl_->active_candidate =
                next_candidate_index(impl_->active_candidate, impl_->candidates.size());
            update();
        }
    }
}

void MeshView::mouseDoubleClickEvent(QMouseEvent* event) {
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
        update_candidates(event->position());
        if (impl_->world_pointer_callback) {
            const auto ray = ray_at(event->position());
            if (ray) impl_->world_pointer_callback(ray->first, ray->second);
        }
    }
}

void MeshView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton && !impl_->middle_dragged) {
        bool handled = impl_->short_middle_click_callback &&
            impl_->short_middle_click_callback();
        if (!handled && impl_->world_click_callback) {
            const auto ray = ray_at(event->position());
            handled = ray && impl_->world_click_callback(ray->first, ray->second);
        }
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
