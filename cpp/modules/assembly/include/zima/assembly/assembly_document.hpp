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

struct PartOccurrence {
    std::string occurrence_id;
    std::string name;
    std::string source_document_id;
    std::filesystem::path source_path;
    ComponentSourceKind source_kind{ComponentSourceKind::Part};
    ComponentPlacement placement;
    bool suppressed{};
    bool visible{true};
    zima::kernel::BodyResult calculated_source;
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
};

class AssemblyDocument {
public:
    std::string document_id;
    std::string name{"Nová sestava"};
    std::vector<PartOccurrence> components;
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
        zima::kernel::ViewerMesh calculated_scene);
    [[nodiscard]] const PartOccurrence* find_occurrence(
        const std::string& occurrence_id) const;
    [[nodiscard]] zima::kernel::ViewerMesh build_scene() const;
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
