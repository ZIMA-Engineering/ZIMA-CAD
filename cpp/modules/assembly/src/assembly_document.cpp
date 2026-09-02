#include <zima/assembly/assembly_document.hpp>
#include <zima/document/versioned_file.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/stable_id.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <fstream>
#include <random>
#include <optional>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace zima::assembly {
namespace {

using IniSections = std::map<std::string, std::map<std::string, std::string>>;

// Keep Assembly placement-reference dimensions consistent with Part feature
// parameters: a numerically zero value is still a valid stored constraint,
// but it must not paint a meaningless dimension in the 3D view.
constexpr double visible_placement_dimension_epsilon = 1.0e-9;

std::string trim_ini(std::string value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r");
    return value.substr(first, last - first + 1);
}

IniSections read_ini(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open assembly: " + path.string());
    IniSections result;
    std::string line;
    std::string section;
    while (std::getline(input, line)) {
        line = trim_ini(std::move(line));
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = trim_ini(line.substr(1, line.size() - 2));
            if (section.empty()) throw std::runtime_error("Invalid INI section");
            result.try_emplace(section);
            continue;
        }
        const auto separator = line.find('=');
        if (section.empty() || separator == std::string::npos) {
            throw std::runtime_error("Invalid INI document line");
        }
        result[section][trim_ini(line.substr(0, separator))] =
            trim_ini(line.substr(separator + 1));
    }
    if (!input.eof()) throw std::runtime_error("Cannot read assembly: " + path.string());
    return result;
}

std::string ini_value(
    const IniSections& ini, const std::string& section, const std::string& key,
    std::string fallback = {}) {
    const auto found_section = ini.find(section);
    if (found_section == ini.end()) return fallback;
    const auto found = found_section->second.find(key);
    return found == found_section->second.end() ? fallback : found->second;
}

void write_ini(const std::filesystem::path& path, const IniSections& ini) {
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot write assembly: " + path.string());
        for (const auto& [section, values] : ini) {
            output << "[" << section << "]\n";
            for (const auto& [key, value] : values) output << key << "=" << value << "\n";
            output << "\n";
        }
        if (!output) throw std::runtime_error("Assembly write failed: " + path.string());
    }
    zima::document::archive_existing_file(path);
    std::filesystem::rename(temporary, path);
}

void append_viewer_mesh(zima::kernel::ViewerMesh& target,
                        zima::kernel::ViewerMesh source) {
    const auto offset = static_cast<std::uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(),
        source.vertices.begin(), source.vertices.end());
    for (const auto index : source.triangles) target.triangles.push_back(offset + index);
    target.triangle_references.insert(target.triangle_references.end(),
        source.triangle_references.begin(), source.triangle_references.end());
    target.edges.insert(target.edges.end(), source.edges.begin(), source.edges.end());
    target.points.insert(target.points.end(), source.points.begin(), source.points.end());
    target.axes.insert(target.axes.end(), source.axes.begin(), source.axes.end());
    target.dimensions.insert(target.dimensions.end(),
        source.dimensions.begin(), source.dimensions.end());
    target.constraint_markers.insert(target.constraint_markers.end(),
        source.constraint_markers.begin(), source.constraint_markers.end());
    auto& references = target.original_references;
    auto& incoming = source.original_references;
    const auto reference_offset = static_cast<std::uint32_t>(references.vertices.size());
    references.vertices.insert(references.vertices.end(),
        incoming.vertices.begin(), incoming.vertices.end());
    for (const auto index : incoming.triangles) {
        references.triangles.push_back(reference_offset + index);
    }
    references.triangle_references.insert(references.triangle_references.end(),
        incoming.triangle_references.begin(), incoming.triangle_references.end());
    references.edges.insert(references.edges.end(),
        incoming.edges.begin(), incoming.edges.end());
    references.points.insert(references.points.end(),
        incoming.points.begin(), incoming.points.end());
    references.axes.insert(references.axes.end(),
        incoming.axes.begin(), incoming.axes.end());
}

std::string make_id() {
    return zima::kernel::make_stable_id();
}

zima::kernel::Vec3 transform_point(
    const zima::kernel::Vec3& source, const ComponentPlacement& placement) {
    constexpr double radians = 3.14159265358979323846 / 180.0;
    const double cx = std::cos(placement.rotation_x * radians);
    const double sx = std::sin(placement.rotation_x * radians);
    const double cy = std::cos(placement.rotation_y * radians);
    const double sy = std::sin(placement.rotation_y * radians);
    const double cz = std::cos(placement.rotation_z * radians);
    const double sz = std::sin(placement.rotation_z * radians);
    const zima::kernel::Vec3 rotated_x{
        source.x, cx * source.y - sx * source.z,
        sx * source.y + cx * source.z};
    const zima::kernel::Vec3 rotated_y{
        cy * rotated_x.x + sy * rotated_x.z, rotated_x.y,
        -sy * rotated_x.x + cy * rotated_x.z};
    return {
        cz * rotated_y.x - sz * rotated_y.y + placement.x,
        sz * rotated_y.x + cz * rotated_y.y + placement.y,
        rotated_y.z + placement.z,
    };
}

zima::kernel::Vec3 transform_direction(
    const zima::kernel::Vec3& source, ComponentPlacement placement) {
    placement.x = 0.0;
    placement.y = 0.0;
    placement.z = 0.0;
    return transform_point(source, placement);
}

struct RotationMatrix {
    double value[3][3]{};
};

double dot(const zima::kernel::Vec3& a, const zima::kernel::Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

zima::kernel::Vec3 cross(const zima::kernel::Vec3& a, const zima::kernel::Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

double length(const zima::kernel::Vec3& value) {
    return std::sqrt(dot(value, value));
}

zima::kernel::Vec3 multiply(
    const RotationMatrix& matrix, const zima::kernel::Vec3& value) {
    return {
        matrix.value[0][0] * value.x + matrix.value[0][1] * value.y +
            matrix.value[0][2] * value.z,
        matrix.value[1][0] * value.x + matrix.value[1][1] * value.y +
            matrix.value[1][2] * value.z,
        matrix.value[2][0] * value.x + matrix.value[2][1] * value.y +
            matrix.value[2][2] * value.z};
}

RotationMatrix multiply(const RotationMatrix& left, const RotationMatrix& right) {
    RotationMatrix result;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            for (int index = 0; index < 3; ++index) {
                result.value[row][column] +=
                    left.value[row][index] * right.value[index][column];
            }
        }
    }
    return result;
}

RotationMatrix placement_rotation(const ComponentPlacement& placement) {
    constexpr double radians = 3.14159265358979323846 / 180.0;
    const double cx = std::cos(placement.rotation_x * radians);
    const double sx = std::sin(placement.rotation_x * radians);
    const double cy = std::cos(placement.rotation_y * radians);
    const double sy = std::sin(placement.rotation_y * radians);
    const double cz = std::cos(placement.rotation_z * radians);
    const double sz = std::sin(placement.rotation_z * radians);
    return {{{cy * cz, cz * sx * sy - cx * sz, sx * sz + cx * cz * sy},
             {cy * sz, cx * cz + sx * sy * sz, cx * sy * sz - cz * sx},
             {-sy, cy * sx, cx * cy}}};
}

RotationMatrix shortest_rotation(
    const zima::kernel::Vec3& source, const zima::kernel::Vec3& target) {
    constexpr double epsilon = 1.0e-12;
    const auto axis = cross(source, target);
    const double sine = length(axis);
    const double cosine = std::clamp(dot(source, target), -1.0, 1.0);
    if (sine < epsilon && cosine > 0.0) {
        return {{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
    }
    zima::kernel::Vec3 unit;
    if (sine < epsilon) {
        const zima::kernel::Vec3 basis = std::abs(source.x) < 0.9
            ? zima::kernel::Vec3{1.0, 0.0, 0.0}
            : zima::kernel::Vec3{0.0, 1.0, 0.0};
        unit = cross(source, basis);
        const double magnitude = length(unit);
        unit = {unit.x / magnitude, unit.y / magnitude, unit.z / magnitude};
    } else {
        unit = {axis.x / sine, axis.y / sine, axis.z / sine};
    }
    const double rotation_sine = sine < epsilon ? 0.0 : sine;
    const double one_minus_cosine = 1.0 - cosine;
    return {{{
        cosine + unit.x * unit.x * one_minus_cosine,
        unit.x * unit.y * one_minus_cosine - unit.z * rotation_sine,
        unit.x * unit.z * one_minus_cosine + unit.y * rotation_sine}, {
        unit.y * unit.x * one_minus_cosine + unit.z * rotation_sine,
        cosine + unit.y * unit.y * one_minus_cosine,
        unit.y * unit.z * one_minus_cosine - unit.x * rotation_sine}, {
        unit.z * unit.x * one_minus_cosine - unit.y * rotation_sine,
        unit.z * unit.y * one_minus_cosine + unit.x * rotation_sine,
        cosine + unit.z * unit.z * one_minus_cosine}}};
}

zima::kernel::Vec3 nearest_direction_at_angle(
    const zima::kernel::Vec3& source,
    const zima::kernel::Vec3& reference,
    double requested_radians) {
    const double projection = dot(source, reference);
    zima::kernel::Vec3 tangent{
        source.x - projection * reference.x,
        source.y - projection * reference.y,
        source.z - projection * reference.z};
    double tangent_length = length(tangent);
    if (tangent_length <= 1.0e-12) {
        const zima::kernel::Vec3 basis = std::abs(reference.x) < 0.9
            ? zima::kernel::Vec3{1.0, 0.0, 0.0}
            : zima::kernel::Vec3{0.0, 1.0, 0.0};
        tangent = cross(reference, basis);
        tangent_length = length(tangent);
    }
    tangent = {tangent.x / tangent_length, tangent.y / tangent_length,
               tangent.z / tangent_length};
    return {
        reference.x * std::cos(requested_radians) +
            tangent.x * std::sin(requested_radians),
        reference.y * std::cos(requested_radians) +
            tangent.y * std::sin(requested_radians),
        reference.z * std::cos(requested_radians) +
            tangent.z * std::sin(requested_radians)};
}

void set_placement_rotation(ComponentPlacement& placement, const RotationMatrix& rotation) {
    constexpr double degrees = 180.0 / 3.14159265358979323846;
    const double y = std::asin(std::clamp(-rotation.value[2][0], -1.0, 1.0));
    const double cy = std::cos(y);
    double x{};
    double z{};
    if (std::abs(cy) > 1.0e-10) {
        x = std::atan2(rotation.value[2][1], rotation.value[2][2]);
        z = std::atan2(rotation.value[1][0], rotation.value[0][0]);
    } else {
        x = 0.0;
        z = std::atan2(-rotation.value[0][1], rotation.value[1][1]);
    }
    placement.rotation_x = x * degrees;
    placement.rotation_y = y * degrees;
    placement.rotation_z = z * degrees;
}

void rotate_occurrence_about(
    PartOccurrence& occurrence, const RotationMatrix& world_rotation,
    const zima::kernel::Vec3& fixed_point) {
    const zima::kernel::Vec3 translation{
        occurrence.placement.x, occurrence.placement.y, occurrence.placement.z};
    const auto rotated_translation = multiply(world_rotation, translation);
    const auto rotated_fixed = multiply(world_rotation, fixed_point);
    occurrence.placement.x = rotated_translation.x + fixed_point.x - rotated_fixed.x;
    occurrence.placement.y = rotated_translation.y + fixed_point.y - rotated_fixed.y;
    occurrence.placement.z = rotated_translation.z + fixed_point.z - rotated_fixed.z;
    set_placement_rotation(
        occurrence.placement,
        multiply(world_rotation, placement_rotation(occurrence.placement)));
}

template <typename Reference>
void assign_instance(Reference& reference, const std::string& instance_path) {
    // Display-body triangles and edges deliberately have no persistent
    // owner/semantic identity, but still belong to one exact occurrence.
    // Carry that viewer-only occurrence path as well so whole-body colour and
    // wire selection never have to rebuild geometry to discover ownership.
    reference.instance_path = instance_path + reference.instance_path;
}

const char* source_kind_name(ComponentSourceKind kind) {
    return kind == ComponentSourceKind::Part ? "part" : "assembly";
}

ComponentSourceKind source_kind_from_name(const std::string& name) {
    if (name == "part") return ComponentSourceKind::Part;
    if (name == "assembly") return ComponentSourceKind::Assembly;
    throw std::runtime_error("Unknown component source kind");
}

const char* dependency_kind_name(ComponentDependencyKind kind) {
    switch (kind) {
    case ComponentDependencyKind::PlacementReference: return "placement_reference";
    case ComponentDependencyKind::ExternalSketchReference:
        return "external_sketch_reference";
    }
    throw std::invalid_argument("Unknown component dependency kind");
}

ComponentDependencyKind dependency_kind_from_name(const std::string& name) {
    if (name == "placement_reference") {
        return ComponentDependencyKind::PlacementReference;
    }
    if (name == "external_sketch_reference") {
        return ComponentDependencyKind::ExternalSketchReference;
    }
    throw std::runtime_error("Unknown component dependency kind");
}

const char* mate_reference_kind_name(MateReferenceKind kind) {
    switch (kind) {
    case MateReferenceKind::Face: return "face";
    case MateReferenceKind::Axis: return "axis";
    case MateReferenceKind::Point: return "point";
    }
    throw std::invalid_argument("Unknown Assembly mate reference kind");
}

MateReferenceKind mate_reference_kind_from_name(const std::string& name) {
    if (name == "face") return MateReferenceKind::Face;
    if (name == "axis") return MateReferenceKind::Axis;
    if (name == "point") return MateReferenceKind::Point;
    throw std::runtime_error("Unknown Assembly mate reference kind");
}

const char* mate_kind_name(MateKind kind) {
    switch (kind) {
    case MateKind::PlaneCoincident: return "plane_coincident";
    case MateKind::AxisCoincident: return "axis_coincident";
    case MateKind::PointCoincident: return "point_coincident";
    case MateKind::AxisAngle: return "axis_angle";
    case MateKind::PlaneAngle: return "plane_angle";
    }
    throw std::invalid_argument("Unknown Assembly mate kind");
}

MateKind mate_kind_from_name(const std::string& name) {
    if (name == "plane_coincident") return MateKind::PlaneCoincident;
    if (name == "axis_coincident") return MateKind::AxisCoincident;
    if (name == "point_coincident") return MateKind::PointCoincident;
    if (name == "axis_angle") return MateKind::AxisAngle;
    if (name == "plane_angle") return MateKind::PlaneAngle;
    throw std::runtime_error("Unknown Assembly mate kind");
}

const char* mate_status_name(MateStatus status) {
    switch (status) {
    case MateStatus::Uncalculated: return "uncalculated";
    case MateStatus::Valid: return "valid";
    case MateStatus::MissingReference: return "missing_reference";
    case MateStatus::UnsupportedGeometry: return "unsupported_geometry";
    }
    throw std::invalid_argument("Unknown Assembly mate status");
}

MateStatus mate_status_from_name(const std::string& name) {
    if (name == "uncalculated") return MateStatus::Uncalculated;
    if (name == "valid") return MateStatus::Valid;
    if (name == "missing_reference") return MateStatus::MissingReference;
    if (name == "unsupported_geometry") return MateStatus::UnsupportedGeometry;
    throw std::runtime_error("Unknown Assembly mate status");
}

nlohmann::json serialize_mate_reference(const MateReference& reference) {
    return nlohmann::json{
        {"kind", mate_reference_kind_name(reference.kind)},
        {"instance_path", reference.instance_path.encoded()},
        {"owner_id", reference.owner_id},
        {"semantic_key", reference.semantic_key}};
}

MateReference load_mate_reference(const nlohmann::json& value) {
    return MateReference{
        mate_reference_kind_from_name(value.at("kind").get<std::string>()),
        InstancePath::decode(value.at("instance_path").get<std::string>()),
        value.at("owner_id").get<std::string>(),
        value.at("semantic_key").get<std::string>()};
}

nlohmann::json serialize_placement_reference(
    const ComponentPlacementReference& reference) {
    nlohmann::json serialized{
        {"mate_type", mate_kind_name(reference.mate_type)},
        {"component_reference", serialize_mate_reference(reference.component_reference)},
        {"target_reference", serialize_mate_reference(reference.target_reference)},
        {"offset", reference.offset},
        {"flip", reference.flip}};
    if (reference.lower_limit) serialized["lower_limit"] = *reference.lower_limit;
    if (reference.upper_limit) serialized["upper_limit"] = *reference.upper_limit;
    return serialized;
}

ComponentPlacementReference load_placement_reference(const nlohmann::json& value) {
    ComponentPlacementReference reference;
    reference.mate_type = mate_kind_from_name(value.at("mate_type").get<std::string>());
    reference.component_reference =
        load_mate_reference(value.at("component_reference"));
    reference.target_reference = load_mate_reference(value.at("target_reference"));
    reference.offset = value.at("offset").get<double>();
    reference.flip = value.at("flip").get<bool>();
    if (value.contains("lower_limit")) {
        reference.lower_limit = value.at("lower_limit").get<double>();
    }
    if (value.contains("upper_limit")) {
        reference.upper_limit = value.at("upper_limit").get<double>();
    }
    return reference;
}

nlohmann::json serialize_snapshot(const OccurrenceSnapshot& snapshot) {
    nlohmann::json children = nlohmann::json::array();
    for (const auto& child : snapshot.children) children.push_back(serialize_snapshot(child));
    return {
        {"occurrence_id", snapshot.occurrence_id}, {"name", snapshot.name},
        {"source_document_id", snapshot.source_document_id},
        {"source_kind", source_kind_name(snapshot.source_kind)},
        {"manually_suppressed", snapshot.manually_suppressed},
        {"dependency_suppressed", snapshot.dependency_suppressed},
        {"visible", snapshot.visible}, {"grounded", snapshot.grounded},
        {"placement", {
            {"x", snapshot.placement.x}, {"y", snapshot.placement.y},
            {"z", snapshot.placement.z},
            {"rotation_x", snapshot.placement.rotation_x},
            {"rotation_y", snapshot.placement.rotation_y},
            {"rotation_z", snapshot.placement.rotation_z},
        }},
        {"children", std::move(children)},
    };
}

OccurrenceSnapshot load_snapshot(const nlohmann::json& source) {
    OccurrenceSnapshot snapshot;
    snapshot.occurrence_id = source.at("occurrence_id").get<std::string>();
    snapshot.name = source.at("name").get<std::string>();
    snapshot.source_document_id = source.at("source_document_id").get<std::string>();
    snapshot.source_kind = source_kind_from_name(source.at("source_kind").get<std::string>());
    snapshot.manually_suppressed = source.at("manually_suppressed").get<bool>();
    snapshot.dependency_suppressed = source.at("dependency_suppressed").get<bool>();
    snapshot.visible = source.at("visible").get<bool>();
    snapshot.grounded = source.at("grounded").get<bool>();
    const auto& placement = source.at("placement");
    snapshot.placement = {
        placement.at("x").get<double>(), placement.at("y").get<double>(),
        placement.at("z").get<double>(),
        placement.at("rotation_x").get<double>(),
        placement.at("rotation_y").get<double>(),
        placement.at("rotation_z").get<double>(),
    };
    if (snapshot.occurrence_id.empty() || snapshot.name.empty() ||
        snapshot.source_document_id.empty()) {
        throw std::runtime_error("Nested Assembly snapshot identity is invalid");
    }
    for (const auto& child : source.at("children")) {
        snapshot.children.push_back(load_snapshot(child));
    }
    if (snapshot.source_kind == ComponentSourceKind::Part &&
        !snapshot.children.empty()) {
        throw std::runtime_error("Part snapshot must not contain child occurrences");
    }
    return snapshot;
}

void validate_snapshot_list(const std::vector<OccurrenceSnapshot>& snapshots) {
    std::unordered_set<std::string> ids;
    for (const auto& snapshot : snapshots) {
        if (snapshot.occurrence_id.empty() || snapshot.name.empty() ||
            snapshot.source_document_id.empty() ||
            !ids.insert(snapshot.occurrence_id).second) {
            throw std::runtime_error("Nested Assembly snapshot identity is invalid");
        }
        if (snapshot.source_kind == ComponentSourceKind::Part &&
            !snapshot.children.empty()) {
            throw std::runtime_error("Part snapshot must not contain child occurrences");
        }
        for (const double value : {
                snapshot.placement.x, snapshot.placement.y, snapshot.placement.z,
                snapshot.placement.rotation_x, snapshot.placement.rotation_y,
                snapshot.placement.rotation_z}) {
            if (!std::isfinite(value)) {
                throw std::runtime_error(
                    "Nested Assembly snapshot placement must be finite");
            }
        }
        validate_snapshot_list(snapshot.children);
    }
}

}  // namespace

InstancePath InstancePath::child(const std::string& occurrence_id) const {
    if (occurrence_id.empty()) {
        throw std::invalid_argument("Occurrence ID must not be empty");
    }
    auto result = *this;
    result.occurrence_ids.push_back(occurrence_id);
    return result;
}

std::optional<InstancePath> InstancePath::parent() const {
    if (occurrence_ids.size() < 2) return std::nullopt;
    InstancePath result = *this;
    result.occurrence_ids.pop_back();
    return result;
}

std::string InstancePath::encoded() const {
    std::string result;
    for (const auto& id : occurrence_ids) {
        result += std::to_string(id.size()) + ":" + id;
    }
    return result;
}

InstancePath InstancePath::decode(const std::string& encoded) {
    InstancePath result;
    std::size_t cursor = 0;
    while (cursor < encoded.size()) {
        const auto separator = encoded.find(':', cursor);
        if (separator == std::string::npos || separator == cursor) {
            throw std::invalid_argument("Instance path length is invalid");
        }
        std::size_t parsed = 0;
        unsigned long long length = 0;
        try {
            length = std::stoull(encoded.substr(cursor, separator - cursor), &parsed);
        } catch (const std::exception&) {
            throw std::invalid_argument("Instance path length is invalid");
        }
        if (parsed != separator - cursor || length == 0 ||
            length > encoded.size() - separator - 1) {
            throw std::invalid_argument("Instance path segment is invalid");
        }
        cursor = separator + 1;
        result.occurrence_ids.push_back(encoded.substr(cursor, length));
        cursor += static_cast<std::size_t>(length);
    }
    return result;
}

AssemblyDocument AssemblyDocument::create_default() {
    AssemblyDocument document;
    document.document_id = make_id();
    return document;
}

PartOccurrence AssemblyDocument::create_part_occurrence(
    std::string name,
    std::string source_document_id,
    std::filesystem::path source_path,
    zima::kernel::BodyResult calculated_source) {
    if (name.empty() || source_document_id.empty()) {
        throw std::invalid_argument("Part occurrence name and source ID are required");
    }
    return {
        make_id(), std::move(name), std::move(source_document_id),
        std::move(source_path), ComponentSourceKind::Part, {}, false, false, true,
        std::move(calculated_source), {},
    };
}

PartOccurrence AssemblyDocument::create_assembly_occurrence(
    std::string name,
    std::string source_document_id,
    std::filesystem::path source_path,
    const AssemblyDocument& calculated_document) {
    auto occurrence = create_part_occurrence(
        std::move(name), std::move(source_document_id), std::move(source_path), {});
    occurrence.source_kind = ComponentSourceKind::Assembly;
    occurrence.calculated_source.mesh = calculated_document.build_scene();
    occurrence.nested_snapshot = calculated_document.occurrence_snapshot();
    return occurrence;
}

std::vector<OccurrenceSnapshot> AssemblyDocument::occurrence_snapshot() const {
    const auto effectively_suppressed = effectively_suppressed_occurrences();
    std::vector<OccurrenceSnapshot> result;
    result.reserve(components.size());
    for (const auto& component : components) {
        result.push_back({
            component.occurrence_id, component.name, component.source_document_id,
            component.source_kind, component.suppressed,
            !component.suppressed &&
                effectively_suppressed.contains(component.occurrence_id),
            component.visible, component.grounded, component.placement,
            component.nested_snapshot});
    }
    return result;
}

const PartOccurrence* AssemblyDocument::find_occurrence(
    const std::string& occurrence_id) const {
    const auto found = std::find_if(components.begin(), components.end(),
        [&](const PartOccurrence& occurrence) {
            return occurrence.occurrence_id == occurrence_id;
        });
    return found == components.end() ? nullptr : &*found;
}

PartOccurrence* AssemblyDocument::find_occurrence(const std::string& occurrence_id) {
    return const_cast<PartOccurrence*>(std::as_const(*this).find_occurrence(occurrence_id));
}

AssemblyCut* AssemblyDocument::find_cut(const std::string& container_id) {
    const auto found = std::find_if(cuts.begin(), cuts.end(), [&](const auto& cut) {
        return cut.definition.id == container_id;
    });
    return found == cuts.end() ? nullptr : &*found;
}

const AssemblyCut* AssemblyDocument::find_cut(
    const std::string& container_id) const {
    const auto found = std::find_if(cuts.begin(), cuts.end(), [&](const auto& cut) {
        return cut.definition.id == container_id;
    });
    return found == cuts.end() ? nullptr : &*found;
}

zima::document::ConstructionObject AssemblyDocument::create_construction(
    zima::document::ConstructionKind kind) {
    return zima::document::PartDocument::create_construction(kind);
}

zima::document::ConstructionObject* AssemblyDocument::find_construction(
    const std::string& id) {
    for (auto& object : constructions) {
        if (object.id == id) return &object;
        const auto child = std::find_if(object.curve_points.begin(),
            object.curve_points.end(),
            [&](const auto& point) { return point.id == id; });
        if (child != object.curve_points.end()) return &*child;
    }
    return nullptr;
}

const zima::document::ConstructionObject* AssemblyDocument::find_construction(
    const std::string& id) const {
    for (const auto& object : constructions) {
        if (object.id == id) return &object;
        const auto child = std::find_if(object.curve_points.begin(),
            object.curve_points.end(),
            [&](const auto& point) { return point.id == id; });
        if (child != object.curve_points.end()) return &*child;
    }
    return nullptr;
}

zima::kernel::ViewerMesh AssemblyDocument::construction_viewer_mesh(
    const std::string& editing_object_id) const {
    auto carrier = zima::document::PartDocument::create_default();
    carrier.document_id = document_id;
    carrier.name = name;
    carrier.constructions = constructions;
    return carrier.construction_viewer_mesh(editing_object_id);
}

zima::kernel::ViewerMesh AssemblyDocument::origin_viewer_mesh() const {
    auto carrier = zima::document::PartDocument::create_default();
    carrier.document_id = document_id;
    return carrier.origin_viewer_mesh();
}

zima::kernel::ViewerMesh AssemblyDocument::origin_viewer_mesh(
    double reference_scene_size) const {
    auto carrier = zima::document::PartDocument::create_default();
    carrier.document_id = document_id;
    return carrier.origin_viewer_mesh(reference_scene_size);
}

void AssemblyDocument::resolve_constructions() {
    auto source_document = *this;
    source_document.constructions.clear();
    auto carrier = zima::document::PartDocument::create_default();
    carrier.constructions = constructions;
    carrier.resolve_constructions(
        source_document.build_scene().original_references);
    constructions = std::move(carrier.constructions);
}

ComponentDependency AssemblyDocument::create_dependency(
    std::string dependent_occurrence_id,
    std::string prerequisite_occurrence_id,
    ComponentDependencyKind kind) {
    if (dependent_occurrence_id.empty() || prerequisite_occurrence_id.empty()) {
        throw std::invalid_argument("Component dependency endpoints are required");
    }
    return {
        make_id(), std::move(dependent_occurrence_id),
        std::move(prerequisite_occurrence_id), kind};
}

void AssemblyDocument::add_dependency(ComponentDependency dependency) {
    if (dependency.dependency_id.empty() ||
        find_occurrence(dependency.dependent_occurrence_id) == nullptr ||
        find_occurrence(dependency.prerequisite_occurrence_id) == nullptr ||
        dependency.dependent_occurrence_id == dependency.prerequisite_occurrence_id) {
        throw std::invalid_argument("Component dependency identity is invalid");
    }
    if (std::any_of(dependencies.begin(), dependencies.end(), [&](const auto& existing) {
            return existing.dependency_id == dependency.dependency_id;
        })) {
        throw std::invalid_argument("Component dependency ID must be unique");
    }
    DependencyGraph graph;
    for (const auto& existing : dependencies) {
        graph.add_dependency(
            existing.dependent_occurrence_id,
            existing.prerequisite_occurrence_id);
    }
    graph.add_dependency(
        dependency.dependent_occurrence_id,
        dependency.prerequisite_occurrence_id);
    dependencies.push_back(std::move(dependency));
}

double AssemblyDocument::project_linear_drag_value(
    const zima::kernel::Vec3& axis_point,
    const zima::kernel::Vec3& axis_direction,
    const zima::kernel::Vec3& ray_origin,
    const zima::kernel::Vec3& ray_direction) {
    const double axis_length = length(axis_direction);
    const double ray_length = length(ray_direction);
    if (axis_length <= 1.0e-12 || ray_length <= 1.0e-12) {
        throw std::invalid_argument("Assembly drag direction is invalid");
    }
    const zima::kernel::Vec3 axis{axis_direction.x / axis_length,
        axis_direction.y / axis_length, axis_direction.z / axis_length};
    const zima::kernel::Vec3 ray{ray_direction.x / ray_length,
        ray_direction.y / ray_length, ray_direction.z / ray_length};
    const zima::kernel::Vec3 offset{axis_point.x - ray_origin.x,
        axis_point.y - ray_origin.y, axis_point.z - ray_origin.z};
    const double alignment = dot(axis, ray);
    const double axis_offset = dot(axis, offset);
    const double ray_offset = dot(ray, offset);
    const double denominator = 1.0 - alignment * alignment;
    return std::abs(denominator) > 1.0e-10
        ? (alignment * ray_offset - axis_offset) / denominator
        : -axis_offset;
}

double AssemblyDocument::project_angular_drag_value(
    const zima::kernel::Vec3& center,
    const zima::kernel::Vec3& reference_direction,
    const zima::kernel::Vec3& ray_origin,
    const zima::kernel::Vec3& ray_direction) {
    const double reference_length = length(reference_direction);
    const double ray_length = length(ray_direction);
    if (reference_length <= 1.0e-12 || ray_length <= 1.0e-12) {
        throw std::invalid_argument("Assembly angular drag direction is invalid");
    }
    const zima::kernel::Vec3 ray{ray_direction.x / ray_length,
        ray_direction.y / ray_length, ray_direction.z / ray_length};
    const zima::kernel::Vec3 center_offset{center.x - ray_origin.x,
        center.y - ray_origin.y, center.z - ray_origin.z};
    const double parameter = dot(center_offset, ray);
    const zima::kernel::Vec3 cursor{
        ray_origin.x + parameter * ray.x - center.x,
        ray_origin.y + parameter * ray.y - center.y,
        ray_origin.z + parameter * ray.z - center.z};
    const double cursor_length = length(cursor);
    if (cursor_length <= 1.0e-12) {
        throw std::invalid_argument("Assembly angular drag cursor is undefined");
    }
    const double cosine = std::clamp(
        dot(reference_direction, cursor) /
            (reference_length * cursor_length), -1.0, 1.0);
    return std::acos(cosine) * 180.0 / 3.14159265358979323846;
}

PlaneResolution AssemblyDocument::resolve_plane(
    const MateReference& reference) const {
    if (reference.kind != MateReferenceKind::Face) {
        return {MateStatus::UnsupportedGeometry, {}};
    }
    const auto scene = build_scene();
    const std::string path = reference.instance_path.encoded();
    constexpr double epsilon = 1.0e-10;
    constexpr double planar_tolerance = 1.0e-7;
    std::optional<ResolvedPlane> result;
    std::vector<zima::kernel::Vec3> points;
    const auto& face_vertices = scene.original_references.vertices;
    const auto& face_triangles = scene.original_references.triangles;
    const auto& face_references = scene.original_references.triangle_references;
    for (std::size_t triangle = 0;
         triangle < face_references.size(); ++triangle) {
        const auto& candidate = face_references[triangle];
        if (candidate.instance_path != path || candidate.owner_id != reference.owner_id ||
            candidate.semantic_key != reference.semantic_key) continue;
        const auto first = face_triangles[triangle * 3];
        const auto second = face_triangles[triangle * 3 + 1];
        const auto third = face_triangles[triangle * 3 + 2];
        const auto& a = face_vertices[first];
        const auto& b = face_vertices[second];
        const auto& c = face_vertices[third];
        points.insert(points.end(), {a, b, c});
        if (!result) {
            const zima::kernel::Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
            const zima::kernel::Vec3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
            zima::kernel::Vec3 normal{
                ab.y * ac.z - ab.z * ac.y,
                ab.z * ac.x - ab.x * ac.z,
                ab.x * ac.y - ab.y * ac.x};
            const double magnitude = std::sqrt(
                normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
            if (magnitude > epsilon) {
                normal = {normal.x / magnitude, normal.y / magnitude,
                          normal.z / magnitude};
                result = ResolvedPlane{a, normal};
            }
        }
    }
    if (!result) return {MateStatus::MissingReference, {}};
    for (const auto& point : points) {
        const double distance =
            (point.x - result->point.x) * result->normal.x +
            (point.y - result->point.y) * result->normal.y +
            (point.z - result->point.z) * result->normal.z;
        if (std::abs(distance) > planar_tolerance) {
            return {MateStatus::UnsupportedGeometry, {}};
        }
    }
    return {MateStatus::Valid, *result};
}

AxisResolution AssemblyDocument::resolve_axis(
    const MateReference& reference) const {
    if (reference.kind != MateReferenceKind::Axis) {
        return {MateStatus::UnsupportedGeometry, {}};
    }
    const auto scene = build_scene();
    const std::string path = reference.instance_path.encoded();
    const auto& axes = scene.original_references.axes;
    const auto found = std::find_if(axes.begin(), axes.end(),
        [&](const auto& axis) {
            return axis.reference.instance_path == path &&
                axis.reference.owner_id == reference.owner_id &&
                axis.reference.semantic_key == reference.semantic_key;
        });
    if (found == axes.end()) return {MateStatus::MissingReference, {}};
    return {MateStatus::Valid, {found->point, found->direction}};
}

PointResolution AssemblyDocument::resolve_point(
    const MateReference& reference) const {
    if (reference.kind != MateReferenceKind::Point) {
        return {MateStatus::UnsupportedGeometry, {}};
    }
    const auto scene = build_scene();
    const std::string path = reference.instance_path.encoded();
    const auto& points = scene.original_references.points;
    const auto found = std::find_if(points.begin(), points.end(),
        [&](const auto& point) {
            return point.reference.instance_path == path &&
                point.reference.owner_id == reference.owner_id &&
                point.reference.semantic_key == reference.semantic_key;
        });
    if (found == points.end()) return {MateStatus::MissingReference, {}};
    return {MateStatus::Valid, found->position};
}


// Solves PartOccurrence::placement_references (the embedded, Python-style
// per-component reference rows) using the per-kind resolution functions
// (resolve_plane/resolve_axis/resolve_point) and a rotate-then-translate
// solve strategy. `flip` mirrors ConstructionReference::flip: it inverts the
// resolved direction/normal of an orientation-driving reference as a
// post-solve step, and is a no-op for a PointCoincident row.
void AssemblyDocument::calculate_placement_references() {
    constexpr double parallel_tolerance = 1.0e-7;
    for (auto& component : components) {
        if (component.grounded || component.placement_references.empty()) continue;
        for (const auto& row : component.placement_references) {
            if (row.mate_type == MateKind::PlaneCoincident) {
                if (row.component_reference.kind != MateReferenceKind::Face ||
                    row.target_reference.kind != MateReferenceKind::Face) {
                    continue;
                }
                const auto moving = resolve_plane(row.component_reference);
                const auto target = resolve_plane(row.target_reference);
                if (moving.status != MateStatus::Valid ||
                    target.status != MateStatus::Valid) {
                    continue;
                }
                const auto& moving_normal = moving.plane.normal;
                const auto& target_normal = target.plane.normal;
                const double alignment = dot(moving_normal, target_normal);
                const zima::kernel::Vec3 desired_normal = row.flip
                    ? zima::kernel::Vec3{-target_normal.x, -target_normal.y,
                                         -target_normal.z}
                    : alignment < 0.0
                        ? zima::kernel::Vec3{-target_normal.x, -target_normal.y,
                                             -target_normal.z}
                        : target_normal;
                const bool orientation_satisfied = row.flip
                    ? std::abs(alignment + 1.0) <= parallel_tolerance
                    : std::abs(std::abs(alignment) - 1.0) <= parallel_tolerance;
                if (!orientation_satisfied) {
                    rotate_occurrence_about(component,
                        shortest_rotation(moving_normal, desired_normal),
                        moving.plane.point);
                }
                const auto aligned = resolve_plane(row.component_reference);
                if (aligned.status != MateStatus::Valid) continue;
                const auto& aligned_point = aligned.plane.point;
                const auto& target_point = target.plane.point;
                const double current_offset =
                    (aligned_point.x - target_point.x) * target_normal.x +
                    (aligned_point.y - target_point.y) * target_normal.y +
                    (aligned_point.z - target_point.z) * target_normal.z;
                const double correction = row.offset - current_offset;
                component.placement.x += target_normal.x * correction;
                component.placement.y += target_normal.y * correction;
                component.placement.z += target_normal.z * correction;
            } else if (row.mate_type == MateKind::AxisCoincident) {
                if (row.component_reference.kind != MateReferenceKind::Axis ||
                    row.target_reference.kind != MateReferenceKind::Axis) {
                    continue;
                }
                const auto moving = resolve_axis(row.component_reference);
                const auto target = resolve_axis(row.target_reference);
                if (moving.status != MateStatus::Valid ||
                    target.status != MateStatus::Valid) {
                    continue;
                }
                const double alignment =
                    dot(moving.axis.direction, target.axis.direction);
                const zima::kernel::Vec3 desired_direction = row.flip
                    ? zima::kernel::Vec3{-target.axis.direction.x,
                                         -target.axis.direction.y,
                                         -target.axis.direction.z}
                    : alignment < 0.0
                        ? zima::kernel::Vec3{-target.axis.direction.x,
                                             -target.axis.direction.y,
                                             -target.axis.direction.z}
                        : target.axis.direction;
                const bool orientation_satisfied = row.flip
                    ? std::abs(alignment + 1.0) <= parallel_tolerance
                    : std::abs(std::abs(alignment) - 1.0) <= parallel_tolerance;
                if (!orientation_satisfied) {
                    rotate_occurrence_about(component,
                        shortest_rotation(moving.axis.direction, desired_direction),
                        moving.axis.point);
                }
                const auto aligned = resolve_axis(row.component_reference);
                if (aligned.status != MateStatus::Valid) continue;
                const zima::kernel::Vec3 delta{
                    target.axis.point.x - aligned.axis.point.x,
                    target.axis.point.y - aligned.axis.point.y,
                    target.axis.point.z - aligned.axis.point.z};
                const double axial = dot(delta, target.axis.direction);
                const zima::kernel::Vec3 correction{
                    delta.x - axial * target.axis.direction.x,
                    delta.y - axial * target.axis.direction.y,
                    delta.z - axial * target.axis.direction.z};
                component.placement.x += correction.x;
                component.placement.y += correction.y;
                component.placement.z += correction.z;
            } else if (row.mate_type == MateKind::PointCoincident) {
                if (row.component_reference.kind != MateReferenceKind::Point ||
                    row.target_reference.kind != MateReferenceKind::Point) {
                    continue;
                }
                const auto moving = resolve_point(row.component_reference);
                const auto target = resolve_point(row.target_reference);
                if (moving.status != MateStatus::Valid ||
                    target.status != MateStatus::Valid) {
                    continue;
                }
                component.placement.x += target.point.x - moving.point.x;
                component.placement.y += target.point.y - moving.point.y;
                component.placement.z += target.point.z - moving.point.z;
            } else if (row.mate_type == MateKind::AxisAngle) {
                if (row.component_reference.kind != MateReferenceKind::Axis ||
                    row.target_reference.kind != MateReferenceKind::Axis) {
                    continue;
                }
                const auto moving = resolve_axis(row.component_reference);
                const auto target = resolve_axis(row.target_reference);
                if (moving.status != MateStatus::Valid ||
                    target.status != MateStatus::Valid) {
                    continue;
                }
                constexpr double radians = 3.14159265358979323846 / 180.0;
                const double requested = row.offset * radians;
                const auto direction = nearest_direction_at_angle(
                    moving.axis.direction, target.axis.direction, requested);
                rotate_occurrence_about(component,
                    shortest_rotation(moving.axis.direction, direction),
                    moving.axis.point);
            } else if (row.mate_type == MateKind::PlaneAngle) {
                if (row.component_reference.kind != MateReferenceKind::Face ||
                    row.target_reference.kind != MateReferenceKind::Face) {
                    continue;
                }
                const auto moving = resolve_plane(row.component_reference);
                const auto target = resolve_plane(row.target_reference);
                if (moving.status != MateStatus::Valid ||
                    target.status != MateStatus::Valid) {
                    continue;
                }
                constexpr double radians = 3.14159265358979323846 / 180.0;
                const double requested = row.offset * radians;
                const auto direction = nearest_direction_at_angle(
                    moving.plane.normal, target.plane.normal, requested);
                rotate_occurrence_about(component,
                    shortest_rotation(moving.plane.normal, direction),
                    moving.plane.point);
            }
        }
    }
}

int AssemblyDocument::remaining_degrees_of_freedom(
    const std::string& occurrence_id) const {
    const auto* occurrence = find_occurrence(occurrence_id);
    if (occurrence == nullptr) throw std::invalid_argument("Assembly occurrence does not exist");
    if (occurrence->grounded) return 0;
    const auto residuals = [&](const AssemblyDocument& document) {
        std::vector<double> values;
        const auto* live_occurrence = document.find_occurrence(occurrence_id);
        if (live_occurrence == nullptr) return values;
        for (const auto& row : live_occurrence->placement_references) {
            if (row.mate_type == MateKind::PointCoincident) {
                const auto dependent = document.resolve_point(row.component_reference);
                const auto prerequisite = document.resolve_point(row.target_reference);
                if (dependent.status != MateStatus::Valid ||
                    prerequisite.status != MateStatus::Valid) continue;
                values.insert(values.end(), {
                    dependent.point.x - prerequisite.point.x,
                    dependent.point.y - prerequisite.point.y,
                    dependent.point.z - prerequisite.point.z});
            } else if (row.mate_type == MateKind::AxisCoincident) {
                const auto dependent = document.resolve_axis(row.component_reference);
                const auto prerequisite = document.resolve_axis(row.target_reference);
                if (dependent.status != MateStatus::Valid ||
                    prerequisite.status != MateStatus::Valid) continue;
                const auto orientation = cross(
                    dependent.axis.direction, prerequisite.axis.direction);
                const zima::kernel::Vec3 delta{
                    dependent.axis.point.x - prerequisite.axis.point.x,
                    dependent.axis.point.y - prerequisite.axis.point.y,
                    dependent.axis.point.z - prerequisite.axis.point.z};
                const double axial = dot(delta, prerequisite.axis.direction);
                values.insert(values.end(), {
                    orientation.x, orientation.y, orientation.z,
                    delta.x - axial * prerequisite.axis.direction.x,
                    delta.y - axial * prerequisite.axis.direction.y,
                    delta.z - axial * prerequisite.axis.direction.z});
            } else if (row.mate_type == MateKind::PlaneCoincident) {
                const auto dependent = document.resolve_plane(row.component_reference);
                const auto prerequisite = document.resolve_plane(row.target_reference);
                if (dependent.status != MateStatus::Valid ||
                    prerequisite.status != MateStatus::Valid) continue;
                const auto orientation = cross(
                    dependent.plane.normal, prerequisite.plane.normal);
                const zima::kernel::Vec3 delta{
                    dependent.plane.point.x - prerequisite.plane.point.x,
                    dependent.plane.point.y - prerequisite.plane.point.y,
                    dependent.plane.point.z - prerequisite.plane.point.z};
                values.insert(values.end(), {orientation.x, orientation.y,
                    orientation.z,
                    dot(delta, prerequisite.plane.normal) - row.offset});
            } else if (row.mate_type == MateKind::AxisAngle) {
                const auto dependent = document.resolve_axis(row.component_reference);
                const auto prerequisite = document.resolve_axis(row.target_reference);
                if (dependent.status != MateStatus::Valid ||
                    prerequisite.status != MateStatus::Valid) continue;
                constexpr double radians = std::numbers::pi / 180.0;
                const double requested = (row.flip
                    ? 180.0 - row.offset : row.offset) * radians;
                values.push_back(dot(dependent.axis.direction,
                    prerequisite.axis.direction) - std::cos(requested));
            } else {
                const auto dependent = document.resolve_plane(row.component_reference);
                const auto prerequisite = document.resolve_plane(row.target_reference);
                if (dependent.status != MateStatus::Valid ||
                    prerequisite.status != MateStatus::Valid) continue;
                constexpr double radians = std::numbers::pi / 180.0;
                const double requested = (row.flip
                    ? 180.0 - row.offset : row.offset) * radians;
                values.push_back(dot(dependent.plane.normal,
                    prerequisite.plane.normal) - std::cos(requested));
            }
        }
        return values;
    };
    const auto baseline = residuals(*this);
    if (baseline.empty()) return 6;
    std::vector<std::vector<double>> jacobian(
        baseline.size(), std::vector<double>(6));
    for (int coordinate = 0; coordinate < 6; ++coordinate) {
        auto perturbed = *this;
        auto* moved = perturbed.find_occurrence(occurrence_id);
        constexpr double translation_step = 1.0e-5;
        constexpr double rotation_step_degrees = 1.0e-4;
        const double step = coordinate < 3
            ? translation_step : rotation_step_degrees;
        if (coordinate == 0) moved->placement.x += step;
        else if (coordinate == 1) moved->placement.y += step;
        else if (coordinate == 2) moved->placement.z += step;
        else if (coordinate == 3) moved->placement.rotation_x += step;
        else if (coordinate == 4) moved->placement.rotation_y += step;
        else moved->placement.rotation_z += step;
        const auto changed = residuals(perturbed);
        if (changed.size() != baseline.size()) continue;
        const double denominator = coordinate < 3
            ? step : step * std::numbers::pi / 180.0;
        for (std::size_t row = 0; row < baseline.size(); ++row) {
            jacobian[row][coordinate] =
                (changed[row] - baseline[row]) / denominator;
        }
    }
    int rank{};
    constexpr double rank_tolerance = 1.0e-6;
    for (int column = 0; column < 6 && rank < static_cast<int>(jacobian.size());
         ++column) {
        int pivot = rank;
        for (int row = rank + 1; row < static_cast<int>(jacobian.size()); ++row) {
            if (std::abs(jacobian[row][column]) >
                std::abs(jacobian[pivot][column])) pivot = row;
        }
        if (std::abs(jacobian[pivot][column]) <= rank_tolerance) continue;
        std::swap(jacobian[pivot], jacobian[rank]);
        const double divisor = jacobian[rank][column];
        for (int value = column; value < 6; ++value) {
            jacobian[rank][value] /= divisor;
        }
        for (int row = 0; row < static_cast<int>(jacobian.size()); ++row) {
            if (row == rank) continue;
            const double factor = jacobian[row][column];
            for (int value = column; value < 6; ++value) {
                jacobian[row][value] -= factor * jacobian[rank][value];
            }
        }
        ++rank;
    }
    return 6 - rank;
}

std::unordered_set<std::string>
AssemblyDocument::effectively_suppressed_occurrences() const {
    std::unordered_set<std::string> result;
    for (const auto& component : components) {
        if (component.suppressed) result.insert(component.occurrence_id);
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& dependency : dependencies) {
            if (result.contains(dependency.prerequisite_occurrence_id) &&
                result.insert(dependency.dependent_occurrence_id).second) {
                changed = true;
            }
        }
    }
    return result;
}

zima::kernel::ViewerMesh AssemblyDocument::build_scene() const {
    // Build the component/construction geometry first so the Origin
    // axis/plane display size can scale with the assembly's actual extent,
    // matching Python's reference_scene_size = _scene_diagonal(layers)
    // (computed over the component bodies before the origin overlay).
    zima::kernel::ViewerMesh scene;
    std::unordered_set<std::string> occurrence_ids;
    std::unordered_set<std::string> dependency_ids;
    std::unordered_set<std::string> cut_ids;
    for (const auto& cut : cuts) {
        const auto& feature = cut.definition;
        const bool extrusion = feature.feature_kind ==
            zima::document::FeatureKind::Extrusion;
        const bool revolution = feature.feature_kind ==
            zima::document::FeatureKind::Revolution;
        const std::string& sketch_id = extrusion
            ? feature.extrusion.sketch_id : feature.revolution.sketch_id;
        if (feature.id.empty() || !cut_ids.insert(feature.id).second ||
            (!extrusion && !revolution) ||
            feature.combine_mode != zima::document::CombineMode::Subtract ||
            std::none_of(sketches.begin(), sketches.end(), [&](const auto& sketch) {
                return sketch.id == sketch_id;
            })) {
            throw std::runtime_error("Assembly cut definition is invalid");
        }
        std::unordered_set<std::string> target_ids;
        for (const auto& target_id : cut.target_occurrence_ids) {
            const auto* target = find_occurrence(target_id);
            if (!target_ids.insert(target_id).second || target == nullptr) {
                throw std::runtime_error(
                    "Assembly cut target must be a unique immediate component occurrence");
            }
        }
    }
    DependencyGraph dependency_graph;
    for (const auto& dependency : dependencies) {
        if (dependency.dependency_id.empty() ||
            !dependency_ids.insert(dependency.dependency_id).second ||
            find_occurrence(dependency.dependent_occurrence_id) == nullptr ||
            find_occurrence(dependency.prerequisite_occurrence_id) == nullptr) {
            throw std::runtime_error("Assembly component dependency is invalid");
        }
        dependency_graph.add_dependency(
            dependency.dependent_occurrence_id,
            dependency.prerequisite_occurrence_id);
    }
    const auto effectively_suppressed = effectively_suppressed_occurrences();
    for (const auto& component : components) {
        if (component.occurrence_id.empty() ||
            !occurrence_ids.insert(component.occurrence_id).second) {
            throw std::runtime_error("Assembly occurrence IDs must be non-empty and unique");
        }
        if (effectively_suppressed.contains(component.occurrence_id) ||
            !component.visible) continue;
        const std::string path = InstancePath{}.child(component.occurrence_id).encoded();
        const std::uint32_t vertex_offset =
            static_cast<std::uint32_t>(scene.vertices.size());
        const auto& source_mesh = component.calculated_source.mesh;
        for (const auto& vertex : source_mesh.vertices) {
            scene.vertices.push_back(transform_point(vertex, component.placement));
        }
        for (const auto index : source_mesh.triangles) {
            if (index >= source_mesh.vertices.size()) {
                throw std::runtime_error("Component viewer triangle index is invalid");
            }
            scene.triangles.push_back(vertex_offset + index);
        }
        for (auto reference : source_mesh.triangle_references) {
            assign_instance(reference, path);
            scene.triangle_references.push_back(std::move(reference));
        }
        for (auto edge : source_mesh.edges) {
            assign_instance(edge.reference, path);
            for (auto& point : edge.points) {
                point = transform_point(point, component.placement);
            }
            for (auto& side : edge.edge_treatment_side_directions) {
                for (auto& direction : side) {
                    direction = transform_direction(direction, component.placement);
                }
            }
            scene.edges.push_back(std::move(edge));
        }
        for (auto point : source_mesh.points) {
            assign_instance(point.reference, path);
            point.position = transform_point(point.position, component.placement);
            scene.points.push_back(std::move(point));
        }
        for (auto axis : source_mesh.axes) {
            assign_instance(axis.reference, path);
            axis.point = transform_point(axis.point, component.placement);
            axis.direction = transform_direction(axis.direction, component.placement);
            scene.axes.push_back(std::move(axis));
        }
        for (auto dimension : source_mesh.dimensions) {
            assign_instance(dimension.reference, path);
            dimension.witness_first = transform_point(dimension.witness_first, component.placement);
            dimension.witness_second = transform_point(dimension.witness_second, component.placement);
            dimension.line_first = transform_point(dimension.line_first, component.placement);
            dimension.line_second = transform_point(dimension.line_second, component.placement);
            scene.dimensions.push_back(std::move(dimension));
        }
        auto& target_references = scene.original_references;
        const auto& source_references = source_mesh.original_references;
        const auto reference_offset =
            static_cast<std::uint32_t>(target_references.vertices.size());
        for (const auto& vertex : source_references.vertices) {
            target_references.vertices.push_back(
                transform_point(vertex, component.placement));
        }
        for (const auto index : source_references.triangles) {
            if (index >= source_references.vertices.size()) {
                throw std::runtime_error(
                    "Component reference triangle index is invalid");
            }
            target_references.triangles.push_back(reference_offset + index);
        }
        for (auto reference : source_references.triangle_references) {
            assign_instance(reference, path);
            target_references.triangle_references.push_back(std::move(reference));
        }
        for (auto edge : source_references.edges) {
            assign_instance(edge.reference, path);
            for (auto& point : edge.points) {
                point = transform_point(point, component.placement);
            }
            for (auto& side : edge.edge_treatment_side_directions) {
                for (auto& direction : side) {
                    direction = transform_direction(direction, component.placement);
                }
            }
            target_references.edges.push_back(std::move(edge));
        }
        for (auto point : source_references.points) {
            assign_instance(point.reference, path);
            point.position = transform_point(point.position, component.placement);
            target_references.points.push_back(std::move(point));
        }
        for (auto axis : source_references.axes) {
            assign_instance(axis.reference, path);
            axis.point = transform_point(axis.point, component.placement);
            axis.direction = transform_direction(axis.direction, component.placement);
            target_references.axes.push_back(std::move(axis));
        }
    }
    const auto find_axis = [&](const MateReference& reference)
        -> std::optional<ResolvedAxis> {
        const auto path = reference.instance_path.encoded();
        const auto found = std::ranges::find_if(scene.original_references.axes,
            [&](const auto& axis) {
                return axis.reference.instance_path == path &&
                    axis.reference.owner_id == reference.owner_id &&
                    axis.reference.semantic_key == reference.semantic_key;
            });
        if (found == scene.original_references.axes.end()) return std::nullopt;
        return ResolvedAxis{found->point, found->direction};
    };
    const auto find_plane = [&](const MateReference& reference)
        -> std::optional<ResolvedPlane> {
        const auto path = reference.instance_path.encoded();
        const auto& geometry = scene.original_references;
        for (std::size_t triangle = 0;
             triangle < geometry.triangle_references.size(); ++triangle) {
            const auto& candidate = geometry.triangle_references[triangle];
            if (candidate.instance_path != path ||
                candidate.owner_id != reference.owner_id ||
                candidate.semantic_key != reference.semantic_key) continue;
            const auto& a = geometry.vertices[geometry.triangles[triangle * 3]];
            const auto& b = geometry.vertices[geometry.triangles[triangle * 3 + 1]];
            const auto& c = geometry.vertices[geometry.triangles[triangle * 3 + 2]];
            auto normal = cross({b.x - a.x, b.y - a.y, b.z - a.z},
                                {c.x - a.x, c.y - a.y, c.z - a.z});
            const double magnitude = length(normal);
            if (magnitude <= 1.0e-12) continue;
            normal = {normal.x / magnitude, normal.y / magnitude,
                      normal.z / magnitude};
            return ResolvedPlane{a, normal};
        }
        return std::nullopt;
    };
    // Embedded placement-reference rows (PartOccurrence::placement_references,
    // the Python-style per-component reference table) get a 3D dimension
    // overlay -- witness/line/value construction using find_axis/find_plane
    // -- giving interactive-viewer parity (double-click to open Properties,
    // drag to adjust). The dimension's semantic key encodes the owning
    // occurrence + row index so a double click / drag site can look the row
    // back up without a separate top-level id:
    // "placement-reference:<occurrence_id>:<row_index>".
    for (const auto& component : components) {
        for (std::size_t index = 0; index < component.placement_references.size();
             ++index) {
            const auto& row = component.placement_references[index];
            if (std::abs(row.offset) <=
                visible_placement_dimension_epsilon) continue;
            zima::kernel::ViewerDimension dimension;
            dimension.reference = {document_id,
                "placement-reference:" + component.occurrence_id + ":" +
                    std::to_string(index), {}};
            if (row.mate_type == MateKind::PlaneCoincident) {
                const auto moving = find_plane(row.component_reference);
                const auto target = find_plane(row.target_reference);
                if (!moving || !target) continue;
                const auto& normal = target->normal;
                const zima::kernel::Vec3 basis = std::abs(normal.x) < 0.9
                    ? zima::kernel::Vec3{1.0, 0.0, 0.0}
                    : zima::kernel::Vec3{0.0, 1.0, 0.0};
                auto side = cross(normal, basis);
                const double side_length = length(side);
                side = {side.x * 10.0 / side_length, side.y * 10.0 / side_length,
                        side.z * 10.0 / side_length};
                dimension.witness_first = target->point;
                dimension.witness_second = moving->point;
                dimension.line_first = {target->point.x + side.x,
                                        target->point.y + side.y,
                                        target->point.z + side.z};
                dimension.line_second = {moving->point.x + side.x,
                                         moving->point.y + side.y,
                                         moving->point.z + side.z};
                dimension.value = row.offset;
                dimension.label_prefix.clear();
                // The measured direction and the deliberately chosen side
                // offset define one stable dimension plane.  Do not leave
                // the default global-Z plane on an arbitrarily oriented mate.
                dimension.plane_normal = cross(normal, side);
            } else if (row.mate_type == MateKind::AxisAngle) {
                const auto moving = find_axis(row.component_reference);
                const auto target = find_axis(row.target_reference);
                if (!moving || !target) continue;
                dimension.witness_first = target->point;
                dimension.witness_second = target->point;
                dimension.line_first = {
                    target->point.x + target->direction.x * 30.0,
                    target->point.y + target->direction.y * 30.0,
                    target->point.z + target->direction.z * 30.0};
                dimension.line_second = {
                    target->point.x + moving->direction.x * 30.0,
                    target->point.y + moving->direction.y * 30.0,
                    target->point.z + moving->direction.z * 30.0};
                dimension.value = row.offset;
                dimension.label_prefix.clear();
                dimension.unit_suffix = " °";
                dimension.kind = zima::kernel::ViewerDimensionKind::Angular;
                dimension.sweep_degrees = row.offset;
                dimension.plane_normal = cross(
                    target->direction, moving->direction);
            } else if (row.mate_type == MateKind::PlaneAngle) {
                const auto moving = find_plane(row.component_reference);
                const auto target = find_plane(row.target_reference);
                if (!moving || !target) continue;
                dimension.witness_first = target->point;
                dimension.witness_second = target->point;
                dimension.line_first = {
                    target->point.x + target->normal.x * 30.0,
                    target->point.y + target->normal.y * 30.0,
                    target->point.z + target->normal.z * 30.0};
                dimension.line_second = {
                    target->point.x + moving->normal.x * 30.0,
                    target->point.y + moving->normal.y * 30.0,
                    target->point.z + moving->normal.z * 30.0};
                dimension.value = row.offset;
                dimension.label_prefix.clear();
                dimension.unit_suffix = " °";
                dimension.kind = zima::kernel::ViewerDimensionKind::Angular;
                dimension.sweep_degrees = row.offset;
                dimension.plane_normal = cross(target->normal, moving->normal);
            } else {
                continue;
            }
            scene.dimensions.push_back(std::move(dimension));
        }
    }
    for (const auto& cut : cuts) {
        const auto& feature = cut.definition;
        if (feature.feature_kind != zima::document::FeatureKind::Extrusion ||
            (feature.extrusion.extent !=
                 zima::document::ExtrusionExtent::UpToPlane &&
             feature.extrusion.extent !=
                 zima::document::ExtrusionExtent::UpToSurface)) continue;
        const auto& target = feature.extrusion.target_face;
        const bool datum = target.instance_path.empty() &&
            std::any_of(constructions.begin(), constructions.end(),
                [&](const auto& construction) {
                    return construction.id == target.owner_id &&
                        construction.kind ==
                            zima::document::ConstructionKind::Plane;
                });
        const bool persisted_face = std::any_of(
            scene.original_references.triangle_references.begin(),
            scene.original_references.triangle_references.end(),
            [&](const auto& reference) {
                return reference == target;
            });
        if (!datum && !persisted_face) {
            throw std::runtime_error(
                "Assembly cut target face reference is missing");
        }
    }
    if (scene.triangle_references.size() != scene.triangles.size() / 3) {
        throw std::runtime_error("Assembly triangle references are not aligned");
    }
    if (scene.original_references.triangle_references.size() !=
        scene.original_references.triangles.size() / 3) {
        throw std::runtime_error("Assembly reference triangle data are not aligned");
    }
    zima::kernel::ViewerMesh result = origin_viewer_mesh(
        zima::document::viewer_mesh_bounds_diagonal(scene));
    append_viewer_mesh(result, construction_viewer_mesh());
    append_viewer_mesh(result, scene);
    return result;
}

zima::kernel::ViewerMesh AssemblyDocument::build_scene_with_part_override(
    const std::string& occurrence_id,
    zima::kernel::BodyResult calculated_source) const {
    auto transient = *this;
    const auto found = std::find_if(
        transient.components.begin(), transient.components.end(),
        [&](const PartOccurrence& occurrence) {
            return occurrence.occurrence_id == occurrence_id;
        });
    if (found == transient.components.end()) {
        throw std::invalid_argument("Rollback occurrence does not exist in Assembly");
    }
    found->calculated_source = std::move(calculated_source);
    return transient.build_scene();
}

AssemblyDocument AssemblyDocument::load(const std::filesystem::path& path) {
    const auto ini = read_ini(path);
    if (ini_value(ini, "Document", "format_version") != "11" ||
        ini_value(ini, "Document", "type") != "assembly") {
        throw std::runtime_error("Unsupported ZIMA-CAD Assembly document format");
    }
    const auto root_section = "Container." +
        ini_value(ini, "Document", "document_id");
    const auto assembly_json = ini_value(ini, root_section, "param.cpp_assembly");
    if (assembly_json.empty()) {
        throw std::runtime_error("Assembly INI is missing its root Container data");
    }
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(assembly_json);
    } catch (const nlohmann::json::exception&) {
        throw std::runtime_error("Assembly INI contains invalid Container data");
    }
    if (root.value("format", "") != "zima-cad-cpp" ||
        root.value("type", "") != "assembly") {
        throw std::runtime_error("Invalid Assembly Container data");
    }
    AssemblyDocument document;
    document.document_id = root.at("document_id").get<std::string>();
    document.name = root.at("name").get<std::string>();
    document.user_parameters =
        root.at("user_parameters").get<std::map<std::string, std::string>>();
    document.user_parameter_order =
        root.at("user_parameter_order").get<std::vector<std::string>>();
    document.user_parameter_labels = root.at("user_parameter_labels").get<
        decltype(document.user_parameter_labels)>();
    document.user_parameter_values = root.at("user_parameter_values").get<
        decltype(document.user_parameter_values)>();
    for (const auto& relation : root.at("relations")) {
        document.relations.push_back({relation.at("target").get<std::string>(),
            relation.at("expression").get<std::string>()});
    }
    document.document_units = root.at("document_units").get<decltype(document.document_units)>();
    document.document_precision = root.at("document_precision").get<decltype(document.document_precision)>();
    document.physical_parameters = root.at("physical_parameters").get<decltype(document.physical_parameters)>();
    document.physical_parameter_units = root.at("physical_parameter_units").get<decltype(document.physical_parameter_units)>();
    document.material_parameter_descriptions = root.at("material_parameter_descriptions").get<decltype(document.material_parameter_descriptions)>();
    document.family_table = root.at("family_table").get<std::string>();
    document.named_views = root.value("named_views", std::string("[]"));
    for (const auto& value : root.at("sketches")) {
        document.sketches.push_back(zima::sketcher::Sketch::from_serialized(
            value.get<std::string>()));
    }
    for (const auto& value : root.at("cuts")) {
        AssemblyCut cut;
        auto& feature = cut.definition;
        feature.id = value.at("id").get<std::string>();
        feature.feature_id = value.at("feature_id").get<std::string>();
        feature.feature_parent_id =
            value.at("feature_parent_id").get<std::string>();
        feature.container_origin =
            zima::document::create_container_origin(feature.id);
        if (feature.feature_parent_id != feature.id) {
            throw std::runtime_error("Assembly cut feature parent is invalid");
        }
        feature.name = value.at("name").get<std::string>();
        const auto kind = value.at("kind").get<std::string>();
        feature.feature_kind = kind == "extrusion"
            ? zima::document::FeatureKind::Extrusion
            : zima::document::FeatureKind::Revolution;
        feature.combine_mode = zima::document::CombineMode::Subtract;
        feature.suppressed = value.at("suppressed").get<bool>();
        feature.placement = {value.at("x").get<double>(), value.at("y").get<double>(),
            value.at("z").get<double>(), value.at("rx").get<double>(),
            value.at("ry").get<double>(), value.at("rz").get<double>()};
        const auto sketch_id = value.at("sketch_id").get<std::string>();
        if (feature.feature_kind == zima::document::FeatureKind::Extrusion) {
            feature.extrusion.sketch_id = sketch_id;
        } else {
            feature.revolution.sketch_id = sketch_id;
        }
        feature.extrusion.height = value.at("height").get<double>();
        feature.extrusion.direction = static_cast<zima::document::ExtrusionDirection>(
            value.at("direction").get<int>());
        feature.extrusion.extent = static_cast<zima::document::ExtrusionExtent>(
            value.at("extent").get<int>());
        feature.extrusion.target_face = {value.at("target_owner").get<std::string>(),
            value.at("target_key").get<std::string>(),
            value.at("target_path").get<std::string>()};
        const auto& origin = value.at("target_origin");
        const auto& normal = value.at("target_normal");
        feature.extrusion.target_plane_origin = {origin.at(0).get<double>(),
            origin.at(1).get<double>(), origin.at(2).get<double>()};
        feature.extrusion.target_plane_normal = {normal.at(0).get<double>(),
            normal.at(1).get<double>(), normal.at(2).get<double>()};
        for (const auto& point : value.at("target_triangles")) {
            feature.extrusion.target_surface_triangles.push_back({
                point.at(0).get<double>(), point.at(1).get<double>(),
                point.at(2).get<double>()});
        }
        feature.revolution.axis_segment_id =
            value.at("axis_segment_id").get<std::string>();
        feature.revolution.angle_degrees = value.at("angle").get<double>();
        cut.target_occurrence_ids = value.at("targets").get<std::vector<std::string>>();
        for (const auto& [occurrence_id, body] : value.at("input_component_bodies").items()) {
            cut.input_component_bodies.emplace(
                occurrence_id, zima::document::load_body_result(body));
        }
        if (feature.id.empty()) {
            throw std::runtime_error("Invalid Assembly cut definition");
        }
        document.cuts.push_back(std::move(cut));
    }
    document.constructions =
        zima::document::deserialize_construction_objects(
            root.at("constructions").dump());
    std::unordered_set<std::string> occurrence_ids;
    for (const auto& source : root.at("components")) {
        PartOccurrence component;
        component.occurrence_id = source.at("occurrence_id").get<std::string>();
        component.name = source.at("name").get<std::string>();
        component.source_document_id = source.at("source_document_id").get<std::string>();
        component.source_path = source.at("source_path").get<std::string>();
        component.source_kind = source_kind_from_name(
            source.at("source_kind").get<std::string>());
        component.suppressed = source.at("suppressed").get<bool>();
        component.visible = source.at("visible").get<bool>();
        component.grounded = source.at("grounded").get<bool>();
        if (component.occurrence_id.empty() || component.name.empty() ||
            component.source_document_id.empty() ||
            !occurrence_ids.insert(component.occurrence_id).second) {
            throw std::runtime_error("Assembly component identity is invalid");
        }
        const auto& placement = source.at("placement");
        component.placement = {
            placement.at("x").get<double>(), placement.at("y").get<double>(),
            placement.at("z").get<double>(),
            placement.at("rotation_x").get<double>(),
            placement.at("rotation_y").get<double>(),
            placement.at("rotation_z").get<double>(),
        };
        for (const double value : {
                component.placement.x, component.placement.y, component.placement.z,
                component.placement.rotation_x, component.placement.rotation_y,
                component.placement.rotation_z}) {
            if (!std::isfinite(value)) {
                throw std::runtime_error("Assembly component placement must be finite");
            }
        }
        component.calculated_source =
            zima::document::load_body_result(source.at("calculated_source"));
        for (const auto& snapshot : source.at("nested_snapshot")) {
            component.nested_snapshot.push_back(load_snapshot(snapshot));
        }
        validate_snapshot_list(component.nested_snapshot);
        if (component.source_kind == ComponentSourceKind::Part &&
            !component.nested_snapshot.empty()) {
            throw std::runtime_error("Part occurrence must not contain an Assembly snapshot");
        }
        for (const auto& reference : component.calculated_source.mesh.triangle_references) {
            if (component.source_kind == ComponentSourceKind::Part &&
                !reference.instance_path.empty()) {
                throw std::runtime_error("Source Part packet contains an occurrence path");
            }
        }
        for (const auto& reference : component.calculated_source.mesh
                 .original_references.triangle_references) {
            if (component.source_kind == ComponentSourceKind::Part &&
                !reference.instance_path.empty()) {
                throw std::runtime_error(
                    "Source Part reference packet contains an occurrence path");
            }
        }
        if (source.contains("placement_references")) {
            for (const auto& reference_json : source.at("placement_references")) {
                component.placement_references.push_back(
                    load_placement_reference(reference_json));
            }
            if (component.placement_references.size() > 3) {
                throw std::runtime_error(
                    "Assembly component placement reference row limit exceeded");
            }
        }
        if (source.contains("body_color_override") &&
            !source.at("body_color_override").is_null()) {
            component.body_color_override =
                source.at("body_color_override").get<std::string>();
        }
        component.body_color =
            source.value("body_color", std::string("#B9C2CC"));
        document.components.push_back(std::move(component));
    }
    for (const auto& source : root.at("dependencies")) {
        ComponentDependency dependency;
        dependency.dependency_id = source.at("dependency_id").get<std::string>();
        dependency.dependent_occurrence_id =
            source.at("dependent_occurrence_id").get<std::string>();
        dependency.prerequisite_occurrence_id =
            source.at("prerequisite_occurrence_id").get<std::string>();
        dependency.kind = dependency_kind_from_name(source.at("kind").get<std::string>());
        document.add_dependency(std::move(dependency));
    }
    static_cast<void>(document.build_scene());
    return document;
}

void AssemblyDocument::save(const std::filesystem::path& path) const {
    static_cast<void>(build_scene());
    auto constructions_json = nlohmann::json::parse(
        zima::document::serialize_construction_objects(constructions));
    nlohmann::json components_json = nlohmann::json::array();
    for (const auto& component : components) {
        validate_snapshot_list(component.nested_snapshot);
        nlohmann::json nested_snapshot = nlohmann::json::array();
        for (const auto& snapshot : component.nested_snapshot) {
            nested_snapshot.push_back(serialize_snapshot(snapshot));
        }
        components_json.push_back({
            {"occurrence_id", component.occurrence_id},
            {"name", component.name},
            {"source_document_id", component.source_document_id},
            {"source_path", component.source_path.generic_string()},
            {"source_kind", source_kind_name(component.source_kind)},
            {"suppressed", component.suppressed},
            {"visible", component.visible},
            {"grounded", component.grounded},
            {"placement", {
                {"x", component.placement.x}, {"y", component.placement.y},
                {"z", component.placement.z},
                {"rotation_x", component.placement.rotation_x},
                {"rotation_y", component.placement.rotation_y},
                {"rotation_z", component.placement.rotation_z},
            }},
            {"calculated_source",
             zima::document::serialize_body_result(component.calculated_source)},
            {"nested_snapshot", std::move(nested_snapshot)},
            {"placement_references", [&component] {
                nlohmann::json references = nlohmann::json::array();
                for (const auto& reference : component.placement_references) {
                    references.push_back(serialize_placement_reference(reference));
                }
                return references;
            }()},
            {"body_color_override", component.body_color_override
                ? nlohmann::json(*component.body_color_override)
                : nlohmann::json(nullptr)},
            {"body_color", component.body_color},
        });
    }
    nlohmann::json dependencies_json = nlohmann::json::array();
    for (const auto& dependency : dependencies) {
        dependencies_json.push_back({
            {"dependency_id", dependency.dependency_id},
            {"dependent_occurrence_id", dependency.dependent_occurrence_id},
            {"prerequisite_occurrence_id", dependency.prerequisite_occurrence_id},
            {"kind", dependency_kind_name(dependency.kind)},
        });
    }
    nlohmann::json relations_json = nlohmann::json::array();
    for (const auto& relation : relations) relations_json.push_back(
        {{"target", relation.target}, {"expression", relation.expression}});
    nlohmann::json sketches_json = nlohmann::json::array();
    for (const auto& sketch : sketches) sketches_json.push_back(sketch.serialized());
    nlohmann::json cuts_json = nlohmann::json::array();
    for (const auto& cut : cuts) {
        const auto& feature = cut.definition;
        if (feature.feature_kind != zima::document::FeatureKind::Extrusion &&
            feature.feature_kind != zima::document::FeatureKind::Revolution) {
            throw std::runtime_error("Assembly cut must be an Extrusion or Revolution");
        }
        nlohmann::json triangles = nlohmann::json::array();
        for (const auto& point : feature.extrusion.target_surface_triangles) {
            triangles.push_back({point.x, point.y, point.z});
        }
        nlohmann::json input_bodies = nlohmann::json::object();
        for (const auto& [occurrence_id, body] : cut.input_component_bodies) {
            input_bodies[occurrence_id] =
                zima::document::serialize_body_result(body);
        }
        if (feature.feature_parent_id != feature.id) {
            throw std::runtime_error("Assembly cut feature parent is invalid");
        }
        cuts_json.push_back({{"id", feature.id},
            {"feature_id", feature.feature_id},
            {"feature_parent_id", feature.feature_parent_id}, {"name", feature.name},
            {"kind", feature.feature_kind == zima::document::FeatureKind::Extrusion
                ? "extrusion" : "revolution"}, {"suppressed", feature.suppressed},
            {"x", feature.placement.x}, {"y", feature.placement.y},
            {"z", feature.placement.z}, {"rx", feature.placement.rotation_x},
            {"ry", feature.placement.rotation_y}, {"rz", feature.placement.rotation_z},
            {"sketch_id", feature.feature_kind == zima::document::FeatureKind::Extrusion
                ? feature.extrusion.sketch_id : feature.revolution.sketch_id},
            {"height", feature.extrusion.height},
            {"direction", static_cast<int>(feature.extrusion.direction)},
            {"extent", static_cast<int>(feature.extrusion.extent)},
            {"target_owner", feature.extrusion.target_face.owner_id},
            {"target_key", feature.extrusion.target_face.semantic_key},
            {"target_path", feature.extrusion.target_face.instance_path},
            {"target_origin", {feature.extrusion.target_plane_origin.x,
                feature.extrusion.target_plane_origin.y,
                feature.extrusion.target_plane_origin.z}},
            {"target_normal", {feature.extrusion.target_plane_normal.x,
                feature.extrusion.target_plane_normal.y,
                feature.extrusion.target_plane_normal.z}},
            {"target_triangles", std::move(triangles)},
            {"axis_segment_id", feature.revolution.axis_segment_id},
            {"angle", feature.revolution.angle_degrees},
            {"targets", cut.target_occurrence_ids},
            {"input_component_bodies", std::move(input_bodies)}});
    }
    const nlohmann::json root = {
        {"format", "zima-cad-cpp"}, {"format_version", 21},
        {"type", "assembly"}, {"document_id", document_id}, {"name", name},
        {"user_parameters", user_parameters},
        {"user_parameter_order", user_parameter_order},
        {"user_parameter_labels", user_parameter_labels},
        {"user_parameter_values", user_parameter_values},
        {"relations", std::move(relations_json)},
        {"document_units", document_units},
        {"document_precision", document_precision},
        {"physical_parameters", physical_parameters},
        {"physical_parameter_units", physical_parameter_units},
        {"material_parameter_descriptions", material_parameter_descriptions},
        {"family_table", family_table},
        {"sketches", std::move(sketches_json)},
        {"cuts", std::move(cuts_json)},
        {"constructions", std::move(constructions_json)},
        {"components", std::move(components_json)},
        {"dependencies", std::move(dependencies_json)},
    };
    IniSections ini;
    ini["Document"] = {
        {"format_version", "11"},
        {"type", "assembly"},
        {"document_id", document_id},
        {"name", name},
        {"family_table", family_table},
    };
    ini["DocumentUnits"] = document_units;
    ini["DocumentPrecision"] = document_precision;
    ini["Material"]["Name"] = physical_parameters.contains("MATERIAL_NAME")
        ? physical_parameters.at("MATERIAL_NAME") : "";
    for (const auto& [key, value] : physical_parameters) {
        if (key != "MATERIAL_NAME") ini["MaterialProperties"][key] = value;
    }
    ini["MaterialUnits"] = physical_parameter_units;
    for (const auto& [key, languages] : material_parameter_descriptions) {
        for (const auto& [language, value] : languages) {
            ini["MaterialDescriptions"][key + (language.empty() ? "" : "\\" + language)] =
                value;
        }
    }
    std::string order;
    for (const auto& value : user_parameter_order) {
        if (!order.empty()) order += ", ";
        order += value;
    }
    ini["UserParameters"]["Order"] = order;
    for (const auto& [key, languages] : user_parameter_labels) {
        for (const auto& [language, value] : languages) {
            ini["UserParameterLabels"][key + (language.empty() ? "" : "\\" + language)] =
                value;
        }
    }
    for (const auto& [key, languages] : user_parameter_values) {
        for (const auto& [language, value] : languages) {
            ini["UserParameterValues"][key + (language.empty() ? "" : "\\" + language)] =
                value;
        }
    }
    for (const auto& [key, value] : user_parameters) {
        if (!ini["UserParameterValues"].contains(key)) {
            ini["UserParameterValues"][key] = value;
        }
    }
    if (!relations.empty()) ini["Relations"]["Data"] = root.at("relations").dump();

    std::string container_items = document_id;
    const auto add_container = [&](const std::string& id, const std::string& name,
                                   const std::string& kind, const nlohmann::json& data) {
        if (!id.empty()) {
            container_items += "," + id;
            auto& section = ini["Container." + id];
            section["id"] = id;
            section["name"] = name;
            section["kind"] = "container";
            section["TYPE"] = kind;
            section["param.cpp_kind"] = kind;
            section["param.cpp_data"] = data.dump();
        }
    };
    auto& root_container = ini["Container." + document_id];
    root_container = {
        {"id", document_id}, {"name", name}, {"kind", "container"},
        {"TYPE", "ASSEMBLY"}, {"param.cpp_kind", "assembly"},
        {"param.cpp_assembly", root.dump()},
    };
    for (const auto& component : components) {
        const auto component_json = std::find_if(
            root.at("components").begin(), root.at("components").end(),
            [&](const nlohmann::json& value) {
                return value.at("occurrence_id").get<std::string>() ==
                    component.occurrence_id;
            });
        add_container(component.occurrence_id, component.name, "OCCURRENCE",
                      *component_json);
    }
    for (const auto& cut : cuts) {
        const auto cut_json = std::find_if(
            root.at("cuts").begin(), root.at("cuts").end(),
            [&](const nlohmann::json& value) {
                return value.at("id").get<std::string>() == cut.definition.id;
            });
        add_container(cut.definition.id, cut.definition.name, "FEATURE", *cut_json);
        ini["Children." + cut.definition.id]["items"] = cut.definition.feature_id;
        ini["Entity." + cut.definition.feature_id] = {
            {"id", cut.definition.feature_id}, {"name", cut.definition.name},
            {"kind", cut.definition.feature_kind ==
                zima::document::FeatureKind::Extrusion ? "protrusion" : "revolve"},
            {"tree_exposure", "internal"},
            {"param.cpp_data", cut_json->dump()},
        };
    }
    for (const auto& construction : constructions) {
        const auto construction_json = std::find_if(
            root.at("constructions").begin(), root.at("constructions").end(),
            [&](const nlohmann::json& value) {
                return value.at("id").get<std::string>() == construction.id;
            });
        add_container(construction.id, construction.name, "CONSTRUCTION",
                      *construction_json);
        ini["Children." + construction.id]["items"] = construction.entity_id;
        ini["Entity." + construction.entity_id] = {
            {"id", construction.entity_id}, {"name", construction.name},
            {"kind", construction.kind == zima::document::ConstructionKind::Point
                ? "point" : construction.kind == zima::document::ConstructionKind::Axis
                    ? "axis" : "plane"},
            {"tree_exposure", "internal"},
            {"param.cpp_data", construction_json->dump()},
        };
    }
    ini["Containers"]["items"] = container_items;
    ini["CachedBodies"] = {
        {"encoding", "zima-cpp-assembly-data"},
        {"data", "{}"},
    };
    write_ini(path, ini);
}

bool DependencyGraph::reaches(
    const std::string& start,
    const std::string& target,
    std::unordered_set<std::string>& visited) const {
    if (start == target) return true;
    if (!visited.insert(start).second) return false;
    const auto found = edges_.find(start);
    if (found == edges_.end()) return false;
    return std::any_of(found->second.begin(), found->second.end(),
        [&](const std::string& dependency) {
            return reaches(dependency, target, visited);
        });
}

bool DependencyGraph::would_create_cycle(
    const std::string& owner_document_id,
    const std::string& dependency_document_id) const {
    if (owner_document_id.empty() || dependency_document_id.empty() ||
        owner_document_id == dependency_document_id) return true;
    std::unordered_set<std::string> visited;
    return reaches(dependency_document_id, owner_document_id, visited);
}

void DependencyGraph::add_dependency(
    const std::string& owner_document_id,
    const std::string& dependency_document_id) {
    if (would_create_cycle(owner_document_id, dependency_document_id)) {
        throw std::invalid_argument("Assembly dependency would create a cycle");
    }
    edges_[owner_document_id].insert(dependency_document_id);
}

}  // namespace zima::assembly
