#include <zima/kernel/occt_boundary_intersection_graph.hpp>

#include <BRepAlgoAPI_Section.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <GeomProjLib.hxx>
#include <Geom_Line.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <IntTools_Context.hxx>
#include <IntTools_FClass2d.hxx>
#include <IntTools_FaceFace.hxx>
#include <TopAbs_State.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Lin.hxx>
#include <gp_Vec2d.hxx>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>

namespace zima::kernel {
namespace {

using Clock=std::chrono::steady_clock;

struct BoundedFace {
    const OcctBoundaryFace* source{};
    Bnd_Box bounds;
};

std::vector<double> trim_parameters(const TopoDS_Face& face,
        const occ::handle<Geom2d_Curve>& pcurve,
        double first,double last,double tolerance) {
    std::vector<double> parameters{first,last};
    if(pcurve.IsNull()||last<=first)return parameters;
    occ::handle<Geom2d_Curve> trimmed=
        new Geom2d_TrimmedCurve(pcurve,first,last);
    for(TopExp_Explorer edge(face,TopAbs_EDGE);edge.More();edge.Next()) {
        double edge_first{},edge_last{};
        auto edge_curve=BRep_Tool::CurveOnSurface(
            TopoDS::Edge(edge.Current()),face,edge_first,edge_last);
        if(edge_curve.IsNull()||edge_last<=edge_first)continue;
        occ::handle<Geom2d_Curve> edge_trimmed=
            new Geom2d_TrimmedCurve(edge_curve,edge_first,edge_last);
        Geom2dAPI_InterCurveCurve intersection(
            trimmed,edge_trimmed,std::max(1.0e-7,tolerance));
        const auto& result=intersection.Intersector();
        if(!result.IsDone())continue;
        const auto append=[&](double parameter) {
            if(parameter>=first-1.0e-9&&parameter<=last+1.0e-9)
                parameters.push_back(std::clamp(parameter,first,last));
        };
        for(int point=1;point<=result.NbPoints();++point)
            append(result.Point(point).ParamOnFirst());
        for(int segment=1;segment<=result.NbSegments();++segment) {
            const auto& overlap=result.Segment(segment);
            if(overlap.HasFirstPoint())
                append(overlap.FirstPoint().ParamOnFirst());
            if(overlap.HasLastPoint())
                append(overlap.LastPoint().ParamOnFirst());
        }
    }
    std::ranges::sort(parameters);
    const auto removed=std::ranges::unique(parameters,[](double left,
            double right) {
        return std::abs(left-right)<=1.0e-9*
            std::max({1.,std::abs(left),std::abs(right)});
    });
    parameters.erase(removed.begin(),parameters.end());
    return parameters;
}

void require_face(const OcctBoundaryFace& face) {
    if(face.shape.IsNull()||face.references.empty()||
       std::ranges::any_of(face.references,[](const auto& reference) {
           return !reference.valid()||!reference.instance_path.empty();
       }))
        throw std::invalid_argument(
            "Boundary graph faces require local stable references");
}

std::optional<IntTools_Curve> tangent_plane_cylinder_curve(
        const TopoDS_Face& first,const TopoDS_Face& second,
        double tolerance) {
    const BRepAdaptor_Surface first_surface(first,true);
    const BRepAdaptor_Surface second_surface(second,true);
    const bool first_is_plane=first_surface.GetType()==GeomAbs_Plane;
    const bool second_is_plane=second_surface.GetType()==GeomAbs_Plane;
    const bool first_is_cylinder=first_surface.GetType()==GeomAbs_Cylinder;
    const bool second_is_cylinder=second_surface.GetType()==GeomAbs_Cylinder;
    if(!((first_is_plane&&second_is_cylinder)||
         (second_is_plane&&first_is_cylinder)))return {};
    const auto plane=first_is_plane
        ?first_surface.Plane():second_surface.Plane();
    const auto cylinder=first_is_cylinder
        ?first_surface.Cylinder():second_surface.Cylinder();
    const auto normal=plane.Axis().Direction();
    const auto axis=cylinder.Axis().Direction();
    if(std::abs(normal.Dot(axis))>1.0e-10)return {};
    const double signed_distance=gp_Vec(
        plane.Location(),cylinder.Location()).Dot(gp_Vec(normal));
    if(std::abs(std::abs(signed_distance)-cylinder.Radius())>
            std::max(tolerance,1.0e-10*cylinder.Radius()))return {};
    const auto origin=cylinder.Location().Translated(
        -gp_Vec(normal)*signed_distance);
    double parameter_min=std::numeric_limits<double>::infinity();
    double parameter_max=-std::numeric_limits<double>::infinity();
    const auto include_vertices=[&](const TopoDS_Face& face) {
        for(TopExp_Explorer vertex(face,TopAbs_VERTEX);vertex.More();
                vertex.Next()) {
            const double parameter=gp_Vec(origin,
                BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current()))).Dot(
                    gp_Vec(axis));
            parameter_min=std::min(parameter_min,parameter);
            parameter_max=std::max(parameter_max,parameter);
        }
    };
    include_vertices(first);include_vertices(second);
    if(!std::isfinite(parameter_min)||!std::isfinite(parameter_max)||
       parameter_max-parameter_min<=tolerance)return {};
    const double extension=std::max(1.0e-6,10.*tolerance);
    parameter_min-=extension;parameter_max+=extension;
    occ::handle<Geom_Line> line=new Geom_Line(gp_Lin(origin,axis));
    occ::handle<Geom_Curve> curve=new Geom_TrimmedCurve(
        line,parameter_min,parameter_max);
    double first_tolerance=tolerance,second_tolerance=tolerance;
    auto first_pcurve=GeomProjLib::Curve2d(line,
        parameter_min,parameter_max,BRep_Tool::Surface(first),
        first_tolerance);
    auto second_pcurve=GeomProjLib::Curve2d(line,
        parameter_min,parameter_max,BRep_Tool::Surface(second),
        second_tolerance);
    if(first_pcurve.IsNull()||second_pcurve.IsNull())return {};
    const auto normalize_cylinder_parameter=[&](const TopoDS_Face& face,
            const BRepAdaptor_Surface& surface,
            const occ::handle<Geom2d_Curve>& pcurve) {
        if(surface.GetType()!=GeomAbs_Cylinder)return;
        double u_min{},u_max{},v_min{},v_max{};
        BRepTools::UVBounds(face,u_min,u_max,v_min,v_max);
        const double middle=(parameter_min+parameter_max)/2.;
        const double u=pcurve->Value(middle).X();
        const double shift=std::round(((u_min+u_max)/2.-u)/
            (2.*std::numbers::pi))*(2.*std::numbers::pi);
        if(std::abs(shift)>1.0e-12)
            pcurve->Translate(gp_Vec2d(shift,0.));
    };
    normalize_cylinder_parameter(first,first_surface,first_pcurve);
    normalize_cylinder_parameter(second,second_surface,second_pcurve);
    return IntTools_Curve(curve,first_pcurve,second_pcurve,
        std::max({tolerance,first_tolerance,second_tolerance}),tolerance);
}

} // namespace

OcctBoundaryIntersectionGraph occt_boundary_intersection_graph(
        std::span<const OcctBoundaryFace> first,
        std::span<const OcctBoundaryFace> second,double tolerance) {
    if(!std::isfinite(tolerance)||tolerance<=0.)
        throw std::invalid_argument(
            "Boundary graph tolerance must be finite and positive");
    for(const auto& face:first)require_face(face);
    for(const auto& face:second)require_face(face);

    OcctBoundaryIntersectionGraph result;
    result.metrics.possible_pairs=first.size()*second.size();
    const auto bounds_start=Clock::now();
    const auto bounded=[&](std::span<const OcctBoundaryFace> faces) {
        std::vector<BoundedFace> values;
        values.reserve(faces.size());
        for(const auto& face:faces) {
            BoundedFace value{&face,{}};
            BRepBndLib::Add(face.shape,value.bounds,true);
            // A tangential contact may be separated by the independent
            // triangulation bounds by a few ulps even when the exact surfaces
            // meet. Keep the broad phase conservative; exact face/face tests
            // still reject non-intersections.
            value.bounds.Enlarge(std::max(1.0e-6,2.*tolerance));
            values.push_back(std::move(value));
        }
        return values;
    };
    const auto first_faces=bounded(first);
    const auto second_faces=bounded(second);
    result.metrics.bounds_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-bounds_start).count();

    std::vector<std::pair<const BoundedFace*,const BoundedFace*>> candidates;
    for(const auto& left:first_faces)
        for(const auto& right:second_faces)
            if(!left.bounds.IsOut(right.bounds))
                candidates.emplace_back(&left,&right);
    result.metrics.candidate_pairs=candidates.size();

    struct SurfaceCurve {
        IntTools_Curve curve;
        const OcctBoundaryFace* first{};
        const OcctBoundaryFace* second{};
    };
    std::vector<SurfaceCurve> surface_curves;
    std::vector<std::pair<const OcctBoundaryFace*,const OcctBoundaryFace*>>
        tangent_pairs;
    occ::handle<IntTools_Context> context=new IntTools_Context;
    const auto intersection_start=Clock::now();
    for(const auto& [left,right]:candidates) {
        if(auto analytic=tangent_plane_cylinder_curve(
                left->source->shape,right->source->shape,tolerance)) {
            surface_curves.push_back(
                {std::move(*analytic),left->source,right->source});
            continue;
        }
        IntTools_FaceFace intersection;
        intersection.SetContext(context);
        intersection.SetFuzzyValue(tolerance);
        intersection.SetParameters(true,true,true,tolerance);
        intersection.Perform(left->source->shape,right->source->shape,false);
        if(!intersection.IsDone())
            throw std::runtime_error("Boundary face intersection failed");
        if(intersection.TangentFaces()) {
            tangent_pairs.emplace_back(left->source,right->source);
            continue;
        }
        for(const auto& curve:intersection.Lines()) {
            if(curve.Curve().IsNull()||curve.FirstCurve2d().IsNull()||
               curve.SecondCurve2d().IsNull())
                throw std::runtime_error(
                    "Boundary face intersection omitted a required PCurve");
            surface_curves.push_back({curve,left->source,right->source});
        }
    }
    result.metrics.intersection_ms=
        std::chrono::duration<double,std::milli>(
            Clock::now()-intersection_start).count();
    result.metrics.transverse_surface_curves=surface_curves.size();
    result.metrics.tangent_pairs=tangent_pairs.size();

    const auto trim_start=Clock::now();
    for(const auto& entry:surface_curves) {
        double first_parameter{},last_parameter{};
        gp_Pnt first_point,last_point;
        if(!entry.curve.Bounds(first_parameter,last_parameter,
                first_point,last_point)||last_parameter<=first_parameter)
            continue;
        auto parameters=trim_parameters(entry.first->shape,
            entry.curve.FirstCurve2d(),first_parameter,last_parameter,
            tolerance);
        const auto second_parameters=trim_parameters(entry.second->shape,
            entry.curve.SecondCurve2d(),first_parameter,last_parameter,
            tolerance);
        parameters.insert(parameters.end(),second_parameters.begin(),
            second_parameters.end());
        std::ranges::sort(parameters);
        const auto removed=std::ranges::unique(parameters,[](double left,
                double right) {
            return std::abs(left-right)<=1.0e-9*
                std::max({1.,std::abs(left),std::abs(right)});
        });
        parameters.erase(removed.begin(),parameters.end());
        auto& first_classifier=context->FClass2d(entry.first->shape);
        auto& second_classifier=context->FClass2d(entry.second->shape);
        for(std::size_t interval=1;interval<parameters.size();++interval) {
            const double begin=parameters[interval-1];
            const double end=parameters[interval];
            if(end<=begin)continue;
            const double middle=(begin+end)/2.;
            if(first_classifier.Perform(
                    entry.curve.FirstCurve2d()->Value(middle))==TopAbs_OUT||
               second_classifier.Perform(
                    entry.curve.SecondCurve2d()->Value(middle))==TopAbs_OUT)
                continue;
            result.curves.push_back({entry.curve.Curve(),
                entry.curve.FirstCurve2d(),entry.curve.SecondCurve2d(),
                begin,end,entry.curve.Tolerance(),
                OcctBoundaryCurveKind::Transverse,
                entry.first->shape,entry.second->shape,
                entry.first->references,entry.second->references,
                entry.first->history_index,entry.second->history_index});
            ++result.metrics.retained_transverse_intervals;
        }
    }
    result.metrics.trim_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-trim_start).count();

    const auto coplanar_start=Clock::now();
    for(const auto& [left,right]:tangent_pairs) {
        // Temporary exact oracle for coplanar wire overlap. It is isolated to
        // one face pair and never constructs a body Boolean; the next step is
        // replacing it with direct 2D wire overlay using the same output type.
        BRepAlgoAPI_Section section(left->shape,right->shape,false);
        section.SetNonDestructive(true);section.SetRunParallel(false);
        section.SetFuzzyValue(tolerance);section.Approximation(true);
        section.ComputePCurveOn1(true);section.ComputePCurveOn2(true);
        section.Build();
        if(!section.IsDone())
            throw std::runtime_error("Coplanar boundary intersection failed");
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(section.Shape(),TopAbs_EDGE,edges);
        for(int index=1;index<=edges.Extent();++index) {
            const auto edge=TopoDS::Edge(edges(index));
            double first_parameter{},last_parameter{};
            const auto curve=BRep_Tool::Curve(
                edge,first_parameter,last_parameter);
            double first_pcurve_parameter{},last_pcurve_parameter{};
            auto first_pcurve=BRep_Tool::CurveOnSurface(edge,left->shape,
                first_pcurve_parameter,last_pcurve_parameter);
            auto second_pcurve=BRep_Tool::CurveOnSurface(edge,right->shape,
                first_pcurve_parameter,last_pcurve_parameter);
            if(curve.IsNull()||first_pcurve.IsNull()||second_pcurve.IsNull()||
               last_parameter<=first_parameter)
                throw std::runtime_error(
                    "Coplanar boundary intersection omitted a required PCurve");
            result.curves.push_back({curve,first_pcurve,second_pcurve,
                first_parameter,last_parameter,
                std::max(BRep_Tool::Tolerance(edge),tolerance),
                OcctBoundaryCurveKind::Coplanar,
                left->shape,right->shape,
                left->references,right->references,
                left->history_index,right->history_index});
            ++result.metrics.retained_coplanar_intervals;
        }
    }
    result.metrics.coplanar_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-coplanar_start).count();
    return result;
}

} // namespace zima::kernel
