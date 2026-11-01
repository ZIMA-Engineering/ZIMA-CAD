#include <zima/kernel/occt_planar_edge_fillet.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>

namespace zima::kernel {
namespace {

using Clock=std::chrono::steady_clock;

struct Cutter {
    TopoDS_Shape solid;
    TopoDS_Face circular_face;
    PlanarFilletAnalyticRemovalDomain analytic_domain;
};

Vec3 vector(const gp_XYZ& value) {
    return {value.X(),value.Y(),value.Z()};
}

Vec3 point(const gp_Pnt& value) {
    return vector(value.XYZ());
}

gp_Dir outward_normal(const TopoDS_Face& face) {
    const BRepAdaptor_Surface support(face,true);
    if(support.GetType()!=GeomAbs_Plane)
        throw std::invalid_argument(
            "Planar edge Fillet requires two planar supports");
    auto normal=support.Plane().Axis().Direction();
    if(!support.Plane().Position().Direct())normal.Reverse();
    if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
    return normal;
}

gp_Dir inward_direction(const TopoDS_Edge& edge,
        const TopoDS_Face& face,const gp_Dir& tangent) {
    std::optional<TopAbs_Orientation> orientation;
    for(TopExp_Explorer item(face,TopAbs_EDGE);item.More();item.Next())
        if(item.Current().IsSame(edge)) {
            const auto current=item.Current().Orientation();
            if(current!=TopAbs_FORWARD&&current!=TopAbs_REVERSED)
                throw std::invalid_argument(
                    "Planar edge Fillet support has an invalid edge orientation");
            if(orientation&&*orientation!=current)
                throw std::invalid_argument(
                    "Planar edge Fillet support uses its edge more than once");
            orientation=current;
        }
    if(!orientation)
        throw std::invalid_argument(
            "Planar edge Fillet support does not contain its edge");
    auto direction=gp_Vec(outward_normal(face)).Crossed(gp_Vec(tangent));
    if(*orientation==TopAbs_REVERSED)direction.Reverse();
    if(direction.SquareMagnitude()<=1.0e-18)
        throw std::invalid_argument(
            "Planar edge Fillet support direction is degenerate");
    return gp_Dir(direction);
}

Cutter make_cutter(const TopoDS_Edge& edge,
        const TopoDS_Face& first_face,
        const TopoDS_Face& second_face,double radius,double tolerance,
        const TopoDS_Solid& material) {
    const BRepAdaptor_Curve curve(edge);
    if(curve.GetType()!=GeomAbs_Line)
        throw std::invalid_argument(
            "Planar edge Fillet requires a straight edge");
    const double first_parameter=curve.FirstParameter();
    const double last_parameter=curve.LastParameter();
    if(!std::isfinite(first_parameter)||!std::isfinite(last_parameter)||
       last_parameter<=first_parameter)
        throw std::invalid_argument(
            "Planar edge Fillet requires a finite edge");
    const gp_Pnt first=curve.Value(first_parameter);
    const gp_Pnt last=curve.Value(last_parameter);
    gp_Vec axis(first,last);
    const double length=axis.Magnitude();
    if(!(length>tolerance))
        throw std::invalid_argument(
            "Planar edge Fillet edge is too short");
    const gp_Dir tangent(axis);
    const gp_Dir first_inward=inward_direction(
        edge,first_face,tangent);
    const gp_Dir second_inward=inward_direction(
        edge,second_face,tangent);
    const double interior_angle=first_inward.Angle(second_inward);
    if(!(interior_angle>1.0e-6&&
         interior_angle<std::numbers::pi-1.0e-6))
        throw std::invalid_argument(
            "Planar edge Fillet has no finite material wedge");
    gp_Vec bisector=gp_Vec(first_inward)+gp_Vec(second_inward);
    if(bisector.SquareMagnitude()<=1.0e-18)
        throw std::invalid_argument(
            "Planar edge Fillet material bisector is degenerate");
    bisector.Normalize();
    const double probe=std::min(radius*.1,
        std::max(100.*tolerance,length*1.0e-5));
    BRepClass3d_SolidClassifier classifier(material,
        curve.Value((first_parameter+last_parameter)/2.).Translated(
            bisector*probe),tolerance);
    if(classifier.State()!=TopAbs_IN)
        throw std::invalid_argument(
            "Planar edge Fillet currently supports convex material edges only");

    const double tangent_distance=radius/
        std::tan(interior_angle/2.);
    const double center_distance=radius/
        std::sin(interior_angle/2.);
    if(!std::isfinite(tangent_distance)||
       !std::isfinite(center_distance))
        throw std::invalid_argument(
            "Planar edge Fillet radius is invalid");
    // A treatment edge can terminate at a four-face junction. The incident
    // terminal face then trims the same cylindrical blend along an oblique
    // curve beyond the topological edge endpoint. Extend by a full radius so
    // that the input Body, rather than an artificial prism cap, owns that
    // terminal boundary. At an ordinary three-face endpoint the extension is
    // outside the Body and has no effect on the result.
    const double extension=std::max(
        radius,std::max(100.*tolerance,length*1.0e-6));
    const gp_Pnt source_first_tangent=first.Translated(
        gp_Vec(first_inward)*tangent_distance);
    const gp_Pnt source_second_tangent=first.Translated(
        gp_Vec(second_inward)*tangent_distance);
    const gp_Pnt source_center=first.Translated(
        bisector*center_distance);
    const gp_Pnt origin=first.Translated(-gp_Vec(tangent)*extension);
    const gp_Pnt first_tangent=origin.Translated(
        gp_Vec(first_inward)*tangent_distance);
    const gp_Pnt second_tangent=origin.Translated(
        gp_Vec(second_inward)*tangent_distance);
    const gp_Pnt center=origin.Translated(bisector*center_distance);
    const gp_Pnt arc_middle=center.Translated(-bisector*radius);
    // Keep the auxiliary cutter walls outside the material wedge. Walls from
    // each tangent point straight back to the source edge would be coincident
    // with the two support faces and make the Boolean split otherwise
    // unchanged planar faces. The outside loop retains the same exact circular
    // removal boundary without introducing those artificial fragments.
    const double outside_extension=std::max(
        100.*tolerance,std::max(radius,length)*1.0e-6);
    const gp_Pnt first_outer=first_tangent.Translated(
        -gp_Vec(second_inward)*outside_extension);
    const gp_Pnt outside_corner=origin.Translated(
        -(gp_Vec(first_inward)+gp_Vec(second_inward))*
            outside_extension);
    const gp_Pnt second_outer=second_tangent.Translated(
        -gp_Vec(first_inward)*outside_extension);
    const auto arc=GC_MakeArcOfCircle(first_tangent,arc_middle,
        second_tangent).Value();
    if(arc.IsNull())
        throw std::runtime_error(
            "Planar edge Fillet could not construct its circular boundary");
    BRepBuilderAPI_MakeWire wire;
    wire.Add(BRepBuilderAPI_MakeEdge(arc).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(second_tangent,second_outer).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(second_outer,outside_corner).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(outside_corner,first_outer).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(first_outer,first_tangent).Edge());
    if(!wire.IsDone())
        throw std::runtime_error(
            "Planar edge Fillet could not construct its cutter wire");
    BRepBuilderAPI_MakeFace section(wire.Wire());
    if(!section.IsDone())
        throw std::runtime_error(
            "Planar edge Fillet could not construct its cutter section");
    BRepPrimAPI_MakePrism prism(section.Face(),
        gp_Vec(tangent)*(length+2.*extension),false,true);
    prism.Build();
    if(!prism.IsDone()||prism.Shape().IsNull()||
       !BRepCheck_Analyzer(prism.Shape()).IsValid())
        throw std::runtime_error(
            "Planar edge Fillet could not construct a valid cutter");
    TopoDS_Face circular_face;
    for(TopExp_Explorer face(prism.Shape(),TopAbs_FACE);
            face.More();face.Next()) {
        const auto candidate=TopoDS::Face(face.Current());
        const BRepAdaptor_Surface support(candidate,true);
        if(support.GetType()==GeomAbs_Cylinder&&
           std::abs(support.Cylinder().Radius()-radius)<=
                std::max(tolerance,1.0e-10*radius)) {
            if(!circular_face.IsNull())
                throw std::runtime_error(
                    "Planar edge Fillet cutter has several circular faces");
            circular_face=candidate;
        }
    }
    if(circular_face.IsNull())
        throw std::runtime_error(
            "Planar edge Fillet cutter omitted its circular face");
    return {prism.Shape(),circular_face,
        {point(first),point(last),vector(tangent.XYZ()),
         vector(first_inward.XYZ()),vector(second_inward.XYZ()),
         point(source_first_tangent),point(source_second_tangent),
         point(source_center),radius,extension}};
}

} // namespace

OcctPlanarEdgeFilletResult occt_planar_edge_fillet(
        const TopoDS_Shape& input,std::span<const TopoDS_Edge> edges,
        double radius,double tolerance) {
    if(input.IsNull()||edges.empty())
        throw std::invalid_argument(
            "Planar edge Fillet requires a Body and selected edges");
    if(!std::isfinite(radius)||radius<=0.||
       !std::isfinite(tolerance)||tolerance<=0.)
        throw std::invalid_argument(
            "Planar edge Fillet radius and tolerance must be positive");
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(input,TopAbs_EDGE,TopAbs_FACE,
        edge_faces);
    TopTools_IndexedDataMapOfShapeListOfShape edge_solids;
    TopExp::MapShapesAndAncestors(input,TopAbs_EDGE,TopAbs_SOLID,
        edge_solids);
    TopTools_IndexedMapOfShape requested_edges,requested_vertices;
    const auto preparation_started=Clock::now();
    std::vector<Cutter> cutters;
    cutters.reserve(edges.size());
    for(const auto& edge:edges) {
        if(requested_edges.Contains(edge))
            throw std::invalid_argument(
                "Planar edge Fillet received the same edge more than once");
        requested_edges.Add(edge);
        TopTools_IndexedMapOfShape edge_vertices;
        TopExp::MapShapes(edge,TopAbs_VERTEX,edge_vertices);
        for(int index=1;index<=edge_vertices.Extent();++index) {
            const auto& vertex=edge_vertices.FindKey(index);
            if(requested_vertices.Contains(vertex))
                throw std::invalid_argument(
                    "Planar edge Fillet does not yet support selected edges sharing an endpoint");
            requested_vertices.Add(vertex);
        }
        const int face_index=edge_faces.FindIndex(edge);
        const int solid_index=edge_solids.FindIndex(edge);
        if(!face_index||!solid_index)
            throw std::invalid_argument(
                "Planar edge Fillet edge is absent from the input Body");
        std::vector<TopoDS_Face> faces;
        for(const auto& value:edge_faces.FindFromIndex(face_index)) {
            const auto face=TopoDS::Face(value);
            if(std::ranges::none_of(faces,[&](const auto& current) {
                    return current.IsSame(face);
                }))faces.push_back(face);
        }
        std::vector<TopoDS_Solid> solids;
        for(const auto& value:edge_solids.FindFromIndex(solid_index)) {
            const auto solid=TopoDS::Solid(value);
            if(std::ranges::none_of(solids,[&](const auto& current) {
                    return current.IsSame(solid);
                }))solids.push_back(solid);
        }
        if(faces.size()!=2||solids.size()!=1)
            throw std::invalid_argument(
                "Planar edge Fillet requires one manifold solid edge");
        cutters.push_back(make_cutter(edge,faces[0],faces[1],
            radius,tolerance,solids.front()));
    }
    OcctPlanarEdgeFilletResult result;
    result.metrics.requested_edges=edges.size();
    result.metrics.cutter_solids=cutters.size();
    result.removal_domains.reserve(cutters.size());
    result.analytic_removal_domains.reserve(cutters.size());
    for(const auto& cutter:cutters) {
        result.removal_domains.push_back(cutter.solid);
        result.analytic_removal_domains.push_back(cutter.analytic_domain);
    }
    result.metrics.preparation_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-preparation_started).count();

    BRepAlgoAPI_Cut cut;
    NCollection_List<TopoDS_Shape> arguments,tools;
    arguments.Append(input);
    for(const auto& cutter:cutters)tools.Append(cutter.solid);
    cut.SetArguments(arguments);cut.SetTools(tools);
    cut.SetNonDestructive(true);cut.SetRunParallel(false);
    cut.SetToFillHistory(true);cut.SetFuzzyValue(tolerance);
    const auto boolean_started=Clock::now();
    cut.Build();
    result.metrics.boolean_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-boolean_started).count();
    if(!cut.IsDone()||cut.Shape().IsNull())
        throw std::runtime_error(
            "Planar edge Fillet Boolean failed");
    result.shape=cut.Shape();result.history=cut.History();

    TopTools_IndexedMapOfShape result_faces;
    TopExp::MapShapes(result.shape,TopAbs_FACE,result_faces);
    result.fillet_faces.resize(cutters.size());
    for(std::size_t index=0;index<cutters.size();++index) {
        const auto append=[&](const TopoDS_Shape& shape) {
            if(shape.ShapeType()!=TopAbs_FACE||
               !result_faces.Contains(shape))return;
            const auto face=TopoDS::Face(shape);
            if(std::ranges::none_of(result.fillet_faces[index],
                    [&](const auto& current) {
                        return current.IsSame(face);
                    }))
                result.fillet_faces[index].push_back(face);
        };
        append(cutters[index].circular_face);
        for(const auto* descendants:{
                &cut.Modified(cutters[index].circular_face),
                &cut.Generated(cutters[index].circular_face)})
            for(const auto& descendant:*descendants)append(descendant);
        result.metrics.generated_fillet_faces+=
            result.fillet_faces[index].size();
        if(result.fillet_faces[index].empty())
            throw std::runtime_error(
                "Planar edge Fillet lost its circular result face ancestry");
    }
    result.source_edges.assign(edges.begin(),edges.end());
    result.generated_from_edges.resize(edges.size());
    for(std::size_t index=0;index<edges.size();++index) {
        auto& generated=result.generated_from_edges[index];
        for(const auto& shape:result.history->Generated(edges[index]))
            generated.Append(shape);
        for(const auto& face:result.fillet_faces[index])
            if(std::ranges::none_of(generated,[&](const auto& shape) {
                    return shape.IsSame(face);
                }))generated.Append(face);
    }
    const auto validation_started=Clock::now();
    if(!BRepCheck_Analyzer(result.shape,true,true,false).IsValid())
        throw std::runtime_error(
            "Planar edge Fillet produced an invalid Body");
    result.metrics.validation_ms=
        std::chrono::duration<double,std::milli>(
            Clock::now()-validation_started).count();
    return result;
}

const NCollection_List<TopoDS_Shape>& OcctPlanarEdgeFilletResult::Modified(
        const TopoDS_Shape& source) const {
    return history->Modified(source);
}

const NCollection_List<TopoDS_Shape>& OcctPlanarEdgeFilletResult::Generated(
        const TopoDS_Shape& source) const {
    for(std::size_t index=0;index<source_edges.size();++index)
        if(source_edges[index].IsSame(source))
            return generated_from_edges[index];
    return history->Generated(source);
}

bool OcctPlanarEdgeFilletResult::IsDeleted(
        const TopoDS_Shape& source) const {
    return history->IsRemoved(source);
}

} // namespace zima::kernel
