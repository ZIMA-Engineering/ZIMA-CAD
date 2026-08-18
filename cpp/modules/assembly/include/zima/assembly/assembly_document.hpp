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

struct PartOccurrence {
    std::string occurrence_id;
    std::string name;
    std::string source_document_id;
    std::filesystem::path source_path;
    ComponentPlacement placement;
    zima::kernel::BodyResult calculated_source;
};

class AssemblyDocument {
public:
    std::string document_id;
    std::string name{"Nová sestava"};
    std::vector<PartOccurrence> components;

    [[nodiscard]] static AssemblyDocument create_default();
    [[nodiscard]] static PartOccurrence create_part_occurrence(
        std::string name,
        std::string source_document_id,
        std::filesystem::path source_path,
        zima::kernel::BodyResult calculated_source);
    [[nodiscard]] const PartOccurrence* find_occurrence(
        const std::string& occurrence_id) const;
    [[nodiscard]] zima::kernel::ViewerMesh build_scene() const;
    [[nodiscard]] static AssemblyDocument load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
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
