#include "transition_surface.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace zima::research::transition {
namespace {
constexpr double half_pi=std::numbers::pi/2;
Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 mul(Vec3 a,double s){return {a.x*s,a.y*s,a.z*s};}
double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double norm(Vec3 a){return std::sqrt(dot(a,a));}
bool finite(Vec3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
Vec3 unit(Vec3 a){const double n=norm(a);if(n<1e-14)throw Failure::SingularSurface;return mul(a,1/n);}
Vec3 mix(Vec3 a,Vec3 b,double t){return add(mul(a,1-t),mul(b,t));}
struct Plane {Vec3 normal;double offset;};
Plane arc_plane(const QuarterArc& a){const auto n=unit(cross(a.first_axis,a.second_axis));return {n,dot(n,a.center)};}
Vec3 intersect(Plane a,Plane b,Plane c) {
    const double det=dot(a.normal,cross(b.normal,c.normal));
    if(std::abs(det)<1e-10)throw Failure::SingularPlanes;
    return mul(add(add(mul(cross(b.normal,c.normal),a.offset),mul(cross(c.normal,a.normal),b.offset)),mul(cross(a.normal,b.normal),c.offset)),1/det);
}
void validate(const QuarterArc& a) {
    if(!finite(a.center)||!finite(a.first_axis)||!finite(a.second_axis))throw Failure::InvalidInput;
    const double x=norm(a.first_axis),y=norm(a.second_axis);
    if(x<.001||y<.001||x>1e6||y>1e6||std::abs(dot(a.first_axis,a.second_axis))>1e-9*x*y)throw Failure::InvalidInput;
}
double residual(const QuarterArc& a,const QuarterArc& b,double u,double v) {
    const auto d=sub(b.point(v),a.point(u)),x=a.tangent(u),y=b.tangent(v);
    if(norm(d)<1e-10)throw Failure::SingularSurface;
    return dot(d,cross(x,y))/(norm(d)*norm(x)*norm(y));
}
double corresponding(const QuarterArc& a,const QuarterArc& b,double u) {
    const auto t=a.tangent(u),w=sub(b.center,a.point(u));
    const double alpha=dot(t,cross(mul(b.first_axis,-1),w));
    const double beta=dot(t,cross(b.second_axis,w));
    const double gamma=-dot(t,cross(b.first_axis,b.second_axis));
    const double radius=std::hypot(alpha,beta);
    if(radius<1e-12*norm(t)*norm(b.first_axis)*std::max(norm(b.second_axis),norm(w)))throw Failure::AmbiguousCorrespondence;
    if(std::abs(gamma)>radius*(1+1e-12))throw Failure::NonMonotoneCorrespondence;
    const double theta=std::asin(std::clamp(-gamma/radius,-1.,1.)),phase=std::atan2(beta,alpha);
    std::vector<double> roots;
    for(double base:{theta,std::numbers::pi-theta})for(int k=-2;k<=2;++k) {
        double v=base-phase+2*std::numbers::pi*k;
        if(v<-1e-9||v>half_pi+1e-9)continue;
        v=std::clamp(v,0.,half_pi);
        if(std::ranges::none_of(roots,[v](double x){return std::abs(x-v)<1e-9;}))roots.push_back(v);
    }
    if(roots.size()!=1)throw Failure::AmbiguousCorrespondence;
    return roots[0];
}
double segment_distance(Vec3 p,Vec3 a,Vec3 b) {
    const auto d=sub(b,a);const double den=dot(d,d);
    return norm(sub(p,add(a,mul(d,den>0?std::clamp(dot(sub(p,a),d)/den,0.,1.):0.))));
}
double poly_distance(Vec3 p,const std::vector<Vec3>& poly) {
    double d=std::numeric_limits<double>::infinity();
    for(std::size_t i=1;i<poly.size();++i)d=std::min(d,segment_distance(p,poly[i-1],poly[i]));return d;
}
// Certified sampling bracket via the 1-Lipschitz distance-to-set function.
// Curve speed <= max semiaxis, so its sampled polyline Hausdorff error <= speed*du/2.
Deviation deviation(const QuarterArc& arc,const std::vector<Vec3>& boundary,double resolution) {
    const double speed=std::max(norm(arc.first_axis),norm(arc.second_axis));
    const auto steps=static_cast<std::size_t>(std::ceil(speed*half_pi/resolution));
    if(steps>200000)throw Failure::DeviationResolution;
    std::vector<Vec3> curve;curve.reserve(steps+1);
    double forward=0.;
    for(std::size_t i=0;i<=steps;++i){curve.push_back(arc.point(half_pi*i/steps));forward=std::max(forward,poly_distance(curve.back(),boundary));}
    const double curve_error=speed*half_pi/(2*steps);
    double backward=0.,polygon_error=0.;
    std::size_t budget=0;
    for(std::size_t i=1;i<boundary.size();++i) {
        const double length=norm(sub(boundary[i],boundary[i-1]));
        const auto samples=std::max<std::size_t>(1,static_cast<std::size_t>(std::ceil(length/resolution)));
        budget+=samples;
        if(budget>200000||budget*curve.size()>60000000)throw Failure::DeviationResolution;
        polygon_error=std::max(polygon_error,length/(2*samples));
        for(std::size_t j=0;j<=samples;++j)backward=std::max(backward,poly_distance(mix(boundary[i-1],boundary[i],static_cast<double>(j)/samples),curve));
    }
    return {std::max(forward,std::max(0.,backward-curve_error)),
        std::max(forward+curve_error,backward+curve_error+polygon_error)};
}
double area(const std::array<Vec3,4>& q){return .5*(norm(cross(sub(q[1],q[0]),sub(q[2],q[0])))+norm(cross(sub(q[2],q[0]),sub(q[3],q[0]))));}
bool convex(const std::array<Vec3,4>& q,Vec3 n,double tolerance) {
    for(std::size_t i=0;i<4;++i) {
        const auto a=sub(q[(i+1)%4],q[i]),b=sub(q[(i+2)%4],q[(i+1)%4]);
        if(norm(a)<tolerance||dot(cross(a,b),n)<=tolerance*norm(a))return false;
    }return true;
}
bool overlaps(const std::array<Vec3,4>& a,const std::array<Vec3,4>& b,Vec3 normal,double tolerance) {
    for(const auto& poly:{a,b})for(std::size_t i=0;i<4;++i) {
        const auto axis=unit(cross(normal,sub(poly[(i+1)%4],poly[i])));
        double amin=1e100,amax=-1e100,bmin=1e100,bmax=-1e100;
        for(auto p:a){amin=std::min(amin,dot(p,axis));amax=std::max(amax,dot(p,axis));}
        for(auto p:b){bmin=std::min(bmin,dot(p,axis));bmax=std::max(bmax,dot(p,axis));}
        if(std::min(amax,bmax)-std::max(amin,bmin)<=tolerance)return false;
    }return true;
}
bool cuts_face(Vec3 a,Vec3 b,const Facet& f,double tolerance) {
    const double da=dot(sub(a,f.folded[0]),f.normal),db=dot(sub(b,f.folded[0]),f.normal);
    if((da>tolerance&&db>tolerance)||(da<-tolerance&&db<-tolerance)||std::abs(da-db)<tolerance)return false;
    const double t=da/(da-db);if(t<-1e-9||t>1+1e-9)return false;
    const auto p=mix(a,b,std::clamp(t,0.,1.));
    for(std::size_t i=0;i<4;++i){const auto edge=sub(f.folded[(i+1)%4],f.folded[i]);if(dot(cross(edge,sub(p,f.folded[i])),f.normal)<-tolerance*norm(edge))return false;}
    return true;
}
bool intersects(const Facet& a,const Facet& b,double tolerance) {
    if(norm(cross(a.normal,b.normal))<1e-9&&std::abs(dot(sub(a.folded[0],b.folded[0]),a.normal))<tolerance)
        return overlaps(a.folded,b.folded,a.normal,tolerance);
    for(std::size_t i=0;i<4;++i)if(cuts_face(a.folded[i],a.folded[(i+1)%4],b,tolerance)||cuts_face(b.folded[i],b.folded[(i+1)%4],a,tolerance))return true;
    return false;
}
}
Vec3 QuarterArc::point(double t)const{return add(center,add(mul(first_axis,std::cos(t)),mul(second_axis,std::sin(t))));}
Vec3 QuarterArc::tangent(double t)const{return add(mul(first_axis,-std::sin(t)),mul(second_axis,std::cos(t)));}

Result calculate(const QuarterArc& input_a,const QuarterArc& input_b,const Options& options) {
    Result result;
    try {
        validate(input_a);validate(input_b);
        if(options.facets<2||options.facets>128||!std::isfinite(options.linear_tolerance)||options.linear_tolerance<1e-10||options.linear_tolerance>.01||
            !std::isfinite(options.deviation_resolution)||options.deviation_resolution<.001||options.deviation_resolution>1.)throw Failure::InvalidInput;
        // Translate to a nearby origin before triple products/plane intersections.
        QuarterArc a=input_a,b=input_b;const auto origin=a.center;a.center={};b.center=sub(b.center,origin);
        if(std::abs(residual(a,b,0,0))>1e-9||std::abs(residual(a,b,half_pi,half_pi))>1e-9)throw Failure::IncompatibleEndpoints;
        double previous=-1.;
        for(std::size_t i=0;i<=512;++i) {
            const double u=half_pi*i/512,v=corresponding(a,b,u);
            if(v<=previous||(!i&&v>1e-8)||(i==512&&std::abs(v-half_pi)>1e-8))throw Failure::NonMonotoneCorrespondence;
            previous=v;const auto d=sub(b.point(v),a.point(u));
            const auto na=cross(a.tangent(u),d),nb=cross(b.tangent(v),d);
            if(norm(na)<1e-10*norm(a.tangent(u))*norm(d)||norm(nb)<1e-10*norm(b.tangent(v))*norm(d)||dot(unit(na),unit(nb))<1-1e-8)throw Failure::SingularSurface;
            result.maximum_developability_residual=std::max(result.maximum_developability_residual,std::abs(residual(a,b,u,v)));
        }
        std::vector<Plane> planes;
        for(std::size_t i=0;i<options.facets;++i) {
            const double u=half_pi*i/(options.facets-1),v=corresponding(a,b,u);
            const auto n=unit(cross(a.tangent(u),sub(b.point(v),a.point(u))));planes.push_back({n,dot(n,a.point(u))});
        }
        std::vector<Vec3> A{a.point(0)},B{b.point(0)};
        for(std::size_t i=1;i<planes.size();++i){A.push_back(intersect(planes[i-1],planes[i],arc_plane(a)));B.push_back(intersect(planes[i-1],planes[i],arc_plane(b)));}
        A.push_back(a.point(half_pi));B.push_back(b.point(half_pi));
        Vec3 flat_a{},flat_b{norm(sub(B[0],A[0])),0,0},old_center{};
        for(std::size_t i=0;i<planes.size();++i) {
            Facet f;f.folded={A[i],B[i],B[i+1],A[i+1]};
            f.normal=unit(cross(sub(f.folded[1],f.folded[0]),sub(f.folded[2],f.folded[0])));
            if(!convex(f.folded,f.normal,options.linear_tolerance))throw Failure::InvalidFacet;
            for(auto p:f.folded)result.maximum_planarity_error=std::max(result.maximum_planarity_error,std::abs(dot(sub(p,f.folded[0]),f.normal)));
            if(result.maximum_planarity_error>options.linear_tolerance)throw Failure::InvalidFacet;
            const auto e=unit(sub(flat_b,flat_a));auto normal=Vec3{-e.y,e.x,0};
            if(i&&dot(sub(old_center,flat_a),normal)>0)normal=mul(normal,-1);
            const double length=norm(sub(B[i],A[i]));
            const auto place=[&](Vec3 p) {
                const double da=norm(sub(p,A[i])),db=norm(sub(p,B[i]));
                const double x=(da*da-db*db+length*length)/(2*length),height_squared=da*da-x*x;
                if(height_squared<-options.linear_tolerance*std::max(1.,da))throw Failure::InvalidFacet;
                return add(flat_a,add(mul(e,x),mul(normal,std::sqrt(std::max(0.,height_squared)))));
            };
            f.unfolded={flat_a,flat_b,place(B[i+1]),place(A[i+1])};
            old_center=mul(add(flat_a,flat_b),.5);flat_a=f.unfolded[3];flat_b=f.unfolded[2];
            for(std::size_t j=0;j<4;++j)for(std::size_t k=0;k<j;++k)result.maximum_metric_error=std::max(result.maximum_metric_error,
                std::abs(norm(sub(f.folded[j],f.folded[k]))-norm(sub(f.unfolded[j],f.unfolded[k]))));
            if(result.maximum_metric_error>options.linear_tolerance)throw Failure::MetricMismatch;
            result.folded_area+=area(f.folded);result.unfolded_area+=area(f.unfolded);
            for(std::size_t j=0;j<result.facets.size();++j) {
                if(overlaps(result.facets[j].unfolded,f.unfolded,{0,0,1},options.linear_tolerance))throw Failure::UnfoldedIntersection;
                if(j+1<i&&intersects(result.facets[j],f,options.linear_tolerance))throw Failure::FoldedIntersection;
            }
            if(i) {
                const auto before=result.facets.back().normal;
                result.folds.push_back({A[i],B[i],std::atan2(dot(unit(sub(B[i],A[i])),cross(before,f.normal)),dot(before,f.normal))});
            }
            result.facets.push_back(f);
        }
        result.boundary_deviation={deviation(a,A,options.deviation_resolution),deviation(b,B,options.deviation_resolution)};
        for(auto& f:result.facets)for(auto& p:f.folded)p=add(p,origin);
        for(auto& f:result.folds){f.first=add(f.first,origin);f.second=add(f.second,origin);}
    }catch(Failure failure){result.failure=failure;result.facets.clear();result.folds.clear();}
    return result;
}
}
