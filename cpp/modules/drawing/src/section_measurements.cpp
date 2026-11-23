#include "section_measurements.hpp"
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/kernel/dimension_layout.hpp>
#include <algorithm>
#include <map>
#include <limits>
#include <tuple>
#include <string_view>

namespace zima::drawing::detail {
namespace {
using V=kernel::Vec3;
// The source and patch references outlive these operation-local maps.
using Key=std::tuple<std::string_view,std::string_view,std::string_view>;
Key key(const kernel::FaceReference& r){return {r.owner_id,r.semantic_key,r.instance_path};}
std::string field(const std::string& s){return std::to_string(s.size())+":"+s;}
using kernel::dimension_add;using kernel::dimension_sub;using kernel::dimension_scale;
using kernel::dimension_dot;using kernel::dimension_cross;using kernel::dimension_unit;
double norm(V p){return std::sqrt(dimension_dot(p,p));}
struct Plane {V origin,normal;bool initialized{},planar{true};};
using SegmentKey=std::array<double,6>;
SegmentKey segment_key(V a,V b){
    if(std::tie(b.x,b.y,b.z)<std::tie(a.x,a.y,a.z))std::swap(a,b);
    return {a.x,a.y,a.z,b.x,b.y,b.z};
}
}
void capture_section_measurements(DrawingView& view,const kernel::ViewerMesh& source,
    document::SectionResult& cut,const document::SectionDefinition& section) {
    // A planar source face and a Section leg define a unique straight support.
    // Verify the complete face, not just a conveniently straight sampled cut of
    // a curved surface. No coordinates or tessellation indices enter identity.
    std::map<Key,Plane> planes;
    for(std::size_t i=0;i+2<source.triangles.size();i+=3) {
        if(i/3>=source.triangle_references.size())continue;
        const auto& ref=source.triangle_references[i/3];if(!ref.valid())continue;
        auto& plane=planes[key(ref)];if(!plane.planar)continue;
        const V a=source.vertices.at(source.triangles[i]),b=source.vertices.at(source.triangles[i+1]),c=source.vertices.at(source.triangles[i+2]);
        if(!plane.initialized) {
            const auto n=dimension_cross(dimension_sub(b,a),dimension_sub(c,a));if(norm(n)<1e-12)continue;
            plane={a,dimension_unit(n),true,true};
        }
        for(auto p:{a,b,c})if(std::abs(dimension_dot(dimension_sub(p,plane.origin),plane.normal))>1e-7)plane.planar=false;
    }
    auto measurements=*view.measurement_geometry;
    std::map<SegmentKey,kernel::EdgeReference> rim_references;
    for(const auto& patch:cut.patches) {
        std::map<std::string,V> legs;
        for(const auto& line:section.sketch.segments) {
            if(line.construction)continue;
            const auto* a=section.sketch.find_point(line.first_point_id);const auto* b=section.sketch.find_point(line.second_point_id);if(!a||!b)continue;
            const auto world=[&](const auto& p){return dimension_add(section.plane_origin,dimension_add(dimension_scale(section.plane_x,p.x),dimension_scale(section.plane_y,p.y)));};
            const auto start=world(*a),end=world(*b);
            if(std::abs(dimension_dot(dimension_sub(start,patch.frame.origin),patch.frame.normal))<1e-7&&
               std::abs(dimension_dot(dimension_sub(end,patch.frame.origin),patch.frame.normal))<1e-7)
                legs.emplace(line.id,dimension_sub(end,start));
        }
        if(legs.empty())continue;
        std::string leg_key;for(const auto& [id,direction]:legs)leg_key+=field(id);
        const auto leg_normal=dimension_cross(legs.begin()->second,dimension_cross(section.plane_x,section.plane_y));
        std::map<Key,std::vector<std::size_t>> groups;
        for(std::size_t i=0;i<patch.boundary.size()&&i<patch.boundary_sources.size();++i)
            if(patch.boundary_sources[i].valid())groups[key(patch.boundary_sources[i])].push_back(i);
        for(const auto& [identity,indices]:groups) {
            const auto found=planes.find(identity);if(found==planes.end()||!found->second.initialized||!found->second.planar)continue;
            const auto axis=dimension_cross(found->second.normal,leg_normal);if(norm(axis)<1e-12)continue;
            const auto direction=dimension_unit(axis),origin=patch.boundary[indices.front()][0];
            double low=std::numeric_limits<double>::infinity(),high=-low;bool straight=true;
            for(auto index:indices)for(const auto p:patch.boundary[index]) {
                const auto delta=dimension_sub(p,origin);
                if(norm(dimension_cross(delta,direction))>1e-7)straight=false;
                const double t=dimension_dot(delta,direction);low=std::min(low,t);high=std::max(high,t);
            }
            if(!straight||high-low<1e-9)continue;
            const auto& face=patch.boundary_sources[indices.front()];
            kernel::EdgeReference ref{face.owner_id,"drawing-section-line:"+field(section.id)+field(leg_key)+field(face.semantic_key),face.instance_path};
            MeasurementCurve curve;curve.source=ref;curve.line=true;
            curve.points={dimension_add(origin,dimension_scale(direction,low)),dimension_add(origin,dimension_scale(direction,high))};
            measurements.curves.push_back(std::move(curve));
            for(auto index:indices) {
                const auto& e=patch.boundary[index];auto [entry,inserted]=rim_references.try_emplace(segment_key(e[0],e[1]),ref);
                if(!inserted&&entry->second!=ref)entry->second={};
            }
        }
    }
    // Only the local Drawing projection acquires these references. Retain each
    // clipped fragment, including holes and disconnected material intervals.
    for(auto& edge:cut.mesh.edges)if(!edge.reference.valid()&&edge.points.size()==2)
        if(const auto found=rim_references.find(segment_key(edge.points[0],edge.points[1]));found!=rim_references.end())edge.reference=found->second;
    view.measurement_geometry=share_measurement_geometry(std::move(measurements));
}
}
