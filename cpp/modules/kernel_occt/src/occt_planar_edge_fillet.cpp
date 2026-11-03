#include <zima/kernel/occt_planar_edge_fillet.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRep_Tool.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>

#include <algorithm>
#include <array>
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
    std::vector<TopoDS_Face> circular_faces;
    PlanarFilletAnalyticRemovalDomain analytic_domain;
    bool adds_material{};
};

struct CutterJob {
    TopoDS_Edge edge;
    TopoDS_Face first_face;
    TopoDS_Face second_face;
    TopoDS_Solid material;
};

struct SharedEndpoint {
    TopoDS_Vertex vertex;
    std::vector<std::size_t> jobs;
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
        const TopoDS_Solid& material,
        const OcctPlanarEdgeEndFaces& end_faces) {
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
    BRepClass3d_SolidClassifier wedge_classifier(material,
        curve.Value((first_parameter+last_parameter)/2.).Translated(
            bisector*probe),tolerance);
    BRepClass3d_SolidClassifier opposite_classifier(material,
        curve.Value((first_parameter+last_parameter)/2.).Translated(
            -bisector*probe),tolerance);
    const bool removes_material=wedge_classifier.State()==TopAbs_IN&&
        opposite_classifier.State()==TopAbs_OUT;
    const bool adds_material=wedge_classifier.State()==TopAbs_OUT&&
        opposite_classifier.State()==TopAbs_IN;
    if(!removes_material&&!adds_material)
        throw std::invalid_argument(
            "Planar edge Fillet could not classify the material wedge");

    const double tangent_distance=radius/
        std::tan(interior_angle/2.);
    const double center_distance=radius/
        std::sin(interior_angle/2.);
    if(!std::isfinite(tangent_distance)||
       !std::isfinite(center_distance))
        throw std::invalid_argument(
            "Planar edge Fillet radius is invalid");
    const auto axial_extension=[&](const TopoDS_Face& face,
            const gp_Pnt& endpoint,double cross_span) {
        if(face.IsNull())return 0.;
        const BRepAdaptor_Surface support(face,true);
        if(support.GetType()!=GeomAbs_Plane)
            throw std::invalid_argument(
                "Planar edge Fillet end face must be planar");
        if(support.Plane().Distance(endpoint)>tolerance)
            throw std::invalid_argument(
                "Planar edge Fillet end face does not contain the route endpoint");
        BRepClass_FaceClassifier classifier(face,endpoint,tolerance);
        if(classifier.State()!=TopAbs_IN&&classifier.State()!=TopAbs_ON)
            throw std::invalid_argument(
                "Planar edge Fillet end face does not contain the route endpoint");
        const auto normal=support.Plane().Axis().Direction();
        const double incidence=std::abs(normal.Dot(tangent));
        if(incidence<=1.0e-8)
            throw std::invalid_argument(
                "Planar edge Fillet end face is parallel to the route");
        return cross_span/incidence+100.*tolerance;
    };
    const gp_Pnt source_first_tangent=first.Translated(
        gp_Vec(first_inward)*tangent_distance);
    const gp_Pnt source_second_tangent=first.Translated(
        gp_Vec(second_inward)*tangent_distance);
    const gp_Pnt source_center=first.Translated(
        bisector*center_distance);
    // Keep the auxiliary cutter walls outside the material wedge. Walls from
    // each tangent point straight back to the source edge would be coincident
    // with the two support faces and make the Boolean split otherwise
    // unchanged planar faces. The outside loop retains the same exact circular
    // removal boundary without introducing those artificial fragments.
    const double outside_extension=std::max(
        100.*tolerance,std::max(radius,length)*1.0e-6);
    const double cross_span=2.*(tangent_distance+radius+
        outside_extension);
    const double first_extension=axial_extension(
        end_faces.first,first,cross_span);
    const double last_extension=axial_extension(
        end_faces.last,last,cross_span);
    const double extension=std::max(first_extension,last_extension);
    const gp_Pnt origin=first.Translated(
        -gp_Vec(tangent)*first_extension);
    const gp_Pnt first_tangent=origin.Translated(
        gp_Vec(first_inward)*tangent_distance);
    const gp_Pnt second_tangent=origin.Translated(
        gp_Vec(second_inward)*tangent_distance);
    const gp_Pnt center=origin.Translated(bisector*center_distance);
    const gp_Pnt arc_middle=center.Translated(-bisector*radius);
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
    const auto circular_edge=BRepBuilderAPI_MakeEdge(arc).Edge();
    BRepBuilderAPI_MakeWire wire;
    wire.Add(circular_edge);
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
        gp_Vec(tangent)*(length+first_extension+last_extension),false,true);
    prism.Build();
    if(!prism.IsDone()||prism.Shape().IsNull()||
       !BRepCheck_Analyzer(prism.Shape()).IsValid())
        throw std::runtime_error(
            "Planar edge Fillet could not construct a valid cutter");
    TopoDS_Shape tool=prism.Shape();
    std::vector<TopoDS_Face> circular_faces;
    for(const auto& generated:prism.Generated(circular_edge))
        if(generated.ShapeType()==TopAbs_FACE)
            circular_faces.push_back(TopoDS::Face(generated));
    if(circular_faces.empty())
        throw std::runtime_error(
            "Planar edge Fillet cutter omitted its circular face");
    const auto clip=[&](const TopoDS_Face& face,const gp_Pnt& inside) {
        if(face.IsNull())return;
        const BRepAdaptor_Surface end_support(face,true);
        BRepBuilderAPI_MakeFace end_plane(end_support.Plane());
        if(!end_plane.IsDone())
            throw std::runtime_error(
                "Planar edge Fillet could not construct an end plane");
        BRepPrimAPI_MakeHalfSpace half_space(end_plane.Face(),inside);
        if(!half_space.IsDone())
            throw std::runtime_error(
                "Planar edge Fillet could not construct an end half-space");
        BRepAlgoAPI_Common common(tool,half_space.Solid());
        common.SetNonDestructive(true);
        common.SetFuzzyValue(tolerance);
        common.Build();
        if(!common.IsDone()||common.Shape().IsNull()||
           !BRepCheck_Analyzer(common.Shape()).IsValid())
            throw std::runtime_error(
                "Planar edge Fillet could not trim its selected end face");
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
        for(const auto& circular_face:circular_faces) {
            append(circular_face);
            for(const auto& descendant:common.Modified(circular_face))
                append(descendant);
            for(const auto& descendant:common.Generated(circular_face))
                append(descendant);
        }
        // OCCT's Common occasionally omits history for a trimmed prism side.
        // The exact analytic support survives, so retain every result fragment
        // that still uses the same support surface instead of guessing by
        // enumeration order.
        if(clipped_faces.empty())
            for(int index=1;index<=result_faces.Extent();++index) {
                const auto candidate=TopoDS::Face(
                    result_faces.FindKey(index));
                if(std::ranges::any_of(circular_faces,
                        [&](const auto& source) {
                            return BRep_Tool::Surface(source)==
                                BRep_Tool::Surface(candidate);
                        }))append(candidate);
            }
        if(clipped_faces.empty())
            throw std::runtime_error(
                "Planar edge Fillet trim lost its circular face");
        circular_faces=std::move(clipped_faces);
        tool=common.Shape();
    };
    const gp_Pnt interior=first.Translated(gp_Vec(tangent)*(length*.5));
    clip(end_faces.first,interior);
    clip(end_faces.last,interior);
    return {tool,std::move(circular_faces),
        {point(first),point(last),vector(tangent.XYZ()),
         vector(first_inward.XYZ()),vector(second_inward.XYZ()),
         point(source_first_tangent),point(source_second_tangent),
         point(source_center),radius,extension},adds_material};
}

} // namespace

OcctPlanarEdgeFilletResult occt_planar_edge_fillet(
        const TopoDS_Shape& input,std::span<const TopoDS_Edge> edges,
        double radius,double tolerance,
        std::span<const OcctPlanarEdgeEndFaces> end_faces) {
    if(input.IsNull()||edges.empty())
        throw std::invalid_argument(
            "Planar edge Fillet requires a Body and selected edges");
    if(!std::isfinite(radius)||radius<=0.||
       !std::isfinite(tolerance)||tolerance<=0.)
        throw std::invalid_argument(
            "Planar edge Fillet radius and tolerance must be positive");
    if(!end_faces.empty()&&end_faces.size()!=edges.size())
        throw std::invalid_argument(
            "Planar edge Fillet end faces must be parallel to selected edges");
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(input,TopAbs_EDGE,TopAbs_FACE,
        edge_faces);
    TopTools_IndexedDataMapOfShapeListOfShape edge_solids;
    TopExp::MapShapesAndAncestors(input,TopAbs_EDGE,TopAbs_SOLID,
        edge_solids);
    TopTools_IndexedMapOfShape requested_edges;
    const auto preparation_started=Clock::now();
    std::vector<CutterJob> jobs;
    std::vector<SharedEndpoint> endpoints;
    jobs.reserve(edges.size());
    for(const auto& edge:edges) {
        if(requested_edges.Contains(edge))
            throw std::invalid_argument(
                "Planar edge Fillet received the same edge more than once");
        requested_edges.Add(edge);
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
        jobs.push_back({edge,faces[0],faces[1],solids.front()});
        TopTools_IndexedMapOfShape edge_vertices;
        TopExp::MapShapes(edge,TopAbs_VERTEX,edge_vertices);
        for(int index=1;index<=edge_vertices.Extent();++index) {
            const auto vertex=TopoDS::Vertex(edge_vertices.FindKey(index));
            auto existing=std::ranges::find_if(endpoints,[&](const auto& item) {
                return item.vertex.IsSame(vertex);
            });
            if(existing==endpoints.end())
                endpoints.push_back({vertex,{jobs.size()-1}});
            else {
                existing->jobs.push_back(jobs.size()-1);
                if(existing->jobs.size()>2)
                    throw std::invalid_argument(
                        "Planar edge Fillet endpoint belongs to more than two selected fragments");
            }
        }
    }
    std::vector<Cutter> cutters;
    cutters.reserve(jobs.size());
    // Preserve authored order while preparing the analytic cutters. The
    // compatible cutters below are still submitted together to one Boolean.
    for(std::size_t index=0;index<jobs.size();++index) {
        const auto& job=jobs[index];
        cutters.push_back(make_cutter(job.edge,job.first_face,
            job.second_face,radius,tolerance,job.material,
            end_faces.empty()?OcctPlanarEdgeEndFaces{}:end_faces[index]));
    }
    // A persisted original edge may be divided into collinear current
    // fragments by later Boolean intersections. Treat the touching fragments
    // as one continuous treatment only when their exact circular profiles and
    // material sense agree at the shared vertex. This preserves the original
    // route without silently rolling around a different authored corner.
    for(const auto& endpoint:endpoints) {
        if(endpoint.jobs.size()!=2)continue;
        const auto first_index=endpoint.jobs[0];
        const auto second_index=endpoint.jobs[1];
        const BRepAdaptor_Curve first_curve(jobs[first_index].edge);
        const BRepAdaptor_Curve second_curve(jobs[second_index].edge);
        if(first_curve.GetType()!=GeomAbs_Line||
           second_curve.GetType()!=GeomAbs_Line||
           !first_curve.Line().Direction().IsParallel(
               second_curve.Line().Direction(),1.0e-9))
            throw std::invalid_argument(
                "Planar edge Fillet does not yet support selected edges sharing a non-collinear endpoint");
        if(cutters[first_index].adds_material!=
           cutters[second_index].adds_material)
            throw std::invalid_argument(
                "Planar edge Fillet fragments change material sense at their shared endpoint");
        const auto shared=BRep_Tool::Pnt(endpoint.vertex);
        const auto profile=[&](std::size_t index) {
            const auto& domain=cutters[index].analytic_domain;
            const gp_Vec offset(
                gp_Pnt(domain.source_start.x,domain.source_start.y,
                    domain.source_start.z),shared);
            const auto translated=[&](const Vec3& value) {
                return gp_Pnt(value.x,value.y,value.z).Translated(offset);
            };
            return std::array{
                translated(domain.center_at_start),
                translated(domain.first_tangent_at_start),
                translated(domain.second_tangent_at_start)};
        };
        const auto first_profile=profile(first_index);
        const auto second_profile=profile(second_index);
        const double profile_tolerance=std::max(
            100.*tolerance,1.0e-9*radius);
        const bool same_center=first_profile[0].Distance(second_profile[0])<=
            profile_tolerance;
        const bool same_tangents=(
            first_profile[1].Distance(second_profile[1])<=profile_tolerance&&
            first_profile[2].Distance(second_profile[2])<=profile_tolerance)||(
            first_profile[1].Distance(second_profile[2])<=profile_tolerance&&
            first_profile[2].Distance(second_profile[1])<=profile_tolerance);
        if(!same_center||!same_tangents)
            throw std::invalid_argument(
                "Planar edge Fillet fragments have incompatible profiles at their shared endpoint");
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

    const auto boolean_started=Clock::now();
    TopoDS_Shape current=input;
    const auto apply_boolean=[&](auto& operation,std::size_t first,
            std::size_t last) {
        NCollection_List<TopoDS_Shape> arguments,tools;
        arguments.Append(current);
        for(std::size_t index=first;index<last;++index)
            tools.Append(cutters[index].solid);
        operation.SetArguments(arguments);operation.SetTools(tools);
        operation.SetNonDestructive(true);operation.SetRunParallel(false);
        operation.SetToFillHistory(true);operation.SetFuzzyValue(tolerance);
        operation.Build();
        if(!operation.IsDone()||operation.Shape().IsNull())
            throw std::runtime_error(
                "Planar edge Fillet Boolean failed");
        auto stage_history=operation.History();
        if(!stage_history)
            throw std::runtime_error(
                "Planar edge Fillet Boolean omitted topology history");
        if(result.history)result.history->Merge(stage_history);
        else result.history=std::move(stage_history);
        current=operation.Shape();
    };
    // Preserve authored edge order. Consecutive edges with the same material
    // sense share one Boolean for the normal fast path; a mixed route changes
    // operation only where its authored CUT/FUSE sense changes.
    for(std::size_t first=0;first<cutters.size();) {
        std::size_t last=first+1;
        while(last<cutters.size()&&
              cutters[last].adds_material==cutters[first].adds_material)
            ++last;
        if(cutters[first].adds_material) {
            BRepAlgoAPI_Fuse fuse;apply_boolean(fuse,first,last);
        } else {
            BRepAlgoAPI_Cut cut;apply_boolean(cut,first,last);
        }
        first=last;
    }
    result.shape=current;
    TopTools_IndexedMapOfShape result_faces;
    TopExp::MapShapes(result.shape,TopAbs_FACE,result_faces);
    result.fillet_faces.resize(cutters.size());
    for(std::size_t index=0;index<cutters.size();++index) {
        const auto append=[&](const TopoDS_Shape& shape) {
            if(shape.ShapeType()!=TopAbs_FACE||
               !result_faces.Contains(shape))return;
            const auto face=TopoDS::Face(shape);
            if(std::ranges::none_of(result.fillet_faces[index],
                    [&](const auto& current_face) {
                        return current_face.IsSame(face);
                    }))
                result.fillet_faces[index].push_back(face);
        };
        for(const auto& circular_face:cutters[index].circular_faces) {
            append(circular_face);
            for(const auto* descendants:{
                    &result.history->Modified(circular_face),
                    &result.history->Generated(circular_face)})
                for(const auto& descendant:*descendants)append(descendant);
        }
        result.metrics.generated_fillet_faces+=
            result.fillet_faces[index].size();
        if(result.fillet_faces[index].empty())
            throw std::runtime_error(
                "Planar edge Fillet lost its circular result face ancestry");
    }
    result.metrics.boolean_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-boolean_started).count();
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
