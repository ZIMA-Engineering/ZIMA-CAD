#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <tuple>

namespace zima::drawing::detail {
// All projection consumes persisted display triangles/curves. No kernel calls
// and no triangle index is promoted to a persistent topology identity.
// Adjacent inward directions are opposite at a smooth surface transition.
// Incomplete or degenerate metadata is deliberately treated as a normal edge.
inline bool tangent_boundary(const zima::kernel::ViewerEdge& edge) {
    const auto& sides=edge.edge_treatment_side_directions;
    if(sides.size()!=2 || edge.points.size()<2 || sides[0].size()!=edge.points.size() || sides[1].size()!=edge.points.size())return false;
    for(std::size_t i=0;i<edge.points.size();++i) {
        const auto& a=sides[0][i];const auto& b=sides[1][i];
        const double aa=a.x*a.x+a.y*a.y+a.z*a.z,bb=b.x*b.x+b.y*b.y+b.z*b.z;
        const double dot=a.x*b.x+a.y*b.y+a.z*b.z;
        if(!(aa>1e-20 && bb>1e-20) || !std::isfinite(dot) || dot/std::sqrt(aa*bb)>-1.0+1e-8)return false;
    }
    return true;
}
struct ProjectionVertex { Point2 p; double z; };
inline std::vector<ProjectedEdge> project_drawing_edges(
    const zima::kernel::ViewerMesh& mesh,const ProjectionCamera& camera,bool silhouettes=true,bool section_caps=false) {
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
    const auto& source_edges=mesh.edges.empty()?mesh.original_references.edges:mesh.edges;
    const auto is_thread=[](const auto& ref){return ref.semantic_key.starts_with("thread:boundary:");};
    using Owner=std::pair<std::string,std::string>;
    const auto owner=[](const auto& ref){return Owner{ref.owner_id,ref.instance_path};};
    // Saved display meshes keep analytic surfaces in original references rather
    // than duplicating them on each result triangle and adjacent edge face.
    using FaceKey=std::tuple<std::string,std::string,std::string>;
    const auto face_key=[](const auto& ref){return FaceKey{ref.owner_id,ref.semantic_key,ref.instance_path};};
    std::map<FaceKey,const zima::kernel::SurfaceGeometry*> surfaces;
    for(const auto& ref:mesh.original_references.triangle_references)if(ref.surface)surfaces.try_emplace(face_key(ref),&*ref.surface);
    for(const auto& ref:mesh.triangle_references)if(ref.surface)surfaces.try_emplace(face_key(ref),&*ref.surface);
    const auto surface_for=[&](const auto& ref)->const zima::kernel::SurfaceGeometry*{
        if(ref.surface)return &*ref.surface;
        const auto found=surfaces.find(face_key(ref));return found==surfaces.end()?nullptr:found->second;
    };
    struct EndRing {Point2 center;double radius{},depth{};zima::kernel::EdgeReference source;};
    std::vector<EndRing> end_rings;
    std::set<Owner> axial_threads;
    // Recognize only a complete, planar, camera-normal circular boundary from
    // persisted viewer samples. Never infer topology identities or call OCCT.
    for(const auto& edge:source_edges) {
        const auto& semantic=edge.reference.semantic_key;
        if(semantic!="thread:boundary:nominal"&&semantic!="thread:boundary:root")continue;
        if(edge.points.size()<9)continue;
        const auto a=project(edge.points.front()),b=project(edge.points[edge.points.size()/3]),c=project(edge.points[2*edge.points.size()/3]);
        const auto last=project(edge.points.back());
        if(std::hypot(last.p.x-a.p.x,last.p.y-a.p.y)>epsilon||std::abs(last.z-a.z)>epsilon)continue;
        const double bx=b.p.x-a.p.x,by=b.p.y-a.p.y,cx=c.p.x-a.p.x,cy=c.p.y-a.p.y;
        const double det=2*(bx*cy-by*cx);if(std::abs(det)<epsilon*epsilon)continue;
        const double bb=bx*bx+by*by,cc=cx*cx+cy*cy;
        const Point2 center{a.p.x+(bb*cy-cc*by)/det,a.p.y+(bx*cc-cx*bb)/det};
        const double radius=std::hypot(a.p.x-center.x,a.p.y-center.y);
        if(radius<=epsilon)continue;
        if(!std::ranges::all_of(edge.points,[&](const auto& p){const auto q=project(p);return std::abs(q.z-a.z)<=epsilon&&std::abs(std::hypot(q.p.x-center.x,q.p.y-center.y)-radius)<=epsilon;}))continue;
        axial_threads.insert(owner(edge.reference));
        auto found=std::ranges::find_if(end_rings,[&](const auto& ring){return owner(ring.source)==owner(edge.reference)&&std::hypot(ring.center.x-center.x,ring.center.y-center.y)<=epsilon;});
        if(found==end_rings.end())end_rings.push_back({center,radius,a.z,edge.reference});
        else if(a.z>found->depth)*found={center,radius,a.z,edge.reference};
    }
    const auto& h=camera.horizontal;const auto& v=camera.vertical;const auto& d=camera.depth;
    const double handedness=(h.y*v.z-h.z*v.y)*d.x+(h.z*v.x-h.x*v.z)*d.y+(h.x*v.y-h.y*v.x)*d.z;
    struct Triangle {std::array<ProjectionVertex,3> v;double determinant;double xmin{},xmax{},ymin{},ymax{};const zima::kernel::FaceReference* source{};};
    std::vector<Triangle> triangles;
    using Key=std::tuple<long long,long long,long long>;
    struct Boundary {zima::kernel::Vec3 a,b;bool front{},back{};int count{};bool thread{};bool axial_surface{true};};
    std::map<std::tuple<Key,Key,Owner>,Boundary> boundaries;
    const auto key=[&](const auto& p){return Key{std::llround(p.x/epsilon),std::llround(p.y/epsilon),std::llround(p.z/epsilon)};};
    for(std::size_t i=0;i+2<mesh.triangles.size();i+=3) {
        const auto ia=mesh.triangles[i],ib=mesh.triangles[i+1],ic=mesh.triangles[i+2];
        if(ia>=vertices.size()||ib>=vertices.size()||ic>=vertices.size())continue;
        Triangle t{{vertices[ia],vertices[ib],vertices[ic]},0};
        const auto& a=t.v[0].p;const auto& b=t.v[1].p;const auto& c=t.v[2].p;
        t.determinant=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
        t.xmin=std::min({a.x,b.x,c.x});t.xmax=std::max({a.x,b.x,c.x});
        t.ymin=std::min({a.y,b.y,c.y});t.ymax=std::max({a.y,b.y,c.y});
        const auto* ref=i/3<mesh.triangle_references.size()?&mesh.triangle_references[i/3]:nullptr;
        t.source=ref;
        const bool thread=ref&&ref->semantic_key.starts_with("thread:surface:");
        // Technological thread surfaces are not solid material and cannot hide
        // the real bore/shaft or other conventional thread lines.
        if(!thread&&std::abs(t.determinant)>epsilon*epsilon)triangles.push_back(t);
        if(thread&&axial_threads.contains(owner(*ref)))continue;
        // Section caps use scan-strip triangulation with T-junctions. Their
        // complete rim is already explicit; unmatched internal strip edges
        // must not be promoted to visible silhouettes.
        if(section_caps && (i/3>=mesh.triangle_references.size() || !mesh.triangle_references[i/3].valid()))continue;
        const std::array ids{ia,ib,ic};
        for(int side=0;side<3;++side) {
            auto pa=mesh.vertices[ids[side]],pb=mesh.vertices[ids[(side+1)%3]];
            auto ka=key(pa),kb=key(pb);if(kb<ka){std::swap(ka,kb);std::swap(pa,pb);}
            auto& edge=boundaries[{ka,kb,thread?owner(*ref):Owner{}}];edge.a=pa;edge.b=pb;++edge.count;edge.thread=thread;
            // A cylinder/cone viewed along its analytic axis has no visible
            // generator. An unmatched mesh seam near a cone apex is not a
            // physical silhouette; its real rims are explicit viewer edges.
            const auto* surface=ref?surface_for(*ref):nullptr;
            const bool axial_surface=surface&&
                (surface->kind==zima::kernel::SurfaceGeometry::Kind::Cone||surface->kind==zima::kernel::SurfaceGeometry::Kind::Cylinder)&&
                std::abs(zima::kernel::dimension_dot(zima::kernel::dimension_unit(surface->axis),camera.depth))>1-1e-9;
            edge.axial_surface&=axial_surface;
            edge.front|=t.determinant*handedness>epsilon*epsilon;
            edge.back|=t.determinant*handedness<=epsilon*epsilon;
        }
    }
    std::vector<ProjectedEdge> result;

    std::vector<zima::kernel::EdgeReference> leadin_edges;
    for(const auto& edge:source_edges) {
        if(edge.points.size()<9||is_thread(edge.reference))continue;
        const auto first=project(edge.points.front()),last=project(edge.points.back());
        if(std::hypot(first.p.x-last.p.x,first.p.y-last.p.y)>epsilon||std::abs(first.z-last.z)>epsilon)continue;
        for(const auto& ring:end_rings){
            if(edge.reference.instance_path!=ring.source.instance_path)continue;
            const double radius=std::hypot(first.p.x-ring.center.x,first.p.y-ring.center.y);
            if(radius<=ring.radius+epsilon)continue;
            const bool circular=std::ranges::all_of(edge.points,[&](const auto& p){const auto q=project(p);return std::abs(q.z-first.z)<epsilon&&std::abs(std::hypot(q.p.x-ring.center.x,q.p.y-ring.center.y)-radius)<epsilon;});
            const bool conical=std::ranges::any_of(edge.edge_treatment_side_references,[&](const auto& face){
                const auto* resolved=surface_for(face);
                if(!resolved||resolved->kind!=zima::kernel::SurfaceGeometry::Kind::Cone)return false;
                const auto& surface=*resolved;const auto center=project(surface.origin);
                const auto alignment=zima::kernel::dimension_dot(surface.axis,camera.depth);
                const auto axial=(ring.depth-center.z)*alignment;
                return std::abs(alignment)>1-1e-8&&std::hypot(center.p.x-ring.center.x,center.p.y-ring.center.y)<epsilon&&
                    axial>=surface.axial_min-epsilon&&axial<=surface.axial_max+epsilon&&
                    std::abs(ring.radius-std::abs(surface.radius+axial*std::tan(surface.semi_angle)))<epsilon;
            });
            if(circular&&conical){leadin_edges.push_back(edge.reference);break;}
        }
    }
    const auto append=[&](const auto& points,const zima::kernel::EdgeReference& source,bool silhouette,bool tangent,bool thread=false,bool symbolic=false) {
        for(std::size_t segment=1;segment<points.size();++segment) {
            const auto a=project(points[segment-1]),b=project(points[segment]);
            if(std::hypot(b.p.x-a.p.x,b.p.y-a.p.y)<=epsilon)continue;
            std::vector<std::pair<double,double>> occluded;
            for(const auto& triangle:triangles) {
                // The analytic arc lies on its own entrance chamfer. Coarse
                // cone facets must not cover that exact boundary. Exempt only
                // the same occurrence's matching persisted conical surface;
                // every other solid remains a normal occluder.
                // The same applies to an axial thread's real bore rim: its
                // sampled polygon must not be occluded by its own cone facets.
                const auto* analytic=(symbolic||axial_threads.contains(owner(source)))&&triangle.source?surface_for(*triangle.source):nullptr;
                if(analytic&&owner(*triangle.source)==owner(source)) {
                    const auto& surface=*analytic;
                    if(surface.kind==zima::kernel::SurfaceGeometry::Kind::Cone) {
                        const auto on_surface=[&](const auto& p){
                            const double x=p.x-surface.origin.x,y=p.y-surface.origin.y,z=p.z-surface.origin.z;
                            const double axial=x*surface.axis.x+y*surface.axis.y+z*surface.axis.z;
                            const double radial=std::sqrt(std::max(0.0,x*x+y*y+z*z-axial*axial));
                            return axial>=surface.axial_min-epsilon&&axial<=surface.axial_max+epsilon&&
                                std::abs(radial-surface.radius-axial*std::tan(surface.semi_angle))<=epsilon;
                        };
                        if(on_surface(points[segment-1])&&on_surface(points[segment]))continue;
                    }
                }
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
                if(!result.empty()&&result.back().source==source&&result.back().silhouette==silhouette&&result.back().hidden==hidden&&result.back().tangent==tangent&&result.back().thread==thread&&
                    std::hypot(result.back().points.back().x-first.x,result.back().points.back().y-first.y)<=epsilon)
                    result.back().points.push_back(last);
                else result.push_back({{first,last},source,hidden,silhouette,tangent,false,0,thread,std::ranges::find(leadin_edges,source)!=leadin_edges.end()});
            };
            double cursor=0;
            for(const auto& interval:merged){emit(cursor,interval.first,false);emit(interval.first,interval.second,true);cursor=interval.second;}
            emit(cursor,1,false);
        }
    };
    for(const auto& edge:source_edges)if(!edge.parameter_seam&&!edge.construction&&!edge.overlay) {
        const bool thread=is_thread(edge.reference);
        if(thread&&axial_threads.contains(owner(edge.reference)))continue;
        append(edge.points,edge.reference,false,thread?false:tangent_boundary(edge),thread);
    }
    for(const auto& ring:end_rings) {
        std::vector<zima::kernel::Vec3> arc;
        // 280 degrees: both ends extend five degrees past the centre lines,
        // leaving the conventional opening in the upper-right quadrant.
        for(int step=0;step<=100;++step) {
            const double angle=(85.0+280.0*step/100)*std::numbers::pi/180;
            const double x=ring.center.x+ring.radius*std::cos(angle),y=ring.center.y+ring.radius*std::sin(angle);
            arc.push_back({h.x*x+v.x*y+d.x*ring.depth,h.y*x+v.y*y+d.y*ring.depth,h.z*x+v.z*y+d.z*ring.depth});
        }
        append(arc,ring.source,false,false,true,true);
    }
    if(silhouettes)for(const auto& [key,edge]:boundaries)if(!edge.axial_surface&&((edge.front&&edge.back)||edge.count==1)) {
        zima::kernel::EdgeReference reference;
        if(edge.thread)for(const auto& original:source_edges)
            if(is_thread(original.reference)&&owner(original.reference)==std::get<2>(key)&&
               (original.reference.semantic_key=="thread:boundary:nominal"||original.reference.semantic_key=="thread:boundary:root")) {
                reference=original.reference;break;
            }
        append(std::array{edge.a,edge.b},reference,true,false,edge.thread);
    }
    return result;
}
} // namespace zima::drawing::detail
