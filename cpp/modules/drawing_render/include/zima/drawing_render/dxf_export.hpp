#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <zima/workspace/workspace.hpp>
#include <cstdint>
namespace zima::drawing_render {
// Export one explicitly identified sheet in paper millimetres, without regeneration.
std::uint64_t export_dxf(const drawing::DrawingDocument&,const std::string& sheet,
    const std::filesystem::path& destination,const std::filesystem::path& document_path,
    const workspace::Workspace* live,bool overwrite=false);
}
