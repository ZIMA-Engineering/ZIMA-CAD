#include <zima/sketcher/text_geometry.hpp>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_BBOX_H
#include FT_TRUETYPE_TABLES_H
#include <hb.h>
#include <hb-ot.h>
#include <memory>
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <numbers>
#include <span>
#include <list>
#include <QByteArray>

namespace zima::sketcher {
std::span<const unsigned char> bundled_sketch_font();
namespace {
using Point=std::array<double,2>;
struct Font {
    FT_Library library{};FT_Face face{};hb_font_t* shaper{};QByteArray source;
    explicit Font(const EmbeddedFont& embedded={}) {
        if(!embedded.empty()) {
            if(embedded.encoded().size()>24*1024*1024)throw std::invalid_argument("The font is too large (maximum 18 MB).");
            const auto decoded=QByteArray::fromBase64Encoding(QByteArray::fromStdString(embedded.encoded()),QByteArray::AbortOnBase64DecodingErrors);
            if(!decoded || decoded.decoded.isEmpty())throw std::invalid_argument("Cannot read the selected font.");
            source=decoded.decoded;
        }
        const auto data=source.isEmpty()?bundled_sketch_font():std::span<const unsigned char>(reinterpret_cast<const unsigned char*>(source.constData()),static_cast<std::size_t>(source.size()));
        if(FT_Init_FreeType(&library))throw std::runtime_error("Cannot initialize the font outline engine.");
        if(FT_New_Memory_Face(library,data.data(),static_cast<FT_Long>(data.size()),0,&face)) {
            FT_Done_FreeType(library);library=nullptr;throw std::runtime_error("Cannot read the selected font.");
        }
        auto* blob=hb_blob_create(reinterpret_cast<const char*>(data.data()),static_cast<unsigned>(data.size()),HB_MEMORY_MODE_READONLY,nullptr,nullptr);
        auto* hb_face=hb_face_create(blob,0);hb_blob_destroy(blob);shaper=hb_font_create(hb_face);hb_face_destroy(hb_face);
        hb_ot_font_set_funcs(shaper);hb_font_set_scale(shaper,face->units_per_EM,face->units_per_EM);
    }
    ~Font(){if(shaper)hb_font_destroy(shaper);if(face)FT_Done_Face(face);if(library)FT_Done_FreeType(library);}
    Font(const Font&)=delete;Font& operator=(const Font&)=delete;
};
Font& cached_font(const EmbeddedFont& source) {
    struct Entry {EmbeddedFont source;std::unique_ptr<Font> font;};
    thread_local std::list<Entry> fonts;
    for(auto it=fonts.begin();it!=fonts.end();++it)if(it->source==source) {
        fonts.splice(fonts.begin(),fonts,it);return *fonts.front().font;
    }
    auto font=std::make_unique<Font>(source);
    fonts.push_front({source,std::move(font)});if(fonts.size()>4)fonts.pop_back();return *fonts.front().font;
}
std::vector<std::uint32_t> codepoints(const std::string& value) {
    std::vector<std::uint32_t> result;
    for(std::size_t i=0;i<value.size();) {
        const auto first=static_cast<unsigned char>(value[i++]);std::uint32_t point=first;unsigned count=0;std::uint32_t minimum=0;
        if(first>=0xf0 && first<=0xf4){count=3;point=first&7;minimum=0x10000;}
        else if(first>=0xe0 && first<=0xef){count=2;point=first&15;minimum=0x800;}
        else if(first>=0xc2 && first<=0xdf){count=1;point=first&31;minimum=0x80;}
        else if(first>=0x80)throw std::invalid_argument("Sketch text must be valid UTF-8.");
        for(unsigned j=0;j<count;++j) {
            if(i==value.size())throw std::invalid_argument("Sketch text must be valid UTF-8.");
            const auto next=static_cast<unsigned char>(value[i++]);if((next&0xc0)!=0x80)throw std::invalid_argument("Sketch text must be valid UTF-8.");point=(point<<6)|(next&63);
        }
        if(point<minimum || point>0x10ffff || (point>=0xd800&&point<=0xdfff))throw std::invalid_argument("Sketch text must be valid UTF-8.");
        if(point<32 && point!='\n' && point!='\r' && point!='\t')throw std::invalid_argument("Sketch text contains unsupported control characters.");
        result.push_back(point);if(result.size()>4096)throw std::invalid_argument("Sketch text supports at most 4096 Unicode characters.");
    }
    return result;
}
Point midpoint(Point a,Point b){return {(a[0]+b[0])*.5,(a[1]+b[1])*.5};}
double distance(Point a,Point b){return std::hypot(a[0]-b[0],a[1]-b[1]);}
double chord_distance(Point p,Point a,Point b) {
    const auto x=b[0]-a[0],y=b[1]-a[1],square=x*x+y*y;
    const auto t=square>0?std::clamp(((p[0]-a[0])*x+(p[1]-a[1])*y)/square,0.,1.):0.;
    return std::hypot(p[0]-a[0]-t*x,p[1]-a[1]-t*y);
}
struct Outlines {
    std::vector<std::vector<Point>> contours;std::vector<Point> active;Point offset{};double tolerance{};std::size_t points{};std::exception_ptr failure;
    std::vector<std::vector<Point>> exact;std::vector<Point> poles;
    void bezier(Point a,Point b,Point c,Point d) {
        if(poles.empty())poles.push_back(a);
        poles.insert(poles.end(),{b,c,d});
    }
    void exact_line(Point a,Point b) {
        if(distance(a,b)<=1e-12)return;
        bezier(a,{(2*a[0]+b[0])/3,(2*a[1]+b[1])/3},
            {(a[0]+2*b[0])/3,(a[1]+2*b[1])/3},b);
    }
    Point position(const FT_Vector* p)const{return {p->x+offset[0],p->y+offset[1]};}
    void append(Point p){if(active.empty()||distance(active.back(),p)>1e-10){if(++points>1000000)throw std::runtime_error("Sketch text outline exceeds the point budget.");active.push_back(p);}}
    void finish(){
        if(!poles.empty())exact_line(poles.back(),poles.front());
        if(active.size()>1&&distance(active.front(),active.back())<1e-10)active.pop_back();
        if(active.size()>=3){contours.push_back(std::move(active));exact.push_back(std::move(poles));}
        active.clear();poles.clear();
    }
    void quadratic(Point a,Point control,Point b,unsigned depth=0) {
        if(chord_distance(control,a,b)<=tolerance){append(b);return;}
        if(depth==20)throw std::runtime_error("Sketch text curve exceeds the subdivision budget.");
        const auto first=midpoint(a,control),second=midpoint(control,b),middle=midpoint(first,second);
        quadratic(a,first,middle,depth+1);quadratic(middle,second,b,depth+1);
    }
    void cubic(Point a,Point first,Point second,Point b,unsigned depth=0) {
        if(std::max(chord_distance(first,a,b),chord_distance(second,a,b))<=tolerance){append(b);return;}
        if(depth==20)throw std::runtime_error("Sketch text curve exceeds the subdivision budget.");
        const auto p=midpoint(a,first),q=midpoint(first,second),r=midpoint(second,b),u=midpoint(p,q),v=midpoint(q,r),middle=midpoint(u,v);
        cubic(a,p,u,middle,depth+1);cubic(middle,v,r,b,depth+1);
    }
    template<class Action>int invoke(Action action)noexcept{try{action();return 0;}catch(...){failure=std::current_exception();return 1;}}
    static int move(const FT_Vector* p,void* data){auto& b=*static_cast<Outlines*>(data);return b.invoke([&]{b.finish();b.append(b.position(p));});}
    static int line(const FT_Vector* p,void* data){auto& b=*static_cast<Outlines*>(data);return b.invoke([&]{b.exact_line(b.active.back(),b.position(p));b.append(b.position(p));});}
    static int conic(const FT_Vector* c,const FT_Vector* p,void* data){auto& b=*static_cast<Outlines*>(data);return b.invoke([&]{const auto a=b.active.back(),q=b.position(c),d=b.position(p);
        b.bezier(a,{(a[0]+2*q[0])/3,(a[1]+2*q[1])/3},{(d[0]+2*q[0])/3,(d[1]+2*q[1])/3},d);b.quadratic(a,q,d);});}
    static int curve(const FT_Vector* c,const FT_Vector* d,const FT_Vector* p,void* data){auto& b=*static_cast<Outlines*>(data);return b.invoke([&]{b.bezier(b.active.back(),b.position(c),b.position(d),b.position(p));b.cubic(b.active.back(),b.position(c),b.position(d),b.position(p));});}
};
struct PreparedText {std::vector<std::vector<Point>> contours;std::vector<kernel::BSplineGeometry> curves;};
PreparedText prepare_text(const SketchText& text,bool y_up) {
    if(text.embedded_font.empty()&&text.font!="osifont")throw std::invalid_argument("Sketch text uses the bundled osifont font.");
    if(!std::isfinite(text.height)||text.height<=0 || !std::isfinite(text.anchor_x)||!std::isfinite(text.anchor_y)||!std::isfinite(text.angle_degrees))throw std::invalid_argument("Sketch text dimensions must be finite and height must be positive.");
    const auto characters=codepoints(text.value);auto& font=cached_font(text.embedded_font);const auto face=font.face;
    double cap=0;if(const auto* os2=static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face,ft_sfnt_os2));os2&&os2->version>=2&&os2->version!=0xffff)cap=os2->sCapHeight;
    constexpr FT_Int32 flags=FT_LOAD_NO_SCALE|FT_LOAD_NO_HINTING|FT_LOAD_NO_BITMAP;
    if(cap<=0) {
        FT_BBox box{};if(FT_Load_Char(face,'H',flags)||FT_Outline_Get_BBox(&face->glyph->outline,&box))throw std::runtime_error("Cannot measure the font cap height.");cap=static_cast<double>(box.yMax-box.yMin);
    }
    if(cap<=0)throw std::runtime_error("Cannot measure the font cap height.");
    const double scale=text.height/cap;
    if(!std::isfinite(scale)||scale<=0)throw std::invalid_argument("Sketch text dimensions must be finite and height must be positive.");
    Outlines outlines;outlines.tolerance=std::min(.01/scale,cap*.001);
    double left=std::numeric_limits<double>::infinity(),right=-left,bottom=left,top=-left;double baseline=0;
    const FT_Outline_Funcs callbacks{Outlines::move,Outlines::line,Outlines::conic,Outlines::curve,0,0};
    std::vector<std::uint32_t> line;
    const auto shape_line=[&] {
        std::unique_ptr<hb_buffer_t,decltype(&hb_buffer_destroy)> buffer(hb_buffer_create(),hb_buffer_destroy);
        hb_buffer_add_codepoints(buffer.get(),line.data(),static_cast<int>(line.size()),0,static_cast<int>(line.size()));
        hb_buffer_guess_segment_properties(buffer.get());hb_buffer_set_language(buffer.get(),hb_language_from_string("und",-1));
        hb_shape(font.shaper,buffer.get(),nullptr,0);
        if(!hb_buffer_allocation_successful(buffer.get()))throw std::runtime_error("Cannot shape Sketch text.");
        unsigned count=0;const auto* glyphs=hb_buffer_get_glyph_infos(buffer.get(),&count);const auto* positions=hb_buffer_get_glyph_positions(buffer.get(),nullptr);
        double x=0,y=baseline;
        for(unsigned i=0;i<count;++i) {
            const auto glyph=glyphs[i].codepoint;if(!glyph)throw std::invalid_argument("The font does not contain a requested character.");
            if(FT_Load_Glyph(face,glyph,flags))throw std::runtime_error("Cannot read a font glyph outline.");
            if(face->glyph->format!=FT_GLYPH_FORMAT_OUTLINE)throw std::runtime_error("The font glyph does not contain vector geometry.");
            if(face->glyph->outline.n_contours>0) {
                const double px=x+positions[i].x_offset,py=y+positions[i].y_offset;
                FT_BBox box{};if(FT_Outline_Get_BBox(&face->glyph->outline,&box))throw std::runtime_error("Cannot measure a font glyph outline.");
                left=std::min(left,px+box.xMin);right=std::max(right,px+box.xMax);bottom=std::min(bottom,py+box.yMin);top=std::max(top,py+box.yMax);
                outlines.offset={px,py};const auto status=FT_Outline_Decompose(&face->glyph->outline,&callbacks,&outlines);
                if(outlines.failure)std::rethrow_exception(outlines.failure);if(status)throw std::runtime_error("Cannot decompose a font glyph outline.");outlines.finish();
            }
            x+=positions[i].x_advance;y+=positions[i].y_advance;
        }
        line.clear();baseline-=face->height;
    };
    for(std::size_t i=0;i<characters.size();++i) {
        const auto point=characters[i];
        if(point=='\r'||point=='\n'){if(point=='\r'&&i+1<characters.size()&&characters[i+1]=='\n')++i;shape_line();}
        else if(point=='\t')line.insert(line.end(),4,' ');
        else line.push_back(point);
    }
    shape_line();
    if(outlines.contours.empty() || !std::isfinite(top-bottom) || top-bottom<=1e-9)throw std::invalid_argument("Text nevytváří žádný platný obrys.");
    const double width=(right-left)*scale,height=(top-bottom)*scale;
    const double horizontal=text.horizontal==TextHorizontalAlignment::Center?-width*.5:text.horizontal==TextHorizontalAlignment::Right?-width:0.;
    const double vertical=text.vertical==TextVerticalAlignment::Middle?height*.5:text.vertical==TextVerticalAlignment::Top?height:0.;
    const double angle=text.angle_degrees*std::numbers::pi/180.,cosine=std::cos(angle),sine=std::sin(angle);
    const auto transform=[&](Point p) {
        double px=(p[0]-left)*scale+horizontal;const double py=((bottom-p[1])*scale+vertical)*(y_up?-1.:1.);if(text.flipped)px=-px;
        p={text.anchor_x+px*cosine-py*sine,text.anchor_y+px*sine+py*cosine};
        if(!std::isfinite(p[0])||!std::isfinite(p[1]))throw std::invalid_argument("Sketch text dimensions must be finite and height must be positive.");
        return p;
    };
    PreparedText result;
    for(auto& contour:outlines.contours)for(auto& p:contour)p=transform(p);
    result.contours=std::move(outlines.contours);
    for(const auto& poles:outlines.exact) {
        if(poles.size()<4||(poles.size()-1)%3)throw std::runtime_error("The font glyph does not contain vector geometry.");
        kernel::BSplineGeometry curve;curve.degree=3;
        const auto spans=(poles.size()-1)/3;
        for(const auto& pole:poles){const auto p=transform(pole);curve.poles.push_back({p[0],p[1],0});}
        curve.knots.insert(curve.knots.end(),4,0.);
        for(std::size_t i=1;i<spans;++i)curve.knots.insert(curve.knots.end(),3,static_cast<double>(i)/spans);
        curve.knots.insert(curve.knots.end(),4,1.);curve.weights.assign(curve.poles.size(),1.);curve.validate();result.curves.push_back(std::move(curve));
    }
    return result;
}
const PreparedText& prepared_text(const SketchText& text,bool y_up) {
    // Exclude generated display contours and persistent IDs from the key.
    // Font blobs are immutable and shared across Sketch/draft copies.
    struct Entry {SketchText input;bool y_up;PreparedText geometry;};
    thread_local std::list<Entry> entries;
    const auto matches=[&](const Entry& e){const auto& t=e.input;return e.y_up==y_up && t.embedded_font==text.embedded_font && t.font==text.font && t.value==text.value &&
        t.anchor_x==text.anchor_x && t.anchor_y==text.anchor_y && t.height==text.height && t.horizontal==text.horizontal && t.vertical==text.vertical && t.angle_degrees==text.angle_degrees && t.flipped==text.flipped;};
    for(auto it=entries.begin();it!=entries.end();++it)if(matches(*it)){entries.splice(entries.begin(),entries,it);return entries.front().geometry;}
    auto geometry=prepare_text(text,y_up);SketchText input;
    input.embedded_font=text.embedded_font;input.font=text.font;input.value=text.value;
    input.anchor_x=text.anchor_x;input.anchor_y=text.anchor_y;input.height=text.height;
    input.horizontal=text.horizontal;input.vertical=text.vertical;input.angle_degrees=text.angle_degrees;input.flipped=text.flipped;
    entries.push_front({std::move(input),y_up,std::move(geometry)});
    const auto bytes=[](const Entry& e) {std::size_t n=e.input.value.size()+e.input.embedded_font.encoded().size();
        for(const auto& c:e.geometry.contours)n+=c.size()*sizeof(Point);
        for(const auto& c:e.geometry.curves)n+=c.poles.size()*sizeof(c.poles.front())+c.knots.size()*sizeof(double)+c.weights.size()*sizeof(double);
        return n;};
    std::size_t total=0;for(const auto& e:entries)total+=bytes(e);
    while(entries.size()>1&&(entries.size()>16||total>64*1024*1024)){total-=bytes(entries.back());entries.pop_back();}
    return entries.front().geometry;
}
}
void rebuild_text_contours(SketchText& text,bool y_up) {
    const auto& prepared=prepared_text(text,y_up);text.contours=prepared.contours;
    if(!text.embedded_font.empty())text.font_y_up=y_up;
}
std::vector<kernel::BSplineGeometry> exact_text_curves(const SketchText& text) {
    return prepared_text(text,text.font_y_up).curves;
}
std::string embedded_font_name(const EmbeddedFont& source) {
    const auto& font=cached_font(source);
    if(!FT_IS_SCALABLE(font.face))throw std::invalid_argument("The font glyph does not contain vector geometry.");
    std::string name=font.face->family_name?font.face->family_name:"Font";
    if(font.face->style_name)name+=" "+std::string(font.face->style_name);
    return name;
}
}
