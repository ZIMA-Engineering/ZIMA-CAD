#include "transition_half.hpp"
#include <algorithm>

namespace zima::research::transition {
namespace {
constexpr double tolerance=1e-7;
Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 mul(Vec3 a,double t){return {a.x*t,a.y*t,a.z*t};}
double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double norm(Vec3 a){return std::sqrt(dot(a,a));}
Vec3 unit(Vec3 a){const auto n=norm(a);if(n<tolerance)throw Failure::InvalidFacet;return mul(a,1/n);}
using Polygon=std::vector<Vec3>;
double area(const Polygon& p){double result=0;for(std::size_t i=2;i<p.size();++i)result+=norm(cross(sub(p[i-1],p[0]),sub(p[i],p[0])))/2;return result;}
bool overlap(const Polygon& a,const Polygon& b,Vec3 normal) {
    for(const auto* poly:{&a,&b})for(std::size_t i=0;i<poly->size();++i) {
        const auto axis=unit(cross(normal,sub((*poly)[(i+1)%poly->size()],(*poly)[i])));
        double amin=1e100,amax=-1e100,bmin=1e100,bmax=-1e100;
        for(auto p:a){amin=std::min(amin,dot(p,axis));amax=std::max(amax,dot(p,axis));}
        for(auto p:b){bmin=std::min(bmin,dot(p,axis));bmax=std::max(bmax,dot(p,axis));}
        if(std::min(amax,bmax)-std::max(amin,bmin)<=tolerance)return false;
    }return true;
}
bool pierces(Vec3 a,Vec3 b,const HalfFace& f) {
    const double da=dot(sub(a,f.folded[0]),f.normal),db=dot(sub(b,f.folded[0]),f.normal);
    // Strict crossings; isolated non-coplanar tangential contacts are not certified.
    if((da>=-tolerance&&db>=-tolerance)||(da<=tolerance&&db<=tolerance))return false;
    const auto p=add(a,mul(sub(b,a),da/(da-db)));
    for(std::size_t i=0;i<f.folded.size();++i){const auto e=sub(f.folded[(i+1)%f.folded.size()],f.folded[i]);if(dot(cross(e,sub(p,f.folded[i])),f.normal)<-tolerance*norm(e))return false;}
    return true;
}
bool intersects(const HalfFace& a,const HalfFace& b) {
    if(norm(cross(a.normal,b.normal))<1e-9&&std::abs(dot(sub(a.folded[0],b.folded[0]),a.normal))<tolerance)
        return overlap(a.folded,b.folded,a.normal);
    for(std::size_t i=0;i<a.folded.size();++i)if(pierces(a.folded[i],a.folded[(i+1)%a.folded.size()],b))return true;
    for(std::size_t i=0;i<b.folded.size();++i)if(pierces(b.folded[i],b.folded[(i+1)%b.folded.size()],a))return true;
    return false;
}
QuarterArc transformed(QuarterArc arc,const Frame& frame){return {frame.point(arc.center),frame.direction(arc.first_axis),frame.direction(arc.second_axis)};}
}
HalfResult calculate(const HalfModel& model) {
    HalfResult result;
    try {
        if(!model.first_origin.valid()||!model.second_relative.valid())throw Failure::InvalidInput;
        for(double value:{model.radius,model.width,model.depth,model.corner_radius})
            if(!std::isfinite(value)||value<.001||value>10000)throw Failure::InvalidInput;
        const double r=model.corner_radius,w=model.width/2,h=model.depth/2,R=model.radius;
        if(r>=std::min(w,h)-tolerance)throw Failure::InvalidInput;
        const std::array<QuarterArc,2> top{{{{},{R,0,0},{0,R,0}},{{},{0,R,0},{-R,0,0}}}};
        const std::array<QuarterArc,2> bottom{{{{w-r,h-r,0},{r,0,0},{0,r,0}},{{-w+r,h-r,0},{0,r,0},{-r,0,0}}}};
        std::array<Result,2> corners;
        // Calculate near the first Origin, then move the verified result rigidly.
        for(std::size_t i=0;i<2;++i) {
            Options options;options.facets=model.corner_facets[i];
            corners[i]=calculate(top[i],transformed(bottom[i],model.second_relative),options);
            if(!corners[i].valid())throw corners[i].failure;
        }
        std::vector<Vec3> A{top[0].point(0)},B{model.second_relative.point({w,0,0})};
        for(const auto& corner:corners) {
            A.push_back(corner.facets.front().folded[0]);B.push_back(corner.facets.front().folded[1]);
            for(const auto& face:corner.facets){A.push_back(face.folded[3]);B.push_back(face.folded[2]);}
        }
        A.push_back(A.back());B.push_back(model.second_relative.point({-w,0,0}));
        Vec3 flat_a{},flat_b{norm(sub(B[0],A[0])),0,0},old_center{};
        for(std::size_t i=0;i+1<A.size();++i) {
            HalfFace face;face.folded={A[i],B[i],B[i+1],A[i+1]};
            if(norm(sub(face.folded.front(),face.folded.back()))<tolerance)face.folded.pop_back();
            face.normal=unit(cross(sub(face.folded[1],face.folded[0]),sub(face.folded[2],face.folded[0])));
            for(std::size_t j=0;j<face.folded.size();++j) {
                const auto p=face.folded[j],e=sub(face.folded[(j+1)%face.folded.size()],p),next=sub(face.folded[(j+2)%face.folded.size()],face.folded[(j+1)%face.folded.size()]);
                if(dot(cross(e,next),face.normal)<=tolerance*norm(e))throw Failure::InvalidFacet;
                result.maximum_planarity_error=std::max(result.maximum_planarity_error,std::abs(dot(sub(p,face.folded[0]),face.normal)));
            }
            if(result.maximum_planarity_error>tolerance)throw Failure::InvalidFacet;
            const auto direction=unit(sub(flat_b,flat_a));auto perpendicular=Vec3{-direction.y,direction.x,0};
            if(i&&dot(sub(old_center,flat_a),perpendicular)>0)perpendicular=mul(perpendicular,-1);
            const auto spatial_direction=unit(sub(B[i],A[i]));
            const auto spatial_perpendicular=unit(cross(face.normal,spatial_direction));
            const auto place=[&](Vec3 p){const auto delta=sub(p,A[i]);return add(flat_a,add(mul(direction,dot(delta,spatial_direction)),mul(perpendicular,dot(delta,spatial_perpendicular))));};
            for(auto p:face.folded)face.unfolded.push_back(place(p));
            const auto next_a=place(A[i+1]),next_b=place(B[i+1]);
            old_center=mul(add(flat_a,flat_b),.5);flat_a=next_a;flat_b=next_b;
            for(std::size_t j=0;j<face.folded.size();++j)for(std::size_t k=0;k<j;++k)
                result.maximum_metric_error=std::max(result.maximum_metric_error,std::abs(norm(sub(face.folded[j],face.folded[k]))-norm(sub(face.unfolded[j],face.unfolded[k]))));
            if(result.maximum_metric_error>tolerance)throw Failure::MetricMismatch;
            for(const auto& previous:result.faces) {
                if(overlap(previous.unfolded,face.unfolded,{0,0,1}))throw Failure::UnfoldedIntersection;
                if(intersects(previous,face))throw Failure::FoldedIntersection;
            }
            if(i) {const auto n=result.faces.back().normal;result.folds.push_back({A[i],B[i],std::atan2(dot(spatial_direction,cross(n,face.normal)),dot(n,face.normal))});}
            result.folded_area+=area(face.folded);result.unfolded_area+=area(face.unfolded);result.faces.push_back(std::move(face));
        }
        for(std::size_t boundary=0;boundary<2;++boundary)
            // The union can have closer points in another patch: only the maximum
            // upper bound carries over. Straight boundary pieces are exact.
            result.boundary_deviation[boundary]={0,std::max(corners[0].boundary_deviation[boundary].upper,corners[1].boundary_deviation[boundary].upper)};
        for(auto& face:result.faces){for(auto& p:face.folded)p=model.first_origin.point(p);face.normal=model.first_origin.direction(face.normal);}
        for(auto& fold:result.folds){fold.first=model.first_origin.point(fold.first);fold.second=model.first_origin.point(fold.second);}
    }catch(Failure failure){result.failure=failure;result.faces.clear();result.folds.clear();}
    return result;
}
kernel::ViewerMesh mesh(const HalfResult& result,bool unfolded) {
    kernel::ViewerMesh output;if(!result.valid())return output;
    for(const auto& face:result.faces) {
        const auto& points=unfolded?face.unfolded:face.folded;const auto start=static_cast<std::uint32_t>(output.vertices.size());
        output.vertices.insert(output.vertices.end(),points.begin(),points.end());
        for(std::uint32_t i=2;i<points.size();++i){output.triangles.insert(output.triangles.end(),{start,start+i-1,start+i});output.triangle_references.emplace_back();}
        for(std::size_t i=0;i<points.size();++i){kernel::ViewerEdge edge;edge.points={points[i],points[(i+1)%points.size()]};output.edges.push_back(std::move(edge));}
    }return output;
}
}
