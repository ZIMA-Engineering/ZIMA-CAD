#pragma once

#include <zima/kernel/geometry_kernel.hpp>
#include <zima/document/part_document.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace zima::assembly {

struct InstancePath {
    std::vector<std::string> occurrence_ids;

    [[nodiscard]] InstancePath child(const std::string& occurrence_id) const;
    [[nodiscard]] std::optional<InstancePath> parent() const;
    [[nodiscard]] std::string encoded() const;
    [[nodiscard]] static InstancePath decode(const std::string& encoded);
    bool operator==(const InstancePath&) const = default;
};

struct ComponentPlacement {
    double x{};
    double y{};
    double z{};
    double rotation_x{};
    double rotation_y{};
    double rotation_z{};
};

enum class ComponentSourceKind {
    Part,
    Assembly,
};

struct OccurrenceSnapshot {
    std::string occurrence_id;
    std::string name;
    std::string source_document_id;
    ComponentSourceKind source_kind{ComponentSourceKind::Part};
    bool manually_suppressed{};
    bool dependency_suppressed{};
    bool visible{true};
    bool grounded{};
    ComponentPlacement placement;
    std::vector<OccurrenceSnapshot> children;
    bool operator==(const OccurrenceSnapshot&) const = default;
};

enum class MateReferenceKind { Face, Axis, Point };
enum class MateKind {
    PlaneCoincident, AxisCoincident, PointCoincident, AxisAngle, PlaneAngle
};
enum class MateStatus { Uncalculated, Valid, MissingReference, UnsupportedGeometry };

struct MateReference {
    MateReferenceKind kind{MateReferenceKind::Face};
    InstancePath instance_path;
    std::string owner_id;
    std::string semantic_key;
    bool operator==(const MateReference&) const = default;
};

// A single placement reference row stored directly on the component that is
// being positioned -- the Python reference design (component.parameters
// ["assembly_mates"]) rather than a separate top-level Mate document object.
// `component_reference` is the moving geometry, picked on the ORIGINAL
// (un-transformed) source body of the occurrence being placed itself;
// `target_reference` is the geometry it is placed against, which may live on
// the assembly's own origin/constructions or on another already-placed
// component. `mate_type`/`offset` select the solving semantics
// (PlaneCoincident/AxisCoincident/PointCoincident distance-or-coincidence,
// AxisAngle/PlaneAngle angle). `flip` mirrors ConstructionReference::flip: it
// inverts the resolved direction/normal of an orientation-driving reference
// as a post-solve step, and is a no-op for a PointCoincident row (no
// direction to invert).
struct ComponentPlacementReference {
    MateKind mate_type{MateKind::PlaneCoincident};
    MateReference component_reference;
    MateReference target_reference;
    double offset{};
    bool flip{};
    std::optional<double> lower_limit;
    std::optional<double> upper_limit;
    bool operator==(const ComponentPlacementReference&) const = default;
};

struct PartOccurrence {
    std::string occurrence_id;
    std::string name;
    std::string source_document_id;
    std::filesystem::path source_path;
    ComponentSourceKind source_kind{ComponentSourceKind::Part};
    ComponentPlacement placement;
    bool grounded{};
    bool suppressed{};
    bool visible{true};
    zima::kernel::BodyResult calculated_source;
    std::vector<OccurrenceSnapshot> nested_snapshot;
    // Placement references entered directly in this component's own
    // Properties dialog (Python-style embedded reference table), capped at 3
    // rows like Python's _retained_mate_rows(value[:3]). Populated rows
    // drive calculate_placement_references()'s ComponentPlacement solve.
    std::vector<ComponentPlacementReference> placement_references;
};

// Assembly-owned subtractive feature. `definition` is deliberately the same
// object edited by the Part feature dialog; only ownership and targets differ.
struct AssemblyCut {
    zima::document::HistoryContainer definition;
    std::vector<std::string> target_occurrence_ids;
    // Persisted full component boundary immediately before this cut. It is
    // consumed by Properties rollback without recalculating through OCCT.
    std::map<std::string, zima::kernel::BodyResult> input_component_bodies;
    bool operator==(const AssemblyCut& other) const {
        return definition == other.definition &&
            target_occurrence_ids == other.target_occurrence_ids;
    }
};

enum class ComponentDependencyKind {
    PlacementReference,
    ExternalSketchReference,
};

struct ComponentDependency {
    std::string dependency_id;
    std::string dependent_occurrence_id;
    std::string prerequisite_occurrence_id;
    ComponentDependencyKind kind{ComponentDependencyKind::PlacementReference};
    bool operator==(const ComponentDependency&) const = default;
};

struct ResolvedPlane {
    zima::kernel::Vec3 point;
    zima::kernel::Vec3 normal;
};

struct PlaneResolution {
    MateStatus status{MateStatus::MissingReference};
    ResolvedPlane plane;
};

struct ResolvedAxis {
    zima::kernel::Vec3 point;
    zima::kernel::Vec3 direction;
};

struct AxisResolution {
    MateStatus status{MateStatus::MissingReference};
    ResolvedAxis axis;
};

struct PointResolution {
    MateStatus status{MateStatus::MissingReference};
    zima::kernel::Vec3 point;
};

class AssemblyDocument {
public:
    std::string document_id;
    std::string name{"Nová sestava"};
    std::map<std::string, std::string> user_parameters;
    std::vector<std::string> user_parameter_order;
    std::map<std::string, std::map<std::string, std::string>> user_parameter_labels;
    std::map<std::string, std::map<std::string, std::string>> user_parameter_values;
    std::vector<zima::document::ModelRelation> relations;
    std::map<std::string, std::string> document_units{
        {"Length", "mm"}, {"Angle", "deg"}, {"Mass", "kg"},
        {"Time", "s"}, {"Temperature", "C"}, {"Stress", "MPa"}};
    std::map<std::string, std::string> document_precision{
        {"linear_tolerance", "0.001"}, {"angular_tolerance", "0.001"},
        {"mesh_deflection", "0.1"}, {"decimal_places", "3"}};
    std::map<std::string, std::string> physical_parameters;
    std::map<std::string, std::string> physical_parameter_units;
    std::map<std::string, std::map<std::string, std::string>>
        material_parameter_descriptions;
    std::string family_table{"{\"columns\":[],\"instances\":[]}"};
    std::string named_views{"[]"};
    std::vector<PartOccurrence> components;
    std::vector<zima::sketcher::Sketch> sketches;
    std::vector<AssemblyCut> cuts;
    std::vector<zima::document::ConstructionObject> constructions;
    std::vector<ComponentDependency> dependencies;

    [[nodiscard]] static AssemblyDocument create_default();
    [[nodiscard]] static PartOccurrence create_part_occurrence(
        std::string name,
        std::string source_document_id,
        std::filesystem::path source_path,
        zima::kernel::BodyResult calculated_source);
    [[nodiscard]] static PartOccurrence create_assembly_occurrence(
        std::string name,
        std::string source_document_id,
        std::filesystem::path source_path,
        const AssemblyDocument& calculated_document);
    [[nodiscard]] const PartOccurrence* find_occurrence(
        const std::string& occurrence_id) const;
    [[nodiscard]] PartOccurrence* find_occurrence(const std::string& occurrence_id);
    [[nodiscard]] AssemblyCut* find_cut(const std::string& container_id);
    [[nodiscard]] const AssemblyCut* find_cut(const std::string& container_id) const;
    [[nodiscard]] static zima::document::ConstructionObject create_construction(
        zima::document::ConstructionKind kind);
    [[nodiscard]] zima::document::ConstructionObject* find_construction(
        const std::string& id);
    [[nodiscard]] const zima::document::ConstructionObject* find_construction(
        const std::string& id) const;
    [[nodiscard]] zima::kernel::ViewerMesh construction_viewer_mesh(
        const std::string& editing_object_id = {}) const;
    [[nodiscard]] zima::kernel::ViewerMesh origin_viewer_mesh() const;
    // Same as above, but scaling the origin axis/plane display size to the
    // supplied scene bounding-box diagonal (matches PartDocument's overload
    // and Python's max(reference_scene_size * 0.12, 4.0) sizing).
    [[nodiscard]] zima::kernel::ViewerMesh origin_viewer_mesh(
        double reference_scene_size) const;
    void resolve_constructions();
    [[nodiscard]] zima::kernel::ViewerMesh build_scene() const;
    [[nodiscard]] std::vector<OccurrenceSnapshot> occurrence_snapshot() const;
    [[nodiscard]] zima::kernel::ViewerMesh build_scene_with_part_override(
        const std::string& occurrence_id,
        zima::kernel::BodyResult calculated_source) const;
    [[nodiscard]] static AssemblyDocument load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
    [[nodiscard]] static ComponentDependency create_dependency(
        std::string dependent_occurrence_id,
        std::string prerequisite_occurrence_id,
        ComponentDependencyKind kind);
    void add_dependency(ComponentDependency dependency);
    [[nodiscard]] static double project_linear_drag_value(
        const zima::kernel::Vec3& axis_point,
        const zima::kernel::Vec3& axis_direction,
        const zima::kernel::Vec3& ray_origin,
        const zima::kernel::Vec3& ray_direction);
    [[nodiscard]] static double project_angular_drag_value(
        const zima::kernel::Vec3& center,
        const zima::kernel::Vec3& reference_direction,
        const zima::kernel::Vec3& ray_origin,
        const zima::kernel::Vec3& ray_direction);
    [[nodiscard]] PlaneResolution resolve_plane(
        const MateReference& reference) const;
    [[nodiscard]] AxisResolution resolve_axis(
        const MateReference& reference) const;
    [[nodiscard]] PointResolution resolve_point(
        const MateReference& reference) const;
    // Solves PartOccurrence::placement_references for every non-grounded
    // component -- the embedded per-component reference-row model (see
    // ComponentPlacementReference).
    void calculate_placement_references();
    [[nodiscard]] int remaining_degrees_of_freedom(
        const std::string& occurrence_id) const;
    [[nodiscard]] std::unordered_set<std::string>
        effectively_suppressed_occurrences() const;
};

class DependencyGraph {
public:
    [[nodiscard]] bool would_create_cycle(
        const std::string& owner_document_id,
        const std::string& dependency_document_id) const;
    void add_dependency(
        const std::string& owner_document_id,
        const std::string& dependency_document_id);

private:
    std::unordered_map<std::string, std::unordered_set<std::string>> edges_;
    [[nodiscard]] bool reaches(
        const std::string& start,
        const std::string& target,
        std::unordered_set<std::string>& visited) const;
};

}  // namespace zima::assembly
