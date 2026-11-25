#include "native_simulation.hpp"
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
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
double area(const TopoDS_Shape& shape) {
    GProp_GProps properties;BRepGProp::SurfaceProperties(shape,properties);return properties.Mass();
}
gp_Pnt point(Point p){return {p.x,p.y,p.z};}
gp_Dir direction(Point p) {
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||
       p.x*p.x+p.y*p.y+p.z*p.z<1e-20)throw std::invalid_argument("Invalid CAM clamping direction.");
    return {p.x,p.y,p.z};
}
struct PreparedClamping {BoundedShape contact,exclusion;TopoDS_Shape below;};
PreparedClamping prepare_clamping(const Clamping& c,const Bnd_Box& envelope,double tolerance) {
    if(c.contact.IsNull())throw std::invalid_argument("CAM clamping requires a planar contact region.");
    const auto contact=BRepBuilderAPI_Copy(c.contact,true,false).Shape();
    const auto normal=direction(c.outward_normal);const gp_Pln plane(point(c.contact_origin),normal);
    bool faces{};
    for(TopExp_Explorer it(contact,TopAbs_FACE);it.More();it.Next()) {
        BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));
        if(surface.GetType()!=GeomAbs_Plane||!surface.Plane().Axis().Direction().IsParallel(normal,1e-10)||
            plane.Distance(surface.Plane().Location())>tolerance)
            throw std::invalid_argument("CAM clamping requires a planar contact region.");
        faces=true;
    }
    if(!faces||!BRepCheck_Analyzer(contact).IsValid()||area(contact)<=tolerance*tolerance)
        throw std::invalid_argument("CAM clamping requires a planar contact region.");
    // Bound the outward prism beyond the complete program/tool envelope. This
    // retains the half-infinite exclusion meaning without an arbitrary world
    // distance or a second series of sampled tool sweeps.
    double x0,y0,z0,x1,y1,z1;envelope.Get(x0,y0,z0,x1,y1,z1);double extent=tolerance*10;
    for(double x:{x0,x1})for(double y:{y0,y1})for(double z:{z0,z1})
        extent=std::max(extent,gp_Vec(point(c.contact_origin),gp_Pnt(x,y,z)).Dot(gp_Vec(normal))+tolerance*10);
    const auto exclusion=BRepPrimAPI_MakePrism(contact,gp_Vec(normal)*extent,true).Shape();require_solid(exclusion);
    const auto up=direction(c.upper_normal);const auto upper=BRepBuilderAPI_MakeFace(gp_Pln(point(c.upper_origin),up)).Face();
    const auto below=BRepPrimAPI_MakeHalfSpace(upper,point(c.upper_origin).Translated(-gp_Vec(up))).Solid();
    return {bounded(contact,tolerance),bounded(exclusion,tolerance),below};
}
}
SimulationResult simulate(const CompiledProgram& program,const std::vector<Tool>& tools,
        const TopoDS_Shape& stock,const TopoDS_Shape& target,const std::vector<Obstacle>& fixtures,
        double tolerance,const std::function<void(std::size_t,std::size_t)>& progress,const std::vector<Clamping>& clampings) {
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
    std::vector<PreparedClamping> prepared;
    if(!clampings.empty()) {
        Bnd_Box envelope=remaining.bounds;
        for(const auto& motion:program.motions) {
            const auto found=by_id.find(motion.tool_id);if(found==by_id.end())throw ProgramError(Error::UnknownTool,motion.block_id);
            const auto& tool=*found->second;auto [low,high]=motion_bounds(motion);double radius{};
            for(const auto* profile:{&tool.cutting,&tool.non_cutting})for(const auto& station:*profile)radius=std::max(radius,station.radius_mm);
            envelope.Add(gp_Pnt(low.x-radius,low.y-radius,low.z));
            envelope.Add(gp_Pnt(high.x+radius,high.y+radius,tool_mount(tool,high).z));
        }
        std::set<std::string> ids;
        for(const auto& c:clampings) {
            if(c.id.empty()||!ids.insert(c.id).second||fixture_ids.contains(c.id))throw ProgramError(Error::DuplicateIdentity);
            prepared.push_back(prepare_clamping(c,envelope,tolerance));
            const auto initial=area(boolean<BRepAlgoAPI_Common>(private_stock,prepared.back().contact.shape,tolerance));
            result.contacts.push_back({c.id,initial,initial});
        }
    }
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
        for(std::size_t c=0;c<prepared.size();++c) {
            collision(CollisionKind::CuttingClamping,clampings[c].id,cutter,prepared[c].exclusion);
            collision(CollisionKind::NonCuttingClamping,clampings[c].id,shank,prepared[c].exclusion);
        }
        if(motion.kind==MotionKind::Rapid) {
            collision(CollisionKind::RapidStock,{},cutter,remaining);
            continue;
        }
        collision(CollisionKind::TargetGouge,{},cutter,design);
        TopoDS_Shape removed;
        double removed_volume{};
        if(prepared.empty())removed_volume=common_volume(cutter,remaining);
        else if(!cutter.bounds.IsOut(remaining.bounds)) {
            ++result.counts.intersections;removed=boolean<BRepAlgoAPI_Common>(remaining.shape,cutter.shape,tolerance);removed_volume=volume(removed);
        }else ++result.counts.bounds_rejections;
        if(removed_volume>minimum_volume) {
            result.remaining=boolean<BRepAlgoAPI_Cut>(remaining.shape,cutter.shape,tolerance);
            ++result.counts.stock_cuts;remaining=bounded(result.remaining,tolerance);
            for(std::size_t c=0;c<prepared.size();++c) {
                const auto& clamp=prepared[c];auto& contact=result.contacts[c];
                ++result.counts.intersections;
                const auto below=volume(boolean<BRepAlgoAPI_Common>(removed,clamp.below,tolerance));
                // Compare real contact before/after the exact removal. Empty
                // passes cannot repeat a warning, and a face merely touched by
                // the cutter does not count as lost grip.
                const auto previous=contact.remaining_mm2;
                if(!cutter.bounds.IsOut(clamp.contact.bounds)) {
                    ++result.counts.intersections;
                    contact.remaining_mm2=area(boolean<BRepAlgoAPI_Common>(result.remaining,clamp.contact.shape,tolerance));
                }else ++result.counts.bounds_rejections;
                const auto lost=std::max(0.,previous-contact.remaining_mm2);
                const auto warn=[&](ClampingWarningKind kind){result.warnings.push_back({kind,motion.operation_id,motion.block_id,motion.tool_id,
                    clampings[c].id,below,lost,contact.remaining_mm2});};
                if(below>minimum_volume)warn(ClampingWarningKind::BelowJawTop);
                if(lost>tolerance*tolerance)warn(ClampingWarningKind::ContactLoss);
            }
        }
    }
    if(progress)progress(program.motions.size(),program.motions.size());
    result.removed_mm3=initial_volume-volume(result.remaining);
    return result;
}
} // namespace zima::cam::native
