#include "profile_command_fixture.hpp"
#include <zima/workspace/profile_operations.hpp>
#include "profile_solid_fixture.hpp"
#include <zima/document/part_document.hpp>
#include <zima/kernel/curve_evaluation.hpp>
#include <zima/kernel/curve_constraints.hpp>
#include <zima/sketcher/sketch.hpp>
#include <zima/command_host/host.hpp>
#include <zima/workspace/primitive_operations.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <zima/workspace/reference_sources.hpp>
#include <zima/workspace/sketch_properties.hpp>
#include <zima/document/sketch_placement.hpp>
#include <zima/document/placement_json.hpp>
#include <chrono>
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace zima;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(double a,double b,double tolerance=1e-6){if(!std::isfinite(a)||std::abs(a-b)>tolerance)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
double length(kernel::Vec3 v){return std::hypot(v.x,v.y,v.z);}
kernel::Vec3 front(const document::Placement& p) {
    const double r=std::numbers::pi/180,cx=std::cos(p.rotation_x*r),sx=std::sin(p.rotation_x*r),
        cy=std::cos(p.rotation_y*r),sy=std::sin(p.rotation_y*r),cz=std::cos(p.rotation_z*r),sz=std::sin(p.rotation_z*r);
    return {cz*sy*sx-sz*cx,sz*sy*sx+cz*cx,cy*sx};
}
kernel::Vec3 top(const document::Placement& p) {
    const double r=std::numbers::pi/180,cx=std::cos(p.rotation_x*r),sx=std::sin(p.rotation_x*r),
        cy=std::cos(p.rotation_y*r),sy=std::sin(p.rotation_y*r),cz=std::cos(p.rotation_z*r),sz=std::sin(p.rotation_z*r);
    return {cz*sy*cx+sz*sx,sz*sy*cx-cz*sx,cy*cx};
}
void curve_matrix() {
    for(bool transformed:{false,true}) {
        auto s=sketcher::Sketch::create_default();
        const std::vector<std::string> ids{s.add_segment(-10,2,15,8),s.add_circle(0,0,10),
            s.add_arc(0,0,10,0,0,10),s.add_ellipse(0,0,15,0,0,4),
            s.add_elliptical_arc(0,0,15,0,0,4,15,0,0,4),
            s.add_bspline({{-10,0},{-5,12},{4,-6},{15,3}})};
        if(transformed){s.resolved_origin={17,-9,4};s.resolved_x_axis={0,1,0};s.resolved_y_axis={0,0,1};s.resolved_normal={1,0,0};}
        auto g=s.placement_reference_geometry();
        for(const auto& edge:g.edges)for(double t:{.13,.43,.87})for(bool flip:{false,true}) {
            const auto p=kernel::bspline_value(*edge.exact_spline,t),d=kernel::bspline_derivative(*edge.exact_spline,t);
            g.points.push_back({p,{"anchor","point",{}}});
            document::Placement placed;placed.absolute_rotation_y=37;
            placed.references={{{},"anchor","point"},{{},s.id,edge.reference.semantic_key,0,false,"direction",true,true,flip}};
            check(document::resolve_placement(placed,g),"Curve/tangent matrix failed");
            near(placed.x,p.x);near(placed.y,p.y);near(placed.z,p.z);
            const auto normal=front(placed);
            near((normal.x*d.x+normal.y*d.y+normal.z*d.z)/length(d),flip?-1:1);
            check(document::point_constraint_remaining_dof(placed.references,g)==0,"Fixed curve anchor left translation free");
            check(document::orientation_constraint_remaining_dof(placed.references,g,true,p)==1,"One tangent did not leave one rotation");
            auto free=placed.references;free.erase(free.begin());free.front().orientation_only=false;
            check(document::point_constraint_state(free,g,p).remaining_dof==1,"Curve did not leave one position DOF");
            g.points.pop_back();
        }
    }
}
void surface_placement() {
    kernel::ViewerReferenceGeometry g;
    auto surface=std::make_shared<kernel::SurfaceGeometry>();
    surface->kind=kernel::SurfaceGeometry::Kind::Cylinder;
    surface->origin={3,-2,7};surface->radius=5;surface->axial_min=0;surface->axial_max=20;
    // Deliberately unrelated displayed triangle: analytical geometry owns
    // the constraint, including outside the face's axial trim.
    g.vertices={{100,0,0},{100,1,0},{100,0,1}};g.triangles={0,1,2};
    g.triangle_references.push_back({"source","face",{},surface});
    document::ConstructionReference ref{{},"source","face",0,true,"front",true};
    for(bool reversed:{false,true})for(bool flip:{false,true})for(double offset:{0.0,-0.0,2.0,-2.0}) {
        surface->reversed=reversed;ref.flip=flip;ref.offset=offset;
        document::Placement p;p.x=3;p.y=8;p.z=50;p.references={ref};
        check(document::resolve_placement(p,g),"Cylinder placement failed");
        near(p.x,3);near(p.y,-2+5+(reversed?-offset:offset));near(p.z,50);
        near(front(p).y,reversed!=flip?-1:1);
        check(document::point_constraint_remaining_dof(p.references,g,{p.x,p.y,p.z})==2,"Surface must leave two translations");
        check(document::orientation_constraint_remaining_dof(p.references,g,true,{p.x,p.y,p.z})==1,"Surface must leave one rotation");
        p.x=13;p.y=-2;
        check(document::resolve_placement(p,g),"Cylinder movement failed");
        near(front(p).x,reversed!=flip?-1:1);
        auto encoded=nlohmann::json(p);auto restored=encoded.get<document::Placement>();
        check(restored.references==p.references,"Surface side or mode lost in JSON");
        check(std::signbit(restored.references[0].offset)==std::signbit(offset),"Signed zero lost");
    }
    surface->reversed=false;ref.flip=false;ref.offset=0;ref.use_axis=true;ref.supports_offset=false;
    document::Placement p;p.x=100;p.y=40;p.z=50;p.references={ref};
    check(document::resolve_placement(p,g),"Cylinder axis placement failed");
    near(p.x,3);near(p.y,-2);near(p.z,50);near(front(p).z,1);
    check(document::point_constraint_remaining_dof(p.references,g,{p.x,p.y,p.z})==1,"Axis must leave one translation");
    check(nlohmann::json(p).get<document::Placement>().references[0].use_axis,"Axis mode lost");
    auto plane=std::make_shared<kernel::SurfaceGeometry>();plane->origin={3,-2,0};plane->axis={1,0,0};plane->radial={0,1,0};
    g.triangle_references.push_back({"plane","face",{},plane});g.triangles.insert(g.triangles.end(),{0,1,2});
    ref.use_axis=false;ref.supports_offset=true;p.references={ref,{{},"plane","face",0,true}};
    p.x=8;p.y=-2;
    check(document::resolve_placement(p,g),"Cylinder/plane intersection failed at singular initial tangent");
    near(p.x,3);near(std::hypot(p.x-3,p.y+2),5);near(p.z,50);
    const auto valid=p;plane->origin.x=30;
    check(!document::resolve_placement(p,g),"Contradictory surface references were accepted");
    near(p.x,valid.x);near(p.y,valid.y);near(p.z,valid.z);
    g.triangle_references.pop_back();g.triangles.resize(3);
    surface->kind=kernel::SurfaceGeometry::Kind::Cone;surface->semi_angle=std::atan(.5);
    ref.use_axis=false;ref.supports_offset=true;p.references={ref};p.x=13;p.y=-2;p.z=17;
    check(document::resolve_placement(p,g),"Cone placement failed");
    near(std::hypot(p.x-3,p.y+2),5+.5*(p.z-7));
    const auto cone_normal=front(p);near(cone_normal.x,1/std::sqrt(1.25));near(cone_normal.z,-.5/std::sqrt(1.25));
    g.triangle_references[0].surface.reset();
    g.vertices.insert(g.vertices.end(),{{0,0,0},{1,0,0},{0,1,0}});
    g.triangles.insert(g.triangles.end(),{3,4,5});g.triangle_references.push_back({"source","face",{}});
    const auto before=p;
    check(!document::resolve_placement(p,g),"Unsupported curved face accepted as first triangle");
    near(p.x,before.x);near(p.y,before.y);near(p.z,before.z);
}
void history() {
    auto doc=document::PartDocument::create_default();
    auto source=document::PartDocument::create_sketch_container();
    auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=source.id;
    const auto circle=sketch.add_circle(0,0,10);
    auto target=zima::test::rectangular_feature(doc,{100,80,50});
    const auto point=sketch.add_point(10,0);
    target.placement.references={{{},sketch.id,"point:"+point},
        {{},sketch.id,"circle:"+circle,0,false,"direction",true,true}};
    doc.sketches.insert(doc.sketches.begin(),sketch);doc.history={source,target};
    doc.history_order={{document::PartHistoryKind::Feature,source.id},{document::PartHistoryKind::Feature,target.id}};
    doc.resolve_constructions();
    check(doc.history.back().placement.reference_valid,"Earlier Sketch reference was not published");
    near(doc.history.back().placement.x,10);
    doc.history.front().placement.x=27;doc.history.front().placement.absolute_rotation_z=90;
    doc.resolve_constructions();
    check(doc.history.back().placement.reference_valid,"Moved source Sketch did not regenerate its dependent");
    near(doc.history.back().placement.x,27);near(doc.history.back().placement.y,10);
    const auto saved=doc.history.back().placement;
    doc.sketches.front().circles.clear();
    doc.resolve_constructions();
    check(!doc.history.back().placement.reference_valid,"Removed curve reference remained valid");
    near(doc.history.back().placement.x,saved.x);near(doc.history.back().placement.y,saved.y);
}
void third_direction() {
    auto source=sketcher::Sketch::create_default();
    const auto arc=source.add_arc(0,-10,0,0,-7.66044443118978,-3.572123903134605);
    auto geometry=source.placement_reference_geometry();
    const auto point=source.arcs.front().end_point_id;
    document::Placement two;
    two.references={{{},source.id,"point:"+point},{{},source.id,"arc:"+arc,0,false,"direction",true,true}};
    check(document::resolve_placement(two,geometry),"Third-reference baseline failed");
    const auto n=front(two);const kernel::Vec3 anchor{two.x,two.y,two.z};
    // An orientation-only straight reference supplies its direction, wherever
    // it lies. Its offset and length must not relocate the existing anchor.
    for(bool exact:{false,true})for(bool reverse:{false,true})for(double offset:{0.,100.}) {
        kernel::ViewerEdge line;line.reference={"remote","line",{}};
        line.points={{anchor.x+offset,anchor.y+50,anchor.z+20},{anchor.x+offset,anchor.y+50,anchor.z+30}};
        if(reverse)std::ranges::reverse(line.points);
        if(exact)line.exact_spline=kernel::BSplineGeometry{1,line.points,{0,0,1,1},{1,1}};
        auto g=geometry;g.edges.push_back(line);
        auto placed=two;placed.references.push_back({{},"remote","line",0,false,"direction",true,true});
        check(document::orientation_constraint_remaining_dof(placed.references,g,true,anchor)==0,"Third remote straight reference did not close roll DOF");
        check(document::resolve_placement(placed,g),"Third remote straight reference failed");
        near(placed.x,anchor.x);near(placed.y,anchor.y);near(placed.z,anchor.z);
        near(front(placed).x,n.x);near(front(placed).y,n.y);near(front(placed).z,n.z);
        near(top(placed).x,0);near(top(placed).y,0);near(top(placed).z,reverse?-1:1);
        const auto saved=placed;
        for(int i=0;i<10;++i){check(document::resolve_placement(placed,g),"Third-reference repeated solve failed");near(placed.rotation_x,saved.rotation_x);near(placed.rotation_y,saved.rotation_y);near(placed.rotation_z,saved.rotation_z);}
        // Constructions and ordinary feature containers share this contract.
        auto datum=document::PartDocument::create_construction(document::ConstructionKind::Plane);
        datum.references=placed.references;
        check(document::resolve_construction(datum,g),"Construction third remote direction failed");
        near(datum.origin.x,anchor.x);near(datum.origin.y,anchor.y);near(datum.origin.z,anchor.z);
    }
    for(int invalid:{0,1,2}) {
        auto g=geometry;kernel::ViewerEdge line;line.reference={"bad","line",{}};
        const kernel::Vec3 remote{anchor.x+100,anchor.y+100,anchor.z+100};
        line.points={remote,{remote.x+n.x*10,remote.y+n.y*10,remote.z+n.z*10}};
        if(invalid==1)line.points.back()=remote; // No direction.
        if(invalid==2)line.points.insert(line.points.begin()+1,{remote.x+10,remote.y-10,remote.z+10}); // Remote bend, no tangent at anchor.
        line.exact_spline=kernel::BSplineGeometry{1,line.points,invalid==2?std::vector<double>{0,0,.5,1,1}:std::vector<double>{0,0,1,1},std::vector<double>(line.points.size(),1)};
        g.edges.push_back(line);auto placed=two;placed.references.push_back({{},"bad","line",0,false,"direction",true,true});
        if (invalid == 0) {
            check(document::resolve_placement(placed,g),"A redundant parallel direction invalidated the anchor");
            check(document::orientation_constraint_remaining_dof(placed.references,g,true,anchor)==1,
                "A parallel direction consumed roll");
        } else check(!document::resolve_placement(placed,g),"Invalid third direction accepted");
        near(placed.x,two.x);near(placed.y,two.y);near(placed.z,two.z);
        near(placed.rotation_x,two.rotation_x);near(placed.rotation_y,two.rotation_y);near(placed.rotation_z,two.rotation_z);
    }
}
void ordered_frames() {
    auto doc = document::PartDocument::create_default();
    auto source = document::PartDocument::create_sketch_container();
    auto sketch = sketcher::Sketch::create_default(); sketch.owner_container_id = source.id;
    const auto segment = sketch.add_segment(-10,0,10,0);
    const auto anchor = sketch.add_point(10,0);
    auto g = doc.origin_viewer_mesh().original_references;
    const auto sg = sketch.placement_reference_geometry();
    g.edges.insert(g.edges.end(),sg.edges.begin(),sg.edges.end());
    g.points.insert(g.points.end(),sg.points.begin(),sg.points.end());
    auto remote=sketcher::Sketch::create_default();const auto remote_id=remote.add_segment(20,20,20,40);
    for(const auto origin:std::vector<kernel::Vec3>{{0,0,0},{0,25,0},{20,30,0},{30,50,0}}) {
        document::Placement placed;placed.x=origin.x;placed.y=origin.y;placed.z=origin.z;
        placed.references={{{},remote.id,"segment:"+remote_id,0,false,"front",true}};
        check(document::resolve_placement(placed,remote.placement_reference_geometry()),"Remote trimmed segment rejected initial attachment");
        near(placed.x,20);near(placed.y,std::clamp(origin.y,20.,40.));near(placed.z,0);
    }
    const document::ConstructionReference edge{{},sketch.id,"segment:"+segment,0,false,"front",true};
    const document::ConstructionReference face{{},doc.document_id+":origin","origin:plane:xy",0,true,"top",true};
    const document::ConstructionReference point{{},sketch.id,"point:"+anchor};
    const auto verify_frame = [&](const document::Placement& placed, kernel::Vec3 expected_front) {
        check(placed.reference_valid,"Ordered placement failed");
        near(placed.x,10);near(placed.y,0);near(placed.z,0);
        near(front(placed).x,expected_front.x);near(front(placed).y,expected_front.y);near(front(placed).z,expected_front.z);
        check(document::orientation_constraint_remaining_dof(placed.references,g,true,{10,0,0})==0,
            "Edge, incident plane and endpoint must have zero rotations");
    };
    for (bool plane_first : {false,true}) {
        document::Placement p;
        p.references = plane_first ? std::vector{face,edge,point} : std::vector{edge,face,point};
        document::normalize_container_front_references(p.references);
        check(document::resolve_placement(p,g),"FRONT/TOP sequence rejected");
        const kernel::Vec3 n = plane_first ? kernel::Vec3{0,0,1} : kernel::Vec3{1,0,0};
        verify_frame(p,n);
        // The real owned Sketch must use the identical frame during history
        // resolution; offsets and manual work-plane choices stay independent.
        for (bool automatic : {false,true}) {
            auto model=doc;auto target=document::PartDocument::create_sketch_container();target.placement=p;
            auto owned=sketcher::Sketch::create_default();owned.owner_container_id=target.id;
            owned.plane_auto=automatic;owned.plane=sketcher::SketchPlane::XY;owned.plane_offset=3;
            model.history={source,target};model.sketches={sketch,owned};
            model.history_order={{document::PartHistoryKind::Feature,source.id},{document::PartHistoryKind::Feature,target.id}};
            model.resolve_constructions();verify_frame(model.history.back().placement,n);
            const auto& resolved=model.sketches.back();
            const auto expected=automatic?n:top(model.history.back().placement);
            near(resolved.resolved_normal.x,expected.x);near(resolved.resolved_normal.y,expected.y);near(resolved.resolved_normal.z,expected.z);
            near(resolved.resolved_origin.x,10+3*expected.x);near(resolved.resolved_origin.y,3*expected.y);near(resolved.resolved_origin.z,3*expected.z);
        }
    }
    // Verify both sides of the 0.01 degree threshold, including antiparallel
    // directions, flips, retained free roll, and a usable third direction.
    for (double degrees : {0.,0.001,0.009,0.011,0.1,90.,179.989,179.991,180.}) {
        const double angle=degrees*std::numbers::pi/180;
        auto geometry=g;
        geometry.axes.push_back({{0,0,0},{std::cos(angle),std::sin(angle),0},100,{"test","second",{}}});
        geometry.axes.push_back({{0,0,0},{0,0,1},100,{"test","third",{}}});
        auto p=document::Placement{};p.absolute_rotation_y=37;
        p.references={edge,{{},"test","second",0,false,"top",true,true}};
        const bool independent=degrees>=0.01 && degrees<=179.99;
        check(document::resolve_placement(p,geometry),"Near-parallel reference rejected placement");
        near(front(p).x,1);near(front(p).y,0);near(front(p).z,0);
        check(document::orientation_constraint_remaining_dof(p.references,geometry,true)==(independent?0:1),
            "Frame and DOF disagree at angular limit");
        if(!independent)near(p.absolute_rotation_y,37);
        for(int i=0;i<20;++i){const auto before=p;check(document::resolve_placement(p,geometry),"Repeated frame failed");near(p.rotation_x,before.rotation_x);near(p.rotation_y,before.rotation_y);near(p.rotation_z,before.rotation_z);}
        p.references.push_back({{},"test","third",0,false,"top",true,true});
        check(document::resolve_placement(p,geometry)&&document::orientation_constraint_remaining_dof(p.references,geometry,true)==0,
            "Parallel second direction prevented a useful third direction");
    }
}
void verify() {
    auto sketch=sketcher::Sketch::create_default();
    const auto circle=sketch.add_circle(0,0,10);
    const auto geometry=sketch.placement_reference_geometry();
    const auto curve=*geometry.edges.front().exact_spline;
    for(double t:{0.,.1,.25,.48,.75,1.}) {
        const auto p=kernel::bspline_value(curve,t),d=kernel::bspline_derivative(curve,t);
        near(std::hypot(p.x,p.y),10);near(p.x*d.x+p.y*d.y,0);
        check(length(d)>1,"Exact circular tangent disappeared");
        const auto q=kernel::project_bspline(curve,p,t);
        near(q.squared_distance,0,1e-16);
    }
    for(const auto initial:std::vector<kernel::Vec3>{{14,0,3},{-8,9,-2},{0,-6,0},{0,0,0}}) {
        document::Placement p;p.x=initial.x;p.y=initial.y;p.z=initial.z;
        p.references={{{},sketch.id,"circle:"+circle,0,false,"front",true}};
        check(document::resolve_placement(p,geometry),"Circle placement failed");
        near(std::hypot(p.x,p.y),10);near(p.z,0);
        const auto before=p;check(document::resolve_placement(p,geometry),"Repeated circle placement failed");
        near(p.x,before.x);near(p.y,before.y);
    }
    kernel::Vec3 p{10,0,0};
    check(kernel::solve_curve_constraints({curve},{{{1,0,0},0}},p),"Circle/diametral-plane intersection failed");
    near(p.x,0);near(std::abs(p.y),10);near(p.z,0);
    const auto before=p;
    check(!kernel::solve_curve_constraints({curve},{{{1,0,0},20}},p),"Impossible circle/plane accepted");
    near(p.x,before.x);near(p.y,before.y);
    auto support=geometry;support.points.push_back({{10,0,0},{"anchor","point",{}}});
    document::Placement attached;
    attached.references={{{},"anchor","point"},{{},sketch.id,"circle:"+circle,0,false,"direction",true,true}};
    check(document::resolve_placement(attached,support),"Point plus circular tangent failed");
    near(attached.x,10);near(attached.y,0);near(attached.rotation_x,0);near(attached.rotation_z,0);
    support.points.back().position={11,0,0};const auto saved=attached;
    check(!document::resolve_placement(attached,support),"Off-curve anchor accepted for tangent placement");
    near(attached.x,saved.x);near(attached.rotation_z,saved.rotation_z);
}
void boundaries() {
    auto sketch=sketcher::Sketch::create_default();const auto id=sketch.add_segment(5,0,15,0);
    auto g=sketch.placement_reference_geometry();
    document::Placement p;p.x=10;p.references={{{},sketch.id,"segment:"+id}};
    check(document::resolve_placement(p,g),"Finite segment attachment failed");near(p.x,10);
    g.points.push_back({{20,0,0},{"anchor","point",{}}});
    p.references.push_back({{},"anchor","point"});
    check(!document::resolve_placement(p,g),"Trimmed segment extended past its endpoint");near(p.x,10);
    g.axes.push_back({{5,0,0},{1,0,0},10,{"axis","axis",{}}});p.references.front()={{},"axis","axis"};
    check(document::resolve_placement(p,g),"Explicit infinite Axis lost its meaning");near(p.x,20);
    auto circle=sketcher::Sketch::create_default();const auto cid=circle.add_circle(0,0,10);
    g=circle.placement_reference_geometry();p={};p.x=10;p.references={{{},circle.id,"circle:"+cid,0,false,"front",true}};
    check(document::resolve_placement(p,g),"Circular initial position failed");
    check(workspace::assign_placement_dimension(p,g,"y",6),"Free curve coordinate is not editable");
    check(document::resolve_placement(p,g),"Circular coordinate edit failed");near(p.x,8);near(p.y,6);
    check(!workspace::assign_placement_dimension(p,g,"z",2),"Constrained coordinate became editable");
    const auto before=p;
    check(!workspace::assign_placement_dimension(p,g,"y",21) && p==before,"Out-of-range coordinate changed the curve attachment");
    check(workspace::assign_placement_dimension(p,g,"rotation_y",35),"Remaining rotation became uneditable");
    check(document::resolve_placement(p,g),"Free rotation invalidated circular placement");
    near(p.y,6);near(std::hypot(p.x,p.y),10);
    // Reject ambiguous tangents at a polyline corner and a self crossing.
    for(const auto& curve:std::vector<kernel::BSplineGeometry>{
        {1,{{-10,0,0},{0,0,0},{0,10,0}},{0,0,.5,1,1},{1,1,1}},
        {1,{{-10,-10,0},{10,10,0},{-10,10,0},{10,-10,0}},{0,0,.33,.66,1,1},{1,1,1,1}}}) {
        g={};kernel::ViewerEdge edge;edge.reference={"curve","edge",{}};edge.points=curve.poles;edge.exact_spline=curve;g.edges.push_back(edge);
        g.points.push_back({{0,0,0},{"anchor","point",{}}});
        p={};p.x=42;p.references={{{},"anchor","point"},{{},"curve","edge",0,false,"direction",true,true}};
        check(!document::resolve_placement(p,g),"Ambiguous curve tangent accepted");near(p.x,42);
    }
}
void native_transactions(const std::filesystem::path& file) {
    auto doc=document::PartDocument::create_default();const auto document_id=doc.document_id;
    auto source=document::PartDocument::create_sketch_container();
    auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=source.id;
    const auto circle=sketch.add_circle(0,0,10),point=sketch.add_point(10,0);
    doc.sketches={sketch};doc.history={source};doc.history_order={{document::PartHistoryKind::Feature,source.id}};
    doc.resolve_constructions();
    workspace::Workspace live;live.add_part(doc,{},file);live.activate(document_id);kernel::OcctKernel kernel;
    auto fixture=doc;auto box=zima::test::rectangular_feature(fixture);const auto target=box.id;
    workspace::commit_profile(live,kernel,document_id,box,workspace::ProfileEditMode::Create,fixture.sketches.back());
    auto working_directory=file.parent_path();command_host::Host host(live,kernel,working_directory);
    const auto run=[&](const char* name,commands::Json args=commands::Json::object()) {
        auto result=host.execute({{"command",name},{"arguments",args}});
        if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);
        return result.data;
    };
    const auto request=[&](int index,const std::string& key){return commands::Json{{"object",target},{"index",index},{"reference",{{"owner",sketch.id},{"key",key}}}};};
    const auto current=[&] {return *live.open_part(document_id)->session.document().find_container(target);};
    run("placement.reference.set",request(0,"point:"+point));
    const auto anchor=current();near(anchor.placement.x,10);
    run("placement.reference.set",request(1,"circle:"+circle));
    const auto tangent=current();near(front(tangent.placement).y,1);
    run("undo");check(current()==anchor,"Tangent assignment is not one Undo step");
    run("redo");check(current()==tangent,"Tangent Redo changed its native identity");
    const auto revision=live.open_part(document_id)->session.revision();
    const auto rejected=host.execute({{"command","placement.reference.set"},{"arguments",request(0,"point:missing")}});
    check(!rejected.ok && current()==tangent && revision==live.open_part(document_id)->session.revision(),"Invalid curve assignment changed committed state");
    run("save");std::vector<kernel::BodyResult> cached;
    auto loaded=document::PartDocument::load(file,&cached);
    check(*loaded.find_container(target)==tangent && !cached.empty(),"Native roundtrip lost dependent frame or cache");
    loaded.resolve_constructions(cached.back().mesh.original_references);
    near(loaded.find_container(target)->placement.x,10);near(front(loaded.find_container(target)->placement).y,1);
    // Query and assignment use the persisted native Sketch, with no new kernel evaluation.
    const auto geometry=loaded.sketch_placement_reference_geometry(target);
    check(geometry.edges.size()==1,"Saved native source curve was not queryable");
    const auto reference=geometry.edges.front().reference;
    kernel::ViewerReferenceGeometry occurrences;
    workspace::ReferenceFrame frame;frame.instance_prefix="first/";
    workspace::append_original_reference_geometry(occurrences,geometry,frame,[](auto,const auto&,const auto&,const auto&){return true;});
    frame.instance_prefix="second/";
    workspace::append_original_reference_geometry(occurrences,geometry,frame,[](auto,const auto&,const auto&,const auto&){return true;});
    check(occurrences.edges.size()==2 && occurrences.edges[0].reference.instance_path!=occurrences.edges[1].reference.instance_path &&
        occurrences.edges[0].reference.owner_id==reference.owner_id,"Repeated occurrence identity collapsed");
}
void solid_edges(const std::filesystem::path& file) {
    auto doc=document::PartDocument::create_default();auto cylinder=zima::test::circular_feature(doc,40,50);
    cylinder.placement.x=31;cylinder.placement.y=-17;cylinder.placement.z=5;
    cylinder.placement.absolute_rotation_x=35;cylinder.placement.rotation_x=35;
    doc.history.push_back(cylinder);doc.resolve_constructions();
    kernel::OcctKernel kernel;auto result=kernel.evaluate_history(doc.kernel_operations());
    const auto& geometry=result.back().mesh.original_references;unsigned curved=0;
    for(const auto& edge:geometry.edges) {
        check(edge.exact_spline.has_value(),"Calculated original edge has no exact persisted curve");
        for(const auto& sample:edge.points)near(kernel::project_bspline(*edge.exact_spline,sample).squared_distance,0,1e-10);
        if(edge.exact_spline->degree<2)continue;++curved;
        for(double t:{0.,.17,.5,.83,1.}) {
            const auto point=kernel::bspline_value(*edge.exact_spline,t);auto references=geometry;
            references.points.push_back({point,{"anchor","point",{}}});
            document::Placement p;p.references={{{},"anchor","point"},{{},edge.reference.owner_id,edge.reference.semantic_key,0,false,"direction",true,true}};
            check(document::resolve_placement(p,references),"Located solid circular edge failed tangent placement");
            near(p.x,point.x);near(p.y,point.y);near(p.z,point.z);
        }
    }
    check(curved>=2,"Cylinder fixture omitted circular edges");
    doc.save(file,result);std::vector<kernel::BodyResult> reopened;static_cast<void>(document::PartDocument::load(file,&reopened));
    check(reopened.back().mesh.original_references.edges.front().exact_spline==geometry.edges.front().exact_spline,"Exact edge packet did not survive native save/reopen");
}
void third_direction_transactions(const std::filesystem::path& file) {
    for(bool transformed:{false,true}) {
        auto doc=document::PartDocument::create_default();
        auto box=zima::test::rectangular_feature(doc,{100,80,50});box.placement.x=100;
        if(transformed){box.placement.absolute_rotation_x=23;box.placement.absolute_rotation_z=41;}
        auto source=document::PartDocument::create_sketch_container();
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=source.id;
        const auto arc=sketch.add_arc(0,-10,0,0,-7.66044443118978,-3.572123903134605);
        const auto line=sketch.add_segment(25,30,40,30);
        if(transformed){source.placement.x=17;source.placement.absolute_rotation_x=32;source.placement.absolute_rotation_z=-19;}
        doc.history={box,source};doc.sketches.push_back(sketch);
        doc.history_order={{document::PartHistoryKind::Feature,box.id},{document::PartHistoryKind::Feature,source.id}};
        doc.resolve_constructions();kernel::OcctKernel kernel;
        const auto cache=kernel.evaluate_history(doc.kernel_operations());
        check(!cache.empty(),"Third-direction box calculation missing");
        workspace::Workspace live;const auto id=doc.document_id;live.add_part(doc,cache);live.activate(id);
        auto target=document::PartDocument::create_sketch_container();auto pending=sketcher::Sketch::create_default();pending.owner_container_id=target.id;
        document::Placement placement;placement.references={{{},sketch.id,"point:"+sketch.arcs.front().end_point_id},{{},sketch.id,"arc:"+arc,0,false,"direction",true,true}};
        check(workspace::commit_part_sketch_properties(live,kernel,id,pending,placement,target),"Third-direction Sketch creation failed");
        const auto baseline=live.open_part(id)->session.document().find_container(target.id)->placement;
        const auto expected_front=front(baseline);
        auto directory=file.parent_path();command_host::Host host(live,kernel,directory);
        const auto run=[&](const char* name,commands::Json args=commands::Json::object()) {
            const auto result=host.execute({{"command",name},{"arguments",args}});
            if(!result.ok)throw std::runtime_error(std::string("Third direction ")+name+": "+result.message);
        };
        std::vector<kernel::EdgeReference> references{{sketch.id,"segment:"+line,{}}};
        for(const auto& edge:cache.back().mesh.original_references.edges)references.push_back(edge.reference);
        const auto verify=[&](const document::PartDocument& current) {
            const auto& p=current.find_container(target.id)->placement;
            check(p.reference_valid,"Third-direction native Sketch became invalid");
            near(p.x,baseline.x);near(p.y,baseline.y);near(p.z,baseline.z);
            near(front(p).x,expected_front.x);near(front(p).y,expected_front.y);near(front(p).z,expected_front.z);
            const auto s=std::ranges::find(current.sketches,pending.id,&sketcher::Sketch::id);
            near(std::abs(s->resolved_normal.x*expected_front.x+s->resolved_normal.y*expected_front.y+s->resolved_normal.z*expected_front.z),1);
        };
        for(const auto& reference:references) {
            run("placement.reference.set",{{"object",target.id},{"index",2},{"reference",{{"owner",reference.owner_id},{"key",reference.semantic_key}}}});
            verify(live.open_part(id)->session.document());
            run("undo");verify(live.open_part(id)->session.document());run("redo");verify(live.open_part(id)->session.document());
            const auto& current=live.open_part(id)->session.document();current.save(file,cache);
            std::vector<kernel::BodyResult> reopened;auto loaded=document::PartDocument::load(file,&reopened);
            loaded.resolve_constructions(reopened.back().mesh.original_references);verify(loaded);
        }
        check(references.size()>=13,"Box fixture omitted original edges");
    }
}
void body_history() {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    const auto templates=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/templates";
    options.settings=[templates]{return command_host::Settings{{templates,"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
    auto directory=std::filesystem::temp_directory_path();command_host::Host host(live,kernel,directory,options);
    const auto run=[&](const char* name,commands::Json args=commands::Json::object()) {
        const auto result=host.execute({{"command",name},{"arguments",args}});
        if(!result.ok)throw std::runtime_error(std::string("Cross-Body ")+name+": "+result.code+": "+result.message);
        return result.data;
    };
    run("new",{{"type","part"},{"name","Curve references across Bodies"}});
    const auto id=live.active_document_id();const auto first=live.open_part(id)->session.document().body_history.active_body_id();
    const auto sketch=run("sketch.create",{{"name","Source curve"}}).at("sketch").get<std::string>();
    const auto circle=run("sketch.circle.create",{{"sketch",sketch},{"center",{0,0}},{"radius_mm",10}}).at("geometry").get<std::string>();
    const auto point=run("sketch.point.create",{{"sketch",sketch},{"position",{10,0}}}).at("point").get<std::string>();
    run("placement.set",{{"object",first},{"values",{{"reference_offset:0",11}}}});
    const auto second=run("body.create",{{"name","Dependent body"}}).at("body").get<std::string>();
    run("placement.set",{{"object",second},{"values",{{"reference_offset:0",30}}}});
    const auto box=zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(n,std::move(a));},{{"length_mm","1"},{"width_mm","1"},{"height_mm","1"}}).at("container").get<std::string>();
    const auto request=[&](const std::string& object,int index,const std::string& key) {
        return commands::Json{{"object",object},{"index",index},{"reference",{{"owner",sketch},{"key",key}}}};
    };
    run("placement.reference.set",request(box,0,"point:"+point));
    run("placement.reference.set",request(box,1,"circle:"+circle));
    const auto position=[&] {
        const auto& doc=live.open_part(id)->session.document();const auto p=doc.find_container(box)->placement;
        const auto origin=doc.body_history.find(second)->scope.translation();return kernel::Vec3{p.x+origin.x,p.y+origin.y,p.z+origin.z};
    };
    near(position().x,10);near(position().z,11);
    run("placement.set",{{"object",first},{"values",{{"reference_offset:0",17}}}});
    near(position().x,10);near(position().z,17);
    // The body frame itself can consume an earlier body's native Sketch.
    // Its initial X/Y datum planes would conflict with that point. A point
    // must not silently discard the user's other existing position rows.
    const auto before_body=*live.open_part(id)->session.document().body_history.find(second);
    const auto conflict=host.execute({{"command","placement.reference.set"},{"arguments",request(second,0,"point:"+point)}});
    check(!conflict.ok && *live.open_part(id)->session.document().body_history.find(second)==before_body,
        "Contradictory Body datum planes were silently discarded");
    run("placement.reference.remove",{{"object",second},{"index",2}});
    run("placement.reference.remove",{{"object",second},{"index",1}});
    run("placement.reference.set",request(second,0,"point:"+point));
    near(position().x,10);near(position().z,17);
    const auto later_sketch=run("sketch.create",{{"name","Later source"}}).at("sketch").get<std::string>();
    const auto later_point=run("sketch.point.create",{{"sketch",later_sketch},{"position",{10,0}}}).at("point").get<std::string>();
    const auto revision=live.open_part(id)->session.revision();
    const auto rejected=host.execute({{"command","placement.reference.set"},{"arguments",{
        {"object",box},{"index",0},{"reference",{{"owner",later_sketch},{"key","point:"+later_point}}}}}});
    check(!rejected.ok && rejected.code=="reference_not_available" && revision==live.open_part(id)->session.revision(),
        "A later native Sketch introduced a history cycle");
}
void saved_endpoint_study(const std::filesystem::path& file) {
    std::vector<kernel::BodyResult> cache;
    auto doc=document::PartDocument::load(file,&cache);
    check(doc.sketches.size()>=2,"Endpoint study needs two Sketches");
    const auto source=doc.sketches[doc.sketches.size()-2],target=doc.sketches.back();
    check(!source.arcs.empty(),"Endpoint study needs an arc");
    workspace::Workspace live;kernel::OcctKernel kernel;
    const auto id=doc.document_id;
    live.add_part(doc,cache);live.activate(id);
    const auto& arc=source.arcs.front();
    static_cast<void>(workspace::set_part_sketch_reference(live,kernel,id,target.id,0,{{},source.id,"point:"+arc.end_point_id}));
    static_cast<void>(workspace::set_part_sketch_reference(live,kernel,id,target.id,1,{{},source.id,"arc:"+arc.id}));
    const auto& result=live.open_part(id)->session.document();
    const auto placed=std::ranges::find(result.sketches,target.id,&sketcher::Sketch::id);
    const auto curves=result.sketch_placement_reference_geometry(target.owner_container_id);
    const auto edge=std::ranges::find_if(curves.edges,[&](const auto& e){return e.reference.owner_id==source.id&&e.reference.semantic_key=="arc:"+arc.id;});
    check(edge!=curves.edges.end()&&edge->exact_spline.has_value(),"Source arc unavailable");
    const auto tangent=kernel::bspline_derivative(*edge->exact_spline,1);
    const auto n=placed->resolved_normal;
    const double alignment=std::abs((n.x*tangent.x+n.y*tangent.y+n.z*tangent.z)/length(tangent));
    std::cout<<"Saved endpoint study normal/tangent alignment: "<<alignment<<'\n';
    near(alignment,1);
}
void sketch_endpoint_assignments(const std::filesystem::path& file) {
    for(int kind=0;kind<3;++kind)for(bool transformed:{false,true})for(bool end:{false,true}) {
        auto doc=document::PartDocument::create_default();
        auto source=document::PartDocument::create_sketch_container();
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=source.id;
        std::string curve,point;
        if(kind==0) {
            curve="segment:"+sketch.add_segment(-7,2,13,9);
            point=end?sketch.segments.front().second_point_id:sketch.segments.front().first_point_id;
        } else if(kind==1) {
            curve="arc:"+sketch.add_arc(0,-10,0,0,-7.66044443118978,-3.572123903134605);
            point=end?sketch.arcs.front().end_point_id:sketch.arcs.front().start_point_id;
        } else {
            curve="bspline:"+sketch.add_bspline({{0,0},{3,9},{12,-7},{17,2}});
            point=end?sketch.bsplines.front().control_point_ids.back():sketch.bsplines.front().control_point_ids.front();
        }
        if(transformed) {
            source.placement.x=50;source.placement.y=40;source.placement.z=.5;
            source.placement.absolute_rotation_z=-90;
            sketch.plane=sketcher::SketchPlane::XZ;sketch.plane_auto=false;
        }
        doc.history={source};doc.sketches={sketch};doc.history_order={{document::PartHistoryKind::Feature,source.id}};
        doc.resolve_constructions();
        auto target=document::PartDocument::create_sketch_container();
        auto pending=sketcher::Sketch::create_default();pending.owner_container_id=target.id;
        static_cast<void>(pending.add_segment(0,0,20,0));
        // Creation uses the dialog's pending rows, then editing uses the same
        // shared native reference commands as an existing Sketch container.
        document::Placement placement;
        placement.references={{{},sketch.id,"point:"+point},{{},sketch.id,curve,0,false,"direction",true,true}};
        workspace::Workspace live;kernel::OcctKernel kernel;const auto id=doc.document_id;
        live.add_part(doc);live.activate(id);
        check(workspace::commit_part_sketch_properties(live,kernel,id,pending,placement,target),"Endpoint Sketch creation failed");
        const auto verify=[&](const document::PartDocument& current) {
            const auto& p=current.find_container(target.id)->placement;
            check(p.reference_valid,"Endpoint Sketch placement invalid");
            const auto geometry=current.sketch_placement_reference_geometry(target.id);
            const auto e=std::ranges::find_if(geometry.edges,[&](const auto& e){return e.reference.owner_id==sketch.id&&e.reference.semantic_key==curve;});
            check(e!=geometry.edges.end()&&e->exact_spline.has_value(),"Endpoint source curve missing");
            const auto q=kernel::bspline_value(*e->exact_spline,end?1:0),d=kernel::bspline_derivative(*e->exact_spline,end?1:0);
            const auto s=std::ranges::find(current.sketches,pending.id,&sketcher::Sketch::id);
            near(p.x,q.x);near(p.y,q.y);near(p.z,q.z);
            near(std::abs((s->resolved_normal.x*d.x+s->resolved_normal.y*d.y+s->resolved_normal.z*d.z)/length(d)),1);
            check(s->plane_auto&&s->plane==sketcher::SketchPlane::XZ,"Saved automatic plane is not perpendicular to tangent");
            check(document::orientation_constraint_remaining_dof(p.references,geometry,true,q)==1,"Endpoint tangent lost free roll");
        };
        verify(live.open_part(id)->session.document());
        auto directory=file.parent_path();command_host::Host host(live,kernel,directory);
        const auto run=[&](const char* name,commands::Json args=commands::Json::object()) {
            const auto result=host.execute({{"command",name},{"arguments",args}});
            if(!result.ok)throw std::runtime_error(std::string("Endpoint Sketch ")+name+": "+result.message);
        };
        run("placement.reference.remove",{{"object",target.id},{"index",1}});
        run("placement.reference.set",{{"object",target.id},{"index",1},{"reference",{{"owner",sketch.id},{"key",curve}}}});
        verify(live.open_part(id)->session.document());
        run("undo");run("redo");verify(live.open_part(id)->session.document());
        run("placement.set",{{"object",target.id},{"values",{{"rotation_y",37}}}});
        verify(live.open_part(id)->session.document());
        auto result=live.open_part(id)->session.document();
        result.history.front().placement.x+=19;result.history.front().placement.absolute_rotation_x=31;
        result.resolve_constructions();verify(result);
        result.save(file);auto loaded=document::PartDocument::load(file);loaded.resolve_constructions();verify(loaded);
    }
}
}
int main(int argc,char** argv){try{
    if(argc>1){saved_endpoint_study(argv[1]);return 0;}
    surface_placement();verify();curve_matrix();history();boundaries();third_direction();ordered_frames();
    const auto root=std::filesystem::temp_directory_path();
    const auto prefix="zima-curve-"+document::PartDocument::create_default().document_id;
    const auto native=root/(prefix+".prtz"),solid=root/(prefix+"-solid.prtz");
    native_transactions(native);solid_edges(solid);third_direction_transactions(solid);body_history();sketch_endpoint_assignments(native);
    std::filesystem::remove(native);std::filesystem::remove(solid);
    std::cout<<"Exact curve placement, tangents, history, native transactions and solid edge checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
