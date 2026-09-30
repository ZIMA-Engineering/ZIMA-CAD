#include <zima/sketcher/sketch.hpp>
#include "../modules/sketcher/src/rectilinear_template_solver.hpp"
#include <cmath>
#include <iostream>
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
void check_branches(const Sketch& sketch,std::size_t changed,double value) {
    for(std::size_t i=0;i<sketch.dimensions.size();++i) {
        const auto& a=sketch.points[2*i];const auto& b=sketch.points[2*i+1];
        require(near(a.x,i*10.)&&near(a.y,i*3.),"Fixed anchor moved");
        require(near(b.x-a.x,i==changed?value:5.)&&near(b.y,a.y),"Edited or independent branch changed incorrectly");
    }
}
}
int main() {
    try {
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
