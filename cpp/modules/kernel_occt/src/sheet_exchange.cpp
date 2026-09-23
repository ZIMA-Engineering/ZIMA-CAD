#include <zima/kernel/sheet_exchange.hpp>
#include <zima/kernel/occt_curve_data.hpp>
#include <zima/kernel/sheet_material.hpp>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_Pln.hxx>
#include <sstream>

namespace zima::kernel {
FlatContour sheet_flat_contour(const BodyResult& body,double thickness,double tolerance) {
    using namespace sheet_material;
    if(body.kernel_shape.empty()||!(thickness>0)||!(tolerance>0))
        throw std::invalid_argument("Sheet DXF requires a calculated sheet body.");
    BRep_Builder builder;TopoDS_Shape shape;std::istringstream input(body.kernel_shape);BRepTools::Read(shape,input,builder);
    if(shape.IsNull())throw std::invalid_argument("Sheet DXF requires a calculated sheet body.");
    double largest=0;gp_Pln plane;bool reversed=false;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface surface(face);
        if(surface.GetType()!=GeomAbs_Plane)continue;
        GProp_GProps area;BRepGProp::SurfaceProperties(face,area);
        if(area.Mass()>largest){largest=area.Mass();plane=surface.Plane();reversed=face.Orientation()==TopAbs_REVERSED;}
    }
    if(largest<=0)throw std::invalid_argument("Sheet DXF requires a planar unfolded body.");
    const auto vec=[](const auto& p)->Vec3{return {p.X(),p.Y(),p.Z()};};
    const auto normal=mul(vec(plane.Axis().Direction()),reversed?-1:1);
    FlatContour result;result.origin=sub(vec(plane.Location()),mul(normal,thickness*.5));
    result.x_axis=vec(plane.XAxis().Direction());result.y_axis=cross(normal,result.x_axis);
    // Export must never silently flatten still-folded walls or a second sheet
    // lying in another plane. The displayed tessellation is already persisted.
    for(const auto p:body.mesh.vertices)
        if(std::abs(dot(sub(p,result.origin),normal))>thickness*.5+tolerance)
            throw std::invalid_argument("Sheet DXF requires all material in one unfolded plane.");
    BRepAlgoAPI_Section section(shape,gp_Pln(gp_Pnt(result.origin.x,result.origin.y,result.origin.z),gp_Dir(normal.x,normal.y,normal.z)),false);
    section.Approximation(true);section.Build();
    if(!section.IsDone())throw std::runtime_error("Sheet DXF contour calculation failed.");
    TopTools_IndexedMapOfShape edges;TopExp::MapShapes(section.Shape(),TopAbs_EDGE,edges);
    const auto local=[&](const auto& p){const auto d=sub(vec(p),result.origin);return Vec3{dot(d,result.x_axis),dot(d,result.y_axis),0};};
    for(int i=1;i<=edges.Extent();++i) {
        BRepAdaptor_Curve adaptor(TopoDS::Edge(edges(i)));
        if(adaptor.GetType()==GeomAbs_Circle) {
            const auto circle=adaptor.Circle();
            const auto start=local(adaptor.Value(adaptor.FirstParameter())),end=local(adaptor.Value(adaptor.LastParameter()));
            result.arcs.push_back({local(circle.Location()),start,end,circle.Radius(),dot(vec(circle.Axis().Direction()),normal)<0,
                std::abs(adaptor.LastParameter()-adaptor.FirstParameter()-2*std::acos(-1.))<1e-7});continue;
        }
        auto curve=capture_bspline_geometry(adaptor);
        if(!curve)throw std::runtime_error("Sheet DXF contour calculation failed.");
        for(auto& p:curve->poles){const auto delta=sub(p,result.origin);p={dot(delta,result.x_axis),dot(delta,result.y_axis),0};}
        result.curves.push_back(std::move(*curve));
    }
    if(result.curves.empty()&&result.arcs.empty())throw std::runtime_error("Sheet DXF contour calculation failed.");
    return result;
}
}
