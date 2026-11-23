#include <zima/cam/facing.hpp>
#include <algorithm>
#include <cmath>

namespace zima::cam {
namespace {
void bounds(const Bounds& b) {
    if(!std::isfinite(b.minimum.x)||!std::isfinite(b.minimum.y)||!std::isfinite(b.minimum.z)||
       !std::isfinite(b.maximum.x)||!std::isfinite(b.maximum.y)||!std::isfinite(b.maximum.z)||
       b.minimum.x>=b.maximum.x||b.minimum.y>=b.maximum.y||b.minimum.z>=b.maximum.z)
        throw ProgramError(Error::InvalidOperation);
}
std::size_t intervals(double extent,double step) {
    const double n=std::ceil(extent/step);
    // Bound an authored operation's memory before allocating its NC blocks.
    if(!std::isfinite(n)||n>1000000)throw ProgramError(Error::InvalidOperation);
    return static_cast<std::size_t>(std::max(0.,n));
}
}
Program face_program(const Facing& operation,const Tool& tool,const Bounds& stock,
        const Bounds& target,const std::vector<Bounds>& fixtures) {
    validate(tool);bounds(stock);bounds(target);
    // Equal-height stations describe shoulders, not an extra zero-thickness
    // cutting disk. Use the first actual axial segment at the tool tip.
    std::size_t tip_station{};
    while(tip_station+1<tool.cutting.size()&&tool.cutting[tip_station+1].z_mm==tool.cutting.front().z_mm)++tip_station;
    const double radius=tool.cutting[tip_station].radius_mm,length=-tool.cutting.front().z_mm;
    if(operation.id.empty()||operation.name.empty()||operation.tool_id!=tool.id||radius<=0||
       !std::isfinite(operation.step_down_mm)||operation.step_down_mm<=0||operation.step_down_mm>length||
       !std::isfinite(operation.step_over_mm)||operation.step_over_mm<=0||operation.step_over_mm>2*radius||
       !std::isfinite(operation.clearance_mm)||operation.clearance_mm<=0||
       !std::isfinite(operation.allowance_mm)||operation.allowance_mm<0)
        throw ProgramError(Error::InvalidOperation,operation.id);
    const double finish=target.maximum.z+operation.allowance_mm;
    double clear=std::max(stock.maximum.z,target.maximum.z);
    for(const auto& fixture:fixtures){bounds(fixture);clear=std::max(clear,fixture.maximum.z);}
    clear+=operation.clearance_mm;
    double maximum_radius{};
    for(const auto& s:tool.cutting)maximum_radius=std::max(maximum_radius,s.radius_mm);
    for(const auto& s:tool.non_cutting)maximum_radius=std::max(maximum_radius,s.radius_mm);
    const double left=stock.minimum.x-maximum_radius-operation.clearance_mm;
    const double right=stock.maximum.x+maximum_radius+operation.clearance_mm;
    const double width=stock.maximum.y-stock.minimum.y;
    const double low_y=width<=2*radius?(stock.minimum.y+stock.maximum.y)/2:stock.minimum.y+radius;
    const double high_y=width<=2*radius?low_y:stock.maximum.y-radius;
    const auto rows=intervals(high_y-low_y,operation.step_over_mm)+1;
    const auto layers=intervals(stock.maximum.z-finish,operation.step_down_mm);
    if(!std::isfinite(clear)||!std::isfinite(left)||!std::isfinite(right)||!std::isfinite(finish)||
       layers>1000000/(2*rows+3))throw ProgramError(Error::InvalidOperation,operation.id);
    Program result;result.initial_tip={left,low_y,clear};
    if(layers==0)return result;
    const auto command=[&](std::string role,BlockKind kind,double value) {
        Block block;block.id=operation.id+":"+role;block.operation_id=operation.id;block.kind=kind;
        block.tool_id=tool.id;block.value=value;block.clockwise=tool.clockwise;result.blocks.push_back(std::move(block));
    };
    command("tool",BlockKind::Tool,0);command("spindle",BlockKind::Spindle,tool.spindle_rpm);
    command("feed",BlockKind::Feed,tool.feed_mm_min);
    const auto move=[&](std::string role,MotionKind kind,Point point) {
        Block block;block.id=operation.id+":"+role;block.operation_id=operation.id;
        block.kind=BlockKind::Motion;block.motion=kind;block.end=point;result.blocks.push_back(std::move(block));
    };
    for(std::size_t layer=0;layer<layers;++layer) {
        const std::string prefix="layer:"+std::to_string(layer)+":";
        const double z=layer+1==layers?finish:stock.maximum.z-(layer+1)*operation.step_down_mm;
        if(layer)move(prefix+"approach",MotionKind::Rapid,{left,low_y,clear});
        move(prefix+"lower",MotionKind::Rapid,{left,low_y,z});
        Point end;
        for(std::size_t row=0;row<rows;++row) {
            const double y=rows==1?low_y:low_y+(high_y-low_y)*row/(rows-1);
            const bool forward=row%2==0;
            if(row)move(prefix+"row:"+std::to_string(row)+":transfer",MotionKind::Linear,{forward?left:right,y,z});
            end={forward?right:left,y,z};
            move(prefix+"row:"+std::to_string(row)+":cut",MotionKind::Linear,end);
        }
        end.z=clear;move(prefix+"retract",MotionKind::Rapid,end);
    }
    return result;
}
} // namespace zima::cam
