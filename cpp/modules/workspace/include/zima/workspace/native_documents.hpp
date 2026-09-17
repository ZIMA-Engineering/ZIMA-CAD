#pragma once
#include <zima/workspace/workspace.hpp>

namespace zima::workspace {
enum class NativeDocumentType { Part, Assembly, Drawing };
[[nodiscard]] NativeDocumentType native_document_type(const std::filesystem::path& path);

struct NativeTemplateSettings {
    std::filesystem::path directory, part_template, assembly_template;
    std::string first_body_name{"Body 1"};
};
// Uses the existing body-origin attachment contract unchanged.
[[nodiscard]] document::PartDocument part_from_template(const NativeTemplateSettings& settings);
[[nodiscard]] assembly::AssemblyDocument assembly_from_template(const NativeTemplateSettings& settings);

class PreparedNativeDocument {
public:
    [[nodiscard]] NativeDocumentType type() const;
    [[nodiscard]] const std::string& id() const;
    [[nodiscard]] bool is_drawing_for(const std::string& source_document_id) const;
    void set_drawing_source(const std::string& source_document_id,
        const std::filesystem::path& source_path, const std::string& source_name);
    // Work only on this private, loaded snapshot; no Workspace or body calculation.
    bool rebase_native_files(std::span<const document::FileRelocation>);
    void write(const std::filesystem::path& target) const;
private:
    friend PreparedNativeDocument read_native_document(const std::filesystem::path&, const assembly::AssemblyDocument::SourceResolver&, bool);
    friend PreparedNativeDocument prepare_new_native_document(NativeDocumentType, const std::string&,
        const std::filesystem::path&, const NativeTemplateSettings&, const std::map<std::string,std::string>&);
    friend std::string insert_native_document(Workspace&, PreparedNativeDocument);
    struct Part { document::PartDocument document; std::vector<kernel::BodyResult> boundaries; };
    std::variant<Part,assembly::AssemblyDocument,drawing::DrawingDocument> document_;
    std::filesystem::path path_;
    bool new_document_{};
    PreparedNativeDocument() = default;
};
// Read/preparation can run without Workspace or Qt. Neither writes a file.
// Captures immutable open-document data on the owner thread for background reads.
[[nodiscard]] assembly::AssemblyDocument::SourceResolver native_source_resolver(const Workspace&);
[[nodiscard]] PreparedNativeDocument read_native_document(const std::filesystem::path& path,
    const assembly::AssemblyDocument::SourceResolver& resolver = {}, bool resolve_sources = true);
[[nodiscard]] PreparedNativeDocument prepare_new_native_document(NativeDocumentType type,
    const std::string& name, const std::filesystem::path& target,
    const NativeTemplateSettings& settings, const std::map<std::string,std::string>& units = {});
// Workspace owner thread. Reuses an already open path on Open, preserving edits.
// New rejects an occupied path. Activation/display remain the caller's decision.
[[nodiscard]] std::string insert_native_document(Workspace& workspace, PreparedNativeDocument prepared);
} // namespace zima::workspace
