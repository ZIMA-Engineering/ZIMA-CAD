#include <zima/kernel/occt_planar_edge_chamfer.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace zima::kernel {
namespace {

struct Tool {
    TopoDS_Shape solid;
    std::vector<TopoDS_Face> treatment_faces;
    bool adds_material{};
};

gp_Dir outward_normal(const TopoDS_Face& face) {
    const BRepAdaptor_Surface support(face,true);
    if(support.GetType()!=GeomAbs_Plane)
        throw std::invalid_argument(
            "ZIMA Chamfer requires two planar supports");
    auto normal=support.Plane().Axis().Direction();
    if(!support.Plane().Position().Direct())normal.Reverse();
    if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
    return normal;
}

gp_Dir support_direction(const TopoDS_Edge& edge,
        const TopoDS_Face& face,const gp_Dir& tangent) {
    std::optional<TopAbs_Orientation> orientation;
    for(TopExp_Explorer item(face,TopAbs_EDGE);item.More();item.Next())
        if(item.Current().IsSame(edge)) {
            const auto current=item.Current().Orientation();
            if(current!=TopAbs_FORWARD&&current!=TopAbs_REVERSED)
                throw std::invalid_argument(
                    "ZIMA Chamfer support has an invalid edge orientation");
            if(orientation&&*orientation!=current)
                throw std::invalid_argument(
                    "ZIMA Chamfer support uses its edge more than once");
            orientation=current;
        }
    if(!orientation)
        throw std::invalid_argument(
            "ZIMA Chamfer support does not contain its edge");
    auto direction=gp_Vec(outward_normal(face)).Crossed(gp_Vec(tangent));
    if(*orientation==TopAbs_REVERSED)direction.Reverse();
    if(direction.SquareMagnitude()<=1.0e-18)
        throw std::invalid_argument(
            "ZIMA Chamfer support direction is degenerate");
    return gp_Dir(direction);
}

Tool make_tool(const TopoDS_Edge& edge,const TopoDS_Face& first_face,
        const TopoDS_Face& second_face,
        OcctPlanarEdgeChamferMode mode,double distance_a,
        double distance_b_or_angle,double tolerance,
        const TopoDS_Solid& material,
        const OcctPlanarEdgeEndFaces& end_faces) {
    const BRepAdaptor_Curve curve(edge);
    if(curve.GetType()!=GeomAbs_Line)
        throw std::invalid_argument(
            "ZIMA Chamfer requires a straight edge");
    const double first_parameter=curve.FirstParameter();
    const double last_parameter=curve.LastParameter();
    if(!std::isfinite(first_parameter)||!std::isfinite(last_parameter)||
       last_parameter<=first_parameter)
        throw std::invalid_argument(
            "ZIMA Chamfer requires a finite edge");
    const gp_Pnt first=curve.Value(first_parameter);
    const gp_Pnt last=curve.Value(last_parameter);
    gp_Vec axis(first,last);
    const double length=axis.Magnitude();
    if(!(length>tolerance))
        throw std::invalid_argument(
            "ZIMA Chamfer edge is too short");
    const gp_Dir tangent(axis);
    const gp_Dir first_direction=support_direction(
        edge,first_face,tangent);
    const gp_Dir second_direction=support_direction(
        edge,second_face,tangent);
    const double angle=first_direction.Angle(second_direction);
    if(!(angle>1.0e-6&&angle<std::numbers::pi-1.0e-6))
        throw std::invalid_argument(
            "ZIMA Chamfer has no finite material wedge");
    gp_Vec bisector=gp_Vec(first_direction)+gp_Vec(second_direction);
    if(bisector.SquareMagnitude()<=1.0e-18)
        throw std::invalid_argument(
            "ZIMA Chamfer material bisector is degenerate");
    bisector.Normalize();
    double distance_b=distance_a;
    if(mode==OcctPlanarEdgeChamferMode::TwoDistances) {
        distance_b=distance_b_or_angle;
    } else if(mode==OcctPlanarEdgeChamferMode::DistanceAngle) {
        // In the plane perpendicular to the edge, the support rays and the
        // chamfer line form one triangle. A is measured on first_face and the
        // authored angle is between that support ray and the chamfer line.
        // The sine rule yields B; for perpendicular faces this reduces to
        // B=A*tan(angle).
        const double denominator=std::sin(angle+distance_b_or_angle);
        if(!(denominator>1.0e-12))
            throw std::invalid_argument(
                "ZIMA Chamfer angle does not intersect the second support");
        distance_b=distance_a*std::sin(distance_b_or_angle)/denominator;
    }
    if(!std::isfinite(distance_b)||distance_b<=0.)
        throw std::invalid_argument(
            "ZIMA Chamfer calculated an invalid second distance");
    const double probe=std::min(std::min(distance_a,distance_b)*.1,
        std::max(100.*tolerance,length*1.0e-5));
    const auto middle=curve.Value((first_parameter+last_parameter)/2.);
    BRepClass3d_SolidClassifier wedge(material,
        middle.Translated(bisector*probe),tolerance);
    BRepClass3d_SolidClassifier opposite(material,
        middle.Translated(-bisector*probe),tolerance);
    const bool removes=wedge.State()==TopAbs_IN&&
        opposite.State()==TopAbs_OUT;
    const bool adds=wedge.State()==TopAbs_OUT&&
        opposite.State()==TopAbs_IN;
    if(!removes&&!adds)
        throw std::invalid_argument(
            "ZIMA Chamfer could not classify the material wedge");

    const auto axial_extension=[&](const TopoDS_Face& face,
            const gp_Pnt& endpoint,double cross_span) {
        if(face.IsNull())return 0.;
        const BRepAdaptor_Surface support(face,true);
        if(support.GetType()!=GeomAbs_Plane)
            throw std::invalid_argument(
                "ZIMA Chamfer end face must be planar");
        if(support.Plane().Distance(endpoint)>tolerance)
            throw std::invalid_argument(
                "ZIMA Chamfer end face does not contain the route endpoint");
        BRepClass_FaceClassifier classifier(face,endpoint,tolerance);
        if(classifier.State()!=TopAbs_IN&&classifier.State()!=TopAbs_ON)
            throw std::invalid_argument(
                "ZIMA Chamfer end face does not contain the route endpoint");
        const double incidence=std::abs(
            support.Plane().Axis().Direction().Dot(tangent));
        if(incidence<=1.0e-8)
            throw std::invalid_argument(
                "ZIMA Chamfer end face is parallel to the route");
        return cross_span/incidence+100.*tolerance;
    };
    // The exact Chamfer profile is one triangle: the authored treatment line
    // and the two support-face segments meeting at the source edge. Closing
    // it outside the corner creates unnecessary coplanar seams and can retain
    // the auxiliary overlap as visible material for an additive Chamfer.
    const double cross_span=2.*(distance_a+distance_b);
    const double first_extension=axial_extension(
        end_faces.first,first,cross_span);
    const double last_extension=axial_extension(
        end_faces.last,last,cross_span);
    const gp_Pnt origin=first.Translated(
        -gp_Vec(tangent)*first_extension);
    const gp_Pnt first_tangent=origin.Translated(
        gp_Vec(first_direction)*distance_a);
    const gp_Pnt second_tangent=origin.Translated(
        gp_Vec(second_direction)*distance_b);
    const auto treatment_edge=
        BRepBuilderAPI_MakeEdge(first_tangent,second_tangent).Edge();
    BRepBuilderAPI_MakeWire wire;
    wire.Add(treatment_edge);
    wire.Add(BRepBuilderAPI_MakeEdge(second_tangent,origin).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(origin,first_tangent).Edge());
    if(!wire.IsDone())
        throw std::runtime_error(
            "ZIMA Chamfer could not construct its tool wire");
    BRepBuilderAPI_MakeFace section(wire.Wire());
    if(!section.IsDone())
        throw std::runtime_error(
            "ZIMA Chamfer could not construct its tool section");
    BRepPrimAPI_MakePrism prism(section.Face(),
        gp_Vec(tangent)*(length+first_extension+last_extension),false,true);
    prism.Build();
    if(!prism.IsDone()||prism.Shape().IsNull()||
       !BRepCheck_Analyzer(prism.Shape()).IsValid())
        throw std::runtime_error(
            "ZIMA Chamfer could not construct a valid tool");
    TopoDS_Shape tool=prism.Shape();
    std::vector<TopoDS_Face> treatment_faces;
    for(const auto& generated:prism.Generated(treatment_edge))
        if(generated.ShapeType()==TopAbs_FACE)
            treatment_faces.push_back(TopoDS::Face(generated));
    if(treatment_faces.empty())
        throw std::runtime_error(
            "ZIMA Chamfer tool omitted its treatment face");
    const auto clip=[&](const TopoDS_Face& face,const gp_Pnt& inside) {
        if(face.IsNull())return;
        const BRepAdaptor_Surface end_support(face,true);
        BRepBuilderAPI_MakeFace end_plane(end_support.Plane());
        if(!end_plane.IsDone())
            throw std::runtime_error(
                "ZIMA Chamfer could not construct an end plane");
        BRepPrimAPI_MakeHalfSpace half_space(end_plane.Face(),inside);
        if(!half_space.IsDone())
            throw std::runtime_error(
                "ZIMA Chamfer could not construct an end half-space");
        BRepAlgoAPI_Common common(tool,half_space.Solid());
        common.SetNonDestructive(true);
        common.SetFuzzyValue(tolerance);
        common.Build();
        if(!common.IsDone()||common.Shape().IsNull()||
           !BRepCheck_Analyzer(common.Shape()).IsValid())
            throw std::runtime_error(
                "ZIMA Chamfer could not trim its selected end face");
        TopTools_IndexedMapOfShape result_faces;
        TopExp::MapShapes(common.Shape(),TopAbs_FACE,result_faces);
        std::vector<TopoDS_Face> clipped_faces;
        const auto append=[&](const TopoDS_Shape& shape) {
            if(shape.ShapeType()!=TopAbs_FACE||
               !result_faces.Contains(shape))return;
            const auto candidate=TopoDS::Face(shape);
            if(std::ranges::none_of(clipped_faces,[&](const auto& current) {
                    return current.IsSame(candidate);
                }))clipped_faces.push_back(candidate);
        };
        for(const auto& treatment_face:treatment_faces) {
            append(treatment_face);
            for(const auto& descendant:common.Modified(treatment_face))
                append(descendant);
            for(const auto& descendant:common.Generated(treatment_face))
                append(descendant);
        }
        // Preserve the exact prism-side support if OCCT omits Common history
        // for an angled trim. All fragments sharing that analytic surface are
        // descendants of this authored treatment face.
        if(clipped_faces.empty())
            for(int index=1;index<=result_faces.Extent();++index) {
                const auto candidate=TopoDS::Face(
                    result_faces.FindKey(index));
                if(std::ranges::any_of(treatment_faces,
                        [&](const auto& source) {
                            return BRep_Tool::Surface(source)==
                                BRep_Tool::Surface(candidate);
                        }))append(candidate);
            }
        if(clipped_faces.empty())
            throw std::runtime_error(
                "ZIMA Chamfer trim lost its treatment face");
        treatment_faces=std::move(clipped_faces);
        tool=common.Shape();
    };
    const gp_Pnt interior=first.Translated(gp_Vec(tangent)*(length*.5));
    clip(end_faces.first,interior);
    clip(end_faces.last,interior);
    return {tool,std::move(treatment_faces),adds};
}

} // namespace

OcctPlanarEdgeChamferResult occt_planar_edge_chamfer(
        const TopoDS_Shape& input,
        std::span<const OcctPlanarEdgeChamferSelection> selections,
        OcctPlanarEdgeChamferMode mode,double distance_a,
        double distance_b_or_angle,double tolerance,
        std::span<const OcctPlanarEdgeEndFaces> end_faces,
        std::span<const OcctPlanarEdgeSupport> supports) {
    if(input.IsNull()||selections.empty())
        throw std::invalid_argument(
            "ZIMA Chamfer requires a Body and selected edges");
    if(!std::isfinite(distance_a)||distance_a<=0.||
       (mode==OcctPlanarEdgeChamferMode::TwoDistances&&
        (!std::isfinite(distance_b_or_angle)||
         distance_b_or_angle<=0.))||
       (mode==OcctPlanarEdgeChamferMode::DistanceAngle&&
        (!std::isfinite(distance_b_or_angle)||
         distance_b_or_angle<=0.||
         distance_b_or_angle>=std::numbers::pi))||
       !std::isfinite(tolerance)||tolerance<=0.)
        throw std::invalid_argument(
            "ZIMA Chamfer distances, angle and tolerance are invalid");
    if(!end_faces.empty()&&end_faces.size()!=selections.size())
        throw std::invalid_argument(
            "ZIMA Chamfer end faces must be parallel to selected edges");
    if(!supports.empty()&&supports.size()!=selections.size())
        throw std::invalid_argument(
            "ZIMA Chamfer supports must be parallel to selected edges");
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces,edge_solids;
    TopExp::MapShapesAndAncestors(input,TopAbs_EDGE,TopAbs_FACE,edge_faces);
    TopExp::MapShapesAndAncestors(input,TopAbs_EDGE,TopAbs_SOLID,edge_solids);
    TopTools_IndexedMapOfShape requested_edges,requested_vertices;
    std::vector<Tool> tools;
    for(std::size_t selection_index=0;
            selection_index<selections.size();++selection_index) {
        const auto& selection=selections[selection_index];
        const auto& edge=selection.edge;
        if(requested_edges.Contains(edge))
            throw std::invalid_argument(
                "ZIMA Chamfer received the same edge more than once");
        requested_edges.Add(edge);
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(edge,TopAbs_VERTEX,vertices);
        for(int index=1;index<=vertices.Extent();++index) {
            const auto& vertex=vertices.FindKey(index);
            if(requested_vertices.Contains(vertex))
                throw std::invalid_argument(
                    "ZIMA Chamfer does not yet support selected edges sharing an endpoint");
            requested_vertices.Add(vertex);
        }
        std::vector<TopoDS_Face> faces;
        std::vector<TopoDS_Solid> solids;
        const bool exact_source=!supports.empty()&&
            !supports[selection_index].material.IsNull();
        if(exact_source) {
            const auto& support=supports[selection_index];
            if(support.first.IsNull()||support.second.IsNull()||
               support.first.IsSame(support.second))
                throw std::invalid_argument(
                    "ZIMA Chamfer original support is invalid");
            TopTools_IndexedMapOfShape first_edges,second_edges,
                material_edges;
            TopExp::MapShapes(support.first,TopAbs_EDGE,first_edges);
            TopExp::MapShapes(support.second,TopAbs_EDGE,second_edges);
            TopExp::MapShapes(support.material,TopAbs_EDGE,material_edges);
            if(!first_edges.Contains(edge)||!second_edges.Contains(edge)||
               !material_edges.Contains(edge))
                throw std::invalid_argument(
                    "ZIMA Chamfer original support does not contain its edge");
            faces={support.first,support.second};
            solids={support.material};
        } else {
            const int face_index=edge_faces.FindIndex(edge);
            const int solid_index=edge_solids.FindIndex(edge);
            if(!face_index||!solid_index)
                throw std::invalid_argument(
                    "ZIMA Chamfer edge is absent from the input Body");
            for(const auto& value:edge_faces.FindFromIndex(face_index)) {
                const auto face=TopoDS::Face(value);
                if(std::ranges::none_of(faces,[&](const auto& current) {
                        return current.IsSame(face);}))faces.push_back(face);
            }
            for(const auto& value:edge_solids.FindFromIndex(solid_index)) {
                const auto solid=TopoDS::Solid(value);
                if(std::ranges::none_of(solids,[&](const auto& current) {
                        return current.IsSame(solid);}))solids.push_back(solid);
            }
        }
        if(faces.size()!=2||solids.size()!=1)
            throw std::invalid_argument(
                "ZIMA Chamfer requires one manifold solid edge");
        if(mode!=OcctPlanarEdgeChamferMode::EqualDistance) {
            if(selection.distance_a_face.IsNull())
                throw std::invalid_argument(
                    "ZIMA Chamfer requires the stable A support face");
            if(selection.distance_a_face.IsSame(faces[1]))
                std::swap(faces[0],faces[1]);
            else if(!selection.distance_a_face.IsSame(faces[0]))
                throw std::invalid_argument(
                    "ZIMA Chamfer A support is not adjacent to its edge");
        }
        auto tool=make_tool(edge,faces[0],faces[1],mode,distance_a,
            distance_b_or_angle,tolerance,
            solids.front(),end_faces.empty()?OcctPlanarEdgeEndFaces{}:
                end_faces[tools.size()]);
        tools.push_back(std::move(tool));
    }

    OcctPlanarEdgeChamferResult result;
    result.treatment_domains.reserve(tools.size());
    for(const auto& tool:tools)result.treatment_domains.push_back(tool.solid);
    TopoDS_Shape current=input;
    const auto apply=[&](auto& operation,std::size_t first,
            std::size_t last) {
        NCollection_List<TopoDS_Shape> arguments,boolean_tools;
        arguments.Append(current);
        for(std::size_t index=first;index<last;++index)
            boolean_tools.Append(tools[index].solid);
        operation.SetArguments(arguments);operation.SetTools(boolean_tools);
        operation.SetNonDestructive(true);operation.SetRunParallel(false);
        operation.SetToFillHistory(true);operation.SetFuzzyValue(tolerance);
        operation.Build();
        if(!operation.IsDone()||operation.Shape().IsNull())
            throw std::runtime_error("ZIMA Chamfer Boolean failed");
        auto stage_history=operation.History();
        if(!stage_history)
            throw std::runtime_error(
                "ZIMA Chamfer Boolean omitted topology history");
        if(result.history)result.history->Merge(stage_history);
        else result.history=std::move(stage_history);
        current=operation.Shape();
    };
    // Preserve authored edge order while batching consecutive tools with the
    // same material sense. This admits useful mixed outer/inner selections
    // without weakening final Body validation or reference history.
    for(std::size_t first=0;first<tools.size();) {
        std::size_t last=first+1;
        while(last<tools.size()&&
              tools[last].adds_material==tools[first].adds_material)
            ++last;
        if(tools[first].adds_material) {
            BRepAlgoAPI_Fuse fuse;apply(fuse,first,last);
        } else {
            BRepAlgoAPI_Cut cut;apply(cut,first,last);
        }
        first=last;
    }
    // The two support sides of the exact triangular tool lie on the original
    // planes. OCCT may retain their end sections as coplanar Boolean seams.
    // Remove only those redundant new seams while protecting surviving input
    // boundaries and their non-coplanar descendants.
    ShapeUpgrade_UnifySameDomain cleanup(current,true,true,false);
    cleanup.SetLinearTolerance(tolerance);
    cleanup.SetAngularTolerance(1.0e-9);
    TopTools_IndexedMapOfShape current_edges,input_edges;
    TopExp::MapShapes(current,TopAbs_EDGE,current_edges);
    TopExp::MapShapes(input,TopAbs_EDGE,input_edges);
    TopTools_IndexedDataMapOfShapeListOfShape current_edge_faces;
    TopExp::MapShapesAndAncestors(current,TopAbs_EDGE,TopAbs_FACE,
        current_edge_faces);
    const auto redundant_coplanar_seam=[&](const TopoDS_Shape& edge) {
        const int edge_index=current_edge_faces.FindIndex(edge);
        if(!edge_index)return false;
        const auto& adjacent=current_edge_faces.FindFromIndex(edge_index);
        if(adjacent.Extent()!=2)return false;
        auto iterator=adjacent.cbegin();
        const BRepAdaptor_Surface first_surface(
            TopoDS::Face(*iterator),true);
        ++iterator;
        const BRepAdaptor_Surface second_surface(
            TopoDS::Face(*iterator),true);
        return first_surface.GetType()==GeomAbs_Plane&&
            second_surface.GetType()==GeomAbs_Plane&&
            first_surface.Plane().Axis().Direction().IsParallel(
                second_surface.Plane().Axis().Direction(),1.0e-9)&&
            first_surface.Plane().Distance(
                second_surface.Plane().Location())<=tolerance;
    };
    // Keep all current non-seam boundaries, including unchanged disconnected
    // split descendants that OCCT does not necessarily list in the Boolean
    // Modified/Generated history.
    for(int index=1;index<=current_edges.Extent();++index) {
        const auto& edge=current_edges.FindKey(index);
        if(!redundant_coplanar_seam(edge)) {
            cleanup.KeepShape(edge);
            continue;
        }
        const bool existing_boundary=std::ranges::any_of(
            std::views::iota(1,input_edges.Extent()+1),
            [&](int input_index) {
                return BRepTools::Compare(TopoDS::Edge(edge),
                    TopoDS::Edge(input_edges.FindKey(input_index)));
            });
        if(existing_boundary)cleanup.KeepShape(edge);
    }
    for(int index=1;index<=input_edges.Extent();++index) {
        const auto& source=input_edges.FindKey(index);
        if(current_edges.Contains(source))cleanup.KeepShape(source);
        for(const auto* descendants:{&result.history->Modified(source),
                &result.history->Generated(source)})
            for(const auto& descendant:*descendants)
                if(descendant.ShapeType()==TopAbs_EDGE&&
                   current_edges.Contains(descendant)&&
                   !redundant_coplanar_seam(descendant))
                    cleanup.KeepShape(descendant);
    }
    cleanup.Build();
    if(cleanup.Shape().IsNull())
        throw std::runtime_error(
            "ZIMA Chamfer could not remove auxiliary result seams");
    if(const auto cleanup_history=cleanup.History();
            !cleanup_history.IsNull())
        result.history->Merge(cleanup_history);
    current=cleanup.Shape();
    result.shape=current;
    TopTools_IndexedMapOfShape result_faces;
    TopExp::MapShapes(result.shape,TopAbs_FACE,result_faces);
    result.chamfer_faces.resize(tools.size());
    for(std::size_t index=0;index<tools.size();++index) {
        const auto append=[&](const TopoDS_Shape& shape) {
            if(shape.ShapeType()!=TopAbs_FACE||
               !result_faces.Contains(shape))return;
            const auto face=TopoDS::Face(shape);
            if(std::ranges::none_of(result.chamfer_faces[index],
                    [&](const auto& current_face) {
                        return current_face.IsSame(face);}))
                result.chamfer_faces[index].push_back(face);
        };
        for(const auto& treatment_face:tools[index].treatment_faces) {
            append(treatment_face);
            for(const auto* descendants:{
                    &result.history->Modified(treatment_face),
                    &result.history->Generated(treatment_face)})
                for(const auto& descendant:*descendants)append(descendant);
        }
        if(result.chamfer_faces[index].empty())
            throw std::runtime_error(
                "ZIMA Chamfer lost its result-face ancestry");
    }
    if(!BRepCheck_Analyzer(result.shape,true,true,false).IsValid())
        throw std::runtime_error(
            "ZIMA Chamfer produced an invalid Body");
    result.source_edges.reserve(selections.size());
    for(const auto& selection:selections)
        result.source_edges.push_back(selection.edge);
    result.generated_from_edges.resize(selections.size());
    for(std::size_t index=0;index<selections.size();++index) {
        auto& generated=result.generated_from_edges[index];
        for(const auto& shape:result.history->Generated(
                selections[index].edge))
            generated.Append(shape);
        for(const auto& face:result.chamfer_faces[index])
            if(std::ranges::none_of(generated,[&](const auto& shape) {
                    return shape.IsSame(face);}))generated.Append(face);
    }
    return result;
}

OcctPlanarEdgeChamferResult occt_planar_edge_chamfer(
        const TopoDS_Shape& input,std::span<const TopoDS_Edge> edges,
        double distance,double tolerance,
        std::span<const OcctPlanarEdgeEndFaces> end_faces,
        std::span<const OcctPlanarEdgeSupport> supports) {
    std::vector<OcctPlanarEdgeChamferSelection> selections;
    selections.reserve(edges.size());
    for(const auto& edge:edges)selections.push_back({edge,{}});
    return occt_planar_edge_chamfer(input,selections,
        OcctPlanarEdgeChamferMode::EqualDistance,distance,distance,
        tolerance,end_faces,supports);
}

const NCollection_List<TopoDS_Shape>& OcctPlanarEdgeChamferResult::Modified(
        const TopoDS_Shape& source) const {
    return history->Modified(source);
}

const NCollection_List<TopoDS_Shape>& OcctPlanarEdgeChamferResult::Generated(
        const TopoDS_Shape& source) const {
    for(std::size_t index=0;index<source_edges.size();++index)
        if(source_edges[index].IsSame(source))
            return generated_from_edges[index];
    return history->Generated(source);
}

bool OcctPlanarEdgeChamferResult::IsDeleted(
        const TopoDS_Shape& source) const {
    return history->IsRemoved(source);
}

} // namespace zima::kernel
