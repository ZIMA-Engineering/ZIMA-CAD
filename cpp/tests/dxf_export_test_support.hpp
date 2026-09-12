#pragma once
#include <zima/sketcher/sketch.hpp>
#include <fstream>
#include <map>
#include <numbers>
#include <cmath>

namespace zima::test {
struct DxfEntity {
    std::string type;
    std::map<int,std::vector<std::string>> values;
    double number(int code,std::size_t index=0)const{return std::stod(values.at(code).at(index));}
};
inline std::vector<DxfEntity> read_dxf_entities(const std::filesystem::path& path) {
    std::ifstream input(path);std::string code,value;std::vector<DxfEntity> result;bool entities=false;
    while(std::getline(input,code)&&std::getline(input,value)) {
        if(!value.empty()&&value.back()=='\r')value.pop_back();
        const auto group=std::stoi(code);
        if(group==2&&value=="ENTITIES"){entities=true;continue;}
        if(group==0&&value=="ENDSEC"){entities=false;continue;}
        if(!entities)continue;
        if(group==0)result.push_back({value,{}});
        else if(!result.empty())result.back().values[group].push_back(value);
    }
    if(!input.eof())throw std::runtime_error("Incomplete DXF test input");return result;
}
inline kernel::BSplineGeometry dxf_spline(const DxfEntity& entity) {
    kernel::BSplineGeometry result;result.degree=static_cast<unsigned>(entity.number(71));
    const auto count=static_cast<std::size_t>(entity.number(73));
    if(entity.values.at(40).size()!=entity.number(72)||entity.values.at(10).size()!=count||entity.values.at(20).size()!=count||entity.values.at(30).size()!=count)throw std::runtime_error("DXF spline array counts disagree");
    for(const auto& knot:entity.values.at(40))result.knots.push_back(std::stod(knot));
    for(std::size_t i=0;i<count;++i) {result.poles.push_back({entity.number(10,i),entity.number(20,i),entity.number(30,i)});result.weights.push_back(entity.values.contains(41)?entity.number(41,i):1);}
    result.validate();return result;
}
inline sketcher::Sketch dxf_curve_fixture() {
    auto sketch=sketcher::Sketch::create_default();
    static_cast<void>(sketch.add_ellipse(10,20,15,20,10,23));
    static_cast<void>(sketch.add_ellipse(30,20,32,20,30,27,true));
    const double r=.5,rx=2,ry=7;const auto position=[&](double t){return std::array{60+rx*std::cos(t)*std::cos(r)+ry*std::sin(t)*std::sin(r),20+rx*std::cos(t)*std::sin(r)-ry*std::sin(t)*std::cos(r)};};
    const auto a=position(5.5),b=position(7.5);
    static_cast<void>(sketch.add_elliptical_arc(60,20,60+rx*std::cos(r),20+rx*std::sin(r),60+ry*std::sin(r),20-ry*std::cos(r),a[0],a[1],b[0],b[1],true));
    static_cast<void>(sketch.add_bspline({{100,0},{100,100},{0,100}},2));
    sketch.bsplines.back().knots={0,0,0,1,1,1};sketch.bsplines.back().weights={1,std::sqrt(.5),1};
    static_cast<void>(sketch.add_bspline({{0,40},{10,60},{20,20},{30,40}},3));
    static_cast<void>(sketch.add_bspline({{0,80},{10,90},{20,70},{30,80}},3,false,false,0,true));
    static_cast<void>(sketch.add_bspline({{40,40},{50,40},{50,50},{40,50}},3,true));
    const auto line=sketch.add_segment(-30,-20,-10,-20);
    const auto offset=sketch.add_offset(line,2,false);static_cast<void>(sketch.retain_curve_intervals(offset,{{.25,.75}}));
    const auto circle=sketch.add_circle(-50,60,5);static_cast<void>(sketch.retain_curve_intervals(circle,{{.1,.4}}));
    static_cast<void>(sketch.add_segment(-10,-10,-7,-6,true));sketch.segments.back().centerline=true;
    auto point=sketcher::Sketch::create_point(123,456);point.construction=true;sketch.points.push_back(point);
    sketch.validate();return sketch;
}
inline void check_dxf_curves(const std::filesystem::path& path) {
    const auto require=[](bool value,const char* text){if(!value)throw std::runtime_error(text);};
    const auto near=[](double a,double b){return std::abs(a-b)<1e-8;};
    std::map<std::string,std::vector<DxfEntity>> types;for(auto& entity:read_dxf_entities(path))types[entity.type].push_back(std::move(entity));
    require(types["ELLIPSE"].size()==3&&types["SPLINE"].size()==6&&types["LINE"].size()==1&&types["XLINE"].size()==1,"DXF curve entities were lost or duplicated");
    require(types["POINT"].size()==1&&near(types["POINT"][0].number(10),123)&&near(types["POINT"][0].number(20),456)&&types["POINT"][0].values.at(8)[0]=="CONSTRUCTION","DXF exported editing handles or lost a standalone point");
    require(near(types["XLINE"][0].number(11),.6)&&near(types["XLINE"][0].number(21),.8),"DXF centerline direction was not normalized");
    const auto& first=types["ELLIPSE"][0];const auto& second=types["ELLIPSE"][1];const auto& arc=types["ELLIPSE"][2];
    require(near(first.number(11),5)&&near(first.number(40),.6)&&near(first.number(42),2*std::numbers::pi),"DXF ellipse changed its exact axes");
    require(near(second.number(11),0)&&near(second.number(21),7)&&near(second.number(40),2./7)&&second.values.at(8)[0]=="CONSTRUCTION","DXF did not normalize a longer second ellipse axis");
    double start=arc.number(41),end=arc.number(42);if(end<=start)end+=2*std::numbers::pi;
    for(int i=0;i<=20;++i) {
        const double f=i/20.,t=start+f*(end-start),sign=arc.number(230),ratio=arc.number(40);
        const double x=arc.number(10)+arc.number(11)*std::cos(t)-sign*arc.number(21)*ratio*std::sin(t);
        const double y=arc.number(20)+arc.number(21)*std::cos(t)+sign*arc.number(11)*ratio*std::sin(t);
        const double u=5.5+2*f;
        require(near(x,60+2*std::cos(u)*std::cos(.5)+7*std::sin(u)*std::sin(.5))&&near(y,20+2*std::cos(u)*std::sin(.5)-7*std::sin(u)*std::cos(.5)),"DXF reversed elliptical arc changed its interval or orientation");
    }
    std::vector<kernel::BSplineGeometry> splines;for(const auto& e:types["SPLINE"])splines.push_back(dxf_spline(e));
    require((int(types["SPLINE"][0].number(70))&4)!=0&&near(splines[0].weights[1],std::sqrt(.5)),"DXF lost rational weights");
    for(int i=0;i<=40;++i) {
        const double t=i/40.,s=1-t;const auto p=kernel::bspline_value(splines[0],t);const auto q=kernel::bspline_value(splines[1],t);
        require(near(p.x*p.x+p.y*p.y,10000),"DXF rational quarter-circle is not a circle");
        require(near(q.x,30*t)&&near(q.y,40*s*s*s+180*s*s*t+60*s*t*t+40*t*t*t),"DXF cubic curve changed its Bernstein polynomial");
    }
    const std::array<std::array<double,2>,4> fits{{{0,80},{10,90},{20,70},{30,80}}};
    for(int i=0;i<4;++i){const auto p=kernel::bspline_value(splines[2],i/3.);require(near(p.x,fits[i][0])&&near(p.y,fits[i][1]),"DXF interpolating spline missed its fit points");}
    const auto closed_start=kernel::bspline_value(splines[3],0),closed_end=kernel::bspline_value(splines[3],1);
    require((int(types["SPLINE"][3].number(70))&1)!=0&&near(closed_start.x,290./6)&&near(closed_start.y,250./6)&&near(closed_start.x,closed_end.x)&&near(closed_start.y,closed_end.y),"DXF periodic curve lost its closure or shape");
    const auto offset_start=kernel::bspline_value(splines[4],0),offset_end=kernel::bspline_value(splines[4],1);
    require(near(offset_start.x,-25)&&near(offset_end.x,-15)&&near(offset_start.y,-18)&&near(offset_end.y,-18),"DXF exported untrimmed offset support");
    for(int i=0;i<=20;++i){const auto p=kernel::bspline_value(splines[5],i/20.);require(near((p.x+50)*(p.x+50)+(p.y-60)*(p.y-60),25),"Trimmed circle lost rational geometry");}
    const auto circle_start=kernel::bspline_value(splines[5],0),circle_end=kernel::bspline_value(splines[5],1);
    const double w=std::sqrt(.5),den=.36+2*w*.24+.16;
    const double x=5*(.36+2*w*.24)/den,y=5*(2*w*.24+.16)/den;
    require(near(circle_start.x,-50+x)&&near(circle_start.y,60+y)&&near(circle_end.x,-50-x)&&near(circle_end.y,60+y),"DXF circle trim changed its rational-parameter endpoints");
}
}
