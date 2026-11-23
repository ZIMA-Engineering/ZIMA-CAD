#include "../modules/cam/src/native_simulation.hpp"
#include <zima/cam/facing.hpp>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace {
using namespace zima::cam;
unsigned checks{};
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
void close(double actual,double expected){check(std::abs(actual-expected)<1e-5,"Unexpected remaining/removed volume");}
TopoDS_Shape box(Point origin,Point size){return BRepPrimAPI_MakeBox(gp_Pnt(origin.x,origin.y,origin.z),size.x,size.y,size.z).Shape();}
std::string serialized(const TopoDS_Shape& shape){std::ostringstream out;BRepTools::Write(shape,out);return out.str();}
CompiledProgram program(Point start,Point end,bool rapid=false) {
    Program p;p.initial_tip=start;
    Block tool;tool.id="tool";tool.operation_id="operation";tool.kind=BlockKind::Tool;tool.tool_id="T1";p.blocks.push_back(tool);
    Block feed;feed.id="feed";feed.operation_id="operation";feed.kind=BlockKind::Feed;feed.value=200;p.blocks.push_back(feed);
    Block spindle;spindle.id="spindle";spindle.operation_id="operation";spindle.kind=BlockKind::Spindle;spindle.value=1000;p.blocks.push_back(spindle);
    Block move;move.id="move";move.operation_id="operation";move.kind=BlockKind::Motion;
    move.motion=rapid?MotionKind::Rapid:MotionKind::Linear;move.end=end;p.blocks.push_back(move);
    Machine machine;machine.id="machine";machine.name="Test machine";
    return compile(p,machine,{{"T1","Tool",{{-5,1},{0,1}},{{0,2},{10,2}}}});
}
bool has(const native::SimulationResult& result,CollisionKind kind,const std::string& obstacle={}) {
    return std::any_of(result.collisions.begin(),result.collisions.end(),[&](const Collision& c){
        return c.kind==kind&&c.block_id=="move"&&c.tool_id=="T1"&&c.obstacle_id==obstacle&&c.intersection_mm3>0;});
}
}
int main() {
    try {
        const std::vector<Tool> tools{{"T1","Tool",{{-5,1},{0,1}},{{0,2},{10,2}}}};
        const auto stock=box({-5,-5,0},{20,10,10}),target=box({-5,-5,0},{20,10,3});
        const auto original_stock=serialized(stock),original_target=serialized(target);
        const auto cut=program({0,0,5},{10,0,5});
        std::size_t callbacks{};
        auto result=native::simulate(cut,tools,stock,target,{},1e-7,[&](auto index,auto count){
            check(index==callbacks&&count==1,"Invalid simulation progress");++callbacks;});
        const double removed=5*(20+std::numbers::pi);
        close(result.removed_mm3,removed);close(native::volume(result.remaining),2000-removed);
        check(result.collisions.empty(),"Safe cutting move reported a collision");
        close(result.missing_target_mm3,0);
        check(result.counts.swept_volumes==2&&result.counts.stock_cuts==1&&callbacks==2,"Unexpected calculation counts");
        check(serialized(stock)==original_stock&&serialized(target)==original_target,"Simulation modified source geometry");
        const auto second=native::simulate(program({5,-3,5},{5,3,5}),tools,result.remaining,target,{});
        close(second.removed_mm3,5*(8+std::numbers::pi));
        close(native::volume(second.remaining),2000-removed-second.removed_mm3);
        const auto repeated=native::simulate(cut,tools,result.remaining,target,{});
        close(repeated.removed_mm3,0);check(repeated.counts.stock_cuts==0,"Repeated air cut recalculated stock");
        result=native::simulate(program({0,0,5},{10,0,5},true),tools,stock,target,{});
        close(result.removed_mm3,0);close(native::volume(result.remaining),2000);
        check(has(result,CollisionKind::RapidStock),"Rapid stock collision was missed");
        check(result.counts.stock_cuts==0,"Rapid move removed material");
        const std::vector<native::Obstacle> fixtures{{"vise-cutter",box({4,-.5,6},{1,1,1})},
            {"vise-shank",box({4,-.5,12},{1,1,1})},{"distant",box({80,80,80},{1,1,1})}};
        const auto fixture_before=serialized(fixtures[0].shape);
        result=native::simulate(cut,tools,stock,target,fixtures);
        check(has(result,CollisionKind::CuttingFixture,"vise-cutter"),"Cutter/fixture collision was missed");
        check(has(result,CollisionKind::NonCuttingFixture,"vise-shank"),"Shank/fixture collision was missed");
        check(result.collisions.size()==2&&result.counts.bounds_rejections>=2,"Unrelated fixture produced a collision");
        check(serialized(fixtures[0].shape)==fixture_before,"Simulation modified a fixture");
        result=native::simulate(program({0,0,1},{10,0,1}),tools,stock,target,{});
        check(has(result,CollisionKind::NonCuttingStock),"Shank/stock collision was missed");
        check(has(result,CollisionKind::TargetGouge),"Target gouge was missed");
        const auto small=box({4,-.2,6},{1,.4,1});
        result=native::simulate(cut,tools,small,target,{});close(result.removed_mm3,.4);close(native::volume(result.remaining),0);
        close(result.missing_target_mm3,600);
        auto twice=cut;twice.motions.push_back(cut.motions.front());twice.motions.back().block_id="repeat";
        result=native::simulate(twice,tools,small,target,{});close(result.removed_mm3,.4);
        check(result.counts.stock_cuts==1,"Empty stock caused another cut");
        bool canceled{};
        try {static_cast<void>(native::simulate(cut,tools,stock,target,{},1e-7,[](auto,auto){throw std::runtime_error("cancel");}));}
        catch(const std::runtime_error& e){canceled=std::string(e.what())=="cancel";}
        check(canceled&&serialized(stock)==original_stock,"Cancellation modified the source stock");
        Machine machine;machine.id="mill";machine.name="Mill";
        Facing face{"face","Facing","T1",5,2,3,0};
        const auto facing=face_program(face,tools.front(),{{-5,-5,0},{15,5,10}},{{-5,-5,0},{15,5,3}});
        const auto facing_copy=face_program(face,tools.front(),{{-5,-5,0},{15,5,10}},{{-5,-5,0},{15,5,3}});
        check(facing==facing_copy,"Facing generation is nondeterministic");
        result=native::simulate(compile(facing,machine,tools),tools,stock,target,{});
        close(native::volume(result.remaining),600);close(result.removed_mm3,1400);
        check(result.collisions.empty(),"Generated face operation collided with stock or target");
        const auto higher_clearance=face_program(face,tools.front(),{{-5,-5,0},{15,5,10}},{{-5,-5,0},{15,5,3}},
            {{{80,80,0},{90,90,40}}});
        check(higher_clearance.initial_tip.z==43,"Facing clearance ignored fixture height");
        face.allowance_mm=2;
        result=native::simulate(compile(face_program(face,tools.front(),{{-5,-5,0},{15,5,10}},{{-5,-5,0},{15,5,3}}),machine,tools),
            tools,stock,target,{});
        close(native::volume(result.remaining),1000);check(result.collisions.empty(),"Facing allowance produced a collision");
        face.step_over_mm=2.01;
        bool rejected{};try {static_cast<void>(face_program(face,tools.front(),{{-5,-5,0},{15,5,10}},{{-5,-5,0},{15,5,3}}));}
        catch(const ProgramError& error){rejected=error.code==Error::InvalidOperation;}
        check(rejected,"Facing allowed an uncut gap between passes");
        auto shoulder_tool=tools.front();shoulder_tool.cutting.insert(shoulder_tool.cutting.begin(),{-5,4});
        face.step_over_mm=4;rejected=false;
        try {static_cast<void>(face_program(face,shoulder_tool,{{-5,-5,0},{15,5,10}},{{-5,-5,0},{15,5,3}}));}
        catch(const ProgramError& error){rejected=error.code==Error::InvalidOperation;}
        check(rejected,"A zero-thickness shoulder enlarged the effective facing tip");
        std::cout<<"CAM simulation: "<<checks<<" checks passed\n";return 0;
    }catch(const Standard_Failure& error){std::cerr<<"OCCT: "<<error.GetMessageString()<<'\n';return 1;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
