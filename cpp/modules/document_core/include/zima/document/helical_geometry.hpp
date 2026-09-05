#pragma once
#include <zima/document/part_document.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <set>
#include <stdexcept>

namespace zima::document::helical_geometry {
using V = kernel::Vec3;
using P = std::array<double,2>;
inline V add(V a,V b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline V mul(V a,double s) { return {a.x*s,a.y*s,a.z*s}; }
inline V sub(V a,V b) { return add(a,mul(b,-1)); }
inline double dot(V a,V b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline V cross(V a,V b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline double norm(V a) { return std::sqrt(dot(a,a)); }
inline V unit(V a) { const auto n=norm(a); if (!std::isfinite(n)||n<1e-12) throw std::runtime_error("Neurčitý směr Helical Sweepu"); return mul(a,1/n); }
inline double distance(P a,P b) { return std::hypot(a[0]-b[0],a[1]-b[1]); }
struct Curve { std::string id; P start,end; std::function<P(double)> at; std::string start_point_id,end_point_id; };
inline P point(const sketcher::Sketch& s,const std::string& id) {
    const auto* p=s.find_point(id); if (!p) throw std::runtime_error("Chybějící bod vodicí skici"); return {p->x,p->y};
}
// Evaluates the same clamped B-spline / interpolating Catmull-Rom convention
// used by Sketcher. No kernel or viewer tessellation participates in the law.
inline P spline_at(const std::vector<P>& p,unsigned degree,bool interpolating,double u) {
    const auto n=p.size();
    if (n<degree+1 || degree<1) throw std::runtime_error("Neplatná spline vodicí skici");
    if (interpolating) {
        const auto i=std::min(n-2,static_cast<std::size_t>(u*(n-1)));
        const double t=u*(n-1)-i;
        const auto a=p[i?i-1:0],b=p[i],c=p[i+1],d=p[std::min(n-1,i+2)]; P r{};
        for (int k=0;k<2;++k) r[k]=.5*(2*b[k]+(-a[k]+c[k])*t+(2*a[k]-5*b[k]+4*c[k]-d[k])*t*t+(-a[k]+3*b[k]-3*c[k]+d[k])*t*t*t);
        return r;
    }
    std::vector<double> knots(n+degree+1,1);
    for (unsigned j=0;j<=degree;++j) knots[j]=0;
    for (std::size_t j=degree+1;j<n;++j) knots[j]=double(j-degree)/(n-degree);
    std::size_t span=n-1;
    if(u<1) for(std::size_t j=degree;j<n;++j) if(u>=knots[j]&&u<knots[j+1]) {span=j;break;}
    std::vector<P> d(degree+1);for(unsigned j=0;j<=degree;++j)d[j]=p[span-degree+j];
    for(unsigned r=1;r<=degree;++r)for(unsigned j=degree;j>=r;--j){
        const auto i=span-degree+j; const auto den=knots[i+degree-r+1]-knots[i];
        const double a=den>0?(u-knots[i])/den:0;
        for(int k=0;k<2;++k)d[j][k]=(1-a)*d[j-1][k]+a*d[j][k];
    }
    return d[degree];
}
inline std::vector<Curve> guide_curves(const sketcher::Sketch& source,P start,bool require_origin=true) {
    auto s=source.evaluated_profile_sketch(); std::vector<Curve> unordered;
    for(const auto& l:s.segments) if(!l.construction) {
        auto a=point(s,l.first_point_id),b=point(s,l.second_point_id);
        unordered.push_back({l.id,a,b,[a,b](double u){return P{a[0]+u*(b[0]-a[0]),a[1]+u*(b[1]-a[1])};},l.first_point_id,l.second_point_id});
    }
    for(const auto& a:s.arcs) if(!a.construction) {
        auto c=point(s,a.center_point_id); const auto r=a.radius,t=a.start_angle;
        double sweep=a.end_angle-t; while(sweep<=0)sweep+=2*std::numbers::pi;
        const auto at=[c,r,t,sweep](double u){return P{c[0]+r*std::cos(t+u*sweep),c[1]+r*std::sin(t+u*sweep)};};
        unordered.push_back({a.id,at(0),at(1),at,a.start_point_id,a.end_point_id});
    }
    for(const auto& a:s.elliptical_arcs) if(!a.construction) {
        auto c=point(s,a.center_point_id); const double rot=a.rotation;
        double sweep=a.end_parameter-a.start_parameter;
        if(a.reversed) {while(sweep>=0)sweep-=2*std::numbers::pi;}
        else {while(sweep<=0)sweep+=2*std::numbers::pi;}
        const auto at=[a,c,rot,sweep](double u){double t=a.start_parameter+u*sweep,x=a.major_radius*std::cos(t),y=a.minor_radius*std::sin(t);return P{c[0]+x*std::cos(rot)-y*std::sin(rot),c[1]+x*std::sin(rot)+y*std::cos(rot)};};
        unordered.push_back({a.id,at(0),at(1),at,a.start_point_id,a.end_point_id});
    }
    for(const auto& b:s.bsplines) if(!b.construction) {
        if(b.closed)throw std::runtime_error("Vodicí křivka musí být otevřená");
        std::vector<P> pts;for(const auto& id:b.control_point_ids)pts.push_back(point(s,id));
        const auto at=[pts,b](double u){return spline_at(pts,b.degree,b.interpolating,u);};
        unordered.push_back({b.id,at(0),at(1),at,b.control_point_ids.front(),b.control_point_ids.back()});
    }
    if(std::ranges::any_of(s.circles,[](auto& c){return !c.construction;})||
       std::ranges::any_of(s.ellipses,[](auto& c){return !c.construction;})||!s.texts.empty())
        throw std::runtime_error("Vodicí skica musí obsahovat jedinou otevřenou dráhu");
    P cursor=start;
    if(require_origin&&distance(cursor,{0,0})>1e-7)throw std::runtime_error("Vodicí křivka musí začínat v počátku skici");
    std::vector<Curve> ordered;
    while(!unordered.empty()) {
        std::size_t found=unordered.size();bool reverse=false;
        for(std::size_t i=0;i<unordered.size();++i) {
            bool a=distance(cursor,unordered[i].start)<1e-7,b=distance(cursor,unordered[i].end)<1e-7;
            if(a||b){if(found!=unordered.size()||a==b)throw std::runtime_error("Vodicí dráha se větví nebo je uzavřená");found=i;reverse=b;}
        }
        if(found==unordered.size())throw std::runtime_error("Vodicí dráha není souvislá");
        auto c=unordered[found];unordered.erase(unordered.begin()+found);
        if(reverse){std::swap(c.start,c.end);std::swap(c.start_point_id,c.end_point_id);c.at=[at=c.at](double u){return at(1-u);};}
        ordered.push_back(c);cursor=c.end;
    }
    if(ordered.empty()||distance(cursor,start)<1e-7)throw std::runtime_error("Chybí otevřená vodicí dráha");
    return ordered;
}
inline std::vector<Curve> guide_curves(const sketcher::Sketch& source,const std::string& start_id,bool require_origin=true) {
    return guide_curves(source,point(source,start_id),require_origin);
}
struct Path {
    V origin,radial,axis,azimuth;
    double radius{},pitch{}; bool left{};
    std::vector<Curve> curves;
    V at(std::size_t i,double u) const {
        const auto q=curves[i].at(u);double r=radius+q[0],a=(left?-1:1)*2*std::numbers::pi*q[1]/pitch;
        return add(origin,add(mul(axis,q[1]),mul(add(mul(radial,std::cos(a)),mul(azimuth,std::sin(a))),r)));
    }
    V derivative(std::size_t i,double u) const {
        const double h=1e-5; // fourth-order interior / one-sided boundary derivative
        if(u<2*h)return mul(add(add(mul(at(i,u),-25),mul(at(i,u+h),48)),add(add(mul(at(i,u+2*h),-36),mul(at(i,u+3*h),16)),mul(at(i,u+4*h),-3))),1/(12*h));
        if(u>1-2*h)return mul(add(add(mul(at(i,u),25),mul(at(i,u-h),-48)),add(add(mul(at(i,u-2*h),36),mul(at(i,u-3*h),-16)),mul(at(i,u-4*h),3))),1/(12*h));
        return mul(add(sub(at(i,u-2*h),at(i,u+2*h)),mul(sub(at(i,u+h),at(i,u-h)),8)),1/(12*h));
    }
};
inline Path path(const HistoryContainer& c,bool require_guide=true) {
    const auto& p=c.helical;
    if(!std::isfinite(p.pitch)||p.pitch<=1e-6)throw std::runtime_error("Stoupání musí být kladné");
    auto s=sketcher::Sketch::from_serialized(p.sketches[0]);
    const auto circle=std::ranges::find_if(s.circles,[&](auto& a){return a.id==p.circle_id&&!a.construction;});
    if(circle==s.circles.end())throw std::runtime_error("Vyberte základní kružnici");
    const auto center=point(s,circle->center_point_id),start=point(s,p.start_point_id);
    if(circle->radius<=1e-7||std::abs(distance(center,start)-circle->radius)>1e-6)throw std::runtime_error("Počáteční bod musí ležet na kružnici");
    Path result;result.origin=s.world_point(center[0],center[1]);result.axis=unit(s.resolved_normal);
    result.radial=unit(sub(s.world_point(start[0],start[1]),result.origin));
    result.azimuth=unit(cross(result.axis,result.radial));result.radius=circle->radius;result.pitch=p.pitch;result.left=p.left_handed;
    if(!require_guide)return result;
    result.curves=guide_curves(sketcher::Sketch::from_serialized(p.sketches[1]),p.guide_start_point_id);
    const double dy=result.curves.back().end[1];
    if(std::abs(dy)<1e-7||std::abs(dy)/p.pitch>1000)throw std::runtime_error("Neplatná výška nebo více než 1000 otáček");
    for(std::size_t i=0;i<result.curves.size();++i){
        const auto& curve=result.curves[i]; P previous=curve.at(0);
        for(int j=1;j<=2048;++j){auto q=curve.at(double(j)/2048);
            if(!std::isfinite(q[0])||!std::isfinite(q[1])||result.radius+q[0]<=1e-6||(q[1]-previous[1])*dy<=0)
                throw std::runtime_error("Vodicí dráha musí postupovat ve výšce jedním směrem a zůstat mimo osu");
            previous=q;
        }
        if(i&&dot(unit(result.derivative(i-1,1)),unit(result.derivative(i,0)))<1-1e-7)
            throw std::runtime_error("Segmenty vodicí dráhy musí navazovat tečně");
        for(double u:{0.,1.})if(std::abs(dot(unit(result.derivative(i,u)),result.axis))<1e-9)
            throw std::runtime_error("Vodicí dráha nesmí mít čistě radiální tečnu");
    }
    return result;
}
} // namespace zima::document::helical_geometry
