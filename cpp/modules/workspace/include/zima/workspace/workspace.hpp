#pragma once

#include <zima/assembly/assembly_document.hpp>
#include <zima/assembly/assembly_session.hpp>
#include <zima/document/document_session.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace zima::workspace {

struct PartState {
    zima::document::DocumentSession session;
    std::filesystem::path path;
};

struct AssemblyState {
    zima::assembly::AssemblySession session;
    std::filesystem::path path;
};

using DocumentState = std::variant<PartState, AssemblyState>;

struct OccurrenceAddress {
    std::string owner_assembly_document_id;
    std::string occurrence_id;
    std::string source_document_id;
    zima::assembly::ComponentSourceKind source_kind;
    zima::assembly::InstancePath instance_path;
};

class Workspace {
public:
    void add_part(
        zima::document::PartDocument document,
        std::vector<zima::kernel::BodyResult> calculated_boundaries = {},
        std::filesystem::path path = {});
    void add_assembly(
        zima::assembly::AssemblyDocument document,
        std::filesystem::path path = {});

    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] DocumentState* find(const std::string& document_id);
    [[nodiscard]] const DocumentState* find(const std::string& document_id) const;
    [[nodiscard]] const std::string& active_document_id() const;
    [[nodiscard]] const std::string& displayed_document_id() const;
    void activate(const std::string& document_id);
    void display_top_level(const std::string& document_id);

    [[nodiscard]] PartState* open_part(const std::string& document_id);
    [[nodiscard]] const PartState* open_part(const std::string& document_id) const;
    [[nodiscard]] AssemblyState* open_assembly(const std::string& document_id);
    [[nodiscard]] const AssemblyState* open_assembly(const std::string& document_id) const;
    [[nodiscard]] const std::vector<DocumentState>& documents() const;
    [[nodiscard]] std::optional<OccurrenceAddress> resolve_occurrence(
        const std::string& top_assembly_document_id,
        const zima::assembly::InstancePath& instance_path) const;
    [[nodiscard]] zima::kernel::ViewerMesh build_scene_with_part_override(
        const std::string& top_assembly_document_id,
        const zima::assembly::InstancePath& instance_path,
        zima::kernel::BodyResult calculated_source) const;
    [[nodiscard]] std::string insert_open_part(
        const std::string& assembly_document_id,
        const std::string& part_document_id,
        std::string occurrence_name);
    [[nodiscard]] std::string insert_open_assembly(
        const std::string& owner_assembly_document_id,
        const std::string& source_assembly_document_id,
        std::string occurrence_name);
    void regenerate_assembly_from_open_dependencies(
        const std::string& assembly_document_id);

private:
    std::vector<DocumentState> documents_;
    std::string active_document_id_;
    std::string displayed_document_id_;
    [[nodiscard]] static const std::string& id_of(const DocumentState& state);
    [[nodiscard]] bool assembly_reaches(
        const std::string& start_document_id,
        const std::string& target_document_id) const;
    [[nodiscard]] zima::assembly::AssemblyDocument refreshed_assembly(
        const std::string& assembly_document_id,
        std::vector<std::string>& recursion_stack) const;
};

}  // namespace zima::workspace
