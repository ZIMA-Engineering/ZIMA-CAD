#include <zima/symbols/placement.hpp>
#include <zima/kernel/annotation_layout.hpp>
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
    check(mesh.edges.size()==local.edges.size()+3,"Leader, arrow and shelf missing");
    check(mesh.edges.back().color=="#F5CD50","Shelf must use yellow pen");
    const auto shelf=mesh.edges.back().points;
    check(near(mesh.edges[local.edges.size()].points.front(),p.frame.origin),"Leader does not start at contact");
    check(near(mesh.edges[local.edges.size()].points.back(),shelf.front())||near(mesh.edges[local.edges.size()].points.back(),shelf.back()),"Leader does not end at shelf end");
    const auto info=*mesh.edges.back().annotation;
    for(auto right:{kernel::Vec3{1,0,0},kernel::Vec3{0,1,0},kernel::Vec3{-1,0,0}}) {
        const kernel::Vec3 up{0,0,1};
        const auto first=kernel::annotation_stroke(info,right,up,true,true),other=kernel::annotation_stroke(info,right,up,false,true);
        check(first==other,"Side change mirrored shelf");
        check(std::abs(std::hypot(first[1].x-first[0].x,first[1].y-first[0].y,first[1].z-first[0].z)-(info.right-info.left))<1e-8,"Shelf width differs from glyph width");
        for(const auto& source:mesh.edges)if(source.annotation&&source.annotation->role==0) {
            const auto a=kernel::annotation_stroke(*source.annotation,right,up,true,true),b=kernel::annotation_stroke(*source.annotation,right,up,false,true);
            check(a==b,"Side change mirrored glyph");
            for(auto point:a)check(kernel::dimension_dot(kernel::dimension_sub(point,info.grip),up)>-1e-8,"Content falls below shelf");
        }
    }
    p.offset_z=4.;check(nlohmann::json(p).get<symbols::Placement>()==p,"Spatial grip offset lost");p.offset_z=0;
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
    for(const auto& stroke:p.viewer_mesh().edges)for(auto point:stroke.points)check(std::isfinite(point.x)&&std::isfinite(point.y)&&std::isfinite(point.z),"Coincident contact and shelf center emitted invalid geometry");
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
    {
        kernel::ViewerEdge edge;edge.reference={"source","curve:original","occurrence"};edge.points={{0,0,0},{10,0,0}};
        symbols::attach_to_edge(p,edge,"part",{3,0,0});check(near(p.frame.origin,{3,0,0}),"Edge contact missed");
        kernel::ViewerReferenceGeometry geometry;edge.points[1]={20,0,0};geometry.edges={edge};
        check(symbols::refresh_surface_attachment(p,geometry)&&near(p.frame.origin,{6,0,0}),"Edge parameter failed regeneration");
        kernel::ViewerPoint point;point.reference={"source","point:original","occurrence"};point.position={2,3,4};
        symbols::attach_to_point(p,point,"part");geometry.points={point};
        check(symbols::refresh_surface_attachment(p,geometry),"Point contact did not resolve");
        const auto current=p.frame;geometry.points.clear();check(!symbols::refresh_surface_attachment(p,geometry)&&p.frame==current,"Missing point moved annotation");
        kernel::AnnotationStroke a;a.role=1;a.kind=0;a.contact={0,0,0};a.grip={20,10,5};a.direction_tip={0,0,-1};a.left=0;a.right=10;
        auto line=kernel::annotation_stroke(a,{1,0,0},{0,1,0},true,true);
        check(line[1].x==0&&line[1].y==0&&line[1].z<0,"Lower surface leader lost normal side");
        a.kind=1;a.direction_tip={1,0,0};line=kernel::annotation_stroke(a,{1,0,0},{0,1,0},true,true);
        check(std::abs(line[1].x)<1e-9,"Edge leader is not perpendicular");
        a.kind=2;line=kernel::annotation_stroke(a,{1,0,0},{0,1,0},true,true);
        check(line.size()==2&&std::abs(line[1].x)<1e-9&&line[1].y>0,"Point leader must be vertical without a diagonal connector");
        a.role=3;const auto shelf=kernel::annotation_stroke(a,{1,0,0},{0,1,0},true,true);
        check(near(line.back(),shelf.front()),"Perpendicular leader does not directly join shelf");
        a.role=1;a.perpendicular=false;line=kernel::annotation_stroke(a,{1,0,0},{0,1,0},true,true);
        check(line.size()==2&&near(line.back(),{15,10,5}),"Free leader gained an intermediate segment");
        const auto paper=p.viewer_mesh(0);check(std::ranges::none_of(paper.edges,[](const auto& e){return e.annotation.has_value();}),"Paper received camera-dependent strokes");
    }
    {
        kernel::AnnotationStroke a;a.contact={0,0,0};a.grip={20,10,0};a.perpendicular=false;a.left=-4;a.right=8;a.bottom=-2;a.short_shelf=true;a.shelf_length=3;
        const kernel::Vec3 right{1,0,0},up{0,1,0};
        for(bool left:{true,false}) {
            const auto handles=kernel::annotation_handles(a,right,up,left,true);
            check(near(handles[2],a.grip),"Short shelf must end at authored insertion point");
            check(std::abs(kernel::dimension_dot(kernel::dimension_sub(handles[2],handles[1]),right))==3,"Short shelf length is not 3 mm");
            a.role=0;a.local_points={{0,0,0},{2,1,0}};const auto glyph=kernel::annotation_stroke(a,right,up,left,true);
            check(near(glyph.front(),handles[2])&&near(kernel::dimension_sub(glyph.back(),glyph.front()),{2,1,0}),"Origin attachment moved or mirrored authored symbol");
            auto moved=kernel::drag_annotation(a,right,up,left,true,2,kernel::dimension_add(handles[1],{left?7.:-7.,9,0}));
            check(moved.shelf_length==7,"End grip did not resize short shelf");
            auto next=a;next.grip=moved.grip;next.shelf_length=moved.shelf_length;
            check(near(kernel::annotation_handles(next,right,up,left,true)[1],handles[1]),"Resizing moved elbow");
            moved=kernel::drag_annotation(a,right,up,left,true,1,kernel::dimension_add(handles[1],{2,4,0}));
            check(near(moved.grip,kernel::dimension_add(a.grip,{2,4,0}))&&moved.shelf_length==3,"Elbow drag did not translate shelf");
            next=a;next.short_shelf=false;const auto full=kernel::annotation_handles(next,right,up,left,true);
            moved=kernel::drag_annotation(next,right,up,left,true,2,kernel::dimension_add(full[2],{5,6,0}));
            check(near(moved.grip,kernel::dimension_add(a.grip,{5,6,0})),"Full-width shelf end did not translate symbol");
        }
        p.short_shelf=true;p.shelf_length=7.5;check(nlohmann::json(p).get<symbols::Placement>()==p,"Shelf settings did not persist");
        auto invalid=p;invalid.shelf_length=0;rejects([&]{invalid.validate();});
    }
    std::cout<<"Symbol placement contracts passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
