#include "transition_model.hpp"
#include <algorithm>
#include <numbers>

namespace zima::research::transition {
namespace {
Vec3 plus(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 times(Vec3 a,double k){return {a.x*k,a.y*k,a.z*k};}
double product(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 vector_product(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double length(Vec3 a){return std::sqrt(product(a,a));}
bool finite_vector(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
QuarterArc local_arc(const Profile& p) {
    const auto& s=p.sketch;s.validate();
    const auto circle=std::ranges::find(s.arcs,p.curve_id,&sketcher::SketchArc::id);
    const auto ellipse=std::ranges::find(s.elliptical_arcs,p.curve_id,&sketcher::SketchEllipticalArc::id);
    std::string center,start,end;double sweep=0;
    if(circle!=s.arcs.end()&&!circle->construction) {
        center=circle->center_point_id;start=circle->start_point_id;end=circle->end_point_id;sweep=circle->end_angle-circle->start_angle;
    }else if(ellipse!=s.elliptical_arcs.end()&&!ellipse->construction) {
        center=ellipse->center_point_id;start=ellipse->start_point_id;end=ellipse->end_point_id;sweep=ellipse->end_parameter-ellipse->start_parameter;
    }else throw Failure::InvalidInput;
    // Geometry may be traversed in either direction, but it must be a quarter.
    sweep=std::remainder(sweep,2*std::numbers::pi);
    if(std::abs(std::abs(sweep)-std::numbers::pi/2)>1e-8)throw Failure::InvalidInput;
    const auto* c=s.find_point(center);const auto* a=s.find_point(start);const auto* b=s.find_point(end);
    if(!c||!a||!b)throw Failure::InvalidInput;
    QuarterArc result{{c->x,c->y,0},{a->x-c->x,a->y-c->y,0},{b->x-c->x,b->y-c->y,0}};
    // An ellipse quarter is bounded by its principal axes. Other parameter
    // intervals cannot be represented by an orthogonal semiaxis pair here.
    if(std::abs(product(result.first_axis,result.second_axis))>1e-9*length(result.first_axis)*length(result.second_axis))throw Failure::InvalidInput;
    return result;
}
QuarterArc apply(QuarterArc a,const Frame& f){return {f.point(a.center),f.direction(a.first_axis),f.direction(a.second_axis)};}
}
Vec3 Frame::point(Vec3 p)const{return plus(origin,direction(p));}
Vec3 Frame::direction(Vec3 p)const{return plus(plus(times(x,p.x),times(y,p.y)),times(z,p.z));}
bool Frame::valid()const {
    return finite_vector(origin)&&finite_vector(x)&&finite_vector(y)&&finite_vector(z)&&
        std::abs(length(x)-1)<1e-9&&std::abs(length(y)-1)<1e-9&&std::abs(length(z)-1)<1e-9&&
        std::abs(product(x,y))<1e-9&&length(plus(vector_product(x,y),times(z,-1)))<1e-9;
}
std::array<QuarterArc,2> Model::world_arcs()const {
    if(!first_origin.valid()||!second_relative.valid())throw Failure::InvalidInput;
    return {apply(local_arc(profiles[0]),first_origin),apply(apply(local_arc(profiles[1]),second_relative),first_origin)};
}
Result Model::evaluate()const {
    try {const auto arcs=world_arcs();return calculate(arcs[0],arcs[1],options);}
    catch(Failure error){Result result;result.failure=error;return result;}
    catch(const std::exception&){Result result;result.failure=Failure::InvalidInput;return result;}
}
Model Model::example() {
    Model m;
    for(auto& p:m.profiles)p.sketch=sketcher::Sketch::create_default();
    m.profiles[0].curve_id=m.profiles[0].sketch.add_arc(0,0,20,0,0,20);
    m.profiles[1].curve_id=m.profiles[1].sketch.add_elliptical_arc(0,0,100,0,0,60,100,0,0,60);
    return m;
}
kernel::ViewerMesh mesh(const Result& r,bool unfolded) {
    kernel::ViewerMesh result;if(!r.valid())return result;
    for(const auto& f:r.facets) {
        const auto& q=unfolded?f.unfolded:f.folded;const auto start=static_cast<std::uint32_t>(result.vertices.size());
        result.vertices.insert(result.vertices.end(),q.begin(),q.end());
        for(auto i:{0u,1u,2u,0u,2u,3u})result.triangles.push_back(start+i);
        result.triangle_references.resize(result.triangle_references.size()+2);
        for(std::size_t i=0;i<4;++i){kernel::ViewerEdge edge;edge.points={q[i],q[(i+1)%4]};result.edges.push_back(std::move(edge));}
    }return result;
}
}
