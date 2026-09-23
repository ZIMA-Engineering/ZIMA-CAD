#include "transition_surface.hpp"
#include <chrono>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

using namespace zima::research::transition;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
double distance(Vec3 a,Vec3 b){return std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z);}
void valid(const Result& result,std::size_t count) {
    if(!result.valid())throw std::runtime_error("Unexpected failure code "+std::to_string(static_cast<int>(result.failure)));
    check(result.facets.size()==count&&result.folds.size()==count-1,"Facet/fold count");
    check(result.maximum_metric_error<1e-7&&result.maximum_planarity_error<1e-7,"Nonplanar or distorted output");
    check(std::abs(result.folded_area-result.unfolded_area)<1e-7*result.folded_area,"Area changed on unfolding");
    for(const auto& bound:result.boundary_deviation)check(bound.lower>=0&&bound.upper>=bound.lower&&bound.upper-bound.lower<.151,"Invalid deviation bracket");
}
Vec3 rotation(Vec3 p) {
    // Fixed independent rigid transform, not a production placement implementation.
    const double c=std::cos(.37),s=std::sin(.37),d=std::cos(-.61),t=std::sin(-.61);
    const Vec3 q{c*p.x-s*p.y,s*p.x+c*p.y,p.z};return {q.x,d*q.y-t*q.z,t*q.y+d*q.z};
}
QuarterArc transformed(QuarterArc a) {a.first_axis=rotation(a.first_axis);a.second_axis=rotation(a.second_axis);a.center=rotation(a.center);a.center.x+=10000;a.center.y-=12000;a.center.z+=9000;return a;}
}
int main()try {
    const auto start=std::chrono::steady_clock::now();
    const QuarterArc a{{0,0,0},{20,0,0},{0,20,0}},b{{0,0,150},{100,0,0},{0,100,0}};
    const auto cone=calculate(a,b);valid(cone,4);
    // Independent circumscribed circle polygon result, not the production distance routine.
    const double expected=100*(1/std::cos(std::numbers::pi/12)-1);
    check(cone.boundary_deviation[1].lower<=expected+1e-8&&cone.boundary_deviation[1].upper>=expected-1e-8,"Circle deviation must bracket analytic secant error");
    const auto moved=calculate(transformed(a),transformed(b));valid(moved,4);
    check(std::abs(cone.folded_area-moved.folded_area)<1e-6,"Rigid frame changed area");
    for(std::size_t i=0;i<4;++i)for(std::size_t j=0;j<4;++j)check(distance(cone.facets[i].unfolded[j],moved.facets[i].unfolded[j])<1e-7,"Rigid frame changed flat pattern");
    const QuarterArc ea{{0,0,0},{20,0,0},{0,30,0}},eb{{0,0,150},{100,0,0},{0,60,0}};
    const auto ellipse=calculate(ea,eb);valid(ellipse,4);
    Options eight;eight.facets=8;const auto refined=calculate(ea,eb,eight);valid(refined,8);
    check(refined.boundary_deviation[1].upper<ellipse.boundary_deviation[1].lower,"More facets did not reduce deviation");
    const auto cylinder=calculate(a,{{0,0,150},{20,0,12},{0,20,0}});valid(cylinder,4);
    const auto tilted=calculate(a,{{0,0,150},{100*std::cos(.5),0,-100*std::sin(.5)},{0,100,0}});
    check(tilted.failure==Failure::IncompatibleEndpoints&&tilted.facets.empty(),"Incompatible endpoints accepted or stale geometry retained");
    const auto intersecting=calculate(a,{{0,0,0},{100,0,0},{0,100,0}});
    check(!intersecting.valid(),"Degenerate coplanar construction accepted");
    auto bad=a;bad.first_axis.x=std::numeric_limits<double>::quiet_NaN();
    check(calculate(bad,b).failure==Failure::InvalidInput,"NaN accepted");
    Options count;count.facets=1;check(calculate(a,b,count).failure==Failure::InvalidInput,"One facet accepted");
    count.facets=129;check(calculate(a,b,count).failure==Failure::InvalidInput,"Excessive count accepted");
    bad=a;bad.second_axis={1,20,0};check(calculate(bad,b).failure==Failure::InvalidInput,"Nonorthogonal ellipse axes accepted");
    // Independent neighborhood directions: corner radius, offset, tilt (compatible ellipse).
    for(double radius:{10.,35.,70.}) {
        const QuarterArc first{{0,0,0},{radius,0,0},{0,radius,0}};
        valid(calculate(first,b),4);
    }
    auto offset=b;offset.center={25,-10,175};valid(calculate(a,offset),4);
    for(double tilt:{-.7,.2,.9})valid(calculate(a,{{0,0,150},{20,0,20*std::tan(tilt)},{0,20,0}}),4);
    std::cout<<"Transition surface contracts passed in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s\n";
    std::cout<<"Circle boundary deviation ["<<cone.boundary_deviation[1].lower<<", "<<cone.boundary_deviation[1].upper<<"] mm\n";
    std::cout<<"Ellipse boundary deviation, 4 facets ["<<ellipse.boundary_deviation[1].lower<<", "<<ellipse.boundary_deviation[1].upper<<"], 8 facets ["<<refined.boundary_deviation[1].lower<<", "<<refined.boundary_deviation[1].upper<<"] mm\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
