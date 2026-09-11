#pragma once

#include <zima/assembly/assembly_document.hpp>
#include <zima/assembly/assembly_session.hpp>
#include <zima/document/document_session.hpp>
#include <zima/drawing/drawing_document.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace zima::workspace {

struct PartState {
    zima::document::DocumentSession session;
    std::filesystem::path path;
    mutable std::optional<zima::kernel::BodySnapshot> source_geometry;
    mutable std::uint64_t source_generation{};
};

struct AssemblyState {
    zima::assembly::AssemblySession session;
    std::filesystem::path path;
};

struct DrawingState {
    zima::drawing::DrawingDocument document;
    std::filesystem::path path;
};

using DocumentState = std::variant<PartState, AssemblyState, DrawingState>;

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
    void add_drawing(
        zima::drawing::DrawingDocument document,
        std::filesystem::path path = {});
    // Save independent copies from the current in-memory state. Does not
    // retarget/mark saved/activate the original documents or calculate OCCT.
    [[nodiscard]] std::vector<std::filesystem::path> save_copy(
        const std::string& document_id, const std::filesystem::path& target,
        const std::filesystem::path& drawing_search_directory = {}) const;
    [[nodiscard]] bool remove(const std::string& document_id);

    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] DocumentState* find(const std::string& document_id);
    [[nodiscard]] const DocumentState* find(const std::string& document_id) const;
    [[nodiscard]] const std::string& active_document_id() const;
    [[nodiscard]] const std::string& displayed_document_id() const;
    void activate(const std::string& document_id);
    // Share already calculated source data; never calculate OCCT or solve mates.
    void refresh_source_geometry();
    void display_top_level(const std::string& document_id);

    [[nodiscard]] PartState* open_part(const std::string& document_id);
    [[nodiscard]] const PartState* open_part(const std::string& document_id) const;
    [[nodiscard]] AssemblyState* open_assembly(const std::string& document_id);
    [[nodiscard]] const AssemblyState* open_assembly(const std::string& document_id) const;
    [[nodiscard]] DrawingState* open_drawing(const std::string& document_id);
    [[nodiscard]] const DrawingState* open_drawing(const std::string& document_id) const;
    [[nodiscard]] std::optional<std::string> document_id_for_path(
        const std::filesystem::path& path) const;
    [[nodiscard]] zima::kernel::ViewerMesh authoritative_viewer_mesh(
        const std::string& document_id) const;
    [[nodiscard]] const std::vector<DocumentState>& documents() const;
    [[nodiscard]] std::vector<DocumentState>& documents();
    [[nodiscard]] std::optional<OccurrenceAddress> resolve_occurrence(
        const std::string& top_assembly_document_id,
        const zima::assembly::InstancePath& instance_path) const;
    // Explicit Open only: resolves relative paths through unopened source assemblies.
    // Reads saved documents without activating them or regenerating dependencies.
    [[nodiscard]] std::optional<std::filesystem::path> occurrence_source_file(
        const std::string& top_assembly_document_id,
        const zima::assembly::InstancePath& instance_path) const;
    // Resolve read-only derived occurrences to their editable source using
    // persisted snapshots, including copies inside nested assemblies.
    [[nodiscard]] zima::assembly::InstancePath derived_source_path(
        const std::string& top_assembly_document_id,
        const zima::assembly::InstancePath& instance_path) const;
    [[nodiscard]] std::optional<OccurrenceAddress> activate_occurrence(
        const std::string& top_assembly_document_id,
        const zima::assembly::InstancePath& instance_path);
    [[nodiscard]] zima::kernel::Vec3 occurrence_point_to_scene(
        const std::string& top_assembly_document_id,
        const zima::assembly::InstancePath& instance_path,
        const zima::kernel::Vec3& local_point) const;
    [[nodiscard]] zima::kernel::Vec3 occurrence_point_from_scene(
        const std::string& top_assembly_document_id,
        const zima::assembly::InstancePath& instance_path,
        const zima::kernel::Vec3& scene_point) const;
    [[nodiscard]] zima::kernel::Vec3 occurrence_direction_to_scene(
        const std::string& top_assembly_document_id,
        const zima::assembly::InstancePath& instance_path,
        const zima::kernel::Vec3& local_direction) const;
    [[nodiscard]] zima::kernel::Vec3 occurrence_direction_from_scene(
        const std::string& top_assembly_document_id,
        const zima::assembly::InstancePath& instance_path,
        const zima::kernel::Vec3& scene_direction) const;
    [[nodiscard]] zima::kernel::ViewerReferenceGeometry
    authoritative_external_reference_geometry(
        const std::string& top_assembly_document_id,
        const zima::assembly::InstancePath& dependent_instance_path,
        const std::string& source_document_id) const;
    [[nodiscard]] bool refresh_context_external_references(
        zima::document::PartDocument& document) const;
    void add_external_sketch_dependency(
        const std::string& top_assembly_document_id,
        const zima::assembly::InstancePath& dependent_instance_path,
        const zima::assembly::InstancePath& prerequisite_instance_path);
    void synchronize_external_sketch_dependencies();
    [[nodiscard]] zima::kernel::ViewerMesh build_scene_with_part_override(
        const std::string& top_assembly_document_id,
        const zima::assembly::InstancePath& instance_path,
        zima::kernel::BodyResult calculated_source) const;
    [[nodiscard]] zima::kernel::ViewerMesh build_scene_with_assembly_override(
        const std::string& top_assembly_document_id,
        const zima::assembly::InstancePath& instance_path,
        const zima::assembly::AssemblyDocument& calculated_source) const;
    [[nodiscard]] std::string insert_open_part(
        const std::string& assembly_document_id,
        const std::string& part_document_id,
        std::string occurrence_name);
    [[nodiscard]] std::string insert_open_assembly(
        const std::string& owner_assembly_document_id,
        const std::string& source_assembly_document_id,
        std::string occurrence_name);
    void calculate_assembly_cuts(zima::assembly::AssemblyDocument& document) const;
    void regenerate_assembly_from_open_dependencies(
        const std::string& assembly_document_id);

private:
    std::vector<DocumentState> documents_;
    struct NativeAssemblyCache {
        std::filesystem::file_time_type modified;
        zima::assembly::AssemblyDocument document;
    };
    struct NativePartCache {
        std::filesystem::file_time_type modified;
        PartState part;
    };
    std::map<std::filesystem::path,NativePartCache> native_part_cache_;
    std::map<std::filesystem::path,NativeAssemblyCache> native_assembly_cache_;
    std::string active_document_id_;
    std::string displayed_document_id_;
    [[nodiscard]] static const std::string& id_of(const DocumentState& state);
    [[nodiscard]] bool assembly_reaches(
        const std::string& start_document_id,
        const std::string& target_document_id) const;
    [[nodiscard]] zima::assembly::AssemblyDocument refreshed_assembly(
        const std::string& assembly_document_id,
        std::vector<std::string>& recursion_stack,
        const std::filesystem::path& source_path = {}) const;
};

}  // namespace zima::workspace
