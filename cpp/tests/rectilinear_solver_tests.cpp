#include <zima/sketcher/sketch.hpp>
#include <zima/sketcher/sketch_trim.hpp>
#include <zima/document/document_session.hpp>
#include <zima/document/part_document.hpp>
#include "../modules/sketcher/src/rectilinear_template_solver.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <ranges>
#include <set>
#include <stdexcept>

namespace {
using namespace zima::sketcher;
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
bool near(double a,double b){return std::abs(a-b)<1e-7;}
Sketch branches(std::size_t count,bool solved=true,bool fixed=true) {
    auto sketch=Sketch::create_default();
    for(std::size_t i=0;i<count;++i) {
        auto a=Sketch::create_point(i*10.,i*3.);a.fixed=fixed;
        auto b=Sketch::create_point(a.x+(solved?5.:3.),a.y+(solved?0.:.75));
        sketch.points.push_back(a);sketch.points.push_back(b);
        auto segment=Sketch::create_segment(a.id,b.id);
        sketch.segments.push_back(segment);
        sketch.constraints.push_back({"h:"+std::to_string(i),ConstraintKind::Horizontal,a.id,b.id,false,segment.id});
        sketch.dimensions.push_back({"d:"+std::to_string(i),DimensionKind::Distance,a.id,b.id,5.});
    }
    return sketch;
}
Sketch parallel_profile() {
    auto sketch=Sketch::create_default();
    const auto point=[&](const char* id,double x,double y) {
        auto value=Sketch::create_point(x,y);value.id=id;
        sketch.points.push_back(std::move(value));
    };
    point("p:e6",27.75185187952404,314.94928776140705);
    point("p:e9",17.5,314.9492877614072);
    point("p:eb",17.5,326.9492877716725);
    point("p:ee",10.,326.94928777167263);
    point("p:f1",10.000000000000004,309.9492877716725);
    point("p:f4",20.,309.94928772554357);
    point("p:f7",79.8917007446289,0.);
    point("p:fa",84.93704986572266,19.006591796875);
    point("p:fb",224.9999999999999,19.006591796875);
    point("p:00",224.99999999999994,0.);
    point("p:center-start",0.,0.);
    point("p:center-end",0.,326.94928777167263);
    const auto segment=[&](const char* id,const char* first,const char* second,
            bool construction=false,bool centerline=false,bool visible=true) {
        auto value=Sketch::create_segment(first,second,construction);value.id=id;
        value.centerline=centerline;value.visible_in_3d=visible;
        sketch.segments.push_back(std::move(value));
    };
    segment("s:ea","p:e6","p:e9");segment("s:ec","p:e9","p:eb");
    segment("s:ef","p:eb","p:ee");segment("s:f2","p:ee","p:f1");
    segment("s:f5","p:f1","p:f4");segment("s:f8","p:f4","p:f7");
    segment("s:fc","p:fa","p:fb");segment("s:01","p:fb","p:00");
    segment("s:04","p:00","p:f7");segment("s:e7","p:fa","p:e6");
    segment("s:center","p:center-start","p:center-end",true,true,false);
    const auto constraint=[&](const char* id,ConstraintKind kind,
            const char* first="",const char* second="",
            const char* geometry="",const char* second_geometry="") {
        SketchConstraint value;value.id=id;value.kind=kind;
        value.first_point_id=first;value.second_point_id=second;
        value.geometry_id=geometry;value.second_geometry_id=second_geometry;
        sketch.constraints.push_back(std::move(value));
    };
    constraint("c:ed",ConstraintKind::Vertical,"p:e9","p:eb");
    constraint("c:f0",ConstraintKind::Horizontal,"p:eb","p:ee");
    constraint("c:f3",ConstraintKind::Vertical,"p:ee","p:f1");
    constraint("c:f6",ConstraintKind::Parallel,"","","s:ea","s:f5");
    constraint("c:f9",ConstraintKind::PointOnLine,"p:f7","","sketch_axis:x");
    constraint("c:fe",ConstraintKind::Horizontal,"p:fa","p:fb");
    constraint("c:02",ConstraintKind::PointOnLine,"p:00","","sketch_axis:x");
    constraint("c:03",ConstraintKind::Vertical,"p:fb","p:00");
    constraint("c:07",ConstraintKind::Horizontal,"p:e6","p:e9");
    constraint("c:10",ConstraintKind::Parallel,"","","s:f8","s:e7");
    constraint("c:7d",ConstraintKind::PointReference,"p:center-start","sketch_origin");
    constraint("c:7e",ConstraintKind::PointOnLine,"p:center-end","","sketch_axis:y");
    constraint("c:80",ConstraintKind::Horizontal,"p:ee","p:center-end");
    const auto dimension=[&](const char* id,DimensionKind kind,double value,
            const char* first="",const char* second="",
            const char* geometry="",const char* second_geometry="",int side=1) {
        SketchDimension d;d.id=id;d.kind=kind;d.value=value;
        d.first_point_id=first;d.second_point_id=second;
        d.geometry_id=geometry;d.second_geometry_id=second_geometry;
        d.solution_side=side;sketch.dimensions.push_back(std::move(d));
    };
    dimension("d:09",DimensionKind::DistanceSymmetric,20.,"p:ee","","sketch_axis:y","",-1);
    dimension("d:0b",DimensionKind::DistanceSymmetric,35.,"p:eb","","sketch_axis:y","",-1);
    dimension("d:0d",DimensionKind::Distance,12.000000010265296,"p:e9","p:eb");
    dimension("d:0f",DimensionKind::DistanceLine,4.999999989734829,"","","s:ea","s:f5");
    dimension("d:12",DimensionKind::DistanceLine,8.559668752743791,"","","s:f8","s:e7");
    dimension("d:14",DimensionKind::DistanceSymmetric,449.9999999999999,"p:00","","sketch_axis:y","",-1);
    dimension("d:1b",DimensionKind::DistanceSymmetric,169.8740997314453,"p:fa","","sketch_axis:y","",-1);
    dimension("d:1d",DimensionKind::DistanceSymmetric,40.,"p:f4","","sketch_axis:y","",-1);
    dimension("d:80",DimensionKind::DistanceY,-19.006591796875,"p:fb","p:00");
    dimension("d:a5",DimensionKind::DistanceY,326.9492877716725,"p:00","p:eb");
    sketch.validate();return Sketch::from_serialized(sketch.serialized());
}
const SketchSegment& parallel_segment(const Sketch& sketch,const std::string& id) {
    const auto found=std::ranges::find(sketch.segments,id,&SketchSegment::id);
    require(found!=sketch.segments.end(),"Parallel profile segment identity missing");
    return *found;
}
std::array<double,4> parallel_line(const Sketch& sketch,const std::string& id) {
    if(id=="sketch_axis:x")return {0.,0.,1.,0.};
    if(id=="sketch_axis:y")return {0.,0.,0.,1.};
    const auto& segment=parallel_segment(sketch,id);
    const auto* a=sketch.find_point(segment.first_point_id);
    const auto* b=sketch.find_point(segment.second_point_id);
    require(a&&b,"Parallel profile line endpoint missing");
    const double length=std::hypot(b->x-a->x,b->y-a->y);
    require(length>1e-9,"Parallel profile line collapsed");
    return {a->x,a->y,(b->x-a->x)/length,(b->y-a->y)/length};
}
double parallel_dimension_measurement(const Sketch& sketch,const SketchDimension& dimension) {
    if(dimension.kind==DimensionKind::Distance) {
        const auto* a=sketch.find_point(dimension.first_point_id);
        const auto* b=sketch.find_point(dimension.second_point_id);
        require(a&&b,"Parallel profile distance point missing");
        return std::hypot(b->x-a->x,b->y-a->y);
    }
    if(dimension.kind==DimensionKind::DistanceY) {
        const auto* a=sketch.find_point(dimension.first_point_id);
        const auto* b=sketch.find_point(dimension.second_point_id);
        require(a&&b,"Parallel profile projected point missing");
        return b->y-a->y;
    }
    if(dimension.kind==DimensionKind::DistanceSymmetric) {
        const auto* point=sketch.find_point(dimension.first_point_id);
        const auto support=parallel_line(sketch,dimension.geometry_id);
        require(point!=nullptr,"Parallel profile symmetric point missing");
        return 2.*std::abs(support[2]*(point->y-support[1])-
            support[3]*(point->x-support[0]));
    }
    if(dimension.kind==DimensionKind::DistanceLine) {
        const auto support=parallel_line(sketch,dimension.geometry_id);
        const auto driven=parallel_line(sketch,dimension.second_geometry_id);
        return support[2]*(driven[1]-support[1])-
            support[3]*(driven[0]-support[0]);
    }
    throw std::runtime_error("Unchecked parallel profile dimension");
}
void verify_parallel_profile(const Sketch& sketch) {
    for(const auto& constraint:sketch.constraints)if(!constraint.suppressed) {
        const auto* a=sketch.find_point(constraint.first_point_id);
        const auto* b=sketch.find_point(constraint.second_point_id);
        if(constraint.kind==ConstraintKind::Horizontal)
            require(a&&b&&near(a->y,b->y),"Parallel profile horizontal equation failed");
        else if(constraint.kind==ConstraintKind::Vertical)
            require(a&&b&&near(a->x,b->x),"Parallel profile vertical equation failed");
        else if(constraint.kind==ConstraintKind::Parallel) {
            const auto first=parallel_line(sketch,constraint.geometry_id);
            const auto second=parallel_line(sketch,constraint.second_geometry_id);
            require(near(first[2]*second[3]-first[3]*second[2],0.),
                "Parallel profile parallel equation failed");
        } else if(constraint.kind==ConstraintKind::PointOnLine) {
            const auto line=parallel_line(sketch,constraint.geometry_id);
            require(a&&near(line[2]*(a->y-line[1])-line[3]*(a->x-line[0]),0.),
                "Parallel profile point-on-line equation failed");
        } else if(constraint.kind==ConstraintKind::PointReference) {
            require(a&&constraint.second_point_id=="sketch_origin"&&near(a->x,0.)&&near(a->y,0.),
                "Parallel profile Origin equation failed");
        } else throw std::runtime_error("Unchecked parallel profile constraint");
    }
    for(const auto& dimension:sketch.dimensions)if(!dimension.suppressed) {
        const double measured=parallel_dimension_measurement(sketch,dimension);
        if(dimension.kind==DimensionKind::DistanceLine)
            require(near(measured,dimension.solution_side*dimension.value),
                "Parallel profile line-distance equation failed");
        else require(near(measured,dimension.value),
            "Parallel profile dimension equation failed");
    }
    auto solved=sketch;const auto result=solved.solve();
    require(result.status!=SolveStatus::Conflicting&&result.status!=SolveStatus::Invalid&&
        result.maximum_residual<1e-7,"Parallel profile common solve failed");
}
void normalize_parallel_dimensions(Sketch& sketch) {
    for(auto& dimension:sketch.dimensions) {
        const double measured=parallel_dimension_measurement(sketch,dimension);
        if(dimension.kind==DimensionKind::DistanceLine) {
            dimension.value=std::abs(measured);
            dimension.solution_side=std::signbit(measured)?-1:1;
        } else dimension.value=measured;
    }
}
Sketch parallel_variant(const Sketch& source,int sx,int sy,bool reverse) {
    auto sketch=source;
    for(auto& point:sketch.points){point.x*=sx;point.y*=sy;}
    if(reverse) {
        std::ranges::reverse(sketch.constraints);
        for(auto& constraint:sketch.constraints)
            if(constraint.kind==ConstraintKind::Parallel)
                std::swap(constraint.geometry_id,constraint.second_geometry_id);
        std::ranges::reverse(sketch.dimensions);
        for(auto& dimension:sketch.dimensions) {
            if(dimension.kind==DimensionKind::DistanceLine)
                std::swap(dimension.geometry_id,dimension.second_geometry_id);
            else if(dimension.kind==DimensionKind::Distance||
                    dimension.kind==DimensionKind::DistanceY)
                std::swap(dimension.first_point_id,dimension.second_point_id);
        }
    }
    normalize_parallel_dimensions(sketch);
    sketch=Sketch::from_serialized(sketch.serialized());
    verify_parallel_profile(sketch);return sketch;
}
zima::document::PartDocument parallel_document(Sketch sketch) {
    using namespace zima::document;
    auto document=PartDocument::create_default();
    auto feature=PartDocument::create_extrusion_container(sketch.id);
    feature.name="Parallel profile";
    feature.extrusion.height=feature.extrusion.length_forward=10.;
    sketch.owner_container_id=feature.id;
    document.history={feature};document.sketches={std::move(sketch)};
    BodyHistoryGraph graph;static_cast<void>(graph.create_body("Parallel profile"));
    graph.insert({PartHistoryKind::Feature,feature.id});document.set_body_history(graph);
    document.resolve_constructions();return document;
}
void verify_parallel_profile_matrix() {
    const auto source=parallel_profile();verify_parallel_profile(source);
    std::size_t edits{};
    for(const int sx:{-1,1})for(const int sy:{-1,1})for(const bool reverse:{false,true})
    for(const bool locked:{false,true}) {
        const auto base=parallel_variant(source,sx,sy,reverse);
        for(const auto& original:base.dimensions) {
            auto sketch=base;
            for(auto& dimension:sketch.dimensions)dimension.locked=locked;
            for(const double factor:{1.01,.99,1.02}) {
                const auto found=std::ranges::find(sketch.dimensions,original.id,&SketchDimension::id);
                require(found!=sketch.dimensions.end(),"Parallel profile dimension identity missing");
                const double displayed=dimension_display_value(*found,sketch.plane);
                require(sketch.set_dimension_value(found->id,displayed*factor),
                    "Parallel profile dimension edit failed");
                verify_parallel_profile(sketch);
                sketch=Sketch::from_serialized(sketch.serialized());
                verify_parallel_profile(sketch);++edits;
            }
        }
    }
    {
        auto reference=source;
        for(auto& dimension:reference.dimensions){dimension.driving=false;dimension.locked=false;}
        verify_parallel_profile(reference);
        std::vector<std::string> ids;
        for(const auto& dimension:reference.dimensions)ids.push_back(dimension.id);
        for(const auto& id:ids) {
            const auto dimension=std::ranges::find(
                reference.dimensions,id,&SketchDimension::id);
            require(dimension!=reference.dimensions.end(),
                "Parallel profile reference dimension identity missing");
            const auto before=reference.serialized();
            require(reference.set_dimension_value(id,
                dimension_display_value(*dimension,reference.plane)+5.),
                "Parallel profile reference dimension refresh failed");
            require(reference.serialized()==before,
                "Parallel profile reference dimension moved geometry");
        }
    }
    {
        auto unlocked=source;const auto old=*unlocked.find_point("p:e6");
        require(unlocked.move_point(old.id,old.x+.5,old.y-.25),
            "Unlocked parallel profile point drag failed");
        require(std::hypot(unlocked.find_point(old.id)->x-old.x,
            unlocked.find_point(old.id)->y-old.y)>.05,
            "Unlocked parallel profile point did not move");
        verify_parallel_profile(unlocked);
        const auto next=*unlocked.find_point(old.id);
        require(unlocked.move_point(next.id,next.x-.3,next.y+.2),
            "Successive parallel profile point drag failed");
        verify_parallel_profile(unlocked);
    }
    {
        auto locked=source;for(auto& dimension:locked.dimensions)dimension.locked=true;
        const auto before=locked.serialized();const auto point=*locked.find_point("p:e6");
        static_cast<void>(locked.move_point(point.id,point.x+.5,point.y-.25));
        require(locked.serialized()==before,"Locked parallel profile escaped its dimensions");
        verify_parallel_profile(locked);
    }
    {
        auto under=source;under.remove_dimension("d:14");
        for(auto& dimension:under.dimensions)dimension.locked=true;
        const auto point=*under.find_point("p:00");
        require(under.move_point(point.id,point.x+3.,point.y),
            "Underconstrained parallel profile drag failed");
        require(std::abs(under.find_point(point.id)->x-point.x)>1.,
            "Underconstrained parallel profile did not move");
        verify_parallel_profile(under);
    }
    {
        auto impossible=source;for(auto& point:impossible.points)point.fixed=true;
        const auto before=impossible.serialized();
        require(!impossible.set_dimension_value("d:0b",36.)&&
            impossible.serialized()==before,
            "Impossible parallel profile edit was not atomic");
        auto redundant=source;auto duplicate=redundant.dimensions.front();
        duplicate.id="d:duplicate";bool rejected=false;const auto original=redundant.serialized();
        try{redundant.apply_dimension(duplicate);}catch(const std::exception&){rejected=true;}
        require(rejected&&redundant.serialized()==original,
            "Redundant parallel profile dimension changed the Sketch");
    }
    {
        auto document=parallel_document(source);
        const auto before=document.sketches.front().serialized();
        zima::document::DocumentSession session(document);
        auto changed=document;
        require(changed.sketches.front().set_dimension_value("d:0b",36.),
            "Parallel profile document edit failed");
        session.commit(changed);const auto accepted=session.document().sketches.front().serialized();
        require(session.undo()&&session.document().sketches.front().serialized()==before,
            "Parallel profile Undo failed");
        require(session.redo()&&session.document().sketches.front().serialized()==accepted,
            "Parallel profile Redo failed");
        verify_parallel_profile(session.document().sketches.front());
#ifdef ZIMA_TEST_BINARY_DIR
        const auto path=std::filesystem::path(ZIMA_TEST_BINARY_DIR)/"parallel-profile-ui.prtz";
#else
        const auto path=std::filesystem::path("parallel-profile-ui.prtz");
#endif
        auto ui=parallel_document(source);
        const auto first=std::ranges::find(ui.sketches.front().dimensions,"d:0b",&SketchDimension::id);
        std::rotate(ui.sketches.front().dimensions.begin(),first,std::next(first));
        ui.save(path);const auto reopened=zima::document::PartDocument::load(path);
        require(reopened.sketches.front().serialized()==ui.sketches.front().serialized(),
            "Parallel profile native reopen changed the Sketch");
        verify_parallel_profile(reopened.sketches.front());
    }
    std::cout<<"Parallel profile matrix: "<<edits
        <<" dimension edits, both participant orders, four orientations, locked/unlocked, "
          "driving/reference, successive drags, atomic rejection, native reopen and Undo/Redo passed\n";
}
void check_branches(const Sketch& sketch,std::size_t changed,double value) {
    for(std::size_t i=0;i<sketch.dimensions.size();++i) {
        const auto& a=sketch.points[2*i];const auto& b=sketch.points[2*i+1];
        require(near(a.x,i*10.)&&near(a.y,i*3.),"Fixed anchor moved");
        require(near(b.x-a.x,i==changed?value:5.)&&near(b.y,a.y),"Edited or independent branch changed incorrectly");
    }
}
#include "capsule_dimension_matrix.inc"
}
int main() {
    try {
        verify_parallel_profile_matrix();
        verify_capsule_dimensions();
        // Large disconnected input, both already solved and initially unsolved.
        // No wall-clock pass threshold: correctness remains machine-independent.
        for(const bool solved:{true,false}) {
            auto sketch=branches(1000,solved);
            require(sketch.set_dimension_value("d:999",6.),"Large dimension edit failed");
            check_branches(sketch,999,6.);
            const auto state=sketch.solve();
            require(state.status==SolveStatus::Solved&&state.remaining_degrees_of_freedom==0&&state.maximum_residual<1e-7,"Solved branch status changed");
            sketch=Sketch::from_serialized(sketch.serialized());
            require(sketch.set_dimension_value("d:0",-7.),"Reversed distance edit failed after round trip");
            require(near(sketch.points[1].x-sketch.points[0].x,-7.),"Distance branch did not reverse");
            require(near(sketch.points.back().x-sketch.points[1998].x,6.),"Independent edited branch was lost");
        }
        // EqualLength connects otherwise separate segments. Preserve the
        // minimum-norm solution and the unconstrained midpoint of each segment.
        {
            auto sketch=branches(2,true,false);sketch.dimensions.resize(1);
            SketchConstraint equal;equal.id="equal";equal.kind=ConstraintKind::EqualLength;
            equal.geometry_id=sketch.segments[0].id;equal.second_geometry_id=sketch.segments[1].id;
            sketch.constraints.push_back(equal);sketch.dimensions[0].value=8.;
            require(seed_rectilinear_equations(sketch,{}),"Coupled seed failed");
            for(std::size_t i=0;i<2;++i) {
                require(near(sketch.points[2*i].x,i*10.-1.5)&&near(sketch.points[2*i+1].x,i*10.+6.5),"Coupled free-coordinate solution changed");
                require(near(sketch.points[2*i].y,i*3.)&&near(sketch.points[2*i+1].y,i*3.),"Free vertical coordinate changed");
            }
        }
        // A conflicting block must not publish successful changes from another.
        {
            auto sketch=branches(3);sketch.points.back().fixed=true;
            sketch.dimensions[0].value=7.;sketch.dimensions.back().value=9.;
            const auto before=sketch.serialized();
            require(!seed_rectilinear_equations(sketch,{}),"Conflicting block accepted");
            require(sketch.serialized()==before,"Partial seed escaped on failure");
            auto edit=branches(3);edit.points.back().fixed=true;
            const auto edit_before=edit.serialized();
            require(!edit.set_dimension_value("d:2",9.)&&edit.serialized()==edit_before,"Failed dimension transaction changed the Sketch");
        }
        // Zero-support rows still detect incompatible equations.
        {
            auto sketch=branches(1);auto& d=sketch.dimensions[0];
            d.kind=DimensionKind::DistanceX;d.second_point_id=d.first_point_id;d.value=1.;
            const auto points=sketch.points;
            require(!seed_rectilinear_equations(sketch,{})&&sketch.points==points,"Zero row conflict was ignored");
        }
        // Independent nonlinear distances keep their original direction while
        // Newton steps update the equation coefficients for each pair.
        {
            auto sketch=branches(2);sketch.constraints.clear();
            for(std::size_t i=0;i<2;++i) {
                sketch.points[2*i+1].x=sketch.points[2*i].x+3.;
                sketch.points[2*i+1].y=sketch.points[2*i].y+(i==0?4.:-4.);
                sketch.dimensions[i].value=10.;
            }
            require(seed_rectilinear_equations(sketch,{},true),"Nonlinear blocks failed");
            for(std::size_t i=0;i<2;++i) {
                require(near(sketch.points[2*i+1].x-sketch.points[2*i].x,6.)&&
                    near(sketch.points[2*i+1].y-sketch.points[2*i].y,i==0?8.:-8.),"Nonlinear distance changed its branch");
            }
        }
        // Signed zero remains authored data while another block is solved.
        for(const double value:{0.0,-0.0}) {
            auto sketch=branches(2);auto& d=sketch.dimensions[0];
            d.kind=DimensionKind::DistanceY;d.value=value;
            sketch.dimensions[1].value=8.;
            require(seed_rectilinear_equations(sketch,{}),"Signed zero seed failed");
            require(std::signbit(d.value)==std::signbit(value),"Signed zero was normalized");
            require(near(sketch.points[3].x-sketch.points[2].x,8.),"Independent signed-zero companion did not solve");
        }
        std::cout<<"Rectilinear equation blocks: large edits, coupled free coordinates, conflict rollback, persistence and signed zero passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
