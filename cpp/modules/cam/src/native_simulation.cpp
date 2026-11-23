#include "native_simulation.hpp"
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <Bnd_Box.hxx>
#include <NCollection_List.hxx>
#include <TopExp_Explorer.hxx>
#include <cmath>
#include <map>
#include <set>

namespace zima::cam::native {
namespace {
struct BoundedShape {TopoDS_Shape shape;Bnd_Box bounds;};
BoundedShape bounded(const TopoDS_Shape& shape,double tolerance) {
    BoundedShape result{shape};
    BRepBndLib::AddOptimal(shape,result.bounds,false,false);result.bounds.Enlarge(tolerance);
    return result;
}
void require_solid(const TopoDS_Shape& shape) {
    if(shape.IsNull()||!TopExp_Explorer(shape,TopAbs_SOLID).More()||!BRepCheck_Analyzer(shape).IsValid())
        throw std::invalid_argument("CAM input is not valid solid geometry.");
}
template<class Operation>
TopoDS_Shape boolean(const TopoDS_Shape& a,const TopoDS_Shape& b,double tolerance) {
    Operation operation;NCollection_List<TopoDS_Shape> arguments,tools;
    arguments.Append(a);tools.Append(b);operation.SetArguments(arguments);operation.SetTools(tools);
    operation.SetNonDestructive(true);operation.SetFuzzyValue(tolerance);operation.Build();
    if(!operation.IsDone()||operation.Shape().IsNull()||!BRepCheck_Analyzer(operation.Shape()).IsValid())
        throw std::runtime_error("CAM material calculation produced invalid geometry.");
    return operation.Shape();
}
}
SimulationResult simulate(const CompiledProgram& program,const std::vector<Tool>& tools,
        const TopoDS_Shape& stock,const TopoDS_Shape& target,const std::vector<Obstacle>& fixtures,
        double tolerance,const std::function<void(std::size_t,std::size_t)>& progress) {
    if(!std::isfinite(tolerance)||tolerance<=0)throw std::invalid_argument("Invalid CAM simulation tolerance.");
    // Even validation/bounds helpers may update OCCT's lazy shape flags.
    // Own topology and geometry before any native inspection or Boolean work.
    const auto private_stock=BRepBuilderAPI_Copy(stock,true,false).Shape();
    const auto private_target=BRepBuilderAPI_Copy(target,true,false).Shape();
    require_solid(private_stock);require_solid(private_target);
    std::map<std::string,const Tool*> by_id;
    for(const auto& tool:tools) {
        validate(tool);
        if(!by_id.emplace(tool.id,&tool).second)throw ProgramError(Error::DuplicateIdentity);
    }
    std::set<std::string> fixture_ids;std::vector<BoundedShape> obstacles;
    for(const auto& fixture:fixtures) {
        if(fixture.id.empty()||!fixture_ids.insert(fixture.id).second)throw ProgramError(Error::DuplicateIdentity);
        const auto shape=BRepBuilderAPI_Copy(fixture.shape,true,false).Shape();
        require_solid(shape);obstacles.push_back(bounded(shape,tolerance));
    }
    SimulationResult result;result.remaining=private_stock;
    auto remaining=bounded(private_stock,tolerance);const auto design=bounded(private_target,tolerance);
    const double initial_volume=volume(private_stock),minimum_volume=tolerance*tolerance*tolerance;
    result.missing_target_mm3=volume(boolean<BRepAlgoAPI_Cut>(private_target,private_stock,tolerance));
    const auto common_volume=[&](const BoundedShape& a,const BoundedShape& b) {
        if(a.bounds.IsOut(b.bounds)){++result.counts.bounds_rejections;return 0.;}
        ++result.counts.intersections;
        return volume(boolean<BRepAlgoAPI_Common>(a.shape,b.shape,tolerance));
    };
    for(std::size_t index=0;index<program.motions.size();++index) {
        if(progress)progress(index,program.motions.size());
        const auto& motion=program.motions[index];
        const auto found=by_id.find(motion.tool_id);
        if(found==by_id.end())throw ProgramError(Error::UnknownTool,motion.block_id);
        const auto cutter=bounded(swept_tool(*found->second,motion,true,tolerance),tolerance);
        const auto shank=bounded(swept_tool(*found->second,motion,false,tolerance),tolerance);
        result.counts.swept_volumes+=2;
        const auto collision=[&](CollisionKind kind,const std::string& obstacle,const BoundedShape& a,const BoundedShape& b) {
            const double intersection=common_volume(a,b);
            if(intersection>minimum_volume)result.collisions.push_back({kind,motion.block_id,motion.tool_id,obstacle,intersection});
            return intersection;
        };
        collision(CollisionKind::NonCuttingStock,{},shank,remaining);
        for(std::size_t f=0;f<fixtures.size();++f) {
            collision(CollisionKind::CuttingFixture,fixtures[f].id,cutter,obstacles[f]);
            collision(CollisionKind::NonCuttingFixture,fixtures[f].id,shank,obstacles[f]);
        }
        if(motion.kind==MotionKind::Rapid) {
            collision(CollisionKind::RapidStock,{},cutter,remaining);
            continue;
        }
        collision(CollisionKind::TargetGouge,{},cutter,design);
        if(common_volume(cutter,remaining)>minimum_volume) {
            result.remaining=boolean<BRepAlgoAPI_Cut>(remaining.shape,cutter.shape,tolerance);
            ++result.counts.stock_cuts;remaining=bounded(result.remaining,tolerance);
        }
    }
    if(progress)progress(program.motions.size(),program.motions.size());
    result.removed_mm3=initial_volume-volume(result.remaining);
    return result;
}
} // namespace zima::cam::native
