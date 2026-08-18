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
    nlohmann::json axes = nlohmann::json::array();
    for (const auto& axis : result.mesh.axes) {
        axes.push_back({
            {"owner", axis.reference.owner_id},
            {"key", axis.reference.semantic_key},
            {"instance_path", axis.reference.instance_path},
            {"point", serialize_vec3(axis.point)},
            {"direction", serialize_vec3(axis.direction)},
            {"display_length", axis.display_length},
        });
    }
    nlohmann::json dimensions = nlohmann::json::array();
    for (const auto& dimension : result.mesh.dimensions) {
        dimensions.push_back({
            {"owner", dimension.reference.owner_id},
            {"key", dimension.reference.semantic_key},
            {"instance_path", dimension.reference.instance_path},
            {"witness_first", serialize_vec3(dimension.witness_first)},
            {"witness_second", serialize_vec3(dimension.witness_second)},
            {"line_first", serialize_vec3(dimension.line_first)},
            {"line_second", serialize_vec3(dimension.line_second)},
            {"value", dimension.value},
            {"label_prefix", dimension.label_prefix},
        });
    }
    return {
        {"volume", result.volume}, {"surface_area", result.surface_area},
        {"source_fingerprint", result.source_fingerprint},
        {"vertices", std::move(vertices)}, {"triangles", result.mesh.triangles},
        {"triangle_references", std::move(faces)},
        {"edges", std::move(edges)}, {"points", std::move(points)},
        {"axes", std::move(axes)}, {"dimensions", std::move(dimensions)},
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
    for (const auto& axis : source.at("axes")) {
        zima::kernel::ViewerAxis loaded;
        loaded.reference = {
            axis.at("owner").get<std::string>(), axis.at("key").get<std::string>(),
            axis.at("instance_path").get<std::string>()};
        loaded.point = load_vec3(axis.at("point"));
        loaded.direction = load_vec3(axis.at("direction"));
        loaded.display_length = axis.at("display_length").get<double>();
        require_finite(loaded.display_length, "viewer axis display length");
        const double magnitude = std::sqrt(
            loaded.direction.x * loaded.direction.x +
            loaded.direction.y * loaded.direction.y +
            loaded.direction.z * loaded.direction.z);
        if (!loaded.reference.valid() || loaded.display_length <= 0.0 ||
            std::abs(magnitude - 1.0) > 1.0e-9) {
            throw std::runtime_error("Persisted viewer axis is invalid");
        }
        result.mesh.axes.push_back(std::move(loaded));
    }
    for (const auto& dimension : source.at("dimensions")) {
        zima::kernel::ViewerDimension loaded;
        loaded.reference = {
            dimension.at("owner").get<std::string>(),
            dimension.at("key").get<std::string>(),
            dimension.at("instance_path").get<std::string>()};
        loaded.witness_first = load_vec3(dimension.at("witness_first"));
        loaded.witness_second = load_vec3(dimension.at("witness_second"));
        loaded.line_first = load_vec3(dimension.at("line_first"));
        loaded.line_second = load_vec3(dimension.at("line_second"));
        loaded.value = dimension.at("value").get<double>();
        loaded.label_prefix = dimension.at("label_prefix").get<std::string>();
        require_finite(loaded.value, "viewer dimension value");
        if (!loaded.reference.valid()) {
            throw std::runtime_error("Persisted viewer dimension is invalid");
        }
        result.mesh.dimensions.push_back(std::move(loaded));
    }
    return result;
}

}  // namespace zima::document
