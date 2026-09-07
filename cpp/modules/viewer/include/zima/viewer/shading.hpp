#pragma once

#include <zima/kernel/geometry_kernel.hpp>
#include <QVector3D>
#include <array>
#include <cmath>
#include <map>
#include <vector>

namespace zima::viewer {
// Render-only data derived from the persisted mesh; no kernel calculation.
inline std::vector<float> shaded_triangle_vertices(const kernel::ViewerMesh& mesh) {
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
    const std::size_t triangle_count = mesh.triangles.size() / 3;
    std::vector<QVector3D> triangle_normals(triangle_count);
    std::map<NormalPointKey, std::vector<std::size_t>> triangles_at_point;
    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        const auto a = mesh.triangles[triangle * 3];
        const auto b = mesh.triangles[triangle * 3 + 1];
        const auto c = mesh.triangles[triangle * 3 + 2];
        if (a >= mesh.vertices.size() || b >= mesh.vertices.size() ||
            c >= mesh.vertices.size()) continue;
        const auto& pa = mesh.vertices[a];
        const auto& pb = mesh.vertices[b];
        const auto& pc = mesh.vertices[c];
        QVector3D normal = QVector3D::crossProduct(
            QVector3D(pb.x - pa.x, pb.y - pa.y, pb.z - pa.z),
            QVector3D(pc.x - pa.x, pc.y - pa.y, pc.z - pa.z));
        if (normal.lengthSquared() > 1.0e-12F) normal.normalize();
        triangle_normals[triangle] = normal;
        for (const auto index : {a, b, c}) {
            triangles_at_point[normal_point_key(mesh.vertices[index])]
                .push_back(triangle);
        }
    }
    std::vector<float> vertex_data;
    vertex_data.reserve(mesh.triangles.size() * 6);
    constexpr float smooth_crease_cosine = 0.75F;
    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        const std::size_t offset = triangle * 3;
        const auto a = mesh.triangles[offset];
        const auto b = mesh.triangles[offset + 1];
        const auto c = mesh.triangles[offset + 2];
        if (a < mesh.vertices.size() && b < mesh.vertices.size() &&
            c < mesh.vertices.size()) {
            for (const auto index : {a, b, c}) {
                const auto& point = mesh.vertices[index];
                const QVector3D face_normal = triangle_normals[triangle];
                QVector3D normal;
                for (const auto adjacent_triangle : triangles_at_point[normal_point_key(point)]) {
                    // Referenced meshes can duplicate seam vertices, so match
                    // the persisted face and exact occurrence at this position.
                    // Result bodies have no selectable face references; their
                    // per-face vertex indices already delimit shading domains.
                    if (adjacent_triangle != triangle) {
                        const bool referenced = triangle < mesh.triangle_references.size() &&
                            mesh.triangle_references[triangle].valid();
                        const bool adjacent_referenced = adjacent_triangle < mesh.triangle_references.size() &&
                            mesh.triangle_references[adjacent_triangle].valid();
                        if (referenced != adjacent_referenced) continue;
                        if (referenced) {
                            if (!(mesh.triangle_references[triangle] ==
                                  mesh.triangle_references[adjacent_triangle])) continue;
                        } else {
                            const auto adjacent_offset = adjacent_triangle * 3;
                            if (mesh.triangles[adjacent_offset] != index &&
                                mesh.triangles[adjacent_offset + 1] != index &&
                                mesh.triangles[adjacent_offset + 2] != index) continue;
                        }
                    }
                    auto adjacent = triangle_normals[adjacent_triangle];
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

    return vertex_data;
}
} // namespace zima::viewer