#include <zima/document/cache_storage.hpp>
#include <zima/document/object_annotation_frames.hpp>
#include <zima/document/dimension_layout_json.hpp>
#include <zima/document/appearance.hpp>
#include <zima/document/derived_copy_json.hpp>
#include <set>
#include <zima/document/placement_json.hpp>
#include <zima/document/document_copy_json.hpp>
#include <zima/assembly/assembly_document.hpp>
#include <zima/assembly/physical_properties.hpp>
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
    target.annotation_frames.insert(source.annotation_frames.begin(),source.annotation_frames.end());
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

void set_placement_rotation(ComponentPlacement& placement, const RotationMatrix& rotation) {
    constexpr double degrees = 180.0 / 3.14159265358979323846;
    const double y = std::atan2(-rotation.value[2][0],
        std::hypot(rotation.value[0][0], rotation.value[1][0]));
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

template <typename Reference>
void assign_instance(Reference& reference, const std::string& instance_path) {
    // Display-body triangles and edges deliberately have no persistent
    // owner/semantic identity, but still belong to one exact occurrence.
    // Carry that viewer-only occurrence path as well so whole-body colour and
    // wire selection never have to rebuild geometry to discover ownership.
    reference.instance_path = instance_path + reference.instance_path;
}

const char* source_kind_name(ComponentSourceKind kind) {
    return kind == ComponentSourceKind::Part ? "part" : kind==ComponentSourceKind::Pattern ? "pattern" : "assembly";
}

ComponentSourceKind source_kind_from_name(const std::string& name) {
    if (name == "pattern") return ComponentSourceKind::Pattern;
    if (name == "part") return ComponentSourceKind::Part;
    if (name == "assembly") return ComponentSourceKind::Assembly;
    throw std::runtime_error("Unknown component source kind");
}

const char* dependency_kind_name(ComponentDependencyKind kind) {
    switch (kind) {
    case ComponentDependencyKind::DerivedCopyReference: return "derived_copy_reference";
    case ComponentDependencyKind::PlacementReference: return "placement_reference";
    case ComponentDependencyKind::ExternalSketchReference:
        return "external_sketch_reference";
    }
    throw std::invalid_argument("Unknown component dependency kind");
}

ComponentDependencyKind dependency_kind_from_name(const std::string& name) {
    if(name=="derived_copy_reference")return ComponentDependencyKind::DerivedCopyReference;
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
    case MateKind::PlaneAngle: return "plane_angle";
    }
    throw std::invalid_argument("Unknown Assembly mate kind");
}

MateKind mate_kind_from_name(const std::string& name) {
    if (name == "plane_coincident") return MateKind::PlaneCoincident;
    if (name == "axis_coincident") return MateKind::AxisCoincident;
    if (name == "point_coincident") return MateKind::PointCoincident;
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
        {"offset", reference.offset}, {"offset_locked",reference.offset_locked},
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
    reference.offset_locked=value.value("offset_locked",false);
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
        {"derived_source_id", snapshot.derived_source_id},{"pattern_group",snapshot.pattern_group},
    };
}

OccurrenceSnapshot load_snapshot(const nlohmann::json& source) {
    OccurrenceSnapshot snapshot;
    snapshot.occurrence_id = source.at("occurrence_id").get<std::string>();
    snapshot.name = source.at("name").get<std::string>();
    snapshot.derived_source_id = source.at("derived_source_id").get<std::string>();
    snapshot.pattern_group=source.at("pattern_group").get<bool>();
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

// The placement solve and mobility report share these same local reference
// equations. OCCT and scene reconstruction are absent from numerical iterations.
using Motion = std::array<double,6>;
using Jacobian = std::vector<Motion>;
using Vec3 = zima::kernel::Vec3;
struct PlacementPose { Vec3 position; RotationMatrix rotation; };
struct PlacementEquation {
    MateKind kind;
    Vec3 point, direction, target_point, target_direction, angle_axis;
    double value{};
};
struct PlacementSystem { std::vector<PlacementEquation> constraints; double scale{1.0}; };
Vec3 subtract(Vec3 a,Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 add(Vec3 a,Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vec3 scaled(Vec3 a,double s) { return {a.x*s,a.y*s,a.z*s}; }
Vec3 unrotate(const RotationMatrix& r,Vec3 v) {
    return {r.value[0][0]*v.x+r.value[1][0]*v.y+r.value[2][0]*v.z,
        r.value[0][1]*v.x+r.value[1][1]*v.y+r.value[2][1]*v.z,
        r.value[0][2]*v.x+r.value[1][2]*v.y+r.value[2][2]*v.z};
}
using ReferenceSources = std::initializer_list<const zima::kernel::ViewerReferenceGeometry*>;
std::optional<Vec3> reference_direction(const MateReference& reference, ReferenceSources sources) {
    const auto matches = [&](const auto& key) { return key.owner_id == reference.owner_id &&
        key.semantic_key == reference.semantic_key && key.instance_path == reference.instance_path.encoded(); };
    for (const auto* geometry : sources) {
        if (reference.kind == MateReferenceKind::Axis) {
            for (const auto& axis : geometry->axes) if (matches(axis.reference)) return axis.direction;
        } else if (reference.kind == MateReferenceKind::Face) {
            for (std::size_t i=0;i<geometry->triangle_references.size();++i) if(matches(geometry->triangle_references[i])) {
                const auto a=geometry->vertices[geometry->triangles[i*3]],b=geometry->vertices[geometry->triangles[i*3+1]],c=geometry->vertices[geometry->triangles[i*3+2]];
                const auto normal=cross(subtract(b,a),subtract(c,a));
                if(length(normal)>1e-12)return scaled(normal,1/length(normal));
            }
        }
    }
    return {};
}
Vec3 angular_orientation_axis(const AssemblyDocument& document,const PartOccurrence* component,
        const ComponentPlacementReference& row,Vec3 normal,ReferenceSources sources) {
    const auto transverse=[&](Vec3 direction)->std::optional<Vec3> {
        const auto projected=subtract(direction,scaled(normal,dot(normal,direction)));
        if(length(projected)>1e-8)return scaled(projected,1/length(projected));
        return {};
    };
    // The existing hinge axis, then its seating plane, define front/top.
    // These are directed persisted references, independent of the camera and
    // the sign/last solved pose of the moving component.
    if(component)for(const auto kind:{MateKind::AxisCoincident,MateKind::PlaneCoincident})
        for(const auto& other:component->placement_references)if(other.mate_type==kind)
            if(const auto direction=reference_direction(other.target_reference,sources))
                if(const auto axis=transverse(*direction))return *axis;
    std::string source_id=document.document_id;
    if(!row.target_reference.instance_path.occurrence_ids.empty()) {
        const auto* target=document.find_occurrence(row.target_reference.instance_path.occurrence_ids.front());
        if(target)source_id=target->source_document_id;
    }
    // With no second orienting reference, use the target's own origin frame.
    // Prefer an explicitly referenced Body/container origin when available.
    for(const auto& owner:{row.target_reference.owner_id,source_id+":origin"})
        for(const auto* key:{"origin:axis:x","origin:axis:y","origin:axis:z"}) {
            const MateReference reference{MateReferenceKind::Axis,row.target_reference.instance_path,owner,key};
            if(const auto direction=reference_direction(reference,sources))
                if(const auto axis=transverse(*direction))return *axis;
        }
    for(const auto direction:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}})
        if(const auto axis=transverse(direction))return *axis;
    throw std::runtime_error("Orientace úhlové reference není platná.");
}
double signed_plane_angle(Vec3 first,Vec3 second,Vec3 axis) {
    const auto perpendicular=cross(first,second);
    const double angle=std::atan2(length(perpendicular),dot(first,second));
    return dot(axis,perpendicular)<0 ? -angle : angle;
}
PlacementPose placement_pose(const ComponentPlacement& p) { return {{p.x,p.y,p.z},placement_rotation(p)}; }
PlacementPose step_pose(PlacementPose pose,const Motion& step,double factor,double scale) {
    pose.position=add(pose.position,{step[0]*factor*scale,step[1]*factor*scale,step[2]*factor*scale});
    const Vec3 w{step[3]*factor,step[4]*factor,step[5]*factor};
    const double angle=length(w);
    if(angle>1e-16) {
        const auto n=scaled(w,1.0/angle);const double c=std::cos(angle),sn=std::sin(angle),v=1-c;
        const RotationMatrix r{{{c+n.x*n.x*v,n.x*n.y*v-n.z*sn,n.x*n.z*v+n.y*sn},
            {n.y*n.x*v+n.z*sn,c+n.y*n.y*v,n.y*n.z*v-n.x*sn},
            {n.z*n.x*v-n.y*sn,n.z*n.y*v+n.x*sn,c+n.z*n.z*v}}};
        pose.rotation=multiply(r,pose.rotation);
    }
    return pose;
}
PlacementSystem make_placement_system(const AssemblyDocument& document,const PartOccurrence& component) {
    PlacementSystem system;
    const auto reference_scene=document.build_scene();
    const auto pose=placement_pose(component.placement);
    for(const auto& row:component.placement_references) {
        PlacementEquation equation;equation.kind=row.mate_type;equation.value=row.offset;
        if(!std::isfinite(row.offset)) throw std::runtime_error("Hodnota vazby musí být konečné číslo.");
        Vec3 moving_point{},moving_direction{};
        if(row.mate_type==MateKind::PointCoincident) {
            const auto moving=document.resolve_point(row.component_reference),target=document.resolve_point(row.target_reference);
            if(moving.status!=MateStatus::Valid || target.status!=MateStatus::Valid)continue;
            moving_point=moving.point;equation.target_point=target.point;
        } else if(row.mate_type==MateKind::AxisCoincident) {
            const auto moving=document.resolve_axis(row.component_reference),target=document.resolve_axis(row.target_reference);
            if(moving.status!=MateStatus::Valid || target.status!=MateStatus::Valid)continue;
            moving_point=moving.axis.point;moving_direction=moving.axis.direction;
            equation.target_point=target.axis.point;equation.target_direction=target.axis.direction;
        } else {
            const auto moving=document.resolve_plane(row.component_reference),target=document.resolve_plane(row.target_reference);
            if(moving.status!=MateStatus::Valid || target.status!=MateStatus::Valid)continue;
            moving_point=moving.plane.point;moving_direction=moving.plane.normal;
            equation.target_point=target.plane.point;equation.target_direction=target.plane.normal;
        }
        equation.point=unrotate(pose.rotation,subtract(moving_point,pose.position));
        equation.direction=unrotate(pose.rotation,moving_direction);
        if(row.mate_type==MateKind::PlaneAngle) {
            if(row.offset < -180 || row.offset > 180) throw std::runtime_error("Úhel ploch musí být v rozsahu −180 až 180 stupňů.");
            equation.angle_axis=angular_orientation_axis(document,&component,row,equation.target_direction,{&reference_scene.original_references});
            if(row.flip)equation.target_direction=scaled(equation.target_direction,-1);
            equation.value=row.offset*std::numbers::pi/180.0;
        } else if(row.mate_type!=MateKind::PointCoincident) {
            // Flip is an absolute orientation choice, independent of the last
            // preview pose: off aligns directions, on makes them opposite.
            // Choosing the nearest branch here would make toggling back a no-op.
            equation.direction=scaled(equation.direction,row.flip?-1.0:1.0);
        }
        system.scale=std::max(system.scale,length(equation.point));
        system.constraints.push_back(equation);
    }
    return system;
}
std::vector<double> placement_residuals(const PlacementSystem& system,const PlacementPose& pose,bool angles=true) {
    std::vector<double> values;
    const auto vector=[&](Vec3 v){values.insert(values.end(),{v.x,v.y,v.z});};
    for(const auto& equation:system.constraints) {
        if(!angles && equation.kind==MateKind::PlaneAngle)continue;
        const auto direction=multiply(pose.rotation,equation.direction);
        const auto delta=subtract(add(pose.position,multiply(pose.rotation,equation.point)),equation.target_point);
        if(equation.kind==MateKind::PointCoincident) vector(scaled(delta,1/system.scale));
        else if(equation.kind==MateKind::PlaneAngle) {
            if(std::abs(equation.value)<1e-10 || std::numbers::pi-std::abs(equation.value)<1e-10)
                vector(subtract(direction,scaled(equation.target_direction,std::abs(equation.value)<1e-10?1.0:-1.0)));
            else values.push_back(std::remainder(signed_plane_angle(equation.target_direction,direction,equation.angle_axis)-equation.value,2*std::numbers::pi));
        } else {
            vector(subtract(direction,equation.target_direction));
            if(equation.kind==MateKind::AxisCoincident)
                vector(scaled(subtract(delta,scaled(equation.target_direction,dot(delta,equation.target_direction))),1/system.scale));
            else values.push_back((dot(delta,equation.target_direction)-equation.value)/system.scale);
        }
    }
    return values;
}
Jacobian placement_jacobian(const PlacementSystem& system,const PlacementPose& pose,bool angles=true) {
    Jacobian result(placement_residuals(system,pose,angles).size());
    constexpr double step=1e-6;
    for(int column=0;column<6;++column) {
        Motion motion{};motion[column]=1;
        const auto plus=placement_residuals(system,step_pose(pose,motion,step,system.scale),angles);
        const auto minus=placement_residuals(system,step_pose(pose,motion,-step,system.scale),angles);
        for(std::size_t row=0;row<result.size();++row)result[row][column]=(plus[row]-minus[row])/(2*step);
    }
    return result;
}
double motion_dot(const Motion& a,const Motion& b) {
    double result{};for(int i=0;i<6;++i)result+=a[i]*b[i];return result;
}
std::vector<Motion> nullspace(const Jacobian& jacobian) {
    std::vector<Motion> rows,basis;
    const auto project=[](Motion& value,const std::vector<Motion>& orthogonal) {
        for(int pass=0;pass<2;++pass)for(const auto& axis:orthogonal) {
            const double coefficient=motion_dot(value,axis);
            for(int i=0;i<6;++i)value[i]-=coefficient*axis[i];
        }
    };
    for(auto row:jacobian) {
        project(row,rows);const double norm=std::sqrt(motion_dot(row,row));
        if(norm>1e-7){for(auto& x:row)x/=norm;rows.push_back(row);}
    }
    for(int i=0;i<6;++i) {
        Motion value{};value[i]=1;project(value,rows);project(value,basis);
        const double norm=std::sqrt(motion_dot(value,value));
        if(norm>1e-7){for(auto& x:value)x/=norm;basis.push_back(value);}
    }
    return basis;
}
Motion damped_step(const Jacobian& jacobian,const std::vector<double>& residual,double damping) {
    std::array<std::array<double,7>,6> normal{};
    for(std::size_t row=0;row<jacobian.size();++row)for(int i=0;i<6;++i) {
        for(int j=0;j<6;++j)normal[i][j]+=jacobian[row][i]*jacobian[row][j];
        normal[i][6]-=jacobian[row][i]*residual[row];
    }
    for(int i=0;i<6;++i)normal[i][i]+=damping;
    for(int i=0;i<6;++i) {
        int pivot=i;for(int row=i+1;row<6;++row)if(std::abs(normal[row][i])>std::abs(normal[pivot][i]))pivot=row;
        std::swap(normal[i],normal[pivot]);const double divisor=normal[i][i];
        for(int j=i;j<7;++j)normal[i][j]/=divisor;
        for(int row=0;row<6;++row)if(row!=i) {
            const double factor=normal[row][i];for(int j=i;j<7;++j)normal[row][j]-=factor*normal[i][j];
        }
    }
    Motion result{};for(int i=0;i<6;++i)result[i]=normal[i][6];return result;
}
ComponentPlacement solve_placement(const PlacementSystem& system,const PartOccurrence& component) {
    if(system.constraints.empty())return component.placement;
    auto pose=placement_pose(component.placement);
    const auto cost=[](const std::vector<double>& residual){double sum{};for(double x:residual)sum+=x*x;return sum;};
    bool changed=false;
    double damping=1e-6;
    for(int iteration=0;iteration<100;++iteration) {
        const auto residual=placement_residuals(system,pose);
        if(std::ranges::all_of(residual,[](double x){return std::abs(x)<1e-10;})) {
            if(!changed)return component.placement;
            ComponentPlacement result{pose.position.x,pose.position.y,pose.position.z};
            set_placement_rotation(result,pose.rotation);return result;
        }
        const double current_cost=cost(residual);
        const auto jacobian=placement_jacobian(system,pose);
        bool accepted=false;
        for(int attempt=0;attempt<10 && !accepted;++attempt) {
            auto step=damped_step(jacobian,residual,damping);
            const double rotation=std::hypot(step[3],step[4],step[5]);
            if(rotation>0.5)for(auto& x:step)x*=0.5/rotation;
            auto next=step_pose(pose,step,1,system.scale);
            if(cost(placement_residuals(system,next))<current_cost-std::max(1e-25,current_cost*1e-12)) {
                pose=next;damping=std::max(1e-12,damping*0.1);accepted=true;
            } else damping=std::min(1e6,damping*10);
        }
        if(!accepted) {
            // Parallel planes have no preferred angular departure direction.
            // First explore the freedoms left by the other rows, including
            // the coupled translation of a hinge whose axis is off the origin.
            auto directions=nullspace(placement_jacobian(system,pose,false));
            for(int i=3;i<6;++i){Motion direction{};direction[i]=1;directions.push_back(direction);}
            double best_cost=current_cost;
            auto best=pose;
            for(const auto& direction:directions) {
                const double angular=std::hypot(direction[3],direction[4],direction[5]);
                if(angular<1e-8)continue;
                for(double angle:{-0.5,-0.15,0.15,0.5}) {
                    const auto next=step_pose(pose,direction,angle/angular,system.scale);
                    const double score=cost(placement_residuals(system,next));
                    if(score<best_cost-1e-14){best_cost=score;best=next;accepted=true;}
                }
            }
            if(accepted){pose=best;damping=1e-6;}else break;
        }
        changed=true;
    }
    throw std::runtime_error("Nelze současně splnit vazby komponenty „"+component.name+"“. Zkontrolujte úhel a ostatní reference.");
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

kernel::BodyResult calculate_component_body(
    const PartOccurrence& occurrence, const kernel::GeometryKernel& kernel) {
    const auto calculate = [&](const auto& self, const kernel::BodySnapshot& source,
            const std::vector<OccurrenceSnapshot>& children, std::size_t depth) -> kernel::BodyResult {
        if (depth > 256) throw std::runtime_error("Assembly calculation nesting is too deep");
        if (!source->kernel_shape.empty() || children.empty()) return source;
        std::vector<kernel::PlacedBody> bodies;
        for (const auto& child : children) {
            if (!child.visible || child.manually_suppressed || child.dependency_suppressed) continue;
            const auto found=source->body_outputs.find(child.occurrence_id);
            if (found==source->body_outputs.end())
                throw std::runtime_error("Assembly calculation is missing a child revision");
            auto body=self(self,found->second,child.children,depth+1);
            if (body.kernel_shape.empty()) continue;
            const auto& p=child.placement;
            bodies.push_back({std::move(body),{p.x,p.y,p.z},
                {p.rotation_x,p.rotation_y,p.rotation_z}});
        }
        auto result=bodies.empty()?kernel::BodyResult{}:kernel.compound_bodies(bodies);
        result.mesh=source->mesh;
        result.body_outputs=source->body_outputs;
        return result;
    };
    return calculate(calculate,occurrence.calculated_source,occurrence.nested_snapshot,0);
}

PartOccurrence AssemblyDocument::create_part_occurrence(
    std::string name,
    std::string source_document_id,
    std::filesystem::path source_path,
    zima::kernel::BodySnapshot calculated_source) {
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
    zima::kernel::BodyResult snapshot;
    snapshot.mesh = calculated_document.build_scene();
    const auto suppressed=calculated_document.effectively_suppressed_occurrences();
    for(const auto& child:calculated_document.components) {
        snapshot.body_outputs.emplace(child.occurrence_id,child.calculated_source);
        if (child.visible && !suppressed.contains(child.occurrence_id)) {
            snapshot.volume += child.calculated_source->volume;
            snapshot.surface_area += child.calculated_source->surface_area;
        }
    }
    occurrence.calculated_source = std::move(snapshot);
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
            component.nested_snapshot,component.derived_copy ? component.derived_copy->source_id : std::string{},component.derived_copy&&component.derived_copy->pattern.has_value()});
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
    for(const auto& component:components)if(component.derived_copy&&component.visible) {
        document::ConstructionObject origin;
        origin.id=component.occurrence_id;origin.entity_id=origin.id+":entity";
        origin.container_origin=document::create_container_origin(origin.id);
        origin.kind=document::ConstructionKind::Point;origin.reference_valid=false;
        const auto& p=component.copy_placement;
        origin.origin={p.x,p.y,p.z};origin.rotation={p.rotation_x,p.rotation_y,p.rotation_z};
        carrier.constructions.push_back(std::move(origin));
    }
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
    const zima::kernel::Vec3& plane_normal,
    const zima::kernel::Vec3& ray_origin,
    const zima::kernel::Vec3& ray_direction) {
    if(length(reference_direction)<=1e-12 || length(plane_normal)<=1e-12 || length(ray_direction)<=1e-12)
        throw std::invalid_argument("Assembly angular drag direction is invalid");
    const auto normal=scaled(plane_normal,1/length(plane_normal));
    const double denominator=dot(normal,ray_direction);
    if(std::abs(denominator)<=1e-12*length(ray_direction))
        throw std::invalid_argument("Assembly angular drag ray is parallel to its plane");
    const double parameter=dot(normal,subtract(center,ray_origin))/denominator;
    const auto cursor=subtract(add(ray_origin,scaled(ray_direction,parameter)),center);
    if(length(cursor)<=1e-12)throw std::invalid_argument("Assembly angular drag cursor is undefined");
    return std::atan2(dot(normal,cross(reference_direction,cursor)),dot(reference_direction,cursor))*180/std::numbers::pi;
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


const PartOccurrence* AssemblyDocument::derived_source(const std::string& id) const {
    const auto* source=find_occurrence(id);std::set<std::string> visited;
    while(source&&source->derived_copy) {
        if(!visited.insert(source->occurrence_id).second)throw std::invalid_argument("Cyklická závislost odkazované kopie.");
        source=find_occurrence(source->derived_copy->source_id);
    }
    return source;
}
void AssemblyDocument::calculate_derived_copies(const zima::kernel::GeometryKernel& kernel) {
    if(std::ranges::none_of(components,[](const auto& c){return c.derived_copy.has_value();}))return;
    const auto reference_owners=[&](const document::ConstructionReference& reference) {
        std::set<std::string> owners,visiting;
        std::function<void(const document::ConstructionReference&)> collect;
        collect=[&](const auto& ref) {
            if(ref.owner_id.empty())return;
            if(!ref.instance_path.empty()){owners.insert(InstancePath::decode(ref.instance_path).occurrence_ids.front());return;}
            for(const auto& c:components)if(ref.owner_id==c.occurrence_id+":origin"){owners.insert(c.occurrence_id);return;}
            const auto object=std::ranges::find_if(constructions,[&](const auto& c){return c.id==ref.owner_id||c.entity_id==ref.owner_id||c.container_origin.id==ref.owner_id;});
            if(object==constructions.end())return;
            if(!visiting.insert(object->id).second)throw std::invalid_argument("Cyklická reference konstrukční geometrie.");
            for(const auto& ref:object->references)collect(ref);visiting.erase(object->id);
        };collect(reference);return owners;
    };
    std::set<std::string> visiting,done;
    std::function<void(const std::string&)> calculate;
    calculate=[&](const std::string& id) {
        if(done.contains(id))return;
        auto* result=find_occurrence(id);if(!result)throw std::invalid_argument("Chybí zdrojová komponenta kopie.");
        if(!visiting.insert(id).second)throw std::invalid_argument("Cyklická závislost odkazované kopie.");
        if(!result->derived_copy){
            if(!result->grounded)for(const auto& row:result->placement_references) {
                const auto& ref=row.target_reference;
                for(const auto& owner:reference_owners({ref.instance_path.encoded(),ref.owner_id,ref.semantic_key}))
                    if(owner!=id)calculate(owner);
            }
            visiting.erase(id);done.insert(id);return;
        }
        calculate(result->derived_copy->source_id);
        const auto prerequisite=[&](const document::ConstructionReference& ref) {
            for(const auto& owner:reference_owners(ref)) {
                if(owner==id) {
                    if(ref.instance_path.empty()&&ref.owner_id==id+":origin")continue;
                    throw std::invalid_argument("Kopie nemůže odkazovat na vlastní geometrii.");
                }
                calculate(owner);
            }
        };
        prerequisite(result->derived_copy->reference);
        for(const auto& ref:result->copy_placement.references) {
            if(ref.instance_path.empty()&&(ref.owner_id==id||ref.owner_id==id+":origin"))
                throw std::invalid_argument("Umístění kopie nemůže záviset na vlastním počátku.");
            prerequisite(ref);
        }
        auto reference_document=*this;
        for(auto& component:reference_document.components)component.visible=true;
        reference_document.resolve_constructions();constructions=reference_document.constructions;
        auto geometry=reference_document.build_scene().original_references;
        const auto append=[&](const kernel::ViewerReferenceGeometry& refs) {
            const auto offset=static_cast<std::uint32_t>(geometry.vertices.size());
            geometry.vertices.insert(geometry.vertices.end(),refs.vertices.begin(),refs.vertices.end());
            for(auto i:refs.triangles)geometry.triangles.push_back(offset+i);
            geometry.triangle_references.insert(geometry.triangle_references.end(),refs.triangle_references.begin(),refs.triangle_references.end());
            geometry.edges.insert(geometry.edges.end(),refs.edges.begin(),refs.edges.end());geometry.points.insert(geometry.points.end(),refs.points.begin(),refs.points.end());
            geometry.axes.insert(geometry.axes.end(),refs.axes.begin(),refs.axes.end());
        };
        append(origin_viewer_mesh().original_references);append(reference_document.construction_viewer_mesh().original_references);
        if(!document::resolve_placement(result->copy_placement,geometry))throw std::invalid_argument("Chybí reference umístění kontejneru.");
        document::PartDocument::resolve_copy_reference(*result->derived_copy,id,result->copy_placement,geometry);
        const auto* source=find_occurrence(result->derived_copy->source_id);
        const auto translation=zima::kernel::Vec3{source->placement.x,source->placement.y,source->placement.z};
        const auto rotation=zima::kernel::Vec3{source->placement.rotation_x,source->placement.rotation_y,source->placement.rotation_z};
        const auto source_body=calculate_component_body(*source,kernel);
        result->calculated_source=result->derived_copy->pattern
            ? kernel.pattern_body(source_body,*result->derived_copy->pattern,id,translation,rotation,true)
            : kernel.mirror_body(source_body,result->derived_copy->resolved_plane,{},translation,rotation);
        result->density_kg_mm3=source->density_kg_mm3;
        result->nested_mass_kg=source->nested_mass_kg;
        result->mass_volume_mm3=std::abs(result->calculated_source->volume);
        if(result->nested_mass_kg && result->derived_copy->pattern)
            *result->nested_mass_kg*=kernel::pattern_instance_count(*result->derived_copy->pattern)-1;
        result->source_document_id=source->source_document_id;result->source_path=source->source_path;result->source_kind=source->source_kind;
        result->nested_snapshot=source->nested_snapshot;result->body_color=source->body_color;result->appearance=source->appearance;result->face_colors=source->face_colors;
        if(result->derived_copy->pattern) {
            result->source_kind=ComponentSourceKind::Pattern;result->nested_snapshot.clear();
            for(unsigned index=1;index<kernel::pattern_instance_count(*result->derived_copy->pattern);++index)
                result->nested_snapshot.push_back({zima::kernel::pattern_copy_id(*result->derived_copy->pattern,index),source->name+" ("+std::to_string(index+1)+")",
                    source->source_document_id,source->source_kind,false,false,true,true,{},source->nested_snapshot});
        }
        result->placement={};result->placement_references.clear();result->grounded=true;
        visiting.erase(id);done.insert(id);
    };
    // A follower may use a reflected/patterned reference, and another copy
    // may in turn use that follower. Settle this finite dependency chain only
    // during the explicitly requested calculation.
    for(std::size_t pass=0;;++pass) {
        if(pass>components.size())throw std::invalid_argument("Umístění odkazovaných kopií se neustálilo.");
        visiting.clear();done.clear();
        for(const auto& component:components)calculate(component.occurrence_id);
        std::vector<ComponentPlacement> before;for(const auto& component:components)before.push_back(component.placement);
        calculate_placement_references();
        bool changed=false;
        for(std::size_t i=0;i<components.size();++i) {
            const auto& a=before[i];const auto& b=components[i].placement;
            for(double delta:{a.x-b.x,a.y-b.y,a.z-b.z,a.rotation_x-b.rotation_x,a.rotation_y-b.rotation_y,a.rotation_z-b.rotation_z})
                if(std::abs(delta)>1e-8)changed=true;
        }
        if(!changed)break;
    }
    std::erase_if(dependencies,[](const auto& dependency){return dependency.kind==ComponentDependencyKind::DerivedCopyReference;});
    for(const auto& component:components)if(component.derived_copy) {
        std::set<std::string> prerequisite_ids{component.derived_copy->source_id};
        const auto collect=[&](const document::ConstructionReference& ref) {
            const auto owners=reference_owners(ref);prerequisite_ids.insert(owners.begin(),owners.end());prerequisite_ids.erase(component.occurrence_id);
        };
        collect(component.derived_copy->reference);for(const auto& ref:component.copy_placement.references)collect(ref);
        for(const auto& id:prerequisite_ids)
            add_dependency(create_dependency(component.occurrence_id,id,ComponentDependencyKind::DerivedCopyReference));
    }
}

void AssemblyDocument::calculate_placement_references() {
    // No partial placement changes escape if any component has conflicting rows.
    auto pending = *this;
    for (auto& component : pending.components) {
        if (component.grounded || component.placement_references.empty()) continue;
        const auto system = make_placement_system(pending, component);
        component.placement = solve_placement(system, component);
    }
    for (std::size_t i = 0; i < components.size(); ++i)
        components[i].placement = pending.components[i].placement;
}

zima::kernel::Vec3 AssemblyDocument::placement_reference_angle_axis(const ComponentPlacementReference& reference) const {
    const auto scene=build_scene();
    const auto direction=reference_direction(reference.target_reference,{&scene.original_references});
    if(!direction)throw std::runtime_error("Chybí orientační reference úhlu.");
    const auto& path=reference.component_reference.instance_path.occurrence_ids;
    const auto* component=path.empty()?nullptr:find_occurrence(path.front());
    return angular_orientation_axis(*this,component,reference,*direction,{&scene.original_references});
}
std::optional<double> AssemblyDocument::measure_placement_reference(
    const ComponentPlacementReference& reference) const {
    if(reference.mate_type!=MateKind::PlaneCoincident && reference.mate_type!=MateKind::PlaneAngle)return {};
    const auto source=resolve_plane(reference.component_reference);
    const auto target=resolve_plane(reference.target_reference);
    if(source.status!=MateStatus::Valid || target.status!=MateStatus::Valid)return {};
    if(reference.mate_type==MateKind::PlaneAngle) {
        const auto a=source.plane.normal,b=scaled(target.plane.normal,reference.flip?-1.0:1.0);
        return signed_plane_angle(b,a,placement_reference_angle_axis(reference))*180.0/std::numbers::pi;
    }
    const auto& path=reference.component_reference.instance_path.occurrence_ids;
    const auto* component=path.empty()?nullptr:find_occurrence(path.front());
    if(!component)return {};
    const Vec3 origin{component->placement.x,component->placement.y,component->placement.z};
    // A planar face's representative point may be any triangle vertex. For
    // tilted planes its distance is arbitrary and would move the origin when
    // the mate aligns the normals. Use the plane's signed distance from the
    // component pivot, preserving that pivot when alignment is applied.
    const double local_offset=dot(subtract(source.plane.point,origin),source.plane.normal);
    return dot(subtract(origin,target.plane.point),target.plane.normal)+
        (reference.flip?-local_offset:local_offset);
}

zima::kernel::Vec3 AssemblyDocument::component_drag_translation(
    const std::string& occurrence_id, const zima::kernel::Vec3& delta) const {
    const auto* component = find_occurrence(occurrence_id);
    if (!component) throw std::invalid_argument("Assembly occurrence does not exist");
    if (component->grounded) return {};
    const auto system = make_placement_system(*this, *component);
    auto jacobian = placement_jacobian(system, placement_pose(component->placement));
    // An origin handle requests translation. Rotational freedoms remain for
    // their angle controls; dragging must never rotate a seated component.
    for (int i=3;i<6;++i) { Motion row{}; row[i]=1; jacobian.push_back(row); }
    for(int i=0;i<3;++i)if(component->value_locks.contains(i==0?"placement:x":i==1?"placement:y":"placement:z")){Motion row{};row[i]=1;jacobian.push_back(row);}
    const Motion requested{delta.x,delta.y,delta.z,0,0,0};
    Motion projected{};
    for (const auto& motion : nullspace(jacobian)) {
        const double amount = motion_dot(requested,motion);
        for (int i=0;i<3;++i) projected[i] += amount*motion[i];
    }
    return {projected[0],projected[1],projected[2]};
}

int AssemblyDocument::remaining_degrees_of_freedom(const std::string& occurrence_id) const {
    return component_constraint_state(occurrence_id).remaining_dof;
}

ComponentConstraintState AssemblyDocument::component_constraint_state(const std::string& occurrence_id) const {
    const auto* component = find_occurrence(occurrence_id);
    if (!component) throw std::invalid_argument("Assembly occurrence does not exist");
    if (component->grounded) return {0, {false,false,false,false,false,false}};
    const auto system = make_placement_system(*this, *component);
    if (system.constraints.empty()) return {};
    const auto pose = placement_pose(component->placement);
    const auto basis = nullspace(placement_jacobian(system, pose));
    ComponentConstraintState state;
    state.remaining_dof = static_cast<int>(basis.size());
    state.coordinate_free.fill(false);
    // Mobility is computed in physical world translations/rotations, not in
    // Euler coordinates (which lose rank at RY=90 degrees). Convert actual
    // free motions back to the displayed coordinates only after counting DOF.
    auto base = component->placement;
    set_placement_rotation(base, pose.rotation);
    const std::array<double,3> initial{base.rotation_x,base.rotation_y,base.rotation_z};
    for (const auto& motion : basis) {
        for (int i=0;i<3;++i) if (std::abs(motion[i])>1e-7) state.coordinate_free[i]=true;
        for (double sign : {-1.0,1.0}) {
            auto shifted=base;
            set_placement_rotation(shifted,step_pose(pose,motion,sign*1e-4,system.scale).rotation);
            const std::array<double,3> angles{shifted.rotation_x,shifted.rotation_y,shifted.rotation_z};
            for(int i=0;i<3;++i)
                if(std::abs(std::remainder(angles[i]-initial[i],360.0))>1e-5) state.coordinate_free[i+3]=true;
        }
    }
    return state;
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
            if (!target_ids.insert(target_id).second || target == nullptr || target->derived_copy) {
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
        const auto append_component_mesh = [&](const zima::kernel::ViewerMesh& source_mesh) {
            auto source_frames=zima::kernel::object_envelopes(source_mesh);
            for(auto [key,frame]:source_frames){
                frame.origin=transform_point(frame.origin,component.placement);
                for(auto& axis:frame.axes)axis=transform_direction(axis,component.placement);
                zima::kernel::EdgeReference reference{key.first,"frame",key.second};assign_instance(reference,path);
                scene.annotation_frames[{key.first,reference.instance_path}]=frame;
                if(key.first.empty()&&key.second.empty()){scene.annotation_frames[{component.occurrence_id,path}]=frame;scene.annotation_frames[{component.source_document_id,path}]=frame;}
            }

            const std::uint32_t vertex_offset =
                static_cast<std::uint32_t>(scene.vertices.size());
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
                if (edge.exact_spline) for (auto& point : edge.exact_spline->poles)
                    point = transform_point(point, component.placement);
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
                dimension.plane_normal = transform_direction(dimension.plane_normal, component.placement);
                if(dimension.label_position)dimension.label_position=transform_point(*dimension.label_position,component.placement);
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
                if (edge.exact_spline) for (auto& point : edge.exact_spline->poles)
                    point = transform_point(point, component.placement);
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
        };
        append_component_mesh(component.calculated_source->mesh);
        if (component.source_kind == ComponentSourceKind::Part) {
            // A Part's built-in Origin is defined by its persisted document
            // identity and occurrence placement, just like this Assembly's
            // own Origin. It is independent of the calculated solid cache.
            zima::document::PartDocument origin;
            origin.document_id = component.source_document_id;
            append_component_mesh(origin.origin_viewer_mesh());
        }
    }
    // A mate can reference the owning Assembly's datums, not just components.
    auto datums = origin_viewer_mesh(zima::document::viewer_mesh_bounds_diagonal(scene));
    append_viewer_mesh(datums, construction_viewer_mesh());
    const auto find_plane = [&](const MateReference& reference)
        -> std::optional<ResolvedPlane> {
        const auto path = reference.instance_path.encoded();
        for (const auto* source : {&scene.original_references, &datums.original_references}) {
        const auto& geometry = *source;
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
            } else if (row.mate_type == MateKind::PlaneAngle) {
                const auto moving = find_plane(row.component_reference);
                const auto target = find_plane(row.target_reference);
                if (!moving || !target) continue;
                dimension.witness_first = target->point;
                dimension.witness_second = target->point;
                const auto ray=scaled(target->normal,row.flip?-1.0:1.0);
                dimension.line_first = add(target->point,scaled(ray,30));
                dimension.line_second = {
                    target->point.x + moving->normal.x * 30.0,
                    target->point.y + moving->normal.y * 30.0,
                    target->point.z + moving->normal.z * 30.0};
                dimension.value = row.offset;
                dimension.label_prefix.clear();
                dimension.unit_suffix = " °";
                dimension.kind = zima::kernel::ViewerDimensionKind::Angular;
                dimension.sweep_degrees = row.offset;
                const auto orientation=angular_orientation_axis(*this,&component,row,target->normal,{&scene.original_references,&datums.original_references});
                auto normal=cross(ray,moving->normal);
                if(length(normal)<1e-10)normal=orientation;
                else if(dot(normal,orientation)<0)normal=scaled(normal,-1);
                dimension.plane_normal=normal;
                // Presentation only: anchor the plane-angle arc at the existing
                // hinge, without altering any placement equation or parameter.
                Vec3 center = target->point;
                bool hinge = false;
                for (const auto& reference : component.placement_references) {
                    if (reference.mate_type != MateKind::AxisCoincident) continue;
                    for (const auto* geometry : {&scene.original_references, &datums.original_references}) {
                        for (const auto& axis : geometry->axes) {
                            const auto& wanted = reference.target_reference;
                            if (axis.reference.owner_id != wanted.owner_id ||
                                axis.reference.semantic_key != wanted.semantic_key ||
                                axis.reference.instance_path != wanted.instance_path.encoded()) continue;
                            if (length(axis.direction) <= 1e-12 || length(normal) <= 1e-12 ||
                                length(cross(axis.direction, normal)) > 1e-6 * length(axis.direction) * length(normal)) continue;
                            center = axis.point;
                            hinge = true;
                            break;
                        }
                        if (hinge) break;
                    }
                    if (hinge) break;
                }
                if (!hinge) {
                    const auto first = scaled(target->normal, 1/length(target->normal));
                    const auto second = scaled(moving->normal, 1/length(moving->normal));
                    const auto middle = scaled(add(target->point, moving->point), .5);
                    const double cosine = dot(first, second), determinant = 1-cosine*cosine;
                    if (determinant > 1e-12) {
                        const double a = dot(first, subtract(target->point, middle));
                        const double b = dot(second, subtract(moving->point, middle));
                        center = add(middle, add(scaled(first,(a-cosine*b)/determinant),
                                                 scaled(second,(b-cosine*a)/determinant)));
                    }
                }
                const auto first_ray = cross(normal, ray);
                const auto second_ray = cross(normal, moving->normal);
                if (length(first_ray)>1e-12 && length(second_ray)>1e-12) {
                    dimension.witness_first = dimension.witness_second = center;
                    dimension.line_first = add(center, scaled(first_ray,30/length(first_ray)));
                    dimension.line_second = add(center, scaled(second_ray,30/length(second_ray)));
                }
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
    zima::kernel::ViewerMesh result = std::move(datums);
    append_viewer_mesh(result, scene);
    zima::document::PartDocument frame_source;frame_source.constructions=constructions;frame_source.sketches=sketches;
    for(const auto& cut:cuts)frame_source.history.push_back(cut.definition);
    result.annotation_frames=zima::document::part_annotation_envelopes(frame_source,result);
    return result;
}

zima::kernel::ViewerMesh AssemblyDocument::build_scene_with_part_override(
    const std::string& occurrence_id,
    zima::kernel::BodySnapshot calculated_source) const {
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
    if (ini_value(ini, "Document", "format_version") != "16" ||
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
        root = zima::document::unpack_cache_storage(nlohmann::json::parse(assembly_json));
    } catch (const nlohmann::json::exception&) {
        throw std::runtime_error("Assembly INI contains invalid Container data");
    }
    if (root.value("format", "") != "zima-cad-cpp" ||
        root.value("type", "") != "assembly") {
        throw std::runtime_error("Invalid Assembly Container data");
    }
    std::map<std::string,zima::kernel::BodySnapshot> source_geometries;
    std::set<std::string> loading_geometries;
    const auto load_geometry=[&](const auto& self,const std::string& id)->zima::kernel::BodySnapshot {
        if(const auto found=source_geometries.find(id);found!=source_geometries.end())return found->second;
        if(loading_geometries.size()>=256||!loading_geometries.insert(id).second)
            throw std::runtime_error("Cyclic Assembly source geometry");
        const auto& packet=root.at("source_geometries").at(id);
        auto body=zima::document::load_body_result(packet);
        for(const auto& [child,reference]:packet.at("source_outputs").items())
            body.body_outputs.emplace(child,self(self,reference.get<std::string>()));
        zima::kernel::BodySnapshot result(std::move(body));
        source_geometries.emplace(id,result);
        loading_geometries.erase(id);
        return result;
    };
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
    document.sections=zima::document::parse_sections(root.value("sections",nlohmann::json::array()).dump());
    document.measurements=zima::document::parse_measurements(root.value("measurements",nlohmann::json::array()).dump());
    document.dimension_layouts=zima::document::dimension_layouts_from_json(root.value("dimension_layouts",nlohmann::json::array()));
    document.dimension_identifiers = zima::document::DimensionIdentifiers::from_serialized(root.at("dimension_identifiers").dump());
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
        component.value_locks=source.value("value_locks",std::set<std::string>{});
        component.name = source.at("name").get<std::string>();
        component.source_document_id = source.at("source_document_id").get<std::string>();
        component.source_path = source.at("source_path").get<std::string>();
        component.source_kind = source_kind_from_name(
            source.at("source_kind").get<std::string>());
        component.suppressed = source.at("suppressed").get<bool>();
        component.visible = source.at("visible").get<bool>();
        component.grounded = source.at("grounded").get<bool>();
        if(!source.at("derived_copy").is_null()) {
            component.derived_copy=source.at("derived_copy").get<document::DerivedCopyParameters>();
            component.copy_placement=source.at("copy_placement").get<document::Placement>();
        }
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
            load_geometry(load_geometry,source.at("source_geometry").get<std::string>());
        if(source.contains("density_kg_mm3")&&!source.at("density_kg_mm3").is_null())component.density_kg_mm3=source.at("density_kg_mm3").get<double>();
        if(source.contains("nested_mass_kg")&&!source.at("nested_mass_kg").is_null())component.nested_mass_kg=source.at("nested_mass_kg").get<double>();
        component.mass_volume_mm3=source.value("mass_volume_mm3",0.0);
        for (const auto& snapshot : source.at("nested_snapshot")) {
            component.nested_snapshot.push_back(load_snapshot(snapshot));
        }
        validate_snapshot_list(component.nested_snapshot);
        if (component.source_kind == ComponentSourceKind::Part &&
            !component.nested_snapshot.empty()) {
            throw std::runtime_error("Part occurrence must not contain an Assembly snapshot");
        }
        for (const auto& reference : component.calculated_source->mesh.triangle_references) {
            if (component.source_kind == ComponentSourceKind::Part &&
                !reference.instance_path.empty()) {
                throw std::runtime_error("Source Part packet contains an occurrence path");
            }
        }
        for (const auto& reference : component.calculated_source->mesh
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
        component.appearance = zima::document::deserialize_appearance(source.value("appearance",std::string("{}")));
        if(source.contains("appearance_override") && !source.at("appearance_override").is_null()) component.appearance_override=zima::document::deserialize_appearance(source.at("appearance_override").get<std::string>());
        component.face_colors = source.value("face_colors",
            std::map<std::string, std::string>{});
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
    document.synchronize_dimension_identifiers();
    return document;
}

std::vector<zima::document::DimensionParameter> AssemblyDocument::dimension_parameters() const {
    zima::document::PartDocument owned;
    owned.sketches = sketches;
    owned.constructions = constructions;
    for (const auto& cut : cuts) owned.history.push_back(cut.definition);
    auto result = owned.dimension_parameters();
    for (const auto& component : components) {
        for (const auto* key : {"x", "y", "z", "rotation_x", "rotation_y", "rotation_z"})
            result.push_back({component.occurrence_id,
                std::string("parameter:placement:") + key, component.name});
        // Match the existing Assembly-owned dimension identity exactly.
        for (std::size_t i = 0; i < component.placement_references.size(); ++i) {
            const auto& reference = component.placement_references[i];
            const auto key = "placement-reference:" + component.occurrence_id + ":" + std::to_string(i);
            result.push_back({document_id, key, component.name});
            if (reference.lower_limit)
                result.push_back({document_id, key + ":lower_limit", component.name});
            if (reference.upper_limit)
                result.push_back({document_id, key + ":upper_limit", component.name});
        }
    }
    return result;
}
void AssemblyDocument::synchronize_dimension_identifiers() {
    dimension_identifiers.synchronize(dimension_parameters());
}

void AssemblyDocument::save(const std::filesystem::path& path,
    const zima::document::DocumentCopyIdentity& copy) const {
    nlohmann::json source_geometries=nlohmann::json::object();
    std::map<const zima::kernel::BodyResult*,std::string> geometry_ids;
    const auto geometry_id=[&](const auto& self,const zima::kernel::BodySnapshot& snapshot)->std::string {
        if(const auto found=geometry_ids.find(&snapshot.get());found!=geometry_ids.end())return found->second;
        const auto id=std::to_string(geometry_ids.size());
        geometry_ids.emplace(&snapshot.get(),id);
        auto packet=zima::document::serialize_body_result(snapshot,false);
        packet["source_outputs"]=nlohmann::json::object();
        for(const auto& [child,body]:snapshot->body_outputs)
            packet["source_outputs"][child]=self(self,body);
        source_geometries[id]=std::move(packet);
        return id;
    };
    auto identifiers = dimension_identifiers;
    identifiers.synchronize(dimension_parameters());
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
            {"occurrence_id", component.occurrence_id}, {"value_locks",component.value_locks},
            {"name", component.name},
            {"source_document_id", component.source_document_id},
            {"source_path", component.source_path.generic_string()},
            {"source_kind", source_kind_name(component.source_kind)},
            {"density_kg_mm3",component.density_kg_mm3?nlohmann::json(*component.density_kg_mm3):nlohmann::json(nullptr)},
            {"nested_mass_kg",component.nested_mass_kg?nlohmann::json(*component.nested_mass_kg):nlohmann::json(nullptr)},
            {"mass_volume_mm3",component.mass_volume_mm3},
            {"suppressed", component.suppressed},
            {"visible", component.visible},
            {"grounded", component.grounded},
            {"derived_copy",component.derived_copy?nlohmann::json(*component.derived_copy):nlohmann::json(nullptr)},
            {"copy_placement",component.derived_copy?nlohmann::json(component.copy_placement):nlohmann::json(nullptr)},
            {"placement", {
                {"x", component.placement.x}, {"y", component.placement.y},
                {"z", component.placement.z},
                {"rotation_x", component.placement.rotation_x},
                {"rotation_y", component.placement.rotation_y},
                {"rotation_z", component.placement.rotation_z},
            }},
            {"source_geometry", geometry_id(geometry_id,component.calculated_source)},
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
            {"appearance", zima::document::serialize_appearance(component.appearance)},
            {"appearance_override",component.appearance_override ? nlohmann::json(zima::document::serialize_appearance(*component.appearance_override)) : nlohmann::json(nullptr)},
            {"face_colors", component.face_colors},
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
    nlohmann::json root = {
        {"format", "zima-cad-cpp"}, {"format_version", 25},
        {"type", "assembly"}, {"document_id", document_id}, {"name", name},
        {"user_parameters", user_parameters},
        {"user_parameter_order", user_parameter_order},
        {"user_parameter_labels", user_parameter_labels},
        {"user_parameter_values", user_parameter_values},
        {"relations", std::move(relations_json)},
        {"dimension_identifiers", nlohmann::json::parse(identifiers.serialized())},
        {"dimension_layouts",zima::document::dimension_layouts_json(dimension_layouts)},
        {"document_units", document_units},
        {"document_precision", document_precision},
        {"physical_parameters", physical_parameters},
        {"physical_parameter_units", physical_parameter_units},
        {"material_parameter_descriptions", material_parameter_descriptions},
        {"family_table", family_table},
        {"sections",nlohmann::json::parse(zima::document::serialize_sections(sections))},
        {"measurements",nlohmann::json::parse(zima::document::serialize_measurements(measurements))},
        {"sketches", std::move(sketches_json)},
        {"cuts", std::move(cuts_json)},
        {"constructions", std::move(constructions_json)},
        {"components", std::move(components_json)},
        {"source_geometries",std::move(source_geometries)},
        {"dependencies", std::move(dependencies_json)},
    };
    zima::document::apply_document_copy_identity(root, copy);
    const auto saved_id = root.at("document_id").get<std::string>();
    const auto saved_name = root.at("name").get<std::string>();
    IniSections ini;
    ini["Document"] = {
        {"format_version", "16"},
        {"type", "assembly"},
        {"document_id", saved_id},
        {"name", saved_name},
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

    std::string container_items = saved_id;
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
            if (kind != "OCCURRENCE") section["param.cpp_data"] = data.dump();
        }
    };
    auto& root_container = ini["Container." + saved_id];
    root_container = {
        {"id", saved_id}, {"name", saved_name}, {"kind", "container"},
        {"TYPE", "ASSEMBLY"}, {"param.cpp_kind", "assembly"},
        {"param.cpp_assembly", zima::document::pack_cache_storage(root).dump()},
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
