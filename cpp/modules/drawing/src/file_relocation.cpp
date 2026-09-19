#include <zima/drawing/file_relocation.hpp>
#include <zima/drawing/drawing_document.hpp>
namespace zima::drawing {
void collect_file_relocation_edits(DrawingDocument& value, document::FileRelocationEdits& edits,
        const std::filesystem::path& owning_file) {
    edits.document_name(value.document_id, value.name);
    edits.source_reference(value.source_document_id, value.source_path, owning_file, &value.source_name);
    for(auto& source:value.sources)edits.source_reference(source.document_id,source.source_path,owning_file,&source.name);
    for (auto& sheet : value.sheets) {
        for (auto& view : sheet.views)
            edits.source_reference(view.source_document_id, view.source_path, owning_file);
        for (auto& row : sheet.bom_rows)
            edits.source_reference(row.source_document_id, row.source_path, owning_file, &row.file_stem);
    }
}
} // namespace zima::drawing
