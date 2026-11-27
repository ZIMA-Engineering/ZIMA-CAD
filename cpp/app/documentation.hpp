#pragma once

#include "application_settings.hpp"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

namespace zima::app::documentation {

// The File Manager source is the one configurable location for both readers.
inline QString root(const ApplicationSettings& settings) {
    for (const auto& source : settings.file_manager.sources)
        if (source.id == "documentation") return source.resolved_path;
    return {};
}

inline QUrl guide_url(const ApplicationSettings& settings, const QString& command = {}) {
    const auto directory = root(settings);
    if (directory.isEmpty()) return {};
    QString guide;
    if (!command.isEmpty()) {
        QSettings metadata(QDir(directory).filePath("0000-index/metadata.ini"), QSettings::IniFormat);
        guide = metadata.value("Commands/" + command).toString();
        static const QRegularExpression safe_id(QStringLiteral("^[a-z0-9]+(?:-[a-z0-9]+)*$"));
        if (!safe_id.match(guide).hasMatch()) return {};
    }
    const QDir index(QDir(directory).filePath(guide.isEmpty() ? "0000-index" : guide + "/0000-index"));
    const auto language = settings.language.left(2).toLower();
    for (const auto& candidate : QStringList{language, QStringLiteral("en")}) {
        if (candidate.size() != 2 || !candidate[0].isLetter() || !candidate[1].isLetter()) continue;
        const auto file = index.filePath("index_" + candidate + ".html");
        if (QFileInfo(file).isFile()) return QUrl::fromLocalFile(QFileInfo(file).absoluteFilePath());
    }
    return {};
}

} // namespace zima::app::documentation
