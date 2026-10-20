#include "../common/technical_font.hpp"
#include <QFile>
#include <QGuiApplication>
#include <QPainterPath>
#include <QRawFont>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    QGuiApplication application(argc,argv);
    try {
        const QString relative=QStringLiteral("config/fonts/osifont-lgpl3fe.ttf");
        if(QFile::exists(QStringLiteral(":/zima/fonts/osifont-lgpl3fe.ttf")) ||
                QFile::exists(relative) || QFile::exists(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../"+relative)))
            throw std::runtime_error("Portable font probe must have no embedded, working-directory or development fallback font");
        const auto family=zima::technical_font_family();
        if(family.isEmpty())throw std::runtime_error("Version-owned technical font could not be loaded without repository CWD");
        auto font=zima::technical_font();font.setPixelSize(100);
        const auto raw=QRawFont::fromFont(font);
        const auto glyphs=raw.glyphIndexesForString(QString::fromUtf8("TH09ŽéЯ"));
        if(!raw.isValid() || raw.familyName()!=family || glyphs.size()!=7)
            throw std::runtime_error("Portable technical font did not retain its actual family and glyphs");
        for(auto glyph:glyphs)if(!glyph || raw.pathForGlyph(glyph).isEmpty())
            throw std::runtime_error("Portable technical font produced a missing glyph");
        std::cout<<"Version-owned technical font and Latin/Czech/French/Russian outlines passed: "<<family.toStdString()<<'\n';
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
