#pragma once
#include <zima/document/placement_surface.hpp>
#include <limits>
#include <tuple>

namespace zima::document::placement_mesh {
using kernel::Vec3;
inline Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 mul(Vec3 a,double k){return {a.x*k,a.y*k,a.z*k};}
inline double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline double square(Vec3 a){return dot(a,a);}
inline constexpr double tolerance=1e-7; // Constraint residual in document units, not mesh accuracy.
struct Triangle { std::array<Vec3,3> points; Vec3 normal; };
template<class F> void visit(const ConstructionReference& ref,
        const kernel::ViewerReferenceGeometry& g,F&& function) {
    for(std::size_t i=0;i<g.triangle_references.size();++i) {
        const auto& face=g.triangle_references[i];
        if(face.owner_id!=ref.owner_id || face.semantic_key!=ref.semantic_key ||
           face.instance_path!=ref.instance_path || i*3+2>=g.triangles.size())continue;
        Triangle t;bool valid=true;
        for(int j=0;j<3;++j) {
            const auto index=g.triangles[i*3+j];
            if(index>=g.vertices.size()){valid=false;break;}
            t.points[j]=g.vertices[index];
        }
        if(!valid)continue;
        t.normal=cross(sub(t.points[1],t.points[0]),sub(t.points[2],t.points[0]));
        const double length=std::sqrt(square(t.normal));
        if(!std::isfinite(length)||length<=1e-14)continue;
        t.normal=mul(t.normal,1/length);function(t);
    }
}
// Analytic faces and coplanar datum patches retain the established exact path.
// No triangle number becomes a reference identity. These are persisted source
// triangles, never the result-body mesh or a newly tessellated display cache.
inline bool is_surface(const ConstructionReference& ref,const kernel::ViewerReferenceGeometry& g) {
    if(ref.use_axis || placement_surface(ref,g))return false;
    std::optional<Triangle> first;bool curved=false;
    visit(ref,g,[&](const Triangle& t) {
        if(!first){first=t;return;}
        if(std::abs(dot(first->normal,t.normal))<1-1e-10)curved=true;
        for(auto p:t.points)if(std::abs(dot(sub(p,first->points[0]),first->normal))>tolerance)curved=true;
    });
    return curved;
}
inline Vec3 segment(Vec3 a,Vec3 b,Vec3 seed) {
    const auto d=sub(b,a);const double s=square(d);
    return add(a,mul(d,s>0?std::clamp(dot(sub(seed,a),d)/s,0.,1.):0.));
}
// Intersect a triangle with linear equality constraints, retaining its finite
// boundary. A fixed-size polygon avoids allocations in the triangle loop.
struct Polygon {std::array<Vec3,6> points{};std::size_t count{};};
inline Polygon intersect(Polygon p,Vec3 n,double rhs) {
    Polygon out;
    const auto append=[&](Vec3 v) {
        for(std::size_t k=0;k<out.count;++k)if(square(sub(out.points[k],v))<=tolerance*tolerance)return;
        if(out.count<out.points.size())out.points[out.count++]=v;
    };
    for(std::size_t i=0;i<p.count;++i) {
        const auto a=p.points[i],b=p.points[(i+1)%p.count];
        const double da=dot(n,a)-rhs,db=dot(n,b)-rhs;
        if(std::abs(da)<=tolerance)append(a);
        if((da>tolerance&&db<-tolerance)||(da<-tolerance&&db>tolerance))
            append(add(a,mul(sub(b,a),da/(da-db))));
    }
    return out;
}
inline Vec3 closest(const Polygon& p,Vec3 normal,Vec3 seed) {
    auto best=p.points[0];double distance=square(sub(best,seed));
    if(p.count>=3) {
        const auto foot=sub(seed,mul(normal,dot(sub(seed,p.points[0]),normal)));
        bool inside=true;
        for(std::size_t i=0;i<p.count;++i)
            if(dot(cross(sub(p.points[(i+1)%p.count],p.points[i]),sub(foot,p.points[i])),normal)<-1e-12)inside=false;
        if(inside)return foot;
    }
    for(std::size_t i=0;i<p.count;++i) {
        const auto q=segment(p.points[i],p.points[(i+1)%p.count],seed);
        const double d=square(sub(q,seed));if(d<distance){best=q;distance=d;}
    }
    return best;
}
inline std::optional<PlacementSurfacePoint> project(const ConstructionReference& ref,
        const kernel::ViewerReferenceGeometry& g,Vec3 seed) {
    std::optional<PlacementSurfacePoint> best;double distance=std::numeric_limits<double>::infinity();
    visit(ref,g,[&](const Triangle& t) {
        Polygon p;p.count=3;
        for(int i=0;i<3;++i)p.points[i]=add(t.points[i],mul(t.normal,ref.offset));
        const auto q=closest(p,t.normal,seed);const double d=square(sub(q,seed));
        if(d<distance || (d==distance && best && std::tie(t.normal.x,t.normal.y,t.normal.z)<
                std::tie(best->normal.x,best->normal.y,best->normal.z))) {
            distance=d;best=PlacementSurfacePoint{sub(q,mul(t.normal,ref.offset)),t.normal};
        }
    });
    return best;
}
} // namespace zima::document::placement_mesh
