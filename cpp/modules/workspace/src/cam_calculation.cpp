#include <zima/workspace/cam_calculation.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <zima/kernel/stable_id.hpp>
#include "../../cam/src/native_simulation.hpp"
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <algorithm>
#include <map>

namespace zima::workspace {
namespace {
cam::Bounds bounds(const TopoDS_Shape& shape) {
    Bnd_Box box;BRepBndLib::AddOptimal(shape,box,false,false);
    if(box.IsVoid()||box.IsOpen())throw cam::ProgramError(cam::Error::InvalidOperation);
    cam::Bounds value;box.Get(value.minimum.x,value.minimum.y,value.minimum.z,
        value.maximum.x,value.maximum.y,value.maximum.z);return value;
}
cam::Program combined_program(const document::CamDefinition& definition,const cam::Bounds& stock,
        const cam::Bounds& target,const std::vector<cam::Bounds>& fixtures) {
    cam::Program result;result.display_units=definition.display_units;cam::Point tip;bool started{};
    const auto transfer=[&](const std::string& operation,const std::string& role,cam::Point end) {
        if(end==tip)return;
        cam::Block block;block.id=operation+":"+role;block.operation_id=operation;
        block.kind=cam::BlockKind::Motion;block.motion=cam::MotionKind::Rapid;block.end=end;
        result.blocks.push_back(std::move(block));tip=end;
    };
    for(const auto& operation:definition.operations) {
        cam::Program part;const auto* facing=std::get_if<cam::Facing>(&operation);
        if(facing) {
            const auto tool=std::ranges::find(definition.tools,facing->tool_id,&cam::Tool::id);
            if(tool==definition.tools.end())throw cam::ProgramError(cam::Error::UnknownTool,facing->id);
            part=cam::face_program(*facing,*tool,stock,target,fixtures);
        }else part=std::get<document::CamManualOperation>(operation).program;
        if(part.blocks.empty())continue;
        if(!started){result.initial_tip=tip=part.initial_tip;started=true;}
        else if(tip!=part.initial_tip) {
            if(!facing)throw cam::ProgramError(cam::Error::InvalidOperation,document::cam_operation_id(operation));
            // Preserve the previous tool during the explicit connection. The
            // complete transfer is simulated too; an obstacle cannot be skipped.
            const double clear=std::max(tip.z,part.initial_tip.z);
            transfer(facing->id,"setup-retract",{tip.x,tip.y,clear});
            transfer(facing->id,"setup-transfer",{part.initial_tip.x,part.initial_tip.y,clear});
            transfer(facing->id,"setup-approach",part.initial_tip);
        }
        for(const auto& block:part.blocks){result.blocks.push_back(block);if(block.kind==cam::BlockKind::Motion)tip=block.end;}
    }
    return result;
}
}
document::CamCalculated calculate_cam(const CamCalculationInput& input,
        const std::function<void(std::size_t,std::size_t)>& progress) {
    const auto& d=input.definition;document::validate_cam_definition(d);
    // Referenced Sketches already participate in persistence and invalidation,
    // but their oriented exclusion/grip regions are not implemented yet. Never
    // publish a seemingly checked setup by silently ignoring authored clamping.
    if(!d.clamping_sketch_ids.empty())
        throw std::invalid_argument("CAM clamping Sketch checks are not available yet. Use fixture Bodies for collision checking.");
    if(!d.work_frame.reference_valid||document::cam_intent_key(d)!=input.intent_key)
        throw cam::ProgramError(cam::Error::InvalidOperation);
    const kernel::KernelRigidPlacement frame{{d.work_frame.x,d.work_frame.y,d.work_frame.z},
        {d.work_frame.rotation_x,d.work_frame.rotation_y,d.work_frame.rotation_z}};
    std::map<std::string,TopoDS_Shape> shapes;
    for(const auto& [id,source]:input.material)
        if(!shapes.emplace(id,kernel::read_cam_work_shape(source,frame)).second)throw cam::ProgramError(cam::Error::DuplicateIdentity);
    const auto source=[&](const std::string& id)->const TopoDS_Shape& {
        const auto found=shapes.find(id);if(found==shapes.end())throw cam::ProgramError(cam::Error::InvalidOperation);return found->second;
    };
    const auto& stock=source(d.stock_id);const auto& target=source(d.target_id);
    std::vector<cam::native::Obstacle> obstacles;std::vector<cam::Bounds> obstacle_bounds;
    for(const auto& id:d.fixture_ids){obstacles.push_back({id,source(id)});obstacle_bounds.push_back(bounds(obstacles.back().shape));}
    auto program=combined_program(d,bounds(stock),bounds(target),obstacle_bounds);
    const auto compiled=cam::compile(program,d.machine,d.tools,d.machine_work_origin);
    auto simulation=cam::native::simulate(compiled,d.tools,stock,target,obstacles,d.tolerance_mm,progress);
    document::CamCalculated result;result.id=kernel::make_stable_id();result.intent_key=input.intent_key;result.sources=input.source_keys;
    result.program=std::move(program);result.machine=d.machine;result.machine_work_origin=d.machine_work_origin;result.tools=d.tools;
    result.work_frame=d.work_frame;result.tolerance_mm=d.tolerance_mm;
    result.remaining=kernel::make_cam_remaining_result(simulation.remaining,frame,input.intent_key);
    result.collisions=std::move(simulation.collisions);result.removed_mm3=simulation.removed_mm3;result.missing_target_mm3=simulation.missing_target_mm3;
    document::validate_cam_calculated(result);return result;
}
} // namespace zima::workspace
