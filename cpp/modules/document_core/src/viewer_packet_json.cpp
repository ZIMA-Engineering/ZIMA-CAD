#include <zima/document/viewer_packet_json.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <stdexcept>

namespace zima::document {
namespace {

void require_finite(double value, const char* field) {
    if (!std::isfinite(value)) {
        throw std::runtime_error(std::string(field) + " must be finite");
    }
}

nlohmann::json serialize_vec3(const zima::kernel::Vec3& point) {
    return nlohmann::json::array({point.x, point.y, point.z});
}

zima::kernel::Vec3 load_vec3(const nlohmann::json& source) {
    if (!source.is_array() || source.size() != 3) {
        throw std::runtime_error("Viewer point must contain three coordinates");
    }
    zima::kernel::Vec3 point{
        source.at(0).get<double>(), source.at(1).get<double>(),
        source.at(2).get<double>()};
    require_finite(point.x, "viewer x");
    require_finite(point.y, "viewer y");
    require_finite(point.z, "viewer z");
    return point;
}

}  // namespace

nlohmann::json serialize_body_result(const zima::kernel::BodyResult& result) {
    nlohmann::json vertices = nlohmann::json::array();
    for (const auto& point : result.mesh.vertices) vertices.push_back(serialize_vec3(point));
    nlohmann::json faces = nlohmann::json::array();
    for (const auto& reference : result.mesh.triangle_references) {
        faces.push_back({
            {"owner", reference.owner_id}, {"key", reference.semantic_key},
            {"instance_path", reference.instance_path}});
    }
    nlohmann::json edges = nlohmann::json::array();
    for (const auto& edge : result.mesh.edges) {
        nlohmann::json points = nlohmann::json::array();
        for (const auto& point : edge.points) points.push_back(serialize_vec3(point));
        edges.push_back({
            {"owner", edge.reference.owner_id}, {"key", edge.reference.semantic_key},
            {"instance_path", edge.reference.instance_path},
            {"points", std::move(points)},
        });
    }
    nlohmann::json points = nlohmann::json::array();
    for (const auto& point : result.mesh.points) {
        points.push_back({
            {"owner", point.reference.owner_id}, {"key", point.reference.semantic_key},
            {"instance_path", point.reference.instance_path},
            {"position", serialize_vec3(point.position)},
        });
    }
    return {
        {"volume", result.volume}, {"surface_area", result.surface_area},
        {"source_fingerprint", result.source_fingerprint},
        {"vertices", std::move(vertices)}, {"triangles", result.mesh.triangles},
        {"triangle_references", std::move(faces)},
        {"edges", std::move(edges)}, {"points", std::move(points)},
    };
}

zima::kernel::BodyResult load_body_result(const nlohmann::json& source) {
    zima::kernel::BodyResult result;
    result.volume = source.at("volume").get<double>();
    result.surface_area = source.at("surface_area").get<double>();
    result.source_fingerprint = source.at("source_fingerprint").get<std::string>();
    require_finite(result.volume, "calculated volume");
    require_finite(result.surface_area, "calculated surface area");
    for (const auto& point : source.at("vertices")) {
        result.mesh.vertices.push_back(load_vec3(point));
    }
    result.mesh.triangles = source.at("triangles").get<std::vector<std::uint32_t>>();
    for (const auto index : result.mesh.triangles) {
        if (index >= result.mesh.vertices.size()) {
            throw std::runtime_error("Viewer triangle index is out of range");
        }
    }
    for (const auto& reference : source.at("triangle_references")) {
        result.mesh.triangle_references.push_back({
            reference.at("owner").get<std::string>(),
            reference.at("key").get<std::string>(),
            reference.at("instance_path").get<std::string>(),
        });
    }
    if (result.mesh.triangles.size() % 3 != 0 ||
        result.mesh.triangle_references.size() != result.mesh.triangles.size() / 3) {
        throw std::runtime_error("Viewer triangle references are not aligned");
    }
    for (const auto& edge : source.at("edges")) {
        zima::kernel::ViewerEdge loaded;
        loaded.reference = {
            edge.at("owner").get<std::string>(), edge.at("key").get<std::string>(),
            edge.at("instance_path").get<std::string>()};
        for (const auto& point : edge.at("points")) loaded.points.push_back(load_vec3(point));
        if (!loaded.reference.valid() || loaded.points.size() < 2) {
            throw std::runtime_error("Persisted viewer edge is invalid");
        }
        result.mesh.edges.push_back(std::move(loaded));
    }
    for (const auto& point : source.at("points")) {
        zima::kernel::ViewerPoint loaded;
        loaded.reference = {
            point.at("owner").get<std::string>(), point.at("key").get<std::string>(),
            point.at("instance_path").get<std::string>()};
        loaded.position = load_vec3(point.at("position"));
        if (!loaded.reference.valid()) {
            throw std::runtime_error("Persisted viewer point is invalid");
        }
        result.mesh.points.push_back(std::move(loaded));
    }
    return result;
}

}  // namespace zima::document
