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
    return kind == MateReferenceKind::Face ? "face" : "axis";
}

MateReferenceKind mate_reference_kind_from_name(const std::string& name) {
    if (name == "face") return MateReferenceKind::Face;
    if (name == "axis") return MateReferenceKind::Axis;
    throw std::runtime_error("Unknown Assembly mate reference kind");
}

const char* mate_kind_name(MateKind kind) {
    return kind == MateKind::PlaneCoincident ? "plane_coincident" : "axis_coincident";
}

MateKind mate_kind_from_name(const std::string& name) {
    if (name == "plane_coincident") return MateKind::PlaneCoincident;
    if (name == "axis_coincident") return MateKind::AxisCoincident;
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
        {"visible", snapshot.visible}, {"children", std::move(children)},
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
    if (result.occurrence_ids.empty()) {
        throw std::invalid_argument("Instance path must not be empty");
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
        std::move(source_path), ComponentSourceKind::Part, {}, false, true,
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
            component.visible, component.nested_snapshot});
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
    if (name.empty() || dependent.instance_path.occurrence_ids.empty() ||
        prerequisite.instance_path.occurrence_ids.empty() ||
        dependent.owner_id.empty() || dependent.semantic_key.empty() ||
        prerequisite.owner_id.empty() || prerequisite.semantic_key.empty() ||
        !std::isfinite(offset)) {
        throw std::invalid_argument("Assembly mate definition is invalid");
    }
    return {make_id(), std::move(name), kind, std::move(dependent),
            std::move(prerequisite), offset, MateStatus::Uncalculated};
}

void AssemblyDocument::add_mate(AssemblyMate mate) {
    if (mate.mate_id.empty() || mate.name.empty() || !std::isfinite(mate.offset) ||
        mate.dependent.instance_path.occurrence_ids.empty() ||
        mate.prerequisite.instance_path.occurrence_ids.empty() ||
        mate.dependent.instance_path.occurrence_ids.front() ==
            mate.prerequisite.instance_path.occurrence_ids.front() ||
        find_occurrence(mate.dependent.instance_path.occurrence_ids.front()) == nullptr ||
        find_occurrence(mate.prerequisite.instance_path.occurrence_ids.front()) == nullptr) {
        throw std::invalid_argument("Assembly mate ownership is invalid");
    }
    if (std::any_of(mates.begin(), mates.end(), [&](const auto& existing) {
            return existing.mate_id == mate.mate_id;
        })) {
        throw std::invalid_argument("Assembly mate ID must be unique");
    }
    if (std::any_of(mates.begin(), mates.end(), [&](const auto& existing) {
            return existing.dependent.instance_path.occurrence_ids.front() ==
                mate.dependent.instance_path.occurrence_ids.front() &&
                existing.kind == mate.kind;
        })) {
        throw std::invalid_argument(
            "A component may currently own only one calculated mate of each kind");
    }
    ComponentDependency dependency{
        mate.mate_id, mate.dependent.instance_path.occurrence_ids.front(),
        mate.prerequisite.instance_path.occurrence_ids.front(),
        ComponentDependencyKind::PlacementReference};
    const auto existing_dependency = std::find_if(
        dependencies.begin(), dependencies.end(), [&](const auto& existing) {
            return existing.dependency_id == mate.mate_id;
        });
    if (existing_dependency == dependencies.end()) {
        add_dependency(std::move(dependency));
    } else if (existing_dependency->dependent_occurrence_id !=
                   dependency.dependent_occurrence_id ||
               existing_dependency->prerequisite_occurrence_id !=
                   dependency.prerequisite_occurrence_id ||
               existing_dependency->kind != ComponentDependencyKind::PlacementReference) {
        throw std::invalid_argument("Assembly mate dependency edge is inconsistent");
    }
    mates.push_back(std::move(mate));
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
    for (std::size_t triangle = 0;
         triangle < scene.triangle_references.size(); ++triangle) {
        const auto& candidate = scene.triangle_references[triangle];
        if (candidate.instance_path != path || candidate.owner_id != reference.owner_id ||
            candidate.semantic_key != reference.semantic_key) continue;
        const auto first = scene.triangles[triangle * 3];
        const auto second = scene.triangles[triangle * 3 + 1];
        const auto third = scene.triangles[triangle * 3 + 2];
        const auto& a = scene.vertices[first];
        const auto& b = scene.vertices[second];
        const auto& c = scene.vertices[third];
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
    const auto found = std::find_if(scene.axes.begin(), scene.axes.end(),
        [&](const auto& axis) {
            return axis.reference.instance_path == path &&
                axis.reference.owner_id == reference.owner_id &&
                axis.reference.semantic_key == reference.semantic_key;
        });
    if (found == scene.axes.end()) return {MateStatus::MissingReference, {}};
    return {MateStatus::Valid, {found->point, found->direction}};
}

void AssemblyDocument::calculate_mates() {
    constexpr double parallel_tolerance = 1.0e-7;
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
        if (std::abs(std::abs(alignment) - 1.0) > parallel_tolerance) {
            mate.status = MateStatus::UnsupportedGeometry;
            return;
        }
        const auto& dependent_point = dependent_plane.plane.point;
        const auto& prerequisite_point = prerequisite_plane.plane.point;
        const double current_offset =
            (dependent_point.x - prerequisite_point.x) * prerequisite_normal.x +
            (dependent_point.y - prerequisite_point.y) * prerequisite_normal.y +
            (dependent_point.z - prerequisite_point.z) * prerequisite_normal.z;
        const double correction = mate.offset - current_offset;
        auto* occurrence = find_occurrence(
            mate.dependent.instance_path.occurrence_ids.front());
        if (occurrence == nullptr) {
            mate.status = MateStatus::MissingReference;
            return;
        }
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
        if (std::abs(std::abs(alignment) - 1.0) > parallel_tolerance) {
            mate.status = MateStatus::UnsupportedGeometry;
            return;
        }
        const zima::kernel::Vec3 delta{
            prerequisite.axis.point.x - dependent.axis.point.x,
            prerequisite.axis.point.y - dependent.axis.point.y,
            prerequisite.axis.point.z - dependent.axis.point.z};
        const double axial =
            delta.x * prerequisite.axis.direction.x +
            delta.y * prerequisite.axis.direction.y +
            delta.z * prerequisite.axis.direction.z;
        const zima::kernel::Vec3 correction{
            delta.x - axial * prerequisite.axis.direction.x,
            delta.y - axial * prerequisite.axis.direction.y,
            delta.z - axial * prerequisite.axis.direction.z};
        auto* occurrence = find_occurrence(
            mate.dependent.instance_path.occurrence_ids.front());
        if (occurrence == nullptr) {
            mate.status = MateStatus::MissingReference;
            return;
        }
        occurrence->placement.x += correction.x;
        occurrence->placement.y += correction.y;
        occurrence->placement.z += correction.z;
        mate.status = MateStatus::Valid;
    };
    for (auto& mate : mates) {
        if (mate.kind == MateKind::AxisCoincident) calculate_axis(mate);
    }
    for (auto& mate : mates) {
        if (mate.kind == MateKind::PlaneCoincident) calculate_plane(mate);
    }
    std::vector<bool> conflicts(mates.size(), false);
    for (std::size_t index = 0; index < mates.size(); ++index) {
        const auto& mate = mates[index];
        if (mate.status != MateStatus::Valid) continue;
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
            conflicts[index] = std::sqrt(
                radial.x * radial.x + radial.y * radial.y + radial.z * radial.z) >
                parallel_tolerance;
        } else {
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
            conflicts[index] = std::abs(offset - mate.offset) > parallel_tolerance;
        }
    }
    for (std::size_t index = 0; index < mates.size(); ++index) {
        if (conflicts[index]) mates[index].status = MateStatus::UnsupportedGeometry;
    }
}

std::unordered_set<std::string>
AssemblyDocument::effectively_suppressed_occurrences() const {
    std::unordered_set<std::string> result;
    for (const auto& component : components) {
        if (component.suppressed) result.insert(component.occurrence_id);
    }
    for (const auto& mate : mates) {
        if (mate.status == MateStatus::MissingReference ||
            mate.status == MateStatus::UnsupportedGeometry) {
            result.insert(mate.dependent.instance_path.occurrence_ids.front());
        }
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
    zima::kernel::ViewerMesh scene;
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
    }
    if (scene.triangle_references.size() != scene.triangles.size() / 3) {
        throw std::runtime_error("Assembly triangle references are not aligned");
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
        root.at("format_version").get<int>() != 1 ||
        root.at("type").get<std::string>() != "assembly") {
        throw std::runtime_error("Unsupported C++ Assembly document format");
    }
    AssemblyDocument document;
    document.document_id = root.at("document_id").get<std::string>();
    document.name = root.at("name").get<std::string>();
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
        mate.status = mate_status_from_name(source.at("status").get<std::string>());
        document.add_mate(std::move(mate));
    }
    static_cast<void>(document.build_scene());
    return document;
}

void AssemblyDocument::save(const std::filesystem::path& path) const {
    static_cast<void>(build_scene());
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
        mates_json.push_back({
            {"mate_id", mate.mate_id}, {"name", mate.name},
            {"kind", mate_kind_name(mate.kind)},
            {"dependent", serialize_reference(mate.dependent)},
            {"prerequisite", serialize_reference(mate.prerequisite)},
            {"offset", mate.offset}, {"status", mate_status_name(mate.status)},
        });
    }
    const nlohmann::json root = {
        {"format", "zima-cad-cpp"}, {"format_version", 1},
        {"type", "assembly"}, {"document_id", document_id}, {"name", name},
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
