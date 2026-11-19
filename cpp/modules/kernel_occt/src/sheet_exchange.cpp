#include <zima/kernel/sheet_exchange.hpp>
#include <zima/kernel/occt_curve_data.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <zima/kernel/sheet_material.hpp>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepGProp.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopoDS_Shape.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <gp_Pln.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <gp_Ax3.hxx>
#include <sstream>

namespace zima::kernel {
FlatContour clip_sheet_face_contour(const BodyResult& body,
        const ModelEnvelope& frame,
        const std::vector<std::array<double,3>>& half_planes,double tolerance) {
    const auto fail=[] {throw std::invalid_argument(
        "Sheet conversion cannot split a curved continuation face.");};
    const auto point=[](Vec3 p){return gp_Pnt(p.x,p.y,p.z);};
    const auto direction=[](Vec3 p){return gp_Dir(p.x,p.y,p.z);};
    auto shape=read_calculated_body_shape(body);
    if(shape.IsNull()||!(tolerance>0))fail();
    gp_Trsf local;local.SetTransformation(gp_Ax3(point(frame.origin),
        direction(frame.axes[2]),direction(frame.axes[0])));
    shape=BRepBuilderAPI_Transform(shape,local,true).Shape();
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())
        if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()!=GeomAbs_Plane)fail();
    Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,true);
    if(bounds.IsVoid())fail();
    double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
    if(std::max(std::abs(z0),std::abs(z1))>tolerance*10)fail();
    const double margin=std::max({x1-x0,y1-y0,1.});
    using P=std::array<double,2>;
    std::vector<P> polygon{{x0-margin,y0-margin},{x1+margin,y0-margin},
        {x1+margin,y1+margin},{x0-margin,y1+margin}};
    for(const auto& plane:half_planes) {
        if(polygon.empty())fail();
        const auto value=[&](P p){return plane[0]*p[0]+plane[1]*p[1]+plane[2];};
        std::vector<P> clipped;auto previous=polygon.back();double before=value(previous);
        for(const auto p:polygon) {
            const double after=value(p);
            if((before>=0)!=(after>=0)) {
                const double t=before/(before-after);
                clipped.push_back({previous[0]+t*(p[0]-previous[0]),previous[1]+t*(p[1]-previous[1])});
            }
            if(after>=0)clipped.push_back(p);
            previous=p;before=after;
        }
        polygon=std::move(clipped);
    }
    if(polygon.size()<3)fail();
    BRepBuilderAPI_MakePolygon boundary;
    for(const auto p:polygon)boundary.Add(gp_Pnt(p[0],p[1],0));
    boundary.Close();if(!boundary.IsDone())fail();
    BRepBuilderAPI_MakeFace mask(boundary.Wire(),true);if(!mask.IsDone())fail();
    BRepAlgoAPI_Common clipped(shape,mask.Face());
    if(!clipped.IsDone()||clipped.Shape().IsNull()||!BRepCheck_Analyzer(clipped.Shape()).IsValid())fail();
    GProp_GProps measured;BRepGProp::SurfaceProperties(clipped.Shape(),measured);
    if(measured.Mass()<=tolerance*tolerance)fail();
    FlatContour result;result.x_axis={1,0,0};result.y_axis={0,1,0};
    NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> edges;
    TopExp::MapShapes(clipped.Shape(),TopAbs_EDGE,edges);
    const auto vec=[](const auto& p)->Vec3{return {p.X(),p.Y(),p.Z()};};
    for(int index=1;index<=edges.Extent();++index) {
        BRepAdaptor_Curve curve(TopoDS::Edge(edges(index)));
        if(curve.GetType()==GeomAbs_Circle) {
            const auto circle=curve.Circle();
            result.arcs.push_back({vec(circle.Location()),vec(curve.Value(curve.FirstParameter())),
                vec(curve.Value(curve.LastParameter())),circle.Radius(),circle.Axis().Direction().Z()<0,
                std::abs(curve.LastParameter()-curve.FirstParameter()-2*std::acos(-1.))<1e-7});
        }else if(auto exact=capture_bspline_geometry(curve))result.curves.push_back(std::move(*exact));
        else fail();
    }
    return result;
}
FlatContour sheet_flat_contour(const BodyResult& body,double thickness,double tolerance) {
    using namespace sheet_material;
    if(body.kernel_shape.empty()||!(thickness>0)||!(tolerance>0))
        throw std::invalid_argument("Sheet DXF requires a calculated sheet body.");
    const auto shape=read_calculated_body_shape(body);
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
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> edges;TopExp::MapShapes(section.Shape(),TopAbs_EDGE,edges);
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
    // Reuse the same persisted, finite development axes as Drawing views.
    // Put each in the middle material plane, then clip it against the real
    // solid so holes and trimming gaps remain gaps in the manufacturing file.
    for(const auto& axis:body.mesh.axes) {
        if(!is_bend_line(axis.reference)||axis.display_length<=1e-9)continue;
        const auto center=sub(axis.point,mul(normal,dot(sub(axis.point,result.origin),normal)));
        const auto half=mul(unit(axis.direction),axis.display_length*.5);
        const auto a=sub(center,half),b=add(center,half);
        BRepBuilderAPI_MakeEdge line(gp_Pnt(a.x,a.y,a.z),gp_Pnt(b.x,b.y,b.z));
        BRepAlgoAPI_Common clipped(shape,line.Edge());
        if(!clipped.IsDone())throw std::runtime_error("Sheet DXF contour calculation failed.");
        for(TopExp_Explorer edge(clipped.Shape(),TopAbs_EDGE);edge.More();edge.Next()) {
            BRepAdaptor_Curve curve(TopoDS::Edge(edge.Current()));
            const auto first=local(curve.Value(curve.FirstParameter())),last=local(curve.Value(curve.LastParameter()));
            if(dot(sub(first,last),sub(first,last))>1e-14)result.bend_axes.push_back({first,last});
        }
    }
    return result;
}
}
