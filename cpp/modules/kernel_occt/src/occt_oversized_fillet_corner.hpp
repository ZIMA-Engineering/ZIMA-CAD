#pragma once
#include <zima/kernel/oversized_fillet_corner.hpp>
#include <Geom_BSplineSurface.hxx>
#include <gp_Elips.hxx>
#include <ShapeFix_Shape.hxx>

namespace zima::kernel::oversized_fillet_corner {

struct Result { TopoDS_Shape solid; std::vector<TopoDS_Face> faces; };

// A planar line/arc/line corner with perpendicular side supports can have a
// larger new round than its existing plan radius. Its ordinary pipe folds
// through the rolling axis. Build two cylindrical strips and their exact
// rational corner instead. Primary straight edges need not be perpendicular.
// All shapes below are private tools; the caller retains terminal clipping,
// material operations and the normal source-edge ancestry publication.
inline std::optional<Result> build(std::span<const TopoDS_Edge> edges,
        std::span<const std::array<TopoDS_Face,2>> supports,
        const gp_Dir& normal,double radius,double tolerance) {
    if(edges.size()!=3||supports.size()!=3||
       BRepAdaptor_Curve(edges[0]).GetType()!=GeomAbs_Line||
       BRepAdaptor_Curve(edges[1]).GetType()!=GeomAbs_Circle||
       BRepAdaptor_Curve(edges[2]).GetType()!=GeomAbs_Line)return {};
    const BRepAdaptor_Curve arc_curve(edges[1]);const auto circle=arc_curve.Circle();
    const double small=circle.Radius();
    if(radius<=small+tolerance||!circle.Axis().Direction().IsParallel(normal,1.e-9))return {};
    const auto endpoint=[](const TopoDS_Edge& edge,bool last) {
        TopoDS_Vertex a,b;TopExp::Vertices(edge,a,b,true);
        return BRep_Tool::Pnt(last?b:a);
    };
    const auto a=endpoint(edges[1],false),b=endpoint(edges[1],true),center=circle.Location();
    const auto far_a=endpoint(edges[0],false),far_b=endpoint(edges[2],true);
    const gp_Vec n(normal),inward_a=gp_Vec(a,center).Normalized(),inward_b=gp_Vec(b,center).Normalized();
    const auto first=gp_Vec(a,far_a).Normalized(),second=gp_Vec(b,far_b).Normalized();
    if(std::abs(first.Dot(inward_a))>1.e-9||std::abs(second.Dot(inward_b))>1.e-9)return {};
    // Limit this construction to its actual plane/cylinder support equations.
    // Other existing route families continue through their established builder.
    for(std::size_t i=0;i<3;++i) {
        bool plane=false,side=false;
        for(const auto& face:supports[i]) {
            const BRepAdaptor_Surface surface(face);
            if(surface.GetType()==GeomAbs_Plane) {
                const auto axis=surface.Plane().Axis().Direction();
                plane|=axis.IsParallel(normal,1.e-9)&&surface.Plane().Distance(a)<=tolerance;
                side|=i!=1&&std::abs(axis.Dot(normal))<1.e-9;
            } else if(i==1&&surface.GetType()==GeomAbs_Cylinder) {
                const auto cylinder=surface.Cylinder();
                side|=cylinder.Axis().Direction().IsParallel(normal,1.e-9)&&
                    std::abs(cylinder.Radius()-small)<=tolerance&&
                    gp_Lin(cylinder.Axis()).Distance(center)<=tolerance;
            }
        }
        if(!plane||!side)return {};
    }
    const auto vec=[](const auto& p){return Vec3{p.X(),p.Y(),p.Z()};};
    const auto frame=oversized_fillet_corner_frame(vec(a),vec(b),vec(center),vec(n),vec(first),vec(second),radius);
    if(!frame)return {};
    const gp_Pnt apex(frame->apex.x,frame->apex.y,frame->apex.z);

    std::array<TopoDS_Face,2> strips,caps;
    std::array<gp_Pnt,2> tangent_far;
    for(std::size_t i=0;i<2;++i) {
        const auto point=i?b:a,far=i?far_b:far_a;
        const auto inward=i?inward_b:inward_a,x=-inward,y=n,z=x.Crossed(y);
        const auto origin=point.Translated(inward*radius-n*radius);
        occ::handle<Geom_CylindricalSurface> cylinder=new Geom_CylindricalSurface(
            gp_Ax3(origin,gp_Dir(z),gp_Dir(x)),radius);
        const auto support=BRepBuilderAPI_MakeFace(cylinder,tolerance).Face();
        const double travel=gp_Vec(point.Translated(inward*radius),apex).Dot(z);
        const auto major=-inward*radius-z*travel;
        const gp_Elips ellipse(gp_Ax2(apex.Translated(-n*radius),
            gp_Dir(major.Crossed(n)),gp_Dir(major)),major.Magnitude(),radius);
        const auto boundary=BRepBuilderAPI_MakeEdge(ellipse,0.,std::numbers::pi/2.).Edge();
        ShapeFix_Edge().FixAddPCurve(boundary,support,false,tolerance);
        support_driven_fillet::precise_pcurve(boundary,support,tolerance*.1);
        const auto low_far=far.Translated(-n*radius);
        tangent_far[i]=far.Translated(inward*radius);
        const auto end_center=far.Translated(inward*radius-n*radius);
        const gp_Circ end_circle(gp_Ax2(end_center,gp_Dir(z),gp_Dir(x)),radius);
        const auto end_arc=BRepBuilderAPI_MakeEdge(end_circle,0.,std::numbers::pi/2.).Edge();
        BRepBuilderAPI_MakeWire strip;
        strip.Add(BRepBuilderAPI_MakeEdge(low_far,point.Translated(-n*radius)));
        strip.Add(boundary);strip.Add(BRepBuilderAPI_MakeEdge(apex,tangent_far[i]));strip.Add(end_arc);
        strips[i]=BRepBuilderAPI_MakeFace(cylinder,strip.Wire(),true).Face();
        ShapeFix_Edge fix;
        for(TopExp_Explorer e(strips[i],TopAbs_EDGE);e.More();e.Next())
            fix.FixAddPCurve(TopoDS::Edge(e.Current()),strips[i],false,tolerance);
        BRepBuilderAPI_MakeWire cap;
        cap.Add(BRepBuilderAPI_MakeEdge(far,tangent_far[i]));cap.Add(end_arc);
        cap.Add(BRepBuilderAPI_MakeEdge(low_far,far));
        caps[i]=BRepBuilderAPI_MakeFace(cap.Wire()).Face();
    }

    // P(u,v) = apex + cos(u)*(arc(v)-apex) + R*(sin(u)-1)*normal.
    // A rational quarter circle in u times the exact source arc in v gives a
    // finite tensor-product surface. It is tangent to all three supports and
    // collapses only its top parameter edge to the regular shared apex.
    double first_parameter,last_parameter;const auto source=BRep_Tool::Curve(edges[1],first_parameter,last_parameter);
    const auto arc=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(source,first_parameter,last_parameter));
    NCollection_Array2<gp_Pnt> poles(1,3,1,arc->NbPoles());
    NCollection_Array2<double> weights(1,3,1,arc->NbPoles());
    for(int j=1;j<=arc->NbPoles();++j) {
        poles(1,j)=arc->Pole(j).Translated(-n*radius);poles(2,j)=arc->Pole(j);poles(3,j)=apex;
        weights(1,j)=arc->Weight(j);weights(2,j)=std::sqrt(.5)*arc->Weight(j);weights(3,j)=arc->Weight(j);
    }
    NCollection_Array1<double> knots(1,2);knots(1)=0.;knots(2)=1.;
    NCollection_Array1<int> multiplicities(1,2);multiplicities.Init(3);
    occ::handle<Geom_BSplineSurface> surface=new Geom_BSplineSurface(poles,weights,knots,arc->Knots(),
        multiplicities,arc->Multiplicities(),2,arc->Degree(),false,false);
    const auto patch=BRepBuilderAPI_MakeFace(surface,tolerance).Face();
    BRepBuilderAPI_MakeWire spine;for(const auto& edge:edges)spine.Add(edge);
    const auto private_spine=TopoDS::Wire(BRepBuilderAPI_Copy(spine.Wire(),true,false).Shape());
    BRepBuilderAPI_MakeWire top;
    top.Add(private_spine);top.Add(BRepBuilderAPI_MakeEdge(far_b,tangent_far[1]));
    top.Add(BRepBuilderAPI_MakeEdge(tangent_far[1],apex));top.Add(BRepBuilderAPI_MakeEdge(apex,tangent_far[0]));
    top.Add(BRepBuilderAPI_MakeEdge(tangent_far[0],far_a));
    BRepBuilderAPI_Sewing sewing(tolerance);
    sewing.Add(BRepPrimAPI_MakePrism(private_spine,-n*radius).Shape());
    sewing.Add(BRepBuilderAPI_MakeFace(top.Wire()).Face());sewing.Add(patch);
    for(const auto& face:strips)sewing.Add(face);
    for(const auto& face:caps)sewing.Add(face);
    sewing.Perform();
    if(sewing.NbFreeEdges()||sewing.SewedShape().ShapeType()!=TopAbs_SHELL)
        throw std::runtime_error(support_driven_fillet::tangent_transition_error);
    auto solid=BRepBuilderAPI_MakeSolid(TopoDS::Shell(sewing.SewedShape())).Solid();
    BRepLib::OrientClosedSolid(solid);
    ShapeFix_Shape fix(solid);fix.SetPrecision(tolerance);fix.SetMinTolerance(tolerance*.01);
    fix.SetMaxTolerance(tolerance);fix.Perform();
    Result result{fix.Shape(),{}};
    for(const auto& face:{strips[0],patch,strips[1]}) {
        const auto sewn=sewing.ModifiedSubShape(face);
        const auto retained=fix.Context()->Apply(sewn.IsNull()?face:sewn);
        if(retained.ShapeType()!=TopAbs_FACE)throw std::runtime_error(support_driven_fillet::tangent_transition_error);
        result.faces.push_back(TopoDS::Face(retained));
    }
    support_driven_fillet::validate_contact_curves(result.solid);
    return result;
}
} // namespace zima::kernel::oversized_fillet_corner
