#pragma once
#include <zima/workspace/drawing_sources.hpp>
namespace zima::drawing_render {
inline drawing::TitleBlockContext sheet_export_context(const drawing::DrawingDocument& doc,
    std::size_t index,const std::filesystem::path& document_path,const workspace::Workspace* live) {
    const auto& sheet=doc.sheets.at(index);
    const auto id=sheet.views.empty()?doc.source_document_id:sheet.views.front().source_document_id;
    auto source=sheet.views.empty()?doc.source_path:sheet.views.front().source_path;
    if(!source.empty()&&source.is_relative()&&!document_path.empty())source=document_path.parent_path()/source;
    auto context=workspace::build_title_block_context_for_source(id,source,live);
    context.sheet_index=static_cast<int>(index);context.sheet_count=static_cast<int>(doc.sheets.size());
    return context;
}
}
