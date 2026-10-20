#pragma once
#include <zima/sketcher/sketch.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <tuple>
#include <vector>
#include <numbers>
#include <stdexcept>

namespace midpoint_profile_study {
using namespace zima::sketcher;
inline void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
inline void near(double a,double b){if(std::abs(a-b)>=1e-7)throw std::runtime_error("Midpoint profile equation differs: "+std::to_string(a)+" versus "+std::to_string(b));}
struct Fixture {
    Sketch sketch=Sketch::create_default();
    std::array<std::string,4> vertices,edges;
    std::string center,width,height;
    double angle{};bool reversed{};int anchor{};
};
inline Fixture make(double angle,bool reversed,bool swap,int anchor) {
    Fixture f;f.angle=angle;f.reversed=reversed;f.anchor=anchor;
    const double a=angle*std::numbers::pi/180.;
    std::array<std::array<double,2>,4> points{{{-1.45,-.4},{1.45,-.4},{1.45,.4},{-1.45,.4}}};
    if(reversed)std::ranges::reverse(points);
    for(int i=0;i<4;++i){auto p=Sketch::create_point(points[i][0]*std::cos(a)-points[i][1]*std::sin(a),
        points[i][0]*std::sin(a)+points[i][1]*std::cos(a));f.vertices[i]=p.id;f.sketch.points.push_back(p);}
    for(int i=0;i<4;++i){auto e=Sketch::create_segment(f.vertices[i],f.vertices[(i+1)%4]);f.edges[i]=e.id;f.sketch.segments.push_back(e);}
    for(const auto& [i,j,kind]:std::vector<std::tuple<int,int,ConstraintKind>>{{0,2,ConstraintKind::Parallel},{1,3,ConstraintKind::Parallel},{0,1,ConstraintKind::Perpendicular}})
        static_cast<void>(f.sketch.add_segment_pair_constraint(f.edges[swap?j:i],f.edges[swap?i:j],kind));
    auto diagonal=Sketch::create_segment(f.vertices[0],f.vertices[2]);diagonal.construction=true;f.sketch.segments.push_back(diagonal);
    auto center=Sketch::create_point(0,0);center.construction=true;f.center=center.id;f.sketch.points.push_back(center);
    if(anchor==0)static_cast<void>(f.sketch.add_point_reference_constraint(f.center,"sketch_origin"));
    if(anchor==1) {
        auto source=Sketch::create_external_reference(ExternalReferenceKind::Point);
        source.source_document_id="read-only-source";source.source_owner_id="center";source.source_semantic_key="point";source.cached_points={{0,0}};
        f.sketch.add_external_reference(source);static_cast<void>(f.sketch.add_point_reference_constraint(f.center,source.id));
    }
    if(anchor==2)f.sketch.find_point(f.center)->fixed=true;
    static_cast<void>(f.sketch.add_midpoint_constraint(f.center,diagonal.id));
    auto width=f.sketch.create_segment_dimension(f.edges[0]);f.width=width.id;f.sketch.apply_dimension(width);
    auto height=f.sketch.create_segment_dimension(f.edges[1]);f.height=height.id;f.sketch.apply_dimension(height);
    if(std::abs(std::sin(a))<1e-10)static_cast<void>(f.sketch.add_segment_constraint(f.edges[0],ConstraintKind::Horizontal));
    else if(std::abs(std::cos(a))<1e-10)static_cast<void>(f.sketch.add_segment_constraint(f.edges[0],ConstraintKind::Vertical));
    // The native API stores the read-only axis as reference; the GUI
    // canonicalizes either selection order to this same representation.
    else f.sketch.apply_dimension(f.sketch.create_line_pair_dimension("sketch_axis:x",f.edges[0],DimensionKind::AngleBetween));
    return f;
}
inline void verify(Fixture& f,double width,double height) {
    const auto external=f.sketch.external_references;
    auto evaluated=f.sketch;
    const auto solved=evaluated.solve();check(solved.status!=SolveStatus::Invalid&&solved.status!=SolveStatus::Conflicting&&solved.maximum_residual<1e-8,"Midpoint profile did not solve");
    check(evaluated.external_references==external,"Midpoint fit changed its external source");
    std::array<std::array<double,2>,4> p;
    for(int i=0;i<4;++i){const auto* q=f.sketch.find_point(f.vertices[i]);check(q,"Midpoint fit lost vertex identity");p[i]={q->x,q->y};}
    const auto* c=f.sketch.find_point(f.center);near(c->x,(p[0][0]+p[2][0])/2);near(c->y,(p[0][1]+p[2][1])/2);
    if(f.anchor!=3){near(c->x,0);near(c->y,0);}
    const auto length=[&](int i,int j){return std::hypot(p[j][0]-p[i][0],p[j][1]-p[i][1]);};
    near(length(0,1),width);near(length(2,3),width);near(length(1,2),height);near(length(3,0),height);
    near((p[1][0]-p[0][0])*(p[2][0]-p[1][0])+(p[1][1]-p[0][1])*(p[2][1]-p[1][1]),0);
    const double a=f.angle*std::numbers::pi/180.;
    near((p[1][0]-p[0][0])*std::sin(a)-(p[1][1]-p[0][1])*std::cos(a),0);
}
}
