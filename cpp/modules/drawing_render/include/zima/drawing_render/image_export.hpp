#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <zima/workspace/workspace.hpp>
#include <QImage>
#include <QRectF>
#include <cstdint>
#include <optional>
namespace zima::drawing_render {
struct ImageExportSettings {
    double dpi{150};
    // Paper millimetres from its top-left corner. Omission means the full sheet.
    std::optional<QRectF> crop_mm;
    int quality{95};
};
struct ImageExportResult {std::uint64_t bytes{};int width_px{},height_px{};double dpi{};};
// Shared atomic encoder for GUI captures and native Drawing raster output.
std::uint64_t write_image(const QImage&,const std::filesystem::path&,bool overwrite=false,int quality=95);
ImageExportResult export_image(const drawing::DrawingDocument&,const std::string& sheet,
    const std::filesystem::path& destination,const std::filesystem::path& document_path,
    const workspace::Workspace* live,const ImageExportSettings& = {},bool overwrite=false);
}
