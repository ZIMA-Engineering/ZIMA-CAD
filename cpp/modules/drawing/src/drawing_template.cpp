#include <zima/sketcher/template_image_json.hpp>
#include <zima/drawing/drawing_template.hpp>
#include <zima/document/versioned_file.hpp>
#include <zima/kernel/stable_id.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iomanip>

namespace zima::drawing {
namespace {
using namespace zima::sketcher;
using Json = nlohmann::json;
using Ini = std::map<std::string,std::map<std::string,std::string>>;
std::string trim(std::string s) {
    const auto a=s.find_first_not_of(" \t\r\n");
    return a==std::string::npos ? "" : s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);
}
Ini read(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary);
    if(!in) throw std::runtime_error("Nelze otevřít šablonu výkresu.");
    Ini ini; std::string line,section;
    while(std::getline(in,line)) {
        if(line.starts_with("\xEF\xBB\xBF")) line.erase(0,3);
        line=trim(line);
        if(line.empty() || line[0]==';' || line[0]=='#') continue;
        if(line.front()=='[' && line.back()==']') {section=line.substr(1,line.size()-2);continue;}
        const auto eq=line.find('=');
        if(eq!=std::string::npos && !section.empty()) ini[section][trim(line.substr(0,eq))]=trim(line.substr(eq+1));
    }
    return ini;
}
std::string value(const Ini& ini,const std::string& section,const std::string& key,std::string fallback={}) {
    const auto s=ini.find(section); if(s==ini.end()) return fallback;
    const auto v=s->second.find(key); return v==s->second.end() ? fallback : v->second;
}
double number(const Ini& ini,const std::string& section,const std::string& key,double fallback=0) {
    const auto s=value(ini,section,key); return s.empty()?fallback:std::stod(s);
}
std::string num(double n) {std::ostringstream out;out<<std::setprecision(15)<<n;return out.str();}
std::vector<std::string> split(const std::string& line) {
    std::stringstream in(line);std::vector<std::string> r;std::string v;
    while(std::getline(in,v,',')) r.push_back(trim(v));return r;
}
TextHorizontalAlignment horizontal(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return std::tolower(c);});
    return s=="center"?TextHorizontalAlignment::Center:s=="right"?TextHorizontalAlignment::Right:TextHorizontalAlignment::Left;
}
TextVerticalAlignment vertical(std::string s) {
    return s=="middle"||s=="center"?TextVerticalAlignment::Middle:s=="top"?TextVerticalAlignment::Top:TextVerticalAlignment::Bottom;
}
SketchTextColor color(const std::string& s) {return s=="RED"||s=="red"?SketchTextColor::Red:s=="WHITE"||s=="white"?SketchTextColor::White:s=="YELLOW"||s=="yellow"?SketchTextColor::Yellow:SketchTextColor::Green;}
std::string pen(SketchTextColor c) {return c==SketchTextColor::Red?"RED":c==SketchTextColor::White?"WHITE":c==SketchTextColor::Yellow?"YELLOW":"GREEN";}
const char* align(TextHorizontalAlignment h) {return h==TextHorizontalAlignment::Center?"center":h==TextHorizontalAlignment::Right?"right":"left";}
const char* valign(TextVerticalAlignment v) {return v==TextVerticalAlignment::Top?"top":v==TextVerticalAlignment::Middle?"middle":"bottom";}
std::string segment_for(const Sketch& s,const std::string& a,const std::string& b) {
    for(const auto& g:s.segments) if((g.first_point_id==a&&g.second_point_id==b)||(g.first_point_id==b&&g.second_point_id==a))return g.id;
    throw std::runtime_error("Vazba šablony odkazuje na neexistující úsečku.");
}
void import_python(Sketch& s,const Json& data,const PrepareTemplateText& prepare) {
    auto& meta=*s.drawing_template;
    for(const auto& [id,p]:data.at("points").items()) {
        if(p.value("text_role","")=="outline_point")continue;
        s.points.push_back({id,p.at("x"),p.at("y"),p.value("fixed",false),p.value("construction",false)});
        if(p.value("text_role","")!="anchor")continue;
        SketchText t;t.modeling_geometry=false;t.id=id+":text";t.anchor_point_id=id;t.value=p.value("text_value","-");t.anchor_x=p.at("x");t.anchor_y=p.at("y");
        t.height=p.value("text_height",2.5);t.horizontal=horizontal(p.value("text_horizontal","left"));
        t.vertical=vertical(p.value("text_vertical","bottom"));t.angle_degrees=p.value("text_angle",0.0);
        t.flipped=p.value("text_flip",true);t.color=color(p.value("pen",p.value("text_color","GREEN")));
        t.font=p.value("text_font","osifont");prepare(t);s.texts.push_back(std::move(t));
        if(p.contains("template_field_id"))meta.field_ids[id+":text"]=p.at("template_field_id");
    }
    for(const auto& [id,g]:data.at("geometry").items()) {
        if(g.value("text_role","")=="outline" || g.contains("repeat_region_id"))continue;
        const auto type=g.at("type").get<std::string>();const auto p=g.at("points").get<std::vector<std::string>>();
        const bool aux=g.value("construction",false)||type=="construction";
        meta.pens[id]=g.value("pen","GREEN");
        if(type=="segment"||type=="construction") s.segments.push_back({id,p.at(0),p.at(1),aux,type=="construction"});
        else if(type=="circle")s.circles.push_back({id,p.at(0),g.at("radius"),aux});
        else if(type=="arc") {
            const auto* c=s.find_point(p.at(0));const auto* a=s.find_point(p.at(1));const auto* b=s.find_point(p.at(2));
            double start=std::atan2(a->y-c->y,a->x-c->x),end=std::atan2(b->y-c->y,b->x-c->x);
            auto first=p[1],last=p[2];if(g.value("clockwise",false)){std::swap(start,end);std::swap(first,last);}
            while(end<=start)end+=2*3.141592653589793;
            s.arcs.push_back({id,p[0],first,last,std::hypot(a->x-c->x,a->y-c->y),start,end,aux});
        } else if(type=="spline")s.bsplines.push_back({id,p,g.value("degree",3u),g.value("interpolating",false),g.value("closed",false),aux});
        else throw std::runtime_error("Nepodporovaná geometrie šablony: "+type);
    }
    const auto constraints=data.value("constraints",Json::object());
    for(const auto& [id,c]:constraints.items()) {
        const auto type=c.at("type").get<std::string>();const auto p=c.value("points",std::vector<std::string>{});
        SketchConstraint v;v.id=id;
        if(type=="horizontal"||type=="vertical"||type=="coincident") {
            v.kind=type=="horizontal"?ConstraintKind::Horizontal:type=="vertical"?ConstraintKind::Vertical:ConstraintKind::Coincident;
            v.first_point_id=p.at(0);v.second_point_id=p.at(1);
        } else if(type=="point_on_reference") {
            const auto refs=c.at("references").get<std::vector<std::string>>();
            if(refs.size()!=1||refs[0]!="sketch_origin")throw std::runtime_error("Neplatná vnější reference šablony.");
            v.kind=ConstraintKind::PointReference;v.first_point_id=p.at(0);v.second_point_id="sketch_origin";
        } else if(type=="equal_length"||type=="parallel"||type=="perpendicular") {
            v.kind=type=="equal_length"?ConstraintKind::EqualLength:type=="parallel"?ConstraintKind::Parallel:ConstraintKind::Perpendicular;
            v.geometry_id=segment_for(s,p.at(0),p.at(1));v.second_geometry_id=segment_for(s,p.at(2),p.at(3));
        } else throw std::runtime_error("Nepodporovaná vazba šablony: "+type);
        s.constraints.push_back(std::move(v));
    }
    const auto dimensions=data.value("dimensions",Json::object());
    for(const auto& [id,d]:dimensions.items()) {
        const auto type=d.at("type").get<std::string>();const auto p=d.at("points").get<std::vector<std::string>>();
        SketchDimension v;v.id=id;v.value=d.at("value");v.first_point_id=p.at(0);v.driving=d.value("driving",true);v.locked=d.value("locked",false);
        if(type=="distance_x"||type=="coordinate_x")v.kind=DimensionKind::DistanceX;
        else if(type=="distance_y"||type=="coordinate_y")v.kind=DimensionKind::DistanceY;
        else if(type=="distance")v.kind=DimensionKind::Distance;
        else throw std::runtime_error("Nepodporovaná kóta šablony: "+type);
        if(p.size()>1) {
            v.second_point_id=p[1];
            // Python stored signed point-pair differences. Current templates
            // show positive lengths; reversing the ordered pair preserves the
            // same equation and geometry. Coordinate placement keeps its sign.
            if(v.value<0){std::swap(v.first_point_id,v.second_point_id);v.value=-v.value;}
        }
        else v.geometry_id=type=="coordinate_x"?"sketch_axis:y":"sketch_axis:x";
        if(d.contains("placement"))v.placement=d.at("placement").get<std::array<double,2>>();
        s.dimensions.push_back(std::move(v));
    }
}
}
void validate_repeat_region(const zima::sketcher::SketchRepeatRegion& r) {
    if(r.id.empty() || !std::isfinite(r.x)||!std::isfinite(r.y)||!std::isfinite(r.width)||!std::isfinite(r.height)||!std::isfinite(r.step)||r.width<=0||r.height<=0||r.step<=0 ||
       (r.direction!="up"&&r.direction!="down"&&r.direction!="left"&&r.direction!="right"))
        throw std::runtime_error("Oblast kusovníku musí mít kladné rozměry a rozteč a platný směr opakování.");
}
zima::sketcher::Sketch create_template_sketch(bool title,std::string name) {
    auto s=zima::sketcher::Sketch::create_default();s.name=std::move(name);s.drawing_template.emplace();
    auto& m=*s.drawing_template;m.kind=title?"title_block":"drawing_format";
    m.sections[title?"TitleBlock":"Format"]={{"SchemaVersion","4"},{"Name",s.name}};
    if(title){m.sections["TitleBlock"]["Width"]="180";m.sections["TitleBlock"]["Height"]="60";m.sections["TitleBlock"]["Anchor"]="bottom-right";}
    else {m.sections["Format"]["SheetFormat"]="A4";m.sections["Format"]["Orientation"]="portrait";m.sections["Format"]["DocumentType"]="any";m.sections["Frame"]={{"LeftMargin","20"},{"RightMargin","10"},{"TopMargin","10"},{"BottomMargin","10"}};}
    return s;
}
zima::sketcher::Sketch load_template_sketch(const std::filesystem::path& path,const PrepareTemplateText& prepare) {
    auto ini=read(path);auto ext=path.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return std::tolower(c);});
    const bool title=ext==".tblz";
    if((!title&&ext!=".frmz")||!ini.contains(title?"TitleBlock":"Format"))throw std::runtime_error("Neplatná šablona rámečku nebo razítka.");
    auto s=create_template_sketch(title,value(ini,title?"TitleBlock":"Format","Name",path.stem().string()));
    if(ini.contains("Sketch")) {
        const auto raw=Json::parse(value(ini,"Sketch","Data"));
        if(raw.value("format","")=="zima-cad-cpp-sketch") {
            s=Sketch::from_serialized(raw.dump());
            if(!s.drawing_template||s.drawing_template->kind!=(title?"title_block":"drawing_format"))throw std::runtime_error("Typ skici neodpovídá příponě šablony.");
            for(auto& text:s.texts)prepare(text);
            s.validate();return s;
        }
        import_python(s,raw,prepare);
    } else {
        const auto section=title?"Geometry":"FrameGeometry";
        for(const auto& [id,raw]:ini[section]) {
            const auto v=split(raw);const auto fresh=[] {return zima::kernel::make_stable_id();};
            if(id.starts_with("Line")&&v.size()>=5) {
                const auto a=fresh(),b=fresh();s.points.push_back({a,std::stod(v[0]),std::stod(v[1])});s.points.push_back({b,std::stod(v[2]),std::stod(v[3])});
                s.segments.push_back({id,a,b});s.drawing_template->pens[id]=v[4];
            } else if(id.starts_with("Circle")&&v.size()>=4) {
                const auto c=fresh();s.points.push_back({c,std::stod(v[0]),std::stod(v[1])});s.circles.push_back({id,c,std::stod(v[2])});s.drawing_template->pens[id]=v[3];
            } else if(id.starts_with("Text")&&v.size()>=5) {
                SketchText t;t.modeling_geometry=false;t.id=id;t.value=v[0];t.anchor_x=std::stod(v[1]);t.anchor_y=std::stod(v[2]);t.height=std::stod(v[3]);t.color=color(v[4]);t.horizontal=horizontal(v.size()>5?v[5]:"left");t.flipped=true;prepare(t);s.texts.push_back(std::move(t));
            }
        }
    }
    for(const auto& [section,values]:ini)if(section.starts_with("Field.")) {
        const auto field_id=section.substr(6);
        if(std::ranges::any_of(s.drawing_template->field_ids,[&](const auto& pair){return pair.second==field_id;}))continue;
        SketchText t;t.modeling_geometry=false;t.id=zima::kernel::make_stable_id();t.value=value(ini,section,"Text");
        if(t.value.empty()){const auto parameter=value(ini,section,"Parameter",value(ini,section,"Source"));t.value=parameter.empty()?value(ini,section,"Default","-"):"&"+parameter;}
        t.horizontal=horizontal(value(ini,section,"Align","left"));t.vertical=vertical(value(ini,section,"VerticalAlign","middle"));
        const double w=number(ini,section,"BoxWidth"),h=number(ini,section,"BoxHeight");
        t.anchor_x=number(ini,section,"X")+(t.horizontal==TextHorizontalAlignment::Left?w:t.horizontal==TextHorizontalAlignment::Center?w/2:0);
        t.anchor_y=number(ini,section,"Y")+(t.vertical==TextVerticalAlignment::Top?h:t.vertical==TextVerticalAlignment::Middle?h/2:0);
        t.height=number(ini,section,"Height",2.5);t.color=color(value(ini,section,"Pen","GREEN"));t.flipped=true;
        prepare(t);s.drawing_template->field_ids[t.id]=field_id;s.texts.push_back(std::move(t));
    }
    for(const auto& [section,values]:ini)if(section.starts_with("RepeatRegion.")) {
        SketchRepeatRegion r{section.substr(13),number(ini,section,"X"),number(ini,section,"Y"),number(ini,section,"Width"),number(ini,section,"Height"),value(ini,section,"Direction","up"),number(ini,section,"Step",number(ini,section,"Height"))};
        validate_repeat_region(r);s.drawing_template->repeat_regions.push_back(std::move(r));
    }
    ini.erase("Sketch");ini.erase("Geometry");ini.erase("FrameGeometry");s.drawing_template->sections=std::move(ini);
    s.validate();return s;
}
void save_template_sketch(const zima::sketcher::Sketch& sketch,const std::filesystem::path& path) {
    sketch.validate();if(!sketch.drawing_template)throw std::runtime_error("Skica není šablonou výkresu.");
    auto s=sketch;auto& m=*s.drawing_template;const bool title=m.kind=="title_block";
    auto ext=path.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return std::tolower(c);});
    if(ext!=(title?".tblz":".frmz"))throw std::runtime_error("Nesprávná přípona šablony.");
    auto& ini=m.sections;
    std::erase_if(ini,[](const auto& p){return p.first.starts_with("Field.")||p.first.starts_with("RepeatRegion.");});
    for(const auto& t:s.texts)if(m.field_ids.contains(t.id)) {
        const auto section="Field."+m.field_ids.at(t.id);
        if(sketch.drawing_template->sections.contains(section))ini[section]=sketch.drawing_template->sections.at(section);
        auto& f=ini[section];const double w=number(ini,section,"BoxWidth"),h=number(ini,section,"BoxHeight");
        f["Text"]=t.value;f["X"]=num(t.anchor_x-(t.horizontal==TextHorizontalAlignment::Left?w:t.horizontal==TextHorizontalAlignment::Center?w/2:0));
        f["Y"]=num(t.anchor_y-(t.vertical==TextVerticalAlignment::Top?h:t.vertical==TextVerticalAlignment::Middle?h/2:0));
        f["Height"]=num(t.height);f["Align"]=align(t.horizontal);f["VerticalAlign"]=valign(t.vertical);f["Pen"]=pen(t.color);
        f.erase("Source");f.erase("Parameter");
    }
    for(const auto& r:m.repeat_regions){validate_repeat_region(r);ini["RepeatRegion."+r.id]={{"Kind","bom"},{"X",num(r.x)},{"Y",num(r.y)},{"Width",num(r.width)},{"Height",num(r.height)},{"Direction",r.direction},{"Step",num(r.step)}};}
    ini[title?"TitleBlock":"Format"]["SchemaVersion"]="4";
    Ini output=ini;auto& geometry=output[title?"Geometry":"FrameGeometry"];int line_index=0,text_index=0,circle_index=0;
    const auto pen_for=[&](const std::string& id){const auto it=m.pens.find(id);return it==m.pens.end()?std::string("GREEN"):it->second;};
    for(const auto& e:s.viewer_mesh().edges) {
        const auto& key=e.reference.semantic_key;
        if(key.starts_with("template_image:")||key.starts_with("text:")||key.starts_with("repeat_region:")||key.starts_with("circle:")||e.construction)continue;
        const auto id=key.substr(key.find(':')+1);
        for(std::size_t i=1;i<e.points.size();++i) {
            const auto a=s.local_point(e.points[i-1]),b=s.local_point(e.points[i]);
            geometry["Line"+std::to_string(++line_index)]=num(a[0])+", "+num(a[1])+", "+num(b[0])+", "+num(b[1])+", "+pen_for(id);
        }
    }
    for(const auto& c:s.circles)if(!c.construction){const auto* p=s.find_point(c.center_point_id);geometry["Circle"+std::to_string(++circle_index)]=num(p->x)+", "+num(p->y)+", "+num(c.radius)+", "+pen_for(c.id);}
    for(const auto& t:s.texts)if(!m.field_ids.contains(t.id))geometry["Text"+std::to_string(++text_index)]=t.value+", "+num(t.anchor_x)+", "+num(t.anchor_y)+", "+num(t.height)+", "+pen(t.color)+", "+align(t.horizontal);
    output["Sketch"]["Data"]=Json::parse(s.serialized()).dump();
    // Validate the complete payload before replacing a library file; preserve its previous version.
    std::ostringstream content;for(const auto& [section,values]:output){content<<'['<<section<<"]\n";for(const auto& [k,v]:values)content<<k<<" = "<<v<<'\n';content<<'\n';}
    auto temporary=path;temporary+=".tmp-"+zima::kernel::make_stable_id();
    try {
        {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out<<content.str();out.flush();if(!out)throw std::runtime_error("Zápis šablony selhal.");}
        const auto checked=read(temporary);Sketch::from_serialized(Json::parse(value(checked,"Sketch","Data")).dump()).validate();
        zima::document::archive_existing_file(path);
        std::filesystem::copy_file(temporary,path,std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(temporary);
    } catch(...) {std::error_code error;std::filesystem::remove(temporary,error);throw;}
}

void load_template_details(DrawingSheet& sheet,const std::filesystem::path& path,bool title) {
    const auto ini=read(path);
    auto& circles=title?sheet.title_block_circles:sheet.frame_circles;circles.clear();
    const auto geometry=ini.find(title?"Geometry":"FrameGeometry");
    const auto drawing_pen=[](const std::string& name){return name=="RED"?DrawingPen::Red:name=="WHITE"?DrawingPen::White:name=="YELLOW"?DrawingPen::Yellow:DrawingPen::Green;};
    if(geometry!=ini.end())for(const auto& [key,raw]:geometry->second)if(key.starts_with("Circle")) {
        const auto v=split(raw);if(v.size()<4)throw std::runtime_error("Neplatná kružnice šablony.");
        circles.push_back({{std::stod(v[0]),std::stod(v[1])},std::stod(v[2]),drawing_pen(v[3])});
    }
    if(title) {
        sheet.repeat_regions.clear();sheet.title_block_images.clear();
        for(const auto& [section,values]:ini)if(section.starts_with("RepeatRegion.")) {
            SketchRepeatRegion r{section.substr(13),number(ini,section,"X"),number(ini,section,"Y"),number(ini,section,"Width"),number(ini,section,"Height"),value(ini,section,"Direction","up"),number(ini,section,"Step",number(ini,section,"Height"))};
            validate_repeat_region(r);sheet.repeat_regions.push_back(std::move(r));
        }
    }
    if(!ini.contains("Sketch"))return;
    const auto data=Json::parse(value(ini,"Sketch","Data"));
    auto& texts=title?sheet.title_block_texts:sheet.frame_texts;texts.clear();
    const auto append=[&](TemplateText t,std::string field_id,const std::string& text_id) {
        const auto tokens=title_block_tokens(t.text);
        const bool repeat=std::ranges::any_of(sheet.repeat_regions,[&](const auto& r){return t.position.x>=r.x && t.position.x<=r.x+r.width && t.position.y>=r.y && t.position.y<=r.y+r.height;});
        if(title && field_id.empty() && !repeat && tokens.size()==1 && t.text=="&"+tokens.front() && title_block_token_scope(tokens.front())!="system") {
            field_id="text:"+text_id;
            TitleBlockField field;field.id=field_id;field.editable=true;field.write_back=title_block_token_scope(tokens.front())=="model";
            sheet.title_block_fields.push_back(std::move(field));
        }
        if(title&&!field_id.empty()) {
            const auto found=std::ranges::find(sheet.title_block_fields,field_id,&TitleBlockField::id);
            if(found!=sheet.title_block_fields.end()) {
                found->position=t.position;found->height=t.height;found->alignment=t.alignment;
                found->vertical_alignment=t.vertical_alignment;found->angle=t.angle;found->flipped=t.flipped;
                found->font=t.font;found->pen=t.pen;found->anchor_position=true;found->expression=t.text;
            }
        } else texts.push_back(std::move(t));
    };
    if(data.value("format","")=="zima-cad-cpp-sketch") {
        if(title)sheet.title_block_images=data.at("drawing_template").value("images",std::vector<TemplateImage>{});
        const auto& metadata=data.at("drawing_template");const auto& fields=metadata.at("field_ids");
        for(const auto& t:data.at("texts")) {
            const auto id=t.at("id").get<std::string>();auto c=t.at("color").get<std::string>();std::transform(c.begin(),c.end(),c.begin(),[](unsigned char b){return std::toupper(b);});
            append({t.at("value"),{t.at("anchor_x"),t.at("anchor_y")},t.at("height"),drawing_pen(c),t.at("horizontal"),t.at("vertical"),t.at("angle_degrees"),t.at("flipped"),t.at("font")},fields.value(id,""),id);
        }
    } else for(const auto& [id,t]:data.at("points").items())if(t.value("text_role","")=="anchor") {
        auto c=t.value("pen",t.value("text_color","GREEN"));std::transform(c.begin(),c.end(),c.begin(),[](unsigned char b){return std::toupper(b);});
        append({t.value("text_value","-"),{t.at("x"),t.at("y")},t.value("text_height",2.5),drawing_pen(c),t.value("text_horizontal","left"),t.value("text_vertical","bottom"),t.value("text_angle",0.0),t.value("text_flip",true),t.value("text_font","osifont")},t.value("template_field_id",""),id);
    }
}
TemplateLayout title_block_layout(const DrawingSheet& sheet,const TitleBlockContext& context) {
    TemplateLayout result;
    const auto in=[](const auto& r,Point2 p){return p.x>=r.x-1e-6&&p.x<=r.x+r.width+1e-6&&p.y>=r.y-1e-6&&p.y<=r.y+r.height+1e-6;};
    const auto region_for=[&](Point2 a,Point2 b) -> const SketchRepeatRegion* {
        for(const auto& r:sheet.repeat_regions)if(in(r,a)&&in(r,b))return &r;return nullptr;
    };
    const auto copies=[&](const SketchRepeatRegion* r,const auto& draw) {
        const auto count=r?std::max<std::size_t>(1,sheet.bom_rows.size()):1;
        for(std::size_t i=0;i<count;++i) {
            Point2 offset;auto c=context;
            if(r){const double distance=i*r->step;
                if(r->direction=="up")offset.y=distance;else if(r->direction=="down")offset.y=-distance;
                else if(r->direction=="left")offset.x=distance;else offset.x=-distance;
                if(i<sheet.bom_rows.size()) {const auto& row=sheet.bom_rows[i];c.has_bom_row=true;c.bom_item_number=row.item_number;c.bom_quantity=row.quantity;
                    c.mass_unit=row.mass_unit;c.file_stem=row.file_stem;c.parameters=row.parameters;c.parameter_values=row.parameter_values;c.parameter_aliases=row.parameter_aliases;}
            }
            draw(offset,c,r&&i<sheet.bom_rows.size()?std::optional{i}:std::nullopt);
        }
    };
    for(const auto& image:sheet.title_block_images) {
        const auto box=image.corners();
        copies(region_for({box[0][0],box[0][1]},{box[2][0],box[2][1]}),[&](Point2 o,const auto&,const auto&){auto i=image;i.x+=o.x;i.y+=o.y;result.images.push_back(std::move(i));});
    }
    for(const auto& line:sheet.title_block_lines)copies(region_for(line.first,line.second),[&](Point2 o,const auto&,const auto&){auto l=line;l.first.x+=o.x;l.first.y+=o.y;l.second.x+=o.x;l.second.y+=o.y;result.lines.push_back(l);});
    for(const auto& circle:sheet.title_block_circles)copies(region_for({circle.center.x-circle.radius,circle.center.y-circle.radius},{circle.center.x+circle.radius,circle.center.y+circle.radius}),[&](Point2 o,const auto&,const auto&){auto c=circle;c.center.x+=o.x;c.center.y+=o.y;result.circles.push_back(c);});
    const auto bind=[&](TemplateText& text,const std::string& expression,std::optional<std::size_t> row,std::string id,bool editable=false){
        const auto tokens=title_block_tokens(expression);
        if(!editable&&std::ranges::none_of(tokens,[](const auto& token){return title_block_token_scope(token)!="system";})){text.field_id.clear();return;}
        if(row)id="bom:"+std::to_string(*row)+":"+id;
        text.field_id=id;result.edit_targets[id]={expression,row};
    };
    for(std::size_t index=0;index<sheet.title_block_texts.size();++index){const auto& text=sheet.title_block_texts[index];
        copies(region_for(text.position,text.position),[&](Point2 o,const auto& c,std::optional<std::size_t> row){
            auto t=text;TitleBlockField f;f.expression=t.text;t.text=resolve_title_block_text(f,c,sheet);t.position.x+=o.x;t.position.y+=o.y;
            bind(t,text.text,row,"text:"+std::to_string(index));result.texts.push_back(std::move(t));
        });
    }
    for(const auto& field:sheet.title_block_fields) {
        auto position=field.position;
        if(!field.anchor_position){position.x+=field.alignment=="left"?field.box_width:field.alignment=="center"?field.box_width/2:0;
            position.y+=field.vertical_alignment=="top"?field.box_height:(field.vertical_alignment=="center"||field.vertical_alignment=="middle")?field.box_height/2:0;}
        copies(region_for(position,position),[&](Point2 o,const auto& c,std::optional<std::size_t> row){
            TemplateText text{resolve_title_block_text(field,c,sheet),{position.x+o.x,position.y+o.y},field.height,field.pen,field.alignment,field.vertical_alignment,field.angle,field.flipped,field.font};
            bind(text,field.expression,row,field.id,field.editable);result.texts.push_back(std::move(text));
        });
    }
    return result;
}
} // namespace zima::drawing
