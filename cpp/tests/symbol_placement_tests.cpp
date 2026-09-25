#include <zima/symbols/placement.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <limits>
#include <cmath>
using namespace zima;
namespace {
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid placement accepted");}
bool near(kernel::Vec3 a,kernel::Vec3 b){return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)<1e-8;}
}
int main(){try {
    auto definition=symbols::projection_method();definition.insertion_point={2,3};
    symbols::Placement p;p.symbol.id="annotation";p.symbol.definition=definition.serialized();p.symbol.variant=definition.default_variant;
    p.symbol.x=20;p.symbol.y=10;p.symbol.angle_degrees=90;p.symbol.scale=2;
    p.frame={{100,200,300},{0,1,0},{0,0,1}};
    p.reference=symbols::Reference{"part","face-owner","face:authored","assembly/occurrence",symbols::ReferenceKind::Face,true};
    p.leader=true;p.leader_bends={{5,8}};p.validate();
    const auto local=symbols::instance_mesh(p.symbol),mesh=p.viewer_mesh();
    check(mesh.edges.size()==local.edges.size()+2,"Leader and arrow missing");
    check(mesh.edges[local.edges.size()].color=="#F5CD50"&&mesh.edges.back().color=="#F5CD50","Leader/arrow must share yellow pen");
    for(std::size_t i=0;i<local.edges.size();++i)for(std::size_t k=0;k<local.edges[i].points.size();++k)
        check(near(p.frame.local(mesh.edges[i].points[k]),local.edges[i].points[k]),"Rotated symbol frame distorted geometry");
    check(near(mesh.edges[local.edges.size()].points.front(),p.frame.origin),"Leader does not start at contact");
    check(near(mesh.edges[local.edges.size()].points.back(),p.frame.world({20,10,0})),"Leader does not end at grip");
    const auto original=p;const auto original_mesh=p.viewer_mesh();p.refresh_reference({});
    check(p.unresolved&&p.frame==original.frame&&p.reference==original.reference&&p.symbol==original.symbol,"Lost reference altered symbol");
    const auto unresolved_mesh=p.viewer_mesh();
    for(std::size_t i=0;i<original_mesh.edges.size();++i)check(original_mesh.edges[i].points==unresolved_mesh.edges[i].points,"Lost reference moved strokes");
    const auto encoded=nlohmann::json(p);check(encoded.get<symbols::Placement>()==p,"Unresolved reference did not round-trip");
    rejects([&]{static_cast<void>(symbols::placements_json({p,p}));});
    rejects([&]{static_cast<void>(symbols::placements_from_json(nlohmann::json::array({encoded,encoded})));});
    auto opposite=p;opposite.reference->reversed=false;
    check(nlohmann::json(opposite).get<symbols::Placement>()==opposite&&opposite.reference!=p.reference,
        "Zero-distance opposite contact sides were merged");
    auto moved=p.frame;moved.origin={-10,0,15};p.refresh_reference(moved);
    check(!p.unresolved&&p.frame==moved&&p.reference->reversed,"Resolved reference lost side or frame");
    auto invalid=p;invalid.frame.x={2,0,0};rejects([&]{invalid.validate();});
    invalid=p;invalid.frame.y=invalid.frame.x;rejects([&]{invalid.validate();});
    invalid=p;invalid.arrow_length=0;rejects([&]{invalid.validate();});
    invalid=p;invalid.leader_bends={{std::numeric_limits<double>::quiet_NaN(),0}};rejects([&]{invalid.validate();});
    auto bad=encoded;bad["reference"]["kind"]=100;rejects([&]{static_cast<void>(bad.get<symbols::Placement>());});
    invalid=p;moved.x={0,0,0};rejects([&]{invalid.refresh_reference(moved);});check(invalid==p,"Rejected resolution changed placement");
    p.symbol.visible=false;check(p.viewer_mesh().edges.empty(),"Hidden symbol retained leader");
    p.symbol.visible=true;p.symbol.x=p.symbol.y=0;p.leader_bends.clear();
    check(p.viewer_mesh().edges.size()==symbols::instance_mesh(p.symbol).edges.size(),"Zero-length leader emitted degenerate arrow");
    p.reference.reset();p.unresolved=false;p.refresh_reference({});check(!p.unresolved,"Free symbol became unresolved");
    auto surface=std::make_shared<kernel::SurfaceGeometry>();surface->origin={10,20,30};
    kernel::FaceReference face{"source","face:original","first-occurrence",surface};
    symbols::attach_to_surface(p,face,"source-document",{14,25,30});
    check(near(p.frame.origin,{14,25,30}),"Planar attachment lost picked contact");
    auto moved_surface=std::make_shared<kernel::SurfaceGeometry>(*surface);moved_surface->origin={20,40,60};
    moved_surface->axis={1,0,0};moved_surface->radial={0,1,0};face.surface=moved_surface;
    kernel::ViewerReferenceGeometry refs;refs.triangle_references={face};
    check(symbols::refresh_surface_attachment(p,refs)&&near(p.frame.origin,{20,44,65}),"Planar source rotation did not transport contact");
    const auto before_missing=p;refs.triangle_references.front().instance_path="other-occurrence";
    check(!symbols::refresh_surface_attachment(p,refs)&&p.unresolved&&p.frame==before_missing.frame,"Another occurrence captured lost reference");
    surface->kind=kernel::SurfaceGeometry::Kind::Cylinder;surface->radius=5;face.surface=surface;
    symbols::attach_to_surface(p,face,"source-document",{10,25,37});
    check(near(p.frame.origin,{10,25,37}),"Cylinder contact incorrect");
    const auto outside=p;symbols::attach_to_surface(p,face,"source-document",{10,25,37},true);
    check(p.frame.origin==outside.frame.origin&&near(p.frame.y,{0,0,-1}),"Opposite cylinder side merged at zero offset");
    surface=std::make_shared<kernel::SurfaceGeometry>(*surface);surface->radius=8;face.surface=surface;refs.triangle_references={face};
    check(symbols::refresh_surface_attachment(p,refs)&&near(p.frame.origin,{10,28,37}),"Changed cylinder radius left symbol inside face");
    check(nlohmann::json(p).get<symbols::Placement>()==p,"Analytic contact parameters did not persist");
    surface->kind=kernel::SurfaceGeometry::Kind::Cone;surface->radius=3;surface->semi_angle=std::atan(.5);
    symbols::attach_to_surface(p,face,"source-document",{15,20,34});
    check(near(p.frame.origin,{15,20,34}),"Cone contact incorrect");
    const auto cone=p;surface->kind=kernel::SurfaceGeometry::Kind::Cylinder;
    check(!symbols::refresh_surface_attachment(p,refs)&&p.frame==cone.frame,"Changed surface type silently reinterpreted attachment");
    auto roughness=definition;roughness.id="ze:surface-texture:iso21920";
    p.symbol.definition=roughness.serialized();
    for(const auto kind:{kernel::SurfaceGeometry::Kind::Plane,kernel::SurfaceGeometry::Kind::Cylinder,kernel::SurfaceGeometry::Kind::Cone}) {
        surface->kind=kind;surface->origin={0,0,0};surface->axis={0,0,1};surface->radial={1,0,0};surface->radius=5;surface->semi_angle=0;
        for(bool reversed:{false,true}) {
            symbols::attach_to_surface(p,face,"source-document",kind==kernel::SurfaceGeometry::Kind::Plane?kernel::Vec3{2,3,0}:kernel::Vec3{5,0,2},reversed);
            const auto expected=kind==kernel::SurfaceGeometry::Kind::Plane?kernel::Vec3{0,0,reversed?-1.:1.}:kernel::Vec3{reversed?-1.:1.,0,0};
            check(near(p.frame.y,expected),"Roughness plane is not perpendicular to contact surface/side");
            refs.triangle_references={face};const auto saved=p;
            check(symbols::refresh_surface_attachment(p,refs)&&p.frame==saved.frame,"Roughness refresh changed orientation");
            check(nlohmann::json(p).get<symbols::Placement>()==p,"Roughness orientation failed persistence");
        }
    }
    std::cout<<"Symbol placement contracts passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
