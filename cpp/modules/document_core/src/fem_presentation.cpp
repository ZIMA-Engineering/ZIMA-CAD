#include <zima/document/fem_presentation.hpp>
#include <zima/sketcher/sketch.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>
#include <limits>

namespace zima::document {
namespace {
using kernel::Vec3;
Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 mul(Vec3 a,double v){return {a.x*v,a.y*v,a.z*v};}
Vec3 sub(Vec3 a,Vec3 b){return add(a,mul(b,-1));}
Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec3 unit(Vec3 a){const double n=std::hypot(a.x,a.y,a.z);return n>1e-12?mul(a,1/n):Vec3{0,0,1};}
template<class R> bool matches(const FemRegionReference& a,const R& b){return a.owner_id==b.owner_id&&a.semantic_key==b.semantic_key&&a.instance_path==b.instance_path;}
Vec3 rotate(const Placement& p,Vec3 a){
    const double x=p.rotation_x*std::numbers::pi/180,y=p.rotation_y*std::numbers::pi/180,z=p.rotation_z*std::numbers::pi/180;
    a={a.x,std::cos(x)*a.y-std::sin(x)*a.z,std::sin(x)*a.y+std::cos(x)*a.z};
    a={std::cos(y)*a.x+std::sin(y)*a.z,a.y,-std::sin(y)*a.x+std::cos(y)*a.z};
    return {std::cos(z)*a.x-std::sin(z)*a.y,std::sin(z)*a.x+std::cos(z)*a.y,a.z};
}
struct Sample {Vec3 point,normal;};
std::vector<Sample> samples(const FemRegionReference& r,FemRegionKind kind,const kernel::ViewerMesh& source){
    std::vector<Sample> result;
    const auto from=[&](const auto& mesh){
        if(kind==FemRegionKind::Point)for(const auto& p:mesh.points)if(matches(r,p.reference)){result.push_back({p.position,{0,0,1}});break;}
        if(kind==FemRegionKind::Curve)for(const auto& e:mesh.edges)if(matches(r,e.reference)&&!e.points.empty()){
            result.push_back({e.points[e.points.size()/2],{0,0,1}});break;
        }
        if(kind==FemRegionKind::Surface) {
            std::vector<std::size_t> triangles;
            for(std::size_t i=0;i<mesh.triangle_references.size()&&3*i+2<mesh.triangles.size();++i)
                if(matches(r,mesh.triangle_references[i]))triangles.push_back(i);
            const std::size_t count=std::min<std::size_t>(4,triangles.size());
            for(std::size_t i=0;i<count;++i){const auto t=triangles[i*triangles.size()/count];
                const auto a=mesh.vertices.at(mesh.triangles[3*t]),b=mesh.vertices.at(mesh.triangles[3*t+1]),c=mesh.vertices.at(mesh.triangles[3*t+2]);
                result.push_back({mul(add(add(a,b),c),1./3),unit(cross(sub(b,a),sub(c,a)))});
            }
        }
    };
    from(source);if(result.empty())from(source.original_references);return result;
}
}
kernel::Vec3 fem_force_direction(const FemCondition& condition){
    return rotate(condition.placement,{0,0,1});
}
bool fem_region_reference_available(const FemRegionReference& r,FemRegionKind k,const kernel::ViewerMesh& source){return !samples(r,k,source).empty();}

kernel::ViewerMesh fem_condition_presentation(const FemCondition& value,const kernel::ViewerMesh& source,double size){
    kernel::ViewerMesh mesh;if(!value.visible)return mesh;
    const Vec3 origin{value.placement.x,value.placement.y,value.placement.z};
    const auto normal=fem_force_direction(value);
    std::vector<Sample> locations;bool broken=!value.placement.reference_valid;
    if(value.projected_sketch) {
        for(const auto& r:value.references)broken|=!fem_region_reference_available(r,FemRegionKind::Surface,source);
        if(!value.projected_sketch->project_to_body) {
            const auto placed=fem_condition_sketch(value);
            auto sketch=placed.viewer_mesh(false);
            for(auto& edge:sketch.edges) {
                if(value.kind==FemConditionKind::Pressure&&!edge.construction&&!edge.points.empty()&&locations.size()<4)
                    locations.push_back({edge.points[edge.points.size()/2],placed.normal()});
                edge.reference.owner_id=value.id;
                edge.reference.semantic_key="fem:region:"+edge.reference.semantic_key;
                edge.display_owner_id=value.id;
                edge.overlay=true;
                mesh.edges.push_back(std::move(edge));
            }
        }else if(value.kind==FemConditionKind::Pressure) {
            for(std::size_t i=0;i<value.references.size()&&i<value.projected_sketch->surface_seeds.size();++i) {
                const auto seed=value.projected_sketch->surface_seeds[i];
                std::optional<Sample> nearest;double distance=std::numeric_limits<double>::infinity();
                for(std::size_t t=0;t<source.triangle_references.size()&&3*t+2<source.triangles.size();++t) {
                    if(!matches(value.references[i],source.triangle_references[t]))continue;
                    const auto a=source.vertices.at(source.triangles[3*t]),b=source.vertices.at(source.triangles[3*t+1]),c=source.vertices.at(source.triangles[3*t+2]);
                    const auto delta=sub(mul(add(add(a,b),c),1./3),seed);
                    const double d=std::hypot(delta.x,delta.y,delta.z);
                    if(d<distance){distance=d;nearest=Sample{seed,unit(cross(sub(b,a),sub(c,a)))};}
                }
                if(nearest)locations.push_back(*nearest);
            }
        }
    } else for(const auto& r:value.references){auto points=samples(r,value.region,source);broken|=points.empty();locations.insert(locations.end(),points.begin(),points.end());}
    if(locations.empty())locations.push_back({origin,normal});
    const auto stroke=[&](std::initializer_list<Vec3> points){kernel::ViewerEdge e;e.reference={value.id,"fem:mark:"+std::to_string(mesh.edges.size()),{}};e.display_owner_id=value.id;e.overlay=true;e.points=points;mesh.edges.push_back(std::move(e));};
    const auto arrow=[&](Vec3 tip,Vec3 d){d=unit(d);const auto side=unit(cross(d,std::abs(d.z)<.9?Vec3{0,0,1}:Vec3{0,1,0}));
        const auto tail=add(tip,mul(d,-size)),head=add(tip,mul(d,-size*.25));
        stroke({tail,tip,add(head,mul(side,size*.12))});stroke({tip,add(head,mul(side,-size*.12))});return tail;
    };
    Vec3 label=locations.front().point;
    if(value.kind==FemConditionKind::Force){Vec3 center{};for(auto s:locations)center=add(center,s.point);center=mul(center,1./locations.size());label=arrow(center,mul(normal,value.force_N<0?-1:1));}
    else if(value.kind==FemConditionKind::Pressure){for(auto s:locations)label=arrow(s.point,mul(s.normal,value.pressure_reversed?1:-1));}
    else {
        for(auto s:locations)for(int i=0;i<3;++i)if(value.restrained[i]) {
            const auto d=rotate(value.placement,i==0?Vec3{1,0,0}:i==1?Vec3{0,1,0}:Vec3{0,0,1});
            const auto side=rotate(value.placement,i==0?Vec3{0,1,0}:i==1?Vec3{0,0,1}:Vec3{1,0,0});
            const auto center=add(s.point,mul(d,-size*.4));
            const auto a=add(center,mul(side,-size*.25)),b=add(center,mul(side,size*.25));
            stroke({s.point,a,b,s.point});
            for(int hatch=-2;hatch<=2;++hatch){auto p=add(center,mul(side,size*.1*hatch));stroke({p,add(add(p,mul(d,-size*.15)),mul(side,-size*.1))});}
        }
    }
    std::ostringstream text;text<<value.name;
    if(value.kind==FemConditionKind::Force)text<<" = "<<value.force_N<<" N";
    else if(value.kind==FemConditionKind::Pressure)text<<" = "<<value.pressure_MPa<<" MPa";
    else {text<<" (";for(int i=0;i<3;++i)if(value.restrained[i])text<<"U"<<char('X'+i)<<"=0 ";text<<")";}
    if(broken)text<<" !";
    mesh.constraint_markers.push_back({label,text.str(),{value.id,"fem:label",{}},{}});
    mesh.original_references.edges=mesh.edges;return mesh;
}
} // namespace zima::document
