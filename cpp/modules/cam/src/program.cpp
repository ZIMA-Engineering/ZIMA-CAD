#include <zima/cam/program.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <numbers>
#include <set>
#include <sstream>

namespace zima::cam {
namespace {
constexpr double tau=2*std::numbers::pi;
bool finite(Point p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
bool positive(double v) { return std::isfinite(v)&&v>0; }
double distance(Point a,Point b) { return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z); }
bool arc(MotionKind kind) { return kind==MotionKind::ArcClockwise||kind==MotionKind::ArcCounterclockwise; }
double positive_angle(double a) { a=std::fmod(a,tau);return a<0?a+tau:a; }
std::string number(double n) {
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17)<<n;return out.str();
}
std::string coordinates(Point p,double scale) {
    return " X"+number(p.x/scale)+" Y"+number(p.y/scale)+" Z"+number(p.z/scale);
}
void profile(const std::vector<ToolStation>& stations,bool cutting) {
    if(stations.size()<2)throw ProgramError(Error::InvalidTool);
    bool volume{};
    for(std::size_t i=0;i<stations.size();++i) {
        const auto& s=stations[i];
        if(!std::isfinite(s.z_mm)||!std::isfinite(s.radius_mm)||s.radius_mm<0||
            (cutting?s.z_mm>0:s.z_mm<0))throw ProgramError(Error::InvalidTool);
        if(i) {
            const auto& previous=stations[i-1];
            if(s.z_mm<previous.z_mm||s==previous)throw ProgramError(Error::InvalidTool);
            volume|=s.z_mm>previous.z_mm&&(s.radius_mm>0||previous.radius_mm>0);
        }
    }
    if(!volume||(cutting?stations.back().z_mm!=0:stations.front().z_mm!=0))
        throw ProgramError(Error::InvalidTool);
}
}

ProgramError::ProgramError(Error value,std::string block)
    :std::runtime_error("Invalid generic CAM program."),code(value),block_id(std::move(block)) {}

void validate(const Machine& m) {
    if(m.id.empty()||m.name.empty()||!finite(m.minimum)||!finite(m.maximum)||
        !(m.minimum.x<m.maximum.x&&m.minimum.y<m.maximum.y&&m.minimum.z<m.maximum.z)||
        !positive(m.maximum_feed_mm_min)||!positive(m.maximum_spindle_rpm)||!positive(m.rapid_mm_min)||
        (m.kind!=MachineKind::Milling&&m.kind!=MachineKind::Turning)||m.axes<2||m.axes>5)
        throw ProgramError(Error::InvalidMachine);
}
void validate(const Tool& tool) {
    if(tool.id.empty()||tool.name.empty()||!positive(tool.feed_mm_min)||!positive(tool.spindle_rpm))
        throw ProgramError(Error::InvalidTool);
    profile(tool.cutting,true);profile(tool.non_cutting,false);
    if(tool.cutting.back().radius_mm<=0||tool.non_cutting.front().radius_mm<=0)
        throw ProgramError(Error::InvalidTool);
}
double tip_offset_mm(const Tool& tool) { validate(tool);return tool.cutting.front().z_mm; }
Point tool_origin(const Tool& tool,Point tip) { tip.z-=tip_offset_mm(tool);return tip; }
Point tool_mount(const Tool& tool,Point tip) {
    auto point=tool_origin(tool,tip);point.z+=tool.non_cutting.back().z_mm;return point;
}

CompiledProgram compile(const Program& program,const Machine& machine,const std::vector<Tool>& tools,
        Point machine_work_origin) {
    validate(machine);
    if(machine.kind!=MachineKind::Milling||machine.axes!=3)throw ProgramError(Error::UnsupportedMachine);
    if(!finite(program.initial_tip)||!finite(machine_work_origin)||
            (program.display_units!=Units::Millimetres&&program.display_units!=Units::Inches))
        throw ProgramError(Error::InvalidProgram);
    std::map<std::string,const Tool*> by_id;
    for(const auto& tool:tools) {
        validate(tool);
        if(!by_id.emplace(tool.id,&tool).second)throw ProgramError(Error::DuplicateIdentity);
    }
    const double scale=program.display_units==Units::Inches?25.4:1;
    CompiledProgram result;
    const auto line=[&](std::string text,const std::string& id={}) {
        result.lines.push_back(std::move(text));result.line_block_ids.push_back(id);
    };
    line("; ZIMA-CAD GENERIC NC - controller postprocessing required");
    line(program.display_units==Units::Inches?"UNITS IN":"UNITS MM");line("ABSOLUTE");
    line("INITIAL"+coordinates(program.initial_tip,scale));
    std::set<std::string> ids;Point position=program.initial_tip;std::string tool_id;
    double feed{},spindle{};bool clockwise{true},stopped{};
    const auto check_travel=[&](Point tip,const std::string& id) {
        const auto& tool=*by_id.at(tool_id);
        auto mount=tip;
        mount.z+=tool.non_cutting.back().z_mm-tool.cutting.front().z_mm;
        mount.x+=machine_work_origin.x;mount.y+=machine_work_origin.y;mount.z+=machine_work_origin.z;
        if(!finite(mount)||mount.x<machine.minimum.x||mount.x>machine.maximum.x||
            mount.y<machine.minimum.y||mount.y>machine.maximum.y||
            mount.z<machine.minimum.z||mount.z>machine.maximum.z)
            throw ProgramError(Error::TravelExceeded,id);
    };
    for(const auto& block:program.blocks) {
        const auto fail=[&](Error error)->void {throw ProgramError(error,block.id);};
        if(block.id.empty()||block.operation_id.empty()||!ids.insert(block.id).second)fail(Error::DuplicateIdentity);
        if(stopped)fail(Error::MotionAfterStop);
        switch(block.kind) {
        case BlockKind::Tool:
            if(!by_id.contains(block.tool_id))fail(Error::UnknownTool);
            tool_id=block.tool_id;feed=spindle=0;
            check_travel(position,block.id);
            // Toolchange implementation and its machine-specific retracts are
            // a postprocessor responsibility. The generic TCP stays explicit.
            line("TOOL "+tool_id,block.id);break;
        case BlockKind::Spindle:
            if(tool_id.empty())fail(Error::MissingTool);
            if(!std::isfinite(block.value)||block.value<0||block.value>machine.maximum_spindle_rpm)fail(Error::InvalidSpindle);
            spindle=block.value;clockwise=block.clockwise;
            line(std::string("SPINDLE ")+(clockwise?"CW ":"CCW ")+number(spindle),block.id);break;
        case BlockKind::Feed:
            if(!positive(block.value)||block.value>machine.maximum_feed_mm_min)fail(Error::InvalidFeed);
            feed=block.value;line("FEED "+number(feed/scale),block.id);break;
        case BlockKind::Stop:line("STOP",block.id);stopped=true;break;
        case BlockKind::Motion: {
            if(!finite(block.end)||!finite(block.center))fail(Error::InvalidProgram);
            if(tool_id.empty())fail(Error::MissingTool);
            if(block.motion!=MotionKind::Rapid&&block.motion!=MotionKind::Linear&&!arc(block.motion))fail(Error::InvalidProgram);
            if(block.motion!=MotionKind::Rapid) {
                if(!positive(feed))fail(Error::InvalidFeed);
                if(!positive(spindle))fail(Error::InvalidSpindle);
            }
            Motion motion{block.id,block.operation_id,tool_id,block.motion,position,block.end,block.center,0,
                block.motion==MotionKind::Rapid?machine.rapid_mm_min:feed,spindle,clockwise};
            const char* keyword=block.motion==MotionKind::Rapid?"RAPID":"LINEAR";
            if(arc(block.motion)) {
                const double r=std::hypot(position.x-block.center.x,position.y-block.center.y);
                const double end_r=std::hypot(block.end.x-block.center.x,block.end.y-block.center.y);
                if(!positive(r)||!std::isfinite(end_r)||std::abs(r-end_r)>1e-8*std::max(1.0,r)||
                    position.z!=block.end.z||position.z!=block.center.z)fail(Error::InvalidArc);
                const double a=std::atan2(position.y-block.center.y,position.x-block.center.x);
                const double b=std::atan2(block.end.y-block.center.y,block.end.x-block.center.x);
                double sweep=positive_angle(block.motion==MotionKind::ArcClockwise?a-b:b-a);
                if(sweep==0)sweep=tau;
                motion.sweep_radians=block.motion==MotionKind::ArcClockwise?-sweep:sweep;
                keyword=block.motion==MotionKind::ArcClockwise?"ARC CW":"ARC CCW";
            }
            const auto [minimum,maximum]=motion_bounds(motion);
            check_travel(minimum,block.id);check_travel(maximum,block.id);
            std::string text=std::string(keyword)+coordinates(block.end,scale);
            if(arc(block.motion))text+=" CENTER"+coordinates(block.center,scale);
            line(std::move(text),block.id);result.motions.push_back(std::move(motion));position=block.end;break;
        }
        default:fail(Error::InvalidProgram);
        }
    }
    return result;
}

Point point_at(const Motion& motion,double fraction) {
    if(!std::isfinite(fraction)||fraction<0||fraction>1)throw ProgramError(Error::InvalidProgram,motion.block_id);
    if(fraction==0)return motion.start;if(fraction==1)return motion.end;
    if(!arc(motion.kind))return {motion.start.x+(motion.end.x-motion.start.x)*fraction,
        motion.start.y+(motion.end.y-motion.start.y)*fraction,motion.start.z+(motion.end.z-motion.start.z)*fraction};
    const double angle=std::atan2(motion.start.y-motion.center.y,motion.start.x-motion.center.x)+motion.sweep_radians*fraction;
    const double radius=std::hypot(motion.start.x-motion.center.x,motion.start.y-motion.center.y);
    return {motion.center.x+radius*std::cos(angle),motion.center.y+radius*std::sin(angle),motion.start.z};
}
double length_mm(const Motion& motion) {
    return arc(motion.kind)?std::hypot(motion.start.x-motion.center.x,motion.start.y-motion.center.y)*std::abs(motion.sweep_radians):distance(motion.start,motion.end);
}
std::pair<Point,Point> motion_bounds(const Motion& motion) {
    Point low=motion.start,high=motion.start;
    const auto add=[&](Point p){low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);
        high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);};
    add(motion.end);
    if(arc(motion.kind)) {
        const double initial=std::atan2(motion.start.y-motion.center.y,motion.start.x-motion.center.x);
        for(unsigned i=0;i<4;++i) {
            const double target=i*std::numbers::pi/2;
            const double angle=positive_angle(motion.sweep_radians<0?initial-target:target-initial);
            if(angle<=std::abs(motion.sweep_radians))add(point_at(motion,angle/std::abs(motion.sweep_radians)));
        }
    }
    return {low,high};
}
} // namespace zima::cam
