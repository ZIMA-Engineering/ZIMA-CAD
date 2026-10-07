#pragma once

#include <zima/kernel/geometry_kernel.hpp>
#include <QVector3D>
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <utility>
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
    struct NormalPointHash {
        std::size_t operator()(const NormalPointKey& point) const noexcept {
            std::size_t value{};
            for (const auto coordinate : point)
                value ^= std::hash<long long>{}(coordinate) + 0x9e3779b9U +
                    (value << 6) + (value >> 2);
            return value;
        }
    };
    // This index is only queried by position, never iterated. Keep each
    // adjacency list in original triangle order so normal sums stay identical.
    std::unordered_map<NormalPointKey, std::vector<std::pair<std::size_t,float>>, NormalPointHash> triangles_at_point;
    triangles_at_point.reserve(std::min(mesh.vertices.size(), mesh.triangles.size()));
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
        const std::array indices{a,b,c};
        for (std::size_t corner=0;corner<indices.size();++corner) {
            const auto index=indices[corner];
            const auto& point=mesh.vertices[index];
            const auto& next=mesh.vertices[indices[(corner+1)%3]];
            const auto& previous=mesh.vertices[indices[(corner+2)%3]];
            const QVector3D u(next.x-point.x,next.y-point.y,next.z-point.z);
            const QVector3D v(previous.x-point.x,previous.y-point.y,previous.z-point.z);
            const float angle=std::atan2(QVector3D::crossProduct(u,v).length(),QVector3D::dotProduct(u,v));
            triangles_at_point[normal_point_key(mesh.vertices[index])]
                .emplace_back(triangle,angle);
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
                const bool referenced = triangle < mesh.triangle_references.size() &&
                    mesh.triangle_references[triangle].valid();
                auto orientation_normal=face_normal;
                if(referenced) {
                    // Use one hemisphere for this whole same-face vertex fan.
                    // A triangle-local angular cutoff gives shared vertices
                    // different normals on coarsely triangulated small radii.
                    for(const auto& [adjacent_triangle,angle]:triangles_at_point[normal_point_key(point)]) {
                        if(adjacent_triangle<mesh.triangle_references.size()&&
                           mesh.triangle_references[triangle]==mesh.triangle_references[adjacent_triangle]&&
                           triangle_normals[adjacent_triangle].lengthSquared()>1.0e-12F) {
                            orientation_normal=triangle_normals[adjacent_triangle];break;
                        }
                    }
                }
                QVector3D normal;
                for (const auto& [adjacent_triangle,corner_angle] : triangles_at_point[normal_point_key(point)]) {
                    // Referenced meshes can duplicate seam vertices, so match
                    // the persisted face and exact occurrence at this position.
                    // Result bodies have no selectable face references; their
                    // per-face vertex indices already delimit shading domains.
                    if (adjacent_triangle != triangle) {
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
                        orientation_normal, adjacent);
                    if (!referenced && std::abs(alignment) < smooth_crease_cosine) continue;
                    // Mirrored/reversed tessellation is not a geometric
                    // crease. Align its hemisphere before averaging.
                    if (alignment < 0.0F) adjacent = -adjacent;
                    // A subdivided fan must not gain influence just because
                    // it contains more triangles around this vertex.
                    normal += adjacent*corner_angle;
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
