#include "directional_projection.hpp"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepLib_CheckCurveOnSurface.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <GeomProjLib.hxx>
#include <Geom_Plane.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierSurface.hxx>
#include <ShapeConstruct_ProjectCurveOnSurface.hxx>
#include <Geom_Curve.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <ShapeFix_Edge.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Lin.hxx>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <set>
#include <optional>

namespace zima::kernel::detail {
namespace {
struct NativeCurve {
    Handle(Geom_Curve) curve;
    double first{}, last{};
    explicit NativeCurve(const TopoDS_Edge& edge) {
        TopLoc_Location location;
        curve = BRep_Tool::Curve(edge, location, first, last);
        if (curve.IsNull() || !std::isfinite(first) || !std::isfinite(last) || first >= last)
            throw std::runtime_error("Projection requires finite native input curves.");
        if (!location.IsIdentity())
            curve = Handle(Geom_Curve)::DownCast(curve->Transformed(location.Transformation()));
    }
};
struct Candidate {
    TopoDS_Edge edge;
    std::size_t target{};
    double first{}, last{}; // Source parameters, in edge parameter order.
};

double distance_to(const gp_Pnt& point, const TopoDS_Shape& shape) {
    BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(point).Vertex(), shape);
    if (!distance.IsDone()) throw std::runtime_error("Projection classification failed.");
    return distance.Value();
}

struct SourceParameters {
    NativeCurve source;
    gp_Pln plane;
    gp_Dir direction;
    double tolerance;
    double parameter(const gp_Pnt& point) const {
        const auto normal = plane.Axis().Direction();
        const double depth = gp_Vec(plane.Location(), point).Dot(gp_Vec(normal)) /
            direction.Dot(normal);
        const auto planar = point.Translated(-depth * gp_Vec(direction));
        GeomAPI_ProjectPointOnCurve locate(planar, source.curve, source.first, source.last);
        if (!locate.NbPoints() || locate.LowerDistance() > tolerance * 8)
            throw std::runtime_error("Projection lost its source curve correspondence.");
        return locate.LowerDistanceParameter();
    }
};

double candidate_parameter(const Candidate& candidate, const SourceParameters& source,
        double requested) {
    const NativeCurve curve(candidate.edge);
    const bool increasing = candidate.last > candidate.first;
    double lo = curve.first, hi = curve.last;
    if (std::abs(requested - candidate.first) < 1e-12) return lo;
    if (std::abs(requested - candidate.last) < 1e-12) return hi;
    // Find the source parameter on an existing exact native section curve.
    // Bisection trims its domain; it does not fit a new modeling curve.
    for (unsigned i = 0; i < 64; ++i) {
        const double mid = (lo + hi) * .5;
        if ((source.parameter(curve.curve->Value(mid)) < requested) == increasing) lo = mid;
        else hi = mid;
    }
    return (lo + hi) * .5;
}

void retain_target_curve(const TopoDS_Edge& edge, const TopoDS_Face& target, double tolerance) {
    const auto precise=[&] {
        BRepLib_CheckCurveOnSurface check(edge,target);check.Perform();
        return check.IsDone()&&std::isfinite(check.MaxDistance())&&check.MaxDistance()<=tolerance*8;
    };
    if(precise())return;
    // Joining native section edges can leave a pcurve outside the same spatial
    // correspondence budget used to classify its source. Reproject the unchanged
    // native 3D curve instead of accepting a mismatching surface representation.
    BRep_Builder builder;
    const NativeCurve curve(edge);
    ShapeConstruct_ProjectCurveOnSurface projector;
    projector.Init(BRep_Tool::Surface(target),tolerance/16);
    Handle(Geom2d_Curve) corrected;
    if(projector.Perform(curve.curve,curve.first,curve.last,corrected,tolerance/16,tolerance/16)&&!corrected.IsNull())
        builder.UpdateEdge(edge,corrected,target,tolerance);
    if(!precise())throw std::runtime_error("Projection could not retain the target parameter curve.");
}

TopoDS_Edge trim_candidate(const Candidate& candidate, const SourceParameters& source,
        double first, double last, const TopoDS_Face& target, double tolerance) {
    const NativeCurve native(candidate.edge);
    const double a = candidate_parameter(candidate, source, first);
    const double b = candidate_parameter(candidate, source, last);
    auto edge = BRepBuilderAPI_MakeEdge(native.curve, std::min(a,b), std::max(a,b)).Edge();
    double pf{}, pl{};
    const auto pcurve = BRep_Tool::CurveOnSurface(candidate.edge, target, pf, pl);
    if (!pcurve.IsNull()) {
        BRep_Builder builder;
        builder.UpdateEdge(edge, pcurve, target, tolerance);
        builder.Range(edge, target, std::min(a,b), std::max(a,b));
    } else {
        ShapeFix_Edge fix;
        fix.FixAddPCurve(edge, target, false, tolerance);
    }
    if (BRep_Tool::CurveOnSurface(edge, target, pf, pl).IsNull())
        throw std::runtime_error("Projection could not retain the target parameter curve.");
    retain_target_curve(edge,target,tolerance);
    if (a > b) edge.Reverse();
    return edge;
}

double forward_extent(const std::vector<ProjectionTarget>& targets,
        const gp_Pln& plane, const gp_Dir& direction, double tolerance) {
    Bnd_Box box;
    for (const auto& target : targets) BRepBndLib::Add(target.face, box);
    if (box.IsVoid() || box.IsOpen()) throw std::runtime_error("Projection requires bounded target faces.");
    double xmin{}, ymin{}, zmin{}, xmax{}, ymax{}, zmax{};
    box.Get(xmin,ymin,zmin,xmax,ymax,zmax);
    double extent = 0;
    const auto normal = plane.Axis().Direction();
    for (double x : {xmin,xmax}) for (double y : {ymin,ymax}) for (double z : {zmin,zmax})
        extent = std::max(extent, gp_Vec(plane.Location(),gp_Pnt(x,y,z)).Dot(gp_Vec(normal)) /
            direction.Dot(normal));
    // Extend the far boundary only. The source plane remains exactly at zero.
    return extent + std::max(1., extent) * .01 + tolerance;
}

std::optional<gp_Pln> exact_plane(const TopoDS_Face& face,double tolerance) {
    const auto surface=BRep_Tool::Surface(face);
    const auto spline=Handle(Geom_BSplineSurface)::DownCast(surface);
    const auto bezier=Handle(Geom_BezierSurface)::DownCast(surface);
    Bnd_Box box;BRepBndLib::Add(face,box);
    double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
    const double scale=std::max({1.,std::abs(x0),std::abs(y0),std::abs(z0),std::abs(x1),std::abs(y1),std::abs(z1)});
    const double roundoff=std::min(tolerance*1e-3,128*std::numeric_limits<double>::epsilon()*scale);
    // Match the established Up To certification: find a candidate plane at
    // kernel precision, then certify every positive-weight spline pole at
    // roundoff precision. Never flatten an almost-planar target.
    const GeomLib_IsPlanarSurface check(surface,spline||bezier?tolerance:roundoff);
    if(!check.IsPlanar())return {};
    if(spline)for(int u=1;u<=spline->NbUPoles();++u)for(int v=1;v<=spline->NbVPoles();++v)
        if(spline->Weight(u,v)<=0||check.Plan().Distance(spline->Pole(u,v))>roundoff)return {};
    if(bezier)for(int u=1;u<=bezier->NbUPoles();++u)for(int v=1;v<=bezier->NbVPoles();++v)
        if(bezier->Weight(u,v)<=0||check.Plan().Distance(bezier->Pole(u,v))>roundoff)return {};
    return check.Plan();
}

std::vector<Candidate> candidates_for(const ProjectionCurve& source,
        const std::vector<ProjectionTarget>& targets, const gp_Dir& direction,
        double extent, double tolerance) {
    const auto curtain = BRepPrimAPI_MakePrism(source.edge, extent * gp_Vec(direction), true).Shape();
    std::vector<Candidate> candidates;
    const auto append = [&](const TopoDS_Shape& shape, std::size_t target) {
        for (TopExp_Explorer it(shape,TopAbs_EDGE);it.More();it.Next()) {
            const auto edge = TopoDS::Edge(it.Current());
            if (!BRep_Tool::Degenerated(edge)) candidates.push_back({edge,target});
        }
    };
    for (std::size_t target = 0; target < targets.size(); ++target) {
        // Preserve the exact source basis for planar targets, including planes
        // stored as spline surfaces. A generic curtain section needlessly fits
        // such circles/splines and can move them off their supporting plane.
        const auto planar=exact_plane(targets[target].face,tolerance);
        if(planar&&std::abs(planar->Axis().Direction().Dot(direction))>1e-12) {
            const NativeCurve input(source.edge);
            const Handle(Geom_Curve) bounded_input=new Geom_TrimmedCurve(input.curve,input.first,input.last);
            const auto projected=GeomProjLib::ProjectOnPlane(bounded_input,new Geom_Plane(*planar),direction,true);
            const auto edge=BRepBuilderAPI_MakeEdge(projected).Edge();
            BRepAlgoAPI_Common forward(edge,curtain);
            forward.SetNonDestructive(true);forward.SetFuzzyValue(tolerance);forward.Build();
            if(!forward.IsDone())throw std::runtime_error("Projection intersection failed.");
            BRepAlgoAPI_Common bounded(forward.Shape(),targets[target].face);
            bounded.SetNonDestructive(true);bounded.SetFuzzyValue(tolerance);bounded.Build();
            if(!bounded.IsDone())throw std::runtime_error("Projection intersection failed.");
            append(bounded.Shape(),target);
            continue;
        }
        BRepAlgoAPI_Section section(targets[target].face,curtain,false);
        section.SetNonDestructive(true); section.SetFuzzyValue(tolerance);
        section.ComputePCurveOn1(true); section.ComputePCurveOn2(true);
        section.Approximation(true); section.Build();
        if (!section.IsDone()) throw std::runtime_error("Projection intersection failed.");
        // Native section algorithms can split one smooth branch at an
        // internal surface seam. As in Surface Intersection, join compatible
        // native edge domains before assigning semantic endpoint ancestry.
        ShapeUpgrade_UnifySameDomain merge(section.Shape(),true,false,true);
        merge.SetSafeInputMode(true);merge.SetLinearTolerance(tolerance);merge.Build();
        append(merge.Shape(),target);
        // A coincident start is a genuine zero-depth hit. Section may omit an
        // edge shared with the curtain's lower boundary, so intersect the
        // source edge itself without shifting the Sketch plane.
        BRepAlgoAPI_Common coincident(source.edge,targets[target].face);
        coincident.SetNonDestructive(true); coincident.SetFuzzyValue(tolerance); coincident.Build();
        if (!coincident.IsDone()) throw std::runtime_error("Projection start classification failed.");
        append(coincident.Shape(),target);
    }
    if (candidates.size() < 2) return candidates;
    // Physical crossings are visibility events. Split them before dividing
    // source domains so nearest-hit selection cannot change within a cell.
    BRepAlgoAPI_Splitter split;
    NCollection_List<TopoDS_Shape> arguments;
    for (const auto& candidate : candidates) arguments.Append(candidate.edge);
    split.SetArguments(arguments); split.SetNonDestructive(true);
    split.SetFuzzyValue(tolerance); split.Build();
    if (!split.IsDone()) throw std::runtime_error("Projection branch partition failed.");
    std::vector<Candidate> divided;
    for (const auto& candidate : candidates) {
        const auto& modified = split.Modified(candidate.edge);
        if (modified.IsEmpty() && !split.IsDeleted(candidate.edge)) divided.push_back(candidate);
        for (NCollection_List<TopoDS_Shape>::Iterator it(modified);it.More();it.Next())
            if (it.Value().ShapeType() == TopAbs_EDGE)
                divided.push_back({TopoDS::Edge(it.Value()),candidate.target});
    }
    return divided;
}
}

DirectionalProjection project_directionally(const std::vector<ProjectionCurve>& sources,
        const std::vector<ProjectionTarget>& targets, const gp_Pln& sketch_plane,
        const gp_Dir& direction, double tolerance) {
    if (sources.empty() || targets.empty() || !std::isfinite(tolerance) || tolerance <= 0)
        throw std::invalid_argument("Projection requires source curves, target faces and a positive tolerance.");
    if (std::abs(direction.Dot(sketch_plane.Axis().Direction())) < 1e-12)
        throw std::invalid_argument("Projection direction must cross the source Sketch plane.");
    const double extent = forward_extent(targets,sketch_plane,direction,tolerance);
    DirectionalProjection result;
    for (const auto& source : sources) {
        SourceParameters parameters{NativeCurve(source.edge),sketch_plane,direction,tolerance};
        auto candidates = candidates_for(source,targets,direction,extent,tolerance);
        std::vector<double> events{parameters.source.first,parameters.source.last};
        const double parameter_tolerance = std::max(1.,parameters.source.last-parameters.source.first)*1e-10;
        const bool closed_source=parameters.source.curve->Value(parameters.source.first).Distance(
            parameters.source.curve->Value(parameters.source.last))<=tolerance;
        for (auto& candidate : candidates) {
            const NativeCurve curve(candidate.edge);
            candidate.first = parameters.parameter(curve.curve->Value(curve.first));
            candidate.last = parameters.parameter(curve.curve->Value(curve.last));
            // A periodic closed source needs its complete native parameter
            // domain; equal endpoints do not mean that its curve is empty.
            if (std::abs(candidate.first-candidate.last) < 1e-10 &&
                curve.curve->Value(curve.first).Distance(curve.curve->Value(curve.last)) <= tolerance) {
                const double quarter = parameters.parameter(curve.curve->Value(curve.first*.75+curve.last*.25));
                const double three_quarters = parameters.parameter(curve.curve->Value(curve.first*.25+curve.last*.75));
                if (quarter < three_quarters) {
                    candidate.first=parameters.source.first; candidate.last=parameters.source.last;
                } else {
                    candidate.first=parameters.source.last; candidate.last=parameters.source.first;
                }
            } else if(closed_source) {
                // A section of a periodic source may end on either side of
                // its seam. Point inversion returns the same seam parameter
                // for both ends; use the interior of this exact branch to
                // select the correct end of the source's native domain.
                const double middle=parameters.parameter(curve.curve->Value((curve.first+curve.last)*.5));
                const auto at_seam=[&](double value){return std::abs(value-parameters.source.first)<=parameter_tolerance||
                    std::abs(value-parameters.source.last)<=parameter_tolerance;};
                if(at_seam(candidate.first))candidate.first=middle>candidate.last?parameters.source.last:parameters.source.first;
                if(at_seam(candidate.last))candidate.last=middle>candidate.first?parameters.source.last:parameters.source.first;
            }
            events.push_back(candidate.first); events.push_back(candidate.last);
        }
        std::ranges::sort(events);
        events.erase(std::unique(events.begin(),events.end(),[&](double a,double b) {
            return std::abs(a-b) <= parameter_tolerance;
        }),events.end());
        for (std::size_t i=1;i<events.size();++i) {
            const double first=events[i-1], last=events[i], middle=(first+last)*.5;
            if (last-first <= parameter_tolerance) continue;
            const auto origin=parameters.source.curve->Value(middle);
            struct Hit { std::size_t target; double distance; TopAbs_State state; };
            std::vector<Hit> hits;
            double nearest=std::numeric_limits<double>::infinity();
            for (std::size_t t=0;t<targets.size();++t) {
                IntCurvesFace_ShapeIntersector ray;
                ray.Load(targets[t].face,tolerance);
                ray.Perform(gp_Lin(origin,direction),-tolerance,extent);
                if (!ray.IsDone()) throw std::runtime_error("Projection ray classification failed.");
                for (int j=1;j<=ray.NbPnt();++j) {
                    const double depth=std::max(0.,ray.WParameter(j));
                    nearest=std::min(nearest,depth);
                    hits.push_back({t,depth,ray.State(j)});
                }
            }
            std::erase_if(hits,[&](const auto& hit){return hit.distance > nearest+tolerance;});
            if (hits.empty()) { result.gaps.push_back({source.source_key,first,last}); continue; }
            if (hits.size()>1 && std::ranges::any_of(hits,[](const auto& hit){return hit.state!=TopAbs_ON;})) {
                result.ambiguous=true; continue;
            }
            const auto point=origin.Translated(nearest*gp_Vec(direction));
            std::vector<std::string> target_keys;
            for (const auto& hit:hits) target_keys.push_back(targets[hit.target].target_key);
            std::ranges::sort(target_keys);
            target_keys.erase(std::unique(target_keys.begin(),target_keys.end()),target_keys.end());
            const Candidate* chosen=nullptr;
            for (const auto& candidate:candidates) {
                if (middle < std::min(candidate.first,candidate.last)-parameter_tolerance ||
                    middle > std::max(candidate.first,candidate.last)+parameter_tolerance ||
                    std::ranges::none_of(hits,[&](const auto& hit){return hit.target==candidate.target;})) continue;
                if (distance_to(point,candidate.edge)>tolerance*8) continue;
                // The key only makes equivalent seam representations
                // deterministic. It never chooses between depths or regions.
                if (!chosen || targets[candidate.target].target_key<targets[chosen->target].target_key) chosen=&candidate;
            }
            if (!chosen) throw std::runtime_error("Projection could not resolve its native first-hit curve.");
            auto edge=trim_candidate(*chosen,parameters,first,last,targets[chosen->target].face,tolerance);
            for(const auto& hit:hits)if(hit.target!=chosen->target) {
                ShapeFix_Edge fix;
                fix.FixAddPCurve(edge,targets[hit.target].face,false,tolerance);
                retain_target_curve(edge,targets[hit.target].face,tolerance);
            }
            result.curves.push_back({std::move(edge),source.source_key,std::move(target_keys),first,last});
        }
    }
    return result;
}

ProjectionPartition partition_projection(const TopoDS_Shape& input,
        const std::vector<ProjectionTarget>& targets,
        const DirectionalProjection& projection,double tolerance) {
    if(projection.ambiguous || !projection.gaps.empty() || projection.curves.empty())
        throw std::invalid_argument("Projection does not completely define a partition.");
    NCollection_List<TopoDS_Shape> arguments,tools;
    if(!input.IsNull())arguments.Append(input);
    else for(const auto& target:targets)arguments.Append(target.face);
    for(const auto& curve:projection.curves)tools.Append(curve.edge);
    BRepAlgoAPI_Splitter split;
    split.SetArguments(arguments);split.SetTools(tools);
    split.SetNonDestructive(true);split.SetFuzzyValue(tolerance);split.Build();
    if(!split.IsDone() || !BRepCheck_Analyzer(split.Shape()).IsValid())
        throw std::runtime_error("Projection face partition failed.");
    ProjectionPartition result;
    result.shape=split.Shape();result.history=split.History();
    bool separated=false;
    for(const auto& target:targets) {
        std::size_t count=0;
        for(NCollection_List<TopoDS_Shape>::Iterator it(split.Modified(target.face));it.More();it.Next())
            if(it.Value().ShapeType()==TopAbs_FACE) {
                result.regions.push_back({TopoDS::Face(it.Value()),target.target_key});++count;
            }
        if(!count && !split.IsDeleted(target.face))result.regions.push_back(target);
        separated=separated||count>1;
    }
    if(!separated)throw std::invalid_argument("Projected curves do not divide the selected faces.");
    return result;
}

void name_projected_curves(DirectionalProjection& projection,
        const std::vector<ProjectionCurve>& sources,
        const std::vector<ProjectionTarget>& targets,
        const gp_Pln& sketch_plane,const gp_Dir& direction,double tolerance) {
    const auto field=[](const std::string& value){return std::to_string(value.size())+":"+value;};
    const auto ancestry=[&](const auto& values) {
        std::string result;for(const auto& value:values)result+=field(value);return result;
    };
    std::set<std::string> curve_keys;
    for(auto& piece:projection.curves) {
        const auto source=std::ranges::find(sources,piece.source_key,&ProjectionCurve::source_key);
        if(source==sources.end())throw std::runtime_error("Projection source ancestry is missing.");
        const NativeCurve curve(source->edge);
        const double parameter_tolerance=std::max(1.,curve.last-curve.first)*1e-10;
        const bool closed=curve.curve->Value(curve.first).Distance(curve.curve->Value(curve.last))<=tolerance;
        const std::string parents="from:"+field(source->source_key)+":on:"+ancestry(piece.target_keys);
        const auto endpoint=[&](double parameter) {
            std::set<std::string> parents_at_point;
            if(std::abs(parameter-curve.first)<=parameter_tolerance&&!source->first_point_key.empty())
                parents_at_point.insert("point:"+source->first_point_key);
            if(std::abs(parameter-curve.last)<=parameter_tolerance&&!source->last_point_key.empty())
                parents_at_point.insert("point:"+source->last_point_key);
            gp_Pnt start;gp_Vec tangent;curve.curve->D1(parameter,start,tangent);
            SourceParameters parameters{curve,sketch_plane,direction,tolerance};
            const NativeCurve output(piece.edge);
            // The output edge may be reversed, while its native parameter
            // range is always increasing. Match endpoints through the source.
            const auto a=output.curve->Value(output.first),b=output.curve->Value(output.last);
            const auto point=std::abs(parameters.parameter(a)-parameter)<std::abs(parameters.parameter(b)-parameter)?a:b;
            const double depth=std::max(0.,gp_Vec(start,point).Dot(gp_Vec(direction)));
            const auto ray=BRepBuilderAPI_MakeEdge(start.Translated(-tolerance*gp_Vec(direction)),
                start.Translated((depth+tolerance)*gp_Vec(direction))).Edge();
            for(const auto& target:targets)for(const auto& boundary:target.boundaries) {
                if(boundary.source_key.empty())continue;
                BRepExtrema_DistShapeShape intersection(ray,boundary.edge);
                if(!intersection.IsDone()||intersection.Value()>tolerance*8)continue;
                const NativeCurve support(boundary.edge);
                const auto hit=intersection.PointOnShape2(1);
                GeomAPI_ProjectPointOnCurve locate(hit,support.curve,support.first,support.last);
                if(!locate.NbPoints())throw std::runtime_error("Projection boundary ancestry is missing.");
                gp_Pnt ignored;gp_Vec boundary_tangent;
                support.curve->D1(locate.LowerDistanceParameter(),ignored,boundary_tangent);
                const double sign=gp_Vec(direction).Dot(tangent.Crossed(boundary_tangent));
                const double scale=tangent.Magnitude()*boundary_tangent.Magnitude();
                const auto crossing=std::abs(sign)<=scale*1e-10?":tangent":sign<0?":negative":":positive";
                parents_at_point.insert("boundary:"+field(target.target_key)+field(boundary.source_key)+crossing);
            }
            if(parents_at_point.empty())return std::string{};
            return "projection:point:"+parents+":at:"+ancestry(parents_at_point);
        };
        const bool whole_closed=closed&&std::abs(piece.source_first-curve.first)<=parameter_tolerance&&
            std::abs(piece.source_last-curve.last)<=parameter_tolerance;
        if(whole_closed) {
            // Its parameter seam is not an authored selectable point.
            piece.first_point_key.clear();piece.last_point_key.clear();
            piece.semantic_key="projection:curve:"+parents+":closed";
        } else {
            piece.first_point_key=endpoint(piece.source_first);piece.last_point_key=endpoint(piece.source_last);
            if(piece.first_point_key.empty()||piece.last_point_key.empty())
                throw std::runtime_error("Projection endpoint ancestry is missing.");
            piece.semantic_key="projection:curve:"+parents+":between:"+field(piece.first_point_key)+field(piece.last_point_key);
        }
        if(!curve_keys.insert(piece.semantic_key).second)
            throw std::runtime_error("Projection curve ancestry is ambiguous.");
    }
}
}
