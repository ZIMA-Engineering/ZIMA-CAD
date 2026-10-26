#include <zima/kernel/bspline_json.hpp>
#include <zima/kernel/axis_display.hpp>
#include <zima/kernel/solid_state_ancestry.hpp>
#include <zima/document/dimension_layout_json.hpp>
#include <zima/document/viewer_packet_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <exception>
#include <future>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include <string_view>
#include <tuple>
#include <vector>

namespace zima::document {
namespace {

thread_local bool processing_parallel_row = false;

template<class Result, class Function>
std::vector<Result> process_rows(std::size_t count, Function&& function) {
    std::vector<Result> rows(count);
    // These immutable rows are allocator- and memory-bandwidth-bound. On the
    // representative 31-frame model four workers reduced complete Open/Save
    // time, while using every logical CPU made later cache traversal slower.
    const auto available = std::min(4u, std::max(1u, std::thread::hardware_concurrency()));
    const auto worker_count = processing_parallel_row || count < available * 2u
        ? std::size_t{1}
        : std::min(count, static_cast<std::size_t>(available));
    if (worker_count == 1) {
        for (std::size_t index = 0; index < count; ++index) rows[index] = function(index);
        return rows;
    }

    std::atomic_size_t next{0};
    const auto work = [&] {
        const bool previous = processing_parallel_row;
        processing_parallel_row = true;
        try {
            for (;;) {
                const auto index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= count) break;
                rows[index] = function(index);
            }
        } catch (...) {
            processing_parallel_row = previous;
            throw;
        }
        processing_parallel_row = previous;
    };
    std::vector<std::future<void>> workers;
    workers.reserve(worker_count);
    for (std::size_t index = 0; index < worker_count; ++index)
        workers.push_back(std::async(std::launch::async, work));
    std::exception_ptr failure;
    for (auto& worker : workers) {
        try { worker.get(); }
        catch (...) { if (!failure) failure = std::current_exception(); }
    }
    if (failure) std::rethrow_exception(failure);
    return rows;
}

void restore_reference_curve_style(zima::kernel::ViewerEdge& edge) {
    std::string source_key;
    if(!zima::kernel::construction_source_key(edge.reference.semantic_key,source_key).starts_with("centerline:from:"))return;
    edge.construction=true;edge.overlay=true;edge.dash_dot=true;
    edge.display_owner_id=edge.reference.owner_id;
}

bool axis_endpoint(std::string_view semantic_key) {
    std::string source_key;
    return !zima::kernel::axis_endpoint_parent_key(
        zima::kernel::construction_source_key(semantic_key,source_key)).empty();
}

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

nlohmann::json serialize_surface(const zima::kernel::SurfaceGeometry& value) {
    nlohmann::json result={{"kind",static_cast<int>(value.kind)},{"origin",serialize_vec3(value.origin)},
        {"axis",serialize_vec3(value.axis)},{"radial",serialize_vec3(value.radial)},
        {"radius",value.radius},{"semi_angle",value.semi_angle},
        {"min",value.axial_min},{"max",value.axial_max},{"reversed",value.reversed}};
    if(value.kind==zima::kernel::SurfaceGeometry::Kind::Torus)
        result["torus"]={{"minor_radius",value.minor_radius},{"u_min",value.u_min},{"u_max",value.u_max},
            {"v_min",value.v_min},{"v_max",value.v_max},{"complete_profile",value.complete_profile}};
    return result;
}
zima::kernel::SurfaceGeometry load_surface(const nlohmann::json& value) {
    zima::kernel::SurfaceGeometry result;
    const int kind=value.at("kind");
    if (kind<0 || kind>3) throw std::runtime_error("Invalid analytic surface kind");
    result.kind=static_cast<zima::kernel::SurfaceGeometry::Kind>(kind);
    result.origin=load_vec3(value.at("origin"));result.axis=load_vec3(value.at("axis"));
    result.radial=load_vec3(value.at("radial"));result.radius=value.at("radius");
    result.semi_angle=value.at("semi_angle");result.axial_min=value.at("min");
    result.axial_max=value.at("max");result.reversed=value.at("reversed");
    require_finite(result.radius,"surface radius");require_finite(result.semi_angle,"surface angle");
    require_finite(result.axial_min,"surface min");require_finite(result.axial_max,"surface max");
    if(result.kind==zima::kernel::SurfaceGeometry::Kind::Torus) {
        const auto& torus=value.at("torus");
        result.minor_radius=torus.at("minor_radius");result.u_min=torus.at("u_min");result.u_max=torus.at("u_max");
        result.v_min=torus.at("v_min");result.v_max=torus.at("v_max");
        result.complete_profile=torus.at("complete_profile");
        for(const auto number:{result.minor_radius,result.u_min,result.u_max,result.v_min,result.v_max})
            require_finite(number,"surface angle");
        if(result.minor_radius<=0||result.radius<=0||result.u_max<=result.u_min||result.v_max<=result.v_min)
            throw std::runtime_error("Invalid analytic surface kind");
    }
    return result;
}

constexpr std::string_view kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void load_sheet_face(zima::kernel::FaceReference& face,const nlohmann::json& value) {
    const int role=value.at("sheet_role");const double thickness=value.at("sheet_thickness");
    if(role<0||role>3||!std::isfinite(thickness)||thickness<0||
        (role!=0&&thickness<=0))throw std::runtime_error("Invalid sheet face metadata");
    face.sheet_role=static_cast<zima::kernel::SheetFaceRole>(role);face.sheet_thickness=thickness;
    face.sheet_owner=value.at("sheet_owner");
    face.display_owner_id=value.value("display_owner",std::string{});
}
nlohmann::json sheet_edge_json(const zima::kernel::ViewerEdge& edge) {
    if(zima::kernel::sheet_edge_role(edge)==zima::kernel::SheetEdgeRole::Unknown)return nullptr;
    nlohmann::json sides=nlohmann::json::array(),ends=nlohmann::json::array();
    for(const auto& r:edge.edge_treatment_side_references)
        sides.push_back({{"owner",r.owner_id},{"key",r.semantic_key},{"instance_path",r.instance_path},
            {"sheet_role",static_cast<int>(r.sheet_role)},{"sheet_thickness",r.sheet_thickness},{"sheet_owner",r.sheet_owner},
            {"display_owner",r.display_owner_id}});
    for(const auto& r:edge.edge_treatment_endpoint_references)
        ends.push_back({{"owner",r.owner_id},{"key",r.semantic_key},{"instance_path",r.instance_path}});
    return {{"sides",sides},{"endpoints",ends}};
}
void load_sheet_edge(zima::kernel::ViewerEdge& edge,const nlohmann::json& value) {
    if(value.is_null())return;
    for(const auto& r:value.at("sides")) {
        zima::kernel::FaceReference face{r.at("owner"),r.at("key"),r.at("instance_path")};
        load_sheet_face(face,r);if(!face.valid())throw std::runtime_error("Invalid sheet side identity");
        edge.edge_treatment_side_references.push_back(std::move(face));
    }
    for(const auto& r:value.at("endpoints")) {
        zima::kernel::VertexReference point{r.at("owner"),r.at("key"),r.at("instance_path")};
        if(!point.valid())throw std::runtime_error("Invalid sheet endpoint identity");
        edge.edge_treatment_endpoint_references.push_back(std::move(point));
    }
    if(edge.edge_treatment_side_references.size()!=2||
       (!edge.edge_treatment_endpoint_references.empty()&&edge.edge_treatment_endpoint_references.size()!=2))
        throw std::runtime_error("Incomplete sheet edge metadata");
}

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
    nlohmann::json references = nlohmann::json::array();
    std::map<std::tuple<std::string, std::string, std::string>, std::uint32_t>
        reference_indices;
    const auto reference_index = [&](const auto& reference) {
        if (!reference.valid()) {
            throw std::runtime_error("Original reference is invalid");
        }
        const auto key = std::tuple{reference.owner_id,
            reference.semantic_key, reference.instance_path};
        const auto found = reference_indices.find(key);
        if (found != reference_indices.end()) return found->second;
        if (reference_indices.size() >=
            std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("Too many original references");
        }
        const auto index = static_cast<std::uint32_t>(
            reference_indices.size());
        reference_indices.emplace(key, index);
        references.push_back({{"owner", reference.owner_id},
            {"key", reference.semantic_key},
            {"instance_path", reference.instance_path},{"sheet_role",0},{"sheet_thickness",0.0},{"sheet_owner",""},
            {"display_owner",""}});
        if constexpr (requires { reference.display_owner_id; })
            references.back()["display_owner"]=reference.display_owner_id;
        if constexpr (requires { reference.surface; }) {
            references.back()["surface_result"]=reference.surface_result;
            references.back()["sheet_role"]=static_cast<int>(reference.sheet_role);
            references.back()["sheet_thickness"]=reference.sheet_thickness;
            references.back()["sheet_owner"]=reference.sheet_owner;
            if (reference.surface) references.back()["surface"]=serialize_surface(*reference.surface);
            if (reference.contour_surface) references.back()["contour_surface"]=serialize_surface(*reference.contour_surface);
            if (reference.measured_area) references.back()["measured_area"]=*reference.measured_area;
        }
        return index;
    };

    std::vector<std::uint32_t> triangle_reference_runs;
    for (const auto& reference : geometry.triangle_references) {
        const auto index = reference_index(reference);
        if (triangle_reference_runs.size() >= 2 &&
            triangle_reference_runs[triangle_reference_runs.size() - 2] == index &&
            triangle_reference_runs.back() <
                std::numeric_limits<std::uint32_t>::max()) {
            ++triangle_reference_runs.back();
        } else {
            triangle_reference_runs.push_back(index);
            triangle_reference_runs.push_back(1);
        }
    }

    std::vector<zima::kernel::Vec3> edge_points;
    std::vector<std::uint32_t> edge_offsets{0};
    std::vector<std::uint32_t> edge_references;
    nlohmann::json edge_lengths = nlohmann::json::array();
    nlohmann::json edge_splines = nlohmann::json::array();
    nlohmann::json edge_sheets = nlohmann::json::array();
    nlohmann::json edge_surfaces=nlohmann::json::array(),point_surfaces=nlohmann::json::array();
    for (const auto& edge : geometry.edges) {
        edge_surfaces.push_back(edge.surface_result);
        edge_sheets.push_back(sheet_edge_json(edge));
        edge_splines.push_back(zima::kernel::spline_json(edge.exact_spline));
        edge_lengths.push_back(edge.measured_length ? nlohmann::json(*edge.measured_length) : nlohmann::json(nullptr));
        if (edge.points.size() < 2 ||
            edge_points.size() + edge.points.size() >
                std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("Original reference edge is invalid");
        }
        edge_points.insert(edge_points.end(), edge.points.begin(), edge.points.end());
        edge_offsets.push_back(static_cast<std::uint32_t>(edge_points.size()));
        edge_references.push_back(reference_index(edge.reference));
    }

    std::vector<zima::kernel::Vec3> point_positions;
    std::vector<std::uint32_t> point_references;
    point_positions.reserve(geometry.points.size());
    point_references.reserve(geometry.points.size());
    for (const auto& point : geometry.points) {
        point_surfaces.push_back(point.surface_result);
        point_positions.push_back(point.position);
        point_references.push_back(reference_index(point.reference));
    }

    nlohmann::json axes = nlohmann::json::array();
    for (const auto& axis : geometry.axes) axes.push_back({
        {"reference", reference_index(axis.reference)},
        {"point", serialize_vec3(axis.point)},
        {"direction", serialize_vec3(axis.direction)},
        {"display_length", axis.display_length}});
    return {{"encoding", "f64le-u32le-base64-v1"},
        {"references", std::move(references)},
        {"vertices_binary", pack_vertices(geometry.vertices)},
        {"triangles_binary", pack_indices(geometry.triangles)},
        {"triangle_reference_runs_binary", pack_indices(triangle_reference_runs)},
        {"edge_points_binary", pack_vertices(edge_points)},
        {"edge_offsets_binary", pack_indices(edge_offsets)},
        {"edge_lengths", std::move(edge_lengths)},
        {"edge_splines", std::move(edge_splines)},
        {"edge_sheets", std::move(edge_sheets)},
        {"edge_surfaces",std::move(edge_surfaces)},{"point_surfaces",std::move(point_surfaces)},
        {"edge_references_binary", pack_indices(edge_references)},
        {"point_positions_binary", pack_vertices(point_positions)},
        {"point_references_binary", pack_indices(point_references)},
        {"axes", std::move(axes)}};
}

zima::kernel::ViewerReferenceGeometry load_reference_geometry(
    const nlohmann::json& source) {
    if (source.at("encoding").get<std::string>() !=
            "f64le-u32le-base64-v1") {
        throw std::runtime_error("Unsupported original reference encoding");
    }
    zima::kernel::ViewerReferenceGeometry result;
    std::vector<zima::kernel::FaceReference> references;
    for (const auto& value : source.at("references")) {
        zima::kernel::FaceReference reference{
            value.at("owner").get<std::string>(),
            value.at("key").get<std::string>(),
            value.at("instance_path").get<std::string>()};
        if (!reference.valid()) {
            throw std::runtime_error("Persisted original reference is invalid");
        }
        if (value.contains("surface")) reference.surface=std::make_shared<const zima::kernel::SurfaceGeometry>(load_surface(value.at("surface")));
        if (value.contains("contour_surface")) reference.contour_surface=std::make_shared<const zima::kernel::SurfaceGeometry>(load_surface(value.at("contour_surface")));
        reference.surface_result=value.value("surface_result",false);
        load_sheet_face(reference,value);
        if (value.contains("measured_area")) {
            reference.measured_area = value.at("measured_area").get<double>();
            require_finite(*reference.measured_area, "measured area");
            if (*reference.measured_area < 0) throw std::runtime_error("Negative measured area");
        }
        references.push_back(std::move(reference));
    }
    const auto reference_at = [&](std::uint32_t index)
        -> const zima::kernel::FaceReference& {
        if (index >= references.size()) {
            throw std::runtime_error("Original reference index is out of range");
        }
        return references[index];
    };

    result.vertices = unpack_vertices(
        source.at("vertices_binary").get_ref<const std::string&>());
    result.triangles = unpack_indices(
        source.at("triangles_binary").get_ref<const std::string&>());
    for (const auto index : result.triangles) {
        if (index >= result.vertices.size()) {
            throw std::runtime_error("Reference triangle index is out of range");
        }
    }
    const auto triangle_runs = unpack_indices(
        source.at("triangle_reference_runs_binary")
            .get_ref<const std::string&>());
    if (triangle_runs.size() % 2 != 0) {
        throw std::runtime_error("Reference triangle runs are invalid");
    }
    for (std::size_t run = 0; run < triangle_runs.size(); run += 2) {
        const auto& reference = reference_at(triangle_runs[run]);
        const auto count = triangle_runs[run + 1];
        if (count == 0 || result.triangle_references.size() + count >
                result.triangles.size() / 3) {
            throw std::runtime_error("Reference triangle run is invalid");
        }
        result.triangle_references.insert(result.triangle_references.end(), count,
            reference);
    }
    if (result.triangles.size() % 3 != 0 ||
        result.triangle_references.size() != result.triangles.size() / 3) {
        throw std::runtime_error("Reference triangle data are not aligned");
    }

    const auto edge_points = unpack_vertices(
        source.at("edge_points_binary").get_ref<const std::string&>());
    const auto edge_offsets = unpack_indices(
        source.at("edge_offsets_binary").get_ref<const std::string&>());
    const auto edge_references = unpack_indices(
        source.at("edge_references_binary").get_ref<const std::string&>());
    if (edge_offsets.size() != edge_references.size() + 1 ||
        edge_offsets.empty() || edge_offsets.front() != 0 ||
        edge_offsets.back() != edge_points.size()) {
        throw std::runtime_error("Persisted reference edge arrays are invalid");
    }
    for (std::size_t index = 0; index < edge_references.size(); ++index) {
        if (edge_offsets[index] > edge_offsets[index + 1] ||
            edge_offsets[index + 1] - edge_offsets[index] < 2) {
            throw std::runtime_error("Persisted reference edge is invalid");
        }
        const auto& reference = reference_at(edge_references[index]);
        zima::kernel::ViewerEdge edge;
        edge.reference = {reference.owner_id, reference.semantic_key,
            reference.instance_path,reference.display_owner_id};
        edge.display_owner_id=reference.display_owner_id;
        edge.points.insert(edge.points.end(),
            edge_points.begin() + edge_offsets[index],
            edge_points.begin() + edge_offsets[index + 1]);
        if (source.contains("edge_lengths")) {
            if (source.at("edge_lengths").size() != edge_references.size())
                throw std::runtime_error("Invalid measured edge lengths");
            const auto& length = source.at("edge_lengths").at(index);
            if (!length.is_null()) {
                edge.measured_length = length.get<double>();
                require_finite(*edge.measured_length, "measured length");
                if (*edge.measured_length < 0) throw std::runtime_error("Negative measured length");
            }
        }
        if (source.at("edge_splines").size() != edge_references.size())
            throw std::runtime_error("Invalid exact reference curves");
        edge.exact_spline = zima::kernel::spline_from_json(source.at("edge_splines").at(index));
        edge.surface_result=source.at("edge_surfaces").at(index).get<bool>();
        load_sheet_edge(edge,source.at("edge_sheets").at(index));
        restore_reference_curve_style(edge);
        result.edges.push_back(std::move(edge));
    }

    const auto point_positions = unpack_vertices(
        source.at("point_positions_binary").get_ref<const std::string&>());
    const auto point_references = unpack_indices(
        source.at("point_references_binary").get_ref<const std::string&>());
    if (point_positions.size() != point_references.size()) {
        throw std::runtime_error("Persisted reference point arrays are invalid");
    }
    for (std::size_t index = 0; index < point_positions.size(); ++index) {
        const auto& reference = reference_at(point_references[index]);
        result.points.push_back({point_positions[index],
            {reference.owner_id, reference.semantic_key,
             reference.instance_path}});
        result.points.back().surface_result=source.at("point_surfaces").at(index).get<bool>();
        if (axis_endpoint(reference.semantic_key))
            result.points.back().display_owner_id = reference.owner_id;
    }
    for (const auto& value : source.at("axes")) {
        const auto& reference = reference_at(
            value.at("reference").get<std::uint32_t>());
        zima::kernel::ViewerAxis axis{load_vec3(value.at("point")),
            load_vec3(value.at("direction")), value.at("display_length").get<double>(),
            {reference.owner_id, reference.semantic_key,
             reference.instance_path}};
        if (!axis.reference.valid() || !std::isfinite(axis.display_length) ||
            axis.display_length <= 0.0) throw std::runtime_error("Reference axis is invalid");
        result.axes.push_back(std::move(axis));
    }
    return result;
}

}  // namespace

nlohmann::json serialize_surface_geometry(const zima::kernel::SurfaceGeometry& value) {
    return serialize_surface(value);
}
zima::kernel::SurfaceGeometry load_surface_geometry(const nlohmann::json& value) {
    return load_surface(value);
}

nlohmann::json serialize_body_result(const zima::kernel::BodyResult& result, bool include_histories,
        bool include_compound_operands,
        const zima::kernel::ViewerReferenceGeometry* reference_geometry) {
    nlohmann::json faces = nlohmann::json::array();
    const bool has_triangle_tags = std::ranges::any_of(
        result.mesh.triangle_references,
        [](const auto& reference) {
            return reference.valid() || !reference.instance_path.empty() || reference.surface_result;
        });
    if (has_triangle_tags) {
        for (const auto& reference : result.mesh.triangle_references) {
            faces.push_back({
                {"owner", reference.owner_id}, {"key", reference.semantic_key},
                {"instance_path", reference.instance_path},{"surface_result",reference.surface_result},
                {"sheet_role",static_cast<int>(reference.sheet_role)},{"sheet_thickness",reference.sheet_thickness},{"sheet_owner",reference.sheet_owner},
                {"display_owner",reference.display_owner_id}});
        }
    }
    nlohmann::json edges = nlohmann::json::array();
    for (const auto& edge : result.mesh.edges) {
        nlohmann::json points = nlohmann::json::array();
        for (const auto& point : edge.points) points.push_back(serialize_vec3(point));
        nlohmann::json side_directions = nlohmann::json::array();
        for (const auto& side : edge.edge_treatment_side_directions) {
            nlohmann::json samples = nlohmann::json::array();
            for (const auto& direction : side) {
                samples.push_back(serialize_vec3(direction));
            }
            side_directions.push_back(std::move(samples));
        }
        nlohmann::json side_references = nlohmann::json::array();
        for (const auto& reference : edge.edge_treatment_side_references) {
            side_references.push_back({
                {"owner", reference.owner_id},
                {"key", reference.semantic_key},
                {"instance_path", reference.instance_path},
                {"sheet_role",static_cast<int>(reference.sheet_role)},{"sheet_thickness",reference.sheet_thickness},{"sheet_owner",reference.sheet_owner},
                {"display_owner",reference.display_owner_id}});
        }
        nlohmann::json endpoint_references = nlohmann::json::array();
        for (const auto& reference :
             edge.edge_treatment_endpoint_references) {
            endpoint_references.push_back({
                {"owner", reference.owner_id},
                {"key", reference.semantic_key},
                {"instance_path", reference.instance_path}});
        }
        edges.push_back({
            {"owner", edge.reference.owner_id}, {"key", edge.reference.semantic_key},
            {"instance_path", edge.reference.instance_path},
            {"display_owner", edge.display_owner_id}, {"color",edge.color},
            {"surface_result",edge.surface_result},{"parameter_seam", edge.parameter_seam}, {"filled_text", edge.filled_text},
            {"edge_treatment_owners", edge.edge_treatment_owner_ids},
            {"edge_treatment_side_directions", std::move(side_directions)},
            {"edge_treatment_side_references", std::move(side_references)},
            {"edge_treatment_endpoint_references",
                std::move(endpoint_references)},
            {"points", std::move(points)},
            {"exact_spline", zima::kernel::spline_json(edge.exact_spline)},
            {"measured_length", edge.measured_length ? nlohmann::json(*edge.measured_length) : nlohmann::json(nullptr)},
        });
    }
    nlohmann::json points = nlohmann::json::array();
    for (const auto& point : result.mesh.points) {
        points.push_back({
            {"owner", point.reference.owner_id}, {"key", point.reference.semantic_key},
            {"instance_path", point.reference.instance_path},
            {"position", serialize_vec3(point.position)},{"surface_result",point.surface_result},
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
            {"source_text_style",dimension.source_text_style?dimension_text_style_json(*dimension.source_text_style):nlohmann::json(nullptr)},
            {"kind", dimension.kind == zima::kernel::ViewerDimensionKind::Angular
                ? "angular"
                : dimension.kind == zima::kernel::ViewerDimensionKind::Radius
                    ? "radius"
                    : dimension.kind == zima::kernel::ViewerDimensionKind::Diameter
                        ? "diameter" : "linear"},
            {"plane_normal", serialize_vec3(dimension.plane_normal)},
            {"measurement_direction",dimension.measurement_direction?serialize_vec3(*dimension.measurement_direction):nlohmann::json(nullptr)},
            {"sweep_degrees", dimension.sweep_degrees},
            {"arrows_reversed", dimension.arrows_reversed},
            {"radius_center_line_hidden", dimension.radius_center_line_hidden},
            {"label_position", dimension.label_position ? serialize_vec3(*dimension.label_position) : nlohmann::json(nullptr)},
        });
    }
    nlohmann::json packet = {
        {"calculation_errors", result.calculation_errors},
        {"volume", result.physical_properties_current?nlohmann::json(result.volume):
            nlohmann::json{{"pending",true},{"solid",result.solid_present}}},
        {"surface_area", result.physical_properties_current?nlohmann::json(result.surface_area):nlohmann::json(nullptr)},
        {"surface_centroid",result.surface_centroid?serialize_vec3(*result.surface_centroid):nlohmann::json(nullptr)},
        {"volume_integrals",result.volume_integrals?nlohmann::json{
            {"centroid",serialize_vec3(result.volume_integrals->centroid)},
            {"inertia",result.volume_integrals->inertia}}:nlohmann::json(nullptr)},
        {"source_fingerprint", result.source_fingerprint},
        {"viewer_binary_encoding", "f64le-u32le-base64-v1"},
    };
    // An initializer_list copies its const elements even when its expressions
    // use std::move. Transfer these large independently owned tables directly.
    packet["kernel_shape"]=result.kernel_shape.get();
    packet["vertices_binary"]=pack_vertices(result.mesh.vertices);
    packet["triangles_binary"]=pack_indices(result.mesh.triangles);
    packet["triangle_references"]=std::move(faces);
    packet["edges"]=std::move(edges);packet["points"]=std::move(points);
    packet["axes"]=std::move(axes);packet["dimensions"]=std::move(dimensions);
    packet["original_references"]=serialize_reference_geometry(
        reference_geometry?*reference_geometry:result.mesh.original_references);
    if (!result.kernel_placements.empty()) {
        packet["kernel_placements"] = nlohmann::json::array();
        for (const auto& placement : result.kernel_placements)
            packet["kernel_placements"].push_back({
                {"translation", serialize_vec3(placement.translation)},
                {"rotation_degrees", serialize_vec3(placement.rotation_degrees)}});
    }
    if (result.kernel_shape == zima::kernel::kernel_body_compound)
        packet["kernel_compound_bodies"] = result.kernel_compound_bodies;
    const auto bindings=[&](const auto& values) {
        auto rows=nlohmann::json::array();
        for(const auto& value:values) {
            const auto& ref=value.reference;
            nlohmann::json row={{"locator",value.locator},{"owner",ref.owner_id},
                {"key",ref.semantic_key},{"instance_path",ref.instance_path}};
            if constexpr(std::is_same_v<std::decay_t<decltype(ref)>,zima::kernel::FaceReference>) {
                row["surface_result"]=ref.surface_result;row["sheet_role"]=static_cast<int>(ref.sheet_role);
                row["sheet_thickness"]=ref.sheet_thickness;row["sheet_owner"]=ref.sheet_owner;
                row["display_owner"]=ref.display_owner_id;
                if(ref.surface)row["surface"]=serialize_surface(*ref.surface);
                if(ref.contour_surface)row["contour_surface"]=serialize_surface(*ref.contour_surface);
                if(ref.measured_area)row["measured_area"]=*ref.measured_area;
            } else if constexpr(std::is_same_v<std::decay_t<decltype(ref)>,zima::kernel::EdgeReference>) {
                row["display_owner"]=ref.display_owner_id;
            }
            rows.push_back(std::move(row));
        }
        return rows;
    };
    packet["kernel_bindings"]={{"faces",bindings(result.kernel_faces)},
        {"edges",bindings(result.kernel_edges)},{"vertices",bindings(result.kernel_vertices)}};
    if(!result.mesh.images.empty()) {
        packet["images"]=nlohmann::json::array();
        for(const auto& image:result.mesh.images) {
            auto corners=nlohmann::json::array();for(const auto& p:image.corners)corners.push_back(serialize_vec3(p));
            packet["images"].push_back({{"corners",corners},{"owner",image.reference.owner_id},
                {"key",image.reference.semantic_key},{"instance_path",image.reference.instance_path},{"data_base64",image.data_base64},{"format",image.format}});
        }
    }
    packet["annotation_frames"]=annotation_frames_json(result.mesh.annotation_frames);
    if(!result.measurement_sources.empty()) {
        auto sources=nlohmann::json::array();
        for(const auto& source:result.measurement_sources)sources.push_back(serialize_body_result(source,false,false));
        packet["measurement_sources"]=std::move(sources);
    }
    if(result.incremental_history&&!result.incremental_history->frames.empty()&&
       result.incremental_history->frames.back().fingerprint==result.source_fingerprint&&result.kernel_placements.empty()&&
       result.kernel_shape!=zima::kernel::kernel_body_compound) {
        const auto& history=*result.incremental_history;
        const auto native=[&](const zima::kernel::BodySnapshot& snapshot) {
            if(snapshot->incremental_history)throw std::runtime_error("Invalid native topology binding");
            return serialize_body_result(snapshot,false,false);
        };
        auto serialized_frames=process_rows<nlohmann::json>(history.frames.size(),[&](std::size_t index) {
            const auto& frame=history.frames[index];
            if(!frame.reference_delta)throw std::runtime_error("Invalid native topology binding");
            auto layers=nlohmann::json::array();for(const auto& layer:frame.original_layers)layers.push_back(native(layer));
            nlohmann::json row={{"fingerprint",frame.fingerprint},{"native",native(frame.native_body)},
                {"hidden_edges",frame.hidden_edges},{"originals",std::move(layers)},
                {"references",serialize_reference_geometry(*frame.reference_delta)},
                {"original_edges_retained",frame.original_edges_retained}};
            if(frame.copy_source)row["copy"]={{"operand",native(frame.copy_source->operand)},
                {"caps",native(frame.copy_source->source_caps)},{"solid",native(frame.copy_source->solid)},
                {"empty_material_operand",frame.copy_source->empty_material_operand},{"reference_only",frame.copy_source->reference_only}};
            return row;
        });
        auto frames=nlohmann::json::array();
        for(auto& frame:serialized_frames)frames.push_back(std::move(frame));
        packet["incremental_history"]={{"algorithm",history.algorithm},{"frames",std::move(frames)}};
    }
    if(!result.solid_state_reference_views.empty()) {
        auto views=nlohmann::json::object();
        for(const auto& [owner,geometry]:result.solid_state_reference_views) {
            if(owner.empty()||!geometry)throw std::invalid_argument("History reference view is invalid.");
            views[owner]=serialize_reference_geometry(*geometry);
        }
        packet["solid_state_reference_views"]=std::move(views);
    }
    if(!result.sheet_cuts.empty()) {
        auto regions=nlohmann::json::array();
        for(const auto& region:result.sheet_cuts) {
            auto loops=nlohmann::json::array();
            for(const auto& loop:region.loops) {
                auto curves=nlohmann::json::array();
                for(const auto& curve:loop)curves.push_back({{"parent_owner",curve.parent_owner},{"parent_key",curve.parent_key},
                    {"degree",curve.degree},{"poles",curve.poles},{"weights",curve.weights},{"knots",curve.knots},
                    {"multiplicities",curve.multiplicities},{"reversed",curve.reversed}});
                loops.push_back(std::move(curves));
            }
            regions.push_back({{"cut_owner",region.cut_owner},{"source_owner",region.source.owner_id},
                {"sheet_owner",region.source.sheet_owner},
                {"source_key",region.source.semantic_key},{"source_path",region.source.instance_path},
                {"surface_type",region.surface_type},{"origin",serialize_vec3(region.origin)},
                {"surface_reversed",region.source.surface&&region.source.surface->reversed},
                {"axis",serialize_vec3(region.axis)},{"x_axis",serialize_vec3(region.x_axis)},
                {"y_axis",serialize_vec3(region.y_axis)},
                {"radius",region.radius},{"semi_angle",region.semi_angle},{"thickness",region.thickness},{"loops",loops}});
        }
        packet["sheet_cuts"]=std::move(regions);
    }
    // Part aggregates own branch caches; an Assembly occurrence stores its
    // calculated child packets by occurrence ID, without Part history rows.
    if (include_histories && (!result.body_boundaries.empty() || !result.body_inputs.empty() || !result.body_outputs.empty())) {
        auto histories = nlohmann::json::object();
        auto inputs = nlohmann::json::object();
        auto outputs = nlohmann::json::object();
        for (const auto& [id, boundaries] : result.body_boundaries) {
            auto serialized_rows=process_rows<nlohmann::json>(boundaries.size(),[&](std::size_t index) {
                return serialize_body_result(boundaries[index]);
            });
            auto rows = nlohmann::json::array();
            for (auto& row:serialized_rows) rows.push_back(std::move(row));
            histories[id] = std::move(rows);
        }
        for (const auto& [id, body] : result.body_inputs) inputs[id] = serialize_body_result(body);
        for (const auto& [id, body] : result.body_outputs) outputs[id] = serialize_body_result(body);
        auto& body_histories=packet["body_histories"];
        body_histories["boundaries"]=std::move(histories);
        body_histories["inputs"]=std::move(inputs);
        body_histories["outputs"]=std::move(outputs);
    } else if (include_compound_operands && result.kernel_shape == zima::kernel::kernel_body_compound) {
        auto outputs = nlohmann::json::object();
        for (const auto& id : result.kernel_compound_bodies)
            outputs[id] = serialize_body_result(result.body_outputs.at(id), false);
        auto& body_histories=packet["body_histories"];
        body_histories["boundaries"]=nlohmann::json::object();
        body_histories["inputs"]=nlohmann::json::object();
        body_histories["outputs"]=std::move(outputs);
    }
    return packet;
}

nlohmann::json serialize_viewer_reference_geometry(
    const zima::kernel::ViewerReferenceGeometry& geometry) {
    return serialize_reference_geometry(geometry);
}

zima::kernel::ViewerReferenceGeometry load_viewer_reference_geometry(
    const nlohmann::json& source) {
    return load_reference_geometry(source);
}

zima::kernel::BodyResult load_body_result(const nlohmann::json& source,
        std::map<std::string,zima::kernel::BodySnapshot> compound_operands) {
    zima::kernel::BodyResult result;
    if(const auto sources=source.find("measurement_sources");sources!=source.end()) {
        if(!sources->is_array())throw std::runtime_error("Invalid native topology binding");
        std::vector<zima::kernel::BodySnapshot> loaded;loaded.reserve(sources->size());
        for(const auto& snapshot:*sources) {
            auto native=load_body_result(snapshot);
            if(native.kernel_shape.empty()||native.kernel_faces.empty()||!native.measurement_sources.empty())
                throw std::runtime_error("Invalid native topology binding");
            loaded.emplace_back(std::move(native));
        }
        result.measurement_sources=std::move(loaded);
    }
    // Continuation metadata is a disposable calculation cache. Corruption or
    // a stale schema must not hide the valid authored document/display packet.
    try {
    if(const auto cached=source.find("incremental_history");cached!=source.end()) {
        if(!cached->is_object()||!cached->contains("algorithm")||!cached->at("algorithm").is_string()||
            !cached->contains("frames")||!cached->at("frames").is_array()||cached->at("frames").size()>50000)
            throw std::runtime_error("Invalid native topology binding");
        auto history=std::make_shared<zima::kernel::KernelHistoryCache>();history->algorithm=cached->at("algorithm");
        const auto& source_frames=cached->at("frames");
        const auto native=[&](const nlohmann::json& value)->zima::kernel::BodySnapshot {
            if(value.contains("incremental_history"))throw std::runtime_error("Invalid native topology binding");
            return load_body_result(value);
        };
        auto frames=process_rows<zima::kernel::KernelHistoryFrame>(source_frames.size(),[&](std::size_t index) {
            const auto& row=source_frames.at(index);
            zima::kernel::KernelHistoryFrame frame;frame.fingerprint=row.at("fingerprint");
            frame.native_body=native(row.at("native"));frame.hidden_edges=row.at("hidden_edges").get<std::vector<std::string>>();
            frame.reference_delta=std::make_shared<const zima::kernel::ViewerReferenceGeometry>(load_reference_geometry(row.at("references")));
            frame.original_edges_retained=row.at("original_edges_retained");
            if(!row.at("originals").is_array()||row.at("originals").size()>50000)throw std::runtime_error("Invalid native topology binding");
            for(const auto& layer:row.at("originals"))frame.original_layers.push_back(native(layer));
            if(const auto copy=row.find("copy");copy!=row.end())frame.copy_source=zima::kernel::KernelHistoryCopy{
                native(copy->at("operand")),native(copy->at("caps")),native(copy->at("solid")),
                copy->at("empty_material_operand"),copy->at("reference_only")};
            return frame;
        });
        history->frames=std::move(frames);result.incremental_history=std::move(history);
    }
    }catch(const std::bad_alloc&){throw;}
     catch(const std::exception&){result.incremental_history.reset();}
    result.body_outputs=std::move(compound_operands);
    if(const auto views=source.find("solid_state_reference_views");views!=source.end())
        for(const auto& [owner,geometry]:views->items()) {
            if(owner.empty())throw std::invalid_argument("History reference view is invalid.");
            result.solid_state_reference_views.emplace(owner,
                std::make_shared<const zima::kernel::ViewerReferenceGeometry>(load_reference_geometry(geometry)));
        }
    for(const auto& row:source.value("sheet_cuts",nlohmann::json::array())) {
        zima::kernel::SheetCutRegion region;region.cut_owner=row.at("cut_owner");
        region.source={row.at("source_owner"),row.at("source_key"),row.at("source_path")};
        region.source.sheet_owner=row.at("sheet_owner");
        region.source.sheet_role=zima::kernel::SheetFaceRole::SideA;
        region.surface_type=row.at("surface_type");region.origin=load_vec3(row.at("origin"));
        region.axis=load_vec3(row.at("axis"));region.x_axis=load_vec3(row.at("x_axis"));
        region.y_axis=load_vec3(row.at("y_axis"));
        region.radius=row.at("radius");region.semi_angle=row.at("semi_angle");region.thickness=row.at("thickness");
        region.source.sheet_thickness=region.thickness;
        if(region.surface_type!="twist") {
            auto geometry=std::make_shared<zima::kernel::SurfaceGeometry>();
            geometry->kind=region.surface_type=="plane"?zima::kernel::SurfaceGeometry::Kind::Plane:
                region.surface_type=="cylinder"?zima::kernel::SurfaceGeometry::Kind::Cylinder:zima::kernel::SurfaceGeometry::Kind::Cone;
            geometry->origin=region.origin;geometry->axis=region.axis;geometry->radial=region.x_axis;
            geometry->radius=region.radius;geometry->semi_angle=region.semi_angle;geometry->reversed=row.at("surface_reversed");
            region.source.surface=std::move(geometry);
        }
        for(const auto& loop:row.at("loops")) {
            std::vector<zima::kernel::SheetTrimCurve> curves;
            for(const auto& curve:loop) {
                zima::kernel::SheetTrimCurve c;c.parent_owner=curve.at("parent_owner");c.parent_key=curve.at("parent_key");
                c.degree=curve.at("degree");c.poles=curve.at("poles").get<decltype(c.poles)>();c.weights=curve.at("weights").get<decltype(c.weights)>();
                c.knots=curve.at("knots").get<decltype(c.knots)>();c.multiplicities=curve.at("multiplicities").get<decltype(c.multiplicities)>();c.reversed=curve.at("reversed");
                curves.push_back(std::move(c));
            }region.loops.push_back(std::move(curves));
        }result.sheet_cuts.push_back(std::move(region));
    }
    result.calculation_errors = source.value("calculation_errors", std::map<std::string, std::string>{});
    if (const auto caches = source.find("body_histories"); caches != source.end()) {
        for (const auto& [id, rows] : caches->at("boundaries").items()) {
            auto loaded=process_rows<zima::kernel::BodyResult>(rows.size(),[&](std::size_t index) {
                return load_body_result(rows.at(index));
            });
            result.body_boundaries[id]=std::move(loaded);
        }
        for (const auto& [id, body] : caches->at("inputs").items())
            result.body_inputs.emplace(id, load_body_result(body));
        for (const auto& [id, body] : caches->at("outputs").items())
            result.body_outputs.emplace(id, load_body_result(body));
    }
    for(const auto& row:source.value("images",nlohmann::json::array())) {
        zima::kernel::ViewerImage image;
        if(row.at("corners").size()!=4)throw std::runtime_error("Invalid image corners");
        for(std::size_t i=0;i<4;++i)image.corners[i]=load_vec3(row.at("corners").at(i));
        image.reference={row.at("owner"),row.at("key"),row.at("instance_path")};image.data_base64=row.at("data_base64");image.format=row.at("format");
        result.mesh.images.push_back(std::move(image));
    }
    result.mesh.annotation_frames=annotation_frames_from_json(source.value("annotation_frames",nlohmann::json::array()));
    const auto& volume=source.at("volume");
    result.physical_properties_current=!volume.is_object();
    if(result.physical_properties_current) {
        result.volume=volume.get<double>();
        result.surface_area=source.at("surface_area").get<double>();
        result.solid_present=result.volume>0.;
    } else {
        if(volume.size()!=2||!volume.at("pending").get<bool>()||!source.at("surface_area").is_null()||
            !source.at("surface_centroid").is_null()||!source.at("volume_integrals").is_null())
            throw std::runtime_error("Calculated solid snapshot is invalid");
        result.solid_present=volume.at("solid").get<bool>();
    }
    if(!source.at("surface_centroid").is_null())result.surface_centroid=load_vec3(source.at("surface_centroid"));
    if(!source.at("volume_integrals").is_null()) {
        zima::kernel::VolumeIntegrals p;
        p.centroid=load_vec3(source.at("volume_integrals").at("centroid"));
        p.inertia=source.at("volume_integrals").at("inertia").get<std::array<double,9>>();
        for(double x:p.inertia)require_finite(x,"volume inertia");
        result.volume_integrals=p;
    }
    result.source_fingerprint = source.at("source_fingerprint").get<std::string>();
    result.kernel_shape = source.at("kernel_shape").get<std::string>();
    for (const auto& placement : source.value("kernel_placements", nlohmann::json::array()))
        result.kernel_placements.push_back({load_vec3(placement.at("translation")),
            load_vec3(placement.at("rotation_degrees"))});
    result.kernel_compound_bodies = source.value("kernel_compound_bodies", std::vector<std::string>{});
    if (result.kernel_shape == zima::kernel::kernel_body_compound) {
        if (!source.contains("kernel_compound_bodies"))
            throw std::runtime_error("Calculated solid snapshot is invalid");
        std::set<std::string> seen;
        for (const auto& id : result.kernel_compound_bodies)
            if (id.empty() || !seen.insert(id).second || !result.body_outputs.contains(id))
                throw std::runtime_error("Calculated solid snapshot is invalid");
    } else if (!result.kernel_compound_bodies.empty() ||
            (result.kernel_shape.empty() && !result.kernel_placements.empty())) {
        throw std::runtime_error("Calculated solid snapshot is invalid");
    }
    if(const auto bindings=source.find("kernel_bindings");bindings!=source.end()) {
        const auto load=[&](const char* kind,auto& values) {
            using Binding=typename std::decay_t<decltype(values)>::value_type;
            std::vector<Binding> captured;captured.reserve(bindings->at(kind).size());
            for(const auto& row:bindings->at(kind)) {
                Binding value;value.locator=row.at("locator");
                auto& ref=value.reference;ref={row.at("owner"),row.at("key"),row.at("instance_path")};
                if(!ref.valid()||value.locator.empty())throw std::runtime_error("Invalid native topology binding");
                if constexpr(std::is_same_v<std::decay_t<decltype(ref)>,zima::kernel::FaceReference>) {
                    ref.surface_result=row.at("surface_result");load_sheet_face(ref,row);
                    if(const auto v=row.find("surface");v!=row.end())ref.surface=std::make_shared<zima::kernel::SurfaceGeometry>(load_surface(*v));
                    if(const auto v=row.find("contour_surface");v!=row.end())ref.contour_surface=std::make_shared<zima::kernel::SurfaceGeometry>(load_surface(*v));
                    if(const auto v=row.find("measured_area");v!=row.end()){const double area=*v;require_finite(area,"measured area");ref.measured_area=area;}
                } else if constexpr(std::is_same_v<std::decay_t<decltype(ref)>,zima::kernel::EdgeReference>) {
                    ref.display_owner_id=row.value("display_owner",std::string{});
                }
                captured.push_back(std::move(value));
            }
            values=std::move(captured);
        };
        load("faces",result.kernel_faces);load("edges",result.kernel_edges);load("vertices",result.kernel_vertices);
    }
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
        result.mesh.triangle_references.back().surface_result=reference.at("surface_result").get<bool>();
        load_sheet_face(result.mesh.triangle_references.back(),reference);
    }
    if (result.mesh.triangle_references.empty()) {
        result.mesh.triangle_references.resize(result.mesh.triangles.size() / 3);
    }
    if (result.mesh.triangles.size() % 3 != 0 ||
        result.mesh.triangle_references.size() != result.mesh.triangles.size() / 3) {
        throw std::runtime_error("Viewer triangle references are not aligned");
    }
    for (const auto& reference : result.mesh.triangle_references) {
        if (reference.owner_id.empty() != reference.semantic_key.empty()) {
            throw std::runtime_error("Persisted viewer triangle tag is invalid");
        }
    }
    for (const auto& edge : source.at("edges")) {
        zima::kernel::ViewerEdge loaded;
        loaded.reference = {
            edge.at("owner").get<std::string>(), edge.at("key").get<std::string>(),
            edge.at("instance_path").get<std::string>(),
            edge.value("display_owner",std::string{})};
        loaded.display_owner_id =
            edge.at("display_owner").get<std::string>();
        loaded.color=edge.value("color",std::string{});
        loaded.exact_spline = zima::kernel::spline_from_json(edge.at("exact_spline"));
        if (edge.contains("measured_length") && !edge.at("measured_length").is_null()) {
            loaded.measured_length = edge.at("measured_length").get<double>();
            require_finite(*loaded.measured_length, "measured length");
            if (*loaded.measured_length < 0) throw std::runtime_error("Negative measured length");
        }
        loaded.surface_result=edge.at("surface_result").get<bool>();
        loaded.parameter_seam = edge.value("parameter_seam", false);
        loaded.filled_text = edge.value("filled_text", false);
        loaded.edge_treatment_owner_ids =
            edge.at("edge_treatment_owners").get<std::vector<std::string>>();
        for (const auto& point : edge.at("points")) loaded.points.push_back(load_vec3(point));
        for (const auto& side : edge.at("edge_treatment_side_directions")) {
            std::vector<zima::kernel::Vec3> samples;
            for (const auto& direction : side) {
                samples.push_back(load_vec3(direction));
            }
            if (samples.size() != loaded.points.size()) {
                throw std::runtime_error(
                    "Persisted viewer edge side directions are not aligned");
            }
            loaded.edge_treatment_side_directions.push_back(
                std::move(samples));
        }
        for (const auto& reference :
             edge.at("edge_treatment_side_references")) {
            zima::kernel::FaceReference loaded_reference{
                reference.at("owner").get<std::string>(),
                reference.at("key").get<std::string>(),
                reference.at("instance_path").get<std::string>()};
            if (!loaded_reference.valid()) {
                throw std::runtime_error(
                    "Persisted viewer treatment side reference is invalid");
            }
            load_sheet_face(loaded_reference,reference);
            loaded.edge_treatment_side_references.push_back(
                std::move(loaded_reference));
        }
        for (const auto& reference :
             edge.at("edge_treatment_endpoint_references")) {
            zima::kernel::VertexReference loaded_reference{
                reference.at("owner").get<std::string>(),
                reference.at("key").get<std::string>(),
                reference.at("instance_path").get<std::string>()};
            if (!loaded_reference.valid()) {
                throw std::runtime_error(
                    "Persisted viewer treatment endpoint reference is invalid");
            }
            loaded.edge_treatment_endpoint_references.push_back(
                std::move(loaded_reference));
        }
        if (!loaded.edge_treatment_side_directions.empty() &&
            (loaded.edge_treatment_side_directions.size() != 2 ||
             loaded.edge_treatment_side_references.size() != 2)) {
            throw std::runtime_error(
                "Persisted viewer edge must have exactly two treatment sides");
        }
        if (!loaded.edge_treatment_endpoint_references.empty() &&
            loaded.edge_treatment_endpoint_references.size() != 2) {
            throw std::runtime_error(
                "Persisted viewer edge must have exactly two treatment endpoints");
        }
        const bool owner_empty = loaded.reference.owner_id.empty();
        const bool key_empty = loaded.reference.semantic_key.empty();
        // Viewer-only body edges deliberately carry an occurrence path without
        // becoming persistent topology references. This lets selection recolour
        // the existing wire for one exact nested occurrence.
        if (owner_empty != key_empty || loaded.points.size() < 2) {
            throw std::runtime_error("Persisted viewer edge is invalid");
        }
        restore_reference_curve_style(loaded);
        result.mesh.edges.push_back(std::move(loaded));
    }
    for (const auto& point : source.at("points")) {
        zima::kernel::ViewerPoint loaded;
        loaded.reference = {
            point.at("owner").get<std::string>(), point.at("key").get<std::string>(),
            point.at("instance_path").get<std::string>()};
        loaded.position = load_vec3(point.at("position"));
        loaded.surface_result=point.at("surface_result").get<bool>();
        // BodyResult points are solid vertices used as exact reference
        // targets. They are never standalone visible Point entities. The
        // explicit kernel calculation applies the same invariant; restore it
        // after cache loading instead of accepting ViewerPoint's general
        // purpose always-visible default.
        loaded.always_visible = false;
        if (axis_endpoint(loaded.reference.semantic_key)) {
            loaded.always_visible = true;
            loaded.display_owner_id = loaded.reference.owner_id;
        }
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
        if(dimension.contains("source_text_style")&&!dimension.at("source_text_style").is_null())loaded.source_text_style=dimension_text_style_from_json(dimension.at("source_text_style"));
        const auto kind = dimension.at("kind").get<std::string>();
        loaded.kind = kind == "angular"
            ? zima::kernel::ViewerDimensionKind::Angular
            : kind == "radius" ? zima::kernel::ViewerDimensionKind::Radius
            : kind == "diameter" ? zima::kernel::ViewerDimensionKind::Diameter
                                 : zima::kernel::ViewerDimensionKind::Linear;
        loaded.plane_normal = load_vec3(dimension.at("plane_normal"));
        if(!dimension.at("measurement_direction").is_null())loaded.measurement_direction=load_vec3(dimension.at("measurement_direction"));
        loaded.sweep_degrees = dimension.at("sweep_degrees").get<double>();
        loaded.arrows_reversed = dimension.value("arrows_reversed", false);
        loaded.radius_center_line_hidden = dimension.value("radius_center_line_hidden", false);
        if(dimension.contains("label_position") && !dimension.at("label_position").is_null())
            loaded.label_position = load_vec3(dimension.at("label_position"));
        require_finite(loaded.value, "viewer dimension value");
        require_finite(loaded.sweep_degrees, "viewer dimension sweep");
        if (!loaded.reference.valid()) {
            throw std::runtime_error("Persisted viewer dimension is invalid");
        }
        result.mesh.dimensions.push_back(std::move(loaded));
    }
    // Rotation datums are visible presentation of the same persisted
    // reference geometry. Reconstruct their display list without recalculation.
    const auto changed_by_solid_state=[&](const auto& reference) {
        return std::ranges::any_of(result.mesh.triangle_references,[&](const auto& face) {
            return face.display_owner_id==reference.owner_id&&face.instance_path==reference.instance_path&&
                zima::kernel::solid_state_parent(face.semantic_key).has_value();
        });
    };
    for(const auto& axis:result.mesh.original_references.axes)
        if(axis.reference.semantic_key.starts_with("helical:rotation-axis:")&&!changed_by_solid_state(axis.reference)&&
            std::none_of(result.mesh.axes.begin(),result.mesh.axes.end(),[&](const auto& value){return value.reference==axis.reference;}))
            result.mesh.axes.push_back(axis);
    for(const auto& point:result.mesh.original_references.points)
        if(point.reference.semantic_key.starts_with("helical:axis-point:")&&!changed_by_solid_state(point.reference)&&
            std::none_of(result.mesh.points.begin(),result.mesh.points.end(),[&](const auto& value){return value.reference==point.reference;}))
            result.mesh.points.push_back(point);
    return result;
}

}  // namespace zima::document
