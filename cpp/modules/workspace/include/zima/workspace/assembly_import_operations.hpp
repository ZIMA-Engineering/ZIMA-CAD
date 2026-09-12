#pragma once
#include <zima/workspace/import_operations.hpp>
#include <zima/workspace/native_documents.hpp>

namespace zima::workspace {
struct AssemblyImportOptions {
    PartImportOptions geometry;
    // Empty output_directory selects a new unique source-name directory next
    // to the owner, or under working_directory for an unsaved owner.
    std::filesystem::path output_directory, working_directory;
    std::optional<NativeTemplateSettings> templates;
};
struct AssemblyImportReport {
    std::filesystem::path directory;
    std::string occurrence_id, source_document_id, sketch_id;
    std::vector<std::string> part_ids, assembly_ids;
    std::vector<std::filesystem::path> files;
    interchange::DxfImportResult dxf;
};
// Creates native source files in a newly reserved directory, then inserts one
// occurrence into the immediate owner. Failure removes only this operation's
// files. The runner completes synchronously and never accesses the live Workspace.
[[nodiscard]] AssemblyImportReport import_assembly(Workspace&, const std::string& owner,
    const std::filesystem::path& source, const AssemblyImportOptions& = {},
    const std::function<void(std::function<void()>)>& runner = {});
}
