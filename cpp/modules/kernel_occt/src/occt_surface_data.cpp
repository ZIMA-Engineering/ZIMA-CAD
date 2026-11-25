#include <zima/kernel/occt_surface_data.hpp>
#include <zima/kernel/occt_curve_data.hpp>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_OffsetCurve.hxx>
#include <Geom_OffsetSurface.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <GeomConvert.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom2dConvert.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Elips.hxx>
#include <gp_Hypr.hxx>
#include <gp_Parab.hxx>
#include <gp_Pln.hxx>

namespace zima::kernel {
namespace {
template<class Point> Vec3 vector(const Point& p) {return {p.X(),p.Y(),p.Z()};}
template<class Data,class Frame> void frame(Data& data,const Frame& f) {
    data.origin=vector(f.Location());data.x_axis=vector(f.XDirection());
    data.y_axis=vector(f.YDirection());data.z_axis=vector(f.Direction());
}
BSplineGeometry spline_data(const Handle(Geom_BSplineCurve)& curve) {
    BSplineGeometry result;result.degree=curve->Degree();
    for(int i=1;i<=curve->NbPoles();++i){result.poles.push_back(vector(curve->Pole(i)));result.weights.push_back(curve->Weight(i));}
    for(int i=1;i<=curve->NbKnots();++i)result.knots.insert(result.knots.end(),curve->Multiplicity(i),curve->Knot(i));
    result.validate();return result;
}
std::shared_ptr<const ExactSurfaceGenerator> generator(Handle(Geom_Curve) source,double first,double last) {
    while(const auto trimmed=Handle(Geom_TrimmedCurve)::DownCast(source))source=trimmed->BasisCurve();
    auto result=std::make_shared<ExactSurfaceGenerator>();using Kind=ExactSurfaceGenerator::Kind;
    if(const auto offset=Handle(Geom_OffsetCurve)::DownCast(source)) {
        result->kind=Kind::Offset;result->offset=offset->Offset();result->z_axis=vector(offset->Direction());
        result->basis=generator(offset->BasisCurve(),first,last);result->validate();return result;
    }
    GeomAdaptor_Curve curve(source,first,last);
    switch(curve.GetType()) {
    case GeomAbs_Line:result->kind=Kind::Line;result->origin=vector(curve.Line().Location());result->x_axis=vector(curve.Line().Direction());break;
    case GeomAbs_Circle:result->kind=Kind::Circle;frame(*result,curve.Circle().Position());result->radius=curve.Circle().Radius();break;
    case GeomAbs_Ellipse:result->kind=Kind::Ellipse;frame(*result,curve.Ellipse().Position());result->radius=curve.Ellipse().MajorRadius();result->minor_radius=curve.Ellipse().MinorRadius();break;
    case GeomAbs_Parabola:result->kind=Kind::Parabola;frame(*result,curve.Parabola().Position());result->radius=curve.Parabola().Focal();break;
    case GeomAbs_Hyperbola:result->kind=Kind::Hyperbola;frame(*result,curve.Hyperbola().Position());result->radius=curve.Hyperbola().MajorRadius();result->minor_radius=curve.Hyperbola().MinorRadius();break;
    case GeomAbs_BSplineCurve:
    case GeomAbs_BezierCurve: {
        auto spline=curve.GetType()==GeomAbs_BSplineCurve?
            Handle(Geom_BSplineCurve)::DownCast(curve.BSpline()->Copy()):GeomConvert::CurveToBSplineCurve(source);
        // A nonperiodic support is already a complete mathematical definition.
        // Its face trim may legitimately use end-span extrapolation; inserting
        // trim knots would reject that domain or unnecessarily alter the basis.
        if(spline->IsPeriodic()){spline->Segment(first,last);spline->SetNotPeriodic();}
        result->spline=spline_data(spline);break;
    }
    default:throw std::runtime_error("Unsupported exact surface generator");
    }
    result->validate();return result;
}
std::shared_ptr<const ExactSurface> support(Handle(Geom_Surface) source,double u0,double u1,double v0,double v1) {
    while(const auto trimmed=Handle(Geom_RectangularTrimmedSurface)::DownCast(source))source=trimmed->BasisSurface();
    auto result=std::make_shared<ExactSurface>();using Kind=ExactSurface::Kind;
    if(const auto offset=Handle(Geom_OffsetSurface)::DownCast(source)) {
        result->kind=Kind::Offset;result->offset=offset->Offset();result->basis=support(offset->BasisSurface(),u0,u1,v0,v1);
        result->validate();return result;
    }
    GeomAdaptor_Surface surface(source);
    switch(surface.GetType()) {
    case GeomAbs_Plane:result->kind=Kind::Plane;frame(*result,surface.Plane().Position());break;
    case GeomAbs_Cylinder:result->kind=Kind::Cylinder;frame(*result,surface.Cylinder().Position());result->radius=surface.Cylinder().Radius();break;
    case GeomAbs_Cone:result->kind=Kind::Cone;frame(*result,surface.Cone().Position());result->radius=surface.Cone().RefRadius();result->semi_angle=surface.Cone().SemiAngle();break;
    case GeomAbs_Sphere:result->kind=Kind::Sphere;frame(*result,surface.Sphere().Position());result->radius=surface.Sphere().Radius();break;
    case GeomAbs_Torus:result->kind=Kind::Torus;frame(*result,surface.Torus().Position());result->radius=surface.Torus().MajorRadius();result->minor_radius=surface.Torus().MinorRadius();break;
    case GeomAbs_BSplineSurface:
    case GeomAbs_BezierSurface: {
        auto spline=surface.GetType()==GeomAbs_BSplineSurface?
            Handle(Geom_BSplineSurface)::DownCast(surface.BSpline()->Copy()):GeomConvert::SurfaceToBSplineSurface(source);
        if(spline->IsUPeriodic()||spline->IsVPeriodic()) {
            double bu0,bu1,bv0,bv1;spline->Bounds(bu0,bu1,bv0,bv1);
            // Unroll periodic directions over the face's parameter window.
            // Preserve nonperiodic directions, including their extrapolation.
            spline->Segment(spline->IsUPeriodic()?u0:bu0,spline->IsUPeriodic()?u1:bu1,
                spline->IsVPeriodic()?v0:bv0,spline->IsVPeriodic()?v1:bv1);
            if(spline->IsUPeriodic())spline->SetUNotPeriodic();
            if(spline->IsVPeriodic())spline->SetVNotPeriodic();
        }
        result->kind=Kind::Spline;result->u_degree=spline->UDegree();result->v_degree=spline->VDegree();
        result->u_count=spline->NbUPoles();result->v_count=spline->NbVPoles();
        for(int u=1;u<=spline->NbUPoles();++u)for(int v=1;v<=spline->NbVPoles();++v) {
            result->poles.push_back(vector(spline->Pole(u,v)));result->weights.push_back(spline->Weight(u,v));
        }
        for(int u=1;u<=spline->NbUKnots();++u)result->u_knots.insert(result->u_knots.end(),spline->UMultiplicity(u),spline->UKnot(u));
        for(int v=1;v<=spline->NbVKnots();++v)result->v_knots.insert(result->v_knots.end(),spline->VMultiplicity(v),spline->VKnot(v));
        break;
    }
    case GeomAbs_SurfaceOfExtrusion: {
        const auto extrusion=Handle(Geom_SurfaceOfLinearExtrusion)::DownCast(source);
        result->kind=Kind::Extrusion;result->z_axis=vector(extrusion->Direction());
        result->generator=generator(extrusion->BasisCurve(),u0,u1);break;
    }
    case GeomAbs_SurfaceOfRevolution: {
        const auto revolution=Handle(Geom_SurfaceOfRevolution)::DownCast(source);
        result->kind=Kind::Revolution;result->origin=vector(revolution->Axis().Location());
        result->z_axis=vector(revolution->Axis().Direction());result->generator=generator(revolution->BasisCurve(),v0,v1);break;
    }
    default:throw std::runtime_error("Unsupported exact surface definition");
    }
    result->validate();return result;
}
}

std::shared_ptr<const ExactSurfaceGeometry> capture_exact_surface_geometry(const TopoDS_Face& face) {
    auto result=std::make_shared<ExactSurfaceGeometry>();
    BRepTools::UVBounds(face,result->u_min,result->u_max,result->v_min,result->v_max);
    TopLoc_Location location;const auto source=BRep_Tool::Surface(face,location);
    result->support=support(source,result->u_min,result->u_max,result->v_min,result->v_max);
    const auto transform=location.Transformation();
    result->origin=vector(gp_Pnt(0,0,0).Transformed(transform));
    result->x_axis=vector(gp_Vec(1,0,0).Transformed(transform));
    result->y_axis=vector(gp_Vec(0,1,0).Transformed(transform));
    result->z_axis=vector(gp_Vec(0,0,1).Transformed(transform));
    result->reversed=face.Orientation()==TopAbs_REVERSED;
    const auto forward=TopoDS::Face(face.Oriented(TopAbs_FORWARD));
    for(TopExp_Explorer wires(forward,TopAbs_WIRE);wires.More();wires.Next()) {
        std::vector<BSplineGeometry> loop;
        for(BRepTools_WireExplorer edges(TopoDS::Wire(wires.Current()),forward);edges.More();edges.Next()) {
            const auto edge=edges.Current();double first,last;
            const auto curve=BRep_Tool::CurveOnSurface(edge,forward,first,last);
            if(curve.IsNull())throw std::runtime_error("Exact surface trimming curve is missing");
            auto spline=Geom2dConvert::CurveToBSplineCurve(new Geom2d_TrimmedCurve(curve,first,last));
            if(spline->IsPeriodic())spline->SetNotPeriodic();
            if(edge.Orientation()==TopAbs_REVERSED)spline->Reverse();
            BSplineGeometry data;data.degree=spline->Degree();
            for(int i=1;i<=spline->NbPoles();++i) {
                const auto p=spline->Pole(i);data.poles.push_back({p.X(),p.Y(),0});data.weights.push_back(spline->Weight(i));
            }
            for(int i=1;i<=spline->NbKnots();++i)data.knots.insert(data.knots.end(),spline->Multiplicity(i),spline->Knot(i));
            data.validate();loop.push_back(std::move(data));
        }
        if(!loop.empty())result->trim_loops.push_back(std::move(loop));
    }
    result->validate();return result;
}
}
