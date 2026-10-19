#include <zima/drawing_render/sheet_renderer.hpp>
#include <QGuiApplication>
#include <QFontMetricsF>
#include <QTextLayout>
#include <QGlyphRun>
#include <QRawFont>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    QGuiApplication application(argc,argv);
    try {
        using namespace zima;
        std::size_t glyphs=0,cases=0;
        for(const auto value:{QStringLiteral("UNRELEASED STUDY - R2 NOT FINISHED"),
                QStringLiteral("abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789"),
                QString::fromUtf8("Příliš žluťoučký kůň - Änderungsmaß - Écrou à oreilles"),
                QString::fromUtf8("Исследование барашковой гайки - Размеры детали")})
        for(double height:{1.8,2.5,3.,5.,10.})for(double width:{.13,.25,.35,.5,.7}) {
            auto doc=drawing::DrawingDocument::create_default();auto& sheet=doc.sheets.front();sheet.thin_line_mm=width;
            drawing::DrawingText text;text.id="coverage-note";text.presentation.text=value.toStdString();text.presentation.font=drawing_render::drawing_font_family().toStdString();
            text.presentation.height=height;text.presentation.position={sheet.width_mm()-20,sheet.height_mm()-20};
            text.presentation.pen=drawing::DrawingPen::Green;text.presentation.flipped=true;text.presentation.vertical_alignment="baseline";
            sheet.texts={text};
            QFont font(QString::fromStdString(text.presentation.font));font.setWeight(QFont::Normal);font.setPixelSize(1000);const QFontMetricsF metrics(font);
            constexpr double zoom=12;const double scale=height*zoom/metrics.capHeight();const auto bounds=metrics.tightBoundingRect(value);
            const int image_width=static_cast<int>(std::ceil(metrics.horizontalAdvance(value)*scale))+60;
            const int image_height=static_cast<int>(std::ceil(metrics.height()*scale))+60;
            QImage image(image_width,image_height,QImage::Format_RGB32);image.fill(Qt::white);
            drawing_render::SheetRenderer renderer;renderer.set_render_sheet(&sheet);
            {QPainter painter(&image);renderer.paint_sheet(painter,zoom,{20-20*zoom,double(image_height-20)-20*zoom},true);}
            QTextLayout layout(value,font);layout.beginLayout();auto line=layout.createLine();line.setLineWidth(1e9);line.setPosition({0,-line.ascent()});layout.endLayout();
            for(const auto& run:layout.glyphRuns()) {
                const auto indexes=run.glyphIndexes();const auto positions=run.positions();const auto raw=run.rawFont();
                for(qsizetype i=0;i<indexes.size();++i) {
                    const auto path=raw.pathForGlyph(indexes[i]);if(path.isEmpty())continue;
                    auto box=path.boundingRect().translated(positions[i]-QPointF(bounds.left(),0));
                    box=QRectF(20+box.left()*scale,image_height-20+box.top()*scale,box.width()*scale,box.height()*scale);
                    const auto pixels=box.toAlignedRect().intersected(image.rect());int ink=0;
                    for(int y=pixels.top();y<=pixels.bottom();++y)for(int x=pixels.left();x<=pixels.right();++x)ink+=image.pixelColor(x,y).red()<192;
                    if(!ink) {
                        const auto output=std::filesystem::path("build/drawing-text-coverage-failed.png");image.save(QString::fromStdString(output.string()));
                        throw std::runtime_error("Drawing text glyph disappeared: glyph="+std::to_string(indexes[i])+" height="+std::to_string(height)+" pen="+std::to_string(width)+" text="+value.toStdString());
                    }
                    ++glyphs;
                }
            }
            ++cases;
        }
        std::cout<<cases<<" weighted drawing text cases, "<<glyphs<<" independently located Latin/Cyrillic glyphs retain visible ink\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
