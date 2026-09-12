#include <zima/drawing_render/image_export.hpp>
#include <zima/drawing_render/sheet_renderer.hpp>
#include <zima/workspace/export_operations.hpp>
#include <zima/document/file_path.hpp>
#include "sheet_export_context.hpp"
#include <QFile>
#include <QGuiApplication>
#include <QImageWriter>
#include <algorithm>
#include <cmath>
#include <cctype>
namespace zima::drawing_render {
namespace {
using workspace::ExportOperationError;
QByteArray image_format(const std::filesystem::path& path) {
    auto extension=path.extension().string();
    std::ranges::transform(extension,extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension==".png")return "PNG";
    if(extension==".jpg"||extension==".jpeg")return "JPG";
    throw ExportOperationError("unsupported_format","Image export requires a PNG or JPEG destination.");
}
void invalid(){throw ExportOperationError("invalid_arguments","Invalid image export settings.");}
void encode(const QImage& image,const std::filesystem::path& staged,const QByteArray& format,int quality) {
    QFile file(QString::fromStdString(document::path_to_utf8(staged)));
    if(!file.open(QIODevice::WriteOnly))throw std::runtime_error(file.errorString().toStdString());
    QImageWriter writer(&file,format);
    if(format=="JPG")writer.setQuality(quality); // PNG keeps its default lossless compression.
    if(!writer.write(image))throw std::runtime_error(writer.errorString().toStdString());
    if(!file.flush())throw std::runtime_error(file.errorString().toStdString());
    file.close();
}
}
std::uint64_t write_image(const QImage& image,const std::filesystem::path& destination,bool overwrite,int quality) {
    const auto format=image_format(destination);
    if(quality<0||quality>100)invalid();
    if(image.isNull())throw ExportOperationError("empty_image","Cannot export an empty image.");
    return workspace::write_export_file(destination,overwrite,[&](const auto& staged){encode(image,staged,format,quality);});
}
ImageExportResult export_image(const drawing::DrawingDocument& doc,const std::string& sheet_id,
    const std::filesystem::path& destination,const std::filesystem::path& document_path,
    const workspace::Workspace* live,const ImageExportSettings& settings,bool overwrite) {
    const auto format=image_format(destination);
    const auto* sheet=doc.find_sheet(sheet_id);
    if(!sheet)throw ExportOperationError("sheet_not_found","The drawing sheet does not exist.");
    if(!std::isfinite(settings.dpi)||settings.dpi<1||settings.dpi>2400||settings.quality<0||settings.quality>100)invalid();
    const auto crop=settings.crop_mm.value_or(QRectF(0,0,sheet->width_mm(),sheet->height_mm()));
    if(!std::isfinite(crop.x())||!std::isfinite(crop.y())||!std::isfinite(crop.width())||!std::isfinite(crop.height())||
       crop.x()<0||crop.y()<0||crop.width()<=0||crop.height()<=0||crop.right()>sheet->width_mm()+1e-9||crop.bottom()>sheet->height_mm()+1e-9)invalid();
    const auto zoom=settings.dpi/25.4;
    const auto width=std::ceil(crop.width()*zoom),height=std::ceil(crop.height()*zoom);
    if(width>16384||height>16384||width*height>64*1024*1024)
        throw ExportOperationError("image_too_large","Requested image exceeds the raster size limit.");
    if(!qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        throw ExportOperationError("graphics_unavailable","Image export requires an initialized graphics runtime.");
    ImageExportResult result{0,static_cast<int>(width),static_cast<int>(height),settings.dpi};
    result.bytes=workspace::write_export_file(destination,overwrite,[&](const auto& staged) {
        QImage image(result.width_px,result.height_px,QImage::Format_RGB32);
        if(image.isNull())throw ExportOperationError("image_allocation_failed","Cannot allocate the requested image.");
        image.fill(Qt::white);image.setDotsPerMeterX(qRound(settings.dpi/.0254));image.setDotsPerMeterY(qRound(settings.dpi/.0254));
        SheetRenderer output;output.set_render_sheet(sheet);
        output.set_render_context(sheet_export_context(doc,static_cast<std::size_t>(sheet-doc.sheets.data()),document_path,live));
        QPainter painter(&image);output.paint_sheet(painter,zoom,{-crop.x()*zoom,-crop.y()*zoom},true);
        if(!painter.end())throw std::runtime_error("Cannot finish image output");
        encode(image,staged,format,settings.quality);
    });
    return result;
}
}
