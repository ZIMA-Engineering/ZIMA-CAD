#pragma once
#include "application_settings.hpp"
#include <zima/workspace/native_documents.hpp>
#include <QDir>
#include <QSettings>
#include <algorithm>

namespace zima::app {
struct NewDrawingFormat {
    QString label, path;
    drawing::SheetFormat format{drawing::SheetFormat::A4};
};
struct NewDrawingOptions {
    std::vector<NewDrawingFormat> formats;
    QString title_block;
};
inline NewDrawingOptions new_drawing_options(const ApplicationSettings& settings) {
    NewDrawingOptions result;
    const auto directory=settings.resolved_paths.value("Formats");
    if(directory.isEmpty()||!QDir(directory).exists())return result;
    const QDir folder(directory);
    const QStringList sizes{"A4","A3","A2","A1","A0"};
    for(const auto& file:folder.entryInfoList({"*.frmz"},QDir::Files,QDir::Name)) {
        QSettings frame(file.absoluteFilePath(),QSettings::IniFormat);
        const auto size=frame.value("Format/SheetFormat").toString().toUpper();
        const int index=sizes.indexOf(size);
        if(index<0)continue;
        const auto name=frame.value("Format/Name",file.completeBaseName()).toString();
        result.formats.push_back({size+QStringLiteral(" — ")+name,file.absoluteFilePath(),static_cast<drawing::SheetFormat>(index)});
    }
    std::stable_sort(result.formats.begin(),result.formats.end(),[](const auto& a,const auto& b){return a.format<b.format;});
    // Prefer the company template in the active language, then any matching
    // locale supplied by the configured library, then the base company block.
    const auto preferred=folder.filePath("ZE-TITLE-BLOCK-"+settings.language.toUpper()+".tblz");
    if(QFileInfo(preferred).isFile())result.title_block=preferred;
    else {
        for(const auto& file:folder.entryInfoList({"*.tblz"},QDir::Files,QDir::Name)) {
            QSettings block(file.absoluteFilePath(),QSettings::IniFormat);
            if(block.value("TitleBlock/Locale").toString().compare(settings.language,Qt::CaseInsensitive)==0) {
                result.title_block=file.absoluteFilePath();break;
            }
        }
        const auto base=folder.filePath("ZE-TITLE-BLOCK-CS.tblz");
        if(result.title_block.isEmpty()&&QFileInfo(base).isFile())result.title_block=base;
    }
    return result;
}
inline void configure_new_drawing(workspace::NativeTemplateSettings& templates,
        const NewDrawingOptions& options,const QString& selected_frame={}) {
    templates.drawing_format=drawing::SheetFormat::A4;
    templates.drawing_frame_template.clear();
    templates.drawing_title_block_template=std::filesystem::u8path(options.title_block.toStdString());
    for(const auto& format:options.formats)
        if(selected_frame.isEmpty()?format.format==drawing::SheetFormat::A4:format.path==selected_frame) {
            templates.drawing_format=format.format;
            templates.drawing_frame_template=std::filesystem::u8path(format.path.toStdString());
            break;
        }
}
} // namespace zima::app
