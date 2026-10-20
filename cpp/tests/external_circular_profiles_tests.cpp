#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <zima/document/document_session.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace zima;
using namespace zima::sketcher;
namespace {
std::string context;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(context+": "+message);}
void near(double a,double b){if(std::abs(a-b)>=1e-8)throw std::runtime_error(context+": independent circular equation "+std::to_string(a)+" != "+std::to_string(b));}
struct Fixture {
    Sketch source=Sketch::create_default(),target=Sketch::create_default();
    std::string source_curve,curve,reference,block;
    bool closed{},body{};double side{1},rotation{},tx{},ty{},sweep{};
    kernel::ViewerReferenceGeometry geometry() const {
        auto mesh=source.placement_reference_geometry();kernel::ViewerReferenceGeometry result;
        for(auto edge:mesh.edges)if(edge.reference.semantic_key==(closed?"circle:":"arc:")+source_curve) {
            check(edge.exact_spline.has_value(),"Original source lacks exact reference geometry");
            // A coarse display chord must not define the mathematical curve.
            edge.points={edge.points.front(),kernel::bspline_value(*edge.exact_spline,.5),edge.points.back()};
            result.edges.push_back(std::move(edge));
        }
        return result;
    }
    void resize(double radius,double x,double y) {
        auto& value=closed?source.circles.front().radius:source.arcs.front().radius;value=radius;
        auto* center=source.find_point(closed?source.circles.front().center_point_id:source.arcs.front().center_point_id);center->x=x;center->y=y;
        if(!closed) {
            auto& arc=source.arcs.front();
            for(const auto& pair:{std::pair{arc.start_point_id,arc.start_angle},std::pair{arc.end_point_id,arc.end_angle}}) {
                auto* p=source.find_point(pair.first);p->x=x+radius*std::cos(pair.second);p->y=y+radius*std::sin(pair.second);
            }
        }
    }
    void verify(double radius,double x,double y) const {
        if((closed?target.circles.size():target.arcs.size())!=1)throw std::runtime_error(context+": expected native curve; circles="+std::to_string(target.circles.size())+" arcs="+std::to_string(target.arcs.size())+" splines="+std::to_string(target.bsplines.size()));
        const auto transform=[&](double x,double y){y*=side;return std::array{std::cos(rotation)*x-std::sin(rotation)*y+tx,std::sin(rotation)*x+std::cos(rotation)*y+ty};};
        const auto position=transform(x,y);
        const auto* center=target.find_point(closed?target.circles.front().center_point_id:target.arcs.front().center_point_id);
        near(center->x,position[0]);near(center->y,position[1]);
        near(closed?target.circles.front().radius:target.arcs.front().radius,radius);
        if(!closed) {
            const auto& arc=target.arcs.front();near(arc.end_angle-arc.start_angle,sweep);
            const auto source_first=transform(x+radius*std::cos(-.6),y+radius*std::sin(-.6));
            const auto source_last=transform(x+radius*std::cos(-.6+sweep),y+radius*std::sin(-.6+sweep));
            const auto* first=target.find_point(arc.start_point_id);const auto* last=target.find_point(arc.end_point_id);
            const auto expected_first=side<0?source_last:source_first,expected_last=side<0?source_first:source_last;
            near(first->x,expected_first[0]);near(first->y,expected_first[1]);near(last->x,expected_last[0]);near(last->y,expected_last[1]);
            for(const auto& id:{arc.start_point_id,arc.end_point_id}) {
                const auto* p=target.find_point(id);near(std::hypot(p->x-center->x,p->y-center->y),radius);
            }
        }
        const auto exact=sketch_curve_geometry(target,curve);
        for(int i=0;i<=100;++i){const auto p=kernel::bspline_value(exact,i/100.);near(std::hypot(p.x-position[0],p.y-position[1]),radius);}
        check(target.bsplines.empty(),"Circular projection became a native spline");
    }
};
Fixture make(int plane,bool closed,bool body,double side,double sweep,double correction) {
    Fixture f;f.closed=closed;f.body=body;f.side=side;f.sweep=sweep;
    f.source.plane_reference_owner_id="source-frame";f.target.plane_reference_owner_id="target-frame";
    const std::array frames{
        std::array{kernel::Vec3{1,0,0},kernel::Vec3{0,1,0},kernel::Vec3{0,0,1}},
        std::array{kernel::Vec3{0,1,0},kernel::Vec3{0,0,1},kernel::Vec3{1,0,0}},
        std::array{kernel::Vec3{0,0,1},kernel::Vec3{1,0,0},kernel::Vec3{0,1,0}}};
    const auto& frame=frames[plane];f.source.resolved_origin={12,-7,31};
    f.source.resolved_x_axis=frame[0];f.source.resolved_y_axis=frame[1];f.source.resolved_normal=frame[2];
    f.target.resolved_origin=f.source.resolved_origin;f.target.resolved_x_axis=frame[0];
    f.target.resolved_y_axis={frame[1].x*side,frame[1].y*side,frame[1].z*side};
    f.target.resolved_normal={frame[2].x*side,frame[2].y*side,frame[2].z*side};
    f.source_curve=closed?f.source.add_circle(2,3,5):f.source.add_arc(2,3,2+5*std::cos(-.6),3+5*std::sin(-.6),2+5*std::cos(-.6+sweep),3+5*std::sin(-.6+sweep));
    auto reference=Sketch::create_external_reference(ExternalReferenceKind::Edge);
    reference.source_document_id="read-only-source";reference.source_owner_id=f.source.id;
    reference.source_semantic_key=(closed?"circle:":"arc:")+f.source_curve;reference.body_edge=body;
    workspace::populate_external_reference_cache(f.target,reference,f.geometry());f.reference=reference.id;
    f.target.add_external_reference(reference);f.curve=f.target.add_external_profile_geometry(reference.id);f.block=f.target.import_blocks.front().id;
    f.rotation=correction==0?std::copysign(0.,side):side*.4;f.tx=std::copysign(correction,side);f.ty=-correction;
    f.target.transform_import_block(f.block,f.tx,f.ty,f.rotation);f.verify(5,2,3);return f;
}
void trimmed_profiles() {
    int cases{};
    for(bool closed:{false,true})for(double side:{-1.,1.})for(double sweep:{1.2,4.5})
    for(double correction:{0.,2.5}) {
        context="trim="+std::to_string(cases);
        auto f=make(0,closed,false,side,sweep,correction);
        const auto before=f.target.supporting_curve(f.curve);
        const auto metadata=f.target.import_blocks.front().external_profile;
        const auto pieces=f.target.retain_curve_intervals(f.curve,{{.1,.35},{.6,.9}});
        check(pieces.size()==2&&f.target.import_blocks.front().external_profile==metadata,"Trim lost source parameterization");
        const auto verify=[&](const kernel::BSplineGeometry& basis) {
            for(std::size_t j=0;j<pieces.size();++j) {
                const auto actual=sketch_curve_geometry(f.target,pieces[j]);
                const auto interval=j==0?std::array{.1,.35}:std::array{.6,.9};
                for(int i=0;i<=100;++i) {
                    const auto a=kernel::bspline_value(actual,i/100.);
                    const auto b=kernel::bspline_value(basis,interval[0]+(interval[1]-interval[0])*i/100.);
                    near(a.x,b.x);near(a.y,b.y);
                }
            }
        };
        verify(before);
        // Move/resize the actual source while retaining each normalized section.
        f.resize(6,4,-2);auto source=f.geometry();
        check(f.target.refresh_external_references("read-only-source",source),"Trimmed source did not refresh");
        auto expected=before;
        const auto old_center=std::array{std::cos(f.rotation)*2-std::sin(f.rotation)*3*side+f.tx,
            std::sin(f.rotation)*2+std::cos(f.rotation)*3*side+f.ty};
        const auto new_center=std::array{std::cos(f.rotation)*4+std::sin(f.rotation)*2*side+f.tx,
            std::sin(f.rotation)*4-std::cos(f.rotation)*2*side+f.ty};
        for(auto& p:expected.poles){p.x=new_center[0]+(p.x-old_center[0])*1.2;p.y=new_center[1]+(p.y-old_center[1])*1.2;}
        verify(expected);
        // Subsequent rigid correction must rotate the kept sections, not reset them.
        const double angle=.3,new_x=-4,new_y=3,c=std::cos(angle),s=std::sin(angle);
        f.target.transform_import_block(f.block,new_x,new_y,f.rotation+angle);
        for(auto& p:expected.poles){const double x=p.x-f.tx,y=p.y-f.ty;p={c*x-s*y+new_x,s*x+c*y+new_y,0};}
        verify(expected);f.target=Sketch::from_serialized(f.target.serialized());verify(expected);
        check(f.target.refresh_external_references("read-only-source",{}),"Trimmed missing source ignored");
        verify(expected);check(f.target.external_references.front().broken,"Trimmed reference detached on loss");
        check(f.target.refresh_external_references("read-only-source",source),"Trimmed source could not reconnect");verify(expected);
        ++cases;
    }
    std::cout<<cases<<" exact trimmed source/correction/persistence cases passed\n";
}
void native_history() {
    const auto root=std::filesystem::temp_directory_path()/("zima-circular-profiles-"+Sketch::create_default().id);
    std::filesystem::create_directory(root);
    for(bool closed:{false,true})for(double side:{-1.,1.}) {
        context="native history";auto f=make(0,closed,false,side,4.5,2.5);
        // Use an ordinary Body-owned Sketch and the production native/session paths.
        f.target.plane_reference_owner_id.clear();f.target.resolved_origin={};
        auto document=document::PartDocument::create_default();auto owner=document::PartDocument::create_sketch_container();
        f.target.owner_container_id=owner.id;workspace::insert_new_sketch(document,f.target,owner);
        document::DocumentSession session(document);const auto before=session.document().sketches.front().serialized();
        auto changed=session.document();
        check(changed.sketches.front().refresh_external_references("read-only-source",{}),"Native missing source ignored");
        const auto after=changed.sketches.front().serialized();session.commit(std::move(changed));
        check(session.undo()&&session.document().sketches.front().serialized()==before,"Native Undo did not restore reference identity");
        check(session.redo()&&session.document().sketches.front().serialized()==after,"Native Redo lost frozen geometry");
        const auto file=root/(std::to_string(closed)+std::to_string(side)+".prtz");session.document().save(file);
        const auto reopened=document::PartDocument::load(file);
        check(reopened.sketches.front().serialized()==after,"Native Part save/reopen changed the unresolved profile");
    }
    std::filesystem::remove_all(root);
}
void incompatible_sources() {
    for(bool closed:{false,true})for(double side:{-1.,1.}) {
        context="incompatible source";auto f=make(0,closed,false,side,4.5,2.5);
        const auto points=f.target.points;
        auto incompatible=f.geometry();
        for(auto& p:incompatible.edges.front().exact_spline->poles)p.y*=1.2;
        for(auto& p:incompatible.edges.front().points)p.y*=1.2;
        check(f.target.refresh_external_references("read-only-source",incompatible),"Incompatible projection was ignored");
        check(f.target.external_references.front().broken&&f.target.points==points,"Changed curve kind destroyed frozen geometry");
        f.target=Sketch::from_serialized(f.target.serialized());f.verify(5,2,3);
        check(f.target.refresh_external_references("read-only-source",f.geometry()),"Compatible source could not repair the same link");
        f.verify(5,2,3);
        for(int invalid:{0,1,2}) {
            auto data=nlohmann::json::parse(f.target.serialized());
            auto& block=data["import_blocks"][0];
            if(invalid==0)block.erase("external_profile");
            if(invalid==1)block["external_profile"]["version"]=0;
            if(invalid==2)block["external_profile"]["kind"]="unknown";
            bool rejected=false;try{static_cast<void>(Sketch::from_serialized(data.dump()));}catch(const std::exception&){rejected=true;}
            check(rejected,"Invalid projection contract was silently accepted");
        }
    }
}
void point_dimensions(const Fixture& f,bool locked) {
    if(f.closed)return;
    const auto& arc=f.target.arcs.front();
    const auto exercise=[&](SketchDimension dimension) {
        auto reference=f.target;dimension.driving=false;dimension.locked=locked;
        if(locked) {
            const auto before=reference.serialized();bool rejected=false;
            try{reference.apply_dimension(dimension);}catch(const std::exception&){rejected=true;}
            check(rejected&&reference.serialized()==before,"Unsupported locked reference dimension changed the Sketch");
            dimension.locked=false;
        }
        reference.apply_dimension(dimension);const auto original=reference.points;
        for(double delta:{1.,-2.,.5}) {
            check(reference.set_dimension_value(dimension.id,dimension.value+delta),"Reference point/angle dimension edit failed");
            check(reference.points==original,"Reference point/angle dimension moved the source-owned profile");
        }
        auto driving=f.target;dimension.driving=true;dimension.locked=locked;dimension.value+=.75;
        const auto before=driving.serialized();bool rejected=false;
        try{driving.apply_dimension(dimension);}catch(const std::exception&){rejected=true;}
        check(rejected&&driving.serialized()==before,"Driving point/angle dimension changed a source-owned profile");
    };
    for(bool reverse:{false,true}) {
        const auto first=reverse?arc.end_point_id:arc.start_point_id,last=reverse?arc.start_point_id:arc.end_point_id;
        for(auto kind:{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY})
            exercise(f.target.create_point_dimension(first,last,kind));
        exercise(f.target.create_three_point_angle_dimension(first,arc.center_point_id,last));
    }
}
void editable_neighbours(const Fixture& f) {
    auto sketch=f.target;const auto id=sketch.add_circle(30,20,2);const auto center=sketch.circles.back().center_point_id;
    auto radius=sketch.create_circle_radius_dimension(id);radius.driving=true;sketch.apply_dimension(radius);
    check(sketch.solve().remaining_degrees_of_freedom==2,"Ordinary neighbour lost its free centre");
    for(double value:{2.1,1.9,2.}) {
        check(sketch.set_dimension_value(radius.id,value),"Ordinary neighbour radius is no longer editable");near(sketch.circles.back().radius,value);
        const auto p=*sketch.find_point(center);
        check(sketch.move_point(center,p.x+1,p.y-.5),"Underconstrained native neighbour cannot be dragged");
        near(sketch.find_point(center)->x,p.x+1);near(sketch.find_point(center)->y,p.y-.5);
    }
    for(const auto* axis:{"sketch_axis:x","sketch_axis:y"}) {
        auto dimension=sketch.create_axis_dimension(center,axis);dimension.driving=true;sketch.apply_dimension(dimension);
    }
    check(sketch.solve().remaining_degrees_of_freedom==0,"Fully dimensioned neighbour retained free motion");
    for(double delta:{1.,-2.,.5}) {
        const auto p=*sketch.find_point(center);
        check(sketch.move_point(center,p.x+delta,p.y-delta),"Unlocked neighbour coordinate drivers cannot follow an intentional drag");
        const auto* moved=sketch.find_point(center);near(moved->x,p.x+delta);near(moved->y,p.y-delta);
        for(const auto& d:sketch.dimensions)if(d.first_point_id==center&&(d.kind==DimensionKind::DistanceX||d.kind==DimensionKind::DistanceY))
            near(d.value,d.kind==DimensionKind::DistanceX?moved->x:moved->y);
    }
    for(auto& d:sketch.dimensions)if(d.first_point_id==center&&(d.kind==DimensionKind::DistanceX||d.kind==DimensionKind::DistanceY))d.locked=true;
    for(double delta:{1.,-2.,.5}) {
        const auto before=sketch.serialized();const auto p=*sketch.find_point(center);
        check(!sketch.move_point(center,p.x+delta,p.y-delta)&&sketch.serialized()==before,"Fully constrained neighbour escaped its dimensions");
    }
    for(const auto& p:f.target.points)check(*sketch.find_point(p.id)==p,"Editing a neighbour changed the source-owned profile");
}
}
int main(){try {
    int cases=0;
    for(int plane:{0,1,2})for(bool closed:{false,true})for(bool body:{false,true})for(double side:{-1.,1.})
    for(double sweep:{1.2,4.5})for(double correction:{0.,2.5})for(bool locked:{false,true}) {
        context="case="+std::to_string(cases)+" plane="+std::to_string(plane)+" closed="+std::to_string(closed)+" body="+std::to_string(body)+" side="+std::to_string(side);
        if(std::getenv("ZIMA_TRACE_CIRCULAR_PROFILES"))std::cerr<<context<<std::endl;
        auto f=make(plane,closed,body,side,sweep,correction);const auto ids=f.target.points;
        const auto solved=f.target.solve();
        check(solved.status!=SolveStatus::Invalid&&solved.status!=SolveStatus::Conflicting&&solved.remaining_degrees_of_freedom==0,
            "Source-owned analytic geometry is not fully constrained");
        const auto native_id=f.curve;const auto constraints=f.target.constraints;
        point_dimensions(f,locked);
        if(correction==0&&!locked&&sweep==1.2)editable_neighbours(f);
        for(bool diameter:{false,true}) {
            auto d=closed?(diameter?f.target.create_circle_diameter_dimension(f.curve):f.target.create_circle_radius_dimension(f.curve)):
                (diameter?f.target.create_arc_diameter_dimension(f.curve):f.target.create_arc_radius_dimension(f.curve));
            d.driving=false;d.locked=false;f.target.apply_dimension(d);
            if(locked) {
                auto candidate=f.target;auto locked_reference=d;locked_reference.locked=true;
                const auto before=candidate.serialized();bool rejected=false;
                try{candidate.apply_dimension(locked_reference);}catch(const std::exception&){rejected=true;}
                check(rejected&&candidate.serialized()==before,"Unsupported locked R/D reference changed the Sketch");
            }
            const auto points=f.target.points;
            for(double factor:{1.1,.9,1.}){check(f.target.set_dimension_value(d.id,d.value*factor),"Reference value edit failed");check(f.target.points==points,"Reference dimension moved imported points");}
            auto driving=closed?(diameter?f.target.create_circle_diameter_dimension(f.curve):f.target.create_circle_radius_dimension(f.curve)):
                (diameter?f.target.create_arc_diameter_dimension(f.curve):f.target.create_arc_radius_dimension(f.curve));
            driving.driving=true;driving.locked=locked;driving.value*=1.1;
            const auto before=f.target.serialized();bool rejected=false;try{f.target.apply_dimension(driving);}catch(const std::exception&){rejected=true;}
            check(rejected&&f.target.serialized()==before,"Driving radius/diameter changed a read-only projection");
        }
        for(double step:{1.,-2.,.5})for(const auto& point:ids) {
            const auto before=f.target.serialized();check(!f.target.move_point(point.id,point.x+step,point.y-step),"Linked point could be dragged independently");
            check(f.target.serialized()==before,"Rejected linked drag changed the Sketch");
        }
        for(const auto& values:{std::array{6.,4.,-2.},std::array{4.,-3.,7.},std::array{5.,2.,3.}}) {
            f.resize(values[0],values[1],values[2]);const auto source=f.geometry();const auto source_copy=source.edges.front().exact_spline;
            check(f.target.refresh_external_references("read-only-source",source,false,body?&source:nullptr),"Source change was not followed");
            f.verify(values[0],values[1],values[2]);
            check(source.edges.front().exact_spline==source_copy,"Projection changed source data");
            check(f.target.constraints==constraints,"Refresh replaced the authored relation identity");
            f.target=Sketch::from_serialized(f.target.serialized());f.verify(values[0],values[1],values[2]);
        }
        const auto retained=f.target.points;const auto circles=f.target.circles;const auto arcs=f.target.arcs;
        check(f.target.refresh_external_references("read-only-source",{},false,nullptr,true),"Missing reference was ignored");
        check(f.target.external_references.size()==1&&f.target.external_references.front().broken,"Missing reference was auto-detached");
        check(f.target.points==retained&&f.target.circles==circles&&f.target.arcs==arcs,"Missing source destroyed last valid native geometry");
        f.target=Sketch::from_serialized(f.target.serialized());f.verify(5,2,3);
        const auto restored=f.geometry();check(f.target.refresh_external_references("read-only-source",restored,false,body?&restored:nullptr),"Returning source did not reconnect");
        check(!f.target.external_references.front().broken&&f.target.import_blocks.front().geometry_ids.front()==native_id,"Reference repair replaced identity");
        f.target.remove_geometry(f.reference);check(f.target.external_references.empty()&&f.target.points==retained,"Explicit detach removed native geometry");
        ++cases;
    }
    std::cout<<cases<<" circular profile plane/side/body/correction/state cases passed\n";
    trimmed_profiles();native_history();incompatible_sources();return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
