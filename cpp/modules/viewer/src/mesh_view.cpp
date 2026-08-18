#include <zima/viewer/mesh_view.hpp>
#include <zima/viewer/picking.hpp>

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <QVector3D>
#include <QVector4D>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace zima::viewer {

struct MeshView::Impl {
    zima::kernel::ViewerMesh mesh;
    QOpenGLShaderProgram program;
    QOpenGLBuffer vertices{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer triangles{QOpenGLBuffer::IndexBuffer};
    QOpenGLBuffer lines{QOpenGLBuffer::IndexBuffer};
    std::vector<std::uint32_t> line_indices;
    std::vector<CandidateKind> allowed_kinds{CandidateKind::Container};
    std::vector<ViewerCandidate> candidates;
    std::size_t active_candidate{};
    std::optional<ViewerCandidate> confirmed_candidate;
    QPoint last_pointer;
    QVector3D center;
    QVector3D pan;
    float radius{1.0F};
    float view_scale{1.4F};
    float yaw{-0.7F};
    float pitch{0.5F};
    bool gpu_dirty{true};

    [[nodiscard]] QVector3D direction() const {
        const float cp = std::cos(pitch);
        return QVector3D(cp * std::cos(yaw), cp * std::sin(yaw), std::sin(pitch));
    }

    [[nodiscard]] QMatrix4x4 view() const {
        const QVector3D target = center + pan;
        const QVector3D eye = target + direction() * std::max(radius * 4.0F, 4.0F);
        QMatrix4x4 result;
        result.lookAt(eye, target, QVector3D(0.0F, 0.0F, 1.0F));
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
        impl_->vertices.destroy();
        impl_->triangles.destroy();
        impl_->lines.destroy();
        doneCurrent();
    }
}

void MeshView::set_mesh(zima::kernel::ViewerMesh mesh) {
    impl_->mesh = std::move(mesh);
    impl_->candidates.clear();
    impl_->confirmed_candidate.reset();
    impl_->gpu_dirty = true;
    fit_all();
    update();
}

void MeshView::set_selection_contract(std::vector<CandidateKind> allowed_kinds) {
    impl_->allowed_kinds = std::move(allowed_kinds);
    impl_->candidates.clear();
    impl_->active_candidate = 0;
    impl_->confirmed_candidate.reset();
    update();
}

std::optional<ViewerCandidate> MeshView::confirmed_candidate() const {
    return impl_->confirmed_candidate;
}

void MeshView::fit_all() {
    if (impl_->mesh.vertices.empty()) {
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
    for (const auto& point : impl_->mesh.vertices) {
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
    impl_->vertices.create();
    impl_->triangles.create();
    impl_->lines.create();
    upload_mesh();
}

void MeshView::resizeGL(int, int) {}

void MeshView::upload_mesh() {
    if (!isValid() || !impl_->gpu_dirty) return;
    std::vector<QVector3D> normals(impl_->mesh.vertices.size());
    impl_->line_indices.clear();
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
        impl_->line_indices.insert(impl_->line_indices.end(), {a, b, b, c, c, a});
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
    impl_->lines.bind();
    impl_->lines.allocate(impl_->line_indices.data(),
        static_cast<int>(impl_->line_indices.size() * sizeof(std::uint32_t)));
    impl_->gpu_dirty = false;
}

void MeshView::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (impl_->gpu_dirty) upload_mesh();
    if (impl_->mesh.vertices.empty() || !impl_->program.isLinked()) return;
    impl_->program.bind();
    const QMatrix4x4 view = impl_->view();
    impl_->program.setUniformValue("mvp",
        impl_->projection(width(), height()) * view);
    impl_->program.setUniformValue("normalMatrix", view.normalMatrix());
    impl_->vertices.bind();
    impl_->program.enableAttributeArray(0);
    impl_->program.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
    impl_->program.enableAttributeArray(1);
    impl_->program.setAttributeBuffer(
        1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));

    impl_->program.setUniformValue("color", QVector4D(0.26F, 0.55F, 0.63F, 1.0F));
    impl_->triangles.bind();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(impl_->mesh.triangles.size()),
                   GL_UNSIGNED_INT, nullptr);
    impl_->program.setUniformValue("color", QVector4D(0.72F, 0.78F, 0.80F, 1.0F));
    impl_->lines.bind();
    glDrawElements(GL_LINES, static_cast<GLsizei>(impl_->line_indices.size()),
                   GL_UNSIGNED_INT, nullptr);

    std::optional<ViewerCandidate> highlighted = impl_->confirmed_candidate;
    QVector4D highlight_color(0.12F, 0.86F, 0.94F, 1.0F);
    if (!highlighted && !impl_->candidates.empty()) {
        highlighted = impl_->candidates[impl_->active_candidate];
        highlight_color = QVector4D(1.0F, 0.55F, 0.05F, 1.0F);
    }
    if (highlighted && (highlighted->kind == CandidateKind::Container ||
                        highlighted->kind == CandidateKind::Face)) {
        glDisable(GL_DEPTH_TEST);
        impl_->program.setUniformValue("color", highlight_color);
        impl_->triangles.bind();
        for (std::size_t triangle = 0;
             triangle < impl_->mesh.triangle_references.size(); ++triangle) {
            const auto& reference = impl_->mesh.triangle_references[triangle];
            const bool matches = highlighted->kind == CandidateKind::Container
                ? reference.owner_id == highlighted->owner_id
                : reference.owner_id == highlighted->owner_id &&
                    reference.semantic_key == highlighted->semantic_key;
            if (!matches) continue;
            const auto byte_offset = triangle * 3 * sizeof(std::uint32_t);
            glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(byte_offset));
        }
        glEnable(GL_DEPTH_TEST);
    }
    impl_->program.disableAttributeArray(0);
    impl_->program.disableAttributeArray(1);
    impl_->program.release();

    if (highlighted && (highlighted->kind == CandidateKind::Edge ||
                        highlighted->kind == CandidateKind::Vertex)) {
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
        const QColor color = impl_->confirmed_candidate
            ? QColor(30, 220, 240) : QColor(255, 140, 12);
        if (highlighted->kind == CandidateKind::Edge &&
            highlighted->geometry_index < impl_->mesh.edges.size()) {
            painter.setPen(QPen(color, 4.0, Qt::SolidLine, Qt::RoundCap));
            const auto& edge = impl_->mesh.edges[highlighted->geometry_index];
            for (std::size_t index = 1; index < edge.points.size(); ++index) {
                painter.drawLine(project(edge.points[index - 1]), project(edge.points[index]));
            }
        } else if (highlighted->kind == CandidateKind::Vertex &&
                   highlighted->geometry_index < impl_->mesh.points.size()) {
            painter.setPen(QPen(color, 2.0));
            painter.setBrush(color);
            painter.drawEllipse(project(
                impl_->mesh.points[highlighted->geometry_index].position), 5.0, 5.0);
        }
    }
}

void MeshView::update_candidates(const QPointF& position) {
    if (impl_->mesh.vertices.empty() || width() <= 0 || height() <= 0) return;
    const float x = static_cast<float>(2.0 * position.x() / width() - 1.0);
    const float y = static_cast<float>(1.0 - 2.0 * position.y() / height());
    bool invertible = false;
    const QMatrix4x4 inverse =
        (impl_->projection(width(), height()) * impl_->view()).inverted(&invertible);
    if (!invertible) return;
    QVector4D near_point = inverse * QVector4D(x, y, -1.0F, 1.0F);
    QVector4D far_point = inverse * QVector4D(x, y, 1.0F, 1.0F);
    near_point /= near_point.w();
    far_point /= far_point.w();
    const QVector3D direction = (far_point.toVector3D() - near_point.toVector3D()).normalized();
    const double world_tolerance =
        8.0 * impl_->view_scale / std::max(height(), 1);
    auto next = filter_candidates(ordered_viewer_candidates(
        impl_->mesh,
        {near_point.x(), near_point.y(), near_point.z()},
        {direction.x(), direction.y(), direction.z()},
        world_tolerance), impl_->allowed_kinds);
    const bool same_order = next.size() == impl_->candidates.size() &&
        std::equal(next.begin(), next.end(), impl_->candidates.begin(),
            [](const ViewerCandidate& left, const ViewerCandidate& right) {
                return left.kind == right.kind && left.owner_id == right.owner_id &&
                    left.semantic_key == right.semantic_key;
            });
    impl_->candidates = std::move(next);
    if (!same_order || impl_->active_candidate >= impl_->candidates.size()) {
        impl_->active_candidate = 0;
    }
    update();
}

void MeshView::mousePressEvent(QMouseEvent* event) {
    impl_->last_pointer = event->position().toPoint();
    if (event->button() == Qt::LeftButton && !impl_->candidates.empty()) {
        impl_->confirmed_candidate = impl_->candidates[impl_->active_candidate];
        update();
    } else if (event->button() == Qt::RightButton &&
               !impl_->confirmed_candidate && impl_->candidates.size() > 1) {
        impl_->active_candidate =
            next_candidate_index(impl_->active_candidate, impl_->candidates.size());
        update();
    }
}

void MeshView::mouseMoveEvent(QMouseEvent* event) {
    const QPoint current = event->position().toPoint();
    const QPoint movement = current - impl_->last_pointer;
    impl_->last_pointer = current;
    if (event->buttons() & Qt::MiddleButton) {
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
        update_candidates(event->position());
    }
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
