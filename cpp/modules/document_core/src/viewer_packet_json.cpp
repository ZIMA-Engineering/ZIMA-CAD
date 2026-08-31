#include <zima/document/viewer_packet_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

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

constexpr std::string_view kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::vector<std::uint8_t>& bytes) {
    std::string result;
    result.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t index = 0; index < bytes.size(); index += 3) {
        const std::uint32_t value =
            static_cast<std::uint32_t>(bytes[index]) << 16 |
            (index + 1 < bytes.size()
                ? static_cast<std::uint32_t>(bytes[index + 1]) << 8 : 0U) |
            (index + 2 < bytes.size()
                ? static_cast<std::uint32_t>(bytes[index + 2]) : 0U);
        result.push_back(kBase64Alphabet[(value >> 18) & 63U]);
        result.push_back(kBase64Alphabet[(value >> 12) & 63U]);
        result.push_back(index + 1 < bytes.size()
            ? kBase64Alphabet[(value >> 6) & 63U] : '=');
        result.push_back(index + 2 < bytes.size()
            ? kBase64Alphabet[value & 63U] : '=');
    }
    return result;
}

std::vector<std::uint8_t> base64_decode(std::string_view text) {
    if (text.size() % 4 != 0) {
        throw std::runtime_error("Packed viewer data have invalid base64 length");
    }
    std::array<std::int16_t, 256> values;
    values.fill(-1);
    for (std::size_t index = 0; index < kBase64Alphabet.size(); ++index) {
        values[static_cast<unsigned char>(kBase64Alphabet[index])] =
            static_cast<std::int16_t>(index);
    }
    std::vector<std::uint8_t> result;
    result.reserve(text.size() / 4 * 3);
    for (std::size_t index = 0; index < text.size(); index += 4) {
        const bool third_padding = text[index + 2] == '=';
        const bool fourth_padding = text[index + 3] == '=';
        if ((third_padding && !fourth_padding) ||
            (index + 4 != text.size() && (third_padding || fourth_padding))) {
            throw std::runtime_error("Packed viewer data have invalid base64 padding");
        }
        const auto decode = [&](std::size_t offset) -> std::uint32_t {
            const auto character = static_cast<unsigned char>(text[index + offset]);
            if (text[index + offset] == '=' && offset >= 2) return 0U;
            if (values[character] < 0) {
                throw std::runtime_error("Packed viewer data contain invalid base64");
            }
            return static_cast<std::uint32_t>(values[character]);
        };
        const std::uint32_t value = decode(0) << 18 | decode(1) << 12 |
            decode(2) << 6 | decode(3);
        result.push_back(static_cast<std::uint8_t>((value >> 16) & 255U));
        if (!third_padding) {
            result.push_back(static_cast<std::uint8_t>((value >> 8) & 255U));
        }
        if (!fourth_padding) result.push_back(static_cast<std::uint8_t>(value & 255U));
    }
    return result;
}

template <typename Integer>
void append_little_endian(std::vector<std::uint8_t>& bytes, Integer value) {
    for (std::size_t byte = 0; byte < sizeof(Integer); ++byte) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
    }
}

template <typename Integer>
Integer read_little_endian(const std::vector<std::uint8_t>& bytes,
                           std::size_t offset) {
    Integer result{};
    for (std::size_t byte = 0; byte < sizeof(Integer); ++byte) {
        result |= static_cast<Integer>(bytes[offset + byte]) << (byte * 8);
    }
    return result;
}

std::string pack_vertices(const std::vector<zima::kernel::Vec3>& vertices) {
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    static_assert(std::numeric_limits<double>::is_iec559);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(vertices.size() * 3 * sizeof(double));
    for (const auto& point : vertices) {
        append_little_endian(bytes, std::bit_cast<std::uint64_t>(point.x));
        append_little_endian(bytes, std::bit_cast<std::uint64_t>(point.y));
        append_little_endian(bytes, std::bit_cast<std::uint64_t>(point.z));
    }
    return base64_encode(bytes);
}

std::vector<zima::kernel::Vec3> unpack_vertices(std::string_view encoded) {
    const auto bytes = base64_decode(encoded);
    if (bytes.size() % (3 * sizeof(double)) != 0) {
        throw std::runtime_error("Packed viewer vertices have invalid length");
    }
    std::vector<zima::kernel::Vec3> result;
    result.reserve(bytes.size() / (3 * sizeof(double)));
    for (std::size_t offset = 0; offset < bytes.size(); offset += 24) {
        zima::kernel::Vec3 point{
            std::bit_cast<double>(read_little_endian<std::uint64_t>(bytes, offset)),
            std::bit_cast<double>(read_little_endian<std::uint64_t>(bytes, offset + 8)),
            std::bit_cast<double>(read_little_endian<std::uint64_t>(bytes, offset + 16))};
        require_finite(point.x, "viewer x");
        require_finite(point.y, "viewer y");
        require_finite(point.z, "viewer z");
        result.push_back(point);
    }
    return result;
}

std::string pack_indices(const std::vector<std::uint32_t>& indices) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(indices.size() * sizeof(std::uint32_t));
    for (const auto index : indices) append_little_endian(bytes, index);
    return base64_encode(bytes);
}

std::vector<std::uint32_t> unpack_indices(std::string_view encoded) {
    const auto bytes = base64_decode(encoded);
    if (bytes.size() % sizeof(std::uint32_t) != 0) {
        throw std::runtime_error("Packed viewer indices have invalid length");
    }
    std::vector<std::uint32_t> result;
    result.reserve(bytes.size() / sizeof(std::uint32_t));
    for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
        result.push_back(read_little_endian<std::uint32_t>(bytes, offset));
    }
    return result;
}

}  // namespace

namespace {

nlohmann::json serialize_reference_geometry(
    const zima::kernel::ViewerReferenceGeometry& geometry) {
    nlohmann::json vertices = nlohmann::json::array();
    for (const auto& value : geometry.vertices) vertices.push_back(serialize_vec3(value));
    nlohmann::json faces = nlohmann::json::array();
    for (const auto& reference : geometry.triangle_references) faces.push_back({
        {"owner", reference.owner_id}, {"key", reference.semantic_key},
        {"instance_path", reference.instance_path}});
    nlohmann::json edges = nlohmann::json::array();
    for (const auto& edge : geometry.edges) {
        nlohmann::json edge_points = nlohmann::json::array();
        for (const auto& point : edge.points) edge_points.push_back(serialize_vec3(point));
        edges.push_back({{"owner", edge.reference.owner_id},
            {"key", edge.reference.semantic_key},
            {"instance_path", edge.reference.instance_path},
            {"points", std::move(edge_points)}});
    }
    nlohmann::json points = nlohmann::json::array();
    for (const auto& point : geometry.points) points.push_back({
        {"owner", point.reference.owner_id}, {"key", point.reference.semantic_key},
        {"instance_path", point.reference.instance_path},
        {"position", serialize_vec3(point.position)}});
    nlohmann::json axes = nlohmann::json::array();
    for (const auto& axis : geometry.axes) axes.push_back({
        {"owner", axis.reference.owner_id}, {"key", axis.reference.semantic_key},
        {"instance_path", axis.reference.instance_path},
        {"point", serialize_vec3(axis.point)},
        {"direction", serialize_vec3(axis.direction)},
        {"display_length", axis.display_length}});
    return {{"vertices", std::move(vertices)}, {"triangles", geometry.triangles},
        {"triangle_references", std::move(faces)}, {"edges", std::move(edges)},
        {"points", std::move(points)}, {"axes", std::move(axes)}};
}

zima::kernel::ViewerReferenceGeometry load_reference_geometry(
    const nlohmann::json& source) {
    zima::kernel::ViewerReferenceGeometry result;
    for (const auto& value : source.at("vertices")) result.vertices.push_back(load_vec3(value));
    result.triangles = source.at("triangles").get<std::vector<std::uint32_t>>();
    for (const auto index : result.triangles) {
        if (index >= result.vertices.size()) {
            throw std::runtime_error("Reference triangle index is out of range");
        }
    }
    for (const auto& value : source.at("triangle_references")) {
        result.triangle_references.push_back({value.at("owner").get<std::string>(),
            value.at("key").get<std::string>(),
            value.at("instance_path").get<std::string>()});
    }
    if (result.triangles.size() % 3 != 0 ||
        result.triangle_references.size() != result.triangles.size() / 3) {
        throw std::runtime_error("Reference triangle data are not aligned");
    }
    for (const auto& value : source.at("edges")) {
        zima::kernel::ViewerEdge edge;
        edge.reference = {value.at("owner").get<std::string>(),
            value.at("key").get<std::string>(),
            value.at("instance_path").get<std::string>()};
        for (const auto& point : value.at("points")) edge.points.push_back(load_vec3(point));
        if (!edge.reference.valid() || edge.points.size() < 2) {
            throw std::runtime_error("Persisted reference edge is invalid");
        }
        result.edges.push_back(std::move(edge));
    }
    for (const auto& value : source.at("points")) {
        zima::kernel::ViewerPoint point{load_vec3(value.at("position")),
            {value.at("owner").get<std::string>(), value.at("key").get<std::string>(),
             value.at("instance_path").get<std::string>()}};
        if (!point.reference.valid()) throw std::runtime_error("Reference point is invalid");
        result.points.push_back(std::move(point));
    }
    for (const auto& value : source.at("axes")) {
        zima::kernel::ViewerAxis axis{load_vec3(value.at("point")),
            load_vec3(value.at("direction")), value.at("display_length").get<double>(),
            {value.at("owner").get<std::string>(), value.at("key").get<std::string>(),
             value.at("instance_path").get<std::string>()}};
        if (!axis.reference.valid() || !std::isfinite(axis.display_length) ||
            axis.display_length <= 0.0) throw std::runtime_error("Reference axis is invalid");
        result.axes.push_back(std::move(axis));
    }
    return result;
}

}  // namespace

nlohmann::json serialize_body_result(const zima::kernel::BodyResult& result) {
    nlohmann::json faces = nlohmann::json::array();
    const bool has_face_references = std::ranges::any_of(
        result.mesh.triangle_references,
        [](const auto& reference) { return reference.valid(); });
    if (has_face_references) {
        for (const auto& reference : result.mesh.triangle_references) {
            faces.push_back({
                {"owner", reference.owner_id}, {"key", reference.semantic_key},
                {"instance_path", reference.instance_path}});
        }
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
            {"unit_suffix", dimension.unit_suffix},
            {"kind", dimension.kind == zima::kernel::ViewerDimensionKind::Angular
                ? "angular"
                : dimension.kind == zima::kernel::ViewerDimensionKind::Radius
                    ? "radius"
                    : dimension.kind == zima::kernel::ViewerDimensionKind::Diameter
                        ? "diameter" : "linear"},
            {"plane_normal", serialize_vec3(dimension.plane_normal)},
            {"sweep_degrees", dimension.sweep_degrees},
        });
    }
    return {
        {"volume", result.volume}, {"surface_area", result.surface_area},
        {"source_fingerprint", result.source_fingerprint},
        {"kernel_shape", result.kernel_shape},
        {"viewer_binary_encoding", "f64le-u32le-base64-v1"},
        {"vertices_binary", pack_vertices(result.mesh.vertices)},
        {"triangles_binary", pack_indices(result.mesh.triangles)},
        {"triangle_references", std::move(faces)},
        {"edges", std::move(edges)}, {"points", std::move(points)},
        {"axes", std::move(axes)}, {"dimensions", std::move(dimensions)},
        {"original_references", serialize_reference_geometry(
            result.mesh.original_references)},
    };
}

nlohmann::json serialize_viewer_reference_geometry(
    const zima::kernel::ViewerReferenceGeometry& geometry) {
    return serialize_reference_geometry(geometry);
}

zima::kernel::ViewerReferenceGeometry load_viewer_reference_geometry(
    const nlohmann::json& source) {
    return load_reference_geometry(source);
}

zima::kernel::BodyResult load_body_result(const nlohmann::json& source) {
    zima::kernel::BodyResult result;
    result.volume = source.at("volume").get<double>();
    result.surface_area = source.at("surface_area").get<double>();
    result.source_fingerprint = source.at("source_fingerprint").get<std::string>();
    result.kernel_shape = source.at("kernel_shape").get<std::string>();
    result.mesh.original_references = load_reference_geometry(
        source.at("original_references"));
    require_finite(result.volume, "calculated volume");
    require_finite(result.surface_area, "calculated surface area");
    if (source.value("viewer_binary_encoding", "") !=
            "f64le-u32le-base64-v1") {
        throw std::runtime_error("Unsupported calculated viewer mesh encoding");
    }
    result.mesh.vertices = unpack_vertices(
        source.at("vertices_binary").get_ref<const std::string&>());
    result.mesh.triangles = unpack_indices(
        source.at("triangles_binary").get_ref<const std::string&>());
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
    if (result.mesh.triangle_references.empty()) {
        result.mesh.triangle_references.resize(result.mesh.triangles.size() / 3);
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
        const bool owner_empty = loaded.reference.owner_id.empty();
        const bool key_empty = loaded.reference.semantic_key.empty();
        if (owner_empty != key_empty ||
            (owner_empty && !loaded.reference.instance_path.empty()) ||
            loaded.points.size() < 2) {
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
        loaded.unit_suffix = dimension.at("unit_suffix").get<std::string>();
        const auto kind = dimension.at("kind").get<std::string>();
        loaded.kind = kind == "angular"
            ? zima::kernel::ViewerDimensionKind::Angular
            : kind == "radius" ? zima::kernel::ViewerDimensionKind::Radius
            : kind == "diameter" ? zima::kernel::ViewerDimensionKind::Diameter
                                 : zima::kernel::ViewerDimensionKind::Linear;
        loaded.plane_normal = load_vec3(dimension.at("plane_normal"));
        loaded.sweep_degrees = dimension.at("sweep_degrees").get<double>();
        require_finite(loaded.value, "viewer dimension value");
        require_finite(loaded.sweep_degrees, "viewer dimension sweep");
        if (!loaded.reference.valid()) {
            throw std::runtime_error("Persisted viewer dimension is invalid");
        }
        result.mesh.dimensions.push_back(std::move(loaded));
    }
    return result;
}

}  // namespace zima::document
