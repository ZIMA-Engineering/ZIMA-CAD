#include <zima/assembly/assembly_document.hpp>
#include <zima/document/viewer_packet_json.hpp>

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

std::string make_id() {
    std::mt19937_64 generator(static_cast<std::mt19937_64::result_type>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<unsigned long long> distribution;
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16)
           << distribution(generator) << std::setw(16) << distribution(generator);
    return stream.str();
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
    if (reference.valid()) reference.instance_path = instance_path + reference.instance_path;
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

zima::document::ConstructionObject AssemblyDocument::create_construction(
    zima::document::ConstructionKind kind) {
    return zima::document::PartDocument::create_construction(kind);
}

zima::document::ConstructionObject* AssemblyDocument::find_construction(
    const std::string& id) {
    const auto found = std::find_if(constructions.begin(), constructions.end(),
        [&](const auto& object) { return object.id == id; });
    return found == constructions.end() ? nullptr : &*found;
}

const zima::document::ConstructionObject* AssemblyDocument::find_construction(
    const std::string& id) const {
    const auto found = std::find_if(constructions.begin(), constructions.end(),
        [&](const auto& object) { return object.id == id; });
    return found == constructions.end() ? nullptr : &*found;
}

zima::kernel::ViewerMesh AssemblyDocument::construction_viewer_mesh() const {
    auto carrier = zima::document::PartDocument::create_default();
    carrier.constructions = constructions;
    return carrier.construction_viewer_mesh();
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

AssemblyMate AssemblyDocument::create_mate(
    std::string name,
    MateKind kind,
    MateReference dependent,
    MateReference prerequisite,
    double offset) {
    if (name.empty() || dependent.instance_path.occurrence_ids.size() != 1 ||
        prerequisite.instance_path.occurrence_ids.size() > 1 ||
        dependent.owner_id.empty() || dependent.semantic_key.empty() ||
        prerequisite.owner_id.empty() || prerequisite.semantic_key.empty() ||
        !std::isfinite(offset)) {
        throw std::invalid_argument("Assembly mate definition is invalid");
    }
    return {make_id(), std::move(name), kind, std::move(dependent),
            std::move(prerequisite), offset, 0.0, std::nullopt, std::nullopt, false,
            MateStatus::Uncalculated, false};
}

void AssemblyDocument::add_mate(AssemblyMate mate) {
    const bool angular = mate.kind == MateKind::AxisAngle ||
        mate.kind == MateKind::PlaneAngle;
    const bool value_mate = angular || mate.kind == MateKind::PlaneCoincident;
    const double value = angular ? mate.angle_degrees : mate.offset;
    if (mate.mate_id.empty() || mate.name.empty() || !std::isfinite(mate.offset) ||
        !std::isfinite(mate.angle_degrees) || mate.angle_degrees < 0.0 ||
        mate.angle_degrees > 180.0 ||
        (mate.lower_limit && !std::isfinite(*mate.lower_limit)) ||
        (mate.upper_limit && !std::isfinite(*mate.upper_limit)) ||
        (!value_mate && (mate.lower_limit || mate.upper_limit)) ||
        (mate.lower_limit && mate.upper_limit &&
            *mate.lower_limit > *mate.upper_limit) ||
        (mate.lower_limit && value < *mate.lower_limit) ||
        (mate.upper_limit && value > *mate.upper_limit) ||
        (angular && ((mate.lower_limit && *mate.lower_limit < 0.0) ||
                     (mate.upper_limit && *mate.upper_limit > 180.0))) ||
        mate.dependent.instance_path.occurrence_ids.size() != 1 ||
        mate.prerequisite.instance_path.occurrence_ids.size() > 1 ||
        (!mate.prerequisite.instance_path.occurrence_ids.empty() &&
         mate.dependent.instance_path.occurrence_ids.front() ==
            mate.prerequisite.instance_path.occurrence_ids.front()) ||
        find_occurrence(mate.dependent.instance_path.occurrence_ids.front()) == nullptr ||
        (!mate.prerequisite.instance_path.occurrence_ids.empty() &&
         find_occurrence(mate.prerequisite.instance_path.occurrence_ids.front()) == nullptr)) {
        throw std::invalid_argument("Assembly mate ownership is invalid");
    }
    if (std::any_of(mates.begin(), mates.end(), [&](const auto& existing) {
            return existing.mate_id == mate.mate_id;
        })) {
        throw std::invalid_argument("Assembly mate ID must be unique");
    }
    const bool component_prerequisite =
        !mate.prerequisite.instance_path.occurrence_ids.empty();
    ComponentDependency dependency;
    if (component_prerequisite) {
        dependency = {mate.mate_id,
            mate.dependent.instance_path.occurrence_ids.front(),
            mate.prerequisite.instance_path.occurrence_ids.front(),
            ComponentDependencyKind::PlacementReference};
    }
    const auto existing_dependency = std::find_if(
        dependencies.begin(), dependencies.end(), [&](const auto& existing) {
            return existing.dependency_id == mate.mate_id;
        });
    if (component_prerequisite && existing_dependency == dependencies.end()) {
        add_dependency(std::move(dependency));
    } else if (component_prerequisite &&
               (existing_dependency->dependent_occurrence_id !=
                   dependency.dependent_occurrence_id ||
                existing_dependency->prerequisite_occurrence_id !=
                   dependency.prerequisite_occurrence_id ||
                existing_dependency->kind !=
                   ComponentDependencyKind::PlacementReference)) {
        throw std::invalid_argument("Assembly mate dependency is inconsistent");
    } else if (!component_prerequisite && existing_dependency != dependencies.end()) {
        throw std::invalid_argument("Assembly datum mate must not own a component dependency");
    }
    mates.push_back(std::move(mate));
}

const AssemblyMate* AssemblyDocument::find_mate(const std::string& mate_id) const {
    const auto found = std::find_if(mates.begin(), mates.end(),
        [&](const auto& mate) { return mate.mate_id == mate_id; });
    return found == mates.end() ? nullptr : &*found;
}

AssemblyMate* AssemblyDocument::find_mate(const std::string& mate_id) {
    return const_cast<AssemblyMate*>(std::as_const(*this).find_mate(mate_id));
}

void AssemblyDocument::replace_mate(AssemblyMate mate) {
    if (find_mate(mate.mate_id) == nullptr) {
        throw std::invalid_argument("Assembly mate to replace does not exist");
    }
    auto replacement = *this;
    std::erase_if(replacement.mates,
        [&](const auto& existing) { return existing.mate_id == mate.mate_id; });
    std::erase_if(replacement.dependencies,
        [&](const auto& dependency) { return dependency.dependency_id == mate.mate_id; });
    replacement.add_mate(std::move(mate));
    *this = std::move(replacement);
}

bool AssemblyDocument::replace_mate_and_calculate(AssemblyMate mate) {
    const auto* existing = find_mate(mate.mate_id);
    if (existing == nullptr || mate.dependent.instance_path.occurrence_ids.empty()) {
        throw std::invalid_argument("Assembly mate to replace does not exist");
    }
    const std::string affected_occurrence =
        mate.dependent.instance_path.occurrence_ids.front();
    auto candidate = *this;
    candidate.replace_mate(std::move(mate));
    candidate.calculate_mates();
    const bool valid = std::ranges::all_of(candidate.mates, [&](const auto& item) {
        return item.suppressed || item.dependent.instance_path.occurrence_ids.empty() ||
            item.dependent.instance_path.occurrence_ids.front() != affected_occurrence ||
            item.status == MateStatus::Valid;
    });
    if (!valid) return false;
    *this = std::move(candidate);
    return true;
}

bool AssemblyDocument::set_mate_value(
    const std::string& mate_id, double value) {
    if (!std::isfinite(value)) return false;
    const auto* existing = find_mate(mate_id);
    if (existing == nullptr) throw std::invalid_argument("Assembly mate does not exist");
    auto changed = *existing;
    if (changed.kind == MateKind::PlaneCoincident) {
        changed.offset = value;
    } else if (changed.kind == MateKind::AxisAngle ||
               changed.kind == MateKind::PlaneAngle) {
        changed.angle_degrees = value;
    } else {
        return false;
    }
    try { return replace_mate_and_calculate(std::move(changed)); }
    catch (const std::invalid_argument&) { return false; }
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

void AssemblyDocument::remove_mate(const std::string& mate_id) {
    if (find_mate(mate_id) == nullptr) {
        throw std::invalid_argument("Assembly mate to remove does not exist");
    }
    std::erase_if(mates,
        [&](const auto& mate) { return mate.mate_id == mate_id; });
    std::erase_if(dependencies,
        [&](const auto& dependency) { return dependency.dependency_id == mate_id; });
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

void AssemblyDocument::calculate_mates() {
    constexpr double parallel_tolerance = 1.0e-7;
    std::unordered_map<std::string, ComponentPlacement> original_placements;
    for (const auto& component : components) {
        original_placements.emplace(component.occurrence_id, component.placement);
    }
    for (auto& mate : mates) mate.status = MateStatus::Uncalculated;
    const auto calculate_plane = [&](AssemblyMate& mate) {
        if (mate.dependent.kind != MateReferenceKind::Face ||
            mate.prerequisite.kind != MateReferenceKind::Face) {
            mate.status = MateStatus::UnsupportedGeometry;
            return;
        }
        const auto dependent_plane = resolve_plane(mate.dependent);
        const auto prerequisite_plane = resolve_plane(mate.prerequisite);
        if (dependent_plane.status != MateStatus::Valid) {
            mate.status = dependent_plane.status;
            return;
        }
        if (prerequisite_plane.status != MateStatus::Valid) {
            mate.status = prerequisite_plane.status;
            return;
        }
        const auto& dependent_normal = dependent_plane.plane.normal;
        const auto& prerequisite_normal = prerequisite_plane.plane.normal;
        const double alignment =
            dependent_normal.x * prerequisite_normal.x +
            dependent_normal.y * prerequisite_normal.y +
            dependent_normal.z * prerequisite_normal.z;
        const zima::kernel::Vec3 desired_normal = mate.flipped
            ? zima::kernel::Vec3{-prerequisite_normal.x, -prerequisite_normal.y,
                                 -prerequisite_normal.z}
            : alignment < 0.0
                ? zima::kernel::Vec3{-prerequisite_normal.x, -prerequisite_normal.y,
                                     -prerequisite_normal.z}
                : prerequisite_normal;
        auto* occurrence = find_occurrence(
            mate.dependent.instance_path.occurrence_ids.front());
        if (occurrence == nullptr) {
            mate.status = MateStatus::MissingReference;
            return;
        }
        if (occurrence->grounded) {
            mate.status = MateStatus::Valid;
            return;
        }
        const bool orientation_satisfied = mate.flipped
            ? std::abs(alignment + 1.0) <= parallel_tolerance
            : std::abs(std::abs(alignment) - 1.0) <= parallel_tolerance;
        if (!orientation_satisfied) {
            rotate_occurrence_about(*occurrence,
                shortest_rotation(dependent_normal, desired_normal),
                dependent_plane.plane.point);
        }
        const auto aligned_dependent = resolve_plane(mate.dependent);
        if (aligned_dependent.status != MateStatus::Valid) {
            mate.status = aligned_dependent.status;
            return;
        }
        const auto& dependent_point = aligned_dependent.plane.point;
        const auto& prerequisite_point = prerequisite_plane.plane.point;
        const double current_offset =
            (dependent_point.x - prerequisite_point.x) * prerequisite_normal.x +
            (dependent_point.y - prerequisite_point.y) * prerequisite_normal.y +
            (dependent_point.z - prerequisite_point.z) * prerequisite_normal.z;
        const double correction = mate.offset - current_offset;
        occurrence->placement.x += prerequisite_normal.x * correction;
        occurrence->placement.y += prerequisite_normal.y * correction;
        occurrence->placement.z += prerequisite_normal.z * correction;
        mate.status = MateStatus::Valid;
    };
    const auto calculate_axis = [&](AssemblyMate& mate) {
        if (mate.dependent.kind != MateReferenceKind::Axis ||
            mate.prerequisite.kind != MateReferenceKind::Axis ||
            std::abs(mate.offset) > 1.0e-12) {
            mate.status = MateStatus::UnsupportedGeometry;
            return;
        }
        const auto dependent = resolve_axis(mate.dependent);
        const auto prerequisite = resolve_axis(mate.prerequisite);
        if (dependent.status != MateStatus::Valid) {
            mate.status = dependent.status;
            return;
        }
        if (prerequisite.status != MateStatus::Valid) {
            mate.status = prerequisite.status;
            return;
        }
        const double alignment =
            dependent.axis.direction.x * prerequisite.axis.direction.x +
            dependent.axis.direction.y * prerequisite.axis.direction.y +
            dependent.axis.direction.z * prerequisite.axis.direction.z;
        const zima::kernel::Vec3 desired_direction = mate.flipped
            ? zima::kernel::Vec3{-prerequisite.axis.direction.x,
                                 -prerequisite.axis.direction.y,
                                 -prerequisite.axis.direction.z}
            : alignment < 0.0
                ? zima::kernel::Vec3{-prerequisite.axis.direction.x,
                                     -prerequisite.axis.direction.y,
                                     -prerequisite.axis.direction.z}
                : prerequisite.axis.direction;
        auto* occurrence = find_occurrence(
            mate.dependent.instance_path.occurrence_ids.front());
        if (occurrence == nullptr) {
            mate.status = MateStatus::MissingReference;
            return;
        }
        if (occurrence->grounded) {
            mate.status = MateStatus::Valid;
            return;
        }
        const bool orientation_satisfied = mate.flipped
            ? std::abs(alignment + 1.0) <= parallel_tolerance
            : std::abs(std::abs(alignment) - 1.0) <= parallel_tolerance;
        if (!orientation_satisfied) {
            rotate_occurrence_about(*occurrence,
                shortest_rotation(dependent.axis.direction, desired_direction),
                dependent.axis.point);
        }
        const auto aligned_dependent = resolve_axis(mate.dependent);
        if (aligned_dependent.status != MateStatus::Valid) {
            mate.status = aligned_dependent.status;
            return;
        }
        const zima::kernel::Vec3 delta{
            prerequisite.axis.point.x - aligned_dependent.axis.point.x,
            prerequisite.axis.point.y - aligned_dependent.axis.point.y,
            prerequisite.axis.point.z - aligned_dependent.axis.point.z};
        const double axial =
            delta.x * prerequisite.axis.direction.x +
            delta.y * prerequisite.axis.direction.y +
            delta.z * prerequisite.axis.direction.z;
        const zima::kernel::Vec3 correction{
            delta.x - axial * prerequisite.axis.direction.x,
            delta.y - axial * prerequisite.axis.direction.y,
            delta.z - axial * prerequisite.axis.direction.z};
        occurrence->placement.x += correction.x;
        occurrence->placement.y += correction.y;
        occurrence->placement.z += correction.z;
        mate.status = MateStatus::Valid;
    };
    const auto calculate_point = [&](AssemblyMate& mate) {
        if (mate.dependent.kind != MateReferenceKind::Point ||
            mate.prerequisite.kind != MateReferenceKind::Point ||
            std::abs(mate.offset) > 1.0e-12 || mate.flipped) {
            mate.status = MateStatus::UnsupportedGeometry;
            return;
        }
        const auto dependent = resolve_point(mate.dependent);
        const auto prerequisite = resolve_point(mate.prerequisite);
        if (dependent.status != MateStatus::Valid) {
            mate.status = dependent.status;
            return;
        }
        if (prerequisite.status != MateStatus::Valid) {
            mate.status = prerequisite.status;
            return;
        }
        auto* occurrence = find_occurrence(
            mate.dependent.instance_path.occurrence_ids.front());
        if (occurrence == nullptr) {
            mate.status = MateStatus::MissingReference;
            return;
        }
        if (!occurrence->grounded) {
            occurrence->placement.x += prerequisite.point.x - dependent.point.x;
            occurrence->placement.y += prerequisite.point.y - dependent.point.y;
            occurrence->placement.z += prerequisite.point.z - dependent.point.z;
        }
        mate.status = MateStatus::Valid;
    };
    const auto calculate_axis_angle = [&](AssemblyMate& mate) {
        if (mate.dependent.kind != MateReferenceKind::Axis ||
            mate.prerequisite.kind != MateReferenceKind::Axis ||
            std::abs(mate.offset) > 1.0e-12) {
            mate.status = MateStatus::UnsupportedGeometry;
            return;
        }
        const auto dependent = resolve_axis(mate.dependent);
        const auto prerequisite = resolve_axis(mate.prerequisite);
        if (dependent.status != MateStatus::Valid) {
            mate.status = dependent.status;
            return;
        }
        if (prerequisite.status != MateStatus::Valid) {
            mate.status = prerequisite.status;
            return;
        }
        auto* occurrence = find_occurrence(
            mate.dependent.instance_path.occurrence_ids.front());
        if (occurrence == nullptr) {
            mate.status = MateStatus::MissingReference;
            return;
        }
        constexpr double radians = 3.14159265358979323846 / 180.0;
        const double requested = (mate.flipped ? 180.0 - mate.angle_degrees
                                               : mate.angle_degrees) * radians;
        const auto& source = dependent.axis.direction;
        const auto& reference = prerequisite.axis.direction;
        const auto target = nearest_direction_at_angle(source, reference, requested);
        if (!occurrence->grounded) {
            rotate_occurrence_about(*occurrence,
                shortest_rotation(source, target), dependent.axis.point);
        }
        mate.status = MateStatus::Valid;
    };
    const auto calculate_plane_angle = [&](AssemblyMate& mate) {
        if (mate.dependent.kind != MateReferenceKind::Face ||
            mate.prerequisite.kind != MateReferenceKind::Face ||
            std::abs(mate.offset) > 1.0e-12) {
            mate.status = MateStatus::UnsupportedGeometry;
            return;
        }
        const auto dependent = resolve_plane(mate.dependent);
        const auto prerequisite = resolve_plane(mate.prerequisite);
        if (dependent.status != MateStatus::Valid) {
            mate.status = dependent.status;
            return;
        }
        if (prerequisite.status != MateStatus::Valid) {
            mate.status = prerequisite.status;
            return;
        }
        auto* occurrence = find_occurrence(
            mate.dependent.instance_path.occurrence_ids.front());
        if (occurrence == nullptr) {
            mate.status = MateStatus::MissingReference;
            return;
        }
        constexpr double radians = 3.14159265358979323846 / 180.0;
        const double requested = (mate.flipped ? 180.0 - mate.angle_degrees
                                               : mate.angle_degrees) * radians;
        const auto target = nearest_direction_at_angle(
            dependent.plane.normal, prerequisite.plane.normal, requested);
        if (!occurrence->grounded) {
            rotate_occurrence_about(*occurrence,
                shortest_rotation(dependent.plane.normal, target),
                dependent.plane.point);
        }
        mate.status = MateStatus::Valid;
    };
    for (auto& mate : mates) {
        if (!mate.suppressed && mate.kind == MateKind::AxisAngle) {
            calculate_axis_angle(mate);
        }
    }
    for (auto& mate : mates) {
        if (!mate.suppressed && mate.kind == MateKind::PlaneAngle) {
            calculate_plane_angle(mate);
        }
    }
    for (auto& mate : mates) {
        if (!mate.suppressed && mate.kind == MateKind::AxisCoincident) {
            calculate_axis(mate);
        }
    }
    for (auto& mate : mates) {
        if (!mate.suppressed && mate.kind == MateKind::PlaneCoincident) {
            calculate_plane(mate);
        }
    }
    for (auto& mate : mates) {
        if (!mate.suppressed && mate.kind == MateKind::PointCoincident) {
            calculate_point(mate);
        }
    }
    std::vector<bool> conflicts(mates.size(), false);
    for (std::size_t index = 0; index < mates.size(); ++index) {
        const auto& mate = mates[index];
        if (mate.suppressed || mate.status != MateStatus::Valid) continue;
        if (mate.kind == MateKind::AxisCoincident) {
            const auto dependent = resolve_axis(mate.dependent);
            const auto prerequisite = resolve_axis(mate.prerequisite);
            if (dependent.status != MateStatus::Valid ||
                prerequisite.status != MateStatus::Valid) {
                conflicts[index] = true;
                continue;
            }
            const zima::kernel::Vec3 delta{
                dependent.axis.point.x - prerequisite.axis.point.x,
                dependent.axis.point.y - prerequisite.axis.point.y,
                dependent.axis.point.z - prerequisite.axis.point.z};
            const double axial =
                delta.x * prerequisite.axis.direction.x +
                delta.y * prerequisite.axis.direction.y +
                delta.z * prerequisite.axis.direction.z;
            const zima::kernel::Vec3 radial{
                delta.x - axial * prerequisite.axis.direction.x,
                delta.y - axial * prerequisite.axis.direction.y,
                delta.z - axial * prerequisite.axis.direction.z};
            const double alignment = dot(
                dependent.axis.direction, prerequisite.axis.direction);
            conflicts[index] =
                (mate.flipped
                    ? std::abs(alignment + 1.0)
                    : std::abs(std::abs(alignment) - 1.0)) > parallel_tolerance ||
                std::sqrt(
                radial.x * radial.x + radial.y * radial.y + radial.z * radial.z) >
                parallel_tolerance;
        } else if (mate.kind == MateKind::PlaneCoincident) {
            const auto dependent = resolve_plane(mate.dependent);
            const auto prerequisite = resolve_plane(mate.prerequisite);
            if (dependent.status != MateStatus::Valid ||
                prerequisite.status != MateStatus::Valid) {
                conflicts[index] = true;
                continue;
            }
            const auto& normal = prerequisite.plane.normal;
            const double offset =
                (dependent.plane.point.x - prerequisite.plane.point.x) * normal.x +
                (dependent.plane.point.y - prerequisite.plane.point.y) * normal.y +
                (dependent.plane.point.z - prerequisite.plane.point.z) * normal.z;
            const double alignment = dot(
                dependent.plane.normal, prerequisite.plane.normal);
            conflicts[index] =
                (mate.flipped
                    ? std::abs(alignment + 1.0)
                    : std::abs(std::abs(alignment) - 1.0)) > parallel_tolerance ||
                std::abs(offset - mate.offset) > parallel_tolerance;
        } else if (mate.kind == MateKind::PointCoincident) {
            const auto dependent = resolve_point(mate.dependent);
            const auto prerequisite = resolve_point(mate.prerequisite);
            if (dependent.status != MateStatus::Valid ||
                prerequisite.status != MateStatus::Valid) {
                conflicts[index] = true;
                continue;
            }
            conflicts[index] =
                length({dependent.point.x - prerequisite.point.x,
                        dependent.point.y - prerequisite.point.y,
                        dependent.point.z - prerequisite.point.z}) > parallel_tolerance;
        } else if (mate.kind == MateKind::AxisAngle) {
            const auto dependent = resolve_axis(mate.dependent);
            const auto prerequisite = resolve_axis(mate.prerequisite);
            if (dependent.status != MateStatus::Valid ||
                prerequisite.status != MateStatus::Valid) {
                conflicts[index] = true;
                continue;
            }
            constexpr double radians = 3.14159265358979323846 / 180.0;
            const double requested = (mate.flipped ? 180.0 - mate.angle_degrees
                                                   : mate.angle_degrees) * radians;
            conflicts[index] = std::abs(dot(dependent.axis.direction,
                prerequisite.axis.direction) - std::cos(requested)) > parallel_tolerance;
        } else {
            const auto dependent = resolve_plane(mate.dependent);
            const auto prerequisite = resolve_plane(mate.prerequisite);
            if (dependent.status != MateStatus::Valid ||
                prerequisite.status != MateStatus::Valid) {
                conflicts[index] = true;
                continue;
            }
            constexpr double radians = 3.14159265358979323846 / 180.0;
            const double requested = (mate.flipped ? 180.0 - mate.angle_degrees
                                                   : mate.angle_degrees) * radians;
            conflicts[index] = std::abs(dot(dependent.plane.normal,
                prerequisite.plane.normal) - std::cos(requested)) > parallel_tolerance;
        }
    }
    std::unordered_set<std::string> conflicted_occurrences;
    for (std::size_t index = 0; index < mates.size(); ++index) {
        if (conflicts[index]) {
            mates[index].status = MateStatus::UnsupportedGeometry;
            conflicted_occurrences.insert(
                mates[index].dependent.instance_path.occurrence_ids.front());
        }
    }
    for (const auto& occurrence_id : conflicted_occurrences) {
        if (auto* occurrence = find_occurrence(occurrence_id)) {
            occurrence->placement = original_placements.at(occurrence_id);
        }
        for (auto& mate : mates) {
            if (!mate.suppressed &&
                mate.dependent.instance_path.occurrence_ids.front() == occurrence_id) {
                mate.status = MateStatus::UnsupportedGeometry;
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
        for (const auto& mate : document.mates) {
            if (mate.suppressed || mate.status != MateStatus::Valid ||
                mate.dependent.instance_path.occurrence_ids.empty() ||
                mate.dependent.instance_path.occurrence_ids.front() != occurrence_id) {
                continue;
            }
            if (mate.kind == MateKind::PointCoincident) {
                const auto dependent = document.resolve_point(mate.dependent);
                const auto prerequisite = document.resolve_point(mate.prerequisite);
                if (dependent.status != MateStatus::Valid ||
                    prerequisite.status != MateStatus::Valid) continue;
                values.insert(values.end(), {
                    dependent.point.x - prerequisite.point.x,
                    dependent.point.y - prerequisite.point.y,
                    dependent.point.z - prerequisite.point.z});
            } else if (mate.kind == MateKind::AxisCoincident) {
                const auto dependent = document.resolve_axis(mate.dependent);
                const auto prerequisite = document.resolve_axis(mate.prerequisite);
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
            } else if (mate.kind == MateKind::PlaneCoincident) {
                const auto dependent = document.resolve_plane(mate.dependent);
                const auto prerequisite = document.resolve_plane(mate.prerequisite);
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
                    dot(delta, prerequisite.plane.normal) - mate.offset});
            } else if (mate.kind == MateKind::AxisAngle) {
                const auto dependent = document.resolve_axis(mate.dependent);
                const auto prerequisite = document.resolve_axis(mate.prerequisite);
                if (dependent.status != MateStatus::Valid ||
                    prerequisite.status != MateStatus::Valid) continue;
                constexpr double radians = std::numbers::pi / 180.0;
                const double requested = (mate.flipped
                    ? 180.0 - mate.angle_degrees : mate.angle_degrees) * radians;
                values.push_back(dot(dependent.axis.direction,
                    prerequisite.axis.direction) - std::cos(requested));
            } else {
                const auto dependent = document.resolve_plane(mate.dependent);
                const auto prerequisite = document.resolve_plane(mate.prerequisite);
                if (dependent.status != MateStatus::Valid ||
                    prerequisite.status != MateStatus::Valid) continue;
                constexpr double radians = std::numbers::pi / 180.0;
                const double requested = (mate.flipped
                    ? 180.0 - mate.angle_degrees : mate.angle_degrees) * radians;
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
    for (const auto& mate : mates) {
        if (mate.suppressed) continue;
        if (mate.status == MateStatus::MissingReference ||
            mate.status == MateStatus::UnsupportedGeometry) {
            result.insert(mate.dependent.instance_path.occurrence_ids.front());
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& dependency : dependencies) {
            const auto* owning_mate = find_mate(dependency.dependency_id);
            if (owning_mate != nullptr && owning_mate->suppressed) continue;
            if (result.contains(dependency.prerequisite_occurrence_id) &&
                result.insert(dependency.dependent_occurrence_id).second) {
                changed = true;
            }
        }
    }
    return result;
}

zima::kernel::ViewerMesh AssemblyDocument::build_scene() const {
    zima::kernel::ViewerMesh scene = construction_viewer_mesh();
    std::unordered_set<std::string> occurrence_ids;
    std::unordered_set<std::string> dependency_ids;
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
    for (const auto& mate : mates) {
        if (mate.suppressed || mate.status != MateStatus::Valid) continue;
        zima::kernel::ViewerDimension dimension;
        dimension.reference = {document_id, "mate:" + mate.mate_id, {}};
        if (mate.kind == MateKind::PlaneCoincident) {
            const auto dependent = find_plane(mate.dependent);
            const auto prerequisite = find_plane(mate.prerequisite);
            if (!dependent || !prerequisite) continue;
            const auto& normal = prerequisite->normal;
            const zima::kernel::Vec3 basis = std::abs(normal.x) < 0.9
                ? zima::kernel::Vec3{1.0, 0.0, 0.0}
                : zima::kernel::Vec3{0.0, 1.0, 0.0};
            auto side = cross(normal, basis);
            const double side_length = length(side);
            side = {side.x * 10.0 / side_length, side.y * 10.0 / side_length,
                    side.z * 10.0 / side_length};
            dimension.witness_first = prerequisite->point;
            dimension.witness_second = dependent->point;
            dimension.line_first = {prerequisite->point.x + side.x,
                                    prerequisite->point.y + side.y,
                                    prerequisite->point.z + side.z};
            dimension.line_second = {dependent->point.x + side.x,
                                     dependent->point.y + side.y,
                                     dependent->point.z + side.z};
            dimension.value = mate.offset;
            dimension.label_prefix = "d=";
        } else if (mate.kind == MateKind::AxisAngle) {
            const auto dependent = find_axis(mate.dependent);
            const auto prerequisite = find_axis(mate.prerequisite);
            if (!dependent || !prerequisite) continue;
            dimension.witness_first = prerequisite->point;
            dimension.witness_second = prerequisite->point;
            dimension.line_first = {
                prerequisite->point.x + prerequisite->direction.x * 30.0,
                prerequisite->point.y + prerequisite->direction.y * 30.0,
                prerequisite->point.z + prerequisite->direction.z * 30.0};
            dimension.line_second = {
                prerequisite->point.x + dependent->direction.x * 30.0,
                prerequisite->point.y + dependent->direction.y * 30.0,
                prerequisite->point.z + dependent->direction.z * 30.0};
            dimension.value = mate.angle_degrees;
            dimension.label_prefix = "∠=";
            dimension.unit_suffix = " °";
        } else if (mate.kind == MateKind::PlaneAngle) {
            const auto dependent = find_plane(mate.dependent);
            const auto prerequisite = find_plane(mate.prerequisite);
            if (!dependent || !prerequisite) continue;
            dimension.witness_first = prerequisite->point;
            dimension.witness_second = prerequisite->point;
            dimension.line_first = {
                prerequisite->point.x + prerequisite->normal.x * 30.0,
                prerequisite->point.y + prerequisite->normal.y * 30.0,
                prerequisite->point.z + prerequisite->normal.z * 30.0};
            dimension.line_second = {
                prerequisite->point.x + dependent->normal.x * 30.0,
                prerequisite->point.y + dependent->normal.y * 30.0,
                prerequisite->point.z + dependent->normal.z * 30.0};
            dimension.value = mate.angle_degrees;
            dimension.label_prefix = "∠=";
            dimension.unit_suffix = " °";
        } else {
            continue;
        }
        scene.dimensions.push_back(std::move(dimension));
    }
    if (scene.triangle_references.size() != scene.triangles.size() / 3) {
        throw std::runtime_error("Assembly triangle references are not aligned");
    }
    if (scene.original_references.triangle_references.size() !=
        scene.original_references.triangles.size() / 3) {
        throw std::runtime_error("Assembly reference triangle data are not aligned");
    }
    return scene;
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
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open assembly: " + path.string());
    nlohmann::json root;
    input >> root;
    if (root.at("format").get<std::string>() != "zima-cad-cpp" ||
        root.at("format_version").get<int>() != 7 ||
        root.at("type").get<std::string>() != "assembly") {
        throw std::runtime_error("Unsupported ZIMA-CAD Assembly document format");
    }
    AssemblyDocument document;
    document.document_id = root.at("document_id").get<std::string>();
    document.name = root.at("name").get<std::string>();
    std::unordered_set<std::string> construction_ids;
    for (const auto& source : root.at("constructions")) {
        zima::document::ConstructionObject object;
        object.id = source.at("id").get<std::string>();
        object.name = source.at("name").get<std::string>();
        const auto type = source.at("type").get<std::string>();
        object.kind = type == "point" ? zima::document::ConstructionKind::Point
            : type == "axis" ? zima::document::ConstructionKind::Axis
            : type == "plane" ? zima::document::ConstructionKind::Plane
            : throw std::runtime_error("Assembly construction type is invalid");
        const auto& origin = source.at("origin");
        const auto& direction = source.at("direction");
        object.origin = {origin.at("x").get<double>(), origin.at("y").get<double>(),
                         origin.at("z").get<double>()};
        object.direction = {direction.at("x").get<double>(),
                            direction.at("y").get<double>(),
                            direction.at("z").get<double>()};
        object.display_size = source.at("display_size").get<double>();
        const double direction_length = length(object.direction);
        if (object.id.empty() || object.name.empty() ||
            !construction_ids.insert(object.id).second ||
            !std::isfinite(object.origin.x) || !std::isfinite(object.origin.y) ||
            !std::isfinite(object.origin.z) || !std::isfinite(object.direction.x) ||
            !std::isfinite(object.direction.y) || !std::isfinite(object.direction.z) ||
            !std::isfinite(object.display_size) || object.display_size <= 0.0 ||
            (object.kind != zima::document::ConstructionKind::Point &&
             direction_length <= 1.0e-12)) {
            throw std::runtime_error("Assembly construction object is invalid");
        }
        document.constructions.push_back(std::move(object));
    }
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
    for (const auto& source : root.at("mates")) {
        const auto load_reference = [](const nlohmann::json& value) {
            return MateReference{
                mate_reference_kind_from_name(value.at("kind").get<std::string>()),
                InstancePath::decode(value.at("instance_path").get<std::string>()),
                value.at("owner_id").get<std::string>(),
                value.at("semantic_key").get<std::string>()};
        };
        AssemblyMate mate;
        mate.mate_id = source.at("mate_id").get<std::string>();
        mate.name = source.at("name").get<std::string>();
        mate.kind = mate_kind_from_name(source.at("kind").get<std::string>());
        mate.dependent = load_reference(source.at("dependent"));
        mate.prerequisite = load_reference(source.at("prerequisite"));
        mate.offset = source.at("offset").get<double>();
        mate.angle_degrees = source.at("angle_degrees").get<double>();
        if (source.contains("lower_limit")) {
            mate.lower_limit = source.at("lower_limit").get<double>();
        }
        if (source.contains("upper_limit")) {
            mate.upper_limit = source.at("upper_limit").get<double>();
        }
        mate.flipped = source.at("flipped").get<bool>();
        mate.status = mate_status_from_name(source.at("status").get<std::string>());
        mate.suppressed = source.at("suppressed").get<bool>();
        document.add_mate(std::move(mate));
    }
    static_cast<void>(document.build_scene());
    return document;
}

void AssemblyDocument::save(const std::filesystem::path& path) const {
    static_cast<void>(build_scene());
    nlohmann::json constructions_json = nlohmann::json::array();
    std::unordered_set<std::string> construction_ids;
    for (const auto& object : constructions) {
        const double direction_length = length(object.direction);
        if (object.id.empty() || object.name.empty() ||
            !construction_ids.insert(object.id).second ||
            !std::isfinite(object.origin.x) || !std::isfinite(object.origin.y) ||
            !std::isfinite(object.origin.z) || !std::isfinite(object.direction.x) ||
            !std::isfinite(object.direction.y) || !std::isfinite(object.direction.z) ||
            !std::isfinite(object.display_size) || object.display_size <= 0.0 ||
            (object.kind != zima::document::ConstructionKind::Point &&
             direction_length <= 1.0e-12)) {
            throw std::runtime_error("Assembly construction object is invalid");
        }
        constructions_json.push_back({
            {"id", object.id}, {"name", object.name},
            {"type", object.kind == zima::document::ConstructionKind::Point
                ? "point" : object.kind == zima::document::ConstructionKind::Axis
                    ? "axis" : "plane"},
            {"origin", {{"x", object.origin.x}, {"y", object.origin.y},
                        {"z", object.origin.z}}},
            {"direction", {{"x", object.direction.x}, {"y", object.direction.y},
                           {"z", object.direction.z}}},
            {"display_size", object.display_size},
        });
    }
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
    const auto serialize_reference = [](const MateReference& reference) {
        return nlohmann::json{
            {"kind", mate_reference_kind_name(reference.kind)},
            {"instance_path", reference.instance_path.encoded()},
            {"owner_id", reference.owner_id},
            {"semantic_key", reference.semantic_key}};
    };
    nlohmann::json mates_json = nlohmann::json::array();
    for (const auto& mate : mates) {
        nlohmann::json serialized = {
            {"mate_id", mate.mate_id}, {"name", mate.name},
            {"kind", mate_kind_name(mate.kind)},
            {"dependent", serialize_reference(mate.dependent)},
            {"prerequisite", serialize_reference(mate.prerequisite)},
            {"offset", mate.offset}, {"angle_degrees", mate.angle_degrees},
            {"flipped", mate.flipped},
            {"status", mate_status_name(mate.status)},
            {"suppressed", mate.suppressed},
        };
        if (mate.lower_limit) serialized["lower_limit"] = *mate.lower_limit;
        if (mate.upper_limit) serialized["upper_limit"] = *mate.upper_limit;
        mates_json.push_back(std::move(serialized));
    }
    const nlohmann::json root = {
        {"format", "zima-cad-cpp"}, {"format_version", 7},
        {"type", "assembly"}, {"document_id", document_id}, {"name", name},
        {"constructions", std::move(constructions_json)},
        {"components", std::move(components_json)},
        {"dependencies", std::move(dependencies_json)},
        {"mates", std::move(mates_json)},
    };
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot write assembly: " + path.string());
        output << std::setw(2) << root << '\n';
        if (!output) throw std::runtime_error("Assembly write failed: " + path.string());
    }
    std::filesystem::rename(temporary, path);
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
