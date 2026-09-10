#pragma once

#include <QFileInfo>
#include <QStringList>

namespace zima::app {

struct StartupArguments {
    QString working_directory;
    QStringList documents;
};

inline StartupArguments parse_startup_arguments(const QStringList& arguments) {
    StartupArguments result;
    bool positional_only = false;
    for (int index = 1; index < arguments.size(); ++index) {
        const auto& argument = arguments.at(index);
        if (!positional_only) {
            if (argument == QStringLiteral("--")) {
                positional_only = true;
                continue;
            }
            if (argument == QStringLiteral("--working-directory") || argument == QStringLiteral("-w")) {
                if (index + 1 < arguments.size())
                    result.working_directory = QFileInfo(arguments.at(++index)).absoluteFilePath();
                continue;
            }
            const QString prefix = QStringLiteral("--working-directory=");
            if (argument.startsWith(prefix)) {
                result.working_directory = QFileInfo(argument.mid(prefix.size())).absoluteFilePath();
                continue;
            }
            if (argument.startsWith('-')) continue;
        }
        const QFileInfo candidate(argument);
        if (candidate.isDir()) {
            result.working_directory = candidate.absoluteFilePath();
        } else if (QStringList{"prtz", "asmz", "drwz", "frmz", "tblz"}
                .contains(candidate.suffix(), Qt::CaseInsensitive)) {
            result.documents.push_back(candidate.absoluteFilePath());
        }
    }
    // An externally opened document chooses its own project/configuration,
    // independently of Explorer's CWD or the desktop launcher's default.
    if (!result.documents.isEmpty())
        result.working_directory = QFileInfo(result.documents.front()).absolutePath();
    return result;
}

} // namespace zima::app