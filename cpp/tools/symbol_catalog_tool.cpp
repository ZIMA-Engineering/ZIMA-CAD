#include <zima/symbols/definition.hpp>
#include <zima/sketcher/text_geometry.hpp>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <iostream>
#include <cmath>
using namespace zima;
namespace {
sketcher::Sketch sketch(const std::string& id){auto s=sketcher::Sketch::create_default();s.id=id;s.name=id;return s;}
void line(sketcher::Sketch& s,const std::string& id,double x,double y,double u,double v) {
    s.points.push_back({id+":a",x,y,true});s.points.push_back({id+":b",u,v,true});s.segments.push_back({id,id+":a",id+":b"});
}
void text(sketcher::Sketch& s,const std::string& id,const std::string& value,double x,double y,double height=2.5) {
    auto t=sketcher::Sketch::create_text();t.id=id;t.value=value;t.anchor_x=x;t.anchor_y=y;t.height=height;t.modeling_geometry=false;t.color=sketcher::SketchTextColor::White;
    sketcher::rebuild_text_contours(t,true);s.texts.push_back(std::move(t));
}
void bracket(sketcher::Sketch& s,const std::string& id,double x,double bottom,double height,bool opening) {
    const double radius=height,cy=bottom+height/2,pi=std::acos(-1.0);
    const double cx=x+(opening?radius:-radius);
    const double start=opening?5*pi/6:-pi/6,end=start+pi/3;
    s.points.push_back({id+":center",cx,cy,true});
    s.points.push_back({id+":start",cx+radius*std::cos(start),cy+radius*std::sin(start),true});
    s.points.push_back({id+":end",cx+radius*std::cos(end),cy+radius*std::sin(end),true});
    s.arcs.push_back({id,id+":center",id+":start",id+":end",radius,start,end,false});
}
void texture_colors(symbols::Definition& d,sketcher::SketchTextColor text_color) {
    for(auto& s:d.sketches) {
        for(const auto& curve:s.segments)d.pens[s.id][curve.id]="yellow";
        for(const auto& curve:s.arcs)d.pens[s.id][curve.id]="yellow";
        for(const auto& curve:s.circles)d.pens[s.id][curve.id]="yellow";
        for(auto& t:s.texts)t.color=text_color;
    }
}
symbols::Definition annotation_text() {
    symbols::Definition d;d.id="ze:annotation:text";d.name="ZE-TEXT";
    auto base=sketch(d.id+":base");text(base,"annotation:text","Text",0,0);
    d.fields["Text"]={base.id,"annotation:text",{},true};
    d.variants["text"].sketches={base.id};d.variants["text"].text_values["Text"]="Text";
    d.sketches={std::move(base)};d.default_variant="text";d.insertion_point={0,0};
    d.validate();return d;
}
symbols::Definition historical_roughness() {
    symbols::Definition d;d.id="ze:surface-texture:iso1302-1978";d.name="ZE-SURFACE-TEXTURE-ISO1302-1978";
    auto base=sketch(d.id+":base");
    line(base,"roughness:left",-1.75,3.031,0,0);
    line(base,"roughness:right",0,0,3.5,6.062);
    text(base,"roughness:value","3,2",-1.75,7.062);
    base.texts.back().horizontal=sketcher::TextHorizontalAlignment::Right;
    base.texts.back().vertical=sketcher::TextVerticalAlignment::Middle;
    base.texts.back().anchor_x=1.5;base.texts.back().anchor_y=4.781;
    base.texts.back().drawing_keep_readable=true;sketcher::rebuild_text_contours(base.texts.back(),true);
    auto bar=sketch(d.id+":removal-required");line(bar,"roughness:removal",-1.75,3.031,1.75,3.031);
    d.sketches={base,bar};d.fields["Specification"]={base.id,"roughness:value",{"0,4","0,8","1,6","3,2","6,3","12,5","Ra 0,4","Ra 0,8","Ra 1,6","Ra 3,2","Ra 6,3","Ra 12,5"},true};
    for(const auto* kind:{"any_process","material_removal"}) {
        auto& row=d.variants[kind];row.sketches={base.id};
        if(std::string(kind)=="material_removal")row.sketches.push_back(bar.id);
        row.text_values["Specification"]="3,2";
    }
    d.default_variant="material_removal";texture_colors(d,sketcher::SketchTextColor::Green);
    d.validate();return d;
}
symbols::Definition roughness() {
    symbols::Definition d;d.id="ze:surface-texture:iso21920";d.name="ZE-SURFACE-TEXTURE-ISO21920";
    auto base=sketch(d.id+":base");
    line(base,"roughness:left",-1.75,3.031,0,0);line(base,"roughness:right",0,0,3.5,6.062);
    line(base,"roughness:top",3.5,6.062,19,6.062);line(base,"roughness:standard-bar",-1.2,4.55,1.2,4.55);
    text(base,"roughness:value","Ra 3.2",4.0,1.0);
    auto required=sketch(d.id+":removal-required");line(required,"roughness:removal",-1.75,3.031,1.75,3.031);
    auto forbidden=sketch(d.id+":removal-forbidden");forbidden.points.push_back({"roughness:circle-center",0,1.85,true});
    forbidden.circles.push_back({"roughness:circle","roughness:circle-center",.925,false});
    d.sketches={base,required,forbidden};d.fields["Specification"]={base.id,"roughness:value",{"Ra 0.4","Ra 0.8","Ra 1.6","Ra 3.2","Ra 6.3","Ra 12.5","Rz 6.3","Rz 12.5"},true};
    d.variants["any_process"].sketches={base.id};d.variants["material_removal"].sketches={base.id,required.id};
    d.variants["no_material_removal"].sketches={base.id,forbidden.id};d.default_variant="material_removal";
    for(auto& [key,row]:d.variants)row.text_values["Specification"]="Ra 3.2";
    texture_colors(d,sketcher::SketchTextColor::Green);
    d.validate();return d;
}
void arrow(sketcher::Sketch& s,const std::string& id,double x,double y,double u,double v) {
    line(s,id+":line",x,y,u,v);const double n=std::hypot(x-u,y-v),dx=(x-u)/n,dy=(y-v)/n;
    line(s,id+":left",u,v,u+1.5*dx+.3*dy,v+1.5*dy-.3*dx);
    line(s,id+":right",u,v,u+1.5*dx-.3*dy,v+1.5*dy+.3*dx);
}
symbols::Definition general_roughness() {
    auto d=roughness();d.id="ze:general-surface-texture:iso21920";d.name="ZE-GENERAL-SURFACE-TEXTURE-ISO21920";
    for(auto& [key,row]:d.variants) {
        auto extra=sketch(d.id+":"+key);
        const double x=26;
        line(extra,key+":left",x-1.75,3.031,x,0);line(extra,key+":right",x,0,x+3.5,6.062);
        line(extra,key+":top",x+3.5,6.062,x+5.5,6.062);line(extra,key+":standard-bar",x-1.2,4.55,x+1.2,4.55);
        if(key=="material_removal")line(extra,key+":removal",x-1.75,3.031,x+1.75,3.031);
        if(key=="no_material_removal") {extra.points.push_back({key+":center",x,1.85,true});extra.circles.push_back({key+":circle",key+":center",.925,false});}
        bracket(extra,key+":open",22,-.7,7.5,true);bracket(extra,key+":close",34,-.7,7.5,false);
        row.sketches.push_back(extra.id);d.sketches.push_back(std::move(extra));
    }
    texture_colors(d,sketcher::SketchTextColor::Green);
    d.validate();return d;
}
symbols::Definition edges() {
    symbols::Definition d;d.id="ze:general-edges:iso13715";d.name="ZE-GENERAL-EDGES-ISO13715";
    const auto make=[&](const std::string& kind,double x,const std::string& field,const std::string& value,bool corner) {
        auto s=sketch(d.id+":"+kind);const auto key="edges:"+kind;
        line(s,key+":vertical",x+4,4,x+4,8.5);line(s,key+":bottom",x+4,4,x+15,4);
        line(s,key+":reference",x+3.5,3.5,x+15,3.5);arrow(s,key+":leader",x+3.5,3.5,x,0);
        for(const auto* suffix:{":reference",":leader:line",":leader:left",":leader:right"})d.pens[s.id][key+suffix]="yellow";
        if(corner) {const bool internal=kind=="internal";line(s,key+":corner-x",x,0,x+(internal?3:-3),0);line(s,key+":corner-y",x,0,x,internal?3:-3);}
        text(s,key+":value",value,x+5,4.8);
        d.fields[field]={s.id,key+":value",{"-0.1","-0.2","-0.5","+0.1","+0.2","+0.5","±0.2"},true};
        const auto id=s.id;d.sketches.push_back(std::move(s));return id;
    };
    const auto external=make("external",0,"External edges","-0.2",true);
    const auto internal=make("internal",19,"Internal edges","+0.2",true);
    const auto all=make("all",0,"All edges","-0.2",false);
    const auto exception=make("exception",42,"Exception","-0.1",false);
    auto brackets=sketch(d.id+":brackets");bracket(brackets,"edges:open",38,-.5,9.5,true);bracket(brackets,"edges:close",61,-.5,9.5,false);
    const auto brackets_id=brackets.id;d.sketches.push_back(std::move(brackets));
    const std::map<std::string,std::vector<std::string>> scopes{{"general",{external,internal}},{"external",{external}},{"internal",{internal}},{"all",{all}}};
    for(const auto& [scope,sketches]:scopes) {
        auto& row=d.variants[scope];row.sketches=sketches;
        auto one=row;one.sketches.push_back(exception);one.sketches.push_back(brackets_id);d.variants[scope+"_exception"]=one;
        one.hidden_texts={"Exception"};d.variants[scope+"_exceptions"]=one;
    }
    for(auto& s:d.sketches) {
        for(auto& t:s.texts)t.color=sketcher::SketchTextColor::Green;
        for(const auto& c:s.segments)if(!d.pens[s.id].contains(c.id))d.pens[s.id][c.id]="green";
        for(const auto& c:s.arcs)if(!d.pens[s.id].contains(c.id))d.pens[s.id][c.id]="green";
    }
    d.default_variant="general";d.validate();return d;
}
symbols::Definition geometric_tolerance(const std::string& kind) {
    symbols::Definition d;d.id="ze:geometric-tolerance:"+kind;d.name="ZE-"+kind+"-ISO1101";
    auto glyph=sketch(d.id+":characteristic");const auto ln=[&](const char* id,double x,double y,double u,double v){line(glyph,id,x,y,u,v);};
    const auto circle=[&](const std::string& id,double r){glyph.points.push_back({id+":center",3.5,3.5,true});glyph.circles.push_back({id,id+":center",r,false});};
    if(kind=="STRAIGHTNESS")ln("line",1.5,3.5,5.5,3.5);
    if(kind=="FLATNESS"){ln("bottom",1,2.2,4.8,2.2);ln("right",4.8,2.2,6,4.8);ln("top",6,4.8,2.2,4.8);ln("left",2.2,4.8,1,2.2);}
    if(kind=="CIRCULARITY"||kind=="CYLINDRICITY"||kind=="POSITION"||kind=="COAXIALITY")circle("circle",1.7);
    if(kind=="CYLINDRICITY"){ln("left",.8,1.2,2,5.8);ln("right",5,1.2,6.2,5.8);}
    if(kind=="COAXIALITY")circle("inner",.9);
    if(kind=="POSITION"){ln("horizontal",1,3.5,6,3.5);ln("vertical",3.5,1,3.5,6);}
    if(kind=="LINE-PROFILE"||kind=="SURFACE-PROFILE") {
        glyph.points.push_back({"center",3.5,2.5,true});glyph.points.push_back({"start",5.5,2.5,true});glyph.points.push_back({"end",1.5,2.5,true});
        glyph.arcs.push_back({"profile","center","start","end",2,0,std::acos(-1.0),false});
        if(kind=="SURFACE-PROFILE")ln("base",1.5,2.5,5.5,2.5);
    }
    if(kind=="PARALLELISM"){ln("first",1.5,1.5,3.5,5.5);ln("second",3.5,1.5,5.5,5.5);}
    if(kind=="PERPENDICULARITY"){ln("horizontal",1.3,1.7,5.7,1.7);ln("vertical",3.5,1.7,3.5,5.3);}
    if(kind=="ANGULARITY"){ln("bottom",1.2,1.7,5.8,1.7);ln("slant",1.2,1.7,4.8,5.3);}
    if(kind=="SYMMETRY"){ln("middle",1,3.5,6,3.5);ln("bottom",1.8,2.2,5.2,2.2);ln("top",1.8,4.8,5.2,4.8);}
    if(kind=="CIRCULAR-RUNOUT"||kind=="TOTAL-RUNOUT")arrow(glyph,"first",1.5,1.5,5.2,5.2);
    if(kind=="TOTAL-RUNOUT"){arrow(glyph,"second",3.2,1.5,6.9,5.2);ln("join",1.5,1.5,3.2,1.5);}
    const auto glyph_id=glyph.id;d.sketches.push_back(glyph);d.frame_layout=symbols::FrameLayout{};d.frame_layout->cells={glyph_id};
    auto tolerance=sketch(d.id+":tolerance");text(tolerance,"value","0.1",1,2.2);
    d.fields["Tolerance"]={tolerance.id,"value",{"0.01","0.02","0.05","0.1","0.2","0.5"},true};
    const auto tolerance_id=tolerance.id;d.sketches.push_back(tolerance);d.frame_layout->cells.push_back(tolerance_id);
    const bool form=kind=="STRAIGHTNESS"||kind=="FLATNESS"||kind=="CIRCULARITY"||kind=="CYLINDRICITY";
    auto& row=d.variants["default"];row.sketches={glyph_id,tolerance_id};row.text_values["Tolerance"]="0.1";
    if(!form)for(int i=0;i<3;++i) {
        const std::string field=i==0?"Primary datum":i==1?"Secondary datum":"Tertiary datum";
        auto datum=sketch(d.id+":datum-"+std::to_string(i));text(datum,"value",std::string(1,char('A'+i)),1,2.2);
        d.fields[field]={datum.id,"value",{"","A","B","C","D","E","F"},true};row.text_values[field]=i==0?"A":"";
        row.sketches.push_back(datum.id);d.frame_layout->cells.push_back(datum.id);d.sketches.push_back(datum);
    }
    for(auto& s:d.sketches){for(auto& p:s.points)p.y-=3.5;for(auto& t:s.texts){t.anchor_y-=3.5;sketcher::rebuild_text_contours(t,true);}}
    d.default_variant="default";d.insertion_point={0,0};d.validate();return d;
}
}
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    try {
        if(argc!=3)throw std::invalid_argument("Expected library root and preview path");
        const auto root=std::filesystem::u8path(argv[1]);
        std::vector<std::pair<std::filesystem::path,symbols::Definition>> definitions{
            {root/"annotations/ZE-TEXT.symz",annotation_text()},
            {root/"surface-texture/ZE-SURFACE-TEXTURE-ISO1302-1978.symz",historical_roughness()},
            {root/"surface-texture/ZE-SURFACE-TEXTURE-ISO21920.symz",roughness()},
            {root/"general/ZE-GENERAL-SURFACE-TEXTURE-ISO21920.symz",general_roughness()},
            {root/"general/ZE-GENERAL-EDGES-ISO13715.symz",edges()}};
        for(const auto* kind:{"STRAIGHTNESS","FLATNESS","CIRCULARITY","CYLINDRICITY","LINE-PROFILE","SURFACE-PROFILE","PARALLELISM","PERPENDICULARITY","ANGULARITY","POSITION","COAXIALITY","SYMMETRY","CIRCULAR-RUNOUT","TOTAL-RUNOUT"}) {
            auto definition=geometric_tolerance(kind);definitions.push_back({root/"geometric-tolerances"/(definition.name+".symz"),std::move(definition)});
        }
        int variants=0;for(const auto& entry:definitions)variants+=int(entry.second.variants.size());
        QImage image(1400,200+variants*290,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);
        int row=0;
        for(const auto& [path,definition]:definitions) {
            std::filesystem::create_directories(path.parent_path());definition.save(path);
            if(symbols::Definition::load(path).serialized()!=definition.serialized())throw std::runtime_error("Catalog roundtrip differs");
            for(const auto& [variant,value]:definition.variants) {
                sketcher::SymbolInstance instance;instance.id="preview";instance.definition=definition.serialized();instance.variant=variant;
                const auto mesh=symbols::instance_mesh(instance);if(mesh.edges.empty())throw std::runtime_error("Empty catalog variant");
                painter.save();painter.translate(100,180+row*290);painter.scale(16,-16);QPainterPath letters;
                for(const auto& edge:mesh.edges) {
                    QPolygonF points;for(const auto& p:edge.points)points<<QPointF(p.x,p.y);
                    if(edge.filled_text){QPainterPath contour;contour.addPolygon(points);letters.addPath(contour);}
                    else {painter.setPen(QPen(Qt::black,.2));painter.drawPolyline(points);}
                }
                painter.fillPath(letters,Qt::black);painter.restore();++row;
            }
        }
        painter.end();if(!image.save(QString::fromUtf8(argv[2])))throw std::runtime_error("Cannot write catalog preview");
        QImage readable(1200,600,QImage::Format_ARGB32_Premultiplied);readable.fill(Qt::white);
        QPainter sample(&readable);sample.setRenderHint(QPainter::Antialiasing);
        const auto historical=historical_roughness();int column=0;
        for(double angle:{0.,90.,135.,180.,270.}) {
            sample.setPen(Qt::black);sample.drawText(QPointF(70+column*240,30),QString::number(angle)+" deg");
            for(int row=0;row<2;++row) {
                sketcher::SymbolInstance instance;instance.id="readable-preview";instance.definition=historical.serialized();
                instance.variant=row?"material_removal":"any_process";instance.angle_degrees=angle;
                sample.save();sample.translate(120+column*240,170+row*280);sample.scale(10,-10);QPainterPath letters;
                for(const auto& edge:symbols::instance_mesh(instance,{},0).edges) {
                    QPolygonF points;for(const auto& p:edge.points)points<<QPointF(p.x,p.y);
                    if(edge.filled_text)letters.addPolygon(points);
                    else {sample.setPen(QPen(Qt::black,.2));sample.drawPolyline(points);}
                }
                sample.fillPath(letters,Qt::black);sample.restore();
            }
            ++column;
        }
        sample.end();if(!readable.save(QString::fromStdString((root.parent_path()/"historical-roughness-orientation.png").string())))throw std::runtime_error("Cannot write readability preview");
        std::cout<<"Catalog definitions, variants and embedded text validated\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
