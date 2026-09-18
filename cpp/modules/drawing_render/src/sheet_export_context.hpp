#pragma once
#include <zima/workspace/drawing_sources.hpp>
namespace zima::drawing_render {
inline drawing::TitleBlockContext sheet_export_context(const drawing::DrawingDocument& doc,
    std::size_t index,const std::filesystem::path& document_path,const workspace::Workspace* live) {
    const auto& sheet=doc.sheets.at(index);
    const auto id=sheet.bom_source_document_id.empty()?doc.source_document_id:sheet.bom_source_document_id;
    auto source=doc.source_path;
    if(source.empty()&&!sheet.views.empty())source=sheet.views.front().source_path;
    if(!source.empty()&&source.is_relative()&&!document_path.empty())source=document_path.parent_path()/source;
    auto context=workspace::build_title_block_context_for_source(id,source,live);
    // A selected sheet variant controls the title block, but export must still
    // reject a sheet containing an unavailable independent view source.
    for(const auto& view:sheet.views) {
        auto view_source=view.source_path;
        if(!view_source.empty()&&view_source.is_relative()&&!document_path.empty())
            view_source=document_path.parent_path()/view_source;
        if(view.source_document_id!=id||view_source.lexically_normal()!=source.lexically_normal())
            static_cast<void>(workspace::build_title_block_context_for_source(
                view.source_document_id,view_source,live));
    }
    context.sheet_index=static_cast<int>(index);context.sheet_count=static_cast<int>(doc.sheets.size());
    return context;
}
}
