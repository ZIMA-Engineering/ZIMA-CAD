#include "native_sweep.hpp"
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_List.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace zima::cam::native {
namespace {
constexpr double tau=2*std::numbers::pi;
TopoDS_Shape fuse(const std::vector<TopoDS_Shape>& shapes,double tolerance) {
    if(shapes.empty())throw std::runtime_error("CAM sweep has no material.");
    if(shapes.size()==1)return shapes.front();
    NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(shapes.front());
    for(std::size_t i=1;i<shapes.size();++i)tools.Append(shapes[i]);
    BRepAlgoAPI_Fuse operation;operation.SetArguments(arguments);operation.SetTools(tools);
    operation.SetNonDestructive(true);operation.SetFuzzyValue(tolerance);operation.Build();
    if(!operation.IsDone()||operation.Shape().IsNull()||!BRepCheck_Analyzer(operation.Shape()).IsValid())
        throw std::runtime_error("CAM sweep union produced invalid material.");
    return operation.Shape();
}
std::vector<TopoDS_Shape> portions(const Tool& tool,Point tip,bool cutting) {
    validate(tool);const auto origin=tool_origin(tool,tip);
    const auto& stations=cutting?tool.cutting:tool.non_cutting;
    std::vector<TopoDS_Shape> result;
    for(std::size_t i=1;i<stations.size();++i) {
        const auto a=stations[i-1],b=stations[i];const double h=b.z_mm-a.z_mm;
        if(h==0||(a.radius_mm==0&&b.radius_mm==0))continue;
        const gp_Ax2 axis(gp_Pnt(origin.x,origin.y,origin.z+a.z_mm),gp_Dir(0,0,1));
        if(a.radius_mm==b.radius_mm)result.push_back(BRepPrimAPI_MakeCylinder(axis,a.radius_mm,h).Shape());
        else result.push_back(BRepPrimAPI_MakeCone(axis,a.radius_mm,b.radius_mm,h).Shape());
    }
    return result;
}
TopoDS_Shape circular_sweep(const Tool& tool,const Motion& motion,bool cutting,double tolerance) {
    validate(tool);
    std::vector<TopoDS_Shape> shapes;
    const double angle=std::abs(motion.sweep_radians);
    if(angle<tau) {
        shapes=portions(tool,motion.start,cutting);
        const auto end=portions(tool,motion.end,cutting);
        shapes.insert(shapes.end(),end.begin(),end.end());
    }
    const double path_radius=std::hypot(motion.start.x-motion.center.x,motion.start.y-motion.center.y);
    const double start_angle=std::atan2(motion.start.y-motion.center.y,motion.start.x-motion.center.x)+
        std::min(0.,motion.sweep_radians);
    const auto origin=tool_origin(tool,motion.start);
    const auto& stations=cutting?tool.cutting:tool.non_cutting;
    for(std::size_t i=1;i<stations.size();++i) {
        const auto a=stations[i-1],b=stations[i];
        if(a.z_mm==b.z_mm||(a.radius_mm==0&&b.radius_mm==0))continue;
        // At each height the swept disk is an annular sector plus the two
        // endpoint disks. Trim the inner cone where it reaches the axis,
        // while retaining one continuous outer cone across that height.
        const auto sector=[&](double z,double height,double r0,double r1)->TopoDS_Shape {
            const gp_Ax2 axis(gp_Pnt(motion.center.x,motion.center.y,origin.z+z),
                gp_Dir(0,0,1),gp_Dir(std::cos(start_angle),std::sin(start_angle),0));
            return r0==r1?BRepPrimAPI_MakeCylinder(axis,r0,height,angle).Shape():
                BRepPrimAPI_MakeCone(axis,r0,r1,height,angle).Shape();
        };
        auto shape=sector(a.z_mm,b.z_mm-a.z_mm,path_radius+a.radius_mm,path_radius+b.radius_mm);
        const double inner0=std::max(0.,path_radius-a.radius_mm),inner1=std::max(0.,path_radius-b.radius_mm);
        if(inner0>0||inner1>0) {
            double z0=a.z_mm,z1=b.z_mm;
            if(a.radius_mm>path_radius||b.radius_mm>path_radius) {
                const double t=(path_radius-a.radius_mm)/(b.radius_mm-a.radius_mm);
                const double apex=a.z_mm+t*(b.z_mm-a.z_mm);
                if(a.radius_mm>path_radius)z0=apex;else z1=apex;
            }
            BRepAlgoAPI_Cut cut;NCollection_List<TopoDS_Shape> arguments,tools;
            arguments.Append(shape);tools.Append(sector(z0,z1-z0,inner0,inner1));
            cut.SetArguments(arguments);cut.SetTools(tools);cut.SetNonDestructive(true);
            cut.SetFuzzyValue(tolerance);cut.Build();
            if(!cut.IsDone()||cut.Shape().IsNull()||!BRepCheck_Analyzer(cut.Shape()).IsValid())
                throw std::runtime_error("CAM circular sweep produced invalid material.");
            shape=cut.Shape();
        }
        shapes.push_back(shape);
    }
    return fuse(shapes,tolerance);
}
void append_solid(std::vector<TopoDS_Shape>& shapes,const TopoDS_Shape& shape) {
    for(TopExp_Explorer solids(shape,TopAbs_SOLID);solids.More();solids.Next()) {
        auto solid=TopoDS::Solid(solids.Current());
        if(!BRepLib::OrientClosedSolid(solid)||!BRepCheck_Analyzer(solid).IsValid())
            throw std::runtime_error("CAM sweep produced an invalid boundary volume.");
        if(volume(solid)>0)shapes.push_back(solid);
    }
}
}
double volume(const TopoDS_Shape& shape) {
    GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);return properties.Mass();
}
TopoDS_Shape tool_shape(const Tool& tool,Point tip,bool cutting) {
    return fuse(portions(tool,tip,cutting),1e-7);
}
TopoDS_Shape swept_tool(const Tool& tool,const Motion& motion,bool cutting,double tolerance) {
    if(!std::isfinite(tolerance)||tolerance<=0)throw std::invalid_argument("Invalid CAM sweep tolerance.");
    const bool curved=motion.kind==MotionKind::ArcClockwise||motion.kind==MotionKind::ArcCounterclockwise;
    if(curved)return circular_sweep(tool,motion,cutting,tolerance);
    auto pieces=portions(tool,motion.start,cutting);auto result=pieces;
    const gp_Vec translation(motion.end.x-motion.start.x,motion.end.y-motion.start.y,motion.end.z-motion.start.z);
    if(translation.Magnitude()<1e-14)return fuse(result,tolerance);
    for(const auto& piece:pieces)for(TopExp_Explorer faces(piece,TopAbs_FACE);faces.More();faces.Next()) {
        const auto face=TopoDS::Face(faces.Current());BRepAdaptor_Surface surface(face);
        double u0{},u1{},v0{},v1{};BRepTools::UVBounds(face,u0,u1,v0,v1);
        const auto velocity_dot_normal=[&](double u) {
            gp_Pnt point;gp_Vec du,dv;surface.D1(u,(v0+v1)/2,point,du,dv);
            auto normal=du.Crossed(dv);if(normal.Magnitude()<1e-14)return 0.;normal.Normalize();
            return normal.Dot(translation);
        };
        std::vector<double> cuts{u0,u1};
        if(surface.GetType()==GeomAbs_Plane) {
            if(std::abs(velocity_dot_normal((u0+u1)/2))<1e-12)continue;
        }else {
            if(surface.GetType()!=GeomAbs_Cylinder&&surface.GetType()!=GeomAbs_Cone)
                throw std::runtime_error("Unexpected CAM tool surface.");
            // Split at the exact motion silhouettes. Each strip is monotone
            // along the sweep and bounds an ordinary solid instead of a folded
            // full-cylinder extrusion. Union it with the initial material.
            const double f0=velocity_dot_normal(0),fp=velocity_dot_normal(std::numbers::pi);
            const double c=(f0+fp)/2,a=(f0-fp)/2,b=velocity_dot_normal(std::numbers::pi/2)-c;
            const double amplitude=std::hypot(a,b);
            if(amplitude<1e-12&&std::abs(c)<1e-12)continue;
            if(amplitude>0&&std::abs(c)<amplitude) {
                const double phase=std::atan2(b,a),delta=std::acos(-c/amplitude);
                for(double root:{phase-delta,phase+delta}) {
                    root=u0+std::fmod(std::fmod(root-u0,tau)+tau,tau);
                    if(root>u0+1e-12&&root<u1-1e-12)cuts.push_back(root);
                }
            }
            std::sort(cuts.begin(),cuts.end());
        }
        for(std::size_t i=1;i<cuts.size();++i) {
            const auto strip=surface.GetType()==GeomAbs_Plane?face:
                BRepBuilderAPI_MakeFace(BRep_Tool::Surface(face),cuts[i-1],cuts[i],v0,v1,tolerance).Face();
            append_solid(result,BRepPrimAPI_MakePrism(strip,translation,true).Shape());
        }
    }
    return fuse(result,tolerance);
}
}
