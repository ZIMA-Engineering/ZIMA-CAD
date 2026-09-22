#include <zima/symbols/definition.hpp>
#include <QGuiApplication>
#include <QImage>
#include <QFontDatabase>
#include <QPainter>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    try {
        check(argc==3||argc==4,"Usage: zima_symbol_library_tool [--generate] file.symz preview.png");
        const bool generate=argc==4&&std::string(argv[1])=="--generate";
        check(argc!=4||generate,"Unknown option");
        const auto path=std::filesystem::u8path(argv[generate?2:1]);
        if(generate)zima::symbols::projection_method().save(path);
        const auto d=zima::symbols::Definition::load(path);
        const auto font_path=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/fonts/osifont-lgpl3fe.ttf";
        const int font_id=QFontDatabase::addApplicationFont(QString::fromStdString(font_path.generic_string()));
        check(font_id>=0,"Cannot load preview font");
        check(d.default_variant=="first_angle"&&d.variant_source=="drawing.projection_method","Projection binding declaration lost");
        check(d.insertion_point==std::array<double,2>{0,0},"Insertion origin changed");
        QTemporaryDir temp;
        const auto roundtrip=std::filesystem::u8path(temp.filePath("roundtrip.symz").toStdString());
        d.save(roundtrip);
        check(zima::symbols::Definition::load(roundtrip).serialized()==d.serialized(),"Symbol round trip changed definition");
        auto invalid=d;invalid.variants.begin()->second.sketches.push_back("missing-sketch");
        bool rejected=false;try{invalid.validate();}catch(const std::exception&){rejected=true;}
        check(rejected,"Dangling symbol group reference accepted");
        auto invalid_version=d.serialized();const auto at=invalid_version.find("\"version\": 2");
        check(at!=std::string::npos,"Missing version");invalid_version.replace(at,12,"\"version\": 9");
        rejected=false;try{static_cast<void>(zima::symbols::Definition::from_serialized(invalid_version));}catch(const std::exception&){rejected=true;}
        check(rejected,"Unsupported symbol format accepted");
        QImage image(960,320,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);
        QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);
        int column=0;
        for(const auto* variant:{"first_angle","third_angle"}) {
            const auto evaluated=d.evaluate(variant);check(evaluated.size()==1,"Projection must select one sketch");
            const auto& sketch=evaluated.front();
            const auto point=[&](const std::string& id)->const zima::sketcher::SketchPoint& {
                const auto it=std::ranges::find(sketch.points,id,&zima::sketcher::SketchPoint::id);
                check(it!=sketch.points.end(),"Missing point");return *it;
            };
            const double origin_x=240+480*column;
            const auto screen=[&](const auto& p){return QPointF(origin_x+p.x*22,175-p.y*22);};
            double circle_x=0, cone_left=100;
            std::string center_id;int circles=0,axes=0,edges=0;
            for(const auto& c:sketch.circles) {
                if(center_id.empty())center_id=c.center_point_id;
                check(center_id==c.center_point_id&&point(center_id).y==0,"Projection circles are not concentric on the shared axis");
                circle_x=point(center_id).x;++circles;
                painter.setPen(QPen(Qt::black,.35*22));painter.drawEllipse(screen(point(center_id)),c.radius*22,c.radius*22);
            }
            for(const auto& s:sketch.segments) {
                const auto& a=point(s.first_point_id);const auto& b=point(s.second_point_id);
                if(s.construction) {
                    ++axes;check(!s.centerline,"Symbol axes must retain finite extents");
                    check((a.y==0&&b.y==0)||(a.x==circle_x&&b.x==circle_x),"Projection axis misses the center");
                } else {++edges;cone_left=std::min({cone_left,a.x,b.x});}
                QPen pen(Qt::black,(s.construction?.18:.35)*22);if(s.construction)pen.setStyle(Qt::DashDotLine);
                painter.setPen(pen);painter.drawLine(screen(a),screen(b));
            }
            check(circles==2&&axes==2&&edges==4,"Projection geometry roles differ");
            check((std::string(variant)=="first_angle")== (circle_x>cone_left),"Projection variants are reversed");
            painter.setPen(Qt::black);QFont font(QFontDatabase::applicationFontFamilies(font_id).front());font.setPixelSize(24);painter.setFont(font);
            painter.drawText(QRectF(column*480,25,480,40),Qt::AlignCenter,column==0?"First angle":"Third angle");++column;
        }
        painter.end();check(image.save(QString::fromUtf8(argv[generate?3:2])),"Cannot save symbol preview");
        std::cout<<"Symbol geometry, variants, native round trip and validation passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
