#include <zima/symbols/placement.hpp>
#include "../modules/symbols/src/definition_cache.hpp"
#include <zima/kernel/annotation_layout.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <limits>
#include <cmath>
#include <chrono>
#include <bit>
using namespace zima;
namespace {
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid placement accepted");}
bool near(kernel::Vec3 a,kernel::Vec3 b){return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)<1e-8;}
}
int main(){try {
    {
        auto definition=symbols::projection_method();
        const auto source=definition.serialized();
        const auto first=symbols::detail::parsed_definition(source);
        check(first==symbols::detail::parsed_definition(source),"Identical symbol definition was reparsed");
        definition.insertion_point[0]=17;
        const auto edited=symbols::detail::parsed_definition(definition.serialized());
        check(edited!=first&&edited->insertion_point[0]==17&&first->insertion_point[0]==0,
            "Edited definition with the same ID reused stale parsed data");
        rejects([&]{static_cast<void>(symbols::detail::parsed_definition("{}"));});
        for(int index=0;index<20;++index) {
            definition.name="cache eviction "+std::to_string(index);
            static_cast<void>(symbols::detail::parsed_definition(definition.serialized()));
        }
        check(first!=symbols::detail::parsed_definition(source)&&first->serialized()==source,
            "Cache eviction retained every entry or invalidated a borrowed definition");
        const auto oversized=source+std::string(1024*1024,' ');
        check(symbols::detail::parsed_definition(oversized)!=symbols::detail::parsed_definition(oversized),
            "Oversized symbol source was retained in the cache");
    }
    auto definition=symbols::projection_method();definition.insertion_point={2,3};
    symbols::Placement p;p.symbol.id="annotation";p.symbol.definition=definition.serialized();p.symbol.variant=definition.default_variant;
    p.symbol.x=20;p.symbol.y=10;p.symbol.angle_degrees=90;p.symbol.scale=2;
    p.frame={{100,200,300},{0,1,0},{0,0,1}};
    p.reference=symbols::Reference{"part","face-owner","face:authored","assembly/occurrence",symbols::ReferenceKind::Face,true};
    p.leader=true;p.leader_bends={{5,8}};p.validate();
    double elapsed=0;std::uint64_t hash=1469598103934665603ULL;
    const auto mix=[&](std::uint64_t value){hash^=value;hash*=1099511628211ULL;};
    const auto text=[&](const std::string& value){for(unsigned char c:value)mix(c);mix(value.size());};
    for(int sample=0;sample<5;++sample) {
        const auto started=std::chrono::steady_clock::now();
        for(int repeat=0;repeat<100;++repeat) {
            const auto result=p.viewer_mesh(37.);
            mix(result.edges.size());
            for(const auto& edge:result.edges) {
                mix(edge.points.size());text(edge.color);text(edge.reference.owner_id);text(edge.reference.semantic_key);
                mix(edge.overlay);mix(edge.construction);mix(edge.dash_dot);mix(edge.infinite);
                for(auto point:edge.points)for(double value:{point.x,point.y,point.z})mix(std::bit_cast<std::uint64_t>(value));
                if(edge.annotation) {
                    const auto& a=*edge.annotation;
                    for(double value:{a.contact.x,a.contact.y,a.contact.z,a.grip.x,a.grip.y,a.grip.z,a.left,a.right,a.bottom,a.arrow_length,a.shelf_length})
                        mix(std::bit_cast<std::uint64_t>(value));
                    mix(a.role);mix(a.framed);mix(a.all_around);mix(a.short_shelf);
                }
            }
        }
        elapsed+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    }
    std::cout<<"Symbol leader 100 renders/sample, mean_ms="<<elapsed/5<<" geometry_hash="<<hash<<"\n";
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
    for(const auto ending:{symbols::LeaderEnding::Arrow,symbols::LeaderEnding::Triangle,symbols::LeaderEnding::Dot}) {
        p.leader_ending=ending;check(nlohmann::json(p).get<symbols::Placement>()==p,"Leader ending failed persistence");
        const auto strokes=p.viewer_mesh(0.);const int role=ending==symbols::LeaderEnding::Arrow?2:ending==symbols::LeaderEnding::Triangle?4:5;
        const auto retained=p.viewer_mesh(0.,true);
        const auto marker=std::ranges::find_if(retained.edges,[&](const auto& edge){return edge.annotation&&edge.annotation->role==role;});
        check(marker!=retained.edges.end(),"Leader ending has no rendered marker");
        check(marker->points.size()==(role==2?3:role==4?4:33),"Leader ending geometry differs");
        check(marker->filled_text==(role==4||role==5)&&marker->color=="#F5CD50","Triangle/dot must use a yellow fill");
        if(role!=2)check(marker->points.front()==marker->points.back(),"Closed leader marker is open");
    }
    p.leader_ending=symbols::LeaderEnding::Arrow;
    {
        auto triangle=p;triangle.frame={};triangle.symbol.x=20;triangle.symbol.y=15;
        triangle.leader_ending=symbols::LeaderEnding::Triangle;triangle.perpendicular_leader=false;
        triangle.paper_tangent=kernel::Vec3{1,1,0};
        const auto mesh=triangle.viewer_mesh(0.,true);
        const auto leader=std::ranges::find_if(mesh.edges,[](const auto& e){return e.annotation&&e.annotation->role==1;});
        const auto marker=std::ranges::find_if(mesh.edges,[](const auto& e){return e.annotation&&e.annotation->role==4;});
        const auto d=kernel::dimension_sub(leader->points.back(),leader->points.front());
        check(std::abs(d.x+d.y)<1e-8,"Triangle ignored mandatory perpendicularity on a Drawing edge");
        check(near(kernel::dimension_scale(kernel::dimension_add(marker->points[1],marker->points[2]),.5),triangle.frame.origin),"Triangle base is not centered on the entity");
        check(kernel::dimension_dot(kernel::dimension_sub(marker->points[0],triangle.frame.origin),d)>0,"Triangle apex was not reversed toward the leader");
        triangle.paper_tangent.reset();triangle.reference->kind=symbols::ReferenceKind::Edge;
        const auto spatial=triangle.viewer_mesh();
        const auto line=std::ranges::find_if(spatial.edges,[](const auto& e){return e.annotation&&e.annotation->role==1;});
        check(std::abs(line->points.back().x-line->points.front().x)<1e-8,"Triangle ignored mandatory perpendicularity in the model");
        for(bool reversed:{false,true}) {
            triangle.reference->kind=symbols::ReferenceKind::Face;triangle.reference->reversed=reversed;triangle.frame.y={0,reversed?-1.:1.,0};
            const auto side_mesh=triangle.viewer_mesh();
            const auto side_line=std::ranges::find_if(side_mesh.edges,[](const auto& e){return e.annotation&&e.annotation->role==1;});
            const auto direction=kernel::dimension_sub(side_line->points.back(),side_line->points.front());
            check(std::abs(direction.x)<1e-8&&direction.y*(reversed?-1.:1.)>0,"Triangle lost the selected face side at zero contact offset");
            check(nlohmann::json(triangle).get<symbols::Placement>()==triangle,"Triangle side choice failed persistence");
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
    check(p.frame.origin==outside.frame.origin&&near(p.frame.y,{0,-1,0}),"Opposite cylinder side merged at zero offset");
    surface=std::make_shared<kernel::SurfaceGeometry>(*surface);surface->radius=8;face.surface=surface;refs.triangle_references={face};
    check(symbols::refresh_surface_attachment(p,refs)&&near(p.frame.origin,{10,28,37}),"Changed cylinder radius left symbol inside face");
    check(nlohmann::json(p).get<symbols::Placement>()==p,"Analytic contact parameters did not persist");
    surface->kind=kernel::SurfaceGeometry::Kind::Cone;surface->radius=3;surface->semi_angle=std::atan(.5);
    symbols::attach_to_surface(p,face,"source-document",{15,20,34});
    check(near(p.frame.origin,{15,20,34}),"Cone contact incorrect");
    const auto cone=p;surface->kind=kernel::SurfaceGeometry::Kind::Cylinder;
    check(!symbols::refresh_surface_attachment(p,refs)&&p.frame==cone.frame,"Changed surface type silently reinterpreted attachment");
    auto roughness=definition;roughness.id="custom-company-symbol";
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
    {
        kernel::AnnotationStroke a;a.contact={0,0,0};a.grip={20,10,0};a.perpendicular=false;a.left=0;a.right=21;a.framed=true;a.shelf_length=3;
        const kernel::Vec3 right{1,0,0},up{0,1,0};
        for(bool left:{true,false}) {
            auto handles=kernel::annotation_handles(a,right,up,left,true);
            check(near(handles[2],{20+(left?-10.5:10.5),10,0}),"Tolerance landing misses the frame side midpoint");
            check(near(kernel::dimension_sub(handles[2],handles[1]),{left?3.:-3.,0,0}),"Tolerance has no horizontal landing outside its frame");
            a.role=1;const auto leader=kernel::annotation_stroke(a,right,up,left,true);
            check(near(leader.back(),handles[1]),"Leader/landing junction differs from the manipulation handle");
            const auto moved=kernel::drag_annotation(a,right,up,left,true,2,kernel::dimension_add(handles[2],{left?4.:-4.,0,0}));
            auto next=a;next.grip=moved.grip;next.shelf_length=moved.shelf_length;
            check(moved.shelf_length==7&&near(kernel::annotation_handles(next,right,up,left,true)[1],handles[1]),"Frame handle cannot resize landing without moving elbow");
        }
        a.framed=false;a.all_around=true;a.role=6;
        for(bool left:{true,false}) {
            const auto handles=kernel::annotation_handles(a,right,up,left,true);
            const auto circle=kernel::annotation_stroke(a,right,up,left,true);
            check(circle.size()==33&&near(circle.front(),circle.back()),"All-around circle is not closed");
            for(auto point:circle)check(std::abs(std::sqrt(kernel::dimension_dot(kernel::dimension_sub(point,handles[1]),kernel::dimension_sub(point,handles[1])))-1.25)<1e-8,"All-around circle is not at the junction");
        }
        p.weld_all_around=true;check(nlohmann::json(p).get<symbols::Placement>()==p,"All-around property failed persistence");
        check(std::ranges::none_of(p.viewer_mesh().edges,[](const auto& e){return e.annotation&&e.annotation->role==6;}),"Non-weld symbol acquired an all-around circle");
    }
    {
        kernel::AnnotationStroke a;a.role=1;a.contact={2,3,4};a.grip={14,19,4};
        a.direction_tip={3,4,5};a.left=0;a.right=10;
        for(int kind:{0,1,2})for(bool left:{false,true})for(bool short_shelf:{false,true}) {
            a.kind=kind;a.short_shelf=short_shelf;
            for(double angle:{0.,.3,1.,1.5707963267948966,2.4,3.14}) {
                const kernel::Vec3 right{std::cos(angle),std::sin(angle),0},up{0,0,1};
                const auto line=kernel::annotation_stroke(a,right,up,left,true,true);
                const auto delta=kernel::dimension_sub(line.back(),line.front());
                check(std::abs(std::sqrt(kernel::dimension_dot(delta,delta))-20)<1e-8,"Model leader length changed while orbiting");
                if(kind!=0)check(std::abs(kernel::dimension_dot(delta,right))<1e-8&&std::abs(delta.z)==20,"Edge/point model leader is not screen vertical");
                const auto handles=kernel::annotation_handles(a,right,up,left,true,true);
                check(near(handles[1],line.back()),"Fixed-length leader and elbow handle disagree");
                for(int handle:{1,2}) {
                    const auto unchanged=kernel::drag_annotation(a,right,up,left,true,handle,handles[handle],true);
                    check(near(unchanged.grip,a.grip)&&std::abs(unchanged.shelf_length-a.shelf_length)<1e-8,"Picking a fixed-length grip changes its stored placement");
                }
                const auto direction=kernel::dimension_scale(delta,1./20);
                const auto moved=kernel::drag_annotation(a,right,up,left,true,1,kernel::dimension_add(handles[1],kernel::dimension_scale(direction,7)),true);
                auto next=a;next.grip=moved.grip;
                const auto changed=kernel::annotation_stroke(next,right,up,left,true,true);
                check(near(changed.back(),kernel::dimension_add(line.back(),kernel::dimension_scale(direction,7))),"Dragging a model elbow does not adjust its fixed length");
                if(short_shelf) {
                    const auto resized=kernel::drag_annotation(a,right,up,left,true,2,kernel::dimension_add(handles[2],kernel::dimension_scale(right,left?4.:-4.)),true);
                    next=a;next.grip=resized.grip;next.shelf_length=resized.shelf_length;
                    check(std::abs(resized.shelf_length-7)<1e-8&&near(kernel::annotation_handles(next,right,up,left,true,true)[1],handles[1]),"Resizing a model shelf moved its elbow");
                }
            }
        }
        a.kind=1;a.short_shelf=false;
        const kernel::Vec3 right{1,0,0},up{0,1,0};
        const auto handles=kernel::annotation_handles(a,right,up,true,true,true);
        const auto moved=kernel::drag_annotation(a,right,up,true,true,1,kernel::dimension_add(handles[1],{-30,0,0}),true);
        const auto offset=kernel::dimension_sub(moved.grip,a.contact);
        check(offset.x<0&&std::abs(std::sqrt(kernel::dimension_dot(offset,offset))-20)<1e-8,"Horizontal drag cannot change the model shelf side at fixed length");
    }
    std::cout<<"Symbol placement contracts passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
