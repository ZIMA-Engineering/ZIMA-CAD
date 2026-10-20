#include <zima/sketcher/sketch.hpp>
#include <zima/document/document_session.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <filesystem>

using namespace zima::sketcher;
namespace {
std::string context;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b){check(std::abs(a-b)<1e-7,"Equal-circle geometric equation differs");}
struct Fixture {
    Sketch sketch=Sketch::create_default();
    std::array<std::string,3> circles,centers;
    std::string length,width;
    bool vertical{},swapped{};
    int expected_dof{2};
};
Fixture make(bool vertical=false,bool swap=false,int side=1,bool external=false) {
    Fixture f;f.vertical=vertical;f.swapped=swap;
    const std::array<std::array<double,2>,3> positions{{{-199.1572872033782,0},{199.1572872033782,0},{73.35541534423828,-125.56172943114277}}};
    for(int i=0;i<3;++i) {
        const double x=positions[i][0],y=side*positions[i][1];
        f.circles[i]=f.sketch.add_circle(vertical?y:x,vertical?x:y,23.35305620547053);
        f.centers[i]=std::ranges::find(f.sketch.circles,f.circles[i],&SketchCircle::id)->center_point_id;
    }
    std::string baseline=vertical?"sketch_axis:y":"sketch_axis:x",axis=vertical?"sketch_axis:x":"sketch_axis:y";
    if(external) {
        auto ref=Sketch::create_external_reference(ExternalReferenceKind::Axis);ref.source_document_id="read-only-axis";
        ref.source_owner_id="axis";ref.source_semantic_key="baseline";ref.infinite=true;
        ref.cached_points=vertical?std::vector<std::array<double,2>>{{0,0},{0,1}}:std::vector<std::array<double,2>>{{0,0},{1,0}};
        f.sketch.add_external_reference(ref);baseline=ref.id;
    }
    for(int i:{0,1})static_cast<void>(f.sketch.add_point_on_line_constraint(f.centers[i],baseline));
    for(int i:{0,1})static_cast<void>(f.sketch.add_equal_radius_constraint(f.circles[swap?i+1:i],f.circles[swap?i:i+1]));
    static_cast<void>(f.sketch.add_symmetric_constraint(f.centers[swap?1:0],f.centers[swap?0:1],axis));
    for(int i:{0,1}){auto edge=Sketch::create_segment(f.centers[i],f.centers[2]);edge.construction=true;f.sketch.segments.push_back(edge);}
    auto length=f.sketch.create_point_dimension(f.centers[swap?2:0],f.centers[swap?0:2],DimensionKind::Distance);f.length=length.id;f.sketch.apply_dimension(length);
    auto width=f.sketch.create_point_dimension(f.centers[swap?1:0],f.centers[swap?0:1],vertical?DimensionKind::DistanceY:DimensionKind::DistanceX);f.width=width.id;f.sketch.apply_dimension(width);
    return f;
}
void verify(const Fixture& f) {
    const auto* a=f.sketch.find_point(f.centers[0]);const auto* b=f.sketch.find_point(f.centers[1]);const auto* c=f.sketch.find_point(f.centers[2]);
    near(f.vertical?a->x:a->y,0);near(f.vertical?b->x:b->y,0);near(f.vertical?a->y:a->x,-(f.vertical?b->y:b->x));
    for(const auto& d:f.sketch.dimensions)if(!d.suppressed&&d.driving) {
        if(d.id==f.length)near(std::hypot(c->x-a->x,c->y-a->y),d.value);
        if(d.id==f.width)near((f.swapped?-1:1)*(f.vertical?b->y-a->y:b->x-a->x),d.value);
        if(d.geometry_id==f.circles[0]&&(d.kind==DimensionKind::Radius||d.kind==DimensionKind::Diameter)) {
            const auto r=std::ranges::find(f.sketch.circles,f.circles[0],&SketchCircle::id)->radius;
            near(r,d.value*(d.kind==DimensionKind::Diameter?.5:1.));
        }
        if(d.first_point_id==f.centers[2]&&d.second_point_id.empty())near(f.vertical?c->x:c->y,d.value);
    }
    const auto radius=[&](int i){return std::ranges::find(f.sketch.circles,f.circles[i],&SketchCircle::id)->radius;};
    near(radius(0),radius(1));near(radius(1),radius(2));
    auto checked=f.sketch;const auto result=checked.solve();
    if(result.status==SolveStatus::Conflicting||result.status==SolveStatus::Invalid||result.maximum_residual>=1e-8)
        throw std::runtime_error("Equal-circle result does not solve: status="+std::to_string(static_cast<int>(result.status))+" residual="+std::to_string(result.maximum_residual));
    check(result.remaining_degrees_of_freedom==f.expected_dof,"Equal-circle degrees of freedom differ");
}
}
int main(int argc,char** argv){try {
    int cases=0;
    for(bool vertical:{false,true})for(bool swap:{false,true})for(int side:{-1,1})for(bool external:{false,true})
    for(bool full:{false,true})for(bool locked:{false,true})for(bool reference:{false,true})for(bool length:{true,false}) {
        context="case="+std::to_string(cases)+" vertical="+std::to_string(vertical)+" swap="+std::to_string(swap)+" side="+std::to_string(side)+" external="+std::to_string(external)+" full="+std::to_string(full)+" locked="+std::to_string(locked)+" reference="+std::to_string(reference)+" length="+std::to_string(length);
        auto f=make(vertical,swap,side,external);const auto sources=f.sketch.external_references;
        const auto id=length?f.length:f.width;
        if(full) {
            auto radius=f.sketch.create_circle_radius_dimension(f.circles[0]);f.sketch.apply_dimension(radius);
            auto coordinate=f.sketch.create_axis_dimension(f.centers[2],vertical?"sketch_axis:y":"sketch_axis:x");f.sketch.apply_dimension(coordinate);
        }
        for(auto& d:f.sketch.dimensions){d.locked=locked;if(d.id==id){d.driving=!reference;if(reference)d.locked=false;}}
        f.expected_dof=(full?0:2)+(reference?1:0);
        verify(f);
        const auto dimension=std::ranges::find(f.sketch.dimensions,id,&SketchDimension::id);const double initial=dimension->value;
        for(double factor:{1.01,1.02,1.}) {
            const auto phase=context;context+=" edit="+std::to_string(factor);
            const auto before=f.sketch.points;
            check(f.sketch.set_dimension_value(id,initial*factor),"Cannot edit the reported symmetric equal-circle dimension");
            if(reference)check(f.sketch.points==before,"Reference edit moved equal circles");
            verify(f);f.sketch=Sketch::from_serialized(f.sketch.serialized());verify(f);
            context=phase;
        }
        for(double step:{.3,-.2,.1}) {
            const auto phase=context;context+=" drag="+std::to_string(step);
            const auto before=f.sketch;const auto p=*f.sketch.find_point(f.centers[2]);
            static_cast<void>(f.sketch.move_point(p.id,p.x+step,p.y+step));verify(f);
            const auto* q=f.sketch.find_point(f.centers[2]);
            if(full&&locked&&!reference)check(f.sketch.points==before.points,"Locked fully constrained circles escaped");
            else check(std::hypot(q->x-p.x,q->y-p.y)>1e-7,"Available equal-circle centre did not move");
            context=phase;
        }
        check(f.sketch.external_references==sources,"Equal-circle fit modified read-only supports");
        if(!reference) {
            const auto before=f.sketch.serialized();
            check(!f.sketch.set_dimension_value(f.length,0),"Collapsed circle-centre distance was accepted");
            check(f.sketch.serialized()==before,"Rejected circle edit changed the document");
        }
        ++cases;
    }
    int base_drags=0;
    for(bool vertical:{false,true})for(bool swap:{false,true})for(int side:{-1,1})for(bool external:{false,true})for(bool locked:{false,true})for(int handle:{0,1}) {
        auto f=make(vertical,swap,side,external);for(auto& d:f.sketch.dimensions)d.locked=locked;
        context="base-centre drag="+std::to_string(base_drags);
        for(double step:{.3,-.2,.1}) {
            const auto before=f.sketch.serialized();const auto p=*f.sketch.find_point(f.centers[handle]);
            const bool moved=f.sketch.move_point(p.id,p.x+(vertical?0:step),p.y+(vertical?step:0));verify(f);
            const auto* q=f.sketch.find_point(p.id);
            if(locked)check(!moved&&f.sketch.serialized()==before,"Locked base-centre drag changed the Sketch");
            else check(moved&&std::hypot(p.x-q->x,p.y-q->y)>1e-7,"Unlocked base centre did not move");
        }
        ++base_drags;
    }
    for(bool diameter:{false,true}) {
        auto f=make();auto d=diameter?f.sketch.create_circle_diameter_dimension(f.circles[0]):f.sketch.create_circle_radius_dimension(f.circles[0]);f.sketch.apply_dimension(d);f.expected_dof=1;
        for(double factor:{1.01,1.02,1.}){check(f.sketch.set_dimension_value(d.id,d.value*factor),"Equal-circle radius edit failed");verify(f);}
    }
    if(argc>1) {
        auto f=make();auto owner=zima::document::PartDocument::create_sketch_container();owner.feature.sketch_id=f.sketch.id;f.sketch.owner_container_id=owner.id;
        auto doc=zima::document::PartDocument::create_default();doc.sketches={f.sketch};doc.history={owner};zima::document::BodyHistoryGraph graph;
        static_cast<void>(graph.create_body("Equal circles"));graph.insert({zima::document::PartHistoryKind::Feature,owner.id});doc.set_body_history(graph);doc.resolve_constructions();
        zima::document::DocumentSession session(doc);auto edited=doc;check(edited.sketches.front().set_dimension_value(f.length,303.),"Document circle edit failed");session.commit(edited);
        check(session.undo()&&session.document().serialized()==doc.serialized(),"Equal-circle Undo failed");check(session.redo(),"Equal-circle Redo failed");
        session.document().save(std::filesystem::path(argv[1]));auto reopened=zima::document::PartDocument::load(std::filesystem::path(argv[1]));
        f.sketch=reopened.sketches.front();verify(f);
    }
    std::cout<<cases<<" equal-circle orientation/order/side/support/state/edit cases, "<<base_drags<<" base-centre drag cases and radius/diameter edits passed\n";return 0;
}catch(const std::exception& error){std::cerr<<context<<": "<<error.what()<<'\n';return 1;}}
