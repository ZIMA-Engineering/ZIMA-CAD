#include <zima/drawing_render/dxf_export.hpp>
#include <zima/drawing_render/sheet_renderer.hpp>
#include <zima/workspace/export_operations.hpp>
#include <zima/document/file_path.hpp>
#include "dxf_device.hpp"
#include "sheet_export_context.hpp"
#include <QGuiApplication>
#include <QFile>
#include <algorithm>
#include <cctype>
namespace zima::drawing_render {
std::uint64_t export_dxf(const drawing::DrawingDocument& doc,const std::string& sheet_id,
    const std::filesystem::path& destination,const std::filesystem::path& document_path,
    const workspace::Workspace* live,bool overwrite) {
    using workspace::ExportOperationError;
    auto extension=destination.extension().string();std::ranges::transform(extension,extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension!=".dxf")throw ExportOperationError("unsupported_format","The destination extension does not match the requested export format.");
    const auto* sheet=doc.find_sheet(sheet_id);
    if(!sheet)throw ExportOperationError("sheet_not_found","The drawing sheet does not exist.");
    if(!qobject_cast<QGuiApplication*>(QCoreApplication::instance()))throw ExportOperationError("graphics_unavailable","Drawing DXF export requires an initialized graphics runtime.");
    return workspace::write_export_file(destination,overwrite,[&](const auto& staged) {
        SheetRenderer output;output.set_render_sheet(sheet);
        output.set_render_context(sheet_export_context(doc,static_cast<std::size_t>(sheet-doc.sheets.data()),document_path,live));
        DrawingDxfDevice device(sheet->width_mm(),sheet->height_mm());
        QPainter painter;
        if(!painter.begin(&device))throw std::runtime_error("Cannot start DXF output");
        output.paint_sheet(painter,1,{},true);
        if(!painter.end())throw std::runtime_error("Cannot finish DXF output");
        const auto data=device.data();
        QFile file(QString::fromStdString(document::path_to_utf8(staged)));
        if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()||!file.flush())throw std::runtime_error("Cannot write DXF output");
        file.close();
    });
}
}
