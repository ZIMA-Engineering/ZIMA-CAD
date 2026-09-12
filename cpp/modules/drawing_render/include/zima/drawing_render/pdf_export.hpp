#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <cstdint>
namespace zima::workspace {class Workspace;}
namespace zima::drawing_render {
// Render all saved sheets from ZIMA data. Requires QGuiApplication but never
// constructs a window, calculates geometry, or changes document history.
std::uint64_t export_pdf(const drawing::DrawingDocument&,const std::filesystem::path& destination,
    const std::filesystem::path& document_path,const workspace::Workspace*,bool overwrite=false);
}
