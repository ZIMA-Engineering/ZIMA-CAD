#include <zima/viewer/mesh_view.hpp>
#include <zima/viewer/picking.hpp>

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPainter>
#include <QVector3D>
#include <QVector4D>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace zima::viewer {

struct MeshView::Impl {
    zima::kernel::ViewerMesh mesh;
    QOpenGLShaderProgram program;
    QOpenGLBuffer vertices{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer triangles{QOpenGLBuffer::IndexBuffer};
    QOpenGLBuffer lines{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject vertex_array;
    std::vector<std::pair<GLint, GLsizei>> line_ranges;
    std::vector<CandidateKind> allowed_kinds{CandidateKind::Container};
    std::function<bool(const ViewerCandidate&)> candidate_filter;
    std::vector<ViewerCandidate> candidates;
    std::size_t active_candidate{};
    std::optional<ViewerCandidate> confirmed_candidate;
    std::function<void(const ViewerCandidate&)> confirmation_callback;
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
    QPoint last_pointer;
    QVector3D center;
    QVector3D pan;
    float radius{1.0F};
    float view_scale{1.4F};
    float yaw{-0.7F};
    float pitch{0.5F};
    DisplayMode display_mode{DisplayMode::ShadedWithEdges};
    bool show_origins{true};
    bool show_points{true};
    bool show_axes{true};
    bool show_planes{true};
    bool show_sketches{true};
    bool gpu_dirty{true};

    [[nodiscard]] QVector3D direction() const {
        const float cp = std::cos(pitch);
        return QVector3D(cp * std::cos(yaw), cp * std::sin(yaw), std::sin(pitch));
    }

    [[nodiscard]] QMatrix4x4 view() const {
        const QVector3D target = center + pan;
        const QVector3D eye = target + direction() * std::max(radius * 4.0F, 4.0F);
        const QVector3D up = std::abs(direction().z()) > 0.99F
            ? QVector3D(0.0F, 1.0F, 0.0F) : QVector3D(0.0F, 0.0F, 1.0F);
        QMatrix4x4 result;
        result.lookAt(eye, target, up);
        return result;
    }

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
        doneCurrent();
    }
}

void MeshView::set_mesh(zima::kernel::ViewerMesh mesh, bool fit_view) {
    impl_->mesh = std::move(mesh);
    impl_->candidates.clear();
    impl_->confirmed_candidate.reset();
    impl_->gpu_dirty = true;
    if (fit_view) fit_all();
    update();
}

void MeshView::set_selection_contract(std::vector<CandidateKind> allowed_kinds) {
    impl_->allowed_kinds = std::move(allowed_kinds);
    impl_->candidate_filter = {};
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
    update();
}

std::optional<ViewerCandidate> MeshView::confirmed_candidate() const {
    return impl_->confirmed_candidate;
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

void MeshView::confirm_container(const std::string& owner_id) {
    auto candidate = container_candidate(impl_->mesh, owner_id);
    if (!candidate) {
        clear_selection();
        return;
    }
    impl_->confirmed_candidate = std::move(candidate);
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
    impl_->candidates.clear();
    update();
}

void MeshView::clear_selection() {
    impl_->confirmed_candidate.reset();
    impl_->candidates.clear();
    impl_->active_candidate = 0;
    update();
}

void MeshView::set_confirmation_callback(
    std::function<void(const ViewerCandidate&)> callback) {
    impl_->confirmation_callback = std::move(callback);
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
    for (const auto& edge : impl_->mesh.edges) {
        bounds.insert(bounds.end(), edge.points.begin(), edge.points.end());
    }
    for (const auto& point : impl_->mesh.points) bounds.push_back(point.position);
    for (const auto& dimension : impl_->mesh.dimensions) {
        bounds.push_back(dimension.witness_first);
        bounds.push_back(dimension.witness_second);
        bounds.push_back(dimension.line_first);
        bounds.push_back(dimension.line_second);
    }
    if (bounds.empty()) {
        impl_->center = {};
        impl_->radius = 1.0F;
        impl_->view_scale = 1.4F;
        impl_->pan = {};
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
    impl_->view_scale = impl_->radius * 1.25F;
    impl_->pan = {};
    update();
}

void MeshView::set_display_mode(DisplayMode mode) {
    impl_->display_mode = mode;
    update();
}

DisplayMode MeshView::display_mode() const { return impl_->display_mode; }

void MeshView::set_standard_view(StandardView view) {
    constexpr float pi = 3.14159265358979323846F;
    switch (view) {
        case StandardView::Isometric:
            impl_->yaw = -0.7F;
            impl_->pitch = 0.5F;
            break;
        case StandardView::Front:
            impl_->yaw = -pi / 2.0F;
            impl_->pitch = 0.0F;
            break;
        case StandardView::Back:
            impl_->yaw = pi / 2.0F;
            impl_->pitch = 0.0F;
            break;
        case StandardView::Left:
            impl_->yaw = pi;
            impl_->pitch = 0.0F;
            break;
        case StandardView::Right:
            impl_->yaw = 0.0F;
            impl_->pitch = 0.0F;
            break;
        case StandardView::Top:
            impl_->yaw = 0.0F;
            impl_->pitch = pi / 2.0F;
            break;
        case StandardView::Bottom:
            impl_->yaw = 0.0F;
            impl_->pitch = -pi / 2.0F;
            break;
    }
    impl_->pan = {};
    impl_->candidates.clear();
    update();
}

void MeshView::set_view_direction(const zima::kernel::Vec3& direction) {
    const double length = std::sqrt(direction.x * direction.x +
        direction.y * direction.y + direction.z * direction.z);
    if (!std::isfinite(length) || length <= 1.0e-12) {
        throw std::invalid_argument("View direction must be finite and non-zero");
    }
    const double x = direction.x / length;
    const double y = direction.y / length;
    const double z = std::clamp(direction.z / length, -1.0, 1.0);
    impl_->yaw = static_cast<float>(std::atan2(y, x));
    impl_->pitch = static_cast<float>(std::asin(z));
    impl_->pan = {};
    impl_->candidates.clear();
    update();
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
    upload_mesh();
    impl_->vertex_array.release();
}

void MeshView::resizeGL(int, int) {}

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
    for (const auto& edge : impl_->mesh.edges) {
        if (edge.overlay || edge.points.size() < 2) continue;
        const auto first = static_cast<GLint>(line_data.size() / 6);
        const auto count = static_cast<GLsizei>(edge.points.size());
        impl_->line_ranges.emplace_back(first, count);
        for (const auto& point : edge.points) {
            line_data.insert(line_data.end(), {
                static_cast<float>(point.x), static_cast<float>(point.y),
                static_cast<float>(point.z), 0.0F, 0.0F, 1.0F});
        }
    }
    impl_->lines.bind();
    impl_->lines.allocate(line_data.data(),
        static_cast<int>(line_data.size() * sizeof(float)));
    impl_->gpu_dirty = false;
}

void MeshView::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    impl_->vertex_array.bind();
    if (impl_->gpu_dirty) upload_mesh();
    if (!impl_->program.isLinked()) {
        impl_->vertex_array.release();
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
    const auto draw_lines = [&] {
        bind_attributes(impl_->lines);
        for (const auto& [first, count] : impl_->line_ranges) {
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
    switch (impl_->display_mode) {
        case DisplayMode::Wire:
            glDisable(GL_DEPTH_TEST);
            impl_->program.setUniformValue(
                "color", QVector4D(0.72F, 0.78F, 0.80F, 1.0F));
            draw_lines();
            glEnable(GL_DEPTH_TEST);
            break;
        case DisplayMode::HiddenEdges:
            depth_prepass();
            glDisable(GL_DEPTH_TEST);
            impl_->program.setUniformValue(
                "color", QVector4D(0.32F, 0.35F, 0.38F, 1.0F));
            draw_lines();
            glEnable(GL_DEPTH_TEST);
            impl_->program.setUniformValue(
                "color", QVector4D(0.80F, 0.84F, 0.86F, 1.0F));
            draw_visible_lines();
            break;
        case DisplayMode::NoHiddenEdges:
            depth_prepass();
            impl_->program.setUniformValue(
                "color", QVector4D(0.80F, 0.84F, 0.86F, 1.0F));
            draw_visible_lines();
            break;
        case DisplayMode::ShadedWithEdges:
            impl_->program.setUniformValue(
                "color", QVector4D(0.25F, 0.47F, 0.33F, 1.0F));
            draw_triangles();
            impl_->program.setUniformValue(
                "color", QVector4D(0.82F, 0.85F, 0.86F, 1.0F));
            draw_visible_lines();
            break;
        case DisplayMode::Shaded:
            impl_->program.setUniformValue(
                "color", QVector4D(0.25F, 0.47F, 0.33F, 1.0F));
            draw_triangles();
            break;
    }

    std::optional<ViewerCandidate> highlighted = impl_->confirmed_candidate;
    QVector4D highlight_color(0.12F, 0.86F, 0.94F, 1.0F);
    if (!highlighted && !impl_->candidates.empty()) {
        highlighted = impl_->candidates[impl_->active_candidate];
        highlight_color = QVector4D(1.0F, 0.55F, 0.05F, 1.0F);
    }
    if (highlighted && highlighted->geometry == CandidateGeometry::Display &&
        (highlighted->kind == CandidateKind::Occurrence ||
                        highlighted->kind == CandidateKind::Container ||
                        highlighted->kind == CandidateKind::Face)) {
        glDisable(GL_DEPTH_TEST);
        impl_->program.setUniformValue("color", highlight_color);
        bind_attributes(impl_->vertices);
        impl_->triangles.bind();
        for (std::size_t triangle = 0;
             triangle < impl_->mesh.triangle_references.size(); ++triangle) {
            const auto& reference = impl_->mesh.triangle_references[triangle];
            const bool matches = highlighted->kind == CandidateKind::Occurrence
                ? reference.instance_path == highlighted->instance_path
                : highlighted->kind == CandidateKind::Container
                ? reference.owner_id == highlighted->owner_id
                : reference.owner_id == highlighted->owner_id &&
                    reference.semantic_key == highlighted->semantic_key;
            if (!matches || reference.instance_path != highlighted->instance_path) continue;
            const auto byte_offset = triangle * 3 * sizeof(std::uint32_t);
            glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(byte_offset));
        }
        glEnable(GL_DEPTH_TEST);
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
    const bool axes_visible = impl_->show_axes || axes_selectable;
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
    const bool points_visible =
        ((impl_->show_points || points_selectable) && !impl_->mesh.points.empty()) ||
        external_points_visible;
    const bool planes_selectable = std::find(
        impl_->allowed_kinds.begin(), impl_->allowed_kinds.end(),
        CandidateKind::Face) != impl_->allowed_kinds.end();
    const bool planes_visible = (impl_->show_planes || planes_selectable) && std::any_of(
        impl_->mesh.edges.begin(), impl_->mesh.edges.end(), [](const auto& edge) {
            return edge.reference.semantic_key == "border";
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
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
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
                    painter.drawLine(project(edge.points[index - 1]), project(edge.points[index]));
                }
            }
        }
        if (planes_visible) {
            painter.setPen(QPen(QColor(90, 180, 225, 180), 1.4, Qt::DashLine));
            for (const auto& edge : impl_->mesh.edges) {
                if (edge.reference.semantic_key != "border") continue;
                for (std::size_t index = 1; index < edge.points.size(); ++index) {
                    painter.drawLine(project(edge.points[index - 1]), project(edge.points[index]));
                }
            }
        }
        if (points_visible) {
            for (const auto& point : impl_->mesh.points) {
                const bool external = point.reference.semantic_key.starts_with(
                    "external_point:");
                if ((!external && !impl_->show_points && !points_selectable) ||
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
                    painter.setPen(QPen(QColor(245, 205, 80), 1.5));
                    painter.setBrush(QColor(245, 205, 80));
                    painter.drawEllipse(project(point.position), 3.5, 3.5);
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
            painter.setPen(QPen(QColor(125, 125, 125), 1.5, Qt::DashLine));
            for (const auto& axis : impl_->mesh.axes) {
                const zima::kernel::Vec3 half{
                    axis.direction.x * axis.display_length * 0.5,
                    axis.direction.y * axis.display_length * 0.5,
                    axis.direction.z * axis.display_length * 0.5};
                painter.drawLine(
                    project({axis.point.x - half.x, axis.point.y - half.y,
                             axis.point.z - half.z}),
                    project({axis.point.x + half.x, axis.point.y + half.y,
                             axis.point.z + half.z}));
            }
        }
        if (highlighted) {
            const QColor color = impl_->confirmed_candidate
                ? QColor(30, 220, 240) : QColor(255, 140, 12);
            if (highlighted->geometry == CandidateGeometry::OriginalReference &&
                (highlighted->kind == CandidateKind::Occurrence ||
                 highlighted->kind == CandidateKind::Container ||
                 highlighted->kind == CandidateKind::Face)) {
                painter.setPen(QPen(color, 1.5));
                painter.setBrush(QColor(color.red(), color.green(), color.blue(), 70));
                const auto& references = impl_->mesh.original_references;
                for (std::size_t triangle = 0;
                     triangle < references.triangle_references.size(); ++triangle) {
                    const auto& reference = references.triangle_references[triangle];
                    const bool matches = highlighted->kind == CandidateKind::Occurrence
                        ? reference.instance_path == highlighted->instance_path
                        : highlighted->kind == CandidateKind::Container
                            ? reference.owner_id == highlighted->owner_id
                            : reference.owner_id == highlighted->owner_id &&
                              reference.semantic_key == highlighted->semantic_key;
                    if (!matches || reference.instance_path != highlighted->instance_path ||
                        triangle * 3 + 2 >= references.triangles.size()) continue;
                    const auto first = references.triangles[triangle * 3];
                    const auto second = references.triangles[triangle * 3 + 1];
                    const auto third = references.triangles[triangle * 3 + 2];
                    if (first >= references.vertices.size() ||
                        second >= references.vertices.size() ||
                        third >= references.vertices.size()) continue;
                    painter.drawPolygon(QPolygonF{
                        project(references.vertices[first]),
                        project(references.vertices[second]),
                        project(references.vertices[third])});
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
            } else if ((highlighted->kind == CandidateKind::Axis ||
                        highlighted->kind == CandidateKind::SketchAxis) &&
                       highlighted->geometry_index < selectable_axes.size()) {
                painter.setPen(QPen(color, 4.0, Qt::SolidLine, Qt::RoundCap));
                const auto& axis = selectable_axes[highlighted->geometry_index];
                const zima::kernel::Vec3 half{
                    axis.direction.x * axis.display_length * 0.5,
                    axis.direction.y * axis.display_length * 0.5,
                    axis.direction.z * axis.display_length * 0.5};
                painter.drawLine(
                    project({axis.point.x - half.x, axis.point.y - half.y,
                             axis.point.z - half.z}),
                    project({axis.point.x + half.x, axis.point.y + half.y,
                             axis.point.z + half.z}));
            }
        }
    }
}

void MeshView::update_candidates(const QPointF& position) {
    if (width() <= 0 || height() <= 0) return;
    const auto ray = ray_at(position);
    if (!ray) return;
    const auto& [ray_origin, ray_direction] = *ray;
    const double world_tolerance =
        8.0 * impl_->view_scale / std::max(height(), 1);
    auto next = filter_candidates(ordered_viewer_candidates(
        impl_->mesh, ray_origin, ray_direction, world_tolerance),
        impl_->allowed_kinds, impl_->candidate_filter);
    const bool same_order = next.size() == impl_->candidates.size() &&
        std::equal(next.begin(), next.end(), impl_->candidates.begin(),
            [](const ViewerCandidate& left, const ViewerCandidate& right) {
                return left.kind == right.kind && left.owner_id == right.owner_id &&
                    left.semantic_key == right.semantic_key &&
                    left.instance_path == right.instance_path;
            });
    impl_->candidates = std::move(next);
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
    if (event->button() == Qt::MiddleButton) {
        impl_->middle_dragged = false;
        impl_->middle_press_position = event->position().toPoint();
    }
    if (event->button() == Qt::LeftButton &&
        impl_->command_gesture_begin_callback) {
        update_candidates(event->position());
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
        notify_confirmation();
        if (impl_->confirmed_candidate && impl_->drag_begin_callback) {
            impl_->drag_active = impl_->drag_begin_callback(*impl_->confirmed_candidate);
        }
        update();
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
        if (event->modifiers() & Qt::ShiftModifier) {
            const QVector3D forward = -impl_->direction();
            const QVector3D right = QVector3D::crossProduct(forward, QVector3D(0, 0, 1)).normalized();
            const QVector3D up = QVector3D::crossProduct(right, forward).normalized();
            const float factor = 2.0F * impl_->view_scale / std::max(height(), 1);
            impl_->pan += right * (-movement.x() * factor) + up * (movement.y() * factor);
        } else {
            impl_->yaw += movement.x() * 0.01F;
            impl_->pitch = std::clamp(
                impl_->pitch + movement.y() * 0.01F, -1.5F, 1.5F);
        }
        impl_->candidates.clear();
        update();
        return;
    }
    if (event->buttons() == Qt::NoButton && !impl_->confirmed_candidate) {
        if (impl_->world_pointer_callback) {
            const auto ray = ray_at(event->position());
            if (ray) impl_->world_pointer_callback(ray->first, ray->second);
        }
        update_candidates(event->position());
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
    impl_->view_scale *= std::pow(0.82F, steps);
    impl_->view_scale = std::clamp(
        impl_->view_scale, impl_->radius * 0.001F, impl_->radius * 1000.0F);
    impl_->candidates.clear();
    update();
    event->accept();
}

}  // namespace zima::viewer
