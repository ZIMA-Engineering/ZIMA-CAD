#include <zima/kernel/occt_surface_data.hpp>
#include <zima/kernel/exact_surface_json.hpp>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRep_Builder.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_OffsetSurface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <GeomConvert.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <TopoDS_Wire.hxx>
#include <chrono>
#include <iostream>
#include <numbers>

using namespace zima::kernel;
namespace {
void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
template<class Point> void near(Vec3 actual,const Point& expected,double tolerance=1e-7) {
    if(std::hypot(actual.x-expected.X(),actual.y-expected.Y(),actual.z-expected.Z())>tolerance)
        throw std::runtime_error("Exact captured surface differs from its native source");
}
Handle(Geom_BSplineSurface) spline() {
    NCollection_Array2<gp_Pnt> poles(1,3,1,3);NCollection_Array2<double> weights(1,3,1,3);
    for(int u=1;u<=3;++u)for(int v=1;v<=3;++v) {
        poles.SetValue(u,v,gp_Pnt(u-1,v-1,(u==3?1.:0)+(v==3?2.:0)));
        weights.SetValue(u,v,1.+.13*(u==2)+.17*(v==2));
    }
    NCollection_Array1<double> knots(1,2);knots.SetValue(1,0);knots.SetValue(2,1);
    NCollection_Array1<int> multiplicities(1,2);multiplicities.Init(3);
    return new Geom_BSplineSurface(poles,weights,knots,knots,multiplicities,multiplicities,2,2);
}
TopoDS_Face extrapolated_trim() {
    // Imported trims may extend beyond the support's knot domain. OCCT
    // evaluates the end span there; knot insertion/Segment rejects it.
    const auto surface=spline();
    const std::array<gp_Pnt2d,4> corners={gp_Pnt2d(-.00001,.1),gp_Pnt2d(1.00005,.1),
        gp_Pnt2d(1.00005,1.00003),gp_Pnt2d(-.00001,1.00003)};
    BRepBuilderAPI_MakeWire wire;
    for(std::size_t i=0;i<corners.size();++i) {
        const auto& from=corners[i];const auto& to=corners[(i+1)%corners.size()];
        const auto curve=new Geom2d_TrimmedCurve(new Geom2d_Line(from,gp_Dir2d(gp_Vec2d(from,to))),0.,from.Distance(to));
        wire.Add(BRepBuilderAPI_MakeEdge(curve,surface).Edge());
    }
    BRep_Builder builder;TopoDS_Face face;builder.MakeFace(face,surface,1e-7);builder.Add(face,wire.Wire());
    return face;
}
void verify(const TopoDS_Face& source) {
    for(bool reverse:{false,true})for(bool transformed:{false,true}) {
        auto face=reverse?TopoDS::Face(source.Reversed()):source;
        if(transformed) {
            gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(3,-2,7),gp_Dir(1,2,3)),.73);
            face=TopoDS::Face(BRepBuilderAPI_Transform(face,rotation,true).Shape());
        }
        const auto exact=capture_exact_surface_geometry(face);
        const auto reopened=exact_surface_from_json(nlohmann::json::parse(exact_surface_json(*exact).dump()));
        BRepAdaptor_Surface native(face);
        for(double fu:{0.,.13,.37,.81,1.})for(double fv:{0.,.17,.53,.88,1.}) {
            const auto u=std::lerp(exact->u_min,exact->u_max,fu),v=std::lerp(exact->v_min,exact->v_max,fv);
            gp_Pnt p;gp_Vec du,dv,duu,dvv,duv;native.D2(u,v,p,du,dv,duu,dvv,duv);
            auto normal=du.Crossed(dv).Normalized();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
            const auto evaluated=evaluate_exact_surface(*exact,u,v);
            check(evaluated.regular,"Captured regular surface became singular");
            near(evaluated.point,p);near(evaluated.du,du);near(evaluated.dv,dv);
            near(evaluated.duu,duu);near(evaluated.duv,duv);near(evaluated.dvv,dvv);near(evaluated.normal,normal);
            const auto persisted=evaluate_exact_surface(*reopened,u,v);
            near(persisted.point,p);near(persisted.normal,normal);near(persisted.du,du);near(persisted.dv,dv);
            near(persisted.duu,duu);near(persisted.duv,duv);near(persisted.dvv,dvv);
            for(double offset:{-.1,0.,.2}) {
                const auto shifted=evaluate_exact_surface(*exact,u,v,offset);
                check(shifted.regular,"Captured offset is singular");near(shifted.point,p.Translated(normal*offset));
            }
            BRepClass_FaceClassifier classified(face,gp_Pnt2d(u,v),1e-7);
            check(exact_surface_domain_contains(*exact,u,v)==(classified.State()==TopAbs_IN||classified.State()==TopAbs_ON),
                "Native trim domain differs from source face");
            check(exact_surface_domain_contains(*exact,u,v)==exact_surface_domain_contains(*reopened,u,v),
                "Native surface reopening changed its trimming domain");
        }
    }
}
}
int main() {
    try {
        const auto start=std::chrono::steady_clock::now();
        verify(BRepBuilderAPI_MakeFace(spline(),.07,.93,.11,.89,1e-7).Face());
        verify(extrapolated_trim());
        verify(BRepBuilderAPI_MakeFace(new Geom_OffsetSurface(spline(),.15),.07,.93,.11,.89,1e-7).Face());
        const auto ellipse=new Geom_Ellipse(gp_Ax2(gp_Pnt(3,-2,1),gp_Dir(0,0,1)),4,2);
        verify(BRepBuilderAPI_MakeFace(new Geom_SurfaceOfLinearExtrusion(ellipse,gp_Dir(0,0,1)),.2,5.9,-4,7,1e-7).Face());
        NCollection_Array1<gp_Pnt> poles(1,3);poles.SetValue(1,{2,0,0});poles.SetValue(2,{3,0,2});poles.SetValue(3,{2,0,4});
        verify(BRepBuilderAPI_MakeFace(new Geom_SurfaceOfRevolution(new Geom_BezierCurve(poles),gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1))),.1,5.8,.1,.9,1e-7).Face());
        verify(BRepBuilderAPI_MakeFace(new Geom_ToroidalSurface(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,0,1)),4,1),.2,5.9,.1,6.1,1e-7).Face());
        const auto periodic=GeomConvert::SurfaceToBSplineSurface(new Geom_ToroidalSurface(
            gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,0,1)),4,1));
        periodic->SetUPeriodic();
        verify(BRepBuilderAPI_MakeFace(periodic,5.8,6.8,.1,6.1,1e-7).Face());
        periodic->SetVPeriodic();
        verify(BRepBuilderAPI_MakeFace(periodic,5.8,6.8,5.7,6.7,1e-7).Face());
        BRepBuilderAPI_MakeFace perforated(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-3,3,-3,3);
        auto hole=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,0,1)),1))).Wire();
        hole.Reverse();perforated.Add(hole);verify(perforated.Face());
        std::cout<<"Exact capture: rational spline, offset, elliptical extrusion, revolution, torus, trim hole, transformed/reversed faces and native derivative oracle passed in "
            <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms\n";
        return 0;
    }catch(const Standard_Failure& e){std::cerr<<e.GetMessageString()<<'\n';return 1;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
