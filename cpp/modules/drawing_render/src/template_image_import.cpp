#include <zima/drawing_render/template_image_import.hpp>
#include <zima/kernel/stable_id.hpp>
#include <QBuffer>
#include <QImageReader>
#include <QSvgRenderer>
#include <QFile>
#include <QFileInfo>
#include <stdexcept>
namespace zima::drawing_render {
zima::sketcher::TemplateImage read_template_image(const std::filesystem::path& source_path) {
    const auto path=QString::fromStdU16String(source_path.u16string());
    if(QFileInfo(path).suffix().compare("svg",Qt::CaseInsensitive)==0) {
        QFile source(path);if(!source.open(QIODevice::ReadOnly))throw std::runtime_error(QObject::tr("SVG nelze otevřít.").toStdString());
        if(source.size()>24*1024*1024)throw std::runtime_error(QObject::tr("SVG je příliš velké (nejvýše 24 MB).").toStdString());
        const auto bytes=source.readAll();QSvgRenderer renderer(bytes);
        if(!renderer.isValid()||renderer.viewBoxF().isEmpty())throw std::runtime_error(QObject::tr("Soubor neobsahuje platný obrázek SVG.").toStdString());
        zima::sketcher::TemplateImage result;result.id=zima::kernel::make_stable_id();result.name=QFileInfo(path).fileName().toStdString();
        result.format="svg";result.data_base64=bytes.toBase64().toStdString();
        const auto size=renderer.viewBoxF().size();result.pixel_width=size.width();result.pixel_height=size.height();
        result.width=30;result.height=30*size.height()/size.width();result.validate();return result;
    }
    QImageReader reader(path);reader.setAutoTransform(true);
    const auto size=reader.size();
    if(size.isValid() && static_cast<double>(size.width())*size.height()>32'000'000)
        throw std::runtime_error(QObject::tr("Obrázek je příliš velký (nejvýše 32 megapixelů).").toStdString());
    const auto image=reader.read();
    if(image.isNull())throw std::runtime_error(QObject::tr("Obrázek nelze načíst: %1").arg(reader.errorString()).toStdString());
    QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);
    if(!image.save(&buffer,"PNG"))throw std::runtime_error(QObject::tr("Obrázek nelze uložit do razítka.").toStdString());
    zima::sketcher::TemplateImage result;
    result.id=zima::kernel::make_stable_id();result.name=QFileInfo(path).fileName().toStdString();
    result.data_base64=bytes.toBase64().toStdString();result.pixel_width=image.width();result.pixel_height=image.height();
    result.width=30;result.height=result.width*image.height()/image.width();result.validate();return result;
}
}
