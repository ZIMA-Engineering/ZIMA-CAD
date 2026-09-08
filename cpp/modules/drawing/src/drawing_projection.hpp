#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace zima::drawing::detail {
// All projection consumes persisted display triangles/curves. No kernel calls
// and no triangle index is promoted to a persistent topology identity.
struct ProjectionVertex { Point2 p; double z; };
inline std::vector<ProjectedEdge> project_drawing_edges(
    const zima::kernel::ViewerMesh& mesh,const ProjectionCamera& camera) {
    const auto project=[&](const zima::kernel::Vec3& p) {
        const auto dot=[&](const auto& d){return p.x*d.x+p.y*d.y+p.z*d.z;};
        return ProjectionVertex{{dot(camera.horizontal),dot(camera.vertical)},dot(camera.depth)};
    };
    std::vector<ProjectionVertex> vertices;vertices.reserve(mesh.vertices.size());
    double extent=1;
    if(!mesh.vertices.empty()) {
        auto low=mesh.vertices.front(),high=low;
        for(const auto& p:mesh.vertices) {
            vertices.push_back(project(p));
            low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);
            high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);
        }
        extent=std::max({extent,high.x-low.x,high.y-low.y,high.z-low.z});
    }
    const double epsilon=1e-8*extent;
    const auto& h=camera.horizontal;const auto& v=camera.vertical;const auto& d=camera.depth;
    const double handedness=(h.y*v.z-h.z*v.y)*d.x+(h.z*v.x-h.x*v.z)*d.y+(h.x*v.y-h.y*v.x)*d.z;
    struct Triangle {std::array<ProjectionVertex,3> v;double determinant;double xmin{},xmax{},ymin{},ymax{};};
    std::vector<Triangle> triangles;
    using Key=std::tuple<long long,long long,long long>;
    struct Boundary {zima::kernel::Vec3 a,b;bool front{},back{};int count{};};
    std::map<std::pair<Key,Key>,Boundary> boundaries;
    const auto key=[&](const auto& p){return Key{std::llround(p.x/epsilon),std::llround(p.y/epsilon),std::llround(p.z/epsilon)};};
    for(std::size_t i=0;i+2<mesh.triangles.size();i+=3) {
        const auto ia=mesh.triangles[i],ib=mesh.triangles[i+1],ic=mesh.triangles[i+2];
        if(ia>=vertices.size()||ib>=vertices.size()||ic>=vertices.size())continue;
        Triangle t{{vertices[ia],vertices[ib],vertices[ic]},0};
        const auto& a=t.v[0].p;const auto& b=t.v[1].p;const auto& c=t.v[2].p;
        t.determinant=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
        t.xmin=std::min({a.x,b.x,c.x});t.xmax=std::max({a.x,b.x,c.x});
        t.ymin=std::min({a.y,b.y,c.y});t.ymax=std::max({a.y,b.y,c.y});
        if(std::abs(t.determinant)>epsilon*epsilon)triangles.push_back(t);
        const std::array ids{ia,ib,ic};
        for(int side=0;side<3;++side) {
            auto pa=mesh.vertices[ids[side]],pb=mesh.vertices[ids[(side+1)%3]];
            auto ka=key(pa),kb=key(pb);if(kb<ka){std::swap(ka,kb);std::swap(pa,pb);}
            auto& edge=boundaries[{ka,kb}];edge.a=pa;edge.b=pb;++edge.count;
            edge.front|=t.determinant*handedness>epsilon*epsilon;
            edge.back|=t.determinant*handedness<=epsilon*epsilon;
        }
    }
    std::vector<ProjectedEdge> result;
    const auto append=[&](const auto& points,const zima::kernel::EdgeReference& source,bool silhouette) {
        for(std::size_t segment=1;segment<points.size();++segment) {
            const auto a=project(points[segment-1]),b=project(points[segment]);
            if(std::hypot(b.p.x-a.p.x,b.p.y-a.p.y)<=epsilon)continue;
            std::vector<std::pair<double,double>> occluded;
            for(const auto& triangle:triangles) {
                if(std::max(a.p.x,b.p.x)<triangle.xmin || std::min(a.p.x,b.p.x)>triangle.xmax ||
                    std::max(a.p.y,b.p.y)<triangle.ymin || std::min(a.p.y,b.p.y)>triangle.ymax)continue;
                const auto weights=[&](Point2 p) {
                    const auto& v=triangle.v;
                    const double wb=((p.x-v[0].p.x)*(v[2].p.y-v[0].p.y)-(p.y-v[0].p.y)*(v[2].p.x-v[0].p.x))/triangle.determinant;
                    const double wc=((v[1].p.x-v[0].p.x)*(p.y-v[0].p.y)-(v[1].p.y-v[0].p.y)*(p.x-v[0].p.x))/triangle.determinant;
                    return std::array{1-wb-wc,wb,wc};
                };
                const auto wa=weights(a.p),wb=weights(b.p);
                double low=0,high=1;
                const auto clip=[&](double start,double end) {
                    if(start>=0&&end>=0)return true;
                    if(start<0&&end<0)return false;
                    const double t=start/(start-end);
                    if(start<0)low=std::max(low,t);else high=std::min(high,t);
                    return low<high;
                };
                bool intersects=true;
                for(int k=0;k<3;++k)intersects=intersects&&clip(wa[k],wb[k]);
                if(!intersects)continue;
                double za=-a.z-epsilon,zb=-b.z-epsilon;
                for(int k=0;k<3;++k){za+=wa[k]*triangle.v[k].z;zb+=wb[k]*triangle.v[k].z;}
                if(clip(za,zb)&&high-low>1e-10)occluded.push_back({low,high});
            }
            std::sort(occluded.begin(),occluded.end());
            std::vector<std::pair<double,double>> merged;
            for(const auto& interval:occluded) {
                if(!merged.empty()&&interval.first<=merged.back().second+1e-10)merged.back().second=std::max(merged.back().second,interval.second);
                else merged.push_back(interval);
            }
            const auto emit=[&](double start,double end,bool hidden) {
                if(end-start<=1e-10)return;
                const auto at=[&](double t){return Point2{a.p.x+(b.p.x-a.p.x)*t,a.p.y+(b.p.y-a.p.y)*t};};
                const auto first=at(start),last=at(end);
                if(!result.empty()&&result.back().source==source&&result.back().silhouette==silhouette&&result.back().hidden==hidden&&
                    std::hypot(result.back().points.back().x-first.x,result.back().points.back().y-first.y)<=epsilon)
                    result.back().points.push_back(last);
                else result.push_back({{first,last},source,hidden,silhouette});
            };
            double cursor=0;
            for(const auto& interval:merged){emit(cursor,interval.first,false);emit(interval.first,interval.second,true);cursor=interval.second;}
            emit(cursor,1,false);
        }
    };
    const auto& source=mesh.edges.empty()?mesh.original_references.edges:mesh.edges;
    for(const auto& edge:source)if(!edge.parameter_seam&&!edge.construction&&!edge.overlay)
        append(edge.points,edge.reference,false);
    for(const auto& [key,edge]:boundaries)if((edge.front&&edge.back)||edge.count==1)
        append(std::array{edge.a,edge.b},zima::kernel::EdgeReference{},true);
    return result;
}
} // namespace zima::drawing::detail
