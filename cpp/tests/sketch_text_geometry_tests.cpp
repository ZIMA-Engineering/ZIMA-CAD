#include <zima/sketcher/text_geometry.hpp>
#include <zima/document/part_document.hpp>
#include <zima/kernel/geometry_kernel.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <QByteArray>
#include <QFile>
#include <chrono>
#include <span>
#include <array>
namespace zima::sketcher { std::span<const unsigned char> bundled_sketch_font(); }
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace zima;
namespace {
void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
using Bounds=std::array<double,4>;
Bounds bounds(const sketcher::SketchText& text) {
    const double inf=std::numeric_limits<double>::infinity();Bounds b{inf,inf,-inf,-inf};
    for(const auto& contour:text.contours)for(const auto& p:contour){b[0]=std::min(b[0],p[0]);b[1]=std::min(b[1],p[1]);b[2]=std::max(b[2],p[0]);b[3]=std::max(b[3],p[1]);}return b;
}
double area(const sketcher::SketchText& text) {
    double sum=0;for(const auto& c:text.contours)for(std::size_t i=0;i<c.size();++i){const auto& a=c[i];const auto& b=c[(i+1)%c.size()];sum+=(a[0]*b[1]-b[0]*a[1])*.5;}return std::abs(sum);
}
void verify() {
    auto text=sketcher::Sketch::create_text();text.value="H";text.height=10;sketcher::rebuild_text_contours(text);const auto normal=bounds(text);
    // Independently read from OSIFONT: OS/2 cap height 1515, H ink box 1510 units.
    constexpr double h_height=10.*1510/1515;
    std::cout<<"OSIFONT H bounds: "<<normal[0]<<", "<<normal[1]<<", "<<normal[2]<<", "<<normal[3]<<"\n";
    require(std::abs(normal[0])<1e-10 && std::abs(normal[3])<1e-10 && std::abs(normal[1]+h_height)<1e-8 && normal[2]>1,"Bundled font cap height or baseline is wrong");
    const auto original=text;sketcher::rebuild_text_contours(text);require(text==original,"Font outline generation is not deterministic");
    for(const auto alignment:{sketcher::TextHorizontalAlignment::Left,sketcher::TextHorizontalAlignment::Center,sketcher::TextHorizontalAlignment::Right}) {
        text=original;text.horizontal=alignment;text.anchor_x=30;sketcher::rebuild_text_contours(text);const auto b=bounds(text);
        const auto anchor=alignment==sketcher::TextHorizontalAlignment::Left?b[0]:alignment==sketcher::TextHorizontalAlignment::Center?(b[0]+b[2])*.5:b[2];require(std::abs(anchor-30)<1e-8,"Text horizontal anchor drifted");
    }
    for(const auto alignment:{sketcher::TextVerticalAlignment::Bottom,sketcher::TextVerticalAlignment::Middle,sketcher::TextVerticalAlignment::Top}) {
        text=original;text.vertical=alignment;text.anchor_y=20;sketcher::rebuild_text_contours(text);const auto b=bounds(text);
        const auto anchor=alignment==sketcher::TextVerticalAlignment::Bottom?b[3]:alignment==sketcher::TextVerticalAlignment::Middle?(b[1]+b[3])*.5:b[1];require(std::abs(anchor-20)<1e-8,"Text vertical anchor drifted");
    }
    text=original;text.flipped=true;sketcher::rebuild_text_contours(text);auto b=bounds(text);require(std::abs(b[0]+normal[2])<1e-8 && std::abs(area(text)-area(original))<1e-8,"Text flip changed its area or scale");
    text=original;text.angle_degrees=90;sketcher::rebuild_text_contours(text);b=bounds(text);require(std::abs((b[2]-b[0])-h_height)<1e-8 && std::abs((b[3]-b[1])-normal[2])<1e-8,"Text rotation changed its dimensions");
    text=original;sketcher::rebuild_text_contours(text,true);b=bounds(text);require(std::abs(b[1])<1e-8 && std::abs(b[3]-h_height)<1e-8,"Template Y-up reflection is incorrect");
    for(const auto& value:{std::string("Žluťoučký kůň"),std::string("Чертёж"),std::string("Řez Ø10"),std::string("H\nH"),std::string("H\tH")}) {
        text=original;text.value=value;sketcher::rebuild_text_contours(text);require(!text.contours.empty() && area(text)>0,"Unicode or multiline text has no geometry");
        if(value=="H\nH")require(bounds(text)[3]-bounds(text)[1]>20,"Multiline baseline spacing collapsed");
    }
    text=original;text.value="Č";sketcher::rebuild_text_contours(text);const auto composed=text.contours;
    text.value="C\xCC\x8C";sketcher::rebuild_text_contours(text);require(text.contours==composed,"Combining accent did not shape as its precomposed glyph");
    for(const auto& value:std::vector<std::string>{"","   ",std::string("\xc0\xaf"),std::string("\xf4\x8f\xbf\xbf"),std::string(4097,'H')}) {
        text=original;text.value=value;const auto before=text;bool rejected=false;try{sketcher::rebuild_text_contours(text);}catch(const std::exception&){rejected=true;}require(rejected && text==before,"Invalid text partly changed its contours");
    }
    text=original;text.value="";bool localized_error=false;try{sketcher::rebuild_text_contours(text);}catch(const std::exception& e){localized_error=std::string(e.what())=="Text nevytváří žádný platný obrys.";}require(localized_error,"Native text error lost UTF-8 encoding");
    text=original;text.height=-1;const auto invalid=text;bool rejected=false;try{sketcher::rebuild_text_contours(text);}catch(const std::exception&){rejected=true;}require(rejected && text==invalid,"Invalid height changed text");
    text=original;text.value="0";sketcher::rebuild_text_contours(text);require(text.contours.size()>=2,"Closed glyph lost its hole");
    std::size_t points=0;for(const auto& c:text.contours)points+=c.size();std::cout<<"Zero outline vertices: "<<points<<"\n";require(points<220,"Text outline is unnecessarily dense for modeling");
    auto sketch=sketcher::Sketch::create_default();sketch.add_text(text);auto doc=document::PartDocument::create_default();doc.sketches.push_back(sketch);
    auto extrusion=document::PartDocument::create_extrusion_container(sketch.id);extrusion.extrusion.height=2;doc.history.push_back(extrusion);
    kernel::OcctKernel kernel;const auto body=kernel.evaluate_history(doc.kernel_operations());require(!body.empty() && std::abs(body.back().volume-2*area(text))<1e-6,"Text extrusion changed glyph area or filled its hole");
    const auto restored=sketcher::Sketch::from_serialized(sketch.serialized());require(restored.texts==sketch.texts,"Native text persistence changed outlines");
    const auto mesh_bounds=[](const kernel::ViewerMesh& mesh){
        const auto inf=std::numeric_limits<double>::infinity();std::array<double,6> result{inf,inf,inf,-inf,-inf,-inf};
        for(const auto& p:mesh.vertices)for(std::size_t axis=0;axis<3;++axis){const auto value=axis==0?p.x:axis==1?p.y:p.z;result[axis]=std::min(result[axis],value);result[axis+3]=std::max(result[axis+3],value);}return result;
    };
    auto previous_body_bounds=mesh_bounds(body.back().mesh);
    auto moving=sketch;const auto grip=moving.texts.front().anchor_point_id;
    require(moving.find_point(grip)&&moving.find_point(grip)->construction,"Ordinary text lacks its native auxiliary anchor");
    for(const auto target:{std::array{20.,30.},std::array{-15.,22.},std::array{35.,45.}}) {
        const auto previous=moving.texts.front();const auto dx=target[0]-previous.anchor_x,dy=target[1]-previous.anchor_y;
        require(moving.move_point(grip,target[0],target[1]),"Ordinary text anchor cannot move");
        const auto& moved=moving.texts.front();require(moved.anchor_point_id==grip,"Text drag replaced its anchor identity");
        for(std::size_t i=0;i<previous.contours.size();++i)for(std::size_t j=0;j<previous.contours[i].size();++j)
            require(std::abs(moved.contours[i][j][0]-previous.contours[i][j][0]-dx)<1e-8&&std::abs(moved.contours[i][j][1]-previous.contours[i][j][1]-dy)<1e-8,"Text drag distorted native glyph contours");
        doc.sketches.front()=moving;const auto translated=kernel.evaluate_history(doc.kernel_operations());
        require(!translated.empty()&&std::abs(translated.back().volume-body.back().volume)<1e-6,"Moving text changed its dependent extrusion volume or glyph hole");
        const auto translated_bounds=mesh_bounds(translated.back().mesh);
        for(std::size_t axis=0;axis<3;++axis)for(std::size_t end=0;end<2;++end)
            require(std::abs(translated_bounds[axis+3*end]-previous_body_bounds[axis+3*end]-(axis==0?dx:axis==1?dy:0.))<1e-6,"Dependent text extrusion did not follow its anchor translation");
        previous_body_bounds=translated_bounds;
    }
    auto x=moving.create_axis_dimension(grip,"sketch_axis:y"),y=moving.create_axis_dimension(grip,"sketch_axis:x");x.locked=y.locked=true;
    moving.apply_dimension(x);moving.apply_dimension(y);auto previous=moving.serialized();
    require(!moving.move_point(grip,100,100)&&moving.serialized()==previous,"Fully constrained text escaped its equations");
    auto properties=moving.texts.front();properties.anchor_x+=10;sketcher::rebuild_text_contours(properties);
    rejected=false;try{moving.update_text(properties);}catch(const std::exception&){rejected=true;}
    require(rejected&&moving.serialized()==previous,"Conflicting text Properties partly changed constrained geometry");
    properties=moving.texts.front();properties.value="Žluťoučký\nЧертёж";properties.height=3;properties.angle_degrees=37;
    sketcher::rebuild_text_contours(properties);moving.update_text(properties);
    require(moving.texts.front().anchor_point_id==grip&&moving.find_point(grip)->x==35&&moving.find_point(grip)->y==45,"Text Properties replaced or moved a constrained anchor");
    previous=moving.serialized();moving.update_text(moving.texts.front());require(moving.serialized()==previous,"Unchanged text Properties changed its native data");
    auto optional=sketcher::Sketch::create_default();auto unattached=text;unattached.anchor_point_id.clear();optional.texts.push_back(unattached);optional.validate();
    optional=sketcher::Sketch::from_serialized(optional.serialized());previous=optional.serialized();optional.update_text(optional.texts.front());
    require(optional.serialized()==previous&&optional.points.empty(),"Unchanged existing text edit created geometry");
    unattached.value="Edited";sketcher::rebuild_text_contours(unattached);optional.update_text(unattached);
    require(optional.find_point(optional.texts.front().anchor_point_id),"An explicit text edit did not create its native anchor");
}
}
void verify_embedded_font(std::span<const unsigned char> bytes=sketcher::bundled_sketch_font()) {
    sketcher::SketchText text;text.id="embedded-text";text.font="Embedded osifont";text.value="OB8";text.height=10;
    text.embedded_font=sketcher::EmbeddedFont(QByteArray(reinterpret_cast<const char*>(bytes.data()),static_cast<int>(bytes.size())).toBase64().toStdString());
    sketcher::rebuild_text_contours(text);
    const auto curves=sketcher::exact_text_curves(text);
    require(!curves.empty(),"Embedded font lost exact outlines");
    // Five-point Gauss integration is exact for the degree-five Green integrand
    // of each cubic polynomial Bezier span, independent of display tessellation.
    double integral=0;
    constexpr std::array nodes{-.906179845938664,-.538469310105683,0.,.538469310105683,.906179845938664};
    constexpr std::array weights{.236926885056189,.478628670499366,.568888888888889,.478628670499366,.236926885056189};
    for(const auto& curve:curves)for(std::size_t j=0;j+3<curve.poles.size();j+=3)for(std::size_t n=0;n<nodes.size();++n) {
        const double t=(nodes[n]+1)/2,u=1-t;const auto a=curve.poles[j],b=curve.poles[j+1],c=curve.poles[j+2],d=curve.poles[j+3];
        const double x=u*u*u*a.x+3*u*u*t*b.x+3*u*t*t*c.x+t*t*t*d.x,y=u*u*u*a.y+3*u*u*t*b.y+3*u*t*t*c.y+t*t*t*d.y;
        const double dx=3*(u*u*(b.x-a.x)+2*u*t*(c.x-b.x)+t*t*(d.x-c.x)),dy=3*(u*u*(b.y-a.y)+2*u*t*(c.y-b.y)+t*t*(d.y-c.y));
        integral+=weights[n]*(x*dy-y*dx)/4;
    }
    const double exact_area=std::abs(integral);require(exact_area>0,"Font area is empty");
    auto sketch=sketcher::Sketch::create_default();sketch.add_text(text);
    auto doc=document::PartDocument::create_default();doc.sketches.push_back(sketch);
    auto feature=document::PartDocument::create_extrusion_container(sketch.id);doc.history.push_back(feature);
    kernel::OcctKernel kernel;
    for(const auto direction:{document::ExtrusionDirection::Forward,document::ExtrusionDirection::Reverse,document::ExtrusionDirection::Symmetric})for(double h:{2.,7.,3.}) {
        auto& ext=doc.history[0].extrusion;ext.height=h;ext.direction=direction;
        const auto begin=std::chrono::steady_clock::now();const auto result=kernel.evaluate_history(doc.kernel_operations());
        require(!result.empty()&&result.back().calculation_errors.empty(),"Exact font extrusion failed");
        require(std::abs(result.back().volume-exact_area*h)<1e-5,"Exact font extrusion disagrees with analytic Bezier area");
        double low=1e100,high=-1e100;for(const auto& p:result.back().mesh.vertices){low=std::min(low,p.z);high=std::max(high,p.z);}
        require(std::abs(low-(direction==document::ExtrusionDirection::Reverse?-h:direction==document::ExtrusionDirection::Symmetric?-h/2:0))<1e-6&&
            std::abs(high-(direction==document::ExtrusionDirection::Reverse?0:direction==document::ExtrusionDirection::Symmetric?h/2:h))<1e-6,"Cached font extrusion shifted symmetric bounds");
        std::cout<<"Exact font extrusion h="<<h<<" ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<'\n';
    }
    // Exercise the retained font through the ordinary native anchor solver.
    const auto anchor=sketch.texts.front().anchor_point_id;
    auto x=sketch.create_axis_dimension(anchor,"sketch_axis:y");x.locked=true;sketch.apply_dimension(x);
    for(const auto target: {std::array{18.,12.},std::array{-9.,-7.},std::array{21.,31.}}) {
        const auto old=sketch.texts.front();const auto exact_before=sketcher::exact_text_curves(old);
        require(sketch.move_point(anchor,target[0],target[1]),"Embedded font anchor cannot move along a free direction");
        const auto& moved=sketch.texts.front();
        require(moved.anchor_point_id==anchor&&std::abs(moved.anchor_x-old.anchor_x)<1e-8&&std::abs(moved.anchor_y-target[1])<1e-8,"Embedded font drag escaped its locked coordinate");
        const auto exact_after=sketcher::exact_text_curves(moved);require(exact_after.size()==exact_before.size(),"Font drag changed contour count");
        for(std::size_t i=0;i<exact_after.size();++i)for(std::size_t j=0;j<exact_after[i].poles.size();++j) {
            const auto a=exact_before[i].poles[j],b=exact_after[i].poles[j];
            require(std::abs(b.x-a.x)<1e-8&&std::abs(b.y-a.y-(moved.anchor_y-old.anchor_y))<1e-8,"Font drag changed an exact Bezier pole");
        }
    }
    auto y=sketch.create_axis_dimension(anchor,"sketch_axis:x");y.locked=true;sketch.apply_dimension(y);
    const auto fixed=sketch.serialized();require(!sketch.move_point(anchor,50,80)&&sketch.serialized()==fixed,"Fully constrained embedded text moved");
    auto edited=sketch.texts.front();edited.value="H8";edited.height=7;edited.angle_degrees=35;sketcher::rebuild_text_contours(edited);sketch.update_text(edited);
    require(sketch.texts.front().anchor_point_id==anchor&&sketch.texts.front().embedded_font==text.embedded_font,"Editing retained text replaced its point or font");
    text=sketch.texts.front();
    const auto saved=sketch.serialized();const auto restored=sketcher::Sketch::from_serialized(saved);
    require(restored.texts[0].embedded_font==text.embedded_font&&restored.texts[0].value==text.value,"Native persistence lost font bytes or editable text");
    require(restored.bsplines.empty()&&restored.segments.empty(),"Extrusion exploded the authored text");
    auto changed=text;changed.value="H";sketcher::rebuild_text_contours(changed);require(sketcher::exact_text_curves(changed).size()!=curves.size(),"Font cache ignored changed text");
}

int main(int argc,char** argv){try{verify();verify_embedded_font();for(int i=1;i<argc;++i){QFile font(QString::fromUtf8(argv[i]));require(font.open(QIODevice::ReadOnly),"Cannot open verification font");const auto bytes=font.readAll();std::cout<<"Embedded font fixture: "<<argv[i]<<'\n';verify_embedded_font({reinterpret_cast<const unsigned char*>(bytes.constData()),static_cast<std::size_t>(bytes.size())});}std::cout<<"Bundled native text, Unicode, metric anchors, flip, rotation, atomicity and holed glyph extrusion passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
