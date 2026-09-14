#pragma once
#include <zima/document/file_relocation.hpp>
namespace zima::drawing {
class DrawingDocument;
// Append metadata edits only. The caller applies the entire batch after validation.
void collect_file_relocation_edits(DrawingDocument&, document::FileRelocationEdits&,
    const std::filesystem::path& owning_file);
} // namespace zima::drawing
