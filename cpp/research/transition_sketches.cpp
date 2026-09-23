#include "transition_sketches.hpp"
#include <zima/kernel/sheet_material.hpp>
#include <algorithm>
#include <map>
#include <numbers>
namespace zima::research::transition {
namespace {
using namespace kernel::sheet_material;
double length(Vec3 p){return std::sqrt(dot(p,p));}
bool near(Vec3 a,Vec3 b){return length(sub(a,b))<1e-6;}
Vec3 local(const sketcher::Sketch& s,const std::string& id){const auto* p=s.find_point(id);if(!p)throw std::invalid_argument("Transition Sketch endpoint is missing.");return {p->x,p->y,0};}
void supported(const sketcher::Sketch& s) {
    s.validate();
    const auto present=[](const auto& curves){return std::ranges::any_of(curves,[](const auto& c){return !c.construction;});};
    if(present(s.circles)||present(s.ellipses)||present(s.elliptical_arcs)||present(s.bsplines)||!s.texts.empty()||!s.corner_radii.empty())
        throw std::invalid_argument("Transition requires explicit circular arcs and straight Sketch segments.");
}
std::array<Vec3,2> ends(const sketcher::Sketch& s) {
    std::map<std::string,int> count;
    for(const auto& line:s.segments)if(!line.construction){++count[line.first_point_id];++count[line.second_point_id];}
    for(const auto& arc:s.arcs)if(!arc.construction){++count[arc.start_point_id];++count[arc.end_point_id];}
    std::vector<Vec3> points;for(const auto& [id,n]:count){if(n==1)points.push_back(local(s,id));else if(n!=2)throw std::invalid_argument("Transition Sketch must be one open chain.");}
    if(points.size()!=2)throw std::invalid_argument("Transition Sketch must be one open chain.");
    if(points[0].x<points[1].x||(points[0].x==points[1].x&&points[0].y<points[1].y))std::swap(points[0],points[1]);
    return {points[0],points[1]};
}
Frame world_frame(const sketcher::Sketch& s,Vec3 origin,Vec3 x,Vec3 y) {
    const auto vector=[&](Vec3 p){return add(mul(s.x_axis(),p.x),mul(s.y_axis(),p.y));};
    const auto X=vector(x),Y=vector(y);return {s.world_point(origin.x,origin.y),X,Y,cross(X,Y)};
}
}
SketchInputs read_sketches(const sketcher::Sketch& authored_round,const sketcher::Sketch& authored_rectangle) {
    // Materialize native fillet records using the Sketcher's persisted parent IDs.
    // This evaluates authored curves only; it never invokes the solid kernel.
    auto round=authored_round.evaluated_profile_sketch();
    auto rectangle=authored_rectangle.evaluated_profile_sketch();
    std::erase_if(round.corner_radii,[](const auto& r){return r.suppressed||r.radius<=1e-9;});
    std::erase_if(rectangle.corner_radii,[](const auto& r){return r.suppressed||r.radius<=1e-9;});
    using namespace kernel::sheet_material;supported(round);supported(rectangle);SketchInputs output;
    std::vector<const sketcher::SketchArc*> arcs,corners;std::vector<const sketcher::SketchSegment*> lines;
    for(const auto& arc:round.arcs)if(!arc.construction)arcs.push_back(&arc);
    if(arcs.empty()||arcs.size()>2||std::ranges::any_of(round.segments,[](const auto& s){return !s.construction;}))
        throw std::invalid_argument("Select one semicircle or two connected quarter-circle arcs.");
    const auto round_ends=ends(round);const auto center=local(round,arcs[0]->center_point_id);const double radius=arcs[0]->radius;
    if(!near(add(round_ends[0],round_ends[1]),mul(center,2)))throw std::invalid_argument("Transition arc endpoints must form a diameter.");
    const auto x=unit(sub(round_ends[0],center));auto y=Vec3{-x.y,x.x,0};
    const auto mid=add(center,{radius*std::cos((arcs[0]->start_angle+arcs[0]->end_angle)/2),radius*std::sin((arcs[0]->start_angle+arcs[0]->end_angle)/2),0});
    if(dot(sub(mid,center),y)<0)y=mul(y,-1);
    for(const auto* arc:arcs) {
        const double span=std::abs(arc->end_angle-arc->start_angle);
        if(!near(local(round,arc->center_point_id),center)||std::abs(arc->radius-radius)>1e-6||std::abs(span-(arcs.size()==1?std::numbers::pi:std::numbers::pi/2))>1e-8)
            throw std::invalid_argument("Transition round profile must be one semicircle.");
        const Vec3 middle{radius*std::cos((arc->start_angle+arc->end_angle)/2),radius*std::sin((arc->start_angle+arc->end_angle)/2),0};
        if(dot(middle,y)<=0)throw std::invalid_argument("Transition arcs must lie on the same half-circle.");
        if(arcs.size()==1)output.arc_parents={arc->id,arc->id};else output.arc_parents[dot(middle,x)>0?0:1]=arc->id;
    }
    if(output.arc_parents[0].empty()||output.arc_parents[1].empty())throw std::invalid_argument("Transition quarter-circle correspondence is missing.");
    output.model.first_origin=world_frame(round,center,x,y);output.model.radius=radius;
    for(const auto& arc:rectangle.arcs)if(!arc.construction)corners.push_back(&arc);
    for(const auto& line:rectangle.segments)if(!line.construction)lines.push_back(&line);
    if(corners.size()!=2||lines.size()!=3)throw std::invalid_argument("Select an open half-rectangle with two rounded corners and three straight segments.");
    const auto rectangle_ends=ends(rectangle);const auto origin=mul(add(rectangle_ends[0],rectangle_ends[1]),.5),rx=unit(sub(rectangle_ends[0],rectangle_ends[1]));auto ry=Vec3{-rx.y,rx.x,0};
    if(dot(sub(local(rectangle,corners[0]->center_point_id),origin),ry)<0)ry=mul(ry,-1);
    const double w=length(sub(rectangle_ends[0],rectangle_ends[1]))/2,r=corners[0]->radius,h=dot(sub(local(rectangle,corners[0]->center_point_id),origin),ry)+r;
    if(r>=std::min(w,h)-1e-6)throw std::invalid_argument("Transition corner radius consumes a straight side.");
    const auto normalized=[&](Vec3 p){p=sub(p,origin);return Vec3{dot(p,rx),dot(p,ry),0};};
    const auto match=[](Vec3 a,Vec3 b,Vec3 c,Vec3 d){return (near(a,c)&&near(b,d))||(near(a,d)&&near(b,c));};
    for(const auto* arc:corners) {
        const auto c=normalized(local(rectangle,arc->center_point_id));const bool right=c.x>0;const double sign=right?1:-1;
        if(!near(c,{sign*(w-r),h-r,0})||std::abs(arc->radius-r)>1e-6||std::abs(std::abs(arc->end_angle-arc->start_angle)-std::numbers::pi/2)>1e-8||
            !match(normalized(local(rectangle,arc->start_point_id)),normalized(local(rectangle,arc->end_point_id)),{sign*w,h-r,0},{sign*(w-r),h,0}))
            throw std::invalid_argument("Transition rectangle corners must be equal tangent quarter-circles.");
        output.corner_parents[right?0:1]=arc->id;
    }
    const std::array<std::array<Vec3,2>,3> expected{
        std::array<Vec3,2>{Vec3{w,0,0},Vec3{w,h-r,0}},
        std::array<Vec3,2>{Vec3{w-r,h,0},Vec3{-w+r,h,0}},
        std::array<Vec3,2>{Vec3{-w,h-r,0},Vec3{-w,0,0}}};
    for(const auto* line:lines) {
        bool matched=false;
        for(std::size_t i=0;i<expected.size();++i)if(match(normalized(local(rectangle,line->first_point_id)),normalized(local(rectangle,line->second_point_id)),expected[i][0],expected[i][1])){if(!output.straight_parents[i].empty())throw std::invalid_argument("Duplicate transition side.");output.straight_parents[i]=line->id;matched=true;}
        if(!matched)throw std::invalid_argument("Transition straight sides do not match the rounded half-rectangle.");
    }
    const auto rectangle_frame=world_frame(rectangle,origin,rx,ry);const auto& root=output.model.first_origin;
    const auto relative=[&](Vec3 p){return Vec3{dot(p,root.x),dot(p,root.y),dot(p,root.z)};};
    output.model.second_relative={relative(sub(rectangle_frame.origin,root.origin)),relative(rectangle_frame.x),relative(rectangle_frame.y),relative(rectangle_frame.z)};
    output.model.width=2*w;output.model.depth=2*h;output.model.corner_radius=r;return output;
}
}
