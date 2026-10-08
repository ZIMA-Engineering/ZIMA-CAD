#pragma once
#include <zima/sketcher/sketch.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace zima::verification::tangency {
using namespace sketcher;using P=std::array<double,2>;
inline void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
inline double distance(P a,P b){return std::hypot(a[0]-b[0],a[1]-b[1]);}
inline P point(const Sketch& s,const std::string& id){const auto* p=s.find_point(id);check(p,"Missing point");return {p->x,p->y};}
inline void arc_equations(const Sketch& s) {
    const auto center_radius=[&](const std::string& id){const auto a=std::ranges::find(s.arcs,id,&SketchArc::id);check(a!=s.arcs.end(),"Arc missing");return std::pair{point(s,a->center_point_id),a->radius};};
    const auto line=[&](const std::string& id){if(id=="sketch_axis:x")return std::pair{P{0,0},P{1,0}};if(id=="sketch_axis:y")return std::pair{P{0,0},P{0,1}};const auto l=std::ranges::find(s.segments,id,&SketchSegment::id);if(l!=s.segments.end())return std::pair{point(s,l->first_point_id),point(s,l->second_point_id)};const auto r=std::ranges::find(s.external_references,id,&SketchExternalReference::id);check(r!=s.external_references.end(),"Line missing");return std::pair{r->cached_points.front(),r->cached_points.back()};};
    for(const auto& a:s.arcs){auto c=point(s,a.center_point_id);for(const auto& id:{a.start_point_id,a.end_point_id})check(std::abs(distance(point(s,id),c)-a.radius)<1e-6,"Arc endpoint escaped its radius");}
    for(const auto& c:s.constraints) {
        if(c.suppressed)continue;
        if(c.kind==ConstraintKind::Tangent){const bool first_arc=std::ranges::any_of(s.arcs,[&](const auto& a){return a.id==c.geometry_id;});const auto [o,r]=center_radius(first_arc?c.geometry_id:c.second_geometry_id);const auto [p,q]=line(first_arc?c.second_geometry_id:c.geometry_id);const double dx=q[0]-p[0],dy=q[1]-p[1],length=distance(p,q);check(std::abs(std::abs((o[0]-p[0])*dy-(o[1]-p[1])*dx)/length-r)<1e-6,"Trimmed arc lost a tangent equation");if(!c.first_point_id.empty()){auto contact=point(s,c.first_point_id);check(std::abs(distance(contact,o)-r)<1e-6,"Tangent contact left its arc");check(std::abs((contact[0]-p[0])*dy-(contact[1]-p[1])*dx)/length<1e-6,"Tangent contact left its line");}}
        if(c.kind==ConstraintKind::PointOnLine){auto p=point(s,c.first_point_id);auto [a,b]=line(c.geometry_id);check(std::abs((p[0]-a[0])*(b[1]-a[1])-(p[1]-a[1])*(b[0]-a[0]))/distance(a,b)<1e-6,"Axis incidence lost");}
        if(c.kind==ConstraintKind::Horizontal||c.kind==ConstraintKind::Vertical){auto a=point(s,c.first_point_id),b=point(s,c.second_point_id);check(std::abs(a[c.kind==ConstraintKind::Horizontal?1:0]-b[c.kind==ConstraintKind::Horizontal?1:0])<1e-6,"Center alignment lost");}
        if(c.kind==ConstraintKind::Symmetric){auto a=point(s,c.first_point_id),b=point(s,c.second_point_id);auto [o,q]=line(c.geometry_id);P d{q[0]-o[0],q[1]-o[1]};double t=((a[0]-o[0])*d[0]+(a[1]-o[1])*d[1])/(d[0]*d[0]+d[1]*d[1]);check(distance(b,P{2*(o[0]+t*d[0])-a[0],2*(o[1]+t*d[1])-a[1]})<1e-6,"Center symmetry lost");}
    }
    for(const auto& d:s.dimensions){if(d.suppressed)continue;double measured{};if(d.kind==DimensionKind::Radius||d.kind==DimensionKind::Diameter)measured=center_radius(d.geometry_id).second*(d.kind==DimensionKind::Diameter?2:1);else if(d.kind==DimensionKind::DistanceX)measured=point(s,d.second_point_id)[0]-point(s,d.first_point_id)[0];else if(d.kind==DimensionKind::DistancePointLine){auto p=point(s,d.first_point_id);auto [a,b]=line(d.geometry_id);measured=((b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0]))/distance(a,b)*d.solution_side;}else throw std::runtime_error("Unverified dimension kind");check(std::abs(measured-d.value)<1e-6,"Driving/reference dimension no longer measures the geometry");}
}
}
