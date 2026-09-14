#include <zima/assembly/file_relocation.hpp>
#include <zima/assembly/assembly_document.hpp>
namespace zima::assembly {
void collect_file_relocation_edits(AssemblyDocument& value, document::FileRelocationEdits& edits,
        const std::filesystem::path& owning_file) {
    edits.document_name(value.document_id, value.name);
    for (auto& component : value.components)
        edits.source_reference(component.source_document_id, component.source_path, owning_file);
}
} // namespace zima::assembly
