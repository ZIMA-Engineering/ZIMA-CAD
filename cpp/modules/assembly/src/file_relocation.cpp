#include <zima/assembly/file_relocation.hpp>
#include <zima/assembly/assembly_document.hpp>
namespace zima::assembly {
namespace {
void rename_snapshot(std::vector<OccurrenceSnapshot>& rows,document::FileRelocationEdits& edits) {
    for(auto& row:rows) {
        if(row.derived_source_id.empty()&&!row.pattern_group)edits.document_name(row.source_document_id,row.name);
        rename_snapshot(row.children,edits);
    }
}
}
void collect_file_relocation_edits(AssemblyDocument& value, document::FileRelocationEdits& edits,
        const std::filesystem::path& owning_file) {
    edits.document_name(value.document_id, value.name);
    for (auto& component : value.components) {
        edits.source_reference(component.source_document_id, component.source_path, owning_file,
            component.derived_copy||component.source_kind==ComponentSourceKind::Pattern?nullptr:&component.name);
        rename_snapshot(component.nested_snapshot,edits);
    }
}
} // namespace zima::assembly
