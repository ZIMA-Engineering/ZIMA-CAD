#pragma once
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepLib_CheckCurveOnSurface.hxx>
#include <BRepTools_ReShape.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <chrono>

namespace zima::kernel::support_driven_fillet {

struct ContactPhaseTimer {
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    void phase(const char* label) {
        const auto now=std::chrono::steady_clock::now();
        if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))
            std::fprintf(stderr,"Rolling contact %s: %.3f ms\n",label,
                std::chrono::duration<double,std::milli>(now-start).count());
        start=now;
    }
};

inline constexpr const char* tangent_transition_error=
    "The tangent transition could not be constructed. Adjust the radius or selected edges.";

template<class Operation>
decltype(auto) tangent_transition(Operation&& operation) {
    try {
        return operation();
    } catch(const Standard_Failure& error) {
        if(std::getenv("ZIMA_CPP_FILLET_END_TRACE"))
            std::fprintf(stderr,"Rolling transition: %s\n",error.GetMessageString());
    } catch(const std::exception& error) {
        if(std::getenv("ZIMA_CPP_FILLET_END_TRACE"))
            std::fprintf(stderr,"Rolling transition: %s\n",error.what());
    }
    throw std::runtime_error(tangent_transition_error);
}

// Contact edges are measured against their actual surfaces. A Boolean may
// preserve an approximate pcurve even when its unchanged 3D curve is accurate.
// Correct only that parameterization; never move a contact to pass validation.
inline void validate_contact_curves(const TopoDS_Shape& shape,bool check_intersections=true) {
    ContactPhaseTimer timer;
    constexpr double tolerance=1.e-7;
    for(TopExp_Explorer faces(shape,TopAbs_FACE);faces.More();faces.Next()) {
        const auto face=TopoDS::Face(faces.Current());
        for(TopExp_Explorer edges(face,TopAbs_EDGE);edges.More();edges.Next()) {
            const auto edge=TopoDS::Edge(edges.Current());
            if(BRep_Tool::Degenerated(edge))continue;
            BRepLib_CheckCurveOnSurface check(edge,face);check.Perform();
            if(!check.IsDone())throw std::runtime_error("Contact boundary misses section");
            if(check.MaxDistance()<=BRep_Tool::Tolerance(edge)&&check.MaxDistance()<=8.*tolerance)continue;
            precise_pcurve(edge,face,tolerance);
            BRepLib_CheckCurveOnSurface corrected(edge,face);corrected.Perform();
            if(!corrected.IsDone()||corrected.MaxDistance()>20.*tolerance)
                throw std::runtime_error("Contact boundary misses section");
            BRep_Builder builder;
            builder.UpdateEdge(edge,std::max(BRep_Tool::Tolerance(edge),1.1*corrected.MaxDistance()));
        }
    }
    timer.phase("curve parameters");
    if(!BRepCheck_Analyzer(shape,true,false,true).IsValid())
        throw std::runtime_error("Corner shell is not a solid");
    timer.phase("exact topology");
    // Intermediate routes need valid topology and accurate pcurves for the
    // next trim. The caller checks self-intersections of the complete result
    // after all routes and same-domain cleanup, once per Fillet operation.
    if(!check_intersections)return;
    BOPAlgo_ArgumentAnalyzer check;
    check.SetShape1(shape);check.SelfInterMode()=true;
    check.CurveOnSurfaceMode()=true;check.Perform();
    if(check.HasFaulty())throw std::runtime_error("Corner shell is not a solid");
    timer.phase("intersections and curves");
}

struct ContactReplacement {
    TopoDS_Shape shape;
    occ::handle<BRepTools_History> history;
};

// At a tangent runout the artificial volume wedge has zero thickness and can
// self-intersect although the rolling envelope is regular. Replace its support
// patches on the real Body boundary instead of Boolean-fusing that wedge.
// All edited topology and geometry are privately copied, including pcurves.
inline ContactReplacement replace_contacts(const TopoDS_Shape& input,
        const TopoDS_Shape& domains,std::span<const TopoDS_Face> envelopes,
        const gp_Pnt& source_point) {
    constexpr double tolerance=1.e-7;
    ContactPhaseTimer timer;
    TopoDS_Solid target;
    for(TopExp_Explorer solids(input,TopAbs_SOLID);solids.More();solids.Next()) {
        BRepClass3d_SolidClassifier classifier(solids.Current(),source_point,tolerance);
        if(classifier.State()!=TopAbs_IN&&classifier.State()!=TopAbs_ON)continue;
        if(!target.IsNull())throw std::runtime_error("Rolling route support trim failed");
        target=TopoDS::Solid(solids.Current());
    }
    if(target.IsNull())throw std::runtime_error("Rolling route support trim failed");
    BRepBuilderAPI_Copy copy(target,true,false);
    if(!copy.IsDone())throw std::runtime_error("Rolling route support trim failed");
    auto history=occ::handle<BRepTools_History>(new BRepTools_History);
    NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> originals,copied;
    TopExp::MapShapes(target,originals);TopExp::MapShapes(copy.Shape(),copied);
    for(const auto& source:originals)if(BRepTools_History::IsSupportedType(source))
        history->AddModified(source,copy.ModifiedShape(source));
    NCollection_List<TopoDS_Shape> tools;
    for(TopExp_Explorer solids(domains,TopAbs_SOLID);solids.More();solids.Next())tools.Append(solids.Current());
    BRepBuilderAPI_Sewing sewing(10.*tolerance);
    NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> trim_members,sewing_inputs;
    std::vector<occ::handle<BRepTools_History>> cuts;
    for(const auto& face:envelopes){sewing.Add(face);TopExp::MapShapes(face,sewing_inputs);}
    for(TopExp_Explorer faces(copy.Shape(),TopAbs_FACE);faces.More();faces.Next()) {
        const auto face=TopoDS::Face(faces.Current());
        Support support;support.face=face;
        NCollection_List<TopoDS_Shape> patches;
        for(const auto& tool:tools)for(TopExp_Explorer candidates(tool,TopAbs_FACE);candidates.More();candidates.Next()) {
            Support candidate;candidate.face=TopoDS::Face(candidates.Current());
            if(same(support,candidate,tolerance))patches.Append(candidate.face);
        }
        TopoDS_Shape trimmed=face;
        if(!patches.IsEmpty()) {
            // A neighboring torus patch can cross the plane before the new
            // plane/torus contact. Include its exact volume cross-section.
            if(BRepAdaptor_Surface(face).GetType()==GeomAbs_Plane)patches=tools;
            BRepAlgoAPI_Cut cut;cut.SetArguments({face});cut.SetTools(patches);
            cut.SetNonDestructive(true);cut.SetToFillHistory(true);
            cut.SetFuzzyValue(tolerance);cut.Build();
            if(!cut.IsDone()||cut.Shape().IsNull()||!cut.History())
                throw std::runtime_error("Rolling route support trim failed");
            trimmed=cut.Shape();cuts.push_back(cut.History());
        }
        sewing.Add(trimmed);TopExp::MapShapes(trimmed,trim_members);
        TopExp::MapShapes(trimmed,sewing_inputs);
    }
    // Face trims are parallel replacements. Keep an edge still used by another
    // support; do not merge a tool's deletion history into the Body history.
    auto trims=occ::handle<BRepTools_History>(new BRepTools_History);
    for(const auto& source:copied)if(BRepTools_History::IsSupportedType(source)) {
        bool retained=trim_members.Contains(source);
        for(const auto& cut:cuts) {
            for(const auto& changed:cut->Modified(source))if(trim_members.Contains(changed)) {
                trims->AddModified(source,changed);retained=true;
            }
            for(const auto& generated:cut->Generated(source))if(trim_members.Contains(generated))
                trims->AddGenerated(source,generated);
        }
        if(!retained)trims->Remove(source);
    }
    history->Merge(trims);
    timer.phase("support trims");
    sewing.Perform();
    auto sewn=sewing.SewedShape();
    BRepTools_ReShape contact_seams;
    std::set<int> paired;
    // A nearly tangent Boolean can shorten a support's contact curve while
    // retaining the same sewn end vertices. Reuse the envelope's accurate
    // contact only after proving it lies on that actual support. This repairs
    // the shared boundary, without increasing the sewing tolerance.
    const auto owner=[&](const TopoDS_Edge& edge) {
        TopoDS_Face result;
        for(TopExp_Explorer faces(sewn,TopAbs_FACE);faces.More();faces.Next())
            for(TopExp_Explorer edges(faces.Current(),TopAbs_EDGE);edges.More();edges.Next())
                if(edges.Current().IsSame(edge)) {
                    if(!result.IsNull())return TopoDS_Face{};
                    result=TopoDS::Face(faces.Current());
                }
        return result;
    };
    const auto is_envelope=[&](const TopoDS_Face& face) {
        return !face.IsNull()&&std::ranges::any_of(envelopes,[&](const auto& envelope) {
            return BRep_Tool::Surface(face)==BRep_Tool::Surface(envelope);
        });
    };
    for(int i=1;i<=sewing.NbFreeEdges();++i) {
        if(paired.contains(i))continue;
        auto exact=TopoDS::Edge(sewing.FreeEdge(i).Oriented(TopAbs_FORWARD));
        const auto envelope=owner(exact);if(!is_envelope(envelope))continue;
        TopoDS_Vertex a,b;TopExp::Vertices(exact,a,b,true);
        int match=0;bool reverse=false;TopoDS_Face support;
        for(int j=1;j<=sewing.NbFreeEdges();++j)if(j!=i&&!paired.contains(j)) {
            const auto candidate=TopoDS::Edge(sewing.FreeEdge(j).Oriented(TopAbs_FORWARD));
            TopoDS_Vertex c,d;TopExp::Vertices(candidate,c,d,true);
            const bool forward=a.IsSame(c)&&b.IsSame(d),back=a.IsSame(d)&&b.IsSame(c);
            if(!forward&&!back)continue;
            const auto face=owner(candidate);if(face.IsNull()||is_envelope(face))continue;
            if(match){match=0;break;}match=j;reverse=back;support=face;
        }
        if(!match)continue;
        ShapeFix_Edge fix;fix.FixAddPCurve(exact,support,false,tolerance);
        precise_pcurve(exact,support,tolerance);
        BRepLib_CheckCurveOnSurface check(exact,support);check.Perform();
        if(!check.IsDone()||check.MaxDistance()>2.*tolerance)continue;
        auto replacement=exact;if(reverse)replacement.Reverse();
        contact_seams.Replace(sewing.FreeEdge(match).Oriented(TopAbs_FORWARD),replacement);
        paired.insert(i);paired.insert(match);
    }
    if(!paired.empty())sewn=contact_seams.Apply(sewn);
    if(sewn.IsNull()||paired.size()!=std::size_t(sewing.NbFreeEdges())||sewing.NbMultipleEdges()) {
        if(std::getenv("ZIMA_CPP_FILLET_END_TRACE")) {
            std::fprintf(stderr,"Rolling replacement: free=%d multiple=%d witness=(%.12g,%.12g,%.12g)\n",sewing.NbFreeEdges(),sewing.NbMultipleEdges(),source_point.X(),source_point.Y(),source_point.Z());
            for(int e=1;e<=sewing.NbFreeEdges();++e) {
                const BRepAdaptor_Curve c(sewing.FreeEdge(e));const auto a=c.Value(c.FirstParameter()),b=c.Value(c.LastParameter());
                TopoDS_Vertex first,last;TopExp::Vertices(sewing.FreeEdge(e),first,last);
                std::fprintf(stderr,"  free=%d type=%d tolerances=%.12g/%.12g/%.12g from=(%.12g,%.12g,%.12g) to=(%.12g,%.12g,%.12g)\n",e,int(c.GetType()),BRep_Tool::Tolerance(sewing.FreeEdge(e)),BRep_Tool::Tolerance(first),BRep_Tool::Tolerance(last),a.X(),a.Y(),a.Z(),b.X(),b.Y(),b.Z());
                for(TopExp_Explorer faces(sewing.SewedShape(),TopAbs_FACE);faces.More();faces.Next())
                    for(TopExp_Explorer edges(faces.Current(),TopAbs_EDGE);edges.More();edges.Next())
                        if(edges.Current().IsSame(sewing.FreeEdge(e)))std::fprintf(stderr,"    support=%d\n",int(BRepAdaptor_Surface(TopoDS::Face(faces.Current())).GetType()));
            }
        }
        throw std::runtime_error("Corner shell is not closed");
    }
    BRepBuilderAPI_MakeSolid make;
    for(TopExp_Explorer shells(sewn,TopAbs_SHELL);shells.More();shells.Next()) {
        if(!BRep_Tool::IsClosed(shells.Current()))throw std::runtime_error("Corner shell is not closed");
        make.Add(TopoDS::Shell(shells.Current()));
    }
    auto solid=make.Solid();
    if(solid.IsNull()||!BRepLib::OrientClosedSolid(solid))
        throw std::runtime_error("Corner shell is not a solid");
    timer.phase("sewing");
    validate_contact_curves(solid,false);
    timer.phase("validation");
    NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> members;
    TopExp::MapShapes(solid,members);
    auto sewn_history=occ::handle<BRepTools_History>(new BRepTools_History);
    for(const auto& source:sewing_inputs)if(BRepTools_History::IsSupportedType(source)) {
        auto changed=sewing.IsModifiedSubShape(source)?sewing.ModifiedSubShape(source):
            sewing.IsModified(source)?sewing.Modified(source):source;
        if(!paired.empty()&&!changed.IsNull())changed=contact_seams.Apply(changed);
        bool retained=members.Contains(source);
        NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> descendants;
        if(!changed.IsNull())TopExp::MapShapes(changed,descendants);
        for(const auto& child:descendants)if(child.ShapeType()==source.ShapeType()&&members.Contains(child)) {
            if(!child.IsSame(source))sewn_history->AddModified(source,child);
            retained=true;
        }
        if(!retained)sewn_history->Remove(source);
    }
    history->Merge(sewn_history);
    BRepTools_ReShape replacement;replacement.Replace(target,solid);
    return {replacement.Apply(input),history};
}
} // namespace zima::kernel::support_driven_fillet
