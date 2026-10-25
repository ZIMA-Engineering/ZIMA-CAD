#pragma once

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QString>

namespace zima::app {
// Shared by the File Manager and the ordinary Qt file choosers. Labels affect
// presentation only; filesystem paths and user-authored filenames stay intact.
inline QString localized_directory_label(const QString& path, const QString& language,
                                         const QString& fallback = {}) {
    QSettings metadata(QDir(path).filePath("0000-index/metadata.ini"), QSettings::IniFormat);
    for (const auto& key : {QStringLiteral("Directory/Label/") + language,
                           QStringLiteral("Directory/Label")}) {
        const QString value = metadata.value(key).toStringList().join(", ").trimmed();
        if (!value.isEmpty()) return value;
        // Early CAD metadata used literal escaped keys inside [Directory].
        if(key.startsWith("Directory/Label/")){
            const QString old_value=metadata.value("Directory/Label\\"+language).toString().trimmed();
            if(!old_value.isEmpty())return old_value;
        }
    }
    return fallback.isEmpty()?QFileInfo(path).fileName():fallback;
}
inline QString document_thumbnail_path(const QString& path) {
    const QFileInfo file(path);
    return QDir(file.absolutePath()).filePath("0000-index/thumbnails/" +
        file.fileName() + ".png");
}
} // namespace zima::app
