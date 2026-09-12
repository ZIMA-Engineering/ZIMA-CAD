#include <zima/drawing_render/pdf_export.hpp>
#include <zima/drawing_render/sheet_renderer.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/workspace/export_operations.hpp>
#include <zima/document/file_path.hpp>
#include <QGuiApplication>
#include <QFile>
#include <QPdfWriter>
#include <QPageSize>
#include <algorithm>
#include <cctype>
namespace zima::drawing_render {
std::uint64_t export_pdf(const drawing::DrawingDocument& doc,const std::filesystem::path& destination,
    const std::filesystem::path& document_path,const workspace::Workspace* live,bool overwrite) {
    using workspace::ExportOperationError;
    auto extension=destination.extension().string();std::ranges::transform(extension,extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension!=".pdf")throw ExportOperationError("unsupported_format","The destination extension does not match the requested export format.");
    if(doc.sheets.empty())throw ExportOperationError("empty_drawing","Drawing has no sheets");
    if(!qobject_cast<QGuiApplication*>(QCoreApplication::instance()))throw ExportOperationError("graphics_unavailable","PDF export requires an initialized graphics runtime.");
    return workspace::write_export_file(destination,overwrite,[&](const auto& file_path) {
        QFile file(QString::fromStdString(document::path_to_utf8(file_path)));
        if(!file.open(QIODevice::WriteOnly))throw std::runtime_error(file.errorString().toStdString());
        {
            QPdfWriter writer(&file);writer.setResolution(720);writer.setTitle(QString::fromStdString(doc.name));writer.setCreator("ZIMA-CAD");
            QPainter painter;
            for(std::size_t index=0;index<doc.sheets.size();++index) {
                const auto& sheet=doc.sheets[index];
                writer.setPageSize(QPageSize(QSizeF(sheet.width_mm(),sheet.height_mm()),QPageSize::Millimeter));
                writer.setPageMargins(QMarginsF(0,0,0,0),QPageLayout::Millimeter);
                if(index==0){if(!painter.begin(&writer))throw std::runtime_error("Cannot start PDF output");}
                else if(!writer.newPage())throw std::runtime_error("Cannot add PDF page");
                const auto id=sheet.views.empty()?doc.source_document_id:sheet.views.front().source_document_id;
                auto source=sheet.views.empty()?doc.source_path:sheet.views.front().source_path;
                if(!source.empty()&&source.is_relative()&&!document_path.empty())source=document_path.parent_path()/source;
                auto context=workspace::build_title_block_context_for_source(id,source,live);
                context.sheet_index=static_cast<int>(index);context.sheet_count=static_cast<int>(doc.sheets.size());
                SheetRenderer output;output.set_render_sheet(&sheet);output.set_render_context(std::move(context));
                output.paint_sheet(painter,writer.resolution()/25.4,{},true);
            }
            if(!painter.end())throw std::runtime_error("Cannot finish PDF output");
        }
        if(!file.flush()||file.error()!=QFileDevice::NoError)throw std::runtime_error(file.errorString().toStdString());
        file.close();
    });
}
}
