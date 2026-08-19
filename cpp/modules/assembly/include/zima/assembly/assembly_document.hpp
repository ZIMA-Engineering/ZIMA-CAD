#pragma once

#include <zima/kernel/geometry_kernel.hpp>

#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace zima::assembly {

struct InstancePath {
    std::vector<std::string> occurrence_ids;

    [[nodiscard]] InstancePath child(const std::string& occurrence_id) const;
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
    std::vector<OccurrenceSnapshot> children;
    bool operator==(const OccurrenceSnapshot&) const = default;
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

enum class MateReferenceKind { Face, Axis, Point };
enum class MateKind { PlaneCoincident, AxisCoincident, PointCoincident };
enum class MateStatus { Uncalculated, Valid, MissingReference, UnsupportedGeometry };

struct MateReference {
    MateReferenceKind kind{MateReferenceKind::Face};
    InstancePath instance_path;
    std::string owner_id;
    std::string semantic_key;
    bool operator==(const MateReference&) const = default;
};

struct AssemblyMate {
    std::string mate_id;
    std::string name;
    MateKind kind{MateKind::PlaneCoincident};
    MateReference dependent;
    MateReference prerequisite;
    double offset{};
    bool flipped{};
    MateStatus status{MateStatus::Uncalculated};
    bool suppressed{};
    bool operator==(const AssemblyMate&) const = default;
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
    std::vector<PartOccurrence> components;
    std::vector<ComponentDependency> dependencies;
    std::vector<AssemblyMate> mates;

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
    [[nodiscard]] static AssemblyMate create_mate(
        std::string name,
        MateKind kind,
        MateReference dependent,
        MateReference prerequisite,
        double offset = 0.0);
    void add_mate(AssemblyMate mate);
    [[nodiscard]] const AssemblyMate* find_mate(const std::string& mate_id) const;
    [[nodiscard]] AssemblyMate* find_mate(const std::string& mate_id);
    void replace_mate(AssemblyMate mate);
    void remove_mate(const std::string& mate_id);
    [[nodiscard]] PlaneResolution resolve_plane(
        const MateReference& reference) const;
    [[nodiscard]] AxisResolution resolve_axis(
        const MateReference& reference) const;
    [[nodiscard]] PointResolution resolve_point(
        const MateReference& reference) const;
    void calculate_mates();
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
