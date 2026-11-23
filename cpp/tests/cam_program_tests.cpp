#include <zima/cam/program.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
using namespace zima::cam;
unsigned checks{};
void check(bool condition,const char* message) {
    ++checks;if(!condition)throw std::runtime_error(message);
}
void near(double actual,double expected,const char* message) {
    check(std::isfinite(actual)&&std::abs(actual-expected)<1e-9,message);
}
template<class F> void rejects(Error expected,F action,const std::string& block={}) {
    try {action();} catch(const ProgramError& error) {
        check(error.code==expected,"Unexpected program diagnostic");
        if(!block.empty())check(error.block_id==block,"Diagnostic lost its block identity");
        return;
    }
    throw std::runtime_error("Invalid CAM input was accepted");
}
Tool cutter() {return {"T1","Authored cutter",{{-20,3},{0,3}},{{0,4},{40,4}},300,6000,true};}
Machine machine() {Machine m;m.id="M1";m.name="Three-axis mill";return m;}
Block block(BlockKind kind,const std::string& id) {Block b;b.id=id;b.operation_id="OP1";b.kind=kind;return b;}
Program program(Point initial={}) {
    Program p;p.initial_tip=initial;
    auto tool=block(BlockKind::Tool,"tool");tool.tool_id="T1";
    auto spindle=block(BlockKind::Spindle,"spindle");spindle.value=6000;
    auto feed=block(BlockKind::Feed,"feed");feed.value=300;
    p.blocks={tool,spindle,feed};return p;
}
void move(Program& p,MotionKind kind,Point end,Point center={}) {
    auto b=block(BlockKind::Motion,"move"+std::to_string(p.blocks.size()));
    b.motion=kind;b.end=end;b.center=center;p.blocks.push_back(b);
}
}
int main() {
    try {
        const auto m=machine();const auto tool=cutter();
        near(tip_offset_mm(tool),-20,"Cutting boundary redefined the tool tip");
        check(tool_origin(tool,{1,2,3})==Point{1,2,23},"Authored tool origin moved to its tip");
        check(tool_mount(tool,{1,2,3})==Point{1,2,63},"Mounting offset ignored the complete non-cutting assembly");
        auto p=program();move(p,MotionKind::Linear,{3,4,12});move(p,MotionKind::Rapid,{6,8,24});
        p.blocks.push_back(block(BlockKind::Stop,"stop"));
        const auto source=p;const auto compiled=compile(p,m,{tool});
        check(p==source,"Compilation changed the authored program");
        check(compiled.motions.size()==2&&compiled.lines.size()==compiled.line_block_ids.size(),"NC/motion map is incomplete");
        check(compiled.lines.front().find("GENERIC NC")!=std::string::npos,"Generic program is not identified");
        for(const auto& b:p.blocks)
            check(std::ranges::count(compiled.line_block_ids,b.id)==1,"A program block lost its NC line");
        const auto& linear=compiled.motions[0];
        check(linear.block_id==p.blocks[3].id&&linear.operation_id=="OP1"&&linear.tool_id=="T1","Motion lost authored identities");
        near(length_mm(linear),13,"Three-axis linear length is incorrect");
        check(point_at(linear,.5)==Point{1.5,2,6},"Three-axis interpolation failed");
        check(point_at(linear,0)==Point{}&&point_at(linear,1)==Point{3,4,12},"Motion endpoints changed");
        near(linear.feed_mm_min,300,"Cutting feed changed");
        near(compiled.motions[1].feed_mm_min,m.rapid_mm_min,"Rapid used the cutting feed");
        const auto [lo,hi]=motion_bounds(linear);
        check(lo==Point{}&&hi==Point{3,4,12},"Linear bounds are incorrect");
        for(const auto direction:{MotionKind::ArcClockwise,MotionKind::ArcCounterclockwise}) {
            const bool clockwise=direction==MotionKind::ArcClockwise;
            auto arc=program({10,0,0});move(arc,direction,{0,clockwise?-10.:10.,0});
            const auto motion=compile(arc,m,{tool}).motions.front();
            near(motion.sweep_radians,(clockwise?-1:1)*std::numbers::pi/2,"Quarter-circle direction failed");
            near(length_mm(motion),5*std::numbers::pi,"Quarter-circle length failed");
            const auto middle=point_at(motion,.5);
            near(middle.x,std::sqrt(50.),"Arc midpoint X failed");
            near(middle.y,(clockwise?-1:1)*std::sqrt(50.),"Arc midpoint Y failed");
            arc.blocks.back().end={10,0,0};
            const auto circle=compile(arc,m,{tool}).motions.front();
            near(length_mm(circle),20*std::numbers::pi,"Closed circle was treated as zero motion");
            const auto [a,b]=motion_bounds(circle);
            near(a.x,-10,"Full-circle minimum X omitted");near(a.y,-10,"Full-circle minimum Y omitted");
            near(b.x,10,"Full-circle maximum X omitted");near(b.y,10,"Full-circle maximum Y omitted");
        }
        auto inches=program({25.4,0,0});inches.display_units=Units::Inches;
        inches.blocks[2].value=254;move(inches,MotionKind::Linear,{50.8,0,0});
        const auto inch_result=compile(inches,m,{tool});
        check(inch_result.lines[1]=="UNITS IN"&&inch_result.lines[3]=="INITIAL X1 Y0 Z0", "Inch initial coordinates failed");
        check(std::ranges::find(inch_result.lines,"FEED 10")!=inch_result.lines.end(),"Inch feed is not converted");
        check(inch_result.motions.front().end.x==50.8,"Display units changed physical motion");
        auto long_arc=program({10,0,0});move(long_arc,MotionKind::ArcCounterclockwise,{0,-10,0});
        auto limited=m;limited.minimum.x=-9;
        rejects(Error::TravelExceeded,[&]{return compile(long_arc,limited,{tool});},long_arc.blocks.back().id);
        auto z=program();move(z,MotionKind::Rapid,{0,0,440});
        check(compile(z,m,{tool}).motions.size()==1,"Exact machine travel boundary rejected");
        rejects(Error::TravelExceeded,[&]{return compile(z,m,{tool},{0,0,1});},z.blocks.back().id);
        limited.maximum.z=59;
        rejects(Error::TravelExceeded,[&]{return compile(program(),limited,{tool});},"tool");
        auto bad=program();bad.blocks[1].value=0;move(bad,MotionKind::Linear,{1,0,0});
        rejects(Error::InvalidSpindle,[&]{return compile(bad,m,{tool});},bad.blocks.back().id);
        bad.blocks.back().motion=MotionKind::Rapid;
        check(compile(bad,m,{tool}).motions.size()==1,"Rapid incorrectly required a running spindle");
        bad=program();bad.blocks[2].value=m.maximum_feed_mm_min+1;
        rejects(Error::InvalidFeed,[&]{return compile(bad,m,{tool});},"feed");
        bad=program();bad.blocks[1].value=m.maximum_spindle_rpm+1;
        rejects(Error::InvalidSpindle,[&]{return compile(bad,m,{tool});},"spindle");
        bad=program();bad.blocks.front().tool_id="missing";
        rejects(Error::UnknownTool,[&]{return compile(bad,m,{tool});},"tool");
        bad=program();bad.blocks.push_back(bad.blocks.front());
        rejects(Error::DuplicateIdentity,[&]{return compile(bad,m,{tool});},"tool");
        rejects(Error::DuplicateIdentity,[&]{return compile(program(),m,{tool,tool});});
        bad=p;move(bad,MotionKind::Rapid,{0,0,0});
        rejects(Error::MotionAfterStop,[&]{return compile(bad,m,{tool});},bad.blocks.back().id);
        for(const auto axes:{2u,4u,5u}) {auto unsupported=m;unsupported.axes=axes;
            rejects(Error::UnsupportedMachine,[&]{return compile(program(),unsupported,{tool});});}
        auto turning=m;turning.kind=MachineKind::Turning;
        rejects(Error::UnsupportedMachine,[&]{return compile(program(),turning,{tool});});
        auto arc=program({10,0,0});move(arc,MotionKind::ArcClockwise,{0,11,0});
        rejects(Error::InvalidArc,[&]{return compile(arc,m,{tool});},arc.blocks.back().id);
        arc.blocks.back().end={0,10,1};
        rejects(Error::InvalidArc,[&]{return compile(arc,m,{tool});});
        const auto nan=std::numeric_limits<double>::quiet_NaN();
        bad=program();bad.initial_tip.x=nan;
        rejects(Error::InvalidProgram,[&]{return compile(bad,m,{tool});});
        rejects(Error::InvalidProgram,[&]{return compile(program(),m,{tool},{nan,0,0});});
        rejects(Error::InvalidProgram,[&]{return point_at(linear,nan);});
        rejects(Error::InvalidProgram,[&]{return point_at(linear,-.1);});
        rejects(Error::InvalidProgram,[&]{return point_at(linear,1.1);});
        auto malformed=tool;malformed.cutting.back().z_mm=1;
        rejects(Error::InvalidTool,[&]{validate(malformed);});
        malformed=tool;malformed.non_cutting.front().z_mm=-1;
        rejects(Error::InvalidTool,[&]{validate(malformed);});
        malformed=tool;malformed.cutting[0].radius_mm=-1;
        rejects(Error::InvalidTool,[&]{validate(malformed);});
        malformed=tool;malformed.cutting={{-20,0},{-15,3},{-15,4},{0,4}};
        validate(malformed);near(tip_offset_mm(malformed),-20,"Conical point/shoulder tool was redefined");
        std::cout<<"CAM generic program: "<<checks<<" checks passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
