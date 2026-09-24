#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QStringList>

namespace zima {

// One bundled technical font, shared by GUI fallback and all annotation views.
// Executables with resources use the embedded copy; standalone rendering tools
// can use the same repository font without installing it into the operating system.
inline QString technical_font_family() {
    static const QString family = [] {
        const QString relative = QStringLiteral("config/fonts/osifont-lgpl3fe.ttf");
        const QStringList paths{QStringLiteral(":/zima/fonts/osifont-lgpl3fe.ttf"),
            relative, QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../" + relative)};
        for (const auto& path : paths) {
            const int id = QFontDatabase::addApplicationFont(path);
            if (id < 0) continue;
            const auto families = QFontDatabase::applicationFontFamilies(id);
            if (!families.empty()) return families.front();
        }
        qWarning("Cannot load the bundled ISO font.");
        return QString{};
    }();
    return family;
}

inline QFont technical_font() {
    auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    const auto family = technical_font_family();
    if (!family.isEmpty()) font.setFamily(family);
    font.setWeight(QFont::Normal);
    return font;
}

} // namespace zima
