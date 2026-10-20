#include <zima/sketcher/external_curve_center.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/document/document_session.hpp>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <filesystem>
using namespace zima;
using namespace zima::sketcher;
namespace {
std::string context;
void check(bool value,const char* message){if(!value)throw std::runtime_error(context+": "+message);}
void near(double a,double b){if(std::abs(a-b)>1e-8)throw std::runtime_error(context+": equation "+std::to_string(a)+" != "+std::to_string(b));}
struct Fixture {
    Sketch source=Sketch::create_default(),target=Sketch::create_default();
    std::string curve,reference,center;unsigned kind{};bool body{};double side{1};
    kernel::ViewerReferenceGeometry geometry()const {
        kernel::ViewerReferenceGeometry result;const auto mesh=source.placement_reference_geometry();
        const std::array<std::string,4> prefixes{"circle:","arc:","ellipse:","elliptical_arc:"};
        for(auto edge:mesh.edges)if(edge.reference.semantic_key==prefixes[kind]+curve) {
            check(edge.exact_spline.has_value(),"Conic source lacks exact geometry");
            edge.points={edge.points.front(),kernel::bspline_value(*edge.exact_spline,.5),edge.points.back()};
            result.edges.push_back(std::move(edge));
        }
        check(result.edges.size()==1,"Expected one source conic");return result;
    }
    void refresh(){const auto g=geometry();static_cast<void>(target.refresh_external_references("read-only-source",g,false,body?&g:nullptr));}
    void move_source(double x,double y,double scale) {
        const auto center_id=kind==0?source.circles.front().center_point_id:kind==1?source.arcs.front().center_point_id:
            kind==2?source.ellipses.front().center_point_id:source.elliptical_arcs.front().center_point_id;
        const auto old=*source.find_point(center_id);
        for(auto& p:source.points){p.x=x+(p.x-old.x)*scale;p.y=y+(p.y-old.y)*scale;}
        for(auto& c:source.circles)c.radius*=scale;
        for(auto& c:source.arcs)c.radius*=scale;
        for(auto& c:source.ellipses){c.major_radius*=scale;c.minor_radius*=scale;}
        for(auto& c:source.elliptical_arcs){c.major_radius*=scale;c.minor_radius*=scale;}
        source.validate();
    }
};
Fixture fixture(unsigned kind,int plane,double side,bool body) {
    Fixture f;f.kind=kind;f.side=side;f.body=body;
    const std::array frames{std::array{kernel::Vec3{1,0,0},kernel::Vec3{0,1,0},kernel::Vec3{0,0,1}},
        std::array{kernel::Vec3{0,1,0},kernel::Vec3{0,0,1},kernel::Vec3{1,0,0}},
        std::array{kernel::Vec3{0,0,1},kernel::Vec3{1,0,0},kernel::Vec3{0,1,0}}};
    const auto frame=frames[plane];f.source.plane_reference_owner_id="source-frame";f.target.plane_reference_owner_id="target-frame";
    f.source.resolved_origin={12,-7,31};f.source.resolved_x_axis=frame[0];f.source.resolved_y_axis=frame[1];f.source.resolved_normal=frame[2];
    f.target.resolved_origin=f.source.resolved_origin;f.target.resolved_x_axis=frame[0];
    f.target.resolved_y_axis={frame[1].x*side,frame[1].y*side,frame[1].z*side};f.target.resolved_normal={frame[2].x*side,frame[2].y*side,frame[2].z*side};
    constexpr double r=5,angle=.37;
    if(kind==0)f.curve=f.source.add_circle(2,3,r);
    else if(kind==1)f.curve=f.source.add_arc(2,3,2+r*std::cos(-.6),3+r*std::sin(-.6),2+r*std::cos(1.2),3+r*std::sin(1.2));
    else if(kind==2)f.curve=f.source.add_ellipse(2,3,2+7*std::cos(angle),3+7*std::sin(angle),2-3*std::sin(angle),3+3*std::cos(angle));
    else f.curve=f.source.add_elliptical_arc(2,3,2+7*std::cos(angle),3+7*std::sin(angle),2-3*std::sin(angle),3+3*std::cos(angle),
        2+7*std::cos(angle),3+7*std::sin(angle),2-3*std::sin(angle),3+3*std::cos(angle));
    auto reference=Sketch::create_external_reference(ExternalReferenceKind::Edge);reference.body_edge=body;
    reference.source_document_id="read-only-source";reference.source_owner_id=f.source.id;
    const std::array<std::string,4> prefixes{"circle:","arc:","ellipse:","elliptical_arc:"};reference.source_semantic_key=prefixes[kind]+f.curve;
    auto mesh=f.source.placement_reference_geometry();kernel::ViewerReferenceGeometry g;
    for(auto edge:mesh.edges)if(edge.reference.semantic_key==reference.source_semantic_key)g.edges.push_back(std::move(edge));
    workspace::populate_external_reference_cache(f.target,reference,g);f.reference=reference.id;
    f.target.add_external_reference(reference);f.center=external_center_point_id(reference.id);
    const auto position=external_reference_center(f.target.external_references.front());check(position.has_value(),"Conic has no reference center");
    near((*position)[0],2);near((*position)[1],side*3);return f;
}
void centers_and_constraints() {
    unsigned cases=0;
    for(unsigned kind=0;kind<4;++kind)for(int plane=0;plane<3;++plane)for(double side:{-1.,1.})for(bool body:{false,true})for(bool reverse:{false,true}) {
        context="center "+std::to_string(cases++);auto f=fixture(kind,plane,side,body);
        const auto source_before=f.source.serialized();const auto mesh=f.target.viewer_mesh();
        check(std::ranges::count_if(mesh.points,[&](const auto& p){return p.reference.semantic_key=="external_point:"+f.center;})==1,"Center cross is absent or duplicated");
        const auto own=f.target.add_circle(15,14,2);const auto point=f.target.circles.front().center_point_id;
        static_cast<void>(f.target.add_point_reference_constraint(point,f.center));
        check(f.source.serialized()==source_before,"Center constraint changed the source document");
        near(f.target.find_point(point)->x,2);near(f.target.find_point(point)->y,side*3);
        near(f.target.circles.front().radius,2);
        if(kind<2){static_cast<void>(f.target.add_equal_radius_constraint(reverse?own:f.reference,reverse?f.reference:own));near(f.target.circles.front().radius,5);}
        const auto stable=f.target.constraints;
        for(const auto xy:std::array{std::array{4.,6.,1.2},std::array{-3.,2.,.8},std::array{2.,3.,1.}}) {
            f.move_source(xy[0],xy[1],xy[2]);const auto g=f.geometry();const auto source_saved=f.source.serialized();
            static_cast<void>(f.target.refresh_external_references("read-only-source",g,false,body?&g:nullptr));
            check(f.source.serialized()==source_saved,"Reference refresh mutated its source");
            near(f.target.find_point(point)->x,xy[0]);near(f.target.find_point(point)->y,side*xy[1]);
            if(kind<2)near(f.target.circles.front().radius,f.source.circles.empty()?f.source.arcs.front().radius:f.source.circles.front().radius);
            check(f.target.constraints==stable,"Center/radius source refresh changed relation identities");
            f.target=Sketch::from_serialized(f.target.serialized());
            const auto before=f.target.serialized();const auto old=*f.target.find_point(point);
            const bool moved=f.target.move_point(point,old.x+2,old.y-1);
            check(!moved&&f.target.serialized()==before,"Dragging escaped read-only center reference");
        }
        const auto before=f.target.serialized();static_cast<void>(f.target.refresh_external_references("read-only-source",{}));
        check(f.target.external_references.front().broken,"Missing source was not reported");
        near(f.target.find_point(point)->x,2);near(f.target.find_point(point)->y,side*3);
        f.refresh();check(!f.target.external_references.front().broken,"Center source did not repair");
        f.target.remove_geometry(f.reference);check(f.target.external_references.empty()&&f.target.constraints.empty(),"Deleting external reference left center/radius dependencies");
        check(f.target.circles.size()==1&&f.target.find_point(point),"Deleting source removed owned circle");
        check(f.target.move_point(point,8,9),"Detached circle center cannot move");
    }
    std::cout<<cases<<" external conic center/reference/equality/drag/refresh/reopen cases passed\n";
}
void point_dimensions() {
    unsigned cases=0;
    for(unsigned source_kind=0;source_kind<4;++source_kind)for(int plane=0;plane<3;++plane)for(double side:{-1.,1.})
    for(auto kind:{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY})for(bool reverse:{false,true})
    for(bool driving:{false,true})for(bool locked:{false,true}) {
        context="point dimension "+std::to_string(cases++);auto f=fixture(source_kind,plane,side,false);
        const auto point=f.target.add_point(5,side*3+4,1e-9);
        auto dimension=f.target.create_point_dimension(reverse?f.center:point,reverse?point:f.center,kind);
        dimension.driving=driving;dimension.locked=locked;const auto before=f.target.serialized();
        try{f.target.apply_dimension(dimension);}catch(const std::exception&){
            check(!driving&&locked&&f.target.serialized()==before,"A valid center dimension failed or an unsupported reference lock mutated the document");continue;
        }
        const auto measure=[&] {
            const auto center=external_reference_center(f.target.external_references.front());check(center.has_value(),"Dimension source lost its center");
            const auto* p=f.target.find_point(point);const double x=p->x-(*center)[0],y=p->y-(*center)[1];
            return kind==DimensionKind::Distance?std::hypot(x,y):kind==DimensionKind::DistanceX?std::abs(x):std::abs(y);
        };
        const auto value=[&] {return std::abs(std::ranges::find(f.target.dimensions,dimension.id,&SketchDimension::id)->value);};
        near(measure(),value());
        if(driving)for(double factor:{1.2,.8,1.}) {
            check(f.target.set_dimension_value(dimension.id,dimension.value*factor),"Center dimension value is not editable");
            near(measure(),std::abs(dimension.value*factor));
        }
        else {
            const auto unchanged=f.target.points;
            for(double delta:{1.,-2.,.5}) {
                check(f.target.set_dimension_value(dimension.id,dimension.value+delta),"Reference center dimension could not refresh its measurement");
                check(f.target.points==unchanged,"Reference center dimension input drove native geometry");near(measure(),value());
            }
        }
        for(int edit=0;edit<3;++edit) {
            const auto* p=f.target.find_point(point);const double x=p->x,y=p->y;
            const auto previous=f.target.serialized();const bool moved=f.target.move_point(point,x+.7,y+.4);
            if(!moved)check(f.target.serialized()==previous,"Rejected drag mutated a center dimension");
            else check(std::hypot(f.target.find_point(point)->x-x,f.target.find_point(point)->y-y)>1e-9,"Accepted drag did not move a free point");
            near(measure(),value());
            if(driving&&locked)near(value(),std::abs(dimension.value));
            f.target=Sketch::from_serialized(f.target.serialized());near(measure(),value());
        }
        const auto old_source=f.source.serialized();f.refresh();check(f.source.serialized()==old_source,"Point dimension modified the external source");
    }
    std::cout<<cases<<" external center Distance/X/Y order/driving/reference/lock/edit/drag cases passed\n";
}
void incompatible_sources() {
    for(unsigned kind:{0u,2u}) {
        context="incompatible conic "+std::to_string(kind);auto f=fixture(kind,0,1,true);
        const auto own=f.target.add_circle(15,14,2);const auto center=f.target.circles.front().center_point_id;
        static_cast<void>(f.target.add_point_reference_constraint(center,f.center));
        if(kind==0)static_cast<void>(f.target.add_equal_radius_constraint(f.reference,own));
        const auto points=f.target.points;const auto cached=f.target.external_references.front().exact_spline;
        auto changed=f.geometry();kernel::BSplineGeometry line;line.degree=1;line.knots={0,0,1,1};line.weights={1,1};
        line.poles={f.source.world_point(-4,0),f.source.world_point(8,0)};
        changed.edges.front().exact_spline=line;changed.edges.front().points=line.poles;
        static_cast<void>(f.target.refresh_external_references("read-only-source",changed,false,&changed));
        check(f.target.external_references.front().broken&&f.target.external_references.front().exact_spline==cached&&f.target.points==points,"Nonconic source did not retain last valid center and owned geometry");
        check(!f.target.refresh_external_references("read-only-source",changed,false,&changed),"Repeated incompatible source was not a no-op");
        f.target=Sketch::from_serialized(f.target.serialized());f.refresh();
        check(!f.target.external_references.front().broken&&f.target.points==points,"Restored conic did not repair the center reference");
    }
    context="circle-to-ellipse radius source";auto f=fixture(0,0,1,true);
    const auto own=f.target.add_circle(15,14,2);const auto center=f.target.circles.front().center_point_id;
    static_cast<void>(f.target.add_point_reference_constraint(center,f.center));static_cast<void>(f.target.add_equal_radius_constraint(f.reference,own));
    const auto cached=f.target.external_references.front().exact_spline;const auto points=f.target.points;
    auto changed=f.geometry();const auto ellipse=fixture(2,0,1,true).geometry().edges.front();
    changed.edges.front().exact_spline=ellipse.exact_spline;changed.edges.front().points=ellipse.points;
    static_cast<void>(f.target.refresh_external_references("read-only-source",changed,false,&changed));
    check(f.target.external_references.front().broken&&f.target.external_references.front().exact_spline==cached&&f.target.points==points,"Noncircular conic changed a required external radius");
    check(!f.target.refresh_external_references("read-only-source",changed,false,&changed),"Repeated noncircular radius source was not a no-op");f.refresh();
    check(!f.target.external_references.front().broken,"Restored circular radius did not repair");
    auto conic=fixture(2,0,1,false).target.external_references.front();
    check(external_reference_center(conic).has_value(),"Certified ellipse center is missing");
    conic.exact_spline->poles[2].x+=.01;check(!external_reference_center(conic),"A deformed nonconic spline received a false ellipse center");
    std::cout<<"Nonconic and noncircular source changes freeze, repeat as no-op, and repair; deformed spline has no false center\n";
}
void centered_native_curves() {
    unsigned cases=0;
    for(unsigned source_kind=0;source_kind<4;++source_kind)for(unsigned own_kind=0;own_kind<4;++own_kind)for(double side:{-1.,1.}) {
        context="owned conic "+std::to_string(cases++);auto f=fixture(source_kind,0,side,false);
        std::string id,center;
        if(own_kind==0){id=f.target.add_circle(15,14,2);center=f.target.circles.back().center_point_id;}
        if(own_kind==1){id=f.target.add_arc(15,14,17,14,15,16);center=f.target.arcs.back().center_point_id;}
        if(own_kind==2){id=f.target.add_ellipse(15,14,19,14,15,16);center=f.target.ellipses.back().center_point_id;}
        if(own_kind==3){id=f.target.add_elliptical_arc(15,14,19,14,15,16,19,14,15,16);center=f.target.elliptical_arcs.back().center_point_id;}
        static_cast<void>(f.target.add_point_reference_constraint(center,f.center));
        for(const auto xy:std::array{std::array{4.,6.,1.2},std::array{-3.,2.,.8},std::array{2.,3.,1.}}) {
            f.move_source(xy[0],xy[1],xy[2]);f.refresh();near(f.target.find_point(center)->x,xy[0]);near(f.target.find_point(center)->y,side*xy[1]);
            const auto before=f.target.serialized();check(!f.target.move_point(center,8,9)&&f.target.serialized()==before,"Owned conic escaped its bound center");
            f.target=Sketch::from_serialized(f.target.serialized());
        }
    }
    std::cout<<cases<<" owned Circle/Arc/Ellipse/EllipticalArc center-follow/edit/reopen cases passed\n";
}
void centered_radius_dimensions() {
    unsigned cases=0;
    for(unsigned source_kind=0;source_kind<4;++source_kind)for(bool arc:{false,true})for(bool diameter:{false,true})
    for(bool driving:{false,true})for(bool locked:{false,true})for(bool equal:{false,true}) {
        if(equal&&source_kind>=2)continue;
        context="centered R/D "+std::to_string(cases++);auto f=fixture(source_kind,0,1,false);
        const auto own=arc?f.target.add_arc(15,14,17,14,15,16):f.target.add_circle(15,14,2);
        const auto center=arc?f.target.arcs.back().center_point_id:f.target.circles.back().center_point_id;
        static_cast<void>(f.target.add_point_reference_constraint(center,f.center));
        if(equal)static_cast<void>(f.target.add_equal_radius_constraint(f.reference,own));
        auto dimension=arc?(diameter?f.target.create_arc_diameter_dimension(own):f.target.create_arc_radius_dimension(own)):
            (diameter?f.target.create_circle_diameter_dimension(own):f.target.create_circle_radius_dimension(own));
        dimension.driving=driving;dimension.locked=locked;const auto before=f.target.serialized();
        try{f.target.apply_dimension(dimension);}catch(const std::exception&){check(((!driving&&locked)||(driving&&equal))&&f.target.serialized()==before,"Valid centered radius dimension was rejected");continue;}
        for(double factor:{1.2,.8,1.}) {
            const auto previous=f.target.serialized();const double value=dimension.value*factor;
            const bool accepted=f.target.set_dimension_value(dimension.id,value);
            if(equal&&driving&&factor!=1)check(!accepted&&f.target.serialized()==previous,"Driving R/D escaped read-only equality");
            else check(accepted,"Centered radius dimension cannot be edited");
            const double radius=arc?f.target.arcs.back().radius:f.target.circles.back().radius;
            near(radius,equal?5:driving?value/(diameter?2:1):2);
            near(f.target.find_point(center)->x,2);near(f.target.find_point(center)->y,3);
            f.target=Sketch::from_serialized(f.target.serialized());
        }
    }
    std::cout<<cases<<" centered R/D driving/reference/lock/free/equal repeated-edit cases passed\n";
}
void native_sessions() {
    for(unsigned kind=0;kind<4;++kind) {
        context="native center "+std::to_string(kind);auto f=fixture(kind,0,1,false);
        const auto own=f.target.add_circle(15,14,2);const auto center=f.target.circles.front().center_point_id;
        static_cast<void>(f.target.add_point_reference_constraint(center,f.center));
        if(kind<2)static_cast<void>(f.target.add_equal_radius_constraint(f.reference,own));
        auto part=document::PartDocument::create_default();
        auto source_owner=document::PartDocument::create_sketch_container(),owner=document::PartDocument::create_sketch_container();
        f.source.owner_container_id=source_owner.id;f.target.owner_container_id=owner.id;
        source_owner.feature.sketch_id=f.source.id;owner.feature.sketch_id=f.target.id;
        f.source.plane_reference_owner_id.clear();f.target.plane_reference_owner_id.clear();f.source.refresh_default_frame();f.target.refresh_default_frame();
        f.target.external_references.front().source_document_id=part.document_id;
        part.history={source_owner,owner};part.sketches={f.source,f.target};
        document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Native centers"));
        graph.insert({document::PartHistoryKind::Feature,source_owner.id});graph.insert({document::PartHistoryKind::Feature,owner.id});part.set_body_history(graph);
        auto before=part;before.sketches.back().constraints.clear();before.sketches.back().circles.front().radius=2;
        before.sketches.back().find_point(center)->x=15;before.sketches.back().find_point(center)->y=14;
        document::DocumentSession session(before);session.commit(part,{});
        check(session.undo()&&session.document().sketches.back().constraints.empty(),"Native center Undo failed");
        check(session.redo()&&session.document().sketches.back().constraints==f.target.constraints,"Native center Redo changed constraint identities");
        const auto file=std::filesystem::absolute("build/external-center-native/center-"+std::to_string(kind)+".prtz");
        std::filesystem::create_directories(file.parent_path());session.document().save(file);
        const auto reopened=document::PartDocument::load(file);
        check(reopened.sketches.back().points==f.target.points&&reopened.sketches.back().constraints==f.target.constraints,"Native Part reopen lost center identities or geometry");
        const auto mesh=reopened.sketches.back().viewer_mesh();check(std::ranges::any_of(mesh.points,[&](const auto& p){return p.reference.semantic_key=="external_point:"+f.center;}),"Native Part reopen lost center cross");
    }
    std::cout<<"Four native conic center Parts passed save/reopen and session Undo/Redo\n";
}
void centered_angle_dimensions() {
    unsigned cases=0;
    for(unsigned source_kind=0;source_kind<4;++source_kind)for(bool reverse:{false,true})for(bool driving:{false,true})for(bool locked:{false,true}) {
        context="centered angle "+std::to_string(cases++);auto f=fixture(source_kind,0,1,false);
        const auto vertex=f.target.add_point(2,3),a=f.target.add_point(7,3),b=f.target.add_point(2,7);
        static_cast<void>(f.target.add_point_reference_constraint(vertex,f.center));
        auto dimension=f.target.create_three_point_angle_dimension(reverse?b:a,vertex,reverse?a:b);
        dimension.driving=driving;dimension.locked=locked;const auto before=f.target.serialized();
        try{f.target.apply_dimension(dimension);}catch(const std::exception&){check(!driving&&locked&&f.target.serialized()==before,"Valid centered angle was rejected");continue;}
        const auto measure=[&]{const auto p=*f.target.find_point(a),q=*f.target.find_point(b),v=*f.target.find_point(vertex);
            const double ux=p.x-v.x,uy=p.y-v.y,vx=q.x-v.x,vy=q.y-v.y;
            return std::acos(std::clamp((ux*vx+uy*vy)/std::hypot(ux,uy)/std::hypot(vx,vy),-1.,1.))*180/std::numbers::pi;};
        for(double value:{75.,105.,90.}) {
            const auto old=f.target.points;check(f.target.set_dimension_value(dimension.id,value),"Centered angle could not be edited");
            if(driving)near(measure(),value);else check(f.target.points==old,"Reference angle moved centered geometry");
            near(f.target.find_point(vertex)->x,2);near(f.target.find_point(vertex)->y,3);
            const auto initial=f.target.serialized();check(!f.target.move_point(vertex,9,8)&&f.target.serialized()==initial,"Angle drag escaped its source-bound vertex");
            const auto p=*f.target.find_point(a);const auto prior=f.target.serialized();const bool moved=f.target.move_point(a,p.x+.6,p.y+.3);
            if(!moved)check(f.target.serialized()==prior,"Rejected angular drag changed the Sketch");
            else check(std::hypot(f.target.find_point(a)->x-p.x,f.target.find_point(a)->y-p.y)>1e-9,"Accepted angular drag did not move a free point");
            const auto stored=std::ranges::find(f.target.dimensions,dimension.id,&SketchDimension::id);near(measure(),stored->value);
            if(driving&&locked)near(stored->value,value);
            f.target=Sketch::from_serialized(f.target.serialized());
        }
        // Direct virtual points retain the existing native-only three-point
        // angle creation contract; a native point bound by C is supported above.
        const auto original=f.target.serialized();bool rejected=false;
        try{static_cast<void>(f.target.create_three_point_angle_dimension(a,f.center,b));}catch(const std::exception&){rejected=true;}
        check(rejected&&f.target.serialized()==original,"Unsupported direct virtual angle changed the Sketch");
    }
    std::cout<<cases<<" C-centered angular driving/reference/lock/order/edit/drag cases passed\n";
}
void centered_ellipse_dimensions() {
    unsigned cases=0;
    for(unsigned source_kind=0;source_kind<4;++source_kind)for(unsigned kind=0;kind<3;++kind)for(bool driving:{false,true})for(bool locked:{false,true}) {
        context="centered ellipse dimension "+std::to_string(cases++);auto f=fixture(source_kind,0,1,false);
        const auto own=f.target.add_ellipse(15,14,19,14,15,16);const auto center=f.target.ellipses.back().center_point_id;
        static_cast<void>(f.target.add_point_reference_constraint(center,f.center));
        auto dimension=kind==2?f.target.create_ellipse_rotation_dimension(own):f.target.create_ellipse_radius_dimension(own,kind==0);
        dimension.driving=driving;dimension.locked=locked;const auto before=f.target.serialized();
        try{f.target.apply_dimension(dimension);}catch(const std::exception&){check(!driving&&locked&&f.target.serialized()==before,"Valid centered ellipse dimension was rejected");continue;}
        const auto measure=[&]{const auto& ellipse=f.target.ellipses.back();const auto c=*f.target.find_point(center);
            const auto p=*f.target.find_point(kind==1?ellipse.minor_point_id:ellipse.major_point_id);
            return kind==2?std::atan2(p.y-c.y,p.x-c.x)*180/std::numbers::pi:std::hypot(p.x-c.x,p.y-c.y);};
        for(double delta:{.25,-.25,0.}) {
            const double value=dimension.value+delta*(kind==2?40:1);const auto old=f.target.points;
            check(f.target.set_dimension_value(dimension.id,value),"Centered ellipse dimension cannot be edited");
            if(driving)near(measure(),value);else check(f.target.points==old,"Reference ellipse dimension drove geometry");
            near(f.target.find_point(center)->x,2);near(f.target.find_point(center)->y,3);
            const auto point=f.target.ellipses.back().major_point_id;const auto p=*f.target.find_point(point);const auto previous=f.target.serialized();
            const bool moved=f.target.move_point(point,p.x+.2,p.y+.1);
            if(!moved)check(f.target.serialized()==previous,"Rejected ellipse drag changed geometry");
            else check(std::hypot(f.target.find_point(point)->x-p.x,f.target.find_point(point)->y-p.y)>1e-9,"Accepted ellipse drag did not move its free defining point");
            const auto stored=std::ranges::find(f.target.dimensions,dimension.id,&SketchDimension::id);near(measure(),stored->value);
            if(driving&&locked)near(stored->value,value);
            near(f.target.find_point(center)->x,2);near(f.target.find_point(center)->y,3);
            f.target=Sketch::from_serialized(f.target.serialized());
        }
    }
    std::cout<<cases<<" centered ellipse major/minor/rotation driving/reference/lock/edit/drag cases passed\n";
}
}
int main()try{centers_and_constraints();point_dimensions();incompatible_sources();centered_native_curves();centered_radius_dimensions();centered_angle_dimensions();centered_ellipse_dimensions();native_sessions();return 0;}catch(const std::exception& error){std::cerr<<context<<": "<<error.what()<<'\n';return 1;}
