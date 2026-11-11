#include <IntCurvesFace_ShapeIntersector.hxx>
#include "ordered_up_to.hpp"
#include "kernel_history_version.hpp"
#include <zima/kernel/ordered_up_to_resolution.hpp>
#include <BRepOffsetAPI_MakeFilling.hxx>
#include <GeomFill_Gordon.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomConvert_CompCurveToBSplineCurve.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>

#include <BRepLib_CheckCurveOnSurface.hxx>
#include <BRepCheck_Result.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <zima/kernel/profile_centerlines.hpp>
#include <zima/kernel/feature_side_identity.hpp>
#include <zima/kernel/edge_treatment_chain.hpp>
#include <zima/kernel/feature_rotation_direction.hpp>
#include <zima/kernel/drill_point_identity.hpp>
#include <zima/kernel/inertia.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <zima/kernel/curve_plane_intersection.hpp>
#include <zima/kernel/occt_curve_data.hpp>
#include <zima/kernel/occt_boundary_union.hpp>
#include <zima/kernel/occt_planar_edge_chamfer.hpp>
#include <zima/kernel/occt_planar_edge_fillet.hpp>
#include <zima/kernel/pattern_geometry.hpp>
#include <zima/kernel/axis_display.hpp>
#include <zima/kernel/mirror_geometry.hpp>
#include <zima/kernel/scale_geometry.hpp>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <zima/kernel/shaft_thread_geometry.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/step_source_document.hpp>
#include <zima/kernel/solid_straightening.hpp>
#include <zima/kernel/solid_state_history.hpp>
#include <zima/kernel/solid_state_ancestry.hpp>
#include <zima/kernel/surface_shell_identity.hpp>

#include <BRepGProp.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepGProp_Domain.hxx>
#include <BRepGProp_VinertGK.hxx>
#include <BRepGProp_Vinert.hxx>
#include <future>
#include <numeric>
#include <atomic>
#include <thread>
#include <GCPnts_AbscissaPoint.hxx>
#include <BRepBndLib.hxx>
#include <gp_Ax3.hxx>
#include <chrono>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <BRepLib.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <zima/kernel/face_plane_intersection.hpp>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Curve2d.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <ShapeFix_Face.hxx>
#include <ShapeFix_Edge.hxx>
#include <ShapeBuild_ReShape.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass_FaceExplorer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffset_MakeSimpleOffset.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <Geom_Plane.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_ConicalSurface.hxx>
#include <BRepOffset_Mode.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <Geom2dConvert.hxx>
#include <Geom2dConvert_CompCurveToBSplineCurve.hxx>
#include <Geom2dAPI_Interpolate.hxx>
#include <GCE2d_MakeSegment.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <BRepTools_ShapeSet.hxx>
#include <BRepTools_History.hxx>
#include <BRep_Tool.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <NCollection_HSequence.hxx>
#include <STEPControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <IGESControl_Writer.hxx>
#include <IGESData_IGESModel.hxx>
#include <STEPControl_Writer.hxx>
#include <XSControl_TransferReader.hxx>
#include <XSControl_WorkSession.hxx>
#include <TransferBRep.hxx>
#include <Interface_InterfaceModel.hxx>
#include <StepShape_FaceSurface.hxx>
#include <StepShape_EdgeCurve.hxx>
#include <StepShape_VertexPoint.hxx>
#include <StlAPI_Writer.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TDataStd_Name.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFPrs_DocumentExplorer.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <TDF_Tool.hxx>
#include <GProp_GProps.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <gp_GTrsf.hxx>
#include <gp_Quaternion.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom_OffsetCurve.hxx>
#include <GeomConvert_ApproxCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <GeomFill_DiscreteTrihedron.hxx>
#include <GeomFill_CorrectedFrenet.hxx>
#include <GeomConvert_CompCurveToBSplineCurve.hxx>
#include <GeomConvert.hxx>
#include <Geom_Surface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <GeomAPI_PointsToBSplineSurface.hxx>
#include <GeomProjLib.hxx>
#include <Geom_BSplineSurface.hxx>
#include <BRepTools_Modification.hxx>
#include <BRepTools_Modifier.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TColStd_HArray1OfReal.hxx>
#include <zima/kernel/sheet_material.hpp>
#include <GeomConvert_SurfToAnaSurf.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomAPI_IntCS.hxx>
#include <Geom_Circle.hxx>
#include <GeomLProp_SLProps.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <TColgp_HArray1OfPnt2d.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <GeomAbs_CurveType.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Elips.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <bit>
#include <cmath>
#include <array>
#include <limits>
#include <numbers>
#include <optional>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <memory>
#include <functional>

namespace zima::kernel {

namespace { thread_local AggregatePropertyDemand aggregate_property_demand=AggregatePropertyDemand::All; }
namespace { thread_local bool eager_face_measurements=true; }
FaceMeasurementScope::FaceMeasurementScope(bool eager):previous_(eager_face_measurements) {eager_face_measurements=eager;}
FaceMeasurementScope::~FaceMeasurementScope() {eager_face_measurements=previous_;}
AggregatePropertyScope::AggregatePropertyScope(AggregatePropertyDemand demand)
    :previous_(aggregate_property_demand) { aggregate_property_demand=demand; }
AggregatePropertyScope::~AggregatePropertyScope() { aggregate_property_demand=previous_; }

namespace {

constexpr std::size_t kLiveShapeCacheLimit = 256;

gp_Trsf primitive_transform(const Vec3& translation, const Vec3& rotation_degrees);

TopoDS_Shape read_kernel_shape(const BodyResult& body) {
    return read_calculated_body_shape(body);
}

TopoDS_Compound placed_compound(const std::vector<PlacedBody>& bodies) {
    if (bodies.empty()) throw std::invalid_argument("Export has no visible bodies");
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (const auto& placed : bodies) {
        const auto shape = read_kernel_shape(placed.body);
        BRepBuilderAPI_Transform transformed(shape,
            primitive_transform(placed.translation, placed.rotation_degrees), true);
        transformed.Build();
        if (!transformed.IsDone() || transformed.Shape().IsNull()) {
            throw std::runtime_error("Export body placement failed");
        }
        builder.Add(compound, transformed.Shape());
    }
    return compound;
}


template <typename Reference>
struct OwnedTopology {
    TopoDS_Shape shape;
    Reference reference;
};

using OwnedFace = OwnedTopology<FaceReference>;
using OwnedEdge = OwnedTopology<EdgeReference>;
using OwnedVertex = OwnedTopology<VertexReference>;

class DirectBatchReferenceFallback final : public std::runtime_error {
public:
    DirectBatchReferenceFallback()
        :std::runtime_error(
            "Direct Add topology cannot resolve every authored reference") {}
};

// Immutable chain of original operands. OCCT handles share their underlying
// shapes; saving a history boundary does not duplicate the source B-Reps.
struct OriginalFaces {
    std::vector<OwnedFace> faces;
    std::shared_ptr<const OriginalFaces> previous;
    std::vector<OwnedEdge> edges;
    std::vector<OwnedVertex> vertices;
    TopoDS_Shape shape;
};
std::string dependency_fingerprint(std::string_view source) {
    std::uint64_t hash=14695981039346656037ull;
    for(const unsigned char byte:source){hash^=byte;hash*=1099511628211ull;}
    std::ostringstream text;text<<std::hex<<hash;return text.str();
}

bool same_face_identity(const FaceReference& a,const FaceReference& b) {
    return a.owner_id==b.owner_id && a.semantic_key==b.semantic_key && a.instance_path==b.instance_path;
}
std::vector<FaceReference> extrusion_references(const std::vector<HistoryOperation>& operations) {
    std::vector<FaceReference> result;
    const auto collect_ordered=[&](const std::optional<OrderedUpToLimit>& limit) {
        if(limit)for(const auto& patch:limit->patches)if(!patch.target.datum)result.push_back(patch.target.reference);
    };
    const auto collect=[&](const ExtrusionRequest& value) {
        collect_ordered(value.ordered_end_limit);
        if((value.extent==ExtrusionRequest::Extent::UpToPlane || value.extent==ExtrusionRequest::Extent::UpToSurface) && !value.target_is_datum)result.push_back(value.target_face);
        if(value.reverse_limit && !value.reverse_limit->datum)result.push_back(value.reverse_limit->reference);
    };
    for(const auto& operation:operations) {
        if(operation.suppressed)continue;
        if(const auto* value=std::get_if<ExtrusionRequest>(&operation.primitive))collect(*value);
        else if(const auto* value=std::get_if<RevolutionRequest>(&operation.primitive))collect_ordered(value->ordered_end_limit);
        else if(const auto* group=std::get_if<FeatureGroupRequest>(&operation.primitive)) {
            for(const auto& child:group->children) {
                if(const auto* value=std::get_if<ExtrusionRequest>(&child))collect(*value);
                else if(const auto* value=std::get_if<RevolutionRequest>(&child))collect_ordered(value->ordered_end_limit);
            }
        } else if(const auto* thread=std::get_if<ThreadSurfaceRequest>(&operation.primitive)) {
            if(thread->enabled && thread->end_plane_reference)result.push_back(*thread->end_plane_reference);
            for(const auto& group:{thread->cuts_before,thread->cuts_after})if(group)
                for(const auto& child:group->children)if(const auto* value=std::get_if<ExtrusionRequest>(&child))collect(*value);
        } else if(const auto* surface=std::get_if<BoundarySurfaceRequest>(&operation.primitive)) {
            for(const auto& constraint:surface->constraints)if(constraint.continuity!=SurfaceContinuity::G0&&constraint.support)
                result.push_back(*constraint.support);
        } else if(const auto* intersection=std::get_if<SurfaceIntersectionRequest>(&operation.primitive)) {
            result.insert(result.end(),intersection->faces.begin(),intersection->faces.end());
        } else if(const auto* trim=std::get_if<SurfaceTrimRequest>(&operation.primitive)) {
            result.push_back(trim->target);
            for(const auto& tool:trim->tools)if(tool.face)result.push_back({tool.reference.owner_id,tool.reference.semantic_key,{}});
        }
    }
    return result;
}

template <typename Algorithm, typename Owned>
std::vector<Owned> propagate_topology(
    Algorithm& algorithm, const std::vector<Owned>& existing,
    const std::vector<Owned>& operand);

struct PrimitiveData {
    TopoDS_Shape shape;
    std::vector<OwnedFace> faces;
    std::vector<OwnedEdge> edges;
    std::vector<OwnedVertex> vertices;
    std::vector<StepRequest::TopologyIdentity> imported_step_topology;
    // Full endpoint faces before intersecting sweep segments are united.
    std::vector<OwnedFace> source_caps;
    std::vector<SheetCutRegion> sheet_cuts;
    ViewerReferenceGeometry profile_references;
    // A derived copy may apply several authored source features with different
    // Boolean roles. Keep those transformed operands together so a later copy
    // of this copy can replay the same batched Cut/Add phases instead of
    // flattening the feature into whichever source happened to be first.
    std::vector<std::pair<BooleanOperation,
        std::shared_ptr<PrimitiveData>>> copy_phases;
    bool empty_material_operand{};
    bool reference_only{}; // Stationary endpoint datums are not a material wire.
};

gp_Trsf primitive_transform(const Vec3& translation, const Vec3& rotation_degrees) {
    constexpr double radians_per_degree = std::numbers::pi / 180.0;
    gp_Trsf transform;
    transform.SetTranslation(gp_Vec(translation.x, translation.y, translation.z));
    for (const auto& rotation : std::array<std::pair<gp_Dir, double>, 3>{
            std::pair{gp_Dir(0.0, 0.0, 1.0), rotation_degrees.z},
            std::pair{gp_Dir(0.0, 1.0, 0.0), rotation_degrees.y},
            std::pair{gp_Dir(1.0, 0.0, 0.0), rotation_degrees.x}}) {
        gp_Trsf next;
        next.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), rotation.first),
                         rotation.second * radians_per_degree);
        transform.Multiply(next);
    }
    return transform;
}

template <typename Owned>
std::vector<Owned> propagate_topology(
    const Handle(BRepTools_History)& history,
    const std::vector<Owned>& existing) {
    if (history.IsNull()) return existing;
    std::vector<Owned> propagated;
    const auto append_unique = [&](const TopoDS_Shape& shape,
                                   const auto& reference) {
        if (std::none_of(propagated.begin(), propagated.end(),
                [&](const auto& value) {
                    return value.shape.IsSame(shape) &&
                        value.reference == reference;
                })) {
            propagated.push_back({shape, reference});
        }
    };
    for (const auto& source : existing) {
        bool has_descendant = false;
        for (const auto* list : {
                &history->Modified(source.shape),
                &history->Generated(source.shape)}) {
            for (TopTools_ListIteratorOfListOfShape iterator(*list);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() != source.shape.ShapeType()) continue;
                append_unique(iterator.Value(), source.reference);
                has_descendant = true;
            }
        }
        if (!has_descendant && !history->IsRemoved(source.shape)) {
            append_unique(source.shape, source.reference);
        }
    }
    return propagated;
}

Handle(Geom_Curve) sweep_spine_curve(const Sweep3DRequest::PathSegment& segment) {
    if(segment.arc_midpoint) {
        const auto& a=segment.start;const auto& m=*segment.arc_midpoint;const auto& b=segment.end;
        GC_MakeArcOfCircle arc(gp_Pnt(a.x,a.y,a.z),gp_Pnt(m.x,m.y,m.z),gp_Pnt(b.x,b.y,b.z));
        if(!arc.IsDone())throw std::runtime_error("Nelze vytvořit oblouk dráhy Sweepu");
        return arc.Value();
    }
    const auto bezier=[](const auto& points) {
        TColgp_Array1OfPnt poles(1,4);
        for(int i=0;i<4;++i){const auto& p=points.at(i);poles.SetValue(i+1,gp_Pnt(p.x,p.y,p.z));}
        return Handle(Geom_BezierCurve)(new Geom_BezierCurve(poles));
    };
    if(!segment.bezier_spans.empty()) {
        GeomConvert_CompCurveToBSplineCurve curve;
        for(const auto& span:segment.bezier_spans)
            if(!curve.Add(bezier(span),1e-7,true,true,1))throw std::runtime_error("Nelze spojit spline dráhy Sweepu");
        return curve.BSplineCurve();
    }
    if(!segment.bezier_control_points.empty())return bezier(segment.bezier_control_points);
    return {};
}

std::vector<ViewerEdge> centerlines_for_operation(const HistoryOperation& operation) {
    const auto* request=std::get_if<Sweep3DRequest>(&operation.primitive);
    if(!request || !request->make_solid)return {};
    std::vector<ViewerEdge> result;
    std::map<std::string,std::size_t> sources;
    std::map<std::string,GeomConvert_CompCurveToBSplineCurve> exact_sources;
    for(const auto& segment:request->path_segments) {
        std::vector<Vec3> points;
        const auto geometry=sweep_spine_curve(segment);
        if(geometry.IsNull()) points={segment.start,segment.end};
        else {
            GeomAdaptor_Curve curve(geometry);
            GCPnts_TangentialDeflection samples(curve,0.15,request->linear_tolerance);
            for(int i=1;i<=samples.NbPoints();++i){const auto p=curve.Value(samples.Parameter(i));points.push_back({p.X(),p.Y(),p.Z()});}
        }
        if(points.size()<2)continue;
        if(!geometry.IsNull()&&!exact_sources[segment.source_id].Add(Handle(Geom_BoundedCurve)::DownCast(geometry),1e-7,true,true,1))
            throw std::runtime_error("Disconnected centerline source segments");
        // Tessellation pieces of one source curve share one identity. The
        // source ZIMA segment, not an approximation index, defines the part.
        const auto [entry,inserted]=sources.emplace(segment.source_id,result.size());
        if(inserted) {
            ViewerEdge edge;edge.reference={operation.owner_id,"centerline:from:"+segment.source_id,{}};
            edge.display_owner_id=operation.owner_id;edge.overlay=true;edge.construction=true;edge.dash_dot=true;
            edge.points=std::move(points);result.push_back(std::move(edge));
        } else {
            auto& edge=result[entry->second];const auto& a=edge.points.back();const auto& b=points.front();
            if(std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z)>1e-7)
                throw std::runtime_error("Disconnected centerline source segments");
            edge.points.insert(edge.points.end(),points.begin()+1,points.end());
        }
    }
    for(const auto& [source,curve]:exact_sources)
        result[sources.at(source)].exact_spline=capture_bspline_geometry(curve.BSplineCurve());
    return result;
}

std::vector<ViewerPoint> sweep_endpoints_for_operation(const HistoryOperation& operation) {
    const auto* request = std::get_if<Sweep3DRequest>(&operation.primitive);
    if (!request) return {};
    std::vector<ViewerPoint> result;
    if(request->rotation_axis) {
        const auto& axis=*request->rotation_axis;
        for(bool start:{true,false}) {
            const double distance=(start?-.5:.5)*axis.display_length;
            ViewerPoint point;
            point.position={axis.point.x+axis.direction.x*distance,axis.point.y+axis.direction.y*distance,axis.point.z+axis.direction.z*distance};
            point.reference={operation.owner_id,std::string(start?"helical:axis-point:start:from:":"helical:axis-point:end:from:")+axis.reference.semantic_key,{}};
            point.display_owner_id=operation.owner_id;
            result.push_back(std::move(point));
        }
    }
    if (!request->attachment_endpoints || !request->make_solid || request->path_segments.empty()) return result;
    for (const bool start : {true, false}) {
        const auto& segment = start ? request->path_segments.front() : request->path_segments.back();
        ViewerPoint point;
        point.position = start ? segment.start : segment.end;
        point.reference = {operation.owner_id,
            std::string("sweep:path-point:") + (start ? "start:from:" : "end:from:") + segment.source_id, {}};
        point.display_owner_id = operation.owner_id;
        result.push_back(std::move(point));
    }
    return result;
}

std::vector<ViewerAxis> axes_for_operation(
    const HistoryOperation& operation, const TopoDS_Shape& calculated_operand,
    const std::vector<ViewerEdge>& centerlines, bool authored_profile_axes=false,
    std::vector<ViewerPoint>* endpoints=nullptr) {
    const auto fitted_axis = [&](Vec3 point, Vec3 direction,
            std::string semantic_key, double fallback_length) {
        const double magnitude = std::hypot(
            std::hypot(direction.x, direction.y), direction.z);
        direction = {direction.x / magnitude, direction.y / magnitude,
                     direction.z / magnitude};
        if (!calculated_operand.IsNull()) {
            Bnd_Box bounds;
            BRepBndLib::Add(calculated_operand, bounds);
            if (!bounds.IsVoid()) {
                Standard_Real xmin{}, ymin{}, zmin{}, xmax{}, ymax{}, zmax{};
                bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
                double minimum = std::numeric_limits<double>::infinity();
                double maximum = -std::numeric_limits<double>::infinity();
                for (unsigned mask = 0; mask < 8; ++mask) {
                    const Vec3 corner{mask & 1 ? xmax : xmin,
                        mask & 2 ? ymax : ymin, mask & 4 ? zmax : zmin};
                    const double projection = (corner.x-point.x)*direction.x +
                        (corner.y-point.y)*direction.y +
                        (corner.z-point.z)*direction.z;
                    minimum = std::min(minimum, projection);
                    maximum = std::max(maximum, projection);
                }
                const double diagonal = std::hypot(
                    std::hypot(xmax-xmin, ymax-ymin), zmax-zmin);
                const double margin = std::max(1.0, diagonal * 1.0e-4);
                const double middle = (minimum + maximum) * 0.5;
                point = {point.x + direction.x * middle,
                         point.y + direction.y * middle,
                         point.z + direction.z * middle};
                fallback_length = maximum - minimum + 2.0 * margin;
            }
        }
        ViewerAxis axis{point, direction, std::max(1.0, fallback_length),
            {operation.owner_id, std::move(semantic_key)}};
        if(endpoints&&!calculated_operand.IsNull()) {
            // Reference points belong to the physical axial ends, not to the
            // display overhang. Measure in the axis frame without mesh padding
            // or shape tolerances (also correct for an oblique cylinder).
            gp_Trsf into_axis;
            into_axis.SetTransformation(gp_Ax3(gp_Pnt(point.x,point.y,point.z),
                gp_Dir(direction.x,direction.y,direction.z)));
            Bnd_Box axial;
            BRepBndLib::AddOptimal(BRepBuilderAPI_Transform(calculated_operand,into_axis,false).Shape(),axial,false,false);
            Standard_Real xmin{},ymin{},zmin{},xmax{},ymax{},zmax{};
            axial.Get(xmin,ymin,zmin,xmax,ymax,zmax);
            profile_centerlines::endpoints(*endpoints,operation.owner_id,axis.reference.semantic_key,
                dimension_add(axis.point,dimension_scale(axis.direction,zmin)),
                dimension_add(axis.point,dimension_scale(axis.direction,zmax)));
        }
        return axis;
    };
    return std::visit([&](const auto& primitive) {
        using Request = std::decay_t<decltype(primitive)>;
        if constexpr (std::is_same_v<Request, ExtrusionRequest>) {
            const double length = std::sqrt(
                primitive.direction.x * primitive.direction.x +
                primitive.direction.y * primitive.direction.y +
                primitive.direction.z * primitive.direction.z);
            const Vec3 direction{primitive.direction.x / length,
                primitive.direction.y / length, primitive.direction.z / length};
            std::vector<Vec3> centers;
            std::vector<std::string> profile_keys;
            const auto append_center = [&](const auto& profile,const std::string& boundary) {
                using Profile = std::decay_t<decltype(profile)>;
                if constexpr (std::is_same_v<Profile,
                                  ExtrusionRequest::CircleProfile> ||
                              std::is_same_v<Profile,
                                  ExtrusionRequest::EllipseProfile>) {
                    // feature_side_request scopes every boundary before
                    // calculation. Require that side ancestry explicitly:
                    // unscoped groups do not acquire new profile identities.
                    if(authored_profile_axes&&!feature_side_parent(boundary))return;
                    if (authored_profile_axes || std::none_of(centers.begin(), centers.end(),
                            [&](const auto& center) {
                                return std::hypot(std::hypot(
                                    center.x-profile.center.x,
                                    center.y-profile.center.y),
                                    center.z-profile.center.z) <= 1.0e-7;
                            })) {
                        centers.push_back(profile.center);
                        profile_keys.push_back("axis:profile:from:"+boundary);
                    }
                }
            };
            const auto append_loop = [&](const ExtrusionRequest::ProfileLoop& loop,const std::string& boundary) {
                std::visit([&](const auto& profile){append_center(profile,boundary);}, loop);
            };
            const auto append_region=[&](const auto& region) {
                append_loop(region.outer_profile,region.outer_boundary_id);
                for(std::size_t i=0;i<region.inner_profiles.size();++i)
                    append_loop(region.inner_profiles[i],i<region.inner_boundary_ids.size()?region.inner_boundary_ids[i]:std::string{});
            };
            append_region(primitive);
            for (const auto& region : primitive.additional_profile_regions) append_region(region);
            std::vector<ViewerAxis> axes;
            for (std::size_t index = 0; index < centers.size(); ++index) {
                axes.push_back(fitted_axis(centers[index], direction,
                    authored_profile_axes ? profile_keys[index] : index == 0 ? "axis:primary"
                               : "axis:profile:" + std::to_string(index + 1),
                    length));
            }
            return axes;
        } else if constexpr (std::is_same_v<Request, FeatureGroupRequest>) {
            auto axes = primitive.axes;
            const auto stationary_revolution=std::ranges::find_if(
                primitive.reference_profiles,[](const auto& profile) {
                    return std::holds_alternative<RevolutionRequest>(profile);
                });
            if(endpoints&&stationary_revolution!=
                    primitive.reference_profiles.end()) {
                HistoryOperation stationary_source{operation.owner_id,
                    std::get<RevolutionRequest>(*stationary_revolution)};
                const auto generated=axes_for_operation(stationary_source,
                    calculated_operand,{},false,nullptr);
                if(!generated.empty())
                    profile_centerlines::endpoints(*endpoints,
                        operation.owner_id,"axis:primary",
                        generated.front().point,generated.front().point);
            }
            const bool has_primary = std::ranges::any_of(
                axes, [](const auto& axis) {
                    return axis.reference.semantic_key == "axis:primary";
                });
            if (!has_primary) {
                const auto revolution = std::ranges::find_if(
                    primitive.children, [](const auto& child) {
                        return std::holds_alternative<RevolutionRequest>(child);
                    });
                const auto* request=revolution!=primitive.children.end()
                    ?&std::get<RevolutionRequest>(*revolution)
                    :stationary_revolution!=
                            primitive.reference_profiles.end()
                        ?&std::get<RevolutionRequest>(
                            *stationary_revolution):nullptr;
                if (request) {
                    HistoryOperation axis_source{operation.owner_id, *request};
                    auto generated = axes_for_operation(axis_source,
                        calculated_operand, {}, false, endpoints);
                    axes.insert(axes.end(),
                        std::make_move_iterator(generated.begin()),
                        std::make_move_iterator(generated.end()));
                }
            }
            return axes;
        } else if constexpr (std::is_same_v<Request, Sweep3DRequest>) {
            std::vector<ViewerAxis> axes;
            if(primitive.rotation_axis)axes.push_back(*primitive.rotation_axis);
            for(const auto& edge:centerlines) {
                const auto& start=edge.points.front();const auto& end=edge.points.back();
                const Vec3 delta{end.x-start.x,end.y-start.y,end.z-start.z};
                const double length=std::hypot(std::hypot(delta.x,delta.y),delta.z);
                if(!std::isfinite(length) || length<=1e-10)continue;
                const Vec3 d{delta.x/length,delta.y/length,delta.z/length};
                const bool straight=std::all_of(edge.points.begin(),edge.points.end(),[&](const auto& p){
                    const Vec3 v{p.x-start.x,p.y-start.y,p.z-start.z};
                    return std::hypot(std::hypot(v.y*d.z-v.z*d.y,v.z*d.x-v.x*d.z),v.x*d.y-v.y*d.x)<=1e-9*std::max(1.0,length);
                });
                if(!straight)continue;
                ViewerAxis axis{{(start.x+end.x)*.5,(start.y+end.y)*.5,(start.z+end.z)*.5},d,length+2.,
                    {operation.owner_id,edge.reference.semantic_key,{}}};
                axis.label="Osa dráhy";axes.push_back(std::move(axis));
            }
            return axes;
        } else if constexpr (std::is_same_v<Request, RevolutionRequest>) {
            const double length = std::max(10.0, std::sqrt(
                primitive.axis_direction.x * primitive.axis_direction.x +
                primitive.axis_direction.y * primitive.axis_direction.y +
                primitive.axis_direction.z * primitive.axis_direction.z));
            const double direction_length = std::sqrt(
                primitive.axis_direction.x * primitive.axis_direction.x +
                primitive.axis_direction.y * primitive.axis_direction.y +
                primitive.axis_direction.z * primitive.axis_direction.z);
            return std::vector<ViewerAxis>{fitted_axis(primitive.axis_point,
                {primitive.axis_direction.x / direction_length,
                 primitive.axis_direction.y / direction_length,
                 primitive.axis_direction.z / direction_length},
                "axis:primary", length)};
        } else {
            return std::vector<ViewerAxis>{};
        }
    }, operation.primitive);
}


PrimitiveData make_drill_point_data(const DrillPointRequest& request,
        const std::vector<std::pair<FaceReference,TopoDS_Face>>& bottom_faces, const TopoDS_Shape& body,
        const std::string& owner_id,
        const std::vector<std::pair<FaceReference,SurfaceGeometry>>& sweep_ends = {}) {
    if (request.bottom_faces.empty() ||
        !std::isfinite(request.included_angle_degrees) ||
        request.included_angle_degrees <= 0.0 ||
        request.included_angle_degrees >= 180.0) {
        throw std::invalid_argument("Drill-point parameters are invalid");
    }
    TopoDS_Compound cones;
    BRep_Builder builder;
    builder.MakeCompound(cones);
    PrimitiveData result{cones, {}, {}, {}};
    for (const auto& [reference,bottom_face] : bottom_faces) {
        BRepAdaptor_Surface surface(bottom_face);
        if (surface.GetType() != GeomAbs_Plane) continue;
        std::vector<gp_Circ> circles;
        for (TopExp_Explorer explorer(bottom_face, TopAbs_EDGE);
             explorer.More(); explorer.Next()) {
            BRepAdaptor_Curve curve(TopoDS::Edge(explorer.Current()));
            if (curve.GetType() == GeomAbs_Circle) {
                const auto circle=curve.Circle();
                // Boolean clipping may split one rim into several arcs. Their
                // analytical support still identifies the same bore; distinct
                // circles remain ambiguous and must not be guessed.
                if(std::ranges::none_of(circles,[&](const auto& previous) {
                    return previous.Location().Distance(circle.Location())<=1e-7 &&
                        std::abs(previous.Radius()-circle.Radius())<=1e-7 &&
                        previous.Axis().Direction().IsParallel(circle.Axis().Direction(),1e-9);
                }))circles.push_back(circle);
            }
        }
        if (circles.size() != 1 || !(circles.front().Radius() > 1.0e-9))
            continue;
        const auto circle = circles.front();
        const gp_Pnt center = circle.Location();
        gp_Dir normal = surface.Plane().Axis().Direction();
        if (bottom_face.Orientation() == TopAbs_REVERSED) normal.Reverse();
        const double probe = std::max(1.0e-5, circle.Radius() * 1.0e-4);
        const auto material_side=[&](const gp_Pnt& point)->std::optional<bool> {
            if(BRepClass_FaceClassifier(bottom_face,point,1e-7).State()!=TopAbs_IN)return std::nullopt;
            BRepClass3d_SolidClassifier positive(body,point.Translated(gp_Vec(normal)*probe),1e-7);
            BRepClass3d_SolidClassifier negative(body,point.Translated(gp_Vec(normal)*-probe),1e-7);
            const bool a=positive.State()==TopAbs_IN,b=negative.State()==TopAbs_IN;
            return a!=b?std::optional<bool>{a}:std::nullopt;
        };
        auto positive_inside=material_side(center);
        if(!positive_inside) {
            // A partial floor can exclude the bore axis altogether. Probe the
            // actual retained face, using its existing tessellation only to
            // find an interior location; exact face/solid classifiers decide.
            TopLoc_Location location;
            const auto triangles=BRep_Tool::Triangulation(bottom_face,location);
            if(!triangles.IsNull())for(int i=1;i<=triangles->NbTriangles()&&!positive_inside;++i) {
                int a,b,c;triangles->Triangle(i).Get(a,b,c);
                const auto point=gp_Pnt((triangles->Node(a).XYZ()+triangles->Node(b).XYZ()+triangles->Node(c).XYZ())/3.);
                positive_inside=material_side(point.Transformed(location.Transformation()));
            }
        }
        if(!positive_inside)continue;
        if(!*positive_inside)normal.Reverse();
        const double depth = circle.Radius() / std::tan(
            request.included_angle_degrees * std::numbers::pi / 360.0);
        const TopoDS_Shape cone = BRepPrimAPI_MakeCone(
            gp_Ax2(center, normal), circle.Radius(), 0.0, depth).Shape();
        if (cone.IsNull() || !BRepCheck_Analyzer(cone).IsValid()) continue;
        builder.Add(cones, cone);
        for (TopExp_Explorer explorer(cone, TopAbs_FACE);
             explorer.More(); explorer.Next()) {
            const auto face = TopoDS::Face(explorer.Current());
            const auto kind = BRepAdaptor_Surface(face).GetType();
            result.faces.push_back({face, {owner_id, drill_point_key(kind == GeomAbs_Cone ? "side" : "base",reference)}});
        }
        for (TopExp_Explorer explorer(cone, TopAbs_EDGE);
             explorer.More(); explorer.Next()) {
            const auto edge = TopoDS::Edge(explorer.Current());
            if (BRepAdaptor_Curve(edge).GetType() == GeomAbs_Circle) {
                result.edges.push_back({edge,
                    {owner_id, drill_point_key("base-circle",reference)}});
            }
        }
    }
    for (const auto& [reference, cap] : sweep_ends) {
        if (!(cap.radius > 1e-9)) continue;
        const double depth = cap.radius / std::tan(
            request.included_angle_degrees * std::numbers::pi / 360.0);
        const auto cone = BRepPrimAPI_MakeCone(gp_Ax2(
            gp_Pnt(cap.origin.x,cap.origin.y,cap.origin.z),
            gp_Dir(cap.axis.x,cap.axis.y,cap.axis.z)),cap.radius,0,depth).Shape();
        if (cone.IsNull() || !BRepCheck_Analyzer(cone).IsValid())
            throw std::runtime_error("Nelze vytvořit špičku konce Sweep/Loftu");
        builder.Add(cones,cone);
        for (TopExp_Explorer faces(cone,TopAbs_FACE);faces.More();faces.Next()) {
            const auto face=TopoDS::Face(faces.Current());
            const auto kind=BRepAdaptor_Surface(face).GetType();
            result.faces.push_back({face,{owner_id,
                drill_point_key(kind==GeomAbs_Cone?"side":"base",reference)}});
        }
        for (TopExp_Explorer edges(cone,TopAbs_EDGE);edges.More();edges.Next()) {
            const auto edge=TopoDS::Edge(edges.Current());
            if(BRepAdaptor_Curve(edge).GetType()==GeomAbs_Circle)
                result.edges.push_back({edge,{owner_id,drill_point_key("base-circle",reference)}});
        }
    }
    if (result.faces.empty())
        throw std::runtime_error("No valid circular drill-point bottom remains");
    return result;
}


struct ExtrusionLimitView {
    bool planar;
    const FaceReference& reference;
    bool datum;
    Vec3 origin;
    Vec3 normal;
    const std::vector<Vec3>& triangles;
};
bool has_forward_limit(const ExtrusionRequest& request) {
    return request.extent==ExtrusionRequest::Extent::UpToPlane || request.extent==ExtrusionRequest::Extent::UpToSurface;
}
ExtrusionLimitView forward_limit(const ExtrusionRequest& request) {
    return {request.extent==ExtrusionRequest::Extent::UpToPlane,request.target_face,request.target_is_datum,
        request.target_plane_origin,request.target_plane_normal,request.target_surface_triangles};
}
ExtrusionLimitView limit_view(const ExtrusionLimit& value) {
    return {value.planar,value.reference,value.datum,value.origin,value.normal,value.triangles};
}
void validate_extrusion_limit(const ExtrusionLimitView& limit,const Vec3& direction) {
    if (limit.planar) {
        const double normal_length = std::sqrt(
            limit.normal.x * limit.normal.x +
            limit.normal.y * limit.normal.y +
            limit.normal.z * limit.normal.z);
        const double dot = direction.x * limit.normal.x +
            direction.y * limit.normal.y +
            direction.z * limit.normal.z;
        if (!limit.reference.valid() || !limit.reference.instance_path.empty() ||
            !std::isfinite(limit.origin.x) ||
            !std::isfinite(limit.origin.y) ||
            !std::isfinite(limit.origin.z) ||
            !std::isfinite(normal_length) || normal_length <= 1.0e-12 ||
            std::abs(dot) <= 1.0e-12) {
            throw std::invalid_argument("Extrusion target plane is invalid or parallel");
        }
    } else {
        if (!limit.reference.valid() || !limit.reference.instance_path.empty() || limit.datum ||
            limit.triangles.empty() ||
            limit.triangles.size() % 3 != 0 ||
            std::any_of(limit.triangles.begin(),
                limit.triangles.end(), [](const auto& point) {
                    return !std::isfinite(point.x) || !std::isfinite(point.y) ||
                           !std::isfinite(point.z);
                })) {
            throw std::invalid_argument("Extrusion target surface is invalid");
        }
    }
}

void validate_extrusion(const ExtrusionRequest& request, bool allow_open_profile = false) {
    allow_open_profile = allow_open_profile || request.wall.has_value() || request.surface_result;
    const auto validate_profile = [&](const auto& profile_variant) {
        std::visit([&](const auto& profile) {
            using Profile = std::decay_t<decltype(profile)>;
            if constexpr (std::is_same_v<Profile,
                              ExtrusionRequest::PolygonProfile>) {
                if (profile.vertices.size() < 3) {
                    throw std::invalid_argument(
                        "Extrusion profile requires at least three vertices");
                }
                for (const auto& point : profile.vertices) {
                    if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                        !std::isfinite(point.z)) {
                        throw std::invalid_argument(
                            "Extrusion profile coordinates must be finite");
                    }
                }
            } else if constexpr (std::is_same_v<Profile,
                                     ExtrusionRequest::CircleProfile>) {
                if (!std::isfinite(profile.center.x) ||
                       !std::isfinite(profile.center.y) ||
                       !std::isfinite(profile.center.z) ||
                       !std::isfinite(profile.radius) || profile.radius <= 0.0) {
                    throw std::invalid_argument(
                        "Extrusion Circle must have a finite center and positive radius");
                }
            } else if constexpr (std::is_same_v<Profile,
                                     ExtrusionRequest::EllipseProfile>) {
                const double axis_length = std::sqrt(
                    profile.major_axis_direction.x * profile.major_axis_direction.x +
                    profile.major_axis_direction.y * profile.major_axis_direction.y +
                    profile.major_axis_direction.z * profile.major_axis_direction.z);
                const double normal_length = std::sqrt(
                    request.direction.x * request.direction.x +
                    request.direction.y * request.direction.y +
                    request.direction.z * request.direction.z);
                const double orthogonality = std::abs(
                    profile.major_axis_direction.x * request.direction.x +
                    profile.major_axis_direction.y * request.direction.y +
                    profile.major_axis_direction.z * request.direction.z);
                if (!std::isfinite(profile.center.x) ||
                    !std::isfinite(profile.center.y) ||
                    !std::isfinite(profile.center.z) ||
                    !std::isfinite(axis_length) || axis_length <= 1.0e-12 ||
                    !std::isfinite(normal_length) || normal_length <= 1.0e-12 ||
                    orthogonality > 1.0e-9 * axis_length * normal_length ||
                    !std::isfinite(profile.major_radius) ||
                    !std::isfinite(profile.minor_radius) ||
                    profile.major_radius <= 0.0 || profile.minor_radius <= 0.0 ||
                    profile.major_radius < profile.minor_radius) {
                    throw std::invalid_argument(
                        "Extrusion Ellipse must have finite axes and positive ordered radii");
                }
            } else {
                const bool single_closed_spline = profile.curves.size() == 1 &&
                    std::holds_alternative<ExtrusionRequest::BSplineCurve>(profile.curves.front()) &&
                    [&] {
                        const auto& curve=std::get<ExtrusionRequest::BSplineCurve>(profile.curves.front());
                        return curve.periodic || (!curve.knots.empty() &&
                            std::hypot(curve.start.x-curve.end.x,curve.start.y-curve.end.y,curve.start.z-curve.end.z)<=1e-12);
                    }();
                if (profile.curves.empty() || (profile.curves.size() < 2 && !single_closed_spline && !allow_open_profile)) {
                    throw std::invalid_argument(
                        "Curved Extrusion profile requires at least two curves");
                }
                const auto finite = [](const Vec3& point) {
                    return std::isfinite(point.x) && std::isfinite(point.y) &&
                        std::isfinite(point.z);
                };
                const auto distance = [](const Vec3& first, const Vec3& second) {
                    return std::sqrt(
                        (first.x - second.x) * (first.x - second.x) +
                        (first.y - second.y) * (first.y - second.y) +
                        (first.z - second.z) * (first.z - second.z));
                };
                std::optional<Vec3> first_start;
                std::optional<Vec3> previous_end;
                for (const auto& curve : profile.curves) {
                    std::visit([&](const auto& exact_curve) {
                        if (!finite(exact_curve.start) || !finite(exact_curve.end)) {
                            throw std::invalid_argument(
                                "Extrusion curve coordinates must be finite");
                        }
                        if constexpr (std::is_same_v<
                                          std::decay_t<decltype(exact_curve)>,
                                          ExtrusionRequest::ArcCurve>) {
                            if (!finite(exact_curve.middle)) {
                                throw std::invalid_argument(
                                    "Extrusion Arc coordinates must be finite");
                            }
                            const Vec3 first_vector{
                                exact_curve.middle.x - exact_curve.start.x,
                                exact_curve.middle.y - exact_curve.start.y,
                                exact_curve.middle.z - exact_curve.start.z};
                            const Vec3 second_vector{
                                exact_curve.end.x - exact_curve.start.x,
                                exact_curve.end.y - exact_curve.start.y,
                                exact_curve.end.z - exact_curve.start.z};
                            const Vec3 cross{
                                first_vector.y * second_vector.z -
                                    first_vector.z * second_vector.y,
                                first_vector.z * second_vector.x -
                                    first_vector.x * second_vector.z,
                                first_vector.x * second_vector.y -
                                    first_vector.y * second_vector.x};
                            if (std::sqrt(cross.x * cross.x + cross.y * cross.y +
                                          cross.z * cross.z) <= 1.0e-12) {
                                throw std::invalid_argument(
                                    "Extrusion Arc points must not be collinear");
                            }
                        }
                        if constexpr (std::is_same_v<
                                          std::decay_t<decltype(exact_curve)>,
                                          ExtrusionRequest::EllipticalArcCurve>) {
                            const double axis_length = std::sqrt(
                                exact_curve.major_axis_direction.x *
                                    exact_curve.major_axis_direction.x +
                                exact_curve.major_axis_direction.y *
                                    exact_curve.major_axis_direction.y +
                                exact_curve.major_axis_direction.z *
                                    exact_curve.major_axis_direction.z);
                            const double normal_length = std::sqrt(
                                request.direction.x * request.direction.x +
                                request.direction.y * request.direction.y +
                                request.direction.z * request.direction.z);
                            const double orthogonality = std::abs(
                                exact_curve.major_axis_direction.x * request.direction.x +
                                exact_curve.major_axis_direction.y * request.direction.y +
                                exact_curve.major_axis_direction.z * request.direction.z);
                            if (!finite(exact_curve.center) ||
                                !finite(exact_curve.major_axis_direction) ||
                                !std::isfinite(axis_length) || axis_length <= 1.0e-12 ||
                                !std::isfinite(normal_length) || normal_length <= 1.0e-12 ||
                                orthogonality > 1.0e-9 * axis_length * normal_length ||
                                !std::isfinite(exact_curve.major_radius) ||
                                !std::isfinite(exact_curve.minor_radius) ||
                                !std::isfinite(exact_curve.start_parameter) ||
                                !std::isfinite(exact_curve.end_parameter) ||
                                exact_curve.major_radius <= 0.0 ||
                                exact_curve.minor_radius <= 0.0 ||
                                exact_curve.major_radius < exact_curve.minor_radius ||
                                exact_curve.end_parameter <= exact_curve.start_parameter ||
                                exact_curve.end_parameter - exact_curve.start_parameter >=
                                    2.0 * std::numbers::pi) {
                                throw std::invalid_argument(
                                    "Extrusion elliptical Arc definition is invalid");
                            }
                            const double orientation = exact_curve.reversed ? -1.0 : 1.0;
                            const Vec3 axis{
                                exact_curve.major_axis_direction.x / axis_length,
                                exact_curve.major_axis_direction.y / axis_length,
                                exact_curve.major_axis_direction.z / axis_length};
                            const Vec3 normal{
                                orientation * request.direction.x / normal_length,
                                orientation * request.direction.y / normal_length,
                                orientation * request.direction.z / normal_length};
                            const Vec3 side{
                                normal.y * axis.z - normal.z * axis.y,
                                normal.z * axis.x - normal.x * axis.z,
                                normal.x * axis.y - normal.y * axis.x};
                            const auto point_at = [&](double parameter) {
                                return Vec3{
                                    exact_curve.center.x + exact_curve.major_radius *
                                        std::cos(parameter) * axis.x +
                                        exact_curve.minor_radius * std::sin(parameter) *
                                            side.x,
                                    exact_curve.center.y + exact_curve.major_radius *
                                        std::cos(parameter) * axis.y +
                                        exact_curve.minor_radius * std::sin(parameter) *
                                            side.y,
                                    exact_curve.center.z + exact_curve.major_radius *
                                        std::cos(parameter) * axis.z +
                                        exact_curve.minor_radius * std::sin(parameter) *
                                            side.z};
                            };
                            const auto expected_start = point_at(
                                exact_curve.start_parameter);
                            const auto expected_end = point_at(
                                exact_curve.end_parameter);
                            const double coordinate_tolerance = 1.0e-7 * std::max(
                                {1.0, exact_curve.major_radius,
                                 exact_curve.minor_radius});
                            if (distance(expected_start, exact_curve.start) >
                                    coordinate_tolerance ||
                                distance(expected_end, exact_curve.end) >
                                    coordinate_tolerance) {
                                throw std::invalid_argument(
                                    "Extrusion elliptical Arc endpoints are inconsistent");
                            }
                        }
                        if constexpr (std::is_same_v<
                                          std::decay_t<decltype(exact_curve)>,
                                          ExtrusionRequest::BSplineCurve>) {
                            if (exact_curve.degree < 1 ||
                                exact_curve.control_points.size() <
                                    static_cast<std::size_t>(exact_curve.degree) + 1 ||
                                std::any_of(exact_curve.control_points.begin(),
                                    exact_curve.control_points.end(),
                                    [&](const auto& point) { return !finite(point); })) {
                                throw std::invalid_argument(
                                    "Extrusion B-spline definition is invalid");
                            }
                        }
                        if (distance(exact_curve.start, exact_curve.end) <= 1.0e-12 &&
                            !std::is_same_v<std::decay_t<decltype(exact_curve)>,
                                ExtrusionRequest::BSplineCurve>) {
                            throw std::invalid_argument(
                                "Extrusion curve must have distinct endpoints");
                        }
                        if (!first_start) first_start = exact_curve.start;
                        if (previous_end &&
                            distance(*previous_end, exact_curve.start) > 1.0e-7) {
                            throw std::invalid_argument(
                                "Extrusion Curved profile is not connected");
                        }
                        previous_end = exact_curve.end;
                    }, curve);
                }
                if (!first_start || !previous_end ||
                    (!allow_open_profile && distance(*previous_end, *first_start) > 1.0e-7)) {
                    throw std::invalid_argument(
                        "Extrusion Curved profile is not closed");
                }
            }
        }, profile_variant);
    };
    validate_profile(request.outer_profile);
    for (const auto& profile : request.inner_profiles) validate_profile(profile);
    for (const auto& region : request.additional_profile_regions) {
        validate_profile(region.outer_profile);
        for (const auto& profile : region.inner_profiles) validate_profile(profile);
    }
    const double length = std::sqrt(
        request.direction.x * request.direction.x +
        request.direction.y * request.direction.y +
        request.direction.z * request.direction.z);
    if (!std::isfinite(length) || length <= 1.0e-12) {
        throw std::invalid_argument("Extrusion direction must be finite and non-zero");
    }
    if(request.symmetric_limit && (!has_forward_limit(request)||request.reverse_limit||request.through_all_reverse))
        throw std::invalid_argument("A symmetric extrusion end requires one forward target.");
    if(request.mirror_forward_limit && ((!has_forward_limit(request)&&!request.ordered_end_limit)||request.symmetric_limit))
        throw std::invalid_argument("A symmetric extrusion end requires one forward target.");
    if(request.ordered_end_limit&&(request.extent!=ExtrusionRequest::Extent::Blind||
            request.reverse_limit||request.symmetric_limit||request.through_all_reverse||request.sheet_cut))
        throw detail::OrderedLimitError(detail::OrderedLimitFailure::InvalidInput,0,0);
    if(has_forward_limit(request))validate_extrusion_limit(forward_limit(request),request.direction);
    if(request.reverse_limit) {
        validate_extrusion_limit(limit_view(*request.reverse_limit),request.direction);
        if(request.through_all_reverse)throw std::invalid_argument("An extrusion end cannot be both Up-to and Through-all");
    }
}

TopoDS_Wire make_profile_wire(
    const ExtrusionRequest::ProfileLoop& profile_variant,
    const Vec3& profile_normal,
    const std::optional<Vec3>& circle_radial_direction = std::nullopt,
    std::vector<TopoDS_Edge>* source_edges = nullptr) {
    return std::visit([&](const auto& profile) {
        using Profile = std::decay_t<decltype(profile)>;
        if constexpr (std::is_same_v<Profile,
                          ExtrusionRequest::PolygonProfile>) {
            if (source_edges) {
                BRepBuilderAPI_MakeWire wire;
                std::vector<TopoDS_Vertex> vertices;
                for (const auto& p : profile.vertices) vertices.push_back(BRepBuilderAPI_MakeVertex(gp_Pnt(p.x,p.y,p.z)).Vertex());
                for (std::size_t i=0;i<vertices.size();++i) {
                    auto edge=BRepBuilderAPI_MakeEdge(vertices[i],vertices[(i+1)%vertices.size()]).Edge();
                    wire.Add(edge);source_edges->push_back(edge);
                }
                if(!wire.IsDone())throw std::runtime_error("Invalid polygon wire");
                return wire.Wire();
            }
            BRepBuilderAPI_MakePolygon polygon;
            for (const auto& point : profile.vertices) {
                polygon.Add(gp_Pnt(point.x, point.y, point.z));
            }
            polygon.Close();
            if (!polygon.IsDone()) {
                throw std::runtime_error("OCCT polygon profile wire failed");
            }
            return polygon.Wire();
        } else if constexpr (std::is_same_v<Profile,
                                 ExtrusionRequest::CircleProfile>) {
            const gp_Dir normal(
                profile_normal.x, profile_normal.y, profile_normal.z);
            gp_Ax2 frame(gp_Pnt(profile.center.x, profile.center.y, profile.center.z), normal);
            if (circle_radial_direction) {
                frame.SetXDirection(gp_Dir(circle_radial_direction->x,
                    circle_radial_direction->y, circle_radial_direction->z));
            }
            BRepBuilderAPI_MakeEdge edge(gp_Circ(frame, profile.radius));
            if (!edge.IsDone()) {
                throw std::runtime_error("OCCT circular profile edge failed");
            }
            BRepBuilderAPI_MakeWire circle_wire(edge.Edge());
            if (!circle_wire.IsDone()) {
                throw std::runtime_error("OCCT circular profile wire failed");
            }
            if(source_edges)source_edges->push_back(edge.Edge());
            return circle_wire.Wire();
        } else if constexpr (std::is_same_v<Profile,
                                 ExtrusionRequest::EllipseProfile>) {
            const gp_Dir normal(
                profile_normal.x, profile_normal.y, profile_normal.z);
            const gp_Dir major_direction(
                profile.major_axis_direction.x,
                profile.major_axis_direction.y,
                profile.major_axis_direction.z);
            BRepBuilderAPI_MakeEdge edge(gp_Elips(
                gp_Ax2(gp_Pnt(profile.center.x, profile.center.y, profile.center.z),
                       normal, major_direction),
                profile.major_radius, profile.minor_radius));
            if (!edge.IsDone()) {
                throw std::runtime_error("OCCT elliptical profile edge failed");
            }
            BRepBuilderAPI_MakeWire ellipse_wire(edge.Edge());
            if (!ellipse_wire.IsDone()) {
                throw std::runtime_error("OCCT elliptical profile wire failed");
            }
            if(source_edges)source_edges->push_back(edge.Edge());
            return ellipse_wire.Wire();
        } else {
            BRepBuilderAPI_MakeWire curved_wire;
            for (const auto& curve : profile.curves) {
                const TopoDS_Edge edge = std::visit([&](const auto& exact_curve) {
                    using Curve = std::decay_t<decltype(exact_curve)>;
                    if constexpr (std::is_same_v<Curve,
                                      ExtrusionRequest::LineCurve>) {
                        return BRepBuilderAPI_MakeEdge(
                            gp_Pnt(exact_curve.start.x, exact_curve.start.y,
                                   exact_curve.start.z),
                            gp_Pnt(exact_curve.end.x, exact_curve.end.y,
                                   exact_curve.end.z)).Edge();
                    } else if constexpr (std::is_same_v<Curve,
                                             ExtrusionRequest::ArcCurve>) {
                        GC_MakeArcOfCircle arc(
                            gp_Pnt(exact_curve.start.x, exact_curve.start.y,
                                   exact_curve.start.z),
                            gp_Pnt(exact_curve.middle.x, exact_curve.middle.y,
                                   exact_curve.middle.z),
                            gp_Pnt(exact_curve.end.x, exact_curve.end.y,
                                   exact_curve.end.z));
                        if (!arc.IsDone()) {
                            throw std::runtime_error("OCCT profile Arc failed");
                        }
                        return BRepBuilderAPI_MakeEdge(arc.Value()).Edge();
                    } else if constexpr (std::is_same_v<Curve,
                                             ExtrusionRequest::EllipticalArcCurve>) {
                        gp_Dir normal(
                            profile_normal.x, profile_normal.y, profile_normal.z);
                        if (exact_curve.reversed) normal.Reverse();
                        const gp_Dir major_direction(
                            exact_curve.major_axis_direction.x,
                            exact_curve.major_axis_direction.y,
                            exact_curve.major_axis_direction.z);
                        const gp_Elips ellipse(
                            gp_Ax2(gp_Pnt(
                                exact_curve.center.x, exact_curve.center.y,
                                exact_curve.center.z), normal, major_direction),
                            exact_curve.major_radius, exact_curve.minor_radius);
                        BRepBuilderAPI_MakeEdge edge(
                            ellipse, exact_curve.start_parameter,
                            exact_curve.end_parameter);
                        if (!edge.IsDone()) {
                            throw std::runtime_error(
                                "OCCT profile elliptical Arc failed");
                        }
                        return edge.Edge();
                    } else {
                        const Standard_Integer pole_count =
                            static_cast<Standard_Integer>(exact_curve.control_points.size());
                        if (exact_curve.interpolating) {
                            Handle(TColgp_HArray1OfPnt) points =
                                new TColgp_HArray1OfPnt(1, pole_count);
                            for (Standard_Integer index = 1; index <= pole_count; ++index) {
                                const auto& point = exact_curve.control_points[
                                    static_cast<std::size_t>(index - 1)];
                                points->SetValue(index,
                                    gp_Pnt(point.x, point.y, point.z));
                            }
                            GeomAPI_Interpolate interpolation(
                                points, exact_curve.periodic, 1.0e-9);
                            interpolation.Perform();
                            if (!interpolation.IsDone()) {
                                throw std::runtime_error(
                                    "OCCT profile interpolating spline failed");
                            }
                            BRepBuilderAPI_MakeEdge edge(interpolation.Curve());
                            if (!edge.IsDone()) {
                                throw std::runtime_error(
                                    "OCCT profile interpolating spline edge failed");
                            }
                            return edge.Edge();
                        }
                        if (!exact_curve.knots.empty()) {
                            BSplineGeometry data{exact_curve.degree, exact_curve.control_points,
                                exact_curve.knots, exact_curve.weights};
                            const auto exact=restore_bspline_geometry(data);
                            BRepBuilderAPI_MakeEdge edge(exact);
                            if (!edge.IsDone()) throw std::runtime_error("Exact rational profile failed");
                            return edge.Edge();
                        }
                        TColgp_Array1OfPnt poles(1, pole_count);
                        for (Standard_Integer index = 1; index <= pole_count; ++index) {
                            const auto& point = exact_curve.control_points[
                                static_cast<std::size_t>(index - 1)];
                            poles.SetValue(index, gp_Pnt(point.x, point.y, point.z));
                        }
                        const Standard_Integer knot_count = exact_curve.periodic
                            ? pole_count + 1 : pole_count -
                                static_cast<Standard_Integer>(exact_curve.degree) + 1;
                        TColStd_Array1OfReal knots(1, knot_count);
                        TColStd_Array1OfInteger multiplicities(1, knot_count);
                        for (Standard_Integer index = 1; index <= knot_count; ++index) {
                            knots.SetValue(index, static_cast<double>(index - 1));
                            multiplicities.SetValue(index, exact_curve.periodic ? 1
                                : (index == 1 || index == knot_count)
                                    ? static_cast<Standard_Integer>(exact_curve.degree) + 1
                                    : 1);
                        }
                        Handle(Geom_BSplineCurve) bspline = new Geom_BSplineCurve(
                            poles, knots, multiplicities,
                            static_cast<Standard_Integer>(exact_curve.degree),
                            exact_curve.periodic);
                        BRepBuilderAPI_MakeEdge edge(bspline);
                        if (!edge.IsDone()) {
                            throw std::runtime_error("OCCT profile B-spline failed");
                        }
                        return edge.Edge();
                    }
                }, curve);
                curved_wire.Add(edge);
                if(source_edges)source_edges->push_back(curved_wire.Edge());
            }
            if (!curved_wire.IsDone()) {
                throw std::runtime_error("OCCT curved profile wire failed");
            }
            const auto wire=curved_wire.Wire();
            if(source_edges){
                // Joining independently built curves may replace an earlier
                // edge to merge its endpoint. Recover that source's final edge
                // by its shared curve handle, never by wire traversal position.
                for(auto& source:*source_edges){
                    Standard_Real a,b;const auto curve=BRep_Tool::Curve(source,a,b);
                    TopoDS_Edge match;unsigned count=0;
                    for(TopExp_Explorer it(wire,TopAbs_EDGE);it.More();it.Next()){
                        const auto edge=TopoDS::Edge(it.Current());Standard_Real first,last;
                        if(BRep_Tool::Curve(edge,first,last)==curve&&std::abs(first-a)<1e-9&&std::abs(last-b)<1e-9){match=edge;++count;}
                    }
                    if(count!=1)throw std::runtime_error("Nelze určit zdrojovou hranu průřezu");
                    source=TopoDS::Edge(match.Oriented(source.Orientation()));
                }
            }
            return wire;
        }
    }, profile_variant);
}

#include "boundary_surface_geometry.inc"

void validate_sweep3d(const Sweep3DRequest& request) {
    if(request.twist) {
        if(request.sections.size()!=1||!request.make_solid||request.thin)
            throw std::invalid_argument("Invalid Feature definition.");
        return; // expand_profile_twist validates the generated ordinary request.
    }
    if (!std::isfinite(request.linear_tolerance) || request.linear_tolerance <= 0)
        throw std::invalid_argument("Invalid sweep tolerance");
    if (request.path_points.size() < 2 ||
        request.path_point_ids.size() != request.path_points.size() ||
        (request.separate_segments ? request.path_segments.size() * 2
                                  : request.path_segments.size() + 1) != request.path_points.size()) {
        throw std::invalid_argument(
            "3D Sweep path requires aligned Points and segments");
    }
    if (request.sections.empty()) {
        throw std::invalid_argument("3D Sweep requires at least one section");
    }
    if(request.separate_segments&&request.transported)
        throw std::invalid_argument("Separate Sweep segments cannot use a single transported profile");
    std::unordered_set<std::size_t> section_locations;
    for (const auto& section : request.sections) {
        if (section.profile_id.empty() || section.point_id.empty() ||
            section.point_index >= request.path_points.size() ||
            request.path_point_ids[section.point_index] != section.point_id ||
            !section_locations.insert(section.point_index).second) {
            throw std::invalid_argument(
                "3D Sweep section has an invalid or duplicate Point relation");
        }
        const double normal_length = std::hypot(std::hypot(
            section.profile_normal.x, section.profile_normal.y),
            section.profile_normal.z);
        if (!std::isfinite(normal_length) || normal_length <= 1.0e-12) {
            throw std::invalid_argument(
                "3D Sweep section has an invalid Sketch plane");
        }
        ExtrusionRequest profile_request;
        profile_request.outer_profile = section.profile.outer_profile;
        profile_request.inner_profiles = section.profile.inner_profiles;
        profile_request.direction = {0.0, 0.0, 1.0};
        validate_extrusion(profile_request, request.thin || !request.make_solid);
    }
    for (std::size_t index = 0; index < request.path_segments.size(); ++index) {
        const auto& segment = request.path_segments[index];
        if (segment.source_id.empty() ||
            (segment.bezier_control_points.size() != 0 &&
             segment.bezier_control_points.size() != 4)) {
            throw std::invalid_argument("3D Sweep path segment is invalid");
        }
        const double distance = std::hypot(std::hypot(
            segment.end.x-segment.start.x,
            segment.end.y-segment.start.y),
            segment.end.z-segment.start.z);
        if (!std::isfinite(distance) || distance <= 1.0e-9) {
            throw std::invalid_argument(
                "3D Sweep path contains a zero-length segment");
        }
    }
}

struct SweepProfileWire {
    TopoDS_Wire wire;
    std::vector<TopoDS_Edge> edges;
    std::vector<std::string> curve_ids,point_ids;
    std::optional<double> circular_radius;
};

// Two-shape Boolean constructors calculate immediately, before tolerances
// and history settings can be applied. Configure once, then explicitly Build.
template<class Algorithm>
void set_boolean_inputs(Algorithm& algorithm, const TopoDS_Shape& argument,
                        const TopoDS_Shape& tool) {
    TopTools_ListOfShape arguments, tools;
    arguments.Append(argument); tools.Append(tool);
    algorithm.SetArguments(arguments); algorithm.SetTools(tools);
    // OCCT's worker scheduler follows the hardware available on this host.
    // Its setup cost is measurable on ordinary small Features, so enable it
    // only once the input topology is large enough to amortize that work.
    // The test overrides compare both execution modes on identical geometry;
    // neither setting changes tolerance, history capture or ZIMA identities.
    constexpr std::size_t parallel_face_threshold=256;
    bool parallel=std::getenv("ZIMA_CPP_TEST_FORCE_PARALLEL_BOOLEANS")!=nullptr;
    if(!parallel&&std::getenv("ZIMA_CPP_TEST_FORCE_SERIAL_BOOLEANS")==nullptr&&
            std::thread::hardware_concurrency()>1) {
        std::size_t faces{};
        const auto complex=[&](const TopoDS_Shape& shape) {
            for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())
                if(++faces>=parallel_face_threshold)return true;
            return false;
        };
        parallel=complex(argument)||complex(tool);
    }
    algorithm.SetRunParallel(parallel);
}

PrimitiveData make_transported_sweep_data(const Sweep3DRequest& request,const std::string& owner_id,
    const SweepProfileWire* prepared=nullptr) {
    if(request.sections.size()!=1)throw std::runtime_error("Sweep vyžaduje jeden průřez");
    GeomConvert_CompCurveToBSplineCurve curve;
    for(const auto& segment:request.path_segments){
        TColgp_Array1OfPnt poles(1,4);
        for(int i=0;i<4;++i){const auto& p=segment.bezier_control_points.at(i);poles.SetValue(i+1,gp_Pnt(p.x,p.y,p.z));}
        Handle(Geom_BezierCurve) part=new Geom_BezierCurve(poles);
        if(!curve.Add(part,1e-7,true,true,1))throw std::runtime_error("Nelze spojit prostorovou dráhu");
    }
    auto spine_edge=BRepBuilderAPI_MakeEdge(curve.BSplineCurve()).Edge();
    auto spine=BRepBuilderAPI_MakeWire(spine_edge).Wire();
    BRepOffsetAPI_MakePipeShell builder(spine);
    builder.SetDiscreteMode();builder.SetTolerance(request.linear_tolerance/2,request.linear_tolerance/2,1e-6);
    builder.SetMaxSegments(2000);
    auto section=request.sections.front();
    std::vector<TopoDS_Edge> profile_edges;
    auto profile=prepared?prepared->wire:make_profile_wire(section.profile.outer_profile,section.profile_normal,std::nullopt,&profile_edges);
    if(prepared){profile_edges=prepared->edges;section.profile.outer_edge_source_ids=prepared->curve_ids;section.profile.outer_vertex_source_ids=prepared->point_ids;}
    builder.Add(profile,false,false);builder.Build();
    if(!builder.IsDone()||(request.make_solid&&!builder.MakeSolid())||!BRepCheck_Analyzer(builder.Shape()).IsValid())
        throw std::runtime_error("Sweep nevytvořil platné těleso; zkontrolujte průřez a dráhu");
    PrimitiveData result{builder.Shape(),{},{},{}};
    if(profile_edges.size()!=section.profile.outer_edge_source_ids.size())throw std::runtime_error("Chybí rodiče hran průřezu");
    const auto key=[](const std::string& role,const std::string& source){return role+":from:"+std::to_string(source.size())+":"+source;};
    for(std::size_t i=0;i<profile_edges.size();++i){
        const auto& source=section.profile.outer_edge_source_ids[i];
        const auto& shapes=builder.Generated(profile_edges[i]);
        for(TopTools_ListIteratorOfListOfShape it(shapes);it.More();it.Next())
            if(it.Value().ShapeType()==TopAbs_FACE)result.faces.push_back({it.Value(),{owner_id,"generated:"+source}});
        // Caps are identified by the source/end normal planes, never traversal order.
    }
    if(request.make_solid) {
    const auto end=request.path_points.back();
    GProp_GProps initial_properties;
    BRepGProp::SurfaceProperties(BRepBuilderAPI_MakeFace(profile,true).Face(),initial_properties);
    const auto initial_center=initial_properties.CentreOfMass();
    TopoDS_Face start_cap,end_cap;
    std::vector<TopoDS_Face> caps;
    for(TopExp_Explorer it(result.shape,TopAbs_FACE);it.More();it.Next()){
        auto face=TopoDS::Face(it.Current());bool generated=false;
        for(const auto& side:result.faces)if(side.shape.IsSame(face))generated=true;
        if(generated)continue;
        BRepAdaptor_Surface surface(face);
        if(surface.GetType()!=GeomAbs_Plane)throw std::runtime_error("Neurčitá koncová plocha Sweepu");
        caps.push_back(face);GProp_GProps properties;BRepGProp::SurfaceProperties(face,properties);
        if(properties.CentreOfMass().Distance(initial_center)<1e-5){
            if(!start_cap.IsNull())throw std::runtime_error("Startovní a koncový průřez splývají");
            start_cap=face;
        }
    }
    if(caps.size()!=2||start_cap.IsNull())throw std::runtime_error("Chybí jednoznačné koncové plochy Sweepu");
    for(const auto& cap:caps)if(!cap.IsSame(start_cap))end_cap=cap;
    const gp_Pnt final_location(end.x,end.y,end.z);
    if(end_cap.IsNull()||BRepAdaptor_Surface(end_cap).Plane().Distance(final_location)>1e-5)
        throw std::runtime_error("Koncová plocha neodpovídá konci vodicí dráhy");
    const auto side_faces=result.faces;
    result.faces.push_back({start_cap,{owner_id,key("start",section.profile.region_id)}});
    result.faces.push_back({end_cap,{owner_id,key("end",section.profile.region_id)}});
    for(const auto& side:side_faces){
        const auto source=side.reference.semantic_key.substr(std::string("generated:").size());
        for(const auto& cap:std::vector<std::pair<TopoDS_Face,std::string>>{{start_cap,"start"},{end_cap,"end"}})
            for(TopExp_Explorer rim(cap.first,TopAbs_EDGE);rim.More();rim.Next())
                for(TopExp_Explorer boundary(side.shape,TopAbs_EDGE);boundary.More();boundary.Next())
                    if(rim.Current().IsSame(boundary.Current())) {
                        result.edges.push_back({rim.Current(),{owner_id,cap.second+":generated:"+source}});break;
                    }
    }
    for(std::size_t i=0;i<profile_edges.size();++i){
        const bool seam=section.profile.outer_vertex_source_ids.empty();
        if(!seam&&i>=section.profile.outer_vertex_source_ids.size())throw std::runtime_error("Chybí rodiče bodů průřezu");
        const auto& source=seam?section.profile.outer_edge_source_ids[i]:section.profile.outer_vertex_source_ids[i];
        const auto vertex=TopExp::FirstVertex(profile_edges[i],true);
        const auto& generated=builder.Generated(vertex);
        for(TopTools_ListIteratorOfListOfShape it(generated);it.More();it.Next())if(it.Value().ShapeType()==TopAbs_EDGE){
            result.edges.push_back({it.Value(),{owner_id,(seam?"seam:generated:":"generated:")+source}});
            for(TopExp_Explorer v(it.Value(),TopAbs_VERTEX);v.More();v.Next()){
                const auto position=BRep_Tool::Pnt(TopoDS::Vertex(v.Current()));
                const auto start_plane=BRepAdaptor_Surface(start_cap).Plane();
                const auto end_plane=BRepAdaptor_Surface(end_cap).Plane();
                const std::string role=start_plane.Distance(position)<1e-5?"start":end_plane.Distance(position)<1e-5?"end":"";
                if(!role.empty())result.vertices.push_back({v.Current(),{owner_id,(seam?"seam:":"")+role+":generated:"+source}});
            }
        }
    }
    } else {
        // Source points name the rails before history locates their shapes.
        // The unshared terminal point of an open contour is equally important.
        std::vector<TopoDS_Shape> rails;
        std::vector<TopoDS_Vertex> end_vertices;
        const auto collect_point=[&](const TopoDS_Vertex& vertex,const std::string& source,bool seam){
            const auto position=BRep_Tool::Pnt(vertex);
            const auto& generated=builder.Generated(vertex);
            for(TopTools_ListIteratorOfListOfShape it(generated);it.More();it.Next()) {
                if(it.Value().ShapeType()!=TopAbs_EDGE)continue;
                rails.push_back(it.Value());
                result.edges.push_back({it.Value(),{owner_id,(seam?"seam:generated:":"generated:")+source}});
                for(TopExp_Explorer v(it.Value(),TopAbs_VERTEX);v.More();v.Next()) {
                    const auto endpoint=TopoDS::Vertex(v.Current());
                    const bool start=BRep_Tool::Pnt(endpoint).Distance(position)<=request.linear_tolerance;
                    result.vertices.push_back({endpoint,{owner_id,std::string(seam?"seam:":"")+(start?"start":"end")+":generated:"+source}});
                    if(!start)end_vertices.push_back(endpoint);
                }
            }
        };
        const bool seam=section.profile.outer_vertex_source_ids.empty();
        for(std::size_t i=0;i<profile_edges.size();++i)
            collect_point(TopExp::FirstVertex(profile_edges[i],true),
                seam?section.profile.outer_edge_source_ids[i]:section.profile.outer_vertex_source_ids.at(i),seam);
        if(!section.thin_end_point_id.empty())
            collect_point(TopExp::LastVertex(profile_edges.back(),true),section.thin_end_point_id,false);
        for(const auto& side:result.faces) {
            const auto source=side.reference.semantic_key.substr(std::string("generated:").size());
            for(TopExp_Explorer e(side.shape,TopAbs_EDGE);e.More();e.Next()) {
                if(std::ranges::any_of(rails,[&](const auto& rail){return rail.IsSame(e.Current());}))continue;
                const auto edge=TopoDS::Edge(e.Current());
                const auto first=TopExp::FirstVertex(edge,true),last=TopExp::LastVertex(edge,true);
                const auto is_end=[&](const auto& vertex){return std::ranges::any_of(end_vertices,
                    [&](const auto& end){return end.IsSame(vertex);});};
                if(is_end(first)&&is_end(last))result.edges.push_back({edge,{owner_id,"end:generated:"+source}});
                else {
                    BRepExtrema_DistShapeShape distance(edge,profile);
                    if(distance.IsDone()&&distance.Value()<=request.linear_tolerance)
                        result.edges.push_back({edge,{owner_id,"start:generated:"+source}});
                }
            }
        }
    }
    for(std::size_t i=0;i<section.profile.inner_profiles.size();++i){
        auto inner=request;auto& region=inner.sections.front().profile;
        region.outer_profile=section.profile.inner_profiles[i];
        region.outer_edge_source_ids=section.profile.inner_edge_source_ids.at(i);
        region.outer_vertex_source_ids=section.profile.inner_vertex_source_ids.at(i);
        region.region_id=section.profile.inner_boundary_ids.at(i);
        region.inner_profiles.clear();region.inner_edge_source_ids.clear();region.inner_vertex_source_ids.clear();region.inner_boundary_ids.clear();
        auto tool=make_transported_sweep_data(inner,owner_id);
        if(!request.make_solid) {
            BRep_Builder compound_builder;TopoDS_Compound compound;compound_builder.MakeCompound(compound);
            compound_builder.Add(compound,result.shape);compound_builder.Add(compound,tool.shape);result.shape=compound;
            result.faces.insert(result.faces.end(),tool.faces.begin(),tool.faces.end());
            result.edges.insert(result.edges.end(),tool.edges.begin(),tool.edges.end());
            result.vertices.insert(result.vertices.end(),tool.vertices.begin(),tool.vertices.end());
            continue;
        }
        BRepAlgoAPI_Cut cut;
        set_boolean_inputs(cut, result.shape, tool.shape);cut.SetFuzzyValue(request.linear_tolerance);cut.Build();
        if(!cut.IsDone()||!BRepCheck_Analyzer(cut.Shape()).IsValid())throw std::runtime_error("Nelze vytvořit dutý průřez");
        result.faces=propagate_topology(cut,result.faces,tool.faces);
        result.edges=propagate_topology(cut,result.edges,tool.edges);
        result.vertices=propagate_topology(cut,result.vertices,tool.vertices);result.shape=cut.Shape();
    }
    // Helical overlaps are permitted. Pipe/Boolean validity checks above remain;
    // this operation neither rejects nor automatically unions overlapping turns.
    return result;
}

struct ThinSweepWires {
    SweepProfileWire outer;
    std::optional<SweepProfileWire> inner;
};

ThinSweepWires make_thin_sweep_profiles(const Sweep3DRequest& request,
    const Sweep3DRequest::Section& section) {
    if(!std::isfinite(request.thin_first)||!std::isfinite(request.thin_second)||
        request.thin_second-request.thin_first<=1e-7)
        throw std::runtime_error("Neplatná tloušťka Sweepu");
    if(!section.profile.inner_profiles.empty())throw std::runtime_error("Thin vyžaduje jednu otevřenou nebo uzavřenou konturu");
    std::vector<TopoDS_Edge> original_edges;
    auto original=make_profile_wire(section.profile.outer_profile,section.profile_normal,std::nullopt,&original_edges);
    if(original_edges.size()!=section.profile.outer_edge_source_ids.size())throw std::runtime_error("Chybí zdrojové křivky Thin profilu");
    const bool closed=BRep_Tool::IsClosed(original);
    const auto p=BRep_Tool::Pnt(TopExp::FirstVertex(original_edges.front(),true));
    const auto n=section.profile_normal;
    const gp_Pln plane(p,gp_Dir(n.x,n.y,n.z));
    // Closed Thin uses the same finite-interior convention as the authored
    // preview. Mirroring a contour must not exchange One side and Other side.
    BRepBuilderAPI_MakeFace support(plane);
    double closed_winding=1.;
    if(closed) {
        support=BRepBuilderAPI_MakeFace(plane,original,true);
        if(!support.IsDone())throw std::runtime_error("OCCT profile face is invalid");
        closed_winding=BRepTools::OuterWire(support.Face()).Orientation()==original.Orientation()?1.:-1.;
    } else support.Add(original);
    const auto offset=[&](double amount,const std::string& role){
        SweepProfileWire result;
        if (const auto* circle=std::get_if<ExtrusionRequest::CircleProfile>(&section.profile.outer_profile)) {
            auto shifted=*circle;shifted.radius-=amount;
            if (shifted.radius<=1e-7)throw std::runtime_error("Tloušťka uzavírá kruhový profil");
            result.circular_radius=shifted.radius;
            result.wire=make_profile_wire(shifted,section.profile_normal,
                section.circle_radial_direction,&result.edges);
            for(const auto& id:section.profile.outer_edge_source_ids)result.curve_ids.push_back("thin:"+role+":from:"+id);
            for(const auto& id:section.profile.outer_vertex_source_ids)result.point_ids.push_back("thin:"+role+":from:"+id);
            return result;
        }
        // Circle correspondence points split the same analytical circle into
        // arcs. Offset those radii exactly and retain each persisted span ID.
        if (const auto* curved=std::get_if<ExtrusionRequest::CurvedProfile>(&section.profile.outer_profile);
            curved && section.thin_end_point_id.empty() && !curved->curves.empty() &&
            std::ranges::all_of(curved->curves,[](const auto& curve) {
                return std::holds_alternative<ExtrusionRequest::ArcCurve>(curve);
            })) {
            std::optional<gp_Circ> circle;
            bool concentric=true;
            for (const auto& edge:original_edges) {
                BRepAdaptor_Curve curve(edge);
                if (curve.GetType()!=GeomAbs_Circle) {concentric=false;break;}
                const auto current=curve.Circle();
                if (circle && (circle->Location().Distance(current.Location())>1e-7 ||
                    std::abs(circle->Radius()-current.Radius())>1e-7)) {concentric=false;break;}
                circle=current;
            }
            if (concentric && circle) {
                const double radius=circle->Radius()-amount;
                if(radius<=1e-7)throw std::runtime_error("Tloušťka uzavírá kruhový profil");
                auto shifted=*curved;
                const auto center=circle->Location();
                for (auto& curve:shifted.curves) {
                    auto& arc=std::get<ExtrusionRequest::ArcCurve>(curve);
                    for (auto* point:{&arc.start,&arc.middle,&arc.end}) {
                        const gp_Pnt p(point->x,point->y,point->z);
                        const auto q=center.Translated(gp_Vec(center,p)*(radius/circle->Radius()));
                        *point={q.X(),q.Y(),q.Z()};
                    }
                }
                result.wire=make_profile_wire(shifted,section.profile_normal,std::nullopt,&result.edges);
                for(const auto& id:section.profile.outer_edge_source_ids)result.curve_ids.push_back("thin:"+role+":from:"+id);
                for(const auto& id:section.profile.outer_vertex_source_ids)result.point_ids.push_back("thin:"+role+":from:"+id);
                return result;
            }
        }
        if(const auto* polygon=std::get_if<ExtrusionRequest::PolygonProfile>(&section.profile.outer_profile)){
            // Straight-sided contours have exact miter offsets. Their source
            // edge/point ancestry is already known, without offset-history loss
            // when OCCT trims the parallel lines at convex corners.
            auto shifted=*polygon;const auto count=polygon->vertices.size();
            std::vector<gp_Vec> sides;const auto n=section.profile_normal;const gp_Vec normal(n.x,n.y,n.z);
            for(std::size_t i=0;i<count;++i){const auto a=polygon->vertices[i],b=polygon->vertices[(i+1)%count];gp_Vec tangent(b.x-a.x,b.y-a.y,b.z-a.z);tangent.Normalize();sides.push_back(normal.Crossed(tangent));}
            for(std::size_t i=0;i<count;++i){const auto& before=sides[(i+count-1)%count];const auto& after=sides[i];const double denominator=1+before.Dot(after);
                if(denominator<1e-8)throw std::runtime_error("Neplatný roh Thin profilu");const auto shift=(before+after)*(amount/denominator);
                auto& p=shifted.vertices[i];p={p.x+shift.X()*closed_winding,p.y+shift.Y()*closed_winding,p.z+shift.Z()*closed_winding};}
            for(std::size_t i=0;i<count;++i){const auto a=polygon->vertices[i],b=polygon->vertices[(i+1)%count],c=shifted.vertices[i],d=shifted.vertices[(i+1)%count];
                if(gp_Vec(b.x-a.x,b.y-a.y,b.z-a.z).Dot(gp_Vec(d.x-c.x,d.y-c.y,d.z-c.z))<=1e-9)throw std::runtime_error("Tloušťka překročila rozměr profilu");}
            result.wire=make_profile_wire(shifted,section.profile_normal,std::nullopt,&result.edges);
            for(const auto& id:section.profile.outer_edge_source_ids)result.curve_ids.push_back("thin:"+role+":from:"+id);
            for(const auto& id:section.profile.outer_vertex_source_ids)result.point_ids.push_back("thin:"+role+":from:"+id);
            return result;
        }
        if (original_edges.size()==1 && BRepAdaptor_Curve(original_edges.front()).GetType()==GeomAbs_BSplineCurve) {
            // A smooth offset spline is one mathematical source curve. The wire
            // offset builder may split its approximation into unrelated edges;
            // instead approximate the offset curve within a checked tolerance
            // and retain one B-spline carrying the pre-existing source identity.
            const auto& original_edge=original_edges.front();
            TopoDS_Edge edge=original_edge;
            if (std::abs(amount)>1e-10) {
                Standard_Real first,last;const auto basis=BRep_Tool::Curve(original_edge,first,last);
                const auto bounded=new Geom_TrimmedCurve(basis,first,last);
                const double sign=original_edge.Orientation()==TopAbs_REVERSED?-1.0:1.0;
                const auto n=section.profile_normal;
                Handle(Geom_Curve) offset_curve=new Geom_OffsetCurve(bounded,-amount*sign*closed_winding,gp_Dir(n.x,n.y,n.z));
                const double tolerance=std::min(1e-7,request.linear_tolerance*.1);
                GeomConvert_ApproxCurve approximation(offset_curve,tolerance,GeomAbs_C1,1024,8);
                if (!approximation.IsDone() || !std::isfinite(approximation.MaxError()) || approximation.MaxError()>tolerance)
                    throw std::runtime_error("Nelze odsadit spline v požadované přesnosti");
                BRepBuilderAPI_MakeEdge maker(approximation.Curve());
                if (!maker.IsDone()) throw std::runtime_error("Nelze vytvořit odsazenou spline hranu");
                edge=TopoDS::Edge(maker.Edge().Oriented(original_edge.Orientation()));
            }
            BRepBuilderAPI_MakeWire wire(edge);
            if (!wire.IsDone()) throw std::runtime_error("Nelze vytvořit odsazený spline profil");
            result.wire=wire.Wire();result.edges.push_back(edge);
            for(const auto& id:section.profile.outer_edge_source_ids)result.curve_ids.push_back("thin:"+role+":from:"+id);
            for(const auto& id:section.profile.outer_vertex_source_ids)result.point_ids.push_back("thin:"+role+":from:"+id);
            return result;
        }
        // Construct in the explicit Sketch plane, including a single straight open wire.
        // Closed contours need their first/last offset edges joined as well.
        // Open-result mode leaves that corner untrimmed on mixed line/arc wires.
        BRepOffsetAPI_MakeOffset builder(support.Face(),GeomAbs_Intersection,!closed);
        if(std::abs(amount)>1e-10){builder.Perform(-amount);if(!builder.IsDone())throw std::runtime_error("Nelze odsadit Thin profil");}
        BRepBuilderAPI_MakeWire wire;
        for(std::size_t i=0;i<original_edges.size();++i){
            std::vector<TopoDS_Edge> generated;
            if(std::abs(amount)<=1e-10)generated.push_back(original_edges[i]);
            else{const auto& shapes=builder.Generated(original_edges[i]);for(TopTools_ListIteratorOfListOfShape it(shapes);it.More();it.Next())if(it.Value().ShapeType()==TopAbs_EDGE)generated.push_back(TopoDS::Edge(it.Value()));}
            // An offset exactly equal to a tangent corner radius legitimately
            // collapses that source arc to one sharp vertex. OCCT reports no
            // generated edge for the arc while the adjacent source edges keep
            // a connected, valid wire. Retain the surviving source ancestry;
            // a split source edge still changes topology and remains invalid.
            if(generated.empty())continue;
            if(generated.size()!=1)throw std::runtime_error("Odsazení změnilo topologii profilu; upravte tloušťku");
            wire.Add(generated.front());if(!wire.IsDone())throw std::runtime_error("Nesouvislé odsazení Thin profilu");
            result.edges.push_back(generated.front());
            result.curve_ids.push_back("thin:"+role+":from:"+section.profile.outer_edge_source_ids[i]);
            if(!section.profile.outer_vertex_source_ids.empty())result.point_ids.push_back("thin:"+role+":from:"+section.profile.outer_vertex_source_ids.at(i));
        }
        if(result.edges.empty()||!wire.IsDone())throw std::runtime_error("Odsazení změnilo topologii profilu; upravte tloušťku");
        result.wire=wire.Wire();
        if(!BRepCheck_Analyzer(result.wire).IsValid() || (closed&&!BRep_Tool::IsClosed(result.wire)))
            throw std::runtime_error("Odsazení změnilo topologii profilu; upravte tloušťku");
        return result;
    };
    auto first=offset(request.thin_first,"first"),second=offset(request.thin_second,"second");
    if(closed){
        const auto area=[](const TopoDS_Wire& w){BRepBuilderAPI_MakeFace face(w,true);if(!face.IsDone()||!BRepCheck_Analyzer(face.Face()).IsValid())throw std::runtime_error("Neplatný odsazený průřez");GProp_GProps props;BRepGProp::SurfaceProperties(face.Face(),props);return props.Mass();};
        if(area(first.wire)<area(second.wire))std::swap(first,second);
        // Name by material role; changing the thickness side cannot swap reference identities.
        const auto rename=[](auto& profile,const std::string& role){for(auto* ids:{&profile.curve_ids,&profile.point_ids})for(auto& id:*ids)id="thin:"+role+id.substr(id.find(":from:"));};
        rename(first,"outside");rename(second,"inside");
        return {std::move(first),std::move(second)};
    }
    // Close the two offset sides at the two source endpoints. Endpoint roles
    // are supplied by ZIMA, never by kernel traversal indices.
    TopoDS_Vertex a,b,c,d;TopExp::Vertices(first.wire,a,b);TopExp::Vertices(second.wire,c,d);
    if(a.IsNull()||b.IsNull()||c.IsNull()||d.IsNull())throw std::runtime_error("Thin profil nemá dva konce");
    if(BRep_Tool::Pnt(a).Distance(BRep_Tool::Pnt(c))>BRep_Tool::Pnt(a).Distance(BRep_Tool::Pnt(d)))std::swap(c,d);
    auto start=BRepBuilderAPI_MakeEdge(a,c).Edge(),end=BRepBuilderAPI_MakeEdge(b,d).Edge();
    // The second side returns from the end closure to the start closure.
    // Add(Wire) may visit stored edges before the edge at the closure.
    // Use connected wire traversal so no disconnected intermediate Add
    // can silently discard a complete offset side.
    auto returning=second.wire;
    BRepTools_WireExplorer first_edge(returning);
    if(first_edge.More()) {
        const auto p=BRep_Tool::Pnt(TopExp::FirstVertex(first_edge.Current(),true));
        if(p.Distance(BRep_Tool::Pnt(c))<p.Distance(BRep_Tool::Pnt(d)))returning.Reverse();
    }
    BRepBuilderAPI_MakeWire wire;wire.Add(first.wire);wire.Add(end);
    for(BRepTools_WireExplorer edge(returning);edge.More();edge.Next()) {
        wire.Add(edge.Current());
        if(!wire.IsDone())throw std::runtime_error("Nelze uzavřít Thin profil");
    }
    wire.Add(start);
    if(!wire.IsDone())throw std::runtime_error("Nelze uzavřít Thin profil");
    SweepProfileWire joined;joined.wire=wire.Wire();joined.edges=first.edges;joined.edges.insert(joined.edges.end(),second.edges.begin(),second.edges.end());joined.edges.push_back(start);joined.edges.push_back(end);
    joined.curve_ids=first.curve_ids;joined.curve_ids.insert(joined.curve_ids.end(),second.curve_ids.begin(),second.curve_ids.end());
    joined.curve_ids.push_back("thin:closure:from:"+section.profile.outer_vertex_source_ids.front());joined.curve_ids.push_back("thin:closure:from:"+section.thin_end_point_id);
    joined.point_ids=first.point_ids;joined.point_ids.insert(joined.point_ids.end(),second.point_ids.begin(),second.point_ids.end());
    joined.point_ids.push_back("thin:first:from:"+section.profile.outer_vertex_source_ids.front());
    joined.point_ids.push_back("thin:first:from:"+section.thin_end_point_id);
    return {std::move(joined),std::nullopt};
}

// A single authored profile on a smooth spline is inherited by rigid transport,
// not by independently rotating its normal at every station. Explicit station
// profiles and sharp/mitered routes keep their established behavior.
std::optional<std::vector<gp_Trsf>> inherited_spline_frames(
        const Sweep3DRequest& request,const std::vector<TopoDS_Edge>& edges) {
    if(request.sections.size()!=1||request.sections.front().point_index!=0||
        request.smooth_loft||request.fixed_section_frames||request.separate_segments||
        std::ranges::none_of(request.path_segments,[](const auto& s){return !s.bezier_control_points.empty()||!s.bezier_spans.empty();}))return std::nullopt;
    // A centered circle is rotationally invariant. Preserve its established
    // seam/radial transport and approximation instead of perturbing a working
    // circular pipe solely to align an unobservable section rotation.
    if(const auto* circle=std::get_if<ExtrusionRequest::CircleProfile>(&request.sections.front().profile.outer_profile)) {
        const auto p=request.path_points.front();
        if(std::hypot(circle->center.x-p.x,circle->center.y-p.y,circle->center.z-p.z)<1e-12)return std::nullopt;
    }
    std::vector<gp_Trsf> frames(request.path_points.size());gp_Trsf rotation;
    std::optional<gp_Vec> previous;
    const auto start=request.path_points.front();const gp_Pnt origin(start.x,start.y,start.z);
    for(std::size_t i=0;i<edges.size();++i) {
        BRepAdaptor_Curve curve(edges[i]);gp_Pnt a,b;gp_Vec da,db;
        curve.D1(curve.FirstParameter(),a,da);curve.D1(curve.LastParameter(),b,db);
        da.Normalize();db.Normalize();
        if(previous&&previous->Dot(da)<1-1e-9)return std::nullopt;
        previous=db;
        if(curve.GetType()==GeomAbs_Circle) {
            gp_Trsf turn;turn.SetRotation(gp_Ax1(gp_Pnt{},curve.Circle().Axis().Direction()),curve.LastParameter()-curve.FirstParameter());rotation=turn*rotation;
        } else if(curve.GetType()!=GeomAbs_Line) {
            double first,last;const auto geometry=BRep_Tool::Curve(edges[i],first,last);
            GeomFill_CorrectedFrenet law;law.SetCurve(new GeomAdaptor_Curve(geometry,first,last));
            const auto frame=[&](double parameter) {
                gp_Vec t,n,binormal;
                if(!law.D0(parameter,t,n,binormal))throw std::runtime_error("Dráha nemá platnou tečnu.");
                return gp_Ax3(geometry->Value(parameter),gp_Dir(t),gp_Dir(n));
            };
            gp_Trsf turn;turn.SetDisplacement(frame(first),frame(last));turn.SetTranslationPart(gp_Vec{});rotation=turn*rotation;
        }
        frames[i+1]=rotation;frames[i+1].SetTranslationPart(gp_Vec(origin.Transformed(rotation),b));
    }
    return frames;
}

PrimitiveData make_sweep3d_data(const Sweep3DRequest& request, const std::string& owner_id,
    const std::vector<SweepProfileWire>* profile_wires = nullptr);

PrimitiveData make_thin_sweep_data(const Sweep3DRequest& request,const std::string& owner_id) {
    std::vector<SweepProfileWire> outside, inside;
    std::optional<bool> closed;
    for (const auto& section : request.sections) {
        auto profiles=make_thin_sweep_profiles(request,section);
        if (closed && *closed != profiles.inner.has_value())
            throw std::runtime_error("Thin Loft nemůže míchat otevřené a uzavřené profily");
        closed=profiles.inner.has_value();
        outside.push_back(std::move(profiles.outer));
        if (profiles.inner) inside.push_back(std::move(*profiles.inner));
    }
    if (outside.empty()) throw std::runtime_error("Thin Sweep nemá profil");
    auto plain=request;plain.thin=false;
    const auto calculate=[&](const std::vector<SweepProfileWire>& profiles) {
        return request.transported ? make_transported_sweep_data(plain,owner_id,&profiles.front())
            : make_sweep3d_data(plain,owner_id,&profiles);
    };
    auto result=calculate(outside);
    if (!inside.empty()) {
        auto tool=calculate(inside);
        BRepAlgoAPI_Cut cut;
        set_boolean_inputs(cut,result.shape,tool.shape);
        cut.SetToFillHistory(true);cut.SetFuzzyValue(request.linear_tolerance);cut.Build();
        if(!cut.IsDone()||cut.Shape().IsNull()||!BRepCheck_Analyzer(cut.Shape()).IsValid())
            throw std::runtime_error("Thin Sweep nevytvořil platné těleso");
        result.faces=propagate_topology(cut,result.faces,tool.faces);
        result.edges=propagate_topology(cut,result.edges,tool.edges);
        result.vertices=propagate_topology(cut,result.vertices,tool.vertices);
        result.source_caps=propagate_topology(cut,result.source_caps,tool.source_caps);
        result.shape=cut.Shape();
    }
    int solids=0;
    for(TopExp_Explorer it(result.shape,TopAbs_SOLID);it.More();it.Next())++solids;
    if(solids!=1)throw std::runtime_error("Tloušťka nevytvořila jedno souvislé těleso");
    return result;
}

Sweep3DRequest expand_profile_twist(const Sweep3DRequest& request,
    double coefficient=1,bool straight=false,GProp_GProps* measured=nullptr);
void append_reference_geometry(ViewerReferenceGeometry&,ViewerReferenceGeometry);
PrimitiveData make_sweep3d_data(
    const Sweep3DRequest& request, const std::string& owner_id,
    const std::vector<SweepProfileWire>* profile_wires) {
    if(request.twist)return make_sweep3d_data(expand_profile_twist(request),owner_id);
    if(request.profile_references) {
        auto input=request;input.profile_references.reset();
        auto result=make_sweep3d_data(input,owner_id,profile_wires);
        auto datums=*request.profile_references;
        for(auto& axis:datums.axes)axis.reference.owner_id=owner_id;
        for(auto& point:datums.points){point.reference.owner_id=owner_id;point.display_owner_id=owner_id;}
        for(auto& edge:datums.edges){edge.reference.owner_id=owner_id;edge.display_owner_id=owner_id;}
        append_reference_geometry(result.profile_references,std::move(datums));return result;
    }
    if(request.thin)return make_thin_sweep_data(request,owner_id);
    if(request.transported)return make_transported_sweep_data(request,owner_id);
    const auto holes=request.sections.front().profile.inner_profiles.size();
    for(const auto& section:request.sections)
        if(section.profile.inner_profiles.size()!=holes)throw std::runtime_error("Profily Loftu musí mít stejný počet otvorů");
    if(holes) {
        const auto clear_holes=[](auto& region){region.inner_profiles.clear();region.inner_boundary_ids.clear();
            region.inner_edge_source_ids.clear();region.inner_vertex_source_ids.clear();};
        auto outside=request;
        for(auto& section:outside.sections)clear_holes(section.profile);
        auto result=make_sweep3d_data(outside,owner_id);
        for(std::size_t i=0;i<holes;++i) {
            auto inside=request;
            for(auto& section:inside.sections) {
                auto& region=section.profile;
                region.outer_profile=region.inner_profiles.at(i);region.outer_boundary_id=region.inner_boundary_ids.at(i);
                region.region_id=region.outer_boundary_id;region.outer_edge_source_ids=region.inner_edge_source_ids.at(i);
                region.outer_vertex_source_ids=region.inner_vertex_source_ids.at(i);clear_holes(region);
                section.circle_radial_direction.reset();
            }
            auto tool=make_sweep3d_data(inside,owner_id);
            if(!request.make_solid) {
                BRep_Builder compound_builder;TopoDS_Compound compound;compound_builder.MakeCompound(compound);
                compound_builder.Add(compound,result.shape);compound_builder.Add(compound,tool.shape);result.shape=compound;
                result.faces.insert(result.faces.end(),tool.faces.begin(),tool.faces.end());
                result.edges.insert(result.edges.end(),tool.edges.begin(),tool.edges.end());
                result.vertices.insert(result.vertices.end(),tool.vertices.begin(),tool.vertices.end());
                continue;
            }
            BRepAlgoAPI_Cut cut;
            set_boolean_inputs(cut,result.shape,tool.shape);cut.SetToFillHistory(true);cut.SetFuzzyValue(request.linear_tolerance);cut.Build();
            if(!cut.IsDone()||cut.Shape().IsNull()||!BRepCheck_Analyzer(cut.Shape()).IsValid())throw std::runtime_error("Nelze vytvořit otvor taženého průřezu");
            result.faces=propagate_topology(cut,result.faces,tool.faces);result.edges=propagate_topology(cut,result.edges,tool.edges);
            result.vertices=propagate_topology(cut,result.vertices,tool.vertices);result.source_caps=propagate_topology(cut,result.source_caps,tool.source_caps);
            result.shape=cut.Shape();
        }
        for(auto* refs:{&result.faces,&result.source_caps})for(auto& ref:*refs)
            if(ref.reference.semantic_key.starts_with("sweep:cap:"))ref.reference.surface.reset();
        int solids=0;for(TopExp_Explorer it(result.shape,TopAbs_SOLID);it.More();it.Next())++solids;
        if(request.make_solid&&solids!=1)throw std::runtime_error("Průřez s otvorem nevytvořil jedno těleso");
        return result;
    }
    const auto first_station = [&](std::size_t segment) {
        return request.separate_segments ? 2 * segment : segment;
    };
    std::vector<TopoDS_Vertex> path_vertices;
    path_vertices.reserve(request.path_points.size());
    for (const auto& point : request.path_points) {
        path_vertices.push_back(BRepBuilderAPI_MakeVertex(
            gp_Pnt(point.x, point.y, point.z)).Vertex());
    }
    BRepBuilderAPI_MakeWire spine_builder;
    std::vector<TopoDS_Edge> spine_edges;
    spine_edges.reserve(request.path_segments.size());
    for (std::size_t index = 0; index < request.path_segments.size(); ++index) {
        const auto& segment = request.path_segments[index];
        TopoDS_Edge edge;
        const auto geometry=sweep_spine_curve(segment);
        const auto first=first_station(index),last=first+1;
        if(geometry.IsNull()) {
            BRepBuilderAPI_MakeEdge builder(path_vertices[first],path_vertices[last]);
            if(!builder.IsDone())throw std::runtime_error("OCCT Sweep line spine failed");
            edge=builder.Edge();
        } else {
            BRepBuilderAPI_MakeEdge builder(geometry,path_vertices[first],path_vertices[last]);
            if(!builder.IsDone())throw std::runtime_error("OCCT Sweep curved spine failed");
            edge=builder.Edge();
        }
        spine_builder.Add(edge);
        spine_edges.push_back(std::move(edge));
    }
    if (!spine_builder.IsDone()) {
        throw std::runtime_error("OCCT 3D Sweep spine wire failed");
    }
    // One shared section wire per station. At a sharp corner this wire is
    // projected onto the angle-bisector plane, so both adjacent sweeps meet
    // at exactly the same boundary, including when profile shapes differ.
    struct StationWire {
        TopoDS_Wire wire;
        std::vector<TopoDS_Edge> edges;
        std::vector<std::string> curve_ids;
        std::vector<std::string> point_ids;
        std::string profile_id;
        std::string open_end_point_id;
        std::optional<SurfaceGeometry> circular_cap;
    };
    std::vector<StationWire> stations;
    const auto tangent = [&](std::size_t segment, bool end) {
        BRepAdaptor_Curve curve(spine_edges[segment]);
        gp_Pnt p; gp_Vec d;
        curve.D1(end ? curve.LastParameter() : curve.FirstParameter(), p, d);
        if(d.Magnitude()<1e-12)throw std::runtime_error("Dráha nemá platnou tečnu.");
        return d.Normalized();
    };
    // Decide before converting section wires: an exact circular revolution
    // needs analytic line edges to retain plane/cylinder surfaces. Revolving
    // degree-one splines instead produced general revolution surfaces and
    // pathological rational-volume quadrature on oblique half-turns.
    const bool exact_revolution=[&] {
        if(request.sections.size()!=2||request.path_segments.size()!=1||
            !request.path_segments.front().arc_midpoint||profile_wires||
            request.sections[0].point_index!=0||request.sections[1].point_index!=1)return false;
        BRepAdaptor_Curve spine(spine_edges.front());
        if(spine.GetType()!=GeomAbs_Circle)return false;
        const auto* first=std::get_if<ExtrusionRequest::PolygonProfile>(&request.sections[0].profile.outer_profile);
        const auto* last=std::get_if<ExtrusionRequest::PolygonProfile>(&request.sections[1].profile.outer_profile);
        if(!first||!last||first->vertices.size()!=last->vertices.size())return false;
        gp_Trsf rotation;rotation.SetRotation(spine.Circle().Axis(),spine.LastParameter()-spine.FirstParameter());
        for(std::size_t n=0;n<first->vertices.size();++n) {
            const auto& a=first->vertices[n];const auto& b=last->vertices[n];
            if(gp_Pnt(a.x,a.y,a.z).Transformed(rotation).Distance(gp_Pnt(b.x,b.y,b.z))>1e-9)return false;
        }
        for(std::size_t n=0;n<2;++n) {
            const auto& normal=request.sections[n].profile_normal;
            if(gp_Vec(normal.x,normal.y,normal.z).Normalized().Dot(tangent(0,n==1))<1-1e-9)return false;
        }
        return true;
    }();
    const auto inherited_frames=inherited_spline_frames(request,spine_edges);
    const auto planar_binormal=[&]() -> std::optional<gp_Vec> {
        if(!request.separate_segments)return std::nullopt;
        const auto first=tangent(0,false);
        std::optional<gp_Vec> normal;
        for(std::size_t i=0;i<spine_edges.size()&&!normal;++i)
            for(double fraction:{0.,.5,1.}) {
                // Semicircle chains can have parallel tangents at every
                // endpoint. Their exact midpoint tangent still defines the
                // route plane and the authored profile side at the half-turn.
                BRepAdaptor_Curve curve(spine_edges[i]);gp_Pnt point;gp_Vec direction;
                curve.D1(curve.FirstParameter()+fraction*(curve.LastParameter()-curve.FirstParameter()),point,direction);
                const auto candidate=first.Crossed(direction.Normalized());
                if(candidate.Magnitude()>1e-8){normal=candidate.Normalized();break;}
            }
        if(!normal)return std::nullopt;
        const auto& origin=request.path_points.front();
        const auto on_plane=[&](const Vec3& p) {
            return std::abs(gp_Vec(p.x-origin.x,p.y-origin.y,p.z-origin.z).Dot(*normal))<=1e-8;
        };
        // Check the exact control data, not a few sampled curve positions.
        // A spatial spline must retain its ordinary transport behavior.
        for(const auto& segment:request.path_segments) {
            if(!on_plane(segment.start)||!on_plane(segment.end)||
                (segment.arc_midpoint&&!on_plane(*segment.arc_midpoint)))return std::nullopt;
            for(const auto& p:segment.bezier_control_points)if(!on_plane(p))return std::nullopt;
            for(const auto& span:segment.bezier_spans)
                for(const auto& p:span)if(!on_plane(p))return std::nullopt;
        }
        return normal;
    }();
    // Keep the established straight/sharp 3D Sweep path. Native circular
    // sections are needed only for a rigid profile on planar circular spans;
    // their semicircle pipe approximation can otherwise collapse.
    const bool rigid_segmented_profile=planar_binormal.has_value() &&
        std::ranges::any_of(request.path_segments,[](const auto& segment){return segment.arc_midpoint.has_value();}) &&
        std::ranges::all_of(request.sections,[&](const auto& section){
            return section.profile_id==request.sections.front().profile_id;
        });
    std::optional<gp_Vec> previous_direction;
    std::optional<gp_Vec> transported_radial;
    const Sweep3DRequest::Section* previous_section{};
    std::optional<gp_Trsf> inherited_section_rotation;
    const auto section_rotation=[&](const gp_Vec& from,const gp_Vec& to) {
        gp_Trsf rotation;
        if(planar_binormal && std::abs(planar_binormal->Dot(from.Normalized()))<1e-8 &&
            std::abs(planar_binormal->Dot(to.Normalized()))<1e-8) {
            const double angle=std::atan2(planar_binormal->Dot(from.Crossed(to)),from.Dot(to));
            rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(*planar_binormal)),angle);
        } else rotation.SetRotation(gp_Quaternion(from,to));
        return rotation;
    };
    for(std::size_t i=0;i<request.path_points.size();++i) {
        // Empty stations inherit the preceding defined profile. The document
        // requires the first station to define a profile.
        const auto* section=&request.sections.front();
        for(const auto& candidate:request.sections) {
            if(candidate.point_index>i)break;
            section=&candidate;
        }
        const auto direction = request.separate_segments ? tangent(i / 2, i % 2 != 0)
            : i<spine_edges.size()?tangent(i,false):tangent(i-1,true);
        const bool inherited_section=previous_section==section;
        if(previous_direction) {
            const auto rotation=section_rotation(*previous_direction,direction);
            transported_radial=transported_radial->Transformed(rotation);
        } else transported_radial=gp_Vec(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(direction)).XDirection());
        auto radial=section->circle_radial_direction;
        if(std::holds_alternative<ExtrusionRequest::CircleProfile>(section->profile.outer_profile) &&
           section->profile.outer_vertex_source_ids.empty()) {
            const auto back=section_rotation(direction,gp_Vec(section->profile_normal.x,
                section->profile_normal.y,section->profile_normal.z));
            const auto v=transported_radial->Transformed(back);
            radial=Vec3{v.X(),v.Y(),v.Z()};
        }
        StationWire station;
        station.curve_ids=section->profile.outer_edge_source_ids;
        station.point_ids=section->profile.outer_vertex_source_ids;
        station.profile_id=section->profile_id;
        if(!request.make_solid)station.open_end_point_id=section->thin_end_point_id;
        if (profile_wires) {
            const auto& profile=profile_wires->at(static_cast<std::size_t>(section-request.sections.data()));
            station.wire=profile.wire;station.edges=profile.edges;
            station.curve_ids=profile.curve_ids;station.point_ids=profile.point_ids;
            if (profile.circular_radius) {
                auto circle=std::get<ExtrusionRequest::CircleProfile>(section->profile.outer_profile);
                circle.radius=*profile.circular_radius;
                station.edges.clear();
                station.wire=make_profile_wire(circle,section->profile_normal,radial,&station.edges);
            }
        } else {
            station.wire=make_profile_wire(section->profile.outer_profile,
                section->profile_normal,radial,&station.edges);
        }
        if(!exact_revolution && !rigid_segmented_profile) {
            BRepBuilderAPI_NurbsConvert nurbs(station.wire,true);
            station.wire=TopoDS::Wire(nurbs.Shape());
            for(auto& edge:station.edges)edge=TopoDS::Edge(nurbs.ModifiedShape(edge));
        }
        const auto& from=request.path_points[section->point_index];
        const auto& to=request.path_points[i];
        const auto& normal=section->profile_normal;
        gp_Trsf movement;
        if(request.fixed_section_frames) {
            if(!request.smooth_loft||request.sections.size()!=request.path_points.size()||section->point_index!=i)
                throw std::invalid_argument("Fixed section frames require an explicit smooth loft section at each station.");
        } else if(request.separate_segments && inherited_section &&
                  previous_direction && inherited_section_rotation) {
            // A section inherited across sharp spatial corners retains the
            // roll accumulated along the authored route. Rotating the original
            // profile independently onto every tangent makes the third
            // mutually perpendicular leg choose a different frame than the
            // ZIMA route/profile preview.
            movement=section_rotation(*previous_direction,direction)*
                *inherited_section_rotation;
            inherited_section_rotation=movement;
        } else if(planar_binormal && std::abs(planar_binormal->Dot(gp_Vec(normal.x,normal.y,normal.z).Normalized()))<1e-8) {
            // Opposite tangents alone do not define a half-turn axis. Use the
            // path plane so the profile keeps its authored material side.
            const gp_Vec source(normal.x,normal.y,normal.z);
            const double angle=std::atan2(planar_binormal->Dot(source.Crossed(direction)),source.Dot(direction));
            movement.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(*planar_binormal)),angle);
        } else movement.SetRotation(gp_Quaternion(gp_Vec(normal.x,normal.y,normal.z),direction));
        if(request.separate_segments && !inherited_section)
            inherited_section_rotation=movement;
        const auto rotated=gp_Pnt(from.x,from.y,from.z).Transformed(movement);
        movement.SetTranslationPart(gp_Vec(rotated,gp_Pnt(to.x,to.y,to.z)));
        if(inherited_frames) {
            gp_Trsf initial;initial.SetRotation(gp_Quaternion(gp_Vec(normal.x,normal.y,normal.z),tangent(0,false)));
            initial.SetTranslationPart(gp_Vec(gp_Pnt(from.x,from.y,from.z).Transformed(initial),gp_Pnt(from.x,from.y,from.z)));
            movement=inherited_frames->at(i)*initial;
        }
        previous_direction=direction;
        previous_section=section;
        BRepBuilderAPI_Transform transport(station.wire,movement,true);
        station.wire=TopoDS::Wire(transport.Shape());
        for(auto& edge:station.edges)edge=TopoDS::Edge(transport.ModifiedShape(edge));
        if (const auto* circle = std::get_if<ExtrusionRequest::CircleProfile>(
                &section->profile.outer_profile); circle && !profile_wires) {
            // Thin caps are annuli, so persist their calculated boundaries
            // instead of publishing the solid profile's disk metadata.
            const auto center = gp_Pnt(circle->center.x,circle->center.y,circle->center.z).Transformed(movement);
            SurfaceGeometry geometry;
            geometry.origin = {center.X(),center.Y(),center.Z()};
            geometry.axis = {direction.X(),direction.Y(),direction.Z()};
            const auto cap_radial=inherited_frames&&radial?gp_Vec(radial->x,radial->y,radial->z).Transformed(movement):*transported_radial;
            geometry.radial = {cap_radial.X(),cap_radial.Y(),cap_radial.Z()};
            geometry.radius = circle->radius;
            station.circular_cap = geometry;
        }
        if(!request.fixed_section_frames&&!request.separate_segments && i>0&&i<spine_edges.size()) {
            const auto incoming=tangent(i-1,true);
            const auto sum=incoming+direction;
            if(sum.Magnitude()<1e-8)throw std::runtime_error("Ostrý obrat dráhy o 180° nelze spojit.");
            const auto bisector=sum.Normalized();
            const double cosine=bisector.Dot(direction);
            const auto shear=(bisector-direction*cosine)/cosine;
            if(shear.Magnitude()>1e-9) {
                gp_Mat matrix;
                for(int row=1;row<=3;++row)for(int col=1;col<=3;++col)
                    matrix.SetValue(row,col,(row==col?1.0:0.0)-direction.Coord(row)*shear.Coord(col));
                const gp_XYZ origin(to.x,to.y,to.z);
                gp_GTrsf projection(matrix,origin-matrix*origin);
                BRepBuilderAPI_GTransform project(station.wire,projection,true);
                station.wire=TopoDS::Wire(project.Shape());
                for(auto& edge:station.edges)edge=TopoDS::Edge(project.ModifiedShape(edge));
            }
        }
        stations.push_back(std::move(station));
    }
    if(request.smooth_loft) {
        BRepOffsetAPI_ThruSections builder(request.make_solid,false,
            request.linear_tolerance);
        builder.CheckCompatibility(false);
        for(const auto& station:stations)builder.AddWire(station.wire);
        builder.Build();
        if(!builder.IsDone()||builder.Shape().IsNull()||
            !BRepCheck_Analyzer(builder.Shape()).IsValid())
            throw std::runtime_error("Plynulý loft nevytvořil platný plech.");
        PrimitiveData smooth{builder.Shape(),{},{},{}};
        const auto source_id=request.path_segments.front().source_id;
        const auto& first=stations.front();const auto& last=stations.back();
        for(std::size_t e=0;e<first.edges.size();++e) {
            const auto semantic="sweep:"+source_id+":profile:"+
                first.profile_id+":from:"+first.curve_ids[e];
            const auto& generated=builder.Generated(first.edges[e]);
            for(TopTools_ListIteratorOfListOfShape it(generated);it.More();it.Next())
                if(it.Value().ShapeType()==TopAbs_FACE)
                    smooth.faces.push_back({it.Value(),{owner_id,semantic}});
        }
        const auto lies_on=[](const TopoDS_Edge& source,const TopoDS_Edge& target,
                double tolerance) {
            BRepAdaptor_Curve curve(source);
            for(double f:{0.,.25,.5,.75,1.}) {
                const auto p=curve.Value(curve.FirstParameter()+
                    (curve.LastParameter()-curve.FirstParameter())*f);
                BRepExtrema_DistShapeShape distance(
                    BRepBuilderAPI_MakeVertex(p).Vertex(),target);
                if(!distance.IsDone()||distance.Value()>tolerance)return false;
            }
            return true;
        };
        for(const auto endpoint:{std::pair{std::size_t{0},true},
                                 std::pair{stations.size()-1,false}}) {
            const auto index=endpoint.first;const bool start=endpoint.second;
            const auto& station=stations[index];
            const auto& location=request.path_points[index];
            const auto normal=[&] {
                if(request.fixed_section_frames) {
                    const auto& authored_normal=request.sections.at(index).profile_normal;
                    return gp_Vec(authored_normal.x,authored_normal.y,authored_normal.z);
                }
                return start?tangent(0,false):tangent(spine_edges.size()-1,true);
            }();
            for(TopExp_Explorer faces(builder.Shape(),TopAbs_FACE);
                    faces.More();faces.Next()) {
                const auto face=TopoDS::Face(faces.Current());
                BRepAdaptor_Surface surface(face);
                if(surface.GetType()!=GeomAbs_Plane)continue;
                const auto plane=surface.Plane();
                if(std::abs(plane.Axis().Direction().Dot(gp_Dir(normal)))<1-1e-8||
                    plane.Distance(gp_Pnt(location.x,location.y,location.z))>
                        request.linear_tolerance)continue;
                BRepExtrema_DistShapeShape distance(station.wire,face);
                if(!distance.IsDone()||distance.Value()>request.linear_tolerance)continue;
                FaceReference reference{owner_id,"sweep:cap:"+
                    std::string(start?"start:from:":"end:from:")+source_id,{}};
                smooth.faces.push_back({face,reference});
                smooth.source_caps.push_back({face,reference});
            }
            for(std::size_t e=0;e<station.edges.size();++e) {
                const auto rim_semantic="sweep:rim:"+
                    std::string(start?"start":"end")+":at:"+
                    request.path_point_ids[index]+":profile:"+
                    station.profile_id+":from:"+station.curve_ids[e];
                for(TopExp_Explorer edges(builder.Shape(),TopAbs_EDGE);
                        edges.More();edges.Next()) {
                    const auto candidate=TopoDS::Edge(edges.Current());
                    if(BRep_Tool::Degenerated(candidate))continue;
                    if(lies_on(station.edges[e],candidate,request.linear_tolerance)&&
                       lies_on(candidate,station.edges[e],request.linear_tolerance)) {
                        smooth.edges.push_back({candidate,{owner_id,rim_semantic}});
                        break;
                    }
                }
                if(e>=station.point_ids.size())continue;
                const auto authored=TopExp::FirstVertex(station.edges[e],true);
                const auto position=BRep_Tool::Pnt(authored);
                const auto vertex_semantic="sweep:vertex:"+
                    (request.canonical_station_ids.contains(request.path_point_ids[index])?std::string{}:std::string(start?"start:":"end:"))+"at:"+
                    request.path_point_ids[index]+":profile:"+
                    station.profile_id+":from:"+station.point_ids[e];
                for(TopExp_Explorer vertices(builder.Shape(),TopAbs_VERTEX);
                        vertices.More();vertices.Next()) {
                    const auto candidate=TopoDS::Vertex(vertices.Current());
                    if(position.Distance(BRep_Tool::Pnt(candidate))<=
                            request.linear_tolerance) {
                        smooth.vertices.push_back({candidate,{owner_id,vertex_semantic}});
                        break;
                    }
                }
            }
        }
        // Each corresponding profile point generates one longitudinal spline.
        // Locate it by its authored endpoint pair rather than OCCT enumeration.
        for(std::size_t p=0;p<first.point_ids.size();++p) {
            const auto a=BRep_Tool::Pnt(TopExp::FirstVertex(first.edges[p],true));
            const auto b=BRep_Tool::Pnt(TopExp::FirstVertex(last.edges[p],true));
            for(TopExp_Explorer edges(builder.Shape(),TopAbs_EDGE);
                    edges.More();edges.Next()) {
                const auto candidate=TopoDS::Edge(edges.Current());
                if(BRep_Tool::Degenerated(candidate))continue;
                const auto c=BRep_Tool::Pnt(TopExp::FirstVertex(candidate,true));
                const auto d=BRep_Tool::Pnt(TopExp::LastVertex(candidate,true));
                if(!((a.Distance(c)<=request.linear_tolerance&&
                      b.Distance(d)<=request.linear_tolerance)||
                     (a.Distance(d)<=request.linear_tolerance&&
                      b.Distance(c)<=request.linear_tolerance)))continue;
                smooth.edges.push_back({candidate,{owner_id,"sweep:"+source_id+
                    ":profile:"+first.profile_id+":from:"+first.point_ids[p]}});
                break;
            }
        }
        return smooth;
    }
    PrimitiveData result;
    for(std::size_t i=0;i<spine_edges.size();++i) {
        const auto& segment=request.path_segments[i];
        const auto collect=[&](auto& builder) {
            if(!builder.IsDone()||builder.Shape().IsNull()||!BRepCheck_Analyzer(builder.Shape()).IsValid())
                throw std::runtime_error("Úsek 3D Sweepu nevytvořil platné těleso.");
            PrimitiveData piece{builder.Shape(),{},{},{}};
            for(const auto index:{first_station(i),first_station(i)+1}) {
                const auto& station=stations[index];
                const bool start = index == first_station(i);
                const bool route_endpoint = start ? i == 0 : i + 1 == spine_edges.size();
                if (request.make_solid && (request.separate_segments || route_endpoint)) {
                    const auto& location = request.path_points[index];
                    const auto normal = tangent(i, !start);
                    const auto key = std::string("sweep:cap:") + (start ? "start:from:" : "end:from:") + segment.source_id;
                    // The ZIMA identity above exists before this lookup. The
                    // plane locates its cap; OCCT enumeration never names it.
                    for (TopExp_Explorer faces(builder.Shape(),TopAbs_FACE); faces.More(); faces.Next()) {
                        const auto face = TopoDS::Face(faces.Current());
                        BRepAdaptor_Surface surface(face);
                        if (surface.GetType() != GeomAbs_Plane) continue;
                        const auto plane = surface.Plane();
                        if (std::abs(plane.Axis().Direction().Dot(gp_Dir(normal))) < 1-1e-9 ||
                            plane.Distance(gp_Pnt(location.x,location.y,location.z)) > request.linear_tolerance) continue;
                        // At a half-turn the two section planes coincide.
                        // Match the authored section boundary as well as its
                        // plane, otherwise both caps receive both identities.
                        BRepExtrema_DistShapeShape distance(station.wire,face);
                        if(!distance.IsDone()||distance.Value()>request.linear_tolerance)continue;
                        bool contains_section=true;
                        for(const auto& edge:station.edges) {
                            BRepAdaptor_Curve curve(edge);
                            const auto probe=BRepBuilderAPI_MakeVertex(curve.Value((curve.FirstParameter()+curve.LastParameter())*.5)).Vertex();
                            BRepExtrema_DistShapeShape probe_distance(probe,face);
                            if(!probe_distance.IsDone()||probe_distance.Value()>request.linear_tolerance) {
                                contains_section=false;break;
                            }
                        }
                        if(!contains_section)continue;
                        FaceReference reference{owner_id,key,{}};
                        if (station.circular_cap) {
                            auto geometry = *station.circular_cap;
                            if (start) geometry.axis = {-geometry.axis.x,-geometry.axis.y,-geometry.axis.z};
                            reference.surface = std::make_shared<const SurfaceGeometry>(geometry);
                        }
                        piece.faces.push_back({face,reference});
                        piece.source_caps.push_back({face,reference});
                    }
                }
                if(station.edges.size()!=station.curve_ids.size())
                    throw std::runtime_error("Chybí původ geometrie profilu Sweepu.");
                for(std::size_t e=0;e<station.edges.size();++e) {
                    // A station's authored boundary is also original topology.
                    // Generated() only finds longitudinal children, so it used
                    // to omit the straight End rim needed to attach another
                    // Bend. Define its ancestry before locating the body edge.
                    const auto rim_semantic="sweep:rim:"+std::string(start?"start":"end")+
                        ":at:"+request.path_point_ids[index]+":profile:"+station.profile_id+
                        ":from:"+station.curve_ids[e];
                    const auto lies_on=[](const TopoDS_Edge& source,const TopoDS_Edge& target,double tolerance) {
                        BRepAdaptor_Curve curve(source);
                        for(double f:{0.,.25,.5,.75,1.}) {
                            const auto p=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*f);
                            const auto v=BRepBuilderAPI_MakeVertex(p).Vertex();
                            BRepExtrema_DistShapeShape distance(v,target);
                            if(!distance.IsDone()||distance.Value()>tolerance)return false;
                        }
                        return true;
                    };
                    for(TopExp_Explorer edges(builder.Shape(),TopAbs_EDGE);edges.More();edges.Next()) {
                        const auto candidate=TopoDS::Edge(edges.Current());
                        if(BRep_Tool::Degenerated(candidate))continue;
                        if(!candidate.IsSame(station.edges[e])) {
                            // Reject unrelated rails cheaply before an extrema solve.
                            BRepAdaptor_Curve authored(station.edges[e]),actual(candidate);
                            const auto a=authored.Value(authored.FirstParameter()),b=authored.Value(authored.LastParameter());
                            const auto c=actual.Value(actual.FirstParameter()),d=actual.Value(actual.LastParameter());
                            const double tolerance=request.linear_tolerance;
                            const bool closed=a.Distance(b)<=tolerance&&c.Distance(d)<=tolerance;
                            if(!closed && !((a.Distance(c)<=tolerance&&b.Distance(d)<=tolerance)||
                                (a.Distance(d)<=tolerance&&b.Distance(c)<=tolerance)))continue;
                        }
                        if(candidate.IsSame(station.edges[e]) ||
                            (lies_on(station.edges[e],candidate,request.linear_tolerance)&&
                             lies_on(candidate,station.edges[e],request.linear_tolerance))) {
                            piece.edges.push_back({candidate,{owner_id,rim_semantic}});
                            break;
                        }
                    }
                    if(e<station.point_ids.size()) {
                        const auto authored=TopExp::FirstVertex(station.edges[e],true);
                        const auto position=BRep_Tool::Pnt(authored);
                        // A station shared by consecutive path segments is one
                        // persisted topology point.  Do not encode its local
                        // start/end role: doing so gives the same fused OCCT
                        // vertex two identities and makes every longitudinal
                        // rail incident to that station lose its selectable
                        // endpoint pair. Keep the established terminal identity
                        // because it is not duplicated and existing downstream
                        // attachments legitimately reference it.
                        const auto canonical_station=request.canonical_station_ids.contains(
                            request.path_point_ids[index]);
                        const auto vertex_semantic=!canonical_station
                            ? "sweep:vertex:"+std::string(start?"start":"end")+
                                ":at:"+request.path_point_ids[index]+":profile:"+station.profile_id+
                                ":from:"+station.point_ids[e]
                            : "sweep:vertex:at:"+request.path_point_ids[index]+":profile:"+station.profile_id+
                                ":from:"+station.point_ids[e];
                        for(TopExp_Explorer vertices(builder.Shape(),TopAbs_VERTEX);vertices.More();vertices.Next()) {
                            const auto candidate=TopoDS::Vertex(vertices.Current());
                            if(position.Distance(BRep_Tool::Pnt(candidate))<=request.linear_tolerance) {
                                piece.vertices.push_back({candidate,{owner_id,vertex_semantic}});break;
                            }
                        }
                    }
                    const auto semantic="sweep:"+request.path_segments[i].source_id+":profile:"+
                        station.profile_id+":from:"+station.curve_ids[e];
                    const auto& generated=builder.Generated(station.edges[e]);
                    for(TopTools_ListIteratorOfListOfShape it(generated);it.More();it.Next()) {
                        if(it.Value().ShapeType()==TopAbs_FACE)piece.faces.push_back({it.Value(),{owner_id,semantic}});
                        if(it.Value().ShapeType()==TopAbs_EDGE)piece.edges.push_back({it.Value(),{owner_id,semantic}});
                    }
                    if(e<station.point_ids.size()) {
                        // The identity is defined by the persisted profile Point;
                        // history only locates its swept child on this segment.
                        const auto point_semantic="sweep:"+segment.source_id+":profile:"+
                            station.profile_id+":from:"+station.point_ids[e];
                        const auto vertex=TopExp::FirstVertex(station.edges[e],true);
                        const auto& rails=builder.Generated(vertex);
                        for(TopTools_ListIteratorOfListOfShape it(rails);it.More();it.Next()) {
                            if(it.Value().ShapeType()==TopAbs_EDGE)piece.edges.push_back({it.Value(),{owner_id,point_semantic}});
                            if(it.Value().ShapeType()==TopAbs_VERTEX)piece.vertices.push_back({it.Value(),{owner_id,point_semantic}});
                        }
                    }
                }
            }
            // Open section wires have one more authored point than edges.
            // Preserve that endpoint and its generated rail as original references.
            for(const auto index:{first_station(i),first_station(i)+1}) {
                const auto& station=stations[index];
                if(station.open_end_point_id.empty())continue;
                const auto endpoint=TopExp::LastVertex(station.edges.back(),true);
                const auto position=BRep_Tool::Pnt(endpoint);
                const auto suffix=":at:"+request.path_point_ids[index]+":profile:"+station.profile_id+":from:"+station.open_end_point_id;
                const auto semantic=request.canonical_station_ids.contains(request.path_point_ids[index])
                    ? "sweep:vertex"+suffix
                    : "sweep:vertex:"+std::string(index==first_station(i)?"start":"end")+suffix;
                for(TopExp_Explorer v(builder.Shape(),TopAbs_VERTEX);v.More();v.Next())
                    if(position.Distance(BRep_Tool::Pnt(TopoDS::Vertex(v.Current())))<=request.linear_tolerance) {
                        piece.vertices.push_back({v.Current(),{owner_id,semantic}});break;
                    }
                const auto& generated=builder.Generated(endpoint);
                for(TopTools_ListIteratorOfListOfShape it(generated);it.More();it.Next())
                    if(it.Value().ShapeType()==TopAbs_EDGE)piece.edges.push_back({it.Value(),{owner_id,
                        "sweep:"+segment.source_id+":profile:"+station.profile_id+":from:"+station.open_end_point_id}});
            }
            return piece;
        };
        PrimitiveData piece;
        if(segment.bezier_control_points.empty()&&segment.bezier_spans.empty()&&!segment.arc_midpoint) {
            // A linear sweep between its true section boundaries. This also
            // preserves oblique miter sections without the pipe algorithm
            // reorienting them onto planes normal to the spine.
            BRepOffsetAPI_ThruSections builder(request.make_solid,false,request.linear_tolerance);
            const auto first = first_station(i), last = first + 1;
            if(stations[first].point_ids.size()!=stations[last].point_ids.size() ||
               stations[first].edges.size()!=stations[last].edges.size())
                throw std::runtime_error("Nesouhlasí párování sousedních profilů Sweepu.");
            builder.CheckCompatibility(stations[first].point_ids.empty() && stations[last].point_ids.empty());
            builder.AddWire(stations[first].wire);builder.AddWire(stations[last].wire);
            builder.Build();piece=collect(builder);
        } else {
            // A rigid polygonal section on a circular route has an exact
            // revolution. Preserve its circular edges (including the R=0
            // inner collapse of a hem) instead of approximating it by a pipe.
            // The same authored Sweep identities are collected in either case.
            BRepAdaptor_Curve spine(spine_edges[i]);
            if(exact_revolution || (rigid_segmented_profile && spine.GetType()==GeomAbs_Circle)) {
                TopoDS_Shape base=request.make_solid?TopoDS_Shape(BRepBuilderAPI_MakeFace(stations[first_station(i)].wire).Face())
                    :TopoDS_Shape(stations[first_station(i)].wire);
                BRepPrimAPI_MakeRevol builder(base,spine.Circle().Axis(),spine.LastParameter()-spine.FirstParameter(),true);
                builder.Build();piece=collect(builder);
            } else {
            BRepOffsetAPI_MakePipeShell builder(BRepBuilderAPI_MakeWire(spine_edges[i]).Wire());
            builder.SetMode(false);
            builder.SetTolerance(request.linear_tolerance,request.linear_tolerance,1e-6);
            const auto first=first_station(i),last=first+1;
            builder.Add(stations[first].wire,path_vertices[first],false,false);
            builder.Add(stations[last].wire,path_vertices[last],false,false);
            builder.Build();
            if(request.make_solid&&!builder.MakeSolid())throw std::runtime_error("Sweep nelze uzavřít.");
            piece=collect(builder);
            }
        }
        if(result.shape.IsNull())result=std::move(piece);
        else {
            BRepAlgoAPI_Fuse join;set_boolean_inputs(join,result.shape,piece.shape);
            join.SetFuzzyValue(request.linear_tolerance);join.Build();
            if(!join.IsDone()||!BRepCheck_Analyzer(join.Shape()).IsValid())
                throw std::runtime_error("Úseky 3D Sweepu nelze spojit.");
            result.faces=propagate_topology(join,result.faces,piece.faces);
            result.edges=propagate_topology(join,result.edges,piece.edges);
            result.vertices=propagate_topology(join,result.vertices,piece.vertices);
            result.source_caps.insert(result.source_caps.end(),piece.source_caps.begin(),piece.source_caps.end());
            result.shape=join.Shape();
        }
    }
    if(request.make_solid) {
        int solids=0;
        for(TopExp_Explorer it(result.shape,TopAbs_SOLID);it.More();it.Next())++solids;
        if(solids!=1)throw std::runtime_error("Úseky Sweepu netvoří jedno souvislé těleso.");
    }
    return result;
}

std::string profile_cap_semantic_key(
    std::string_view role, std::string_view profile_region_id) {
    if (profile_region_id.empty()) {
        throw std::runtime_error(
            "Profile cap is missing its persisted profile-region parent");
    }
    return std::string(role) + ":from:" +
        std::to_string(profile_region_id.size()) + ":" +
        std::string(profile_region_id);
}

// Keep the explicit curve-to-edge correspondence produced while constructing
// the wire. OCCT traversal order must never assign a ZIMA source identity.
template <typename Request>
std::vector<SweepProfileWire> make_body_profiles(const Request& request,
    const Vec3& normal, const std::optional<Vec3>& radial = std::nullopt) {
    std::vector<SweepProfileWire> result;
    if (request.wall) {
        Sweep3DRequest thin;
        thin.thin_first=request.wall->first_offset;
        thin.thin_second=request.wall->second_offset;
        Sweep3DRequest::Section section;
        section.profile_normal=normal;section.circle_radial_direction=radial;
        section.thin_end_point_id=request.wall->end_point_id;
        section.profile.outer_profile=request.outer_profile;
        section.profile.inner_profiles=request.inner_profiles;
        section.profile.outer_edge_source_ids=request.outer_edge_source_ids;
        section.profile.outer_vertex_source_ids=request.outer_vertex_source_ids;
        auto profiles=make_thin_sweep_profiles(thin,section);
        result.push_back(std::move(profiles.outer));
        if (profiles.inner) result.push_back(std::move(*profiles.inner));
    } else {
        const auto append=[&](const auto& loop,const auto& curves,const auto& points) {
            SweepProfileWire profile;
            profile.wire=make_profile_wire(loop,normal,radial,&profile.edges);
            profile.curve_ids=curves;profile.point_ids=points;
            result.push_back(std::move(profile));
        };
        append(request.outer_profile,request.outer_edge_source_ids,request.outer_vertex_source_ids);
        if (request.inner_profiles.size()!=request.inner_edge_source_ids.size() ||
            request.inner_profiles.size()!=request.inner_vertex_source_ids.size())
            throw std::runtime_error("Profile provenance group mismatch");
        for(std::size_t i=0;i<request.inner_profiles.size();++i)
            append(request.inner_profiles[i],request.inner_edge_source_ids[i],request.inner_vertex_source_ids[i]);
    }
    for(std::size_t i=1;i<result.size();++i) result[i].wire.Reverse();
    return result;
}

void transform_profile_wire(SweepProfileWire& profile, const gp_Trsf& transform) {
    BRepBuilderAPI_Transform builder(profile.wire,transform,true);
    for(auto& edge:profile.edges) edge=TopoDS::Edge(builder.ModifiedShape(edge));
    profile.wire=TopoDS::Wire(builder.Shape());
}

// A surface sweeps profile wires; a solid sweeps their filled region.
TopoDS_Shape profile_base(const std::vector<TopoDS_Wire>& wires,bool surface) {
    if(surface) {
        TopoDS_Compound result;BRep_Builder builder;builder.MakeCompound(result);
        for(const auto& wire:wires)builder.Add(result,wire);
        return result;
    }
    BRepBuilderAPI_MakeFace builder(wires.front(),true);
    if(!builder.IsDone())throw std::runtime_error("OCCT profile face is invalid");
    for(std::size_t i=1;i<wires.size();++i) {
        // Offset wires can traverse the opposite way after a mirrored source
        // contour. Orient each hole on the actual outer face's plane before
        // reversing its finite boundary; a blind wire reversal can add material
        // instead of bounding the hole and produces an invalid planar face.
        BRepBuilderAPI_MakeFace hole(BRepAdaptor_Surface(builder.Face()).Plane(),wires[i],true);
        if(!hole.IsDone() || !BRepCheck_Analyzer(hole.Face()).IsValid())
            throw std::runtime_error("OCCT profile face is invalid");
        builder.Add(TopoDS::Wire(BRepTools::OuterWire(hole.Face()).Reversed()));
    }
    if(!builder.IsDone() || !BRepCheck_Analyzer(builder.Face()).IsValid())
        throw std::runtime_error("OCCT profile face is invalid");
    return builder.Face();
}

template<class Builder>
void append_surface_end_point(PrimitiveData& result,Builder& builder,
    const TopoDS_Vertex& vertex,const std::string& owner,const std::string& source,
    const std::string& first_role,const std::string& last_role,bool closed=false) {
    if(source.empty() || vertex.IsNull())return;
    for(TopTools_ListIteratorOfListOfShape it(builder.Generated(vertex));it.More();it.Next())
        if(it.Value().ShapeType()==TopAbs_EDGE)
            result.edges.push_back({it.Value(),{owner,"generated:"+source}});
    if(!closed) {
        const auto first=builder.FirstShape(vertex),last=builder.LastShape(vertex);
        if(!first.IsNull())result.vertices.push_back({first,{owner,first_role+":"+source}});
        if(!last.IsNull())result.vertices.push_back({last,{owner,last_role+":"+source}});
    }
}

// Build authored endpoint datums, never an epsilon-length solid. Keep each
// coincident cap in a separate packet so shape lookup cannot merge identities.
template<class Request>
std::vector<PrimitiveData> stationary_profile_data(const Request& request,
        const std::string& owner) {
    const Vec3 normal=[&] {
        if constexpr(std::is_same_v<Request,ExtrusionRequest>) {
            const double length=std::hypot(request.direction.x,request.direction.y,request.direction.z);
            if(!std::isfinite(length)||length<=1e-12)
                throw std::invalid_argument("Extrusion direction must be finite and non-zero");
            const double sign=request.first_cap_is_start?1.:-1.;
            return Vec3{sign*request.direction.x/length,sign*request.direction.y/length,sign*request.direction.z/length};
        } else return request.profile_normal;
    }();
    auto wire_normal=normal;
    if constexpr(std::is_same_v<Request,ExtrusionRequest>) {
        // Native polygon/arc winding already follows the authored extrusion
        // direction. The stationary reverse side must construct its analytic
        // hole wires in that same frame, even though its datum normal is fixed.
        const auto directed=[](const auto& loop){return std::holds_alternative<ExtrusionRequest::PolygonProfile>(loop)||
            std::holds_alternative<ExtrusionRequest::CurvedProfile>(loop);};
        if(!request.wall&&(directed(request.outer_profile)||std::ranges::any_of(request.inner_profiles,directed)))
            wire_normal=request.direction;
    }
    const auto profiles=make_body_profiles(request,wire_normal);
    std::vector<TopoDS_Wire> wires;
    for(const auto& profile:profiles)wires.push_back(profile.wire);
    const auto base=profile_base(wires,request.surface_result);
    std::vector<PrimitiveData> result;
    for(const auto* role:{"start","end"}) {
        PrimitiveData datum;datum.shape=base;datum.reference_only=true;
        if(!request.surface_result) {
            auto cap=base;
            if(std::string_view(role)=="start")cap.Reverse();
            datum.faces.push_back({cap,{owner,profile_cap_semantic_key(role,request.profile_region_id)}});
            datum.shape=cap;
        }
        for(const auto& profile:profiles) {
            if(profile.edges.size()!=profile.curve_ids.size() ||
               (!profile.point_ids.empty()&&profile.point_ids.size()!=profile.edges.size()))
                throw std::runtime_error("Profile provenance group mismatch");
            for(std::size_t i=0;i<profile.edges.size();++i) {
                datum.edges.push_back({profile.edges[i],{owner,std::string(role)+":"+profile.curve_ids[i]}});
                if(!profile.point_ids.empty())datum.vertices.push_back({
                    TopExp::FirstVertex(profile.edges[i],true),{owner,std::string(role)+":"+profile.point_ids[i]}});
            }
        }
        if(!request.open_profile_end_id.empty())datum.vertices.push_back({
            TopExp::LastVertex(profiles.front().edges.back(),true),{owner,std::string(role)+":"+request.open_profile_end_id}});
        result.push_back(std::move(datum));
    }
    for(const auto& region:request.additional_profile_regions) {
        auto child=request;child.additional_profile_regions.clear();
        child.profile_region_id=region.region_id;
        child.outer_profile=region.outer_profile;child.inner_profiles=region.inner_profiles;
        child.outer_edge_source_ids=region.outer_edge_source_ids;child.inner_edge_source_ids=region.inner_edge_source_ids;
        child.outer_vertex_source_ids=region.outer_vertex_source_ids;child.inner_vertex_source_ids=region.inner_vertex_source_ids;
        auto additional=stationary_profile_data(child,owner);
        result.insert(result.end(),std::make_move_iterator(additional.begin()),std::make_move_iterator(additional.end()));
    }
    return result;
}

double extrusion_limit_span(const ExtrusionLimitView& limit,const Vec3& unit,
    const TopoDS_Shape& face,const std::vector<TopoDS_Wire>& wires,bool wall_overhang=false) {
    if(limit.planar) {
        // Bound the complete analytic profile along the plane normal. A circle's
        // seam vertex does not prove that the rest stays before an inclined plane.
        gp_Trsf into_plane;
        into_plane.SetTransformation(gp_Ax3(gp_Pnt(limit.origin.x,limit.origin.y,limit.origin.z),
            gp_Dir(limit.normal.x,limit.normal.y,limit.normal.z)));
        const auto local=BRepBuilderAPI_Transform(face,into_plane,false).Shape();
        Bnd_Box bounds;BRepBndLib::AddOptimal(local,bounds,false,false);
        if(bounds.IsVoid())throw std::runtime_error("Extrusion profile has no bounds");
        double xmin,ymin,zmin,xmax,ymax,zmax;bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);
        const gp_Dir normal(limit.normal.x,limit.normal.y,limit.normal.z);
        const double dot=unit.x*normal.X()+unit.y*normal.Y()+unit.z*normal.Z();
        const double a=-zmin/dot,b=-zmax/dot;
        const double minimum=std::min(a,b),maximum=std::max(a,b);
        // A valid profile can touch the end plane at a corner or edge. It
        // must stay on the extrusion side and have a non-zero swept extent.
        // A Thin wall may straddle that touching edge; its authored profile
        // is checked separately before the wall itself is clipped.
        if(!std::isfinite(minimum)||!std::isfinite(maximum)||
                (!wall_overhang&&minimum < -1e-9)||maximum<=1e-9)
            throw std::runtime_error("Extrusion profile crosses or lies beyond target plane");
        return maximum+std::max(1.0,maximum*.01);
    }

        const auto ray_distance = [&](const gp_Pnt& point) {
            double nearest = std::numeric_limits<double>::infinity();
            const auto& triangles = limit.triangles;
            for (std::size_t index = 0; index < triangles.size(); index += 3) {
                const auto& v0 = triangles[index];
                const auto& v1 = triangles[index + 1];
                const auto& v2 = triangles[index + 2];
                const Vec3 edge1{v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
                const Vec3 edge2{v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
                const Vec3 h{unit.y * edge2.z - unit.z * edge2.y,
                             unit.z * edge2.x - unit.x * edge2.z,
                             unit.x * edge2.y - unit.y * edge2.x};
                const double determinant =
                    edge1.x * h.x + edge1.y * h.y + edge1.z * h.z;
                if (std::abs(determinant) <= 1e-12) continue;
                const double inverse = 1.0 / determinant;
                const Vec3 s{point.X() - v0.x, point.Y() - v0.y, point.Z() - v0.z};
                const double u = inverse * (s.x * h.x + s.y * h.y + s.z * h.z);
                if (u < -1e-8 || u > 1.0 + 1e-8) continue;
                const Vec3 q{s.y * edge1.z - s.z * edge1.y,
                             s.z * edge1.x - s.x * edge1.z,
                             s.x * edge1.y - s.y * edge1.x};
                const double v = inverse *
                    (unit.x * q.x + unit.y * q.y + unit.z * q.z);
                if (v < -1e-8 || u + v > 1.0 + 1e-8) continue;
                const double distance = inverse *
                    (edge2.x * q.x + edge2.y * q.y + edge2.z * q.z);
                if (distance > 1e-8) nearest = std::min(nearest, distance);
            }
            if (!std::isfinite(nearest)) {
                throw std::runtime_error("Extrusion profile misses target surface");
            }
            return nearest;
        };
        double maximum_distance = 0.0;
        for (const auto& wire : wires) {
            for (TopExp_Explorer explorer(wire, TopAbs_EDGE);
                 explorer.More(); explorer.Next()) {
                BRepAdaptor_Curve curve(TopoDS::Edge(explorer.Current()));
                const double first = curve.FirstParameter();
                const double last = curve.LastParameter();
                constexpr int samples = 32;
                for (int sample = 0; sample <= samples; ++sample) {
                    const double parameter = first +
                        (last - first) * static_cast<double>(sample) / samples;
                    maximum_distance = std::max(
                        maximum_distance, ray_distance(curve.Value(parameter)));
                }
            }
        }
        return maximum_distance+std::max(1.0,maximum_distance*.01);
}

std::optional<TopoDS_Face> exact_extrusion_limit(const ExtrusionLimitView& limit,const std::vector<OwnedFace>& faces) {
    if(limit.datum)return std::nullopt;
    std::vector<const OwnedFace*> matches;
    for(const auto& face:faces)if(face.reference.owner_id==limit.reference.owner_id &&
        face.reference.semantic_key==limit.reference.semantic_key && face.reference.instance_path==limit.reference.instance_path)matches.push_back(&face);
    if(matches.empty())throw std::runtime_error("Extrusion target face is missing at this history boundary");
    if(!limit.planar && matches.size()!=1)throw std::runtime_error("Exact Extrusion target surface is missing or ambiguous");
    return TopoDS::Face(matches.front()->shape);
}

ExtrusionLimitView resolved_extrusion_limit(ExtrusionLimitView limit,const std::optional<TopoDS_Face>& exact) {
    if(limit.planar && exact) {
        BRepAdaptor_Surface surface(*exact,true);
        if(surface.GetType()!=GeomAbs_Plane)throw std::runtime_error("Extrusion target face is no longer planar");
        const auto plane=surface.Plane();const auto p=plane.Location();const auto n=plane.Axis().Direction();
        limit.origin={p.X(),p.Y(),p.Z()};limit.normal={n.X(),n.Y(),n.Z()};
    }
    return limit;
}

// End rims and points keep their authored Sketch parents when a limiting face
// replaces an overrun cap. This also applies to open surface prisms/revolutions,
// where there is no cap face in the result to supply adjacency.
void locate_clipped_profile_end(BRepAlgoAPI_BooleanOperation& clip,PrimitiveData& data,
        const std::string& owner,const std::string& role) {
    TopTools_IndexedMapOfShape actual;TopExp::MapShapes(clip.Shape(),actual);
    const auto append=[&](auto& items,const TopoDS_Shape& shape,const std::string& key) {
        if(actual.Contains(shape)&&std::none_of(items.begin(),items.end(),[&](const auto& item){
            return item.shape.IsSame(shape)&&item.reference.owner_id==owner&&item.reference.semantic_key==key;
        }))items.push_back({shape,{owner,key,{}}});
    };
    const auto faces=data.faces;const auto edges=data.edges;
    for(const auto& side:faces)if(side.reference.semantic_key.starts_with("generated:"))
        for(TopTools_ListIteratorOfListOfShape it(clip.Generated(side.shape));it.More();it.Next())
            if(it.Value().ShapeType()==TopAbs_EDGE)
                append(data.edges,it.Value(),role+":"+side.reference.semantic_key.substr(10));
    for(const auto& edge:edges)if(edge.reference.semantic_key.starts_with("generated:"))
        for(TopTools_ListIteratorOfListOfShape it(clip.Generated(edge.shape));it.More();it.Next())
            if(it.Value().ShapeType()==TopAbs_VERTEX)
                append(data.vertices,it.Value(),role+":"+edge.reference.semantic_key.substr(10));
}

std::vector<OwnedEdge> complete_boolean_edges(const TopoDS_Shape&,
    const std::vector<OwnedFace>&,const std::vector<OwnedEdge>&,
    const std::string&,std::string_view,const std::vector<OwnedFace>&,
    const std::vector<OwnedVertex>&);
std::vector<OwnedVertex> complete_boolean_vertices(const TopoDS_Shape&,
    const std::vector<OwnedFace>&,const std::vector<OwnedEdge>&,
    const std::vector<OwnedVertex>&,const std::string&,std::string_view);
#include "ordered_up_to_profile.inc"

PrimitiveData make_extrusion_data(
    const ExtrusionRequest& request, const std::string& owner_id,
    const std::optional<TopoDS_Face>& original_exact_target = std::nullopt,
    double through_all_forward_span = 2'000'000.0,
    double through_all_reverse_span = 2'000'000.0,
    const std::optional<Vec3>& circle_radial_direction = std::nullopt,
    double linear_tolerance = 0.001,
    const std::optional<TopoDS_Face>& exact_reverse_target = std::nullopt,
    const std::vector<std::optional<TopoDS_Face>>& exact_ordered_targets = {},
    std::vector<ResolvedOrderedUpToLimit>* resolved_up_to=nullptr) {
    if(!std::isfinite(request.draft_angle_degrees)||std::abs(request.draft_angle_degrees)>=90)
        throw std::invalid_argument("Draft angle must be between -90 and 90 degrees.");
    if(request.draft_angle_degrees!=0&&request.surface_result&&!request.open_profile_end_id.empty())
        throw std::invalid_argument("Surface draft requires a closed profile.");
    auto exact_target=original_exact_target;
    auto normal=request.direction;
    if (request.wall) {
        const double scale=(request.first_cap_is_start?1.0:-1.0)/std::sqrt(
            normal.x*normal.x+normal.y*normal.y+normal.z*normal.z);
        normal={normal.x*scale,normal.y*scale,normal.z*scale};
    }
    auto profiles=make_body_profiles(request,normal,circle_radial_direction);
    std::vector<TopoDS_Wire> wires;
    for(const auto& profile:profiles) {
        wires.push_back(profile.wire);
    }
    TopoDS_Shape face=profile_base(wires,request.surface_result);
    const double direction_length = std::sqrt(
        request.direction.x * request.direction.x +
        request.direction.y * request.direction.y +
        request.direction.z * request.direction.z);
    const Vec3 unit{request.direction.x / direction_length,
                    request.direction.y / direction_length,
                    request.direction.z / direction_length};
    const auto reference_vertex=TopExp_Explorer(face,TopAbs_VERTEX);
    if(!reference_vertex.More())throw std::runtime_error("Extrusion profile has no reference point");
    const auto profile_keep_point=BRep_Tool::Pnt(TopoDS::Vertex(reference_vertex.Current()));
    std::optional<detail::OrderedLimitResult> ordered;
    if(request.ordered_end_limit) {
        ordered=prepare_ordered_up_to(*request.ordered_end_limit,exact_ordered_targets,face,linear_tolerance);
        if(request.mirror_forward_limit) {
            gp_Trsf mirror;mirror.SetMirror(gp_Ax2(profile_keep_point,gp_Dir(unit.x,unit.y,unit.z)));
            ordered=detail::transform_ordered_up_to(*ordered,mirror);
        }
        if(request.ordered_end_limit->path_correction!=0) {
            gp_Trsf advance;advance.SetTranslation(gp_Vec(unit.x,unit.y,unit.z)*request.ordered_end_limit->path_correction);
            ordered=detail::transform_ordered_up_to(*ordered,advance);
        }
    }
    std::optional<ExtrusionLimitView> forward_boundary,reverse_boundary;
    if(has_forward_limit(request))forward_boundary.emplace(resolved_extrusion_limit(forward_limit(request),exact_target));
    std::optional<ExtrusionLimit> mirrored_forward_limit;
    if(request.mirror_forward_limit&&forward_boundary) {
        const auto reflect_point=[&](Vec3 p) {
            const double distance=(p.x-profile_keep_point.X())*unit.x+(p.y-profile_keep_point.Y())*unit.y+(p.z-profile_keep_point.Z())*unit.z;
            return Vec3{p.x-2*distance*unit.x,p.y-2*distance*unit.y,p.z-2*distance*unit.z};
        };
        const auto& source=*forward_boundary;
        const double dot=source.normal.x*unit.x+source.normal.y*unit.y+source.normal.z*unit.z;
        mirrored_forward_limit=ExtrusionLimit{source.planar,source.reference,source.datum,reflect_point(source.origin),
            {source.normal.x-2*dot*unit.x,source.normal.y-2*dot*unit.y,source.normal.z-2*dot*unit.z},{}};
        for(const auto p:source.triangles)mirrored_forward_limit->triangles.push_back(reflect_point(p));
        forward_boundary.emplace(limit_view(*mirrored_forward_limit));
        if(exact_target) {
            gp_Trsf mirror;mirror.SetMirror(gp_Ax2(profile_keep_point,gp_Dir(unit.x,unit.y,unit.z)));
            exact_target=TopoDS::Face(BRepBuilderAPI_Transform(*exact_target,mirror,true).Shape());
        }
    }
    if(request.reverse_limit)reverse_boundary.emplace(resolved_extrusion_limit(limit_view(*request.reverse_limit),exact_reverse_target));
    std::optional<ExtrusionLimit> mirrored_limit;
    auto reverse_exact=exact_reverse_target;
    if(request.symmetric_limit) {
        const auto reflect_point=[&](Vec3 p) {
            const double distance=(p.x-profile_keep_point.X())*unit.x+(p.y-profile_keep_point.Y())*unit.y+(p.z-profile_keep_point.Z())*unit.z;
            return Vec3{p.x-2*distance*unit.x,p.y-2*distance*unit.y,p.z-2*distance*unit.z};
        };
        const auto reflect_direction=[&](Vec3 n) {const double dot=n.x*unit.x+n.y*unit.y+n.z*unit.z;return Vec3{n.x-2*dot*unit.x,n.y-2*dot*unit.y,n.z-2*dot*unit.z};};
        const auto& source=*forward_boundary;
        mirrored_limit=ExtrusionLimit{source.planar,source.reference,source.datum,reflect_point(source.origin),reflect_direction(source.normal),{}};
        for(const auto p:source.triangles)mirrored_limit->triangles.push_back(reflect_point(p));
        reverse_boundary.emplace(limit_view(*mirrored_limit));
        if(exact_target) {
            gp_Trsf mirror;mirror.SetMirror(gp_Ax2(profile_keep_point,gp_Dir(unit.x,unit.y,unit.z)));
            reverse_exact=TopoDS::Face(BRepBuilderAPI_Transform(*exact_target,mirror,true).Shape());
        }
    }
    double bounded_start=request.start_offset;
    double bounded_end=request.start_offset+direction_length;
    if(request.extent==ExtrusionRequest::Extent::ThroughAll && request.through_all_forward)
        bounded_end=std::max(1.0,through_all_forward_span);
    if(request.through_all_reverse)bounded_start=-std::max(1.0,through_all_reverse_span);
    if(request.wall && ((forward_boundary&&forward_boundary->planar)||(reverse_boundary&&reverse_boundary->planar))) {
        auto authored=request;authored.wall.reset();
        const auto source_profiles=make_body_profiles(authored,normal,circle_radial_direction);
        std::vector<TopoDS_Wire> source_wires;for(const auto& profile:source_profiles)source_wires.push_back(profile.wire);
        const auto source=profile_base(source_wires,true);
        if(forward_boundary&&forward_boundary->planar)static_cast<void>(extrusion_limit_span(*forward_boundary,unit,source,source_wires));
        if(reverse_boundary&&reverse_boundary->planar)static_cast<void>(extrusion_limit_span(*reverse_boundary,{-unit.x,-unit.y,-unit.z},source,source_wires));
    }
    if(forward_boundary)bounded_end=extrusion_limit_span(*forward_boundary,unit,face,wires,request.wall.has_value());
    if(ordered) {
        bounded_end=ordered_up_to_linear_span(*ordered,face,profile_keep_point,gp_Dir(unit.x,unit.y,unit.z));
    }
    if(reverse_boundary)bounded_start=-extrusion_limit_span(*reverse_boundary,{-unit.x,-unit.y,-unit.z},face,wires,request.wall.has_value());
    if(bounded_end<=bounded_start+1e-12)throw std::runtime_error("OCCT Extrusion interval is empty");
    if(std::abs(bounded_start)>1e-12) {
        gp_Trsf shift;shift.SetTranslation(gp_Vec(unit.x*bounded_start,unit.y*bounded_start,unit.z*bounded_start));
        for(std::size_t i=0;i<wires.size();++i){transform_profile_wire(profiles[i],shift);wires[i]=profiles[i].wire;}
        face=profile_base(wires,request.surface_result);
    }
    const double bounded_length=bounded_end-bounded_start;
    const Vec3 prism_direction{unit.x*bounded_length,unit.y*bounded_length,unit.z*bounded_length};
    BRepPrimAPI_MakePrism prism(face, gp_Vec(
        prism_direction.x, prism_direction.y, prism_direction.z), true, true);
    prism.Build();
    if (!prism.IsDone() || !BRepCheck_Analyzer(prism.Shape()).IsValid()) {
        throw std::runtime_error("OCCT extrusion failed or produced an invalid solid");
    }
    PrimitiveData result{prism.Shape(), {}, {}, {}};
    TopoDS_Shape first_boundary=prism.FirstShape(),last_boundary=prism.LastShape();
    if(request.centerlines.origin_enabled||request.centerlines.centroid_enabled||
       request.centerlines.individual_centroids_enabled) {
        const auto distance_to=[&](const ExtrusionLimitView& limit,const std::optional<TopoDS_Face>& exact,Vec3 seed,Vec3 direction)->double {
            using namespace profile_centerlines;
            if(limit.planar)return dimension_dot(dimension_sub(limit.origin,seed),limit.normal)/dimension_dot(direction,limit.normal);
            double nearest=std::numeric_limits<double>::infinity();
            if(exact){IntCurvesFace_ShapeIntersector hit;hit.Load(*exact,1e-8);hit.Perform(gp_Lin(gp_Pnt(seed.x,seed.y,seed.z),gp_Dir(direction.x,direction.y,direction.z)),0,1e12);
                if(hit.IsDone())for(int i=1;i<=hit.NbPnt();++i)nearest=std::min(nearest,hit.WParameter(i));
                if(std::isfinite(nearest))return nearest;
                throw std::runtime_error("Profile centerline does not intersect its end reference.");
            }
            for(std::size_t i=0;i+2<limit.triangles.size();i+=3){
                const auto a=limit.triangles[i],e1=dimension_sub(limit.triangles[i+1],a),e2=dimension_sub(limit.triangles[i+2],a);
                const auto h=dimension_cross(direction,e2);const double det=dimension_dot(e1,h);if(std::abs(det)<1e-14)continue;
                const auto d=dimension_sub(seed,a);const double u=dimension_dot(d,h)/det;if(u<-1e-9||u>1+1e-9)continue;
                const auto q=dimension_cross(d,e1);const double v=dimension_dot(direction,q)/det;if(v<-1e-9||u+v>1+1e-9)continue;
                const double t=dimension_dot(e2,q)/det;if(t>=0)nearest=std::min(nearest,t);
            }
            if(!std::isfinite(nearest))throw std::runtime_error("Profile centerline does not intersect its end reference.");
            return nearest;
        };
        for(const auto& [key,seed]:profile_centerlines::seeds(request)) {
            const double start=reverse_boundary?-distance_to(*reverse_boundary,reverse_exact,seed,{-unit.x,-unit.y,-unit.z}):bounded_start;
            double end=forward_boundary?distance_to(*forward_boundary,exact_target,seed,unit):bounded_end;
            if(ordered) {
                IntCurvesFace_ShapeIntersector hit;hit.Load(ordered->shell,linear_tolerance);
                hit.Perform(gp_Lin(gp_Pnt(seed.x,seed.y,seed.z),gp_Dir(unit.x,unit.y,unit.z)),0,bounded_end);
                end=std::numeric_limits<double>::infinity();
                if(hit.IsDone())for(int i=1;i<=hit.NbPnt();++i)end=std::min(end,hit.WParameter(i));
                if(!std::isfinite(end))throw std::runtime_error("Profile centerline does not intersect its end reference.");
            }
            const auto first=dimension_add(seed,dimension_scale(unit,start)),last=dimension_add(seed,dimension_scale(unit,end));
            profile_centerlines::line(result.profile_references,owner_id,key,first_semantic_cap_is_start(request)?first:last,first_semantic_cap_is_start(request)?last:first);
        }
    }

    const std::string first_role = first_semantic_cap_is_start(request) ? "start" : "end";
    const std::string last_role = first_semantic_cap_is_start(request) ? "end" : "start";
    FaceReference first_cap_reference{owner_id,
        profile_cap_semantic_key(first_role, request.profile_region_id), {}};
    FaceReference last_cap_reference{owner_id,
        profile_cap_semantic_key(last_role, request.profile_region_id), {}};
    // A planar Face has a free in-plane parameter direction. OCCT may choose
    // its X direction from a different edge after an otherwise equivalent
    // Boolean/replay, which would rotate a later TOP-constrained profile.
    // Publish the cap frame from the authored profile instead. The sampling
    // path below still validates it against the actual supporting plane.
    const auto cap_radial=[&]() -> std::optional<gp_Vec> {
        const gp_Vec axis(unit.x,unit.y,unit.z);
        for(const auto& profile:profiles)for(const auto& edge:profile.edges) {
            BRepAdaptor_Curve curve(edge);
            gp_Pnt point;gp_Vec tangent;
            curve.D1((curve.FirstParameter()+curve.LastParameter())*.5,
                point,tangent);
            tangent-=axis*tangent.Dot(axis);
            if(tangent.SquareMagnitude()>1e-18) {
                tangent.Normalize();return tangent;
            }
        }
        return {};
    }();
    if(cap_radial) {
        const auto cap_surface=[&](double station,double sign) {
            SurfaceGeometry surface;
            surface.kind=SurfaceGeometry::Kind::Plane;
            surface.origin={profile_keep_point.X()+unit.x*station,
                profile_keep_point.Y()+unit.y*station,
                profile_keep_point.Z()+unit.z*station};
            surface.axis={unit.x*sign,unit.y*sign,unit.z*sign};
            surface.radial={cap_radial->X(),cap_radial->Y(),cap_radial->Z()};
            return std::make_shared<const SurfaceGeometry>(surface);
        };
        first_cap_reference.surface=cap_surface(bounded_start,-1.);
        last_cap_reference.surface=cap_surface(bounded_end,1.);
    }
    if(!request.surface_result) {
        result.faces.push_back({prism.FirstShape(), first_cap_reference});
        result.faces.push_back({prism.LastShape(), last_cap_reference});
    }
    std::vector<std::vector<std::string>> edge_sources,vertex_sources;
    for(const auto& profile:profiles) {
        edge_sources.push_back(profile.curve_ids);vertex_sources.push_back(profile.point_ids);
    }
    if (edge_sources.size() != wires.size() || vertex_sources.size() != wires.size()) {
        throw std::runtime_error("Extrusion profile provenance group mismatch");
    }
    for (std::size_t wire_index = 0; wire_index < wires.size(); ++wire_index) {
        std::size_t boundary_edge{};
        for (; boundary_edge<profiles[wire_index].edges.size(); ++boundary_edge) {
            if (boundary_edge >= edge_sources[wire_index].size()) {
                throw std::runtime_error("Extrusion edge provenance mismatch");
            }
            const auto& edge = profiles[wire_index].edges[boundary_edge];
            const auto& curve_id = edge_sources[wire_index][boundary_edge];
            if (curve_id.empty()) {
                throw std::runtime_error("Extrusion curve provenance is empty");
            }
            const auto& generated = prism.Generated(edge);
            for (TopTools_ListIteratorOfListOfShape iterator(generated);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() == TopAbs_FACE) {
                    result.faces.push_back(
                        {iterator.Value(), {owner_id, "generated:" + curve_id}});
                }
            }
            const auto first = prism.FirstShape(edge);
            const auto last = prism.LastShape(edge);
            if (!first.IsNull()) {
                result.edges.push_back(
                    {first, {owner_id, first_role + ":" + curve_id}});
            }
            if (!last.IsNull()) {
                result.edges.push_back(
                    {last, {owner_id, last_role + ":" + curve_id}});
            }
            if (vertex_sources[wire_index].empty()) continue;
            if (boundary_edge >= vertex_sources[wire_index].size()) {
                throw std::runtime_error("Extrusion point provenance mismatch");
            }
            const auto& point_id = vertex_sources[wire_index][boundary_edge];
            const TopoDS_Vertex vertex = TopExp::FirstVertex(edge, true);
            const TopoDS_Vertex end_vertex = TopExp::LastVertex(edge, true);
            const bool closed_profile_curve = !vertex.IsNull() &&
                !end_vertex.IsNull() && vertex.IsSame(end_vertex);
            const auto first_vertex = prism.FirstShape(vertex);
            const auto last_vertex = prism.LastShape(vertex);
            if (!first_vertex.IsNull()) {
                result.vertices.push_back(
                    {first_vertex, {owner_id, first_role + ":" + point_id}});
            }
            if (!last_vertex.IsNull()) {
                result.vertices.push_back(
                    {last_vertex, {owner_id, last_role + ":" + point_id}});
            }
            const auto& generated_from_vertex = prism.Generated(vertex);
            for (TopTools_ListIteratorOfListOfShape iterator(generated_from_vertex);
                iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() == TopAbs_EDGE) {
                    result.edges.push_back(
                        {iterator.Value(), {owner_id,
                            (closed_profile_curve ? "seam:generated:"
                                                  : "generated:") + point_id}});
                }
            }
        }
        if (boundary_edge != edge_sources[wire_index].size() ||
            (!vertex_sources[wire_index].empty() &&
             boundary_edge != vertex_sources[wire_index].size())) {
            throw std::runtime_error("Extrusion profile provenance count mismatch");
        }
    }
    if(!request.open_profile_end_id.empty())
        append_surface_end_point(result,prism,TopExp::LastVertex(profiles.front().edges.back(),true),
            owner_id,request.open_profile_end_id,first_role,last_role);
    for(auto& owned:result.faces)owned.reference.surface_result=request.surface_result;
    const auto clip_end=[&](const ExtrusionLimitView& limit,const Vec3& outward,
        const std::optional<TopoDS_Face>& exact,const FaceReference& cap_reference) {
        TopoDS_Face limiting_face;
        gp_Pnt keep_point;
        if (limit.planar) {
            const gp_Dir plane_normal(limit.normal.x,
                                  limit.normal.y,
                                  limit.normal.z);
            const gp_Pln plane(gp_Pnt(limit.origin.x,
                                  limit.origin.y,
                                  limit.origin.z), plane_normal);
            BRepBuilderAPI_MakeFace plane_face(plane, -5'000'000.0, 5'000'000.0,
                                           -5'000'000.0, 5'000'000.0);
            limiting_face = plane_face.Face();
            const double side = outward.x * limit.normal.x +
                            outward.y * limit.normal.y +
                            outward.z * limit.normal.z;
            keep_point = gp_Pnt(
                limit.origin.x - std::copysign(1.0, side) * plane_normal.X(),
                limit.origin.y - std::copysign(1.0, side) * plane_normal.Y(),
                limit.origin.z - std::copysign(1.0, side) * plane_normal.Z());
        } else {
            if (!exact) {
                throw std::runtime_error("Exact Extrusion target surface is missing");
            }
            limiting_face = *exact;
            keep_point = profile_keep_point;
        }
        BRepPrimAPI_MakeHalfSpace half_space(limiting_face, keep_point);
        BRepAlgoAPI_Common clip;
        set_boolean_inputs(clip, result.shape, half_space.Solid());
        clip.SetFuzzyValue(linear_tolerance);
        clip.SetToFillHistory(true);
        clip.Build();
        if (!clip.IsDone() || clip.Shape().IsNull() ||
            !BRepCheck_Analyzer(clip.Shape()).IsValid()) {
            throw std::runtime_error("OCCT Up-to-plane clipping failed");
        }
        // The clipping face is the semantic replacement for the prism's
        // overrun cap. Its identity is defined before OCCT calculation by the
        // Extrusion feature, cap role and persisted profile-region parent.
        // OCCT history only locates the runtime descendant created by Common.
        locate_clipped_profile_end(clip,result,owner_id,
            cap_reference.semantic_key.starts_with("start:")?"start":"end");
        result.faces = propagate_topology(clip, result.faces,
            std::vector<OwnedFace>{{limiting_face, cap_reference}});
        TopTools_IndexedMapOfShape clipped_faces;
        TopExp::MapShapes(clip.Shape(), TopAbs_FACE, clipped_faces);
        std::erase_if(result.faces, [&](const auto& owned) {
            return !clipped_faces.Contains(owned.shape);
        });
        result.edges = propagate_topology(clip, result.edges, {});
        result.vertices = propagate_topology(clip, result.vertices, {});
        TopTools_IndexedMapOfShape clipped_members;TopExp::MapShapes(clip.Shape(),clipped_members);
        std::erase_if(result.edges,[&](const auto& item){return !clipped_members.Contains(item.shape);});
        std::erase_if(result.vertices,[&](const auto& item){return !clipped_members.Contains(item.shape);});
        result.shape = clip.Shape();
    };
    // OCCT Draft cannot reliably adjust the free curved rims of an open
    // shell. Draft its overrun prism first, then create the exact curved rim.
    const bool draft_before_clip=request.surface_result&&request.draft_angle_degrees!=0&&
        ((forward_boundary&&!forward_boundary->planar)||(reverse_boundary&&!reverse_boundary->planar));
    const bool ordered_draft_before_clip=ordered&&request.surface_result&&request.draft_angle_degrees!=0;
    if(!draft_before_clip) {
        if(forward_boundary)clip_end(*forward_boundary,unit,exact_target,last_cap_reference);
        if(reverse_boundary)clip_end(*reverse_boundary,{-unit.x,-unit.y,-unit.z},reverse_exact,first_cap_reference);
    }
    if(ordered&&!ordered_draft_before_clip)
        clip_ordered_up_to_profile(result,*ordered,*request.ordered_end_limit,first_boundary,last_boundary,
            profile_keep_point,last_cap_reference,last_role,request.surface_result,linear_tolerance);
    if(request.draft_angle_degrees!=0) {
        BRepOffsetAPI_DraftAngle draft(result.shape);
        const gp_Dir pull(unit.x,unit.y,unit.z);
        const gp_Pln neutral(profile_keep_point,pull);
        for(const auto& owned:result.faces)if(owned.reference.semantic_key.starts_with("generated:")) {
            const auto support=BRepAdaptor_Surface(TopoDS::Face(owned.shape)).GetType();
            if(support!=GeomAbs_Plane&&support!=GeomAbs_Cylinder&&support!=GeomAbs_Cone)
                throw std::runtime_error("Draft supports profiles made of lines and circular arcs.");
            // A prism of wires retains the wire-oriented surface, opposite to
            // the material-oriented side of a filled profile's solid prism.
            draft.Add(TopoDS::Face(owned.shape),pull,
                (request.surface_result?-1.:1.)*request.draft_angle_degrees*std::numbers::pi/180.,neutral);
            if(!draft.AddDone())throw std::runtime_error("Draft cannot preserve the profile; reduce the angle or length.");
        }
        draft.Build();
        if(!draft.IsDone()||draft.Shape().IsNull()||!BRepCheck_Analyzer(draft.Shape()).IsValid())
            throw std::runtime_error("Draft cannot preserve the profile; reduce the angle or length.");
        const auto preserve=[&](const auto& source) {
            auto updated=source;
            TopTools_IndexedMapOfShape actual;
            TopExp::MapShapes(draft.Shape(),actual);
            for(auto& item:updated) {
                const auto replacement=draft.ModifiedShape(item.shape);
                if(replacement.IsNull()||replacement.ShapeType()!=item.shape.ShapeType()||!actual.Contains(replacement))
                    throw std::runtime_error("Draft cannot preserve the profile; reduce the angle or length.");
                item.shape=replacement;
            }
            return updated;
        };
        result.faces=preserve(result.faces);result.edges=preserve(result.edges);result.vertices=preserve(result.vertices);
        if(ordered_draft_before_clip) {
            // Open surface boundaries are wires, not Draft-owned cap faces.
            // Assemble their already named descendants rather than asking
            // ModifiedShape for a wire that Draft need not map as a whole.
            BRep_Builder builder;TopoDS_Compound first,last;builder.MakeCompound(first);builder.MakeCompound(last);
            for(const auto& edge:result.edges) {
                if(edge.reference.semantic_key.starts_with(first_role+":"))builder.Add(first,edge.shape);
                if(edge.reference.semantic_key.starts_with(last_role+":"))builder.Add(last,edge.shape);
            }
            first_boundary=first;last_boundary=last;
        }
        BOPAlgo_ArgumentAnalyzer check;check.SetShape1(draft.Shape());check.SelfInterMode()=true;check.Perform();
        if(check.HasFaulty())throw std::runtime_error("Draft cannot preserve the profile; reduce the angle or length.");
        result.shape=draft.Shape();
    }
    if(draft_before_clip) {
        if(forward_boundary)clip_end(*forward_boundary,unit,exact_target,last_cap_reference);
        if(reverse_boundary)clip_end(*reverse_boundary,{-unit.x,-unit.y,-unit.z},reverse_exact,first_cap_reference);
    }
    if(ordered_draft_before_clip)clip_ordered_up_to_profile(result,*ordered,*request.ordered_end_limit,first_boundary,last_boundary,
        profile_keep_point,last_cap_reference,last_role,request.surface_result,linear_tolerance);
    if (!request.additional_profile_regions.empty()) {
        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        builder.Add(compound, result.shape);
        for (std::size_t index = 0;
             index < request.additional_profile_regions.size(); ++index) {
            auto additional_request = request;
            additional_request.centerlines.origin_enabled=additional_request.centerlines.centroid_enabled=false;
            additional_request.centerlines.individual_centroids_enabled=false;
            additional_request.outer_profile =
                request.additional_profile_regions[index].outer_profile;
            additional_request.inner_profiles =
                request.additional_profile_regions[index].inner_profiles;
            additional_request.profile_region_id =
                request.additional_profile_regions[index].region_id;
            additional_request.outer_boundary_id =
                request.additional_profile_regions[index].outer_boundary_id;
            additional_request.inner_boundary_ids =
                request.additional_profile_regions[index].inner_boundary_ids;
            additional_request.outer_edge_source_ids =
                request.additional_profile_regions[index].outer_edge_source_ids;
            additional_request.inner_edge_source_ids =
                request.additional_profile_regions[index].inner_edge_source_ids;
            additional_request.outer_vertex_source_ids =
                request.additional_profile_regions[index].outer_vertex_source_ids;
            additional_request.inner_vertex_source_ids =
                request.additional_profile_regions[index].inner_vertex_source_ids;
            additional_request.additional_profile_regions.clear();
            auto additional = make_extrusion_data(
                additional_request, owner_id, exact_target,
                through_all_forward_span, through_all_reverse_span, circle_radial_direction, linear_tolerance, exact_reverse_target,exact_ordered_targets,resolved_up_to);
            builder.Add(compound, additional.shape);
            result.faces.insert(result.faces.end(),
                std::make_move_iterator(additional.faces.begin()),
                std::make_move_iterator(additional.faces.end()));
            result.edges.insert(result.edges.end(),
                std::make_move_iterator(additional.edges.begin()),
                std::make_move_iterator(additional.edges.end()));
            result.vertices.insert(result.vertices.end(),
                std::make_move_iterator(additional.vertices.begin()),
                std::make_move_iterator(additional.vertices.end()));
        }
        result.shape = compound;
        if(request.draft_angle_degrees!=0) {
            BOPAlgo_ArgumentAnalyzer check;check.SetShape1(result.shape);check.SelfInterMode()=true;check.Perform();
            if(check.HasFaulty())throw std::runtime_error("Draft cannot preserve the profile; reduce the angle or length.");
        }
    }
    if(ordered)record_ordered_up_to_resolution(resolved_up_to,*request.ordered_end_limit,*ordered,owner_id,request.profile_region_id);
    return result;
}

std::string step_shape_locator(const TopoDS_Shape& shape) {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto append_integer = [&](std::uint64_t value) {
        for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
            hash ^= static_cast<unsigned char>(value >> (byte * 8));
            hash *= 1099511628211ULL;
        }
    };
    const auto append_real = [&](double value) {
        append_integer(std::bit_cast<std::uint64_t>(value));
    };
    const auto append_point = [&](const gp_Pnt& point) {
        append_real(point.X());
        append_real(point.Y());
        append_real(point.Z());
    };
    append_integer(static_cast<std::uint64_t>(shape.ShapeType()));
    append_integer(static_cast<std::uint64_t>(shape.Orientation()));
    const gp_Trsf placement = shape.Location().Transformation();
    for (int row = 1; row <= 3; ++row) {
        for (int column = 1; column <= 4; ++column) {
            append_real(placement.Value(row, column));
        }
    }
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    if (!bounds.IsVoid()) {
        Standard_Real xmin{}, ymin{}, zmin{}, xmax{}, ymax{}, zmax{};
        bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        for (const double value : {xmin, ymin, zmin, xmax, ymax, zmax}) {
            append_real(value);
        }
    }
    if (shape.ShapeType() == TopAbs_VERTEX) {
        append_point(BRep_Tool::Pnt(TopoDS::Vertex(shape)));
    } else if (shape.ShapeType() == TopAbs_EDGE) {
        const TopoDS_Edge edge = TopoDS::Edge(shape);
        BRepAdaptor_Curve curve(edge);
        append_integer(static_cast<std::uint64_t>(curve.GetType()));
        const double first = curve.FirstParameter();
        const double last = curve.LastParameter();
        append_real(first);
        append_real(last);
        for (const double fraction : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            append_point(curve.Value(first + (last - first) * fraction));
        }
        GProp_GProps properties;
        BRepGProp::LinearProperties(edge, properties);
        append_real(properties.Mass());
        append_point(properties.CentreOfMass());
    } else if (shape.ShapeType() == TopAbs_FACE) {
        const TopoDS_Face face = TopoDS::Face(shape);
        BRepAdaptor_Surface surface(face);
        append_integer(static_cast<std::uint64_t>(surface.GetType()));
        Standard_Real u_min{}, u_max{}, v_min{}, v_max{};
        BRepTools::UVBounds(face, u_min, u_max, v_min, v_max);
        for (const double value : {u_min, u_max, v_min, v_max}) {
            append_real(value);
        }
        GProp_GProps properties;
        BRepGProp::SurfaceProperties(face, properties);
        append_real(properties.Mass());
        append_point(properties.CentreOfMass());
        const gp_Mat inertia = properties.MatrixOfInertia();
        for (int row = 1; row <= 3; ++row) {
            for (int column = 1; column <= 3; ++column) {
                append_real(inertia.Value(row, column));
            }
        }
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index) {
        result[static_cast<std::size_t>(index)] = digits[hash & 0xfU];
        hash >>= 4;
    }
    return result;
}

template <typename Owned>
void capture_step_topology_kind(
    const STEPControl_Reader& reader, const TopoDS_Shape& imported,
    TopAbs_ShapeEnum shape_kind, StepRequest::TopologyIdentity::Kind identity_kind,
    const Handle(Standard_Type)& source_type, const char* semantic_prefix,
    const std::string& owner_id,
    std::vector<Owned>& owned,
    std::vector<StepRequest::TopologyIdentity>& identities) {
    const auto transfer = reader.WS()->TransferReader()->TransientProcess();
    const auto model = reader.Model();
    struct Candidate {
        TopoDS_Shape shape;
        std::string semantic_key;
        std::string locator;
    };
    TopTools_IndexedMapOfShape runtime_shapes;
    for (TopExp_Explorer explorer(imported, shape_kind);
         explorer.More(); explorer.Next()) {
        const TopoDS_Shape shape = explorer.Current();
        if (!runtime_shapes.Contains(shape)) runtime_shapes.Add(shape);
    }
    std::map<int, std::vector<Candidate>> by_runtime_shape;
    for (int index = 1; index <= model->NbEntities(); ++index) {
        const auto entity = model->Value(index);
        if (entity.IsNull() || !entity->IsKind(source_type)) continue;
        const TopoDS_Shape shape = TransferBRep::ShapeResult(transfer, entity);
        if (shape.IsNull()) continue;
        if (shape.ShapeType() != shape_kind) continue;
        std::vector<int> runtime_indices;
        const int exact_runtime_index = runtime_shapes.FindIndex(shape);
        if (exact_runtime_index != 0) {
            runtime_indices.push_back(exact_runtime_index);
        } else {
            // STEP assemblies transfer one definition subshape and place the
            // same TShape into one or more product occurrences. IsPartner()
            // follows that source relation without using traversal position.
            for (int runtime_index = 1;
                 runtime_index <= runtime_shapes.Size(); ++runtime_index) {
                if (runtime_shapes.FindKey(runtime_index).IsPartner(shape)) {
                    runtime_indices.push_back(runtime_index);
                }
            }
        }
        if (runtime_indices.empty()) continue;
        const int source_entity_number = model->Number(entity);
        if (source_entity_number <= 0) continue;
        const std::string source_label =
            "#" + std::to_string(source_entity_number);
        for (const int runtime_index : runtime_indices) {
            const TopoDS_Shape runtime_shape =
                runtime_shapes.FindKey(runtime_index);
            const std::string locator = step_shape_locator(runtime_shape);
            std::string semantic_key =
                std::string(semantic_prefix) + source_label;
            if (runtime_indices.size() > 1) {
                // One STEP definition can appear many times in an assembly.
                // The parent remains the source STEP entity; the occurrence
                // role comes from its persisted placement/shape locator.
                semantic_key += "@occ:" + locator;
            }
            by_runtime_shape[runtime_index].push_back({runtime_shape,
                std::move(semantic_key), locator});
        }
    }
    std::set<std::string> semantic_keys;
    for (auto& [runtime_index, candidates] : by_runtime_shape) {
        static_cast<void>(runtime_index);
        // A runtime subshape must come from exactly one canonical STEP
        // topological entity. Ambiguous transfer results remain display-only.
        if (candidates.size() != 1) continue;
        auto& candidate = candidates.front();
        if (!semantic_keys.insert(candidate.semantic_key).second) continue;
        owned.push_back({candidate.shape,
            {owner_id, candidate.semantic_key, {}}});
        identities.push_back({identity_kind, std::move(candidate.semantic_key),
            std::move(candidate.locator)});
    }
}

void capture_step_topology(const STEPControl_Reader& reader,
    const TopoDS_Shape& imported, const std::string& owner_id,
    PrimitiveData& result) {
    using Kind = StepRequest::TopologyIdentity::Kind;
    capture_step_topology_kind(reader, imported, TopAbs_FACE, Kind::Face,
        STANDARD_TYPE(StepShape_FaceSurface), "step:face:", owner_id,
        result.faces, result.imported_step_topology);
    capture_step_topology_kind(reader, imported, TopAbs_EDGE, Kind::Edge,
        STANDARD_TYPE(StepShape_EdgeCurve), "step:edge:", owner_id,
        result.edges, result.imported_step_topology);
    capture_step_topology_kind(reader, imported, TopAbs_VERTEX, Kind::Vertex,
        STANDARD_TYPE(StepShape_VertexPoint), "step:vertex:", owner_id,
        result.vertices, result.imported_step_topology);
}

// Semantic identities come from the source document. These opaque references
// address objects in one particular B-Rep archive; they are never topology IDs.
// Publish archive and bindings together whenever the archive is rewritten.
void persist_imported_topology(const PrimitiveData& data, BodyResult& result) {
    BRepTools_ShapeSet archive(false, false);
    archive.SetFormatNb(TopTools_FormatVersion_CURRENT);
    archive.Add(data.shape);
    const auto add = [&](const auto& owned) {
        for (const auto& item : owned) archive.Add(item.shape);
    };
    add(data.faces); add(data.edges); add(data.vertices);
    auto topology=data.imported_step_topology;
    const auto bind = [&](const auto& owned, StepRequest::TopologyIdentity::Kind kind) {
        // Index once: scanning all identities for every owned shape becomes
        // quadratic on whole-model imports with hundreds of thousands of IDs.
        std::unordered_map<std::string_view, StepRequest::TopologyIdentity*> identities;
        identities.reserve(owned.size());
        for (auto& identity : topology) {
            if (identity.kind == kind &&
                !identities.emplace(identity.semantic_key, &identity).second)
                throw std::runtime_error("Imported topology identity is duplicated");
        }
        for (const auto& item : owned) {
            const auto identity = identities.find(item.reference.semantic_key);
            if (identity == identities.end())
                throw std::runtime_error("Imported topology identity is missing");
            std::ostringstream address;
            archive.Write(item.shape, address);
            identity->second->shape_locator = "brep-ref-v1:" + address.str();
        }
    };
    using Kind = StepRequest::TopologyIdentity::Kind;
    bind(data.faces, Kind::Face); bind(data.edges, Kind::Edge); bind(data.vertices, Kind::Vertex);
    result.imported_step_topology=std::move(topology);
    std::ostringstream stream;
    archive.Write(stream);
    archive.Write(data.shape, stream);
    result.kernel_shape = stream.str();
}

template <typename Owned, typename Reference>
void restore_step_topology_kind(const BRepTools_ShapeSet& archive,
    const TopoDS_Shape& imported, TopAbs_ShapeEnum shape_kind,
    StepRequest::TopologyIdentity::Kind identity_kind,
    const std::vector<StepRequest::TopologyIdentity>& identities,
    const std::string& owner_id, std::vector<Owned>& owned) {
    TopTools_IndexedMapOfShape members;
    TopExp::MapShapes(imported, shape_kind, members);
    std::set<std::string> semantic_keys;
    TopTools_IndexedMapOfShape bound;
    for (const auto& identity : identities) {
        if (identity.kind != identity_kind) continue;
        constexpr std::string_view prefix = "brep-ref-v1:";
        if (!identity.shape_locator.starts_with(prefix) || identity.semantic_key.empty() ||
            !semantic_keys.insert(identity.semantic_key).second)
            throw std::runtime_error("Invalid imported topology binding");
        const auto address = identity.shape_locator.substr(prefix.size());
        std::istringstream fields(address);
        char orientation{}; int object{}, location{};
        if (!(fields >> orientation >> object >> location) ||
            std::string_view("+-ie").find(orientation) == std::string_view::npos ||
            object < 1 || object > archive.NbShapes() || location < 0)
            throw std::runtime_error("Invalid imported B-Rep object reference");
        fields >> std::ws;
        if (!fields.eof()) throw std::runtime_error("Invalid imported B-Rep reference suffix");
        TopoDS_Shape shape;
        std::istringstream stream(address);
        try {
            archive.Read(shape, stream);
        } catch (const Standard_Failure&) {
            throw std::runtime_error("Imported B-Rep object reference is out of range");
        }
        if (shape.IsNull() || shape.ShapeType() != shape_kind ||
            !members.Contains(shape) || bound.Contains(shape))
            throw std::runtime_error("Imported topology does not belong to its source body");
        bound.Add(shape);
        owned.push_back({shape, Reference{owner_id, identity.semantic_key, {}}});
    }
}

void restore_step_topology(const StepRequest& request,
    const BRepTools_ShapeSet& archive, const TopoDS_Shape& imported,
    const std::string& owner_id, PrimitiveData& result) {
    using Kind = StepRequest::TopologyIdentity::Kind;
    restore_step_topology_kind<OwnedFace, FaceReference>(archive, imported, TopAbs_FACE,
        Kind::Face, request.topology, owner_id, result.faces);
    restore_step_topology_kind<OwnedEdge, EdgeReference>(archive, imported, TopAbs_EDGE,
        Kind::Edge, request.topology, owner_id, result.edges);
    restore_step_topology_kind<OwnedVertex, VertexReference>(archive, imported, TopAbs_VERTEX,
        Kind::Vertex, request.topology, owner_id, result.vertices);
}

using StepDocumentCache = std::unique_ptr<StepSourceDocument>;

PrimitiveData make_step_data(
    const StepRequest& request, const std::string& owner_id,
    std::unordered_map<std::string, StepDocumentCache>& documents,
    const StepSourceDocument* source = nullptr) {
    PrimitiveData result;
    if (request.frozen_brep && !request.frozen_brep->empty()) {
        std::istringstream stream(*request.frozen_brep);
        BRep_Builder builder;
        BRepTools_ShapeSet archive(builder);
        archive.Read(stream);
        archive.Read(result.shape, stream);
        if (result.shape.IsNull()) {
            throw std::runtime_error("Frozen STEP body is invalid");
        }
        restore_step_topology(request, archive, result.shape, owner_id, result);
        result.imported_step_topology = request.topology;
    } else if (request.source_path.empty()) {
        throw std::invalid_argument("STEP source path is empty");
    } else if (request.component_path.empty()) {
        if(source)throw std::invalid_argument("STEP component is missing");
        STEPControl_Reader reader;
        if (reader.ReadFile(request.source_path.c_str()) != IFSelect_RetDone ||
            reader.TransferRoots() == 0) {
            throw std::runtime_error("OCCT STEP import failed");
        }
        result.shape = reader.OneShape();
        capture_step_topology(reader, result.shape, owner_id, result);
    } else {
        if(source&&source->source_path()!=request.source_path)
            throw std::invalid_argument("STEP component is missing");
        if(!source) {
            auto& cached=documents[request.source_path];
            if(!cached)cached=std::make_unique<StepSourceDocument>(request.source_path);
            source=cached.get();
        }
        TDF_Label definition;
        TDF_Tool::Label(
            source->document()->GetData(), request.component_path.c_str(), definition, false);
        result.shape = definition.IsNull() ? TopoDS_Shape{}
            : XCAFDoc_DocumentTool::ShapeTool(source->document()->Main())->GetShape(definition);
        if (result.shape.IsNull()) throw std::runtime_error("STEP component is missing");
        capture_step_topology(
            source->reader().Reader(), result.shape, owner_id, result);
    }
    if (result.shape.IsNull() || !BRepCheck_Analyzer(result.shape).IsValid()) {
        throw std::runtime_error("STEP did not produce a valid shape");
    }
    bool placed = false;
    for (const double value : {request.translation.x, request.translation.y, request.translation.z,
            request.rotation_degrees.x, request.rotation_degrees.y, request.rotation_degrees.z}) {
        if (!std::isfinite(value)) throw std::invalid_argument("Placement value must be finite.");
        placed = placed || value != 0;
    }
    if (placed) {
        // Recover source identities before positioning the runtime operand.
        // Moving the matching topology retains those exact owners and keys;
        // the frozen B-Rep and its archive locators are never rewritten.
        const TopLoc_Location location(primitive_transform(request.translation, request.rotation_degrees));
        result.shape.Move(location);
        for (auto& face : result.faces) face.shape.Move(location);
        for (auto& edge : result.edges) edge.shape.Move(location);
        for (auto& vertex : result.vertices) vertex.shape.Move(location);
    }
    return result;
}

void validate_revolution(const RevolutionRequest& request) {
    ExtrusionRequest profile_request;
    profile_request.outer_profile = request.outer_profile;
    profile_request.inner_profiles = request.inner_profiles;
    profile_request.additional_profile_regions = request.additional_profile_regions;
    profile_request.direction = request.profile_normal;
    validate_extrusion(profile_request,request.wall.has_value() || request.surface_result);
    const double axis_length = std::sqrt(
        request.axis_direction.x * request.axis_direction.x +
        request.axis_direction.y * request.axis_direction.y +
        request.axis_direction.z * request.axis_direction.z);
    if (!std::isfinite(request.axis_point.x) ||
        !std::isfinite(request.axis_point.y) ||
        !std::isfinite(request.axis_point.z) || !std::isfinite(axis_length) ||
        axis_length <= 1.0e-12 || !std::isfinite(request.start_angle_degrees) ||
        !std::isfinite(request.angle_degrees) ||
        request.angle_degrees <= 0.0 || request.angle_degrees > 360.0) {
        throw std::invalid_argument("Revolution axis or angle is invalid");
    }
}

#include "revolution_limit.inc"
PrimitiveData make_revolution_data(
    const RevolutionRequest& original_request, const std::string& owner_id,
    const std::optional<TopoDS_Face>& exact_target=std::nullopt,double tolerance=.001,
    double* angular_span=nullptr,
    const std::vector<std::optional<TopoDS_Face>>& exact_ordered_targets={},
    std::vector<ResolvedOrderedUpToLimit>* resolved_up_to=nullptr) {
    auto request=original_request;
    auto profiles=make_body_profiles(request,request.profile_normal);
    std::vector<TopoDS_Wire> wires;
    for(const auto& profile:profiles)wires.push_back(profile.wire);
    const gp_Ax1 axis(gp_Pnt(request.axis_point.x, request.axis_point.y,
                            request.axis_point.z),
                      gp_Dir(request.axis_direction.x, request.axis_direction.y,
                             request.axis_direction.z));
    TopoDS_Shape face=profile_base(wires,request.surface_result);
    if (std::abs(request.start_angle_degrees) > 1.0e-12) {
        gp_Trsf rotation;
        rotation.SetRotation(axis,
            request.start_angle_degrees * std::numbers::pi / 180.0);
        for (std::size_t i=0;i<wires.size();++i) {
            transform_profile_wire(profiles[i],rotation);wires[i]=profiles[i].wire;
        }
        face=profile_base(wires,request.surface_result);
    }
    std::optional<RevolutionContacts> contact;
    if(request.end_limit||request.ordered_end_limit) {
        contact=revolution_contacts(request,exact_target,tolerance,exact_ordered_targets,face);
        request.angle_degrees=revolution_limit_span(wires,*contact,angular_span);
    }
    else if(angular_span)*angular_span=request.angle_degrees;
    BRepPrimAPI_MakeRevol revolution(
        face, axis,
        request.angle_degrees * std::numbers::pi / 180.0, true);
    revolution.Build();
    if (!revolution.IsDone() ||
        !BRepCheck_Analyzer(revolution.Shape()).IsValid()) {
        throw std::runtime_error("OCCT Revolution failed or produced an invalid solid");
    }
    PrimitiveData result{revolution.Shape(), {}, {}, {}};
    result.profile_references=profile_centerlines::revolution(request,owner_id);
    const std::string first_role = first_semantic_cap_is_start(request) ? "start" : "end";
    const std::string last_role = first_semantic_cap_is_start(request) ? "end" : "start";
    if (!request.surface_result && request.angle_degrees < 360.0 - 1.0e-9) {
        result.faces.push_back(
            {revolution.FirstShape(), {owner_id,
                profile_cap_semantic_key(
                    first_role, request.profile_region_id), {}}});
        result.faces.push_back(
            {revolution.LastShape(), {owner_id,
                profile_cap_semantic_key(
                    last_role, request.profile_region_id), {}}});
    }
    std::vector<std::vector<std::string>> edge_sources,vertex_sources;
    for(const auto& profile:profiles) {
        edge_sources.push_back(profile.curve_ids);vertex_sources.push_back(profile.point_ids);
    }
    if (edge_sources.size() != wires.size() || vertex_sources.size() != wires.size()) {
        throw std::runtime_error("Revolution profile provenance group mismatch");
    }
    for (std::size_t wire_index = 0; wire_index < wires.size(); ++wire_index) {
        std::size_t boundary_edge{};
        for (; boundary_edge<profiles[wire_index].edges.size(); ++boundary_edge) {
            if (boundary_edge >= edge_sources[wire_index].size()) {
                throw std::runtime_error("Revolution edge provenance mismatch");
            }
            const auto& edge = profiles[wire_index].edges[boundary_edge];
            const auto& curve_id = edge_sources[wire_index][boundary_edge];
            const auto& generated = revolution.Generated(edge);
            for (TopTools_ListIteratorOfListOfShape iterator(generated);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() == TopAbs_FACE) {
                    result.faces.push_back(
                        {iterator.Value(), {owner_id, "generated:" + curve_id}});
                }
            }
            if (request.angle_degrees < 360.0 - 1.0e-9) {
                const auto first = revolution.FirstShape(edge);
                const auto last = revolution.LastShape(edge);
                if (!first.IsNull()) {
                    result.edges.push_back(
                        {first, {owner_id, first_role + ":" + curve_id}});
                }
                if (!last.IsNull()) {
                    result.edges.push_back(
                        {last, {owner_id, last_role + ":" + curve_id}});
                }
            }
            if (vertex_sources[wire_index].empty()) continue;
            if (boundary_edge >= vertex_sources[wire_index].size()) {
                throw std::runtime_error("Revolution point provenance mismatch");
            }
            const auto& point_id = vertex_sources[wire_index][boundary_edge];
            const TopoDS_Vertex vertex = TopExp::FirstVertex(edge, true);
            const auto& generated_from_vertex = revolution.Generated(vertex);
            for (TopTools_ListIteratorOfListOfShape iterator(generated_from_vertex);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() == TopAbs_EDGE) {
                    result.edges.push_back(
                        {iterator.Value(), {owner_id, "generated:" + point_id}});
                }
            }
            if (request.angle_degrees < 360.0 - 1.0e-9) {
                const auto first = revolution.FirstShape(vertex);
                const auto last = revolution.LastShape(vertex);
                if (!first.IsNull()) {
                    result.vertices.push_back(
                        {first, {owner_id, first_role + ":" + point_id}});
                }
                if (!last.IsNull()) {
                    result.vertices.push_back(
                        {last, {owner_id, last_role + ":" + point_id}});
                }
            }
        }
        if (boundary_edge != edge_sources[wire_index].size() ||
            (!vertex_sources[wire_index].empty() &&
             boundary_edge != vertex_sources[wire_index].size())) {
            throw std::runtime_error("Revolution profile provenance count mismatch");
        }
    }
    if(!request.open_profile_end_id.empty())
        append_surface_end_point(result,revolution,TopExp::LastVertex(profiles.front().edges.back(),true),
            owner_id,request.open_profile_end_id,first_role,last_role,request.angle_degrees>=360.0-1e-9);
    for(auto& owned:result.faces)owned.reference.surface_result=request.surface_result;
    if(contact) {
        clip_revolution_end(result,request,*contact,revolution.FirstShape(),revolution.LastShape(),
            BRep_Tool::Pnt(TopExp::FirstVertex(profiles.front().edges.front(),true)),owner_id);
        result.profile_references={};
        for(const auto& [key,seed]:profile_centerlines::seeds(request)) {
            auto line=request;line.centerlines.origin_enabled=true;line.centerlines.centroid_enabled=false;
            line.centerlines.individual_centroids_enabled=false;
            line.centerlines.origin=seed;
            const gp_Vec radial(gp_Pnt(request.axis_point.x,request.axis_point.y,request.axis_point.z),gp_Pnt(seed.x,seed.y,seed.z));
            if(gp_Vec(contact->axis.Direction()).Crossed(radial).Magnitude()>contact->tolerance)
            {
                gp_Pnt start(seed.x,seed.y,seed.z);gp_Trsf rotation;
                rotation.SetRotation(axis,request.start_angle_degrees*std::numbers::pi/180.);
                start.Transform(rotation);
                line.angle_degrees=contact->angles(start).first*180/std::numbers::pi;
            }
            auto refs=profile_centerlines::revolution(line,owner_id);
            const auto original="centerline:from:origin:"+line.centerlines.origin_id;
            for(auto& edge:refs.edges)edge.reference.semantic_key=key;
            for(auto& point:refs.points) {
                const auto offset=point.reference.semantic_key.find(original);
                if(offset!=std::string::npos)point.reference.semantic_key.replace(offset,original.size(),key);
            }
            append_reference_geometry(result.profile_references,std::move(refs));
        }
    }
    if (!request.additional_profile_regions.empty()) {
        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        builder.Add(compound, result.shape);
        for (std::size_t index = 0;
             index < request.additional_profile_regions.size(); ++index) {
            auto additional_request = request;
            additional_request.centerlines.origin_enabled=additional_request.centerlines.centroid_enabled=false;
            additional_request.centerlines.individual_centroids_enabled=false;
            additional_request.outer_profile =
                request.additional_profile_regions[index].outer_profile;
            additional_request.inner_profiles =
                request.additional_profile_regions[index].inner_profiles;
            additional_request.profile_region_id =
                request.additional_profile_regions[index].region_id;
            additional_request.outer_boundary_id =
                request.additional_profile_regions[index].outer_boundary_id;
            additional_request.inner_boundary_ids =
                request.additional_profile_regions[index].inner_boundary_ids;
            additional_request.outer_edge_source_ids =
                request.additional_profile_regions[index].outer_edge_source_ids;
            additional_request.inner_edge_source_ids =
                request.additional_profile_regions[index].inner_edge_source_ids;
            additional_request.outer_vertex_source_ids =
                request.additional_profile_regions[index].outer_vertex_source_ids;
            additional_request.inner_vertex_source_ids =
                request.additional_profile_regions[index].inner_vertex_source_ids;
            additional_request.additional_profile_regions.clear();
            double additional_span{};
            auto additional = make_revolution_data(additional_request, owner_id,exact_target,tolerance,&additional_span,exact_ordered_targets,resolved_up_to);
            if(angular_span)*angular_span=std::max(*angular_span,additional_span);
            builder.Add(compound, additional.shape);
            result.faces.insert(result.faces.end(),
                std::make_move_iterator(additional.faces.begin()),
                std::make_move_iterator(additional.faces.end()));
            result.edges.insert(result.edges.end(),
                std::make_move_iterator(additional.edges.begin()),
                std::make_move_iterator(additional.edges.end()));
            result.vertices.insert(result.vertices.end(),
                std::make_move_iterator(additional.vertices.begin()),
                std::make_move_iterator(additional.vertices.end()));
        }
        result.shape = compound;
    }
    if(contact&&contact->ordered)record_ordered_up_to_resolution(resolved_up_to,*request.ordered_end_limit,*contact->ordered,owner_id,request.profile_region_id);
    return result;
}

template <typename Algorithm, typename Owned>
std::vector<Owned> propagate_topology(
    Algorithm& algorithm,
    const std::vector<Owned>& existing,
    const std::vector<Owned>& operand) {
    std::vector<Owned> propagated;
    const auto append_unique = [&](const TopoDS_Shape& shape,
                                   const auto& reference) {
        if (std::none_of(propagated.begin(), propagated.end(),
                [&](const auto& value) {
                    return value.shape.IsSame(shape) &&
                        value.reference == reference;
                })) {
            propagated.push_back({shape, reference});
        }
    };
    auto propagate_one = [&](const Owned& source) {
        bool has_descendant = false;
        for (const auto* list : {
                &algorithm.Modified(source.shape), &algorithm.Generated(source.shape)}) {
            for (TopTools_ListIteratorOfListOfShape iterator(*list);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() != source.shape.ShapeType()) continue;
                append_unique(iterator.Value(), source.reference);
                has_descendant = true;
            }
        }
        if (!has_descendant && !algorithm.IsDeleted(source.shape)) {
            append_unique(source.shape, source.reference);
        }
    };
    for (const auto& source : existing) propagate_one(source);
    for (const auto& source : operand) propagate_one(source);
    return propagated;
}

std::vector<OwnedEdge> complete_boolean_edges(const TopoDS_Shape&,
    const std::vector<OwnedFace>&,const std::vector<OwnedEdge>&,
    const std::string&,std::string_view,
    const std::vector<OwnedFace>& = {},const std::vector<OwnedVertex>& = {});
std::vector<OwnedFace> complete_boolean_faces(const TopoDS_Shape&,
    const std::vector<OwnedFace>&,const std::vector<OwnedEdge>&,
    const std::vector<OwnedVertex>&,const std::string&,std::string_view,
    const std::vector<HistoryOperation>&);
std::vector<OwnedVertex> complete_boolean_vertices(const TopoDS_Shape&,
    const std::vector<OwnedFace>&,const std::vector<OwnedEdge>&,
    const std::vector<OwnedVertex>&,const std::string&,std::string_view);

#include "solid_straightening.inc"
Sweep3DRequest expand_profile_twist(const Sweep3DRequest& source,
        double coefficient,bool straight,GProp_GProps* measured) {
    if(!source.twist||source.sections.size()!=1||!source.make_solid||source.thin||
       !std::isfinite(coefficient)||coefficient<=0)
        throw std::invalid_argument("Straightening requires a solid with a constant cross-section.");
    const auto law=*source.twist;
    if(!std::isfinite(law.length)||law.length<=0||!std::isfinite(law.angle_degrees)||
       std::abs(law.angle_degrees)>36000||(law.axis_point&&
       (!std::isfinite(law.axis_point->x)||!std::isfinite(law.axis_point->y)||!std::isfinite(law.axis_point->z))))
        throw std::invalid_argument("Invalid Feature definition.");
    const auto& seed=source.sections.front();
    ExtrusionRequest profile;
    profile.outer_profile=seed.profile.outer_profile;profile.inner_profiles=seed.profile.inner_profiles;
    profile.outer_edge_source_ids=seed.profile.outer_edge_source_ids;profile.outer_vertex_source_ids=seed.profile.outer_vertex_source_ids;
    profile.inner_edge_source_ids=seed.profile.inner_edge_source_ids;profile.inner_vertex_source_ids=seed.profile.inner_vertex_source_ids;
    std::vector<TopoDS_Wire> wires;
    for(const auto& item:make_body_profiles(profile,seed.profile_normal,seed.circle_radial_direction))wires.push_back(item.wire);
    GProp_GProps area;BRepGProp::SurfaceProperties(profile_base(wires,false),area);
    if(!std::isfinite(area.Mass())||area.Mass()<=0)
        throw std::invalid_argument("Straightening requires a valid cross-section area.");
    if(measured)*measured=area;
    const gp_Ax1 axis(law.axis_point?solid_straightening::point(*law.axis_point):area.CentreOfMass(),
        gp_Dir(seed.profile_normal.x,seed.profile_normal.y,seed.profile_normal.z));
    const auto span_id=law.axis_point?"twist:path:span:from:"+seed.profile.region_id:std::string("twist:path:span");
    auto target=source;target.twist.reset();target.sections.clear();target.path_points.clear();
    target.path_point_ids.clear();target.path_segments.clear();target.smooth_loft=true;
    const int intervals=straight?1:std::clamp(static_cast<int>(std::ceil(std::abs(law.angle_degrees)/5)),1,720);
    for(int i=0;i<=intervals;++i) {
        const double fraction=static_cast<double>(i)/intervals;
        const double angle=straight?0:law.angle_degrees*(law.smooth?sheet_material::twist_progress(fraction):fraction);
        gp_Trsf turn;turn.SetRotation(axis,angle*std::numbers::pi/180.);
        const gp_Vec shift(axis.Direction().XYZ()*(law.length*coefficient*fraction));
        turn.SetTranslationPart(turn.TranslationPart()+shift.XYZ());
        auto section=seed;
        solid_straightening::transform_loop(section.profile.outer_profile,turn);
        for(auto& hole:section.profile.inner_profiles)solid_straightening::transform_loop(hole,turn);
        if(section.circle_radial_direction) {
            const auto d=*section.circle_radial_direction;
            section.circle_radial_direction=solid_straightening::vector(gp_Vec(d.x,d.y,d.z).Transformed(turn).XYZ());
        }
        section.point_index=i;section.point_id=i==0?"twist:path:start":i==intervals?"twist:path:end":"twist:path:interior";
        const auto center=solid_straightening::vector(area.CentreOfMass().Translated(shift).XYZ());
        target.path_points.push_back(center);target.path_point_ids.push_back(section.point_id);
        if(i>0&&i<intervals)target.canonical_station_ids.insert(section.point_id);
        if(i)target.path_segments.push_back({span_id,target.path_points[i-1],center});
        target.sections.push_back(std::move(section));
    }
    if(law.centerlines.origin_enabled||law.centerlines.centroid_enabled) {
        ViewerReferenceGeometry datums;
        const auto center=solid_straightening::vector(axis.Location().XYZ());
        const auto direction=solid_straightening::vector(axis.Direction().XYZ());
        const auto shift=dimension_scale(direction,law.length*coefficient);
        if(law.centerlines.centroid_enabled)
            profile_centerlines::line(datums,{},"centerline:from:centroid:"+law.centerlines.profile_id,center,dimension_add(center,shift));
        if(law.centerlines.origin_enabled) {
            const auto key="centerline:from:origin:"+law.centerlines.origin_id;
            const auto origin=solid_straightening::point(law.centerlines.origin);
            if(straight||std::abs(law.angle_degrees)<1e-12||gp_Vec(axis.Location(),origin).Crossed(gp_Vec(axis.Direction())).Magnitude()<1e-12) {
                profile_centerlines::line(datums,{},key,law.centerlines.origin,dimension_add(law.centerlines.origin,shift));
            } else {
                Handle(TColgp_HArray1OfPnt) samples=new TColgp_HArray1OfPnt(1,intervals+1);
                for(int i=0;i<=intervals;++i) {
                    const double fraction=double(i)/intervals;
                    gp_Trsf turn;turn.SetRotation(axis,law.angle_degrees*(law.smooth?sheet_material::twist_progress(fraction):fraction)*std::numbers::pi/180.);
                    const auto point=origin.Transformed(turn).Translated(gp_Vec(axis.Direction().XYZ()*(law.length*coefficient*fraction)));
                    samples->SetValue(i+1,point);
                }
                GeomAPI_Interpolate interpolation(samples,false,source.linear_tolerance);
                interpolation.Perform();
                if(!interpolation.IsDone())throw std::runtime_error("OCCT profile interpolating spline failed");
                BRepAdaptor_Curve curve(BRepBuilderAPI_MakeEdge(interpolation.Curve()).Edge());
                ViewerEdge edge;edge.reference.semantic_key=key;edge.construction=edge.overlay=edge.dash_dot=true;
                edge.exact_spline=capture_bspline_geometry(curve);
                GCPnts_TangentialDeflection points(curve,.15,source.linear_tolerance);
                for(int i=1;i<=points.NbPoints();++i)edge.points.push_back(solid_straightening::vector(points.Value(i).XYZ()));
                datums.edges.push_back(std::move(edge));
                profile_centerlines::endpoints(datums,{},key,solid_straightening::vector(samples->Value(1).XYZ()),
                    solid_straightening::vector(samples->Value(intervals+1).XYZ()));
            }
        }
        target.profile_references=std::move(datums);
    }
    validate_sweep3d(target);return target;
}
#include "solid_state_replay.inc"
#include "sheet_cut_clearance.inc"
#include "sheet_state_geometry.inc"
#include "sheet_rip_geometry.inc"
#include "sheet_state_sources.inc"

PrimitiveData select_sheet_clearance_projection(const TopoDS_Face& face,
        const FaceReference& reference,const std::vector<OwnedEdge>& input_edges,
        const std::vector<OwnedFace>& input_faces,const TopoDS_Shape& input_shape,
        const PrimitiveData& requested,const PrimitiveData& full,double model_tolerance) {
    const double tolerance=std::min(model_tolerance,1.e-7);
    auto stock=sheet_cut_normal_volume(face,reference,input_edges,tolerance);
    const auto intersect=[&](const PrimitiveData& a,const PrimitiveData& b) {
        BRepAlgoAPI_Common common;set_sheet_cut_boolean_inputs(common,a.shape,b.shape);
        common.SetToFillHistory(true);common.SetFuzzyValue(tolerance);common.Build();
        if(!common.IsDone()||!BRepCheck_Analyzer(common.Shape()).IsValid())
            throw std::runtime_error("Sheet Cut could not resolve a valid reached sheet region.");
        PrimitiveData result;result.shape=common.Shape();
        result.faces=propagate_topology(common,a.faces,b.faces);
        result.edges=propagate_topology(common,a.edges,b.edges);
        result.edges=complete_boolean_edges(result.shape,result.faces,result.edges,
            reference.owner_id,"sheetcut-project");
        return result;
    };
    PrimitiveData actual;actual.shape=input_shape;actual.faces=input_faces;actual.edges=input_edges;
    stock=intersect(stock,actual);
    auto passage=intersect(stock,full);
    // Length selects the connected wall intersections it reaches. Once reached,
    // that wall is cut through; another disconnected wall remains untouched.
    TopoDS_Compound selected;BRep_Builder builder;builder.MakeCompound(selected);
    Bnd_Box requested_bounds;BRepBndLib::Add(requested.shape,requested_bounds);
    for(TopExp_Explorer solids(passage.shape,TopAbs_SOLID);solids.More();solids.Next()) {
        Bnd_Box bounds;BRepBndLib::Add(solids.Current(),bounds);
        if(bounds.IsOut(requested_bounds))continue;
        BRepAlgoAPI_Common reached;set_sheet_cut_boolean_inputs(reached,solids.Current(),requested.shape);
        reached.SetFuzzyValue(tolerance);reached.Build();
        if(!reached.IsDone()||!BRepCheck_Analyzer(reached.Shape()).IsValid())
            throw std::runtime_error("Sheet Cut could not resolve a valid projection range.");
        GProp_GProps area;BRepGProp::SurfaceProperties(reached.Shape(),area);
        if(area.Mass()>tolerance*tolerance)builder.Add(selected,solids.Current());
    }
    passage.shape=selected;
    return passage;
}

// Project the authored cutting profile onto the actual sheet side, then carry
// that trimmed domain through the thickness. A spatial prism alone would leave
// non-normal walls on cylinders/cones. All OCCT work stays at calculation time.
bool symbolic_form_face(const FaceReference& reference) {
    std::string storage;auto key=solid_state_source_key(reference.semantic_key,storage);
    std::string copied_key;
    for(;;) {
        if(key.starts_with("sheet-state:from:")) {
            key.remove_prefix(17);const auto separator=key.find(':');
            if(separator==std::string_view::npos)return false;key.remove_prefix(separator+1);
            continue;
        }
        if(key.starts_with("pattern:")) {
            key.remove_prefix(8);const auto separator=key.find(':');
            if(separator==std::string_view::npos)return false;key.remove_prefix(separator+1);
        }
        const auto parent=mirror_parent_reference(std::string(key));
        if(!parent)break;
        copied_key=parent->semantic_key;key=copied_key;
    }
    return key.starts_with("form:shape:")||
        (reference.sheet_owner==reference.owner_id&&reference.sheet_thickness>0.&&
         key.starts_with("fillet:face:")&&key.find("form:shape:")!=std::string_view::npos);
}

PrimitiveData make_sheet_cut_data(const PrimitiveData& projection,
        const std::vector<OwnedFace>& input_faces,const std::vector<OwnedEdge>& input_edges,
        const TopoDS_Shape& input_shape,const std::string& owner,double tolerance,
        const PrimitiveData* clearance_projection=nullptr,double cut_tolerance=.05) {
    PrimitiveData result;BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
    Bnd_Box projection_bounds;BRepBndLib::Add(projection.shape,projection_bounds);
    const auto token=[](const std::string& text){return std::to_string(text.size())+":"+text;};
    const auto vec=[](const auto& p)->Vec3{return {p.X(),p.Y(),p.Z()};};
    for(const auto& source:input_faces) {
        if(source.reference.sheet_role!=SheetFaceRole::SideA)continue;
        // Idealized FORM skins are visual manufacturing indications, not
        // analytic sheet regions used by Sheet Cut's normal-wall projection.
        if(symbolic_form_face(source.reference))continue;
        Bnd_Box source_bounds;BRepBndLib::Add(source.shape,source_bounds);
        source_bounds.Enlarge(source.reference.sheet_thickness);
        if(source_bounds.IsOut(projection_bounds))continue;
        auto face=TopoDS::Face(source.shape);
        // Generator history may return a face with standalone orientation.
        // Its occurrence in the real input solid defines the outward side.
        bool found=false;
        for(TopExp_Explorer faces(input_shape,TopAbs_FACE);faces.More();faces.Next())
            if(faces.Current().IsSame(source.shape)){face=TopoDS::Face(faces.Current());found=true;break;}
        if(!found)continue;
        auto skin_edges=input_edges;
        const auto on_analytic_support=[&](const occ::handle<Geom_Surface>& surface) {
            // A converted support has a different UV frame. Work on private
            // edges, rebuild their pcurves, and retain their native parents.
            BRepBuilderAPI_Copy copied(face,true,false);
            auto local=TopoDS::Face(copied.Shape());local.Orientation(TopAbs_FORWARD);
            TopoDS_Face support;BRep_Builder builder;builder.MakeFace(support,surface,tolerance);
            ShapeFix_Edge project;
            for(TopExp_Explorer edges(local,TopAbs_EDGE);edges.More();edges.Next()) {
                const auto edge=TopoDS::Edge(edges.Current());
                project.FixAddPCurve(edge,support,BRep_Tool::IsClosed(edge,local),tolerance);
                project.FixSameParameter(edge,support,tolerance);
            }
            const auto outer=BRepTools::OuterWire(local);
            BRepBuilderAPI_MakeFace build(surface,outer,true);
            for(TopExp_Explorer wires(local,TopAbs_WIRE);wires.More();wires.Next())
                if(!wires.Current().IsSame(outer))build.Add(TopoDS::Wire(wires.Current()));
            if(!build.IsDone())throw std::runtime_error("Sheet Cut surface projection failed.");
            ShapeFix_Face repair(build.Face());occ::handle<ShapeBuild_ReShape> context=new ShapeBuild_ReShape;
            repair.SetContext(context);repair.SetPrecision(tolerance);repair.SetMaxTolerance(tolerance);
            repair.Perform();auto converted=repair.Face();converted.Orientation(face.Orientation());
            BRepCheck_Analyzer checked(converted);
            if(!checked.IsValid()) {
                if(std::getenv("ZIMA_CPP_SHEET_CUT_PROFILE")) {
                    std::cerr<<"Sheet Cut analytic support invalid: type="<<GeomAdaptor_Surface(surface).GetType()<<'\n';
                    const auto dump=[&](const TopoDS_Shape& shape) {
                        const auto& result=checked.Result(shape);
                        for(const auto status:result->Status())if(status!=BRepCheck_NoError)
                            std::cerr<<" shape="<<shape.ShapeType()<<" status="<<status<<'\n';
                        for(result->InitContextIterator();result->MoreShapeInContext();result->NextShapeInContext())
                            for(const auto status:result->StatusOnShape())if(status!=BRepCheck_NoError)
                                std::cerr<<" shape="<<shape.ShapeType()<<" context="<<result->ContextualShape().ShapeType()<<" status="<<status<<'\n';
                    };
                    dump(converted);
                    for(TopExp_Explorer edges(converted,TopAbs_EDGE);edges.More();edges.Next())dump(edges.Current());
                }
                throw std::runtime_error("Sheet Cut surface projection failed.");
            }
            TopTools_IndexedMapOfShape members;TopExp::MapShapes(face,TopAbs_EDGE,members);
            for(auto& edge:skin_edges)if(members.Contains(edge.shape)) {
                const auto image=copied.ModifiedShape(edge.shape);
                if(!image.IsNull())edge.shape=context->Apply(image);
            }
            face=converted;
        };
        const bool twisted_source=source.reference.semantic_key.find("twist:")!=
            std::string::npos;
        // A sampled Sheet Profile loft retains a cylindrical skin. Resolve
        // its analytic support only during this explicit cut calculation.
        if(!twisted_source&&BRepAdaptor_Surface(face).GetType()==GeomAbs_BSplineSurface) {
            BRepAdaptor_Surface original(face);
            GeomConvert_SurfToAnaSurf conversion(BRep_Tool::Surface(face));
            const auto analytic=conversion.ConvertToAnalytical(tolerance,
                original.FirstUParameter(),original.LastUParameter(),
                original.FirstVParameter(),original.LastVParameter());
            if(!analytic.IsNull()&&GeomAdaptor_Surface(analytic).GetType()==GeomAbs_Cylinder) {
                on_analytic_support(analytic);
            }
        }
        if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane&&
           BRepAdaptor_Surface(face).GetType()!=GeomAbs_Cylinder&&
           BRepAdaptor_Surface(face).GetType()!=GeomAbs_Cone&&
           !(twisted_source&&BRepAdaptor_Surface(face).GetType()==
                GeomAbs_BSplineSurface)) {
            GeomLib_IsPlanarSurface planar(BRep_Tool::Surface(face),tolerance);
            if(!planar.IsPlanar())throw std::runtime_error(
                "Sheet Cut supports planar, cylindrical, conical and twisted sheet regions.");
            auto plane=planar.Plan();BRepAdaptor_Surface original(face);
            gp_Pnt point;gp_Vec du,dv;original.D1((original.FirstUParameter()+original.LastUParameter())*.5,
                (original.FirstVParameter()+original.LastVParameter())*.5,point,du,dv);
            if(du.Crossed(dv).Dot(gp_Vec(plane.Axis().Direction()))<0)plane.UReverse();
            on_analytic_support(new Geom_Plane(plane));
        }
        BRepAlgoAPI_Common clip;
        PrimitiveData domain;
        if(clearance_projection) {
            const auto selected=select_sheet_clearance_projection(face,source.reference,input_edges,
                input_faces,input_shape,projection,*clearance_projection,tolerance);
            if(!TopExp_Explorer(selected.shape,TopAbs_SOLID).More())continue;
            domain=sheet_cut_clearance_domain(face,source.reference,input_edges,*clearance_projection,selected,tolerance,cut_tolerance);
        } else {
            set_boolean_inputs(clip,face,projection.shape);
            clip.SetToFillHistory(true);clip.SetFuzzyValue(tolerance);clip.Build();
            if(!clip.IsDone()) {
                if(std::getenv("ZIMA_CPP_SHEET_CUT_PROFILE")) {
                    std::cerr<<"Sheet Cut failed skin "<<source.reference.owner_id<<" "<<source.reference.semantic_key
                        <<" type="<<BRepAdaptor_Surface(face).GetType()<<" valid="<<BRepCheck_Analyzer(face).IsValid()
                        <<" projection_valid="<<BRepCheck_Analyzer(projection.shape).IsValid()<<'\n';
                    clip.DumpErrors(std::cerr);
                }
                throw std::runtime_error("Sheet Cut surface projection failed.");
            }
            domain.shape=clip.Shape();
        }
        for(TopExp_Explorer patches(domain.shape,TopAbs_FACE);patches.More();patches.Next()) {
            auto patch=TopoDS::Face(patches.Current());patch.Orientation(face.Orientation());
            GProp_GProps area;BRepGProp::SurfaceProperties(patch,area);
            if(area.Mass()<=tolerance*tolerance)continue;
            SheetCutRegion region;region.cut_owner=owner;region.source=source.reference;
            region.thickness=source.reference.sheet_thickness;
            if(!(region.thickness>0))throw std::runtime_error("Sheet Cut source has no thickness.");
            BRepAdaptor_Surface surface(patch);gp_Ax3 frame;
            switch(surface.GetType()) {
            case GeomAbs_Plane: region.surface_type="plane";frame=surface.Plane().Position();break;
            case GeomAbs_Cylinder: region.surface_type="cylinder";frame=surface.Cylinder().Position();region.radius=surface.Cylinder().Radius();break;
            case GeomAbs_Cone: region.surface_type="cone";frame=surface.Cone().Position();region.radius=surface.Cone().RefRadius();region.semi_angle=surface.Cone().SemiAngle();break;
            case GeomAbs_BSplineSurface: {
                if(!twisted_source)throw std::runtime_error(
                    "Sheet Cut received an unrelated spline sheet region.");
                region.surface_type="twist";
                const double u=(surface.FirstUParameter()+surface.LastUParameter())*.5;
                const double v=(surface.FirstVParameter()+surface.LastVParameter())*.5;
                gp_Pnt point;gp_Vec du,dv;surface.D1(u,v,point,du,dv);
                const auto normal=du.Crossed(dv);
                if(du.SquareMagnitude()<1e-20||normal.SquareMagnitude()<1e-20)
                    throw std::runtime_error("Sheet Cut found a degenerate twisted sheet region.");
                frame=gp_Ax3(point,gp_Dir(normal),gp_Dir(du));break;
            }
            default: throw std::runtime_error(
                "Sheet Cut supports planar, cylindrical, conical and twisted sheet regions.");
            }
            region.origin=vec(frame.Location());region.axis=vec(frame.Direction());
            region.x_axis=vec(frame.XDirection());region.y_axis=vec(frame.YDirection());
            if(region.surface_type!="twist") {
                auto surface_data=std::make_shared<SurfaceGeometry>();
                surface_data->kind=region.surface_type=="plane"?SurfaceGeometry::Kind::Plane:
                    region.surface_type=="cylinder"?SurfaceGeometry::Kind::Cylinder:SurfaceGeometry::Kind::Cone;
                surface_data->origin=region.origin;surface_data->axis=region.axis;surface_data->radial=region.x_axis;
                surface_data->radius=region.radius;surface_data->semi_angle=region.semi_angle;
                surface_data->reversed=patch.Orientation()==TopAbs_REVERSED;region.source.surface=surface_data;
            } else region.source.surface.reset();
            const auto child=[&](std::string role,const std::string& parent_owner,const std::string& parent_key) {
                return "sheetcut:"+role+":source:"+token(source.reference.owner_id)+":"+token(source.reference.semantic_key)+
                    ":from:"+token(parent_owner)+":"+token(parent_key);
            };
            std::vector<OwnedEdge> patch_edges;
            for(TopExp_Explorer wires(patch,TopAbs_WIRE);wires.More();wires.Next()) {
                std::vector<SheetTrimCurve> loop;
                for(BRepTools_WireExplorer edges(TopoDS::Wire(wires.Current()),patch);edges.More();edges.Next()) {
                    const auto edge=edges.Current();
                    std::set<std::pair<std::string,std::string>> parents;
                    const auto descendant=[&](const auto& input) {
                        if(input.shape.IsSame(edge))return true;
                        if(clearance_projection)return false;
                        for(const auto* list:{&clip.Generated(input.shape),&clip.Modified(input.shape)})
                            for(TopTools_ListIteratorOfListOfShape it(*list);it.More();it.Next())
                                if(it.Value().IsSame(edge))return true;
                        return false;
                    };
                    if(clearance_projection) {
                        for(const auto& e:domain.edges)if(descendant(e))parents.emplace(e.reference.owner_id,e.reference.semantic_key);
                    } else {
                        for(const auto& f:projection.faces)if(descendant(f))parents.emplace(f.reference.owner_id,f.reference.semantic_key);
                        // At an exactly coincident start/end skin, Common can
                        // retain the projection's rim instead of generating an
                        // edge from its side face. That rim already has native
                        // Sketch-curve ancestry; consume its exact history.
                        if(parents.empty())for(const auto& e:projection.edges)
                            if(descendant(e))parents.emplace(e.reference.owner_id,e.reference.semantic_key);
                        // Up-to clipping creates a new rim on a retained side
                        // face. Common at that coincident rim need not report
                        // the face as its generator. Locate the exact boundary
                        // on the already named Sketch-curve child, excluding
                        // profile-region caps from curve ancestry.
                        if(parents.empty())for(const auto& f:projection.faces)
                            if(f.reference.semantic_key.starts_with("generated:"))
                                for(TopExp_Explorer rims(f.shape,TopAbs_EDGE);rims.More();rims.Next())
                                    if(rims.Current().IsSame(edge)) {
                                        parents.emplace(f.reference.owner_id,f.reference.semantic_key);break;
                                    }
                    }
                    for(const auto& e:skin_edges)if(descendant(e))parents.emplace(e.reference.owner_id,e.reference.semantic_key);
                    if(parents.empty())throw std::runtime_error("Sheet Cut cannot resolve a trim boundary's authored ancestry.");
                    std::string parent_key;for(const auto& [po,pk]:parents)parent_key+=token(po)+":"+token(pk)+":";
                    const auto key=child("boundary",owner,parent_key);
                    patch_edges.push_back({edge,{owner,key}});
                    double first{},last{};const auto curve=BRep_Tool::CurveOnSurface(edge,patch,first,last);
                    if(curve.IsNull())throw std::runtime_error("Sheet Cut is missing its material-space boundary.");
                    const auto spline=Geom2dConvert::CurveToBSplineCurve(new Geom2d_TrimmedCurve(curve,first,last));
                    SheetTrimCurve trim;trim.parent_owner=owner;trim.parent_key=key;trim.degree=spline->Degree();trim.reversed=edge.Orientation()==TopAbs_REVERSED;
                    for(int i=1;i<=spline->NbPoles();++i){const auto p=spline->Pole(i);trim.poles.push_back({p.X(),p.Y()});trim.weights.push_back(spline->Weight(i));}
                    for(int i=1;i<=spline->NbKnots();++i){trim.knots.push_back(spline->Knot(i));trim.multiplicities.push_back(spline->Multiplicity(i));}
                    loop.push_back(std::move(trim));
                }
                region.loops.push_back(std::move(loop));
            }
            // Offset the correctly oriented input skin into its material.
            // The low-level simple builder exposes both wall and offset ancestry.
            // OCCT's analytic cone offset uses the radial direction without
            // accounting for an indirect UV frame (unlike its cylinder offset).
            // Preserve the authored frame and compensate only the offset sign.
            // Pass the opposite skin by the Boolean tolerance so a separately
            // represented coincident cone cannot leave a zero-thickness cap.
            // The walls follow the same normals; the real input body trims them.
            const double tool_depth=region.thickness+
                (surface.GetType()==GeomAbs_Cone?10*tolerance:0);
            const double offset=surface.GetType()==GeomAbs_Cone&&!frame.Direct()
                ? tool_depth : -tool_depth;
            BRepOffset_MakeSimpleOffset thicken(patch,offset);
            thicken.SetBuildSolidFlag(true);thicken.SetTolerance(tolerance);thicken.Perform();
            if(!thicken.IsDone()||thicken.GetResultShape().IsNull()||!BRepCheck_Analyzer(thicken.GetResultShape()).IsValid())
                throw std::runtime_error("Sheet Cut could not carry its boundary through the sheet thickness.");
            bool solid_found=false;
            for(TopExp_Explorer solids(thicken.GetResultShape(),TopAbs_SOLID);solids.More();solids.Next()) {
                auto solid=TopoDS::Solid(solids.Current());
                if(!BRepLib::OrientClosedSolid(solid))throw std::runtime_error("Sheet Cut could not orient its thickness volume.");
                builder.Add(compound,solid);solid_found=true;
            }
            if(!solid_found)throw std::runtime_error("Sheet Cut thickness did not produce a solid.");
            const auto add_face=[&](const TopoDS_Shape& shape,const std::string& key) {
                if(shape.IsNull()||shape.ShapeType()!=TopAbs_FACE)return;
                FaceReference ref{owner,key};ref.sheet_role=SheetFaceRole::ThicknessFace;ref.sheet_thickness=region.thickness;ref.sheet_owner=region.source.sheet_owner;
                result.faces.push_back({shape,ref});
            };
            // The two skins coincide with the source sheet. Their surviving
            // faces retain the source identity through the subtraction history;
            // assigning tool identities as well would make them ambiguous.
            for(const auto& edge:patch_edges) {
                // Generated reports the offset image, not the connecting wall.
                // Locate the authored wall by its source boundary edge.
                const auto offset_skin=thicken.Generated(patch);
                for(TopExp_Explorer walls(thicken.GetResultShape(),TopAbs_FACE);walls.More();walls.Next()) {
                    const auto& wall=walls.Current();
                    if(wall.IsSame(patch)||(!offset_skin.IsNull()&&wall.IsSame(offset_skin)))continue;
                    for(TopExp_Explorer rims(wall,TopAbs_EDGE);rims.More();rims.Next())
                        if(rims.Current().IsSame(edge.shape)) {
                            add_face(wall,child("wall",edge.reference.owner_id,edge.reference.semantic_key));break;
                        }
                }
                result.edges.push_back(edge);
                const auto offset_edge=thicken.Generated(edge.shape);
                if(!offset_edge.IsNull()&&offset_edge.ShapeType()==TopAbs_EDGE)
                    result.edges.push_back({offset_edge,{owner,child("offset-edge",owner,edge.reference.semantic_key)}});
            }
            result.sheet_cuts.push_back(std::move(region));
        }
    }
    if(result.sheet_cuts.empty()) {
        if(!std::ranges::any_of(input_faces,[](const auto& face){return face.reference.sheet_role==SheetFaceRole::SideA;}))
            throw std::runtime_error("Sheet Cut profile does not intersect a supported sheet side.");
        // The projection was already calculated from a valid authored profile.
        // A miss is a valid no-op cut, with the original tool ancestry retained.
        result=projection;result.empty_material_operand=true;return result;
    }
    result.shape=compound;return result;
}

template <typename Algorithm, typename Owned>
std::vector<Owned> propagate_edge_treatment_topology(
    Algorithm& algorithm,
    const TopoDS_Shape& result_shape,
    const std::vector<Owned>& existing) {
    // BRepFilletAPI_MakeFillet and BRepFilletAPI_MakeChamfer populate their
    // internal result map with faces only. Consequently IsDeleted(edge) and
    // IsDeleted(vertex) report true even when the exact input sub-shape still
    // exists in the result. Never use that broken history predicate here.
    //
    // OCCT remains the geometry calculator. ZIMA owns persistent identity:
    // exact survivors retain their reference, while reported descendants are
    // accepted only when they really occur in the calculated result.
    TopTools_IndexedMapOfShape result_shapes;
    TopExp::MapShapes(result_shape, result_shapes);
    std::vector<Owned> propagated;
    const auto append_unique = [&](const TopoDS_Shape& shape,
                                   const auto& reference) {
        if (!result_shapes.Contains(shape) ||
            std::any_of(propagated.begin(), propagated.end(),
                [&](const auto& value) {
                    return value.shape.IsSame(shape) &&
                        value.reference == reference;
                })) {
            return;
        }
        propagated.push_back({shape, reference});
    };
    for (const auto& source : existing) {
        if (result_shapes.Contains(source.shape)) {
            append_unique(source.shape, source.reference);
        }
        for (const auto* descendants : {
                &algorithm.Modified(source.shape),
                &algorithm.Generated(source.shape)}) {
            for (TopTools_ListIteratorOfListOfShape iterator(*descendants);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() != source.shape.ShapeType()) {
                    continue;
                }
                append_unique(iterator.Value(), source.reference);
            }
        }
    }
    return propagated;
}

template <typename Reference, typename Owned>
class TopologyReferenceIndex {
public:
    explicit TopologyReferenceIndex(const std::vector<Owned>& owned_shapes) {
        references_.resize(1);
        ambiguous_.resize(1);
        for (const auto& owned : owned_shapes) {
            int index = shapes_.FindIndex(owned.shape);
            if (index == 0) {
                index = shapes_.Add(owned.shape);
                references_.resize(static_cast<std::size_t>(index) + 1);
                ambiguous_.resize(static_cast<std::size_t>(index) + 1);
                references_[static_cast<std::size_t>(index)] = owned.reference;
            } else if (references_[static_cast<std::size_t>(index)] !=
                       owned.reference) {
                // Multiple persisted parents for one runtime OCCT shape are
                // deliberately ambiguous and therefore not selectable.
                ambiguous_[static_cast<std::size_t>(index)] = true;
            }
        }
    }

    [[nodiscard]] Reference reference_for(const TopoDS_Shape& shape) const {
        const int index = shapes_.FindIndex(shape);
        if (index == 0 || ambiguous_[static_cast<std::size_t>(index)]) return {};
        return references_[static_cast<std::size_t>(index)];
    }

private:
    TopTools_IndexedMapOfShape shapes_;
    std::vector<Reference> references_;
    std::vector<bool> ambiguous_;
};

template <typename Algorithm>
std::vector<TopoDS_Shape> propagate_display_edges(
    Algorithm& algorithm, const std::vector<TopoDS_Shape>& existing) {
    std::vector<TopoDS_Shape> propagated;
    const auto append_unique = [&](const TopoDS_Shape& shape) {
        if (shape.ShapeType() != TopAbs_EDGE ||
            std::any_of(propagated.begin(), propagated.end(),
                [&](const auto& value) { return value.IsSame(shape); })) {
            return;
        }
        propagated.push_back(shape);
    };
    for (const auto& source : existing) {
        bool has_descendant = false;
        for (const auto* list : {
                &algorithm.Modified(source), &algorithm.Generated(source)}) {
            for (TopTools_ListIteratorOfListOfShape iterator(*list);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() != TopAbs_EDGE) continue;
                append_unique(iterator.Value());
                has_descendant = true;
            }
        }
        if (!has_descendant && !algorithm.IsDeleted(source)) {
            append_unique(source);
        }
    }
    return propagated;
}

std::vector<TopoDS_Shape> propagate_display_edges(
    const Handle(BRepTools_History)& history,
    const std::vector<TopoDS_Shape>& existing) {
    if (history.IsNull()) return existing;
    std::vector<TopoDS_Shape> propagated;
    const auto append_unique = [&](const TopoDS_Shape& shape) {
        if (shape.ShapeType() != TopAbs_EDGE ||
            std::any_of(propagated.begin(), propagated.end(),
                [&](const auto& value) { return value.IsSame(shape); })) {
            return;
        }
        propagated.push_back(shape);
    };
    for (const auto& source : existing) {
        bool has_descendant = false;
        for (const auto* list : {
                &history->Modified(source), &history->Generated(source)}) {
            for (TopTools_ListIteratorOfListOfShape iterator(*list);
                 iterator.More(); iterator.Next()) {
                if (iterator.Value().ShapeType() != TopAbs_EDGE) continue;
                append_unique(iterator.Value());
                has_descendant = true;
            }
        }
        if (!has_descendant && !history->IsRemoved(source)) append_unique(source);
    }
    return propagated;
}

std::vector<TopoDS_Shape> cross_reference_face_edges(
    const TopoDS_Shape& shape, const std::vector<OwnedFace>& owned_faces,
    bool merge_coplanar=false,double tolerance=1e-7) {
    const TopologyReferenceIndex<FaceReference, OwnedFace> references(owned_faces);
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(
        shape, TopAbs_EDGE, TopAbs_FACE, edge_faces);
    std::vector<TopoDS_Shape> result;
    for (int index = 1; index <= edge_faces.Extent(); ++index) {
        const auto& adjacent = edge_faces.FindFromIndex(index);
        if (adjacent.Extent() != 2) continue;
        TopTools_ListIteratorOfListOfShape iterator(adjacent);
        const auto first_face=TopoDS::Face(iterator.Value());
        const auto first = references.reference_for(iterator.Value());
        iterator.Next();
        const auto second_face=TopoDS::Face(iterator.Value());
        const auto second = references.reference_for(iterator.Value());
        if (!first.valid() || !second.valid() || first == second) continue;
        if(merge_coplanar&&first.surface_result==second.surface_result&&first.sheet_role==second.sheet_role) {
            const BRepAdaptor_Surface a(first_face),b(second_face);
            if(a.GetType()==GeomAbs_Plane&&b.GetType()==GeomAbs_Plane) {
                auto normal_a=a.Plane().Axis().Direction();auto normal_b=b.Plane().Axis().Direction();
                if(!a.Plane().Position().Direct())normal_a.Reverse();
                if(!b.Plane().Position().Direct())normal_b.Reverse();
                if(first_face.Orientation()==TopAbs_REVERSED)normal_a.Reverse();
                if(second_face.Orientation()==TopAbs_REVERSED)normal_b.Reverse();
                if(normal_a.Angle(normal_b)<=1e-9&&a.Plane().Distance(b.Plane().Location())<=tolerance)
                    continue;
            }
        }
        result.push_back(edge_faces.FindKey(index));
    }
    return result;
}

bool history_edge_survives(const Handle(BRepTools_History)& history,
    const TopoDS_Shape& edge, const TopTools_IndexedMapOfShape& result_edges) {
    if (result_edges.Contains(edge)) return true;
    if (history.IsNull()) return false;
    for (const auto* list : {
            &history->Modified(edge), &history->Generated(edge)}) {
        for (TopTools_ListIteratorOfListOfShape iterator(*list);
             iterator.More(); iterator.Next()) {
            if (iterator.Value().ShapeType() == TopAbs_EDGE &&
                result_edges.Contains(iterator.Value())) return true;
        }
    }
    return false;
}

struct ProvenanceUnifyResult {
    TopoDS_Shape shape;
    std::vector<OwnedFace> faces;
    std::vector<OwnedEdge> edges;
    std::vector<OwnedVertex> vertices;
    std::vector<TopoDS_Shape> hidden_display_edges;
    // True only when this exact shape passed BRepCheck_Analyzer here. A caller
    // that receives an unmodified fallback can then validate the published
    // input once, without also validating a discarded unification probe.
    bool validated{};
};

std::string encoded_topology_ancestry(
    const std::set<std::string>& parents);

ProvenanceUnifyResult unify_preserving_face_provenance(
    const TopoDS_Shape& shape,
    const std::vector<OwnedFace>& faces,
    const std::vector<OwnedEdge>& edges,
    const std::vector<OwnedVertex>& vertices,
    double tolerance,bool merge_coplanar=false) {
    const bool trace=std::getenv("ZIMA_CPP_CALC_PROFILE")!=nullptr;
    auto stamp=trace?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    const auto phase=[&](const char* name,std::size_t count=0) {
        if(!trace)return;
        const auto now=std::chrono::steady_clock::now();
        std::fprintf(stderr,"Kernel unify %s count=%zu: %.6f s\n",name,count,
            std::chrono::duration<double>(now-stamp).count());
        stamp=now;
    };
    ShapeUpgrade_UnifySameDomain probe(shape, true, true, false);
    probe.SetLinearTolerance(tolerance);
    // Sweep cap frames can differ by sub-nanoradian roundoff despite the
    // same supporting plane. Keep this tolerance local to ordinary Add;
    // cross-source nonplanar boundaries remain protected below.
    if(merge_coplanar)probe.SetAngularTolerance(1e-9);
    probe.Build();
    phase("probe-build");
    if (probe.Shape().IsNull()) {
        return {shape, faces, edges, vertices, {}};
    }
    const bool baseline_probe_validation=
        std::getenv("ZIMA_CPP_TEST_VALIDATE_UNIFY_PROBE")!=nullptr;
    if(baseline_probe_validation) {
        if(!BRepCheck_Analyzer(probe.Shape(),true,true,false).IsValid())
            return {shape, faces, edges, vertices, {}};
        phase("probe-validate");
    }

    TopTools_IndexedMapOfShape probe_edges;
    TopExp::MapShapes(probe.Shape(), TopAbs_EDGE, probe_edges);
    std::vector<TopoDS_Shape> protected_edges;
    for (const auto& edge : cross_reference_face_edges(shape, faces,merge_coplanar,tolerance)) {
        if (!history_edge_survives(probe.History(), edge, probe_edges)) {
            protected_edges.push_back(edge);
        }
    }
    phase("protection-scan",protected_edges.size());

    if (protected_edges.empty()) {
        // This is the result that will be published, so it must be validated.
        // When protected edges require a second build below, validating this
        // discarded probe would duplicate the same expensive whole-BRep pass.
        if(!baseline_probe_validation) {
            // OCCT's parallel analyzer performs the same complete geometric
            // checks. Large unified Bodies otherwise leave independent faces
            // on one core after the Boolean has already completed.
            if(!BRepCheck_Analyzer(probe.Shape(),true,true,false).IsValid())
                return {shape, faces, edges, vertices, {}};
            phase("probe-validate");
        }
        return {probe.Shape(),
            propagate_topology(probe.History(), faces),
            propagate_topology(probe.History(), edges),
            propagate_topology(probe.History(), vertices), {}, true};
    }

    ShapeUpgrade_UnifySameDomain protected_unifier(shape, true, true, false);
    protected_unifier.SetLinearTolerance(tolerance);
    if(merge_coplanar)protected_unifier.SetAngularTolerance(1e-9);
    for (const auto& edge : protected_edges) {
        protected_unifier.KeepShape(edge);
    }
    protected_unifier.Build();
    phase("protected-build",protected_edges.size());
    if (protected_unifier.Shape().IsNull() ||
        !BRepCheck_Analyzer(
            protected_unifier.Shape(),true,true,false).IsValid()) {
        return {shape, faces, edges, vertices, protected_edges};
    }
    phase("protected-validate",protected_edges.size());
    const auto history = protected_unifier.History();
    return {protected_unifier.Shape(),
        propagate_topology(history, faces),
        propagate_topology(history, edges),
        propagate_topology(history, vertices),
        propagate_display_edges(history, protected_edges), true};
}

template <typename Algorithm>
std::vector<OwnedFace> generated_edge_treatment_faces(
    Algorithm& algorithm,
    const std::vector<std::pair<TopoDS_Edge, EdgeReference>>& selected,
    const std::string& treatment_owner,
    std::string_view role) {
    TopTools_IndexedMapOfShape generated_shapes;
    std::vector<std::set<std::string>> parents(1);
    for (const auto& [edge, reference] : selected) {
        const std::string parent = std::to_string(reference.owner_id.size()) +
            ":" + reference.owner_id + std::to_string(reference.semantic_key.size()) +
            ":" + reference.semantic_key;
        const auto& generated = algorithm.Generated(edge);
        for (TopTools_ListIteratorOfListOfShape iterator(generated);
             iterator.More(); iterator.Next()) {
            if (iterator.Value().ShapeType() != TopAbs_FACE) continue;
            int index = generated_shapes.FindIndex(iterator.Value());
            if (index == 0) {
                index = generated_shapes.Add(iterator.Value());
                parents.resize(static_cast<std::size_t>(index) + 1);
            }
            parents[static_cast<std::size_t>(index)].insert(parent);
        }
    }
    std::vector<OwnedFace> result;
    result.reserve(static_cast<std::size_t>(generated_shapes.Extent()));
    for (int index = 1; index <= generated_shapes.Extent(); ++index) {
        std::string key(role);
        key += ":from:" + encoded_topology_ancestry(
            parents[static_cast<std::size_t>(index)]);
        result.push_back({generated_shapes.FindKey(index),
            FaceReference{treatment_owner, std::move(key), {}}});
    }
    return result;
}

void classify_edge_treatment_surfaces(const TopoDS_Shape& shape,
    const std::string& owner,std::vector<OwnedFace>& faces) {
    // Explicit calculation only: distinguish a surface blend from a solid
    // blend even when both coexist in the same Part compound. This flag is
    // persisted with the existing native reference; it does not define identity.
    TopTools_IndexedMapOfShape solid_faces;
    for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())
        TopExp::MapShapes(it.Current(),TopAbs_FACE,solid_faces);
    for(auto& face:faces)if(face.reference.owner_id==owner)
        face.reference.surface_result=!solid_faces.Contains(face.shape);
}

void append_unmapped_edge_treatment_faces(
    const TopoDS_Shape& result_shape,
    const std::vector<std::pair<TopoDS_Edge, EdgeReference>>& selected,
    const std::vector<OwnedFace>& input_faces,
    const std::string& treatment_owner,
    std::string_view role,
    double tolerance,
    std::vector<OwnedFace>& faces) {
    std::set<std::string> parents;
    for (const auto& [edge, reference] : selected) {
        static_cast<void>(edge);
        parents.insert(std::to_string(reference.owner_id.size()) + ":" +
            reference.owner_id + std::to_string(reference.semantic_key.size()) +
            ":" + reference.semantic_key);
    }
    std::string key(role);
    key += ":from:" + encoded_topology_ancestry(parents);
    const FaceReference fallback_reference{
        treatment_owner, std::move(key), {}};
    TopTools_IndexedMapOfShape mapped_faces;
    for (const auto& face : faces) mapped_faces.Add(face.shape);
    const double geometric_tolerance = std::max(1.0e-6, tolerance * 10.0);
    const auto inherited_input_reference = [&](const TopoDS_Face& fragment)
            -> std::optional<FaceReference> {
        double u_min{}, u_max{}, v_min{}, v_max{};
        BRepTools::UVBounds(fragment, u_min, u_max, v_min, v_max);
        if (!std::isfinite(u_min) || !std::isfinite(u_max) ||
            !std::isfinite(v_min) || !std::isfinite(v_max)) return std::nullopt;
        TopLoc_Location fragment_location;
        const auto& fragment_surface =
            BRep_Tool::Surface(fragment, fragment_location);
        if (fragment_surface.IsNull()) return std::nullopt;

        // UV midpoints are not necessarily inside a concave trimmed face.
        // Sample a small, deterministic grid and retain only points classified
        // on the actual fragment. This is part of the explicit body calculation,
        // never a viewer/picking path.
        constexpr std::array<double, 5> fractions{
            0.5, 0.25, 0.75, 0.125, 0.875};
        struct OrientedSample {
            gp_Pnt point;
            gp_Vec normal;
        };
        std::vector<OrientedSample> samples;
        samples.reserve(9);
        for (const double u_fraction : fractions) {
            for (const double v_fraction : fractions) {
                const double u = u_min + (u_max - u_min) * u_fraction;
                const double v = v_min + (v_max - v_min) * v_fraction;
                BRepClass_FaceClassifier classifier(
                    fragment, gp_Pnt2d(u, v), geometric_tolerance);
                if (classifier.State() != TopAbs_IN &&
                    classifier.State() != TopAbs_ON) continue;
                gp_Pnt point;
                gp_Vec du,dv;
                fragment_surface->D1(u,v,point,du,dv);
                point.Transform(fragment_location.Transformation());
                du.Transform(fragment_location.Transformation());
                dv.Transform(fragment_location.Transformation());
                auto normal=du.Crossed(dv);
                if(normal.SquareMagnitude()<=1.0e-18)continue;
                if(fragment.Orientation()==TopAbs_REVERSED)normal.Reverse();
                normal.Normalize();
                samples.push_back({point,normal});
                if (samples.size() == 9) break;
            }
            if (samples.size() == 9) break;
        }
        if (samples.empty()) return std::nullopt;

        int best_inside_count{};
        std::vector<FaceReference> best_references;
        for (const auto& input : input_faces) {
            if (!input.reference.valid() ||
                input.shape.ShapeType() != TopAbs_FACE) continue;
            const auto input_face = TopoDS::Face(input.shape);
            TopLoc_Location input_location;
            const auto& input_surface =
                BRep_Tool::Surface(input_face, input_location);
            if (input_surface.IsNull()) continue;
            const auto inverse_location =
                input_location.Transformation().Inverted();
            bool same_support = true;
            int inside_count{};
            for (const auto& sample : samples) {
                auto point=sample.point;
                point.Transform(inverse_location);
                GeomAPI_ProjectPointOnSurf projection(
                    point, input_surface, geometric_tolerance);
                if (!projection.IsDone() || projection.NbPoints() == 0 ||
                    projection.LowerDistance() > geometric_tolerance) {
                    same_support = false;
                    break;
                }
                double u{}, v{};
                projection.LowerDistanceParameters(u, v);
                gp_Pnt input_point;
                gp_Vec input_du,input_dv;
                input_surface->D1(u,v,input_point,input_du,input_dv);
                input_du.Transform(input_location.Transformation());
                input_dv.Transform(input_location.Transformation());
                auto input_normal=input_du.Crossed(input_dv);
                if(input_normal.SquareMagnitude()<=1.0e-18) {
                    same_support=false;
                    break;
                }
                if(input_face.Orientation()==TopAbs_REVERSED)
                    input_normal.Reverse();
                input_normal.Normalize();
                // A shared support plane or surface is not enough: the
                // topological side is permanent modeling intent.  Never let
                // an opposite-oriented fragment inherit the same stable face
                // identity, even at zero offset.
                if(sample.normal.Dot(input_normal)<1.-1.0e-10) {
                    same_support=false;
                    break;
                }
                BRepClass_FaceClassifier classifier(
                    input_face, gp_Pnt2d(u, v), geometric_tolerance);
                if (classifier.State() == TopAbs_IN ||
                    classifier.State() == TopAbs_ON) ++inside_count;
            }
            if (!same_support || inside_count == 0 ||
                inside_count < best_inside_count) continue;
            if (inside_count > best_inside_count) {
                best_inside_count = inside_count;
                best_references.clear();
            }
            if (std::find(best_references.begin(), best_references.end(),
                    input.reference) == best_references.end()) {
                best_references.push_back(input.reference);
            }
        }
        // Never invent an owner when two different persisted source faces are
        // geometrically indistinguishable. A unique support/trim match inherits
        // its already-defined ZIMA identity; only truly new or ambiguous faces
        // fall through to the treatment identity below.
        return best_references.size() == 1
            ? std::optional<FaceReference>{best_references.front()}
            : std::nullopt;
    };
    for (TopExp_Explorer explorer(result_shape, TopAbs_FACE);
         explorer.More(); explorer.Next()) {
        if (mapped_faces.Contains(explorer.Current())) continue;
        mapped_faces.Add(explorer.Current());
        if (const auto inherited = inherited_input_reference(
                TopoDS::Face(explorer.Current()))) {
            faces.push_back({explorer.Current(), *inherited});
            continue;
        }
        // The identity is defined exclusively by the treatment feature,
        // semantic role and persisted selected parents. OCCT traversal only
        // locates every runtime fragment belonging to that already-defined
        // identity; its enumeration position never enters the key.
        faces.push_back({explorer.Current(), fallback_reference});
    }
}

template <typename Reference>
std::string encoded_topology_reference(const Reference& reference) {
    return std::to_string(reference.owner_id.size()) + ":" +
        reference.owner_id + std::to_string(reference.semantic_key.size()) +
        ":" + reference.semantic_key +
        std::to_string(reference.instance_path.size()) + ":" +
        reference.instance_path;
}

std::optional<EdgeReference> decoded_edge_reference(std::string_view encoded) {
    std::size_t offset{};
    const auto field=[&]() -> std::optional<std::string> {
        const auto separator=encoded.find(':',offset);
        if(separator==std::string_view::npos)return std::nullopt;
        std::size_t length{};
        const auto text=encoded.substr(offset,separator-offset);
        const auto parsed=std::from_chars(
            text.data(),text.data()+text.size(),length);
        if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||
           separator+1+length>encoded.size())return std::nullopt;
        offset=separator+1+length;
        return std::string(encoded.substr(separator+1,length));
    };
    const auto owner=field();
    const auto key=field();
    const auto instance=field();
    if(!owner||!key||!instance)return std::nullopt;
    return EdgeReference{*owner,*key,*instance};
}

std::optional<EdgeReference> split_edge_parent(
        const EdgeReference& candidate) {
    constexpr std::string_view marker=":split-edge:from:";
    const auto parent_offset=candidate.semantic_key.find(marker);
    if(parent_offset==std::string::npos)return std::nullopt;
    return decoded_edge_reference(std::string_view(
        candidate.semantic_key).substr(parent_offset+marker.size()));
}

bool split_edge_descends_from(
        const EdgeReference& candidate,const EdgeReference& requested) {
    const auto parent=split_edge_parent(candidate);
    if(!parent)return false;
    return *parent==requested||split_edge_descends_from(*parent,requested);
}

std::string encoded_topology_reference_set(
    const std::set<std::string>& references) {
    std::string encoded;
    for (const auto& reference : references) {
        encoded += std::to_string(reference.size()) + ":" + reference;
    }
    return encoded;
}

std::string compact_topology_token(std::string_view value) {
    std::uint64_t first = 14695981039346656037ULL;
    std::uint64_t second = 7809847782465536322ULL;
    for (const unsigned char byte : value) {
        first ^= byte;
        first *= 1099511628211ULL;
        second ^= static_cast<std::uint64_t>(byte) + 0x9eU;
        second *= 14029467366897019727ULL;
    }
    std::ostringstream encoded;
    encoded << std::hex << first << ':' << second;
    return encoded.str();
}

std::string encoded_topology_ancestry(
    const std::set<std::string>& parents) {
    if (parents.empty()) return {};
    const auto primary = std::ranges::min_element(parents,
        [](const auto& first, const auto& second) {
            return std::tuple{first.size(), first} <
                std::tuple{second.size(), second};
        });
    const auto context =
        encoded_topology_reference_set(parents);
    // Keep one exact deterministic parent relation in the persisted key.
    // Additional OCCT history/adjacency context is only a discriminator and
    // is stored as a compact 128-bit token. Runtime uniqueness checks below
    // still reject a collision instead of silently reusing an identity.
    return "parent:" + std::to_string(primary->size()) + ":" +
        *primary + ":context-digest:" +
        compact_topology_token(context);
}

template <typename Owned>
std::vector<Owned> keep_unambiguous_topology_references(
    std::vector<Owned> values) {
    std::map<std::string, std::size_t> counts;
    for (const auto& value : values) {
        ++counts[encoded_topology_reference(value.reference)];
    }
    std::erase_if(values, [&](const auto& value) {
        return counts[encoded_topology_reference(value.reference)] != 1;
    });
    return values;
}

template <typename Algorithm, typename Owned>
void collect_shell_face_parent_tokens(
    Algorithm& algorithm,
    const TopoDS_Face& result_face,
    const std::vector<Owned>& inputs,
    std::string_view topology_role,
    std::set<std::string>& parents) {
    for (const auto& input : inputs) {
        if (!input.reference.valid()) continue;
        TopTools_IndexedMapOfShape result_subshapes;
        TopExp::MapShapes(result_face, input.shape.ShapeType(),
            result_subshapes);
        if (result_subshapes.Contains(input.shape)) {
            parents.insert(std::string(topology_role) + ":unchanged:" +
                encoded_topology_reference(input.reference));
        }
        const auto collect = [&](const TopTools_ListOfShape& descendants,
                                 std::string_view history_role) {
            for (TopTools_ListIteratorOfListOfShape iterator(descendants);
                 iterator.More(); iterator.Next()) {
                const auto& descendant = iterator.Value();
                if ((descendant.ShapeType() == TopAbs_FACE &&
                     descendant.IsSame(result_face)) ||
                    (descendant.ShapeType() == input.shape.ShapeType() &&
                     result_subshapes.Contains(descendant))) {
                    parents.insert(std::string(topology_role) + ":" +
                        std::string(history_role) + ":" +
                        encoded_topology_reference(input.reference));
                }
            }
        };
        collect(algorithm.Modified(input.shape), "modified");
        collect(algorithm.Generated(input.shape), "generated");
    }
}

template <typename Algorithm>
std::vector<OwnedFace> shell_result_faces(
    Algorithm& algorithm,
    const TopoDS_Shape& result_shape,
    const std::vector<OwnedFace>& input_faces,
    const std::vector<OwnedEdge>& input_edges,
    const std::vector<OwnedVertex>& input_vertices,
    const std::string& shell_owner,
    std::string_view semantic_role) {
    struct PendingFace {
        TopoDS_Face shape;
        std::vector<FaceReference> unchanged;
        std::set<std::string> parents;
    };
    std::vector<PendingFace> pending;
    TopTools_IndexedMapOfShape visited;
    for (TopExp_Explorer explorer(result_shape, TopAbs_FACE);
         explorer.More(); explorer.Next()) {
        const auto face = TopoDS::Face(explorer.Current());
        if (visited.Contains(face)) continue;
        visited.Add(face);

        PendingFace value;
        value.shape = face;
        for (const auto& input : input_faces) {
            if (input.reference.valid() && input.shape.IsSame(face) &&
                std::find(value.unchanged.begin(), value.unchanged.end(),
                    input.reference) == value.unchanged.end()) {
                value.unchanged.push_back(input.reference);
            }
        }
        if (value.unchanged.size() != 1) {
            collect_shell_face_parent_tokens(
                algorithm, face, input_faces, "face", value.parents);
            collect_shell_face_parent_tokens(
                algorithm, face, input_edges, "edge", value.parents);
            collect_shell_face_parent_tokens(
                algorithm, face, input_vertices, "vertex", value.parents);
            if (value.parents.empty()) {
                throw std::runtime_error(
                    "Shell result face has no stable ZIMA ancestry in " +
                    std::string(semantic_role));
            }
        }
        pending.push_back(std::move(value));
    }

    std::map<std::string, std::size_t> unchanged_reference_counts;
    for (const auto& value : pending) {
        if (value.unchanged.size() == 1) {
            ++unchanged_reference_counts[
                encoded_topology_reference(value.unchanged.front())];
        }
    }
    for (auto& value : pending) {
        if (value.unchanged.size() != 1) continue;
        const auto parent = encoded_topology_reference(value.unchanged.front());
        if (unchanged_reference_counts[parent] <= 1) continue;
        // A prior treatment may expose several geometric fragments carrying
        // one conceptual source-face reference. Shell owns distinct children
        // for those fragments; preserving the same ID on both would make the
        // topology ambiguous.
        value.parents.insert("face:fragment-of:" + parent);
        value.unchanged.clear();
    }

    const auto base_parent_token = [](const PendingFace& value) {
        if (value.unchanged.size() == 1) {
            return std::string("unchanged:") +
                encoded_topology_reference(value.unchanged.front());
        }
        return std::string("derived:") +
            encoded_topology_ancestry(value.parents);
    };
    std::map<std::string, std::size_t> base_parent_counts;
    for (const auto& value : pending) {
        if (value.unchanged.size() != 1) {
            ++base_parent_counts[base_parent_token(value)];
        }
    }
    for (auto& value : pending) {
        if (value.unchanged.size() == 1 ||
            base_parent_counts[base_parent_token(value)] == 1) {
            continue;
        }
        GProp_GProps properties;
        BRepGProp::SurfaceProperties(value.shape, properties);
        const BRepAdaptor_Surface support(value.shape, true);
        value.parents.insert("transition-surface-kind:" + std::to_string(
            static_cast<int>(support.GetType())));
        const auto probe = BRepBuilderAPI_MakeVertex(
            properties.CentreOfMass()).Vertex();
        const auto add_nearest_parents = [&](const auto& inputs,
                                             std::string_view role) {
            struct MeasuredParent {
                double distance{};
                std::string role;
                std::string reference;
            };
            double best = std::numeric_limits<double>::infinity();
            std::vector<MeasuredParent> distances;
            for (const auto& input : inputs) {
                if (!input.reference.valid()) continue;
                BRepExtrema_DistShapeShape distance(probe, input.shape);
                if (!distance.IsDone() || distance.NbSolution() == 0) continue;
                const double measured = distance.Value();
                best = std::min(best, measured);
                std::string resolved_role(role);
                if (input.shape.ShapeType() == TopAbs_EDGE) {
                    const auto edge = TopoDS::Edge(input.shape);
                    const auto first = TopExp::FirstVertex(edge, true);
                    const auto last = TopExp::LastVertex(edge, true);
                    const auto closest = distance.PointOnShape2(1);
                    const bool at_endpoint =
                        (!first.IsNull() &&
                         closest.Distance(BRep_Tool::Pnt(first)) <= 1.0e-7) ||
                        (!last.IsNull() &&
                         closest.Distance(BRep_Tool::Pnt(last)) <= 1.0e-7);
                    resolved_role += at_endpoint ? "-endpoint" : "-interior";
                }
                distances.push_back({measured, std::move(resolved_role),
                    encoded_topology_reference(input.reference)});
            }
            if (!std::isfinite(best)) return;
            std::map<std::pair<std::string, std::string>, double> unique;
            for (const auto& measured : distances) {
                const auto key = std::pair{measured.role, measured.reference};
                const auto found = unique.find(key);
                if (found == unique.end() || measured.distance < found->second) {
                    unique.insert_or_assign(key, measured.distance);
                }
            }
            distances.clear();
            for (const auto& [key, distance] : unique) {
                distances.push_back({distance, key.first, key.second});
            }
            std::ranges::sort(distances, {}, &MeasuredParent::distance);
            const double tolerance = std::max(1.0e-7,
                std::max(1.0, best) * 1.0e-8);
            int rank{};
            double rank_distance = best;
            for (const auto& measured : distances) {
                if (measured.distance > rank_distance + tolerance) {
                    ++rank;
                    rank_distance = measured.distance;
                }
                if (rank >= 4) break;
                value.parents.insert(measured.role + ":rank:" +
                    std::to_string(rank) + ":" + measured.reference);
            }
        };
        // OCCT can split one offset parent into several transition patches
        // without reporting distinct history. The nearest persisted source
        // edge/vertex supplies the missing parent relation; coordinates and
        // traversal position never enter the identity.
        std::vector<OwnedFace> prior_feature_faces;
        std::ranges::copy_if(input_faces,
            std::back_inserter(prior_feature_faces), [&](const auto& face) {
                return face.reference.owner_id != shell_owner;
            });
        add_nearest_parents(prior_feature_faces,
            "nearest-prior-feature-face");
        add_nearest_parents(input_faces, "nearest-face");
        add_nearest_parents(input_edges, "nearest-edge");
        add_nearest_parents(input_vertices, "nearest-vertex");
    }
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(
        result_shape, TopAbs_EDGE, TopAbs_FACE, edge_faces);
    // Adjacency describes the same original parent graph for every face.
    // Reading a neighbor's mutable parents below would include discriminator
    // tokens added earlier in this loop, making identities depend on OCCT's
    // face traversal order even when the geometry and ancestry are unchanged.
    std::vector<std::string> adjacency_base_tokens;
    adjacency_base_tokens.reserve(pending.size());
    for(const auto& value:pending)adjacency_base_tokens.push_back(base_parent_token(value));
    for (auto& value : pending) {
        if (value.unchanged.size() == 1) continue;
        std::set<std::string> adjacent_parents;
        std::vector<std::string> boundary_contexts;
        for (TopExp_Explorer edges(value.shape, TopAbs_EDGE);
             edges.More(); edges.Next()) {
            const int edge_index = edge_faces.FindIndex(edges.Current());
            if (edge_index == 0) continue;
            std::set<std::string> edge_neighbors;
            for (TopTools_ListIteratorOfListOfShape neighbor(
                    edge_faces.FindFromIndex(edge_index));
                 neighbor.More(); neighbor.Next()) {
                if (neighbor.Value().IsSame(value.shape)) continue;
                const auto found = std::find_if(pending.begin(), pending.end(),
                    [&](const auto& candidate) {
                        return candidate.shape.IsSame(neighbor.Value());
                    });
                if (found != pending.end()) {
                    auto token = adjacency_base_tokens.at(static_cast<std::size_t>(found-pending.begin()));
                    adjacent_parents.insert(token);
                    edge_neighbors.insert(std::move(token));
                }
            }
            boundary_contexts.push_back(
                encoded_topology_reference_set(edge_neighbors));
        }
        if (!adjacent_parents.empty()) {
            value.parents.insert("adjacent-digest:" + compact_topology_token(
                encoded_topology_reference_set(adjacent_parents)));
        }
        std::ranges::sort(boundary_contexts);
        std::string encoded_boundaries;
        for (const auto& context : boundary_contexts) {
            encoded_boundaries += std::to_string(context.size()) + ":" +
                context;
        }
        value.parents.insert("boundary-context-digest:" +
            compact_topology_token(encoded_boundaries));
    }

    std::vector<OwnedFace> result;
    std::map<std::string, TopoDS_Shape> identities;
    for (auto& value : pending) {
        FaceReference reference;
        if (value.unchanged.size() == 1) {
            reference = value.unchanged.front();
        } else {
            reference = {shell_owner,
                std::string(semantic_role) + ":from:" +
                    encoded_topology_ancestry(value.parents), {}};
        }
        const auto identity = encoded_topology_reference(reference);
        const auto [found, inserted] = identities.emplace(identity, value.shape);
        if (!inserted && !found->second.IsSame(value.shape)) {
            throw std::runtime_error(
                "Shell produced ambiguous face ancestry in " +
                std::string(semantic_role));
        }
        result.push_back({value.shape, std::move(reference)});
    }
    return result;
}

// Smooth spherical/toroidal seams and collapsed sphere poles are parameterization,
// not boundaries between ZIMA faces. A seam endpoint shared with a real rim
// remains real topology. This classification only runs during Shell calculation.
TopTools_IndexedMapOfShape shell_parameterization_artifacts(const TopoDS_Shape& shape) {
    TopTools_IndexedMapOfShape artifacts;
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces,vertex_edges;
    TopExp::MapShapesAndAncestors(shape,TopAbs_EDGE,TopAbs_FACE,edge_faces);
    TopExp::MapShapesAndAncestors(shape,TopAbs_VERTEX,TopAbs_EDGE,vertex_edges);
    for(int i=1;i<=edge_faces.Extent();++i) {
        const auto edge=TopoDS::Edge(edge_faces.FindKey(i));
        TopTools_IndexedMapOfShape adjacent;
        for(TopTools_ListIteratorOfListOfShape it(edge_faces.FindFromIndex(i));it.More();it.Next())adjacent.Add(it.Value());
        if(adjacent.Extent()!=1)continue;
        const auto face=TopoDS::Face(adjacent.FindKey(1));
        // Smooth toroidal seams have the same non-boundary role. Do not
        // classify cone apices or degenerate toroidal edges as artifacts.
        const auto type=BRepAdaptor_Surface(face).GetType();
        if((type==GeomAbs_Sphere&&BRep_Tool::Degenerated(edge))||
            ((type==GeomAbs_Sphere||type==GeomAbs_Torus)&&BRep_Tool::IsClosed(edge,face)))artifacts.Add(edge);
    }
    for(int i=1;i<=vertex_edges.Extent();++i) {
        bool only_artifacts=!vertex_edges.FindFromIndex(i).IsEmpty();
        for(TopTools_ListIteratorOfListOfShape it(vertex_edges.FindFromIndex(i));it.More();it.Next())
            if(!artifacts.Contains(it.Value())){only_artifacts=false;break;}
        if(only_artifacts)artifacts.Add(vertex_edges.FindKey(i));
    }
    return artifacts;
}

template <typename Owned>
void require_complete_unique_shell_topology(
    const TopoDS_Shape& result_shape, TopAbs_ShapeEnum kind,
    const std::vector<Owned>& topology, std::string_view label,
    const std::vector<Owned>* unchanged_input = nullptr) {
    TopTools_IndexedMapOfShape expected;
    TopExp::MapShapes(result_shape, kind, expected);
    TopTools_IndexedMapOfShape mapped;
    std::map<std::string, TopoDS_Shape> identities;
    for (const auto& owned : topology) {
        if (!owned.reference.valid() || owned.shape.ShapeType() != kind) continue;
        mapped.Add(owned.shape);
        const auto identity = encoded_topology_reference(owned.reference);
        const auto [found, inserted] = identities.emplace(identity, owned.shape);
        if (!inserted && !found->second.IsSame(owned.shape)) {
            // Disconnected, untouched source fragments may already retain one
            // original reference. Preserve those exact inherited shapes; this
            // exception must never accept a newly split or generated shape.
            const auto inherited=[&](const TopoDS_Shape& shape) {
                return unchanged_input&&std::ranges::any_of(*unchanged_input,[&](const auto& input) {
                    return input.shape.IsSame(shape)&&encoded_topology_reference(input.reference)==identity;
                });
            };
            if(inherited(found->second)&&inherited(owned.shape))continue;
            throw std::runtime_error(
                "Shell reused one stable identity for multiple " +
                std::string(label));
        }
    }
    const auto artifacts=shell_parameterization_artifacts(result_shape);
    for(int i=1;i<=expected.Extent();++i) {
        const auto& shape=expected.FindKey(i);
        if(!mapped.Contains(shape)&&!artifacts.Contains(shape)) {
            throw std::runtime_error(
                "Shell did not define stable identity for every " +
                std::string(label));
        }
    }
}

std::vector<OwnedFace> shell_opening_tool_faces(
    const TopoDS_Shape& tool_shape,
    const OwnedFace& opening_source,
    const FaceReference& conceptual_source,
    const std::vector<OwnedEdge>& opening_source_edges,
    const std::string& shell_owner,
    std::string_view tool_role) {
    const auto source_face_token =
        encoded_topology_reference(conceptual_source);
    std::vector<OwnedFace> result;
    std::map<std::string, TopoDS_Shape> identities;
    TopTools_IndexedMapOfShape visited;
    for (TopExp_Explorer explorer(tool_shape, TopAbs_FACE);
         explorer.More(); explorer.Next()) {
        const auto face = TopoDS::Face(explorer.Current());
        if (visited.Contains(face)) continue;
        visited.Add(face);

        std::string role;
        std::set<std::string> edge_parents;
        if (face.IsSame(opening_source.shape)) {
            role = std::string(tool_role) + ":source-cap:from:" +
                source_face_token;
        } else {
            TopTools_IndexedMapOfShape face_edges;
            TopExp::MapShapes(face, TopAbs_EDGE, face_edges);
            for (const auto& source_edge : opening_source_edges) {
                if (source_edge.reference.valid() &&
                    face_edges.Contains(source_edge.shape)) {
                    edge_parents.insert(
                        encoded_topology_reference(source_edge.reference));
                }
            }
            if (!edge_parents.empty()) {
                role = std::string(tool_role) + ":side:from:" +
                    source_face_token + ":boundary-digest:" +
                    compact_topology_token(
                        encoded_topology_reference_set(edge_parents));
            } else {
                role = std::string(tool_role) + ":offset-cap:from:" +
                    source_face_token;
            }
        }
        const FaceReference reference{shell_owner, std::move(role), {}};
        const auto identity = encoded_topology_reference(reference);
        const auto [found, inserted] = identities.emplace(identity, face);
        if (!inserted && !found->second.IsSame(face)) {
            throw std::runtime_error(
                "Shell opening tool produced ambiguous stable face ancestry");
        }
        result.push_back({face, reference});
    }
    return result;
}

template <typename Reference, typename Owned>
std::set<std::string> referenced_ancestor_tokens(
    const TopTools_IndexedDataMapOfShapeListOfShape& ancestors,
    const TopoDS_Shape& child,
    const TopologyReferenceIndex<Reference, Owned>& references) {
    std::set<std::string> result;
    const int child_index = ancestors.FindIndex(child);
    if (child_index == 0) return result;
    for (TopTools_ListIteratorOfListOfShape iterator(
            ancestors.FindFromIndex(child_index));
         iterator.More(); iterator.Next()) {
        const auto reference = references.reference_for(iterator.Value());
        if (reference.valid()) {
            result.insert(encoded_topology_reference(reference));
        }
    }
    return result;
}

std::vector<OwnedFace> complete_boolean_faces(
    const TopoDS_Shape& shape, const std::vector<OwnedFace>& propagated,
    const std::vector<OwnedEdge>& edges, const std::vector<OwnedVertex>& vertices,
    const std::string& owner, std::string_view role,
    const std::vector<HistoryOperation>& history) {
    TopTools_IndexedMapOfShape actual;
    TopExp::MapShapes(shape, TopAbs_FACE, actual);
    std::map<std::string, TopTools_IndexedMapOfShape> descendants;
    for (const auto& face : propagated)
        if (face.reference.valid() && actual.Contains(face.shape))
            descendants[encoded_topology_reference(face.reference)].Add(face.shape);
    std::vector<OwnedFace> result;
    const auto age=[&](const FaceReference& reference) {
        const auto source=std::ranges::find(history,reference.owner_id,&HistoryOperation::owner_id);
        return std::distance(history.begin(),source);
    };
    for (int i=1; i<=actual.Extent(); ++i) {
        const auto& face=actual.FindKey(i);
        std::set<std::string> parents;
        FaceReference reference;
        for (const auto& source : propagated) if (source.shape.IsSame(face) && source.reference.valid()) {
            parents.insert(encoded_topology_reference(source.reference));
            if(!reference.valid()||age(source.reference)<age(reference))reference=source.reference;
        }
        if (parents.empty()) continue;
        // A coplanar union retains its oldest source reference. Operation
        // ownership must not displace the original placement support.
        if(parents.size()>1&&BRepAdaptor_Surface(TopoDS::Face(face)).GetType()==GeomAbs_Plane)
            parents={encoded_topology_reference(reference)};
        const bool split=parents.size()==1 && descendants.at(*parents.begin()).Extent()>1;
        if (parents.size()>1 || split) {
            TopTools_IndexedMapOfShape boundary;
            TopExp::MapShapes(face, boundary);
            std::set<std::string> context;
            for (const auto& edge : edges) if (edge.reference.valid() && boundary.Contains(edge.shape))
                context.insert("edge:"+encoded_topology_reference(edge.reference));
            for (const auto& vertex : vertices) if (vertex.reference.valid() && boundary.Contains(vertex.shape))
                context.insert("vertex:"+encoded_topology_reference(vertex.reference));
            // A merged/split display face is a child of its persisted input
            // faces. Boundary ancestry distinguishes disconnected descendants;
            // neither shape enumeration nor measured coordinates define IDs.
            reference.owner_id=owner;
            reference.semantic_key="boolean:"+std::string(role)+(split?":split-face:from:":":merged-face:from:")+
                encoded_topology_reference_set(parents)+":boundary:"+
                compact_topology_token(encoded_topology_reference_set(context));
        }
        result.push_back({face,std::move(reference)});
    }
    // Some symmetric fragments have the same immediate boundary parents.
    // Their neighboring, already named face fragments carry the missing
    // ancestry (for example the two front walls around a circular cut).
    // Refine only colliding IDs, keeping ordinary inherited faces unchanged.
    std::set<std::string> identities;
    for(const auto& face:result)identities.insert(encoded_topology_reference(face.reference));
    if(identities.size()==result.size())return result;
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(shape,TopAbs_EDGE,TopAbs_FACE,edge_faces);
    std::vector<std::string> base_keys;
    for(const auto& face:result)base_keys.push_back(face.reference.semantic_key);
    for(std::size_t pass=0;pass<result.size();++pass) {
        std::map<std::string,std::vector<std::size_t>> groups;
        for(std::size_t i=0;i<result.size();++i)
            groups[encoded_topology_reference(result[i].reference)].push_back(i);
        const TopologyReferenceIndex<FaceReference,OwnedFace> references(result);
        bool changed=false;
        for(const auto& [identity,members]:groups) {
            if(members.size()<2)continue;
            std::map<std::string,std::vector<std::size_t>> contexts;
            for(const auto i:members) {
                std::set<std::string> adjacent;
                for(TopExp_Explorer edge(result[i].shape,TopAbs_EDGE);edge.More();edge.Next()) {
                    const int index=edge_faces.FindIndex(edge.Current());
                    if(!index)continue;
                    for(TopTools_ListIteratorOfListOfShape face(edge_faces.FindFromIndex(index));face.More();face.Next()) {
                        if(face.Value().IsSame(result[i].shape))continue;
                        const auto reference=references.reference_for(face.Value());
                        if(reference.valid())adjacent.insert(encoded_topology_reference(reference));
                    }
                }
                contexts[encoded_topology_reference_set(adjacent)].push_back(i);
            }
            // Identical ancestry remains ambiguous; never break ties with
            // runtime enumeration, coordinates, dimensions or random IDs.
            if(contexts.size()<2)continue;
            for(const auto& [context,indices]:contexts)for(const auto i:indices)
                result[i].reference.semantic_key=base_keys[i]+":adjacent-face-context:"+compact_topology_token(context);
            changed=true;
        }
        if(!changed)break;
    }
    return result;
}

std::vector<OwnedEdge> complete_boolean_edges(
    const TopoDS_Shape& result_shape,
    const std::vector<OwnedFace>& faces,
    const std::vector<OwnedEdge>& propagated_edges,
    const std::string& operation_owner,
    std::string_view operation_role,
    const std::vector<OwnedFace>& generated_supports,
    const std::vector<OwnedVertex>& source_vertices) {
    const TopologyReferenceIndex<FaceReference, OwnedFace> face_references(faces);
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(
        result_shape, TopAbs_EDGE, TopAbs_FACE, edge_faces);
    TopTools_IndexedDataMapOfShapeListOfShape vertex_faces;
    TopExp::MapShapesAndAncestors(
        result_shape, TopAbs_VERTEX, TopAbs_FACE, vertex_faces);

    // History locates descendants, but a one-to-many relation is not identity.
    // Count distinct surviving shapes, not repeated uses in adjacent faces or
    // descendants which a later stage removed from this result.
    TopTools_IndexedMapOfShape actual_edges;
    TopExp::MapShapes(result_shape, TopAbs_EDGE, actual_edges);
    std::map<std::string, TopTools_IndexedMapOfShape> descendants;
    for (const auto& candidate : propagated_edges)
        if (candidate.reference.valid() && actual_edges.Contains(candidate.shape))
            descendants[encoded_topology_reference(candidate.reference)].Add(candidate.shape);

    std::vector<OwnedEdge> result;
    TopTools_IndexedMapOfShape visited;
    for (TopExp_Explorer explorer(result_shape, TopAbs_EDGE);
         explorer.More(); explorer.Next()) {
        const auto edge = explorer.Current();
        if (visited.Contains(edge)) continue;
        visited.Add(edge);

        std::vector<EdgeReference> inherited_references;
        for (const auto& candidate : propagated_edges) {
            if (!candidate.reference.valid() || !candidate.shape.IsSame(edge)) continue;
            if (std::find(inherited_references.begin(),
                    inherited_references.end(), candidate.reference) ==
                inherited_references.end()) {
                inherited_references.push_back(candidate.reference);
            }
        }
        const bool split = inherited_references.size() == 1 &&
            descendants.at(encoded_topology_reference(inherited_references.front())).Extent() > 1;
        if (inherited_references.size() == 1 && !split) {
            result.push_back({edge, inherited_references.front()});
            continue;
        }
        // When a Boolean reports several different parents for one runtime
        // edge, the edge is a new intersection. Define one child identity
        // from the two persisted adjacent faces below instead of exposing an
        // ambiguous reference.

        auto adjacent_faces = referenced_ancestor_tokens(
            edge_faces, edge, face_references);
        for(const auto& support:generated_supports)
            if(support.shape.IsSame(edge))adjacent_faces.insert(encoded_topology_reference(support.reference));
        if (adjacent_faces.empty()) continue;
        std::set<std::string> endpoint_supports;
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(edge, TopAbs_VERTEX, vertices);
        for (int index = 1; index <= vertices.Extent(); ++index) {
            auto support = referenced_ancestor_tokens(
                vertex_faces, vertices.FindKey(index), face_references);
            if(!generated_supports.empty()) {
                for(const auto& source:source_vertices)
                    if(source.shape.IsSame(vertices.FindKey(index)))
                        support.insert(encoded_topology_reference(source.reference));
                for(const auto& source:generated_supports) {
                    TopTools_IndexedMapOfShape endpoints;
                    TopExp::MapShapes(source.shape,TopAbs_VERTEX,endpoints);
                    if(endpoints.Contains(vertices.FindKey(index)))
                        support.insert(encoded_topology_reference(source.reference));
                }
            }
            if (!support.empty()) {
                endpoint_supports.insert(
                    encoded_topology_reference_set(support));
            }
        }
        // Identity is defined exclusively by the Boolean feature, semantic
        // role and persisted face ancestry. OCCT traversal only locates the
        // runtime edge; its enumeration order and coordinates never enter
        // the persisted key.
        std::string semantic_key = "boolean:";
        semantic_key += operation_role;
        if (split) {
            // The parent remains recoverable from persisted ZIMA data. Boundary
            // face ancestry distinguishes fragments even when their lengths are
            // equal and continues to identify them when dimensions change.
            semantic_key += ":split-edge:from:" +
                encoded_topology_reference(inherited_references.front());
        } else {
            semantic_key += ":intersection";
        }
        semantic_key += ":between:" +
            encoded_topology_reference_set(adjacent_faces);
        semantic_key += ":ends:" +
            encoded_topology_reference_set(endpoint_supports);
        result.push_back({edge,
            EdgeReference{operation_owner, std::move(semantic_key), {}}});
    }
    return result;
}

std::vector<OwnedVertex> complete_boolean_vertices(
    const TopoDS_Shape& shape, const std::vector<OwnedFace>& faces,
    const std::vector<OwnedEdge>& edges,
    const std::vector<OwnedVertex>& propagated,
    const std::string& owner, std::string_view role) {
    const TopologyReferenceIndex<FaceReference, OwnedFace> face_references(faces);
    const TopologyReferenceIndex<EdgeReference, OwnedEdge> edge_references(edges);
    TopTools_IndexedDataMapOfShapeListOfShape vertex_faces, vertex_edges;
    TopExp::MapShapesAndAncestors(shape, TopAbs_VERTEX, TopAbs_FACE, vertex_faces);
    TopExp::MapShapesAndAncestors(shape, TopAbs_VERTEX, TopAbs_EDGE, vertex_edges);
    TopTools_IndexedMapOfShape vertices;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
    std::map<std::string, TopTools_IndexedMapOfShape> descendants;
    for (const auto& value : propagated)
        if (value.reference.valid() && vertices.Contains(value.shape))
            descendants[encoded_topology_reference(value.reference)].Add(value.shape);
    std::vector<OwnedVertex> result;
    for (int index = 1; index <= vertices.Extent(); ++index) {
        const auto& vertex = vertices.FindKey(index);
        VertexReference inherited;
        // propagate_topology appends the existing Body before the current
        // operand, so the first surviving unsplit parent is the oldest one in
        // document history.  A Boolean join may map both an old Body point and
        // a new operand endpoint onto the same runtime vertex; that is not a
        // reason to discard the old persisted identity.  A genuinely split
        // parent still has several descendants and receives a Boolean identity
        // below for each resulting fragment.
        for (const auto& candidate : propagated) {
            if (!candidate.reference.valid() ||
                !candidate.shape.IsSame(vertex)) continue;
            const auto descendant = descendants.find(
                encoded_topology_reference(candidate.reference));
            if (descendant != descendants.end() &&
                descendant->second.Extent() == 1) {
                inherited = candidate.reference;
                break;
            }
        }
        if (inherited.valid()) {
            result.push_back({vertex, inherited});
            continue;
        }
        const auto support = referenced_ancestor_tokens(vertex_faces, vertex, face_references);
        const auto incident = referenced_ancestor_tokens(vertex_edges, vertex, edge_references);
        if (support.empty() || incident.empty()) continue;
        std::string key = "boolean:" + std::string(role) + ":vertex";
        if (inherited.valid()) key += ":from:" + encoded_topology_reference(inherited);
        key += ":at:" + encoded_topology_reference_set(support);
        key += ":edges:" + encoded_topology_reference_set(incident);
        result.push_back({vertex, VertexReference{owner, std::move(key), {}}});
    }
    return result;
}

// Endpoint inspection needs one operational name at a joined Feature side.
// Keep the propagated aliases in the native topology: downstream source
// operations own those parents independently of this inspection name.
std::vector<OwnedVertex> feature_endpoint_vertices(const TopoDS_Shape& shape,
        const std::vector<OwnedFace>& faces,const std::vector<OwnedEdge>& edges,
        const std::vector<OwnedVertex>& vertices) {
    const TopologyReferenceIndex<VertexReference,OwnedVertex> references(vertices);
    TopTools_IndexedMapOfShape junctions;
    std::vector<std::string> owners;
    for(const auto& vertex:vertices) {
        if(references.reference_for(vertex.shape).valid()||junctions.Contains(vertex.shape))continue;
        std::string owner;bool feature_parents=true;
        for(const auto& parent:vertices)if(parent.shape.IsSame(vertex.shape)) {
            const auto& ref=parent.reference;
            if(!ref.valid()||(!ref.semantic_key.starts_with("start:feature-side:")&&
                !ref.semantic_key.starts_with("end:feature-side:"))||
                (!owner.empty()&&owner!=ref.owner_id)) {feature_parents=false;break;}
            owner=ref.owner_id;
        }
        if(feature_parents&&!owner.empty()) {junctions.Add(vertex.shape);owners.push_back(std::move(owner));}
    }
    if(junctions.IsEmpty())return vertices;
    // This packet is an inspection alias for an intentionally joined point,
    // not source-topology propagation.  Exclude only the competing authored
    // aliases at those junctions; the other propagated endpoints still own
    // the stable identities used by edge-treatment routes.  The operational
    // junction name itself is derived exclusively from current face/edge
    // ancestry and therefore remains independent of child order.
    auto propagated=vertices;
    std::erase_if(propagated,[&](const auto& item) {
        return junctions.Contains(item.shape);
    });
    const auto completed=complete_boolean_vertices(
        shape,faces,edges,propagated,{},"add");
    auto result=vertices;
    for(const auto& point:completed) {
        const int index=junctions.FindIndex(point.shape);
        if(!index)continue;
        std::erase_if(result,[&](const auto& item){return item.shape.IsSame(point.shape);});
        auto endpoint=point;endpoint.reference.owner_id=owners.at(static_cast<std::size_t>(index-1));
        result.push_back(std::move(endpoint));
    }
    return result;
}

std::set<std::string> selected_edge_parent_tokens(
    const std::vector<std::pair<TopoDS_Edge, EdgeReference>>& selected) {
    std::set<std::string> result;
    for (const auto& [edge, reference] : selected) {
        static_cast<void>(edge);
        result.insert(encoded_topology_reference(reference));
    }
    return result;
}

template <typename Algorithm>
std::vector<std::pair<TopoDS_Edge, EdgeReference>>
resolve_edge_treatment_contours(
    Algorithm& algorithm,
    const std::vector<std::pair<TopoDS_Edge, EdgeReference>>& requested,
    const std::vector<OwnedEdge>& available,
    std::string_view operation_name,
    std::string_view feature_id) {
    TopTools_IndexedMapOfShape requested_shapes;
    for (const auto& [edge, reference] : requested) {
        static_cast<void>(reference);
        requested_shapes.Add(edge);
        if (algorithm.Contour(edge) <= 0) {
            throw std::runtime_error(std::string("OCCT did not create a ") +
                std::string(operation_name) + " contour");
        }
    }

    // Fillet and Chamfer accept a seed edge and may expand it to an OCCT
    // tangent contour. That expansion is a geometric decision only: every
    // member must resolve back to an identity already persisted by ZIMA at
    // the input boundary. OCCT contour order must never create identity.
    std::vector<std::pair<TopoDS_Edge, EdgeReference>> resolved;
    TopTools_IndexedMapOfShape actual_shapes;
    for (int contour = 1; contour <= algorithm.NbContours(); ++contour) {
        for (int edge_index = 1;
             edge_index <= algorithm.NbEdges(contour); ++edge_index) {
            const auto& edge = algorithm.Edge(contour, edge_index);
            if (edge.IsNull()) {
                throw std::runtime_error(std::string("OCCT returned an empty ") +
                    std::string(operation_name) + " contour edge");
            }
            if (actual_shapes.Contains(edge)) continue;
            std::vector<EdgeReference> aliases;
            for (const auto& candidate : available) {
                if (!candidate.shape.IsSame(edge) ||
                    std::ranges::find(aliases, candidate.reference) !=
                        aliases.end()) {
                    continue;
                }
                aliases.push_back(candidate.reference);
            }
            std::vector<EdgeReference> requested_aliases;
            for (const auto& selected : requested) {
                if (!selected.first.IsSame(edge) ||
                    std::ranges::find(requested_aliases,
                        selected.second) != requested_aliases.end()) {
                    continue;
                }
                requested_aliases.push_back(selected.second);
            }
            if (!requested_aliases.empty()) {
                if (!std::ranges::all_of(requested_aliases,
                        [&](const auto& reference) {
                            return std::ranges::find(aliases, reference) !=
                                aliases.end();
                        })) {
                    throw std::runtime_error(std::string("The ") +
                        std::string(operation_name) + " feature " +
                        std::string(feature_id) +
                        " selected an alias that is absent from its OCCT contour edge");
                }
                actual_shapes.Add(edge);
                for (const auto& reference : requested_aliases) {
                    resolved.emplace_back(edge, reference);
                }
                continue;
            }
            if (aliases.size() != 1) {
                throw std::runtime_error(std::string("The ") +
                    std::string(operation_name) + " feature " +
                    std::string(feature_id) +
                    " contains a contour edge without an unambiguous persisted ZIMA identity");
            }
            actual_shapes.Add(edge);
            resolved.emplace_back(edge, aliases.front());
        }
    }

    for (int index = 1; index <= requested_shapes.Extent(); ++index) {
        if (!actual_shapes.Contains(requested_shapes.FindKey(index))) {
            throw std::runtime_error(std::string("OCCT omitted a requested ") +
                std::string(operation_name) + " edge from feature " +
                std::string(feature_id));
        }
    }
    return resolved;
}

std::vector<OwnedEdge> complete_edge_treatment_edges(
    const TopoDS_Shape& result_shape,
    const std::vector<OwnedFace>& faces,
    const std::vector<OwnedEdge>& propagated_edges,
    const std::vector<OwnedEdge>& input_edges,
    const std::vector<std::pair<TopoDS_Edge, EdgeReference>>& selected,
    const std::string& treatment_owner,
    std::string_view role) {
    const TopologyReferenceIndex<FaceReference, OwnedFace> face_references(faces);
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(
        result_shape, TopAbs_EDGE, TopAbs_FACE, edge_faces);
    TopTools_IndexedDataMapOfShapeListOfShape vertex_faces;
    TopExp::MapShapesAndAncestors(
        result_shape, TopAbs_VERTEX, TopAbs_FACE, vertex_faces);
    const auto selected_parents = selected_edge_parent_tokens(selected);

    using EdgeEndpointKey = std::array<double, 6>;
    const auto endpoint_key = [](const TopoDS_Edge& edge)
            -> std::optional<EdgeEndpointKey> {
        const auto first = TopExp::FirstVertex(edge, true);
        const auto last = TopExp::LastVertex(edge, true);
        if (first.IsNull() || last.IsNull()) return std::nullopt;
        const auto first_point = BRep_Tool::Pnt(first);
        const auto last_point = BRep_Tool::Pnt(last);
        std::array<double, 3> first_coordinates{
            first_point.X(), first_point.Y(), first_point.Z()};
        std::array<double, 3> last_coordinates{
            last_point.X(), last_point.Y(), last_point.Z()};
        for (auto* coordinates : {&first_coordinates, &last_coordinates}) {
            for (auto& value : *coordinates) {
                if (value == 0.0) value = 0.0;
            }
        }
        if (last_coordinates < first_coordinates) {
            std::swap(first_coordinates, last_coordinates);
        }
        return EdgeEndpointKey{first_coordinates[0], first_coordinates[1],
            first_coordinates[2], last_coordinates[0], last_coordinates[1],
            last_coordinates[2]};
    };
    std::map<EdgeEndpointKey, std::vector<const OwnedEdge*>> input_by_endpoints;
    for (const auto& input : input_edges) {
        if (!input.reference.valid() || input.shape.ShapeType() != TopAbs_EDGE) {
            continue;
        }
        if (const auto key = endpoint_key(TopoDS::Edge(input.shape))) {
            input_by_endpoints[*key].push_back(&input);
        }
    }
    const auto inherited_input_reference = [&](const TopoDS_Edge& edge)
            -> std::optional<EdgeReference> {
        const auto key = endpoint_key(edge);
        if (!key) return std::nullopt;
        const auto candidates = input_by_endpoints.find(*key);
        if (candidates == input_by_endpoints.end()) return std::nullopt;
        std::vector<EdgeReference> matching_references;
        for (const auto* input : candidates->second) {
            if (!BRepTools::Compare(TopoDS::Edge(input->shape), edge)) continue;
            if (std::find(matching_references.begin(), matching_references.end(),
                    input->reference) == matching_references.end()) {
                matching_references.push_back(input->reference);
            }
        }
        // OCCT may rebuild an untouched edge without reporting it through
        // Modified/Generated. The exact geometric comparison only locates
        // the already-defined input identity. Coincident edges with different
        // persisted parents remain deliberately ambiguous.
        return matching_references.size() == 1
            ? std::optional<EdgeReference>{matching_references.front()}
            : std::nullopt;
    };

    std::vector<OwnedEdge> result;
    TopTools_IndexedMapOfShape visited;
    for (TopExp_Explorer explorer(result_shape, TopAbs_EDGE);
         explorer.More(); explorer.Next()) {
        const auto edge = explorer.Current();
        if (visited.Contains(edge)) continue;
        visited.Add(edge);
        std::vector<EdgeReference> inherited_references;
        for (const auto& propagated : propagated_edges) {
            if (!propagated.shape.IsSame(edge) ||
                !propagated.reference.valid() ||
                std::ranges::find(inherited_references,
                    propagated.reference) != inherited_references.end()) {
                continue;
            }
            inherited_references.push_back(propagated.reference);
        }
        if (!inherited_references.empty()) {
            // A direct Add block can retain several authored parents on one
            // physical edge. OCCT history proves that every alias below has
            // this exact descendant, so keep them all instead of replacing
            // an intentionally multi-parent edge with a Fillet-owned name.
            for (const auto& inherited : inherited_references) {
                result.push_back({edge, inherited});
            }
            continue;
        }
        if (const auto inherited = inherited_input_reference(
                TopoDS::Edge(edge))) {
            result.push_back({edge, *inherited});
            continue;
        }

        const auto adjacent_faces = referenced_ancestor_tokens(
            edge_faces, edge, face_references);
        std::set<std::string> endpoint_supports;
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(edge, TopAbs_VERTEX, vertices);
        for (int index = 1; index <= vertices.Extent(); ++index) {
            const auto support = referenced_ancestor_tokens(
                vertex_faces, vertices.FindKey(index), face_references);
            if (!support.empty()) {
                endpoint_supports.insert(
                    encoded_topology_reference_set(support));
            }
        }
        std::string semantic_key(role);
        semantic_key += ":from:" +
            encoded_topology_ancestry(selected_parents);
        semantic_key += ":between:" +
            encoded_topology_ancestry(adjacent_faces);
        semantic_key += ":ends:" +
            encoded_topology_ancestry(endpoint_supports);
        result.push_back({edge,
            EdgeReference{treatment_owner, std::move(semantic_key), {}}});
    }
    return result;
}

std::vector<OwnedVertex> complete_edge_treatment_vertices(
    const TopoDS_Shape& result_shape,
    const std::vector<OwnedEdge>& edges,
    const std::vector<OwnedVertex>& propagated_vertices,
    const std::vector<std::pair<TopoDS_Edge, EdgeReference>>& selected,
    const std::string& treatment_owner,
    std::string_view role) {
    const TopologyReferenceIndex<EdgeReference, OwnedEdge> edge_references(edges);
    TopTools_IndexedDataMapOfShapeListOfShape vertex_edges;
    TopExp::MapShapesAndAncestors(
        result_shape, TopAbs_VERTEX, TopAbs_EDGE, vertex_edges);
    const auto selected_parents = selected_edge_parent_tokens(selected);

    std::vector<OwnedVertex> result;
    TopTools_IndexedMapOfShape visited;
    for (TopExp_Explorer explorer(result_shape, TopAbs_VERTEX);
         explorer.More(); explorer.Next()) {
        const auto vertex = explorer.Current();
        if (visited.Contains(vertex)) continue;
        visited.Add(vertex);
        std::vector<VertexReference> inherited_references;
        for (const auto& propagated : propagated_vertices) {
            if (!propagated.shape.IsSame(vertex) ||
                !propagated.reference.valid() ||
                std::ranges::find(inherited_references,
                    propagated.reference) != inherited_references.end()) {
                continue;
            }
            inherited_references.push_back(propagated.reference);
        }
        if (!inherited_references.empty()) {
            for (const auto& inherited : inherited_references) {
                result.push_back({vertex, inherited});
            }
            continue;
        }
        const auto incident_edges = referenced_ancestor_tokens(
            vertex_edges, vertex, edge_references);
        std::string semantic_key(role);
        semantic_key += ":from:" +
            encoded_topology_ancestry(selected_parents);
        semantic_key += ":at:" +
            encoded_topology_ancestry(incident_edges);
        result.push_back({vertex,
            VertexReference{treatment_owner, std::move(semantic_key), {}}});
    }
    return result;
}

// Preserve any already persisted source references, but do not invent a
// Shell-owned entity for an unowned seam/pole. Complete real topology using
// the same parent-derived identities as before.
std::vector<OwnedEdge> complete_shell_edges(const TopoDS_Shape& shape,
    const std::vector<OwnedFace>& faces,const std::vector<OwnedEdge>& propagated,
    const std::string& owner,std::string_view role) {
    auto result=complete_boolean_edges(shape,faces,propagated,owner,role);
    const auto artifacts=shell_parameterization_artifacts(shape);
    std::erase_if(result,[&](const auto& edge){return edge.reference.owner_id==owner&&artifacts.Contains(edge.shape);});
    return result;
}
std::vector<OwnedVertex> complete_shell_vertices(const TopoDS_Shape& shape,
    const std::vector<OwnedEdge>& edges,const std::vector<OwnedVertex>& propagated,
    const std::vector<std::pair<TopoDS_Edge,EdgeReference>>& selected,
    const std::string& owner,std::string_view role) {
    auto result=complete_edge_treatment_vertices(shape,edges,propagated,selected,owner,role);
    const auto artifacts=shell_parameterization_artifacts(shape);
    std::erase_if(result,[&](const auto& vertex){return vertex.reference.owner_id==owner&&artifacts.Contains(vertex.shape);});
    return result;
}

#include "surface_sewing_geometry.inc"
#include "surface_thicken_geometry.inc"
#include "loft_geometry.inc"
#include "surface_intersection_geometry.inc"
#include "surface_trim_geometry.inc"
#include "sheet_form_geometry.inc"

std::string serialize_kernel_shape(const TopoDS_Shape& shape) {
    std::ostringstream serialized_shape;
    // Viewer triangles are persisted separately in ViewerMesh. The kernel
    // snapshot exists only to resume an explicitly requested solid
    // calculation, so storing OCCT's duplicate triangulation wastes both
    // serialization time and document space.
    BRepTools::Write(shape, serialized_shape, false, false,
        TopTools_FormatVersion_CURRENT);
    return serialized_shape.str();
}

void persist_surface_topology(const TopoDS_Shape& shape,const std::vector<OwnedFace>& faces,
        const std::vector<OwnedEdge>& edges,const std::vector<OwnedVertex>& vertices,BodyResult& result,
        const std::vector<TopoDS_Shape>* hidden=nullptr,std::vector<std::string>* hidden_locators=nullptr,
        bool clean_unused_pcurves=false) {
    // Repeated Boolean edits can leave p-curves that refer to faces no longer
    // present in the result.  They are neither geometry nor topology identity,
    // but OCCT would otherwise copy them into every following native snapshot.
    if(clean_unused_pcurves)BRepTools::RemoveUnusedPCurves(shape);
    BRepTools_ShapeSet archive(false,false);
    archive.SetFormatNb(TopTools_FormatVersion_CURRENT);archive.Add(shape);
    const auto bind=[&](const auto& owned,TopAbs_ShapeEnum kind,auto& bindings) {
        std::vector<typename std::decay_t<decltype(bindings)>::value_type> captured;
        captured.reserve(owned.size());
        TopTools_IndexedMapOfShape members;TopExp::MapShapes(shape,kind,members);
        for(const auto& item:owned)if(item.reference.valid()&&members.Contains(item.shape)) {
            std::ostringstream address;archive.Write(item.shape,address);
            captured.push_back({address.str(),item.reference});
        }
        bindings=std::move(captured);
    };
    bind(faces,TopAbs_FACE,result.kernel_faces);bind(edges,TopAbs_EDGE,result.kernel_edges);
    bind(vertices,TopAbs_VERTEX,result.kernel_vertices);
    if(hidden&&hidden_locators) {
        TopTools_IndexedMapOfShape members;TopExp::MapShapes(shape,TopAbs_EDGE,members);
        hidden_locators->clear();
        for(const auto& edge:*hidden)if(members.Contains(edge)) {
            std::ostringstream address;archive.Write(edge,address);hidden_locators->push_back(address.str());
        }
    }
    std::ostringstream stream;archive.Write(stream);archive.Write(shape,stream);result.kernel_shape=stream.str();
}

PrimitiveData bound_face_geometry(const BodyResult& body,bool curve_ancestry=false,
        const std::vector<std::string>* hidden_locators=nullptr,std::vector<TopoDS_Shape>* hidden=nullptr) {
    PrimitiveData result;BRep_Builder builder;BRepTools_ShapeSet archive(builder);
    std::istringstream stream(body.kernel_shape.get());archive.Read(stream);archive.Read(result.shape,stream);
    if(result.shape.IsNull())throw std::runtime_error("Regenerate the model before measuring body properties.");
    TopTools_IndexedMapOfShape members;
    if(curve_ancestry)TopExp::MapShapes(result.shape,members);
    else TopExp::MapShapes(result.shape,TopAbs_FACE,members);
    const auto locate_address=[&](const std::string& locator,TopAbs_ShapeEnum kind) {
        char orientation{};int object{},location{};std::istringstream fields(locator);
        if(!(fields>>orientation>>object>>location)||std::string_view("+-ie").find(orientation)==std::string_view::npos||
            object<1||object>archive.NbShapes()||location<0)
            throw std::runtime_error("Invalid native topology binding");
        fields>>std::ws;if(!fields.eof())throw std::runtime_error("Invalid native topology binding");
        TopoDS_Shape shape;std::istringstream address(locator);archive.Read(shape,address);
        if(shape.IsNull()||shape.ShapeType()!=kind||!members.Contains(shape))
            throw std::runtime_error("Invalid native topology binding");
        return shape;
    };
    const auto locate=[&](const auto& binding,TopAbs_ShapeEnum kind) {
        if(!binding.reference.valid())throw std::runtime_error("Invalid native topology binding");
        return locate_address(binding.locator,kind);
    };
    for(const auto& binding:body.kernel_faces)result.faces.push_back({TopoDS::Face(locate(binding,TopAbs_FACE)),binding.reference});
    if(curve_ancestry) {
        for(const auto& binding:body.kernel_edges)result.edges.push_back({TopoDS::Edge(locate(binding,TopAbs_EDGE)),binding.reference});
        for(const auto& binding:body.kernel_vertices)result.vertices.push_back({TopoDS::Vertex(locate(binding,TopAbs_VERTEX)),binding.reference});
    }
    if(hidden_locators&&hidden) {
        hidden->clear();
        for(const auto& locator:*hidden_locators)hidden->push_back(locate_address(locator,TopAbs_EDGE));
    }
    return result;
}

#include "incremental_history_native.inc"

PrimitiveData place_bound_geometry(PrimitiveData result,const BodyResult& body) {
    for(const auto& placement:body.kernel_placements) {
        BRepBuilderAPI_Transform transform(result.shape,primitive_transform(placement.translation,placement.rotation_degrees),true);
        transform.Build();
        for(auto& face:result.faces)face.shape=TopoDS::Face(transform.ModifiedShape(face.shape));
        for(auto& edge:result.edges)edge.shape=TopoDS::Edge(transform.ModifiedShape(edge.shape));
        for(auto& point:result.vertices)point.shape=TopoDS::Vertex(transform.ModifiedShape(point.shape));
        result.shape=transform.Shape();
    }
    return result;
}

PrimitiveData placed_bound_face_geometry(const BodyResult& body,bool curve_ancestry=false) {
    return place_bound_geometry(bound_face_geometry(body,curve_ancestry),body);
}

std::optional<Vec3> inward_face_direction(const TopoDS_Edge& edge,
    const TopoDS_Face& face, const gp_Pnt& edge_point,
    const gp_Vec& edge_tangent,
    double probe_distance, double tolerance,
    GeomAPI_ProjectPointOnSurf& projection,
    const Handle(Geom_Surface)& surface,const TopLoc_Location& location,
    BRepClass_FaceExplorer& face_explorer) {
    if (surface.IsNull() || edge_tangent.SquareMagnitude() <= 1.0e-18) {
        return std::nullopt;
    }
    const gp_Trsf inverse = location.Transformation().Inverted();
    auto local_edge_point = edge_point.Transformed(inverse);
    projection.Perform(local_edge_point);
    if (!projection.IsDone() || projection.NbPoints() == 0) {
        return std::nullopt;
    }
    double u{}, v{};
    projection.LowerDistanceParameters(u, v);
    gp_Pnt surface_point;
    gp_Vec du;
    gp_Vec dv;
    surface->D1(u, v, surface_point, du, dv);
    du.Transform(location.Transformation());
    dv.Transform(location.Transformation());
    auto normal = du.Crossed(dv);
    if (normal.SquareMagnitude() <= 1.0e-18) return std::nullopt;
    // D1 returns the normal of the underlying geometric surface. A face can
    // use that surface with REVERSED topological orientation (for example the
    // bottom face of a solid), so material-side preview directions must use
    // the oriented face normal rather than the raw supporting-surface normal.
    if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
    normal.Normalize();
    auto tangent = edge_tangent;
    tangent.Normalize();
    auto candidate = normal.Crossed(tangent);
    if (candidate.SquareMagnitude() <= 1.0e-18) return std::nullopt;
    candidate.Normalize();
    const auto is_inside = [&](const gp_Vec& direction) {
        auto test = edge_point.Translated(direction * probe_distance);
        test.Transform(inverse);
        projection.Perform(test);
        if (!projection.IsDone() || projection.NbPoints() == 0 ||
            projection.LowerDistance() > probe_distance * 0.25 + tolerance) {
            return false;
        }
        double test_u{}, test_v{};
        projection.LowerDistanceParameters(test_u, test_v);
        const gp_Pnt2d uv(test_u,test_v);const auto classifier_tolerance=std::max(1.0e-9,tolerance);
        BRepClass_FaceClassifier classifier(face_explorer,uv,classifier_tolerance);
        if(std::getenv("ZIMA_CPP_VERIFY_FACE_CLASSIFIER_REUSE")) {
            // Internal verification oracle; the ordinary path keeps exactly
            // the original OCCT domain, gap policy and classification tolerance.
            BRepClass_FaceClassifier baseline(face,uv,classifier_tolerance);
            if(baseline.State()!=classifier.State())throw std::logic_error("Face classifier reuse differs from the baseline");
        }
        return classifier.State() == TopAbs_IN ||
            classifier.State() == TopAbs_ON;
    };
    auto opposite = candidate;
    opposite.Reverse();
    const bool candidate_inside = is_inside(candidate);
    const bool opposite_inside = is_inside(opposite);
    if (candidate_inside != opposite_inside) {
        if (!candidate_inside) candidate.Reverse();
    } else {
        // A Boolean intersection can split an otherwise continuous planar
        // face along an operational edge. At that split both probes may be
        // classified ON/IN, so geometry alone does not tell which half of
        // the supporting surface OCCT trims for a Fillet/Chamfer. The edge
        // orientation in the owning face wire does: for a FORWARD edge the
        // face lies to the left (surface normal x oriented tangent), and for
        // a REVERSED edge it lies to the right. Use that topology as the
        // deterministic tie-breaker instead of accepting the arbitrary first
        // probe. This is essential for non-right-angle intersections where
        // choosing the other half changes the supplementary corner angle.
        std::optional<TopAbs_Orientation> face_edge_orientation;
        for (TopExp_Explorer explorer(face, TopAbs_EDGE);
             explorer.More(); explorer.Next()) {
            const auto face_edge = TopoDS::Edge(explorer.Current());
            if (!face_edge.IsSame(edge) ||
                (face_edge.Orientation() != TopAbs_FORWARD &&
                 face_edge.Orientation() != TopAbs_REVERSED)) {
                continue;
            }
            face_edge_orientation = face_edge.Orientation();
            break;
        }
        if (face_edge_orientation == TopAbs_REVERSED) candidate.Reverse();
        if (!face_edge_orientation && !candidate_inside) return std::nullopt;
    }
    return Vec3{candidate.X(), candidate.Y(), candidate.Z()};
}

std::vector<Vec3> sampled_inward_face_directions(const TopoDS_Edge& edge,
    const TopoDS_Face& face, const std::vector<double>& parameters,
    BRepClass_FaceExplorer& face_explorer) {
    if (parameters.empty()) return {};
    BRepAdaptor_Curve curve(edge);
    GProp_GProps properties;
    BRepGProp::LinearProperties(edge, properties);
    const double edge_length = std::max(properties.Mass(), 1.0e-6);
    const double tolerance = std::max(BRep_Tool::Tolerance(edge), 1.0e-8);
    const double probe = std::clamp(
        std::max(tolerance * 25.0, edge_length * 1.0e-4),
        edge_length * 1.0e-6, edge_length * 0.02);
    TopLoc_Location location;
    const Handle(Geom_Surface) surface=BRep_Tool::Surface(face,location);
    if(surface.IsNull())return {};
    double umin{},umax{},vmin{},vmax{};surface->Bounds(umin,umax,vmin,vmax);
    // OCCT's many-point API retains only surface preparation. Perform() runs
    // the same extrema search for each original edge/probe point, with the
    // same domain, tolerance and ordering. Avoid rebuilding the spline search
    // data three times per sample; keep this reuse local to one edge/face.
    GeomAPI_ProjectPointOnSurf projection;
    projection.Init(surface,umin,umax,vmin,vmax,std::max(1.0e-9,tolerance*10.0));
    std::vector<std::optional<Vec3>> sampled;
    sampled.reserve(parameters.size());
    for (const double parameter : parameters) {
        gp_Pnt point;
        gp_Vec tangent;
        curve.D1(parameter, point, tangent);
        sampled.push_back(inward_face_direction(
            edge, face, point, tangent, probe, tolerance * 10.0,projection,surface,location,face_explorer));
    }
    if (std::none_of(sampled.begin(), sampled.end(),
            [](const auto& value) { return value.has_value(); })) return {};
    std::vector<Vec3> result;
    result.reserve(sampled.size());
    for (std::size_t index = 0; index < sampled.size(); ++index) {
        if (sampled[index]) {
            result.push_back(*sampled[index]);
            continue;
        }
        auto nearest = sampled.size();
        for (std::size_t distance = 1; distance < sampled.size(); ++distance) {
            if (index >= distance && sampled[index - distance]) {
                nearest = index - distance;
                break;
            }
            if (index + distance < sampled.size() &&
                sampled[index + distance]) {
                nearest = index + distance;
                break;
            }
        }
        result.push_back(*sampled[nearest]);
    }
    return result;
}

// FORM's idealized shell already has an exact edge p-curve on each face.
// Use those UV coordinates and the owning wire's orientation instead of
// globally projecting each sample and two probe points onto a spline again.
// Unavailable/degenerate p-curves retain the established resolver below.
std::vector<Vec3> form_inward_face_directions(const TopoDS_Edge& edge,
        const TopoDS_Face& face,const std::vector<double>& parameters,bool normalize_components=false) {
    double first{},last{};const auto uv=BRep_Tool::CurveOnSurface(edge,face,first,last);
    if(uv.IsNull()||!BRep_Tool::SameParameter(edge))return {};
    std::optional<TopAbs_Orientation> orientation;
    for(TopExp_Explorer item(face,TopAbs_EDGE);item.More();item.Next())if(item.Current().IsSame(edge)) {
        const auto value=item.Current().Orientation();
        if(value!=TopAbs_FORWARD&&value!=TopAbs_REVERSED)return {};
        if(orientation&&*orientation!=value)return {};
        orientation=value;
    }
    if(!orientation)return {};
    BRepAdaptor_Curve curve(edge);BRepAdaptor_Surface surface(face);
    const auto tolerance=std::max({BRep_Tool::Tolerance(edge),BRep_Tool::Tolerance(face),1e-7});
    std::vector<Vec3> result;result.reserve(parameters.size());
    for(const auto parameter:parameters) {
        if(parameter<first-tolerance||parameter>last+tolerance)return {};
        const auto p=uv->Value(parameter);gp_Pnt point,actual;gp_Vec du,dv,tangent;
        surface.D1(p.X(),p.Y(),point,du,dv);curve.D1(parameter,actual,tangent);
        if(point.Distance(actual)>tolerance||tangent.SquareMagnitude()<1e-18)return {};
        auto normal=du.Crossed(dv);if(normal.SquareMagnitude()<1e-18)return {};
        if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
        // Native guides retain the established normalization order.
        // FORM/STEP keep their existing numerical calculation unchanged.
        if(normalize_components){normal.Normalize();tangent.Normalize();}
        auto direction=normal.Crossed(tangent);if(direction.SquareMagnitude()<1e-18)return {};
        if(*orientation==TopAbs_REVERSED)direction.Reverse();direction.Normalize();
        result.push_back({direction.X(),direction.Y(),direction.Z()});
    }
    return result;
}

std::optional<SurfaceGeometry> analytic_surface(const TopoDS_Face& face) {
    BRepAdaptor_Surface surface(face);
    SurfaceGeometry result;
    result.reversed=face.Orientation()==TopAbs_REVERSED;
    gp_Ax3 frame;
    if (surface.GetType()==GeomAbs_Plane) {
        result.kind=SurfaceGeometry::Kind::Plane;
        frame=surface.Plane().Position();
    } else if (surface.GetType()==GeomAbs_Cylinder) {
        result.kind=SurfaceGeometry::Kind::Cylinder;
        const auto cylinder=surface.Cylinder();frame=cylinder.Position();
        result.radius=cylinder.Radius();
        result.axial_min=surface.FirstVParameter();result.axial_max=surface.LastVParameter();
    } else if (surface.GetType()==GeomAbs_Cone) {
        result.kind=SurfaceGeometry::Kind::Cone;
        const auto cone=surface.Cone();frame=cone.Position();
        result.radius=cone.RefRadius();result.semi_angle=cone.SemiAngle();
        result.axial_min=surface.FirstVParameter()*std::cos(result.semi_angle);
        result.axial_max=surface.LastVParameter()*std::cos(result.semi_angle);
    } else if (surface.GetType()==GeomAbs_Torus) {
        result.kind=SurfaceGeometry::Kind::Torus;
        const auto torus=surface.Torus();frame=torus.Position();
        result.radius=torus.MajorRadius();result.minor_radius=torus.MinorRadius();
        result.u_min=surface.FirstUParameter();result.u_max=surface.LastUParameter();
        result.v_min=surface.FirstVParameter();result.v_max=surface.LastVParameter();
        unsigned wires=0,rims=0;bool rectangular=true;
        for(TopExp_Explorer it(face,TopAbs_WIRE);it.More();it.Next())++wires;
        TopTools_IndexedMapOfShape profile_edges;TopExp::MapShapes(face,TopAbs_EDGE,profile_edges);
        for(int i=1;i<=profile_edges.Extent();++i) {
            const auto edge=TopoDS::Edge(profile_edges(i));
            if(BRep_Tool::IsClosed(edge,face))continue;
            BRepAdaptor_Curve curve(edge);
            if(curve.GetType()!=GeomAbs_Circle||!curve.IsClosed()||
                std::abs(curve.Circle().Axis().Direction().Dot(frame.Direction()))<1-1e-9)rectangular=false;
            ++rims;
        }
        result.complete_profile=wires==1&&rectangular&&rims==2&&std::abs(result.u_max-result.u_min-2*std::numbers::pi)<1e-9;
    } else return std::nullopt;
    // OCCT face orientation is relative to the surface parameterization.
    // An indirect frame reverses its natural normal (common for an extruded
    // circle), so TopAbs_REVERSED alone does not mean an internal cylinder.
    result.reversed = result.reversed != !frame.Direct();
    const auto p=frame.Location();const auto z=frame.Direction();const auto x=frame.XDirection();
    result.origin={p.X(),p.Y(),p.Z()};result.axis={z.X(),z.Y(),z.Z()};
    result.radial={x.X(),x.Y(),x.Z()};
    return result;
}

void store_volume_integrals(BodyResult& result,const GProp_GProps& properties) {
    result.volume_integrals.reset();
    if(std::abs(properties.Mass())<=0)return;
    const auto c=properties.CentreOfMass();const auto inertia=properties.MatrixOfInertia();
    VolumeIntegrals p;p.centroid={c.X(),c.Y(),c.Z()};
    const double sign=properties.Mass()<0?-1:1;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)p.inertia[3*i+j]=sign*inertia.Value(i+1,j+1);
    result.volume_integrals=p;
}

// Corner FORM and STEP have independent trimmed patches. Integrate their
// volumes with the same OCCT 8.0.0 face integrator, common reference point,
// span handling and tolerance as VolumePropertiesGK. Only the independent
// calculations run concurrently; reduction retains the original face order.
double corner_form_volume(const TopoDS_Shape& shape,GProp_GProps& properties,
        const char* failure="FORM could not create a valid sheet.",bool split_step_curves=false) {
    gp_XYZ sum(0,0,0);unsigned count=0;
    for(TopExp_Explorer it(shape,TopAbs_VERTEX);it.More();it.Next(),++count)
        sum+=BRep_Tool::Pnt(TopoDS::Vertex(it.Current())).XYZ();
    if(!count)throw std::runtime_error(failure);
    sum/=count;const gp_Pnt location(sum);
    std::vector<TopoDS_Face> faces;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());
        if(face.Orientation()==TopAbs_FORWARD||face.Orientation()==TopAbs_REVERSED)faces.push_back(face);
    }
    struct Task {std::size_t face;TopoDS_Face domain;};
    std::vector<Task> tasks;std::vector<std::size_t> offsets;offsets.reserve(faces.size()+1);
    BRep_Builder builder;std::size_t split_faces=0;
    for(std::size_t index=0;index<faces.size();++index) {
        offsets.push_back(tasks.size());
        // Long extrusion trims can dominate one face worker. Each boundary
        // curve is independent in VinertGK (its curve tolerance does not depend
        // on the number of curves). Keep the real face for all surface bounds,
        // pcurves, orientation and integration; only restrict the domain iterator.
        std::vector<TopoDS_Edge> edges;bool complex_trim=false;
        if(split_step_curves&&BRepAdaptor_Surface(faces[index]).GetType()==GeomAbs_SurfaceOfExtrusion) {
            BRepGProp_Face surface(faces[index],true);
            for(BRepGProp_Domain domain(faces[index]);domain.More();domain.Next()) {
                if(!surface.Load(domain.Value())) {edges.clear();complex_trim=false;break;}
                edges.push_back(domain.Value());complex_trim|=surface.LIntSubs()>64;
            }
        }
        if(complex_trim&&edges.size()>1) {
            ++split_faces;
            for(const auto& edge:edges) {
                TopoDS_Wire wire;builder.MakeWire(wire);builder.Add(wire,edge);
                TopoDS_Face domain;builder.MakeFace(domain);builder.Add(domain,wire);
                tasks.push_back({index,domain});
            }
        } else tasks.push_back({index,{}});
    }
    offsets.push_back(tasks.size());
    const auto workers=std::min<std::size_t>({4,std::max(1u,std::thread::hardware_concurrency()),tasks.size()});
    if(workers<2)return BRepGProp::VolumePropertiesGK(shape,properties,1e-12,false,true);
    if(std::getenv("ZIMA_CPP_CALC_PROFILE"))std::fprintf(stderr,
        "Kernel GK scheduling faces=%zu tasks=%zu split_faces=%zu workers=%zu\n",
        faces.size(),tasks.size(),split_faces,workers);
    std::vector<GProp_GProps> pieces(tasks.size());std::vector<double> errors(tasks.size());
    std::atomic_size_t cursor{0};std::vector<std::future<void>> jobs;
    for(std::size_t worker=0;worker<workers;++worker)jobs.push_back(std::async(std::launch::async,[&] {
        for(auto index=cursor.fetch_add(1);index<tasks.size();index=cursor.fetch_add(1)) {
            const auto& source=faces[tasks[index].face];
            BRepGProp_Face face(source,true);
            BRepGProp_Domain domain(tasks[index].domain.IsNull()?source:tasks[index].domain);
            BRepGProp_VinertGK contribution;contribution.SetLocation(location);
            errors[index]=source.NbChildren()==0?contribution.Perform(face,1e-12,false,false):
                contribution.Perform(face,domain,1e-12,false,false);
            pieces[index]=contribution;
        }
    }));
    for(auto& job:jobs)job.get();
    properties=GProp_GProps{};double error=0.;
    for(std::size_t face=0;face<faces.size();++face) {
        GProp_GProps contribution(location);double face_error=0.;
        for(auto index=offsets[face];index<offsets[face+1];++index) {
            if(!std::isfinite(errors[index])||errors[index]<0.)throw std::runtime_error(failure);
            contribution.Add(pieces[index]);face_error+=errors[index];
        }
        // Preserve both levels of the original ordered addition: curves within
        // their face, then faces within the shape. Do not flatten that reduction.
        properties.Add(offsets[face+1]-offsets[face]==1?pieces[offsets[face]]:contribution);
        error+=face_error;
    }
    return error;
}

// Match OCCT's adaptive VolumeProperties face loop, including its reference
// point and ordered reduction. Only independent face integrations are parallel.
// Repeated solid instances retain OCCT's own shared-solid handling.
double corner_form_integrals(const TopoDS_Shape& shape,GProp_GProps& properties,
        const char* failure="FORM could not create a valid sheet.") {
    std::unordered_set<const TopoDS_TShape*> solids;
    for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next()) {
        const auto solid=it.Current();
        if(!solids.insert(solid.TShape().get()).second)
            return BRepGProp::VolumeProperties(shape,properties,1e-12);
    }
    gp_XYZ sum(0,0,0);unsigned count=0;
    for(TopExp_Explorer it(shape,TopAbs_VERTEX);it.More();it.Next(),++count)
        sum+=BRep_Tool::Pnt(TopoDS::Vertex(it.Current())).XYZ();
    if(!count)return BRepGProp::VolumeProperties(shape,properties,1e-12);
    sum/=count;const gp_Pnt location(sum);
    std::vector<TopoDS_Face> faces;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());
        // A mesh-only face needs OCCT's mesh integration fallback.
        if(BRep_Tool::Surface(face).IsNull())
            return BRepGProp::VolumeProperties(shape,properties,1e-12);
        if(face.Orientation()==TopAbs_FORWARD||face.Orientation()==TopAbs_REVERSED)faces.push_back(face);
    }
    const auto workers=std::min<std::size_t>({4,std::max(1u,std::thread::hardware_concurrency()),faces.size()});
    if(workers<2)return BRepGProp::VolumeProperties(shape,properties,1e-12);
    std::vector<GProp_GProps> pieces(faces.size());std::vector<double> errors(faces.size());
    std::atomic_size_t cursor{0};std::vector<std::future<void>> jobs;
    for(std::size_t worker=0;worker<workers;++worker)jobs.push_back(std::async(std::launch::async,[&] {
        for(auto index=cursor.fetch_add(1);index<faces.size();index=cursor.fetch_add(1)) {
            BRepGProp_Face face(faces[index]);BRepGProp_Domain domain(faces[index]);
            BRepGProp_Vinert contribution;contribution.SetLocation(location);
            errors[index]=contribution.Perform(face,domain,1e-12);
            pieces[index]=contribution;
        }
    }));
    for(auto& job:jobs)job.get();
    gp_Pnt origin(0,0,0);origin.Transform(shape.Location());
    properties=GProp_GProps(origin);double error=0.;
    for(std::size_t index=0;index<pieces.size();++index) {
        if(!std::isfinite(errors[index])||errors[index]<0.)
            throw std::runtime_error(failure);
        properties.Add(pieces[index]);error=std::max(error,errors[index]);
    }
    return error;
}

void calculate_aggregate_properties(const TopoDS_Shape& shape,BodyResult& result,
    bool surface_result,bool imported_step,bool parallel_corner_volume,
    const std::function<void(const char*)>& profile=[](const char*){}) {
    const bool profile_calc=std::getenv("ZIMA_CPP_CALC_PROFILE")!=nullptr;
    GProp_GProps volume_properties;
    GProp_GProps surface_properties;
    // Rational sweep surfaces need adaptive integration; fixed Gauss
    // quadrature can misreport even an exact circular section by percent.
    // Toroidal fillets trimmed by a later cut also require this path: their
    // nontrivial trimming curves can shift the last displayed BOM mass digit.
    bool rational_surface = false;
    bool adaptive_analytic_surface = false;
    for (TopExp_Explorer face(shape, TopAbs_FACE); face.More(); face.Next()) {
        BRepAdaptor_Surface surface(TopoDS::Face(face.Current()));
        if(!adaptive_analytic_surface&&(surface.GetType()==GeomAbs_Torus||surface.GetType()==GeomAbs_Cylinder)) {
            // Complete cylinders and fillets have rectangular UV domains and retain the
            // established fast integral. Only non-isoparametric trimming
            // needs the adaptive path (e.g. an oblique washer split or a
            // round-root castle slot intersecting the threaded bore).
            const auto support=TopoDS::Face(face.Current());
            for(TopExp_Explorer edge(support,TopAbs_EDGE);edge.More();edge.Next()) {
                BRepAdaptor_Curve2d curve(TopoDS::Edge(edge.Current()),support);
                if(curve.GetType()!=GeomAbs_Line) {adaptive_analytic_surface=true;break;}
                const auto direction=curve.Line().Direction();
                if(std::abs(direction.X())>1e-12&&std::abs(direction.Y())>1e-12) {adaptive_analytic_surface=true;break;}
            }
        }
        // A symmetric normal offset can wrap every original rational face.
        // Its GeomAbs_OffsetSurface type must retain adaptive integration too;
        // otherwise an exact cylindrical Thin Loft loses percent-level mass.
        if (surface.GetType() == GeomAbs_BSplineSurface ||
            surface.GetType() == GeomAbs_BezierSurface ||
            surface.GetType() == GeomAbs_OffsetSurface ||
            surface.GetType() == GeomAbs_SurfaceOfExtrusion ||
            surface.GetType() == GeomAbs_SurfaceOfRevolution) {
            rational_surface = true;
            break;
        }
    }
    TopoDS_Compound volume_shape;BRep_Builder volume_builder;volume_builder.MakeCompound(volume_shape);
    for(TopExp_Explorer solid(shape,TopAbs_SOLID);solid.More();solid.Next())volume_builder.Add(volume_shape,solid.Current());
    const bool parallel_properties=parallel_corner_volume&&volume_shape.NbChildren()>0;
    std::optional<double> rational_volume;
    const auto integration_start=profile_calc?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    double tensor_seconds=0.,volume_seconds=0.;
    if (rational_surface) {
        // The adaptive Gauss overload includes all physical properties.
        // GK defaults to volume only; enabling its centroid/inertia flags
        // is substantially slower on trimmed FORM skins at this precision.
        std::future<std::pair<GProp_GProps,double>> tensor;
        if(parallel_properties)tensor=std::async(std::launch::async,[volume_shape,profile_calc,imported_step,&tensor_seconds] {
            const auto start=profile_calc?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
            GProp_GProps properties;
            const auto error=corner_form_integrals(volume_shape,properties,
                imported_step?"OCCT rational volume integration failed":"FORM could not create a valid sheet.");
            if(profile_calc)tensor_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            return std::pair{properties,error};
        });
        else {
            const double error=BRepGProp::VolumeProperties(volume_shape,volume_properties,1e-12);
            if(profile_calc)tensor_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-integration_start).count();
            if(!std::isfinite(error)||error<0)throw std::runtime_error("OCCT rational volume integration failed");
        }
        // Preserve the established span-aware volume calculation. The
        // fast full tensor integration is independently checked against GK.
        GProp_GProps volume_only;
        const auto volume_start=profile_calc?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        const auto volume_error=parallel_properties?corner_form_volume(volume_shape,volume_only,
                imported_step?"OCCT rational volume integration failed":"FORM could not create a valid sheet.",imported_step):
            imported_step&&volume_shape.NbChildren()>0?corner_form_volume(volume_shape,volume_only,
                "OCCT rational volume integration failed",true):
            BRepGProp::VolumePropertiesGK(volume_shape,volume_only,1e-12,false,true);
        if(profile_calc)volume_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-volume_start).count();
        if(!std::isfinite(volume_error)||volume_error<0)throw std::runtime_error("OCCT rational volume integration failed");
        rational_volume=volume_only.Mass();
        if(tensor.valid()) {
            auto [properties,error]=tensor.get();
            if(!std::isfinite(error)||error<0)throw std::runtime_error(
                imported_step?"OCCT rational volume integration failed":"FORM could not create a valid sheet.");
            volume_properties=std::move(properties);
        }
    }
    else if(adaptive_analytic_surface) {
        // Trimmed analytic surfaces need adaptive integration, but
        // do not need the separate span-aware rational-surface GK pass.
        const double error=BRepGProp::VolumeProperties(volume_shape,volume_properties,1e-12);
        if(!std::isfinite(error)||error<0)throw std::runtime_error("OCCT rational volume integration failed");
        if(profile_calc)tensor_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-integration_start).count();
    }
    else {
        BRepGProp::VolumeProperties(volume_shape, volume_properties);
        if(profile_calc)tensor_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-integration_start).count();
    }
    if(profile_calc)std::fprintf(stderr,"Kernel properties tensor=%.6f volume_gk=%.6f combined=%.6f parallel=%d\n",
        tensor_seconds,volume_seconds,std::chrono::duration<double>(std::chrono::steady_clock::now()-integration_start).count(),int(rational_surface&&parallel_properties));
    profile("volume and inertia");
    // Uncapped rational surfaces need adaptive area integration too: fixed
    // quadrature over a circular loft overstates even a cylinder's lateral area.
    // Keep the established solid-only calculation path unchanged.
    if(rational_surface&&surface_result)
        BRepGProp::SurfaceProperties(shape,surface_properties,1e-9);
    else BRepGProp::SurfaceProperties(shape, surface_properties);
    result.volume = rational_volume.value_or(volume_properties.Mass());
    result.surface_area = surface_properties.Mass();
    if(result.surface_area>0) {
        const auto center=surface_properties.CentreOfMass();
        result.surface_centroid=Vec3{center.X(),center.Y(),center.Z()};
    }
    store_volume_integrals(result,volume_properties);
    profile("surface properties");
    result.physical_properties_current=true;
    result.solid_present=TopExp_Explorer(shape,TopAbs_SOLID).More();
}

#include "edge_guide_reuse.inc"

BodyResult make_result(
    const TopoDS_Shape& shape,
    const std::vector<OwnedFace>& owned_faces,
    const std::vector<OwnedEdge>& owned_edges,
    const std::vector<OwnedVertex>& owned_vertices,
    bool original_reference_geometry = false,
    bool persist_kernel_shape = true,
    bool collect_original_references = false,
    const std::vector<TopoDS_Shape>& hidden_display_edges = {},
    double mesh_deflection = 0.0,
    bool calculate_body_properties = true,
    bool parallel_corner_volume = false,
    EdgeGuideReuse* edge_guide_reuse = nullptr,
    bool dependency_edges_only = false,
    const std::set<HistoryReferenceIdentity>* requested_references = nullptr,
    bool retain_all_dependency_edges = true) {
    const bool profile_calc=std::getenv("ZIMA_CPP_CALC_PROFILE")!=nullptr;
    const bool profile_form=std::getenv("ZIMA_CPP_FORM_PROFILE")&&std::ranges::any_of(owned_faces,
        [](const auto& face){return face.reference.semantic_key.starts_with("form:");});
    const bool imported_step=std::ranges::any_of(owned_faces,[](const auto& face){
        return face.reference.semantic_key.starts_with("step:face:#");});
    const bool profile_import=imported_step&&std::getenv("ZIMA_CPP_IMPORT_PROFILE");
    auto profile_start=std::chrono::steady_clock::now();
    const auto profile=[&](const char* phase) {
        const auto now=std::chrono::steady_clock::now();
        if(profile_calc)std::fprintf(stderr,"Kernel result %s: %.6f s\n",phase,std::chrono::duration<double>(now-profile_start).count());
        if(profile_form)std::fprintf(stderr,"FORM result %s: %.6f s\n",phase,std::chrono::duration<double>(now-profile_start).count());
        if(profile_import)std::fprintf(stderr,"STEP result %s: %.6f s\n",phase,std::chrono::duration<double>(now-profile_start).count());
        profile_start=now;
    };
    Bnd_Box mesh_bounds;
    BRepBndLib::Add(shape, mesh_bounds);
    Standard_Real xmin{}, ymin{}, zmin{}, xmax{}, ymax{}, zmax{};
    double linear_deflection = 0.1;
    if (!mesh_bounds.IsVoid()) {
        mesh_bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        const double diagonal = std::hypot(
            std::hypot(xmax - xmin, ymax - ymin), zmax - zmin);
        // 0.1 mm is appropriate for ordinary parts but pathological for a
        // machine/plant-sized STEP compound. Keep roughly 1e-4 of the scene
        // diagonal while retaining the existing detail floor for small work.
        linear_deflection = std::max(0.1, diagonal * 1.0e-4);
    }
    if (mesh_deflection > 0) linear_deflection = mesh_deflection;
    BRepMesh_IncrementalMesh(
        shape, linear_deflection, false, 0.5, true).Perform();
    profile("meshing");
    BodyResult result;
    const auto reference_requested=[&](const auto& reference) {
        return requested_references&&requested_references->contains(
            HistoryReferenceIdentity{reference.owner_id,
                reference.semantic_key,reference.instance_path});
    };
    // Mesh-only reference packets discard aggregate properties. Keep face/edge
    // measurements below: downstream references still require those values.
    // Cheap analytic properties may be completed immediately. Adaptive surface
    // integration is optional analysis and stays off the ordinary editing path.
    const bool adaptive_analysis=calculate_body_properties&&
        aggregate_property_demand==AggregatePropertyDemand::DeferredAdaptive&&[&] {
        for(TopExp_Explorer face(shape,TopAbs_FACE);face.More();face.Next()) {
            const auto type=BRepAdaptor_Surface(TopoDS::Face(face.Current())).GetType();
            if(type==GeomAbs_BSplineSurface||type==GeomAbs_BezierSurface||
                type==GeomAbs_SurfaceOfExtrusion||type==GeomAbs_SurfaceOfRevolution)return true;
        }
        return false;
    }();
    calculate_body_properties=calculate_body_properties&&!dependency_edges_only;
    result.physical_properties_current=calculate_body_properties&&!adaptive_analysis;
    result.solid_present=TopExp_Explorer(shape,TopAbs_SOLID).More();
    if(result.physical_properties_current)calculate_aggregate_properties(shape,result,
        std::ranges::any_of(owned_faces,[](const auto& face){return face.reference.surface_result;}),
        // Imported STEP owns immutable faces too. Reuse the existing ordered
        // parallel integrals instead of forcing its full tensor through the
        // serial OCCT face loop. Precision and requested properties are equal.
        imported_step,parallel_corner_volume||imported_step,profile);
    const bool pending_face_measurements=!eager_face_measurements&&!owned_faces.empty();
    if (!dependency_edges_only&&(persist_kernel_shape||pending_face_measurements)) {
        // Native boundaries must retain the same exact source ancestry after
        // reopening as in the live calculation cache. Face-only bindings cannot
        // identify the endpoints of a bounded face/Sketch-plane intersection.
        // Reuse one archive for all owned kinds; inspection never rebuilds it.
        if(!owned_faces.empty())
            persist_surface_topology(shape,owned_faces,owned_edges,owned_vertices,result,
                nullptr,nullptr,persist_kernel_shape);
        else result.kernel_shape = serialize_kernel_shape(shape);
    }
    profile("BRep persistence");
    std::optional<TopologyReferenceIndex<FaceReference, OwnedFace>> face_references;
    std::optional<TopologyReferenceIndex<EdgeReference, OwnedEdge>> edge_references;
    std::optional<TopologyReferenceIndex<VertexReference, OwnedVertex>>
        vertex_references;
    face_references.emplace(owned_faces);
    if (original_reference_geometry || collect_original_references ||
            requested_references) {
        vertex_references.emplace(feature_endpoint_vertices(shape,owned_faces,owned_edges,owned_vertices));
    }
    // Display body edges remain non-reference geometry, but retain their
    // owning history container solely for cheap wire recolouring.
    edge_references.emplace(owned_edges);
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(
        shape, TopAbs_EDGE, TopAbs_FACE, edge_faces);

    double face_measurement_seconds=0.;
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const TopoDS_Face face = TopoDS::Face(explorer.Current());
        FaceReference reference = face_references->reference_for(face);
        const bool requested_face=reference.valid()&&
            reference_requested(reference);
        if(dependency_edges_only&&!requested_face)continue;
        TopLoc_Location location;
        const Handle(Poly_Triangulation) triangulation =
            BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull()) continue;
        if (reference.valid()&&eager_face_measurements) {
            const auto start=profile_calc?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
            GProp_GProps measured;
            BRepGProp::SurfaceProperties(face, measured);
            if(profile_calc)face_measurement_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            reference.measured_area = std::abs(measured.Mass());
        }
        else reference.measured_area.reset();
        reference.contour_surface.reset();
        if (reference.valid()) if (const auto surface=analytic_surface(face)) {
            auto geometry=*surface;
            if(reference.surface&&
               reference.surface->kind==SurfaceGeometry::Kind::Plane&&
               geometry.kind==SurfaceGeometry::Kind::Plane) {
                const auto& authored=*reference.surface;
                const gp_Vec authored_axis(authored.axis.x,authored.axis.y,
                    authored.axis.z);
                const gp_Vec actual_axis(geometry.axis.x,geometry.axis.y,
                    geometry.axis.z);
                const gp_Vec delta(gp_Pnt(authored.origin.x,authored.origin.y,
                    authored.origin.z),gp_Pnt(geometry.origin.x,
                    geometry.origin.y,geometry.origin.z));
                if(authored_axis.SquareMagnitude()>1e-18&&
                   actual_axis.SquareMagnitude()>1e-18&&
                   std::abs(authored_axis.Normalized().Dot(
                       actual_axis.Normalized()))>1.-1e-9&&
                   std::abs(delta.Dot(actual_axis.Normalized()))<=1e-7) {
                    gp_Vec radial(authored.radial.x,authored.radial.y,
                        authored.radial.z);
                    radial-=authored_axis.Normalized()*
                        radial.Dot(authored_axis.Normalized());
                    if(radial.SquareMagnitude()>1e-18) {
                        radial.Normalize();geometry=authored;
                        geometry.radial={radial.X(),radial.Y(),radial.Z()};
                        const gp_Vec actual_normal=actual_axis.Normalized()*
                            (surface->reversed?-1.:1.);
                        geometry.reversed=authored_axis.Normalized().Dot(
                            actual_normal)<0.;
                    }
                }
            }
            auto& target=geometry.kind==SurfaceGeometry::Kind::Torus?
                reference.contour_surface:reference.surface;
            target=std::make_shared<const SurfaceGeometry>(geometry);
        }
        const bool publish_face=!dependency_edges_only;
        const std::uint32_t base = publish_face
            ?static_cast<std::uint32_t>(result.mesh.vertices.size()):0;
        const gp_Trsf transform = location.Transformation();
        const bool collect_face =
            (collect_original_references||requested_face)&&reference.valid();
        const std::uint32_t reference_base = static_cast<std::uint32_t>(
            result.mesh.original_references.vertices.size());
        for (int node = 1; node <= triangulation->NbNodes(); ++node) {
            const gp_Pnt point = triangulation->Node(node).Transformed(transform);
            const Vec3 viewer_point{point.X(), point.Y(), point.Z()};
            if(publish_face)result.mesh.vertices.push_back(viewer_point);
            if (collect_face) {
                result.mesh.original_references.vertices.push_back(viewer_point);
            }
        }
        for (int triangle = 1; triangle <= triangulation->NbTriangles(); ++triangle) {
            int first{};
            int second{};
            int third{};
            triangulation->Triangle(triangle).Get(first, second, third);
            if (face.Orientation() == TopAbs_REVERSED) std::swap(second, third);
            if(publish_face) {
                result.mesh.triangles.insert(result.mesh.triangles.end(), {
                    base + static_cast<std::uint32_t>(first - 1),
                    base + static_cast<std::uint32_t>(second - 1),
                    base + static_cast<std::uint32_t>(third - 1),
                });
                auto display_reference=original_reference_geometry ? reference : FaceReference{};
                display_reference.surface_result=reference.surface_result;
                result.mesh.triangle_references.push_back(std::move(display_reference));
            }
            if (collect_face) {
                result.mesh.original_references.triangles.insert(
                    result.mesh.original_references.triangles.end(), {
                        reference_base + static_cast<std::uint32_t>(first - 1),
                        reference_base + static_cast<std::uint32_t>(second - 1),
                        reference_base + static_cast<std::uint32_t>(third - 1)});
                result.mesh.original_references.triangle_references.push_back(
                    reference);
            }
        }
    }
    profile("face packets");
    if(profile_calc)std::fprintf(stderr,"Kernel face measurements: %.6f s\n",face_measurement_seconds);
    TopTools_IndexedMapOfShape sampled_edges;
    EdgeGuidePacketSignatures guide_edge_signatures,guide_face_signatures;
    // The packet owns fixed native faces throughout sampling. Reuse each
    // face's UV bounds and incidence preparation across its edge probes;
    // discard it with this packet, before any modeling operation can change it.
    TopTools_IndexedMapOfShape classification_faces;
    std::vector<std::unique_ptr<BRepClass_FaceExplorer>> classification_explorers;
    const auto classifier_for=[&](const TopoDS_Face& face)->BRepClass_FaceExplorer& {
        const auto index=classification_faces.Add(face);
        if(static_cast<std::size_t>(index)>classification_explorers.size()) {
            auto explorer=std::make_unique<BRepClass_FaceExplorer>(face);
            explorer->SetMaxTolerance(0.1);explorer->SetUseBndBox(false);
            classification_explorers.push_back(std::move(explorer));
        }
        return *classification_explorers[static_cast<std::size_t>(index-1)];
    };
    TopTools_IndexedMapOfShape hidden_edges;
    for (const auto& edge : hidden_display_edges) hidden_edges.Add(edge);
    for (TopExp_Explorer explorer(shape, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(explorer.Current());
        if (sampled_edges.Contains(edge) || hidden_edges.Contains(edge)) continue;
        sampled_edges.Add(edge);
        const EdgeReference reference = edge_references->reference_for(edge);
        const bool requested_edge=reference.valid()&&
            reference_requested(reference);
        if(dependency_edges_only&&!retain_all_dependency_edges&&
                !requested_edge)continue;
        BRepAdaptor_Curve curve(edge);
        GCPnts_TangentialDeflection samples(curve, 0.5, linear_deflection);
        if (samples.NbPoints() < 2) continue;
        ViewerEdge viewer_edge;
        viewer_edge.reference = original_reference_geometry
            ? reference : EdgeReference{};
        viewer_edge.display_owner_id = reference.display_owner_id.empty()
            ? reference.owner_id : reference.display_owner_id;
        if (original_reference_geometry)
            viewer_edge.exact_spline = capture_bspline_geometry(curve);
        GProp_GProps measured_edge;
        BRepGProp::LinearProperties(edge, measured_edge);
        viewer_edge.measured_length = std::abs(measured_edge.Mass());
        const int edge_index = edge_faces.FindIndex(edge);
        if(edge_index!=0) {
            viewer_edge.surface_result=true;
            for(TopTools_ListIteratorOfListOfShape it(edge_faces.FindFromIndex(edge_index));it.More();it.Next())
                viewer_edge.surface_result &= face_references->reference_for(it.Value()).surface_result;
        }
        if (edge_index != 0) {
            std::vector<TopoDS_Face> adjacent_faces;
            for (TopTools_ListIteratorOfListOfShape iterator(
                     edge_faces.FindFromIndex(edge_index));
                 iterator.More(); iterator.Next()) {
                const auto face = TopoDS::Face(iterator.Value());
                if (std::none_of(adjacent_faces.begin(), adjacent_faces.end(),
                        [&](const auto& existing) {
                            return existing.IsSame(face);
                        })) adjacent_faces.push_back(face);
            }
            // Boolean intersections can retain paired p-curves on a source
            // surface even though the resulting edge separates distinct faces.
            // A parameter seam belongs to one face; a shared boundary stays visible.
            viewer_edge.parameter_seam = adjacent_faces.size() == 1 &&
                BRep_Tool::IsClosed(edge, adjacent_faces.front());
            if (!viewer_edge.parameter_seam && adjacent_faces.size() == 2) {
                const BRepAdaptor_Surface first(adjacent_faces[0], true);
                const BRepAdaptor_Surface second(adjacent_faces[1], true);
                if (first.GetType() == GeomAbs_Cylinder &&
                    second.GetType() == GeomAbs_Cylinder) {
                    const auto first_cylinder=first.Cylinder();
                    const auto second_cylinder=second.Cylinder();
                    const bool same_cylinder=
                        std::abs(first_cylinder.Radius()-
                            second_cylinder.Radius())<=1.0e-6&&
                        first_cylinder.Axis().Direction().IsParallel(
                            second_cylinder.Axis().Direction(),1.0e-9)&&
                        gp_Lin(first_cylinder.Axis()).Distance(
                            second_cylinder.Axis().Location())<=1.0e-6;
                    if(same_cylinder&&
                       BRepLib::ContinuityOfFaces(edge,adjacent_faces[0],
                           adjacent_faces[1],1.0e-6)!=GeomAbs_C0)
                        viewer_edge.parameter_seam=true;
                }
            }
        }
        if (face_references) {
            std::map<std::string, std::size_t> treatment_face_counts;
            if (edge_index != 0) {
                for (TopTools_ListIteratorOfListOfShape iterator(
                         edge_faces.FindFromIndex(edge_index));
                     iterator.More(); iterator.Next()) {
                    const auto face_reference =
                        face_references->reference_for(iterator.Value());
                    if (face_reference.valid() &&
                        (face_reference.semantic_key.starts_with(
                             "fillet:face") ||
                         face_reference.semantic_key.starts_with(
                             "chamfer:face"))) {
                        ++treatment_face_counts[face_reference.owner_id];
                    }
                }
            }
            for (const auto& [owner_id, adjacent_face_count] :
                 treatment_face_counts) {
                // The visible wire of a treatment feature is the boundary of
                // the union of all faces generated by that feature. An edge
                // shared by two such faces is internal to the union.
                if (adjacent_face_count == 1) {
                    viewer_edge.edge_treatment_owner_ids.push_back(owner_id);
                }
            }
        }
        std::vector<double> sample_parameters;
        Handle(Poly_PolygonOnTriangulation) boundary_polygon;
        Handle(Poly_Triangulation) boundary_mesh;
        TopLoc_Location boundary_location;
        if(edge_index!=0)for(TopTools_ListIteratorOfListOfShape face(edge_faces.FindFromIndex(edge_index));face.More();face.Next()) {
            TopLoc_Location location;
            const auto triangulation=BRep_Tool::Triangulation(TopoDS::Face(face.Value()),location);
            if(triangulation.IsNull())continue;
            const auto polygon=BRep_Tool::PolygonOnTriangulation(edge,triangulation,location);
            if(polygon.IsNull()||!polygon->HasParameters()||polygon->NbNodes()<2)continue;
            if(boundary_polygon.IsNull()||polygon->NbNodes()>boundary_polygon->NbNodes()) {
                boundary_polygon=polygon;boundary_mesh=triangulation;boundary_location=location;
            }
        }
        if(!boundary_polygon.IsNull()) {
            // Draw the same boundary chords as the supporting face mesh.
            // Independently sampled curve chords can lie inside that mesh
            // and disappear in both 3D and depth-rendered Drawing views.
            // Exact splines and measurements remain the source geometry;
            // the existing meshing accuracy is not relaxed.
            const bool reverse=boundary_polygon->Parameter(1)>boundary_polygon->Parameter(boundary_polygon->NbNodes());
            for(int j=1;j<=boundary_polygon->NbNodes();++j) {
                const int index=reverse?boundary_polygon->NbNodes()+1-j:j;
                sample_parameters.push_back(boundary_polygon->Parameter(index));
                const auto point=boundary_mesh->Node(boundary_polygon->Node(index)).Transformed(boundary_location.Transformation());
                viewer_edge.points.push_back({point.X(),point.Y(),point.Z()});
            }
        } else for (int index = 1; index <= samples.NbPoints(); ++index) {
            sample_parameters.push_back(samples.Parameter(index));
            const auto point=curve.Value(samples.Parameter(index));
            viewer_edge.points.push_back({point.X(),point.Y(),point.Z()});
        }
        if (vertex_references) {
            const TopoDS_Vertex first_vertex =
                TopExp::FirstVertex(edge, true);
            const TopoDS_Vertex last_vertex =
                TopExp::LastVertex(edge, true);
            const auto first_reference =
                vertex_references->reference_for(first_vertex);
            const auto last_reference =
                vertex_references->reference_for(last_vertex);
            if (first_reference.valid() && last_reference.valid() &&
                first_reference != last_reference) {
                const gp_Pnt first_point = BRep_Tool::Pnt(first_vertex);
                const auto squared_distance = [](const gp_Pnt& first,
                                                  const Vec3& second) {
                    const double x = first.X() - second.x;
                    const double y = first.Y() - second.y;
                    const double z = first.Z() - second.z;
                    return x * x + y * y + z * z;
                };
                if (squared_distance(first_point, viewer_edge.points.front()) <=
                    squared_distance(first_point, viewer_edge.points.back())) {
                    viewer_edge.edge_treatment_endpoint_references = {
                        first_reference, last_reference};
                } else {
                    viewer_edge.edge_treatment_endpoint_references = {
                        last_reference, first_reference};
                }
            }
        }
        // A seam or boundary with fewer than two distinct persisted face
        // identities cannot publish a treatment-side pair. Avoid sampling
        // directions that the existing pair validation would discard anyway.
        const bool has_distinct_sides = [&] {
            if (edge_index == 0 || !face_references) return false;
            std::optional<FaceReference> first;
            for (TopTools_ListIteratorOfListOfShape iterator(
                     edge_faces.FindFromIndex(edge_index));
                 iterator.More(); iterator.Next()) {
                const auto reference = face_references->reference_for(iterator.Value());
                if (!reference.valid()) continue;
                if (!first) first = reference;
                else if (*first != reference) return true;
            }
            return false;
        }();
        if (has_distinct_sides) {
            std::vector<std::pair<FaceReference, std::vector<Vec3>>> sides;
            std::vector<std::vector<Vec3>> guide_baseline;
            const bool verify_guides=std::getenv("ZIMA_CPP_FORM_VERIFY_GUIDES")!=nullptr;
            for (TopTools_ListIteratorOfListOfShape iterator(
                     edge_faces.FindFromIndex(edge_index));
                 iterator.More(); iterator.Next()) {
                const auto face_reference =
                    face_references->reference_for(iterator.Value());
                if (!face_reference.valid()) continue;
                const bool form_side=symbolic_form_face(face_reference);
                const bool step_side=face_reference.semantic_key.starts_with("step:face:#");
                const auto face=TopoDS::Face(iterator.Value());
                const bool native_face=!form_side&&!step_side;
                auto directions=(form_side||step_side||native_face)?form_inward_face_directions(
                    edge,face,sample_parameters,native_face):std::vector<Vec3>{};
                if(native_face&&!directions.empty()&&std::getenv("ZIMA_CPP_VERIFY_NATIVE_FACE_GUIDES")) {
                    const auto baseline=sampled_inward_face_directions(edge,face,sample_parameters,classifier_for(face));
                    bool same=baseline.size()==directions.size();double deviation{};
                    for(std::size_t i=0;same&&i<baseline.size();++i) {
                        const auto a=baseline[i],b=directions[i];
                        deviation=std::max(deviation,std::hypot(a.x-b.x,a.y-b.y,a.z-b.z));
                        same=deviation<=1e-12;
                    }
                    if(!same) {
                        std::fprintf(stderr,"Native face guide mismatch owner=%s key=%s deviation=%.17g samples=%zu/%zu\n",
                            face_reference.owner_id.c_str(),face_reference.semantic_key.c_str(),deviation,baseline.size(),directions.size());
                        throw std::logic_error("Native face guide differs from the baseline");
                    }
                }
                if(directions.empty()) {
                    const auto calculate=[&]{return sampled_inward_face_directions(
                        edge,TopoDS::Face(iterator.Value()),sample_parameters,classifier_for(TopoDS::Face(iterator.Value())));};
                    if(edge_guide_reuse)directions=edge_guide_reuse->directions(
                        guide_edge_signatures.get(edge),guide_face_signatures.get(iterator.Value()),sample_parameters,calculate);
                    else directions=calculate();
                }
                else if(verify_guides) {
                    const auto original=sampled_inward_face_directions(edge,TopoDS::Face(iterator.Value()),sample_parameters,classifier_for(TopoDS::Face(iterator.Value())));
                    if(original.size()!=directions.size())throw std::runtime_error("FORM definition calculation failed.");
                    for(std::size_t i=0;i<original.size();++i) {
                        const auto a=original[i],b=directions[i];
                        // UV coordinates belong to the actual trimmed edge;
                        // the old global nearest-point projection can select
                        // a slightly different parameter on offset splines.
                        // This check detects side/sign changes, independently
                        // of the exact geometric equation checked above.
                        if(std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z)>1e-3) {
                            std::fprintf(stderr,"FORM guide mismatch side=%s sample=%zu old=(%.9f,%.9f,%.9f) uv=(%.9f,%.9f,%.9f)\n",
                                face_reference.semantic_key.substr(0,42).c_str(),i,a.x,a.y,a.z,b.x,b.y,b.z);
                            throw std::runtime_error("FORM definition calculation failed.");
                        }
                    }
                    guide_baseline.push_back(original);
                }
                if(verify_guides&&guide_baseline.size()<sides.size()+1)guide_baseline.push_back(directions);
                if (!directions.empty()) {
                    sides.emplace_back(face_reference, std::move(directions));
                }
            }
            std::ranges::sort(sides, [](const auto& first, const auto& second) {
                return std::tie(first.first.owner_id,
                           first.first.semantic_key,
                           first.first.instance_path) <
                    std::tie(second.first.owner_id,
                           second.first.semantic_key,
                           second.first.instance_path);
            });
            sides.erase(std::ranges::unique(sides,
                [](const auto& first, const auto& second) {
                    return first.first == second.first;
                }).begin(), sides.end());
            if (sides.size() == 2) {
                if(verify_guides&&guide_baseline.size()==2) {
                    const auto tangent=[&](const auto& a,const auto& b) {
                        if(a.size()!=sample_parameters.size()||b.size()!=a.size())return false;
                        for(std::size_t i=0;i<a.size();++i) {
                            const auto u=a[i],v=b[i];
                            const auto aa=u.x*u.x+u.y*u.y+u.z*u.z,bb=v.x*v.x+v.y*v.y+v.z*v.z;
                            if(aa<=1e-20||bb<=1e-20||(u.x*v.x+u.y*v.y+u.z*v.z)/std::sqrt(aa*bb)>-1.+1e-8)return false;
                        }
                        return true;
                    };
                    if(tangent(guide_baseline[0],guide_baseline[1])!=tangent(sides[0].second,sides[1].second)) {
                        std::fprintf(stderr,"FORM guide tangency classification changed\n");
                        throw std::runtime_error("FORM definition calculation failed.");
                    }
                }
                for (auto& [reference, directions] : sides) {
                    viewer_edge.edge_treatment_side_references.push_back(
                        std::move(reference));
                    viewer_edge.edge_treatment_side_directions.push_back(
                        std::move(directions));
                }
            }
        }
        // A treatment cross-section is defined only when both adjacent face
        // sides were resolved. Never persist a misleading half-pair: sheet
        // boundaries and degenerate seams remain valid display edges, but
        // simply do not offer the fake two-sided preview.
        if (viewer_edge.edge_treatment_side_directions.size() != 2) {
            viewer_edge.edge_treatment_side_directions.clear();
            viewer_edge.edge_treatment_side_references.clear();
        }
        if ((collect_original_references||requested_edge) &&
                reference.valid()) {
            auto reference_edge = viewer_edge;
            reference_edge.reference = reference;
            result.mesh.original_references.edges.push_back(
                std::move(reference_edge));
        }
        result.mesh.edges.push_back(std::move(viewer_edge));
    }
    profile("edge packets");
    TopTools_IndexedDataMapOfShapeListOfShape vertex_surface_faces;
    TopExp::MapShapesAndAncestors(shape,TopAbs_VERTEX,TopAbs_FACE,vertex_surface_faces);
    TopTools_IndexedMapOfShape sampled_vertices;
    if ((original_reference_geometry || collect_original_references ||
            requested_references)&&
            (!dependency_edges_only||requested_references)) for (
        TopExp_Explorer explorer(shape, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
        const TopoDS_Vertex vertex = TopoDS::Vertex(explorer.Current());
        if (sampled_vertices.Contains(vertex)) continue;
        sampled_vertices.Add(vertex);
        const VertexReference reference =
            vertex_references->reference_for(vertex);
        if (!reference.valid()) continue;
        const bool requested_point=reference_requested(reference);
        if(dependency_edges_only&&!requested_point)continue;
        const gp_Pnt point = BRep_Tool::Pnt(vertex);
        ViewerPoint viewer_point{
            {point.X(), point.Y(), point.Z()}, reference};
        // Result-body vertices are reference targets, not permanent screen
        // markers. The viewer reveals them only while a Vertex-taking
        // command is active (or when explicitly highlighted).
        viewer_point.always_visible = false;
        if(vertex_surface_faces.Contains(vertex)) {
            viewer_point.surface_result=true;
            for(TopTools_ListIteratorOfListOfShape it(vertex_surface_faces.FindFromKey(vertex));it.More();it.Next())
                viewer_point.surface_result &= face_references->reference_for(it.Value()).surface_result;
        }
        if (!dependency_edges_only&&original_reference_geometry)
            result.mesh.points.push_back(viewer_point);
        if (collect_original_references||requested_point) {
            result.mesh.original_references.points.push_back(viewer_point);
        }
    }
    profile("vertex packets");
    return result;
}

TopoDS_Shape technological_lateral_face(
        const TopoDS_Shape& primitive, GeomAbs_SurfaceType expected) {
    for (TopExp_Explorer explorer(primitive, TopAbs_FACE);
         explorer.More(); explorer.Next()) {
        const auto face = TopoDS::Face(explorer.Current());
        if (BRepAdaptor_Surface(face).GetType() == expected) return face;
    }
    throw std::runtime_error("OCCT technological surface has no lateral face");
}

TopoDS_Shape make_thread_cylinder_face(const ThreadSurfaceRequest& request,
        double radius, double offset, double length) {
    if (!(radius > 0.0) || !(length > 1.0e-9)) return {};
    const gp_Dir axis(request.axis_direction.x, request.axis_direction.y,
        request.axis_direction.z);
    const gp_Dir radial(request.radial_direction.x, request.radial_direction.y,
        request.radial_direction.z);
    const gp_Pnt origin(request.origin.x + request.axis_direction.x * offset,
        request.origin.y + request.axis_direction.y * offset,
        request.origin.z + request.axis_direction.z * offset);
    const auto solid = BRepPrimAPI_MakeCylinder(
        gp_Ax2(origin, axis, radial), radius, length).Shape();
    return technological_lateral_face(solid, GeomAbs_Cylinder);
}

TopoDS_Shape make_thread_cone_face(const ThreadSurfaceRequest& request,
        double first_radius, double second_radius, double offset, double length) {
    if (!(first_radius > 0.0) || !(second_radius > 0.0) ||
        !(length > 1.0e-9)) return {};
    const gp_Dir axis(request.axis_direction.x, request.axis_direction.y,
        request.axis_direction.z);
    const gp_Dir radial(request.radial_direction.x, request.radial_direction.y,
        request.radial_direction.z);
    const gp_Pnt origin(request.origin.x + request.axis_direction.x * offset,
        request.origin.y + request.axis_direction.y * offset,
        request.origin.z + request.axis_direction.z * offset);
    if (std::abs(first_radius-second_radius)<1e-9)
        return make_thread_cylinder_face(request,first_radius,offset,length);
    const auto solid = BRepPrimAPI_MakeCone(gp_Ax2(origin, axis, radial),
        first_radius, second_radius, length).Shape();
    return technological_lateral_face(solid, GeomAbs_Cone);
}

struct ThreadSurfaceBoundaries {
    Vec3 first, first_normal, last, last_normal;
    std::string first_key,last_key;
};
struct ThreadRimRegistry {
    using Keys=std::set<std::tuple<std::string,std::string,std::string>>;
    Keys display,original;
};

void append_technological_surface(ViewerMesh& target, const TopoDS_Shape& shape,
        const std::string& owner, const std::string& role,
        const ThreadSurfaceBoundaries& bounds, ThreadRimRegistry& rims, double mesh_deflection) {
    if (shape.IsNull()) return;
    auto source = make_result(shape, {}, {}, {}, false, false, false, {}, mesh_deflection).mesh;
    const FaceReference face_reference{owner, "thread:surface:" + role, {}};
    std::shared_ptr<const SurfaceGeometry> analytic_contour;
    for(TopExp_Explorer faces(shape,TopAbs_FACE);faces.More();faces.Next())
        if(const auto support=analytic_surface(TopoDS::Face(faces.Current()));support&&
            (support->kind==SurfaceGeometry::Kind::Cylinder||support->kind==SurfaceGeometry::Kind::Cone)) {
            analytic_contour=std::make_shared<const SurfaceGeometry>(*support);break;
        }
    for (auto& reference : source.triangle_references) {
        reference=face_reference;
        // Keep the already calculated analytic outline without exposing these
        // cosmetic technological surfaces as placement supports.
        reference.contour_surface=analytic_contour;
    }
    double minimum=std::numeric_limits<double>::infinity(),maximum=-minimum;
    for(const auto& edge:source.edges)for(const auto& p:edge.points) {
        const double t=(p.x-bounds.first.x)*bounds.first_normal.x+
            (p.y-bounds.first.y)*bounds.first_normal.y+(p.z-bounds.first.z)*bounds.first_normal.z;
        minimum=std::min(minimum,t);maximum=std::max(maximum,t);
    }
    const auto axial_plane=[&](double t) {return Vec3{bounds.first.x+bounds.first_normal.x*t,
        bounds.first.y+bounds.first_normal.y*t,bounds.first.z+bounds.first_normal.z*t};};
    for (auto& edge : source.edges) {
        const auto on_plane=[&](const Vec3& origin,const Vec3& normal) {
            return edge.points.size()>1 && std::ranges::all_of(edge.points,[&](const auto& p) {
                return std::abs((p.x-origin.x)*normal.x+(p.y-origin.y)*normal.y+
                    (p.z-origin.z)*normal.z)<=1e-7;
            });
        };
        // Identity follows the authored cap roles, including a cap shortened by
        // an explicit cut, never OCCT edge order or the tessellation seam.
        // Keep other unclassified trimming wires cosmetic.
        const auto end=edge.parameter_seam ? "seam" :
            (on_plane(bounds.first,bounds.first_normal)||on_plane(axial_plane(minimum),bounds.first_normal))
            ? "start" : (on_plane(bounds.last,bounds.last_normal)||on_plane(axial_plane(maximum),bounds.first_normal)) ? "end" : "trimmed";
        const auto boundary=end==std::string_view("start") ? bounds.first_key :
            end==std::string_view("end") ? bounds.last_key : role+":"+end;
        const bool cosmetic=edge.parameter_seam||end==std::string_view("trimmed");
        edge.reference = {owner, std::string(cosmetic
            ? "thread:wire:" : "thread:boundary:") + boundary, {}};
        edge.display_owner_id = owner;
    }
    const auto vertex_base = static_cast<std::uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(), source.vertices.begin(),
        source.vertices.end());
    for (const auto index : source.triangles)
        target.triangles.push_back(vertex_base + index);
    target.triangle_references.insert(target.triangle_references.end(),
        source.triangle_references.begin(), source.triangle_references.end());
    const auto append_edge=[](auto& edges,ViewerEdge edge,ThreadRimRegistry::Keys& closed_rims) {
        const auto closed=[](const auto& e) {
            if(e.points.size()<4)return false;
            const auto& a=e.points.front();const auto& b=e.points.back();
            return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)<=1e-7;
        };
        // A complete conic rim shared by the root and its runout has one
        // identity. Retain separate open fragments after subsequent trimming.
        if(edge.reference.semantic_key.starts_with("thread:boundary:")&&closed(edge)&&!closed_rims.emplace(edge.reference.owner_id,
            edge.reference.semantic_key,edge.reference.instance_path).second)return;
        edges.push_back(std::move(edge));
    };
    for(const auto& edge:source.edges)append_edge(target.edges,edge,rims.display);

    const auto reference_base = static_cast<std::uint32_t>(
        target.original_references.vertices.size());
    target.original_references.vertices.insert(
        target.original_references.vertices.end(), source.vertices.begin(),
        source.vertices.end());
    for (const auto index : source.triangles)
        target.original_references.triangles.push_back(reference_base + index);
    target.original_references.triangle_references.insert(
        target.original_references.triangle_references.end(),
        source.triangle_references.begin(), source.triangle_references.end());
    for (const auto& edge : source.edges) {
        if(edge.reference.semantic_key.starts_with("thread:wire:"))continue;
        auto persisted = edge;
        persisted.display_owner_id.clear();
        append_edge(target.original_references.edges,std::move(persisted),rims.original);
    }
}

void append_reference_geometry(
    ViewerReferenceGeometry& target, ViewerReferenceGeometry source) {
    const auto offset = static_cast<std::uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(),
        std::make_move_iterator(source.vertices.begin()),
        std::make_move_iterator(source.vertices.end()));
    for (const auto index : source.triangles) {
        target.triangles.push_back(offset + index);
    }
    target.triangle_references.insert(target.triangle_references.end(),
        std::make_move_iterator(source.triangle_references.begin()),
        std::make_move_iterator(source.triangle_references.end()));
    target.edges.insert(target.edges.end(),
        std::make_move_iterator(source.edges.begin()),
        std::make_move_iterator(source.edges.end()));
    target.points.insert(target.points.end(),
        std::make_move_iterator(source.points.begin()),
        std::make_move_iterator(source.points.end()));
    target.axes.insert(target.axes.end(),
        std::make_move_iterator(source.axes.begin()),
        std::make_move_iterator(source.axes.end()));
}

void append_original_reference_geometry(
    ViewerReferenceGeometry& target, ViewerMesh source) {
    const auto offset = static_cast<std::uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(),
        source.vertices.begin(), source.vertices.end());
    for (std::size_t triangle = 0;
         triangle < source.triangle_references.size(); ++triangle) {
        const auto& reference = source.triangle_references[triangle];
        if (!reference.valid()) continue;
        target.triangles.insert(target.triangles.end(), {
            offset + source.triangles[triangle * 3],
            offset + source.triangles[triangle * 3 + 1],
            offset + source.triangles[triangle * 3 + 2]});
        target.triangle_references.push_back(reference);
    }
    for (auto& edge : source.edges) {
        if (edge.reference.valid()) target.edges.push_back(std::move(edge));
    }
    target.points.insert(target.points.end(), source.points.begin(), source.points.end());
    target.axes.insert(target.axes.end(), source.axes.begin(), source.axes.end());
}

ViewerReferenceGeometry reference_geometry_for_owners(
    const ViewerReferenceGeometry& source,
    const std::unordered_set<std::string>& owners) {
    ViewerReferenceGeometry result;
    for (std::size_t triangle = 0;
         triangle < source.triangle_references.size(); ++triangle) {
        const auto& reference = source.triangle_references[triangle];
        if (!owners.contains(reference.owner_id)) continue;
        const auto output_offset =
            static_cast<std::uint32_t>(result.vertices.size());
        for (int corner = 0; corner < 3; ++corner) {
            result.vertices.push_back(source.vertices.at(
                source.triangles.at(triangle * 3 + corner)));
            result.triangles.push_back(output_offset + corner);
        }
        result.triangle_references.push_back(reference);
    }
    std::ranges::copy_if(source.edges, std::back_inserter(result.edges),
        [&](const auto& edge) { return owners.contains(edge.reference.owner_id); });
    std::ranges::copy_if(source.points, std::back_inserter(result.points),
        [&](const auto& point) { return owners.contains(point.reference.owner_id); });
    std::ranges::copy_if(source.axes, std::back_inserter(result.axes),
        [&](const auto& axis) { return owners.contains(axis.reference.owner_id); });
    return result;
}

ViewerReferenceGeometry reference_geometry_for_history_references(
    const ViewerReferenceGeometry& source,
    const std::set<HistoryReferenceIdentity>& requested,
    bool selected = true) {
    ViewerReferenceGeometry result;
    const auto matches = [&](const auto& reference) {
        return requested.contains({reference.owner_id,
            reference.semantic_key, reference.instance_path}) == selected;
    };
    for (std::size_t triangle = 0;
         triangle < source.triangle_references.size(); ++triangle) {
        const auto& reference = source.triangle_references[triangle];
        if (!matches(reference)) continue;
        const auto output_offset =
            static_cast<std::uint32_t>(result.vertices.size());
        for (int corner = 0; corner < 3; ++corner) {
            result.vertices.push_back(source.vertices.at(
                source.triangles.at(triangle * 3 + corner)));
            result.triangles.push_back(output_offset + corner);
        }
        result.triangle_references.push_back(reference);
    }
    std::ranges::copy_if(source.edges, std::back_inserter(result.edges),
        [&](const auto& edge) { return matches(edge.reference); });
    std::ranges::copy_if(source.points, std::back_inserter(result.points),
        [&](const auto& point) { return matches(point.reference); });
    std::ranges::copy_if(source.axes, std::back_inserter(result.axes),
        [&](const auto& axis) { return matches(axis.reference); });
    return result;
}

template <typename Geometry>
bool contains_history_reference(const Geometry& geometry,
        const HistoryReferenceIdentity& identity) {
    const auto matches=[&](const auto& reference) {
        return HistoryReferenceIdentity{reference.owner_id,
            reference.semantic_key,reference.instance_path}==identity;
    };
    return std::ranges::any_of(geometry.triangle_references,matches)||
        std::ranges::any_of(geometry.edges,[&](const auto& edge) {
            return matches(edge.reference);
        })||std::ranges::any_of(geometry.points,[&](const auto& point) {
            return matches(point.reference);
        })||std::ranges::any_of(geometry.axes,[&](const auto& axis) {
            return matches(axis.reference);
        });
}

bool contains_history_references(const BodyResult& boundary,
        const std::set<HistoryReferenceIdentity>& requested) {
    return std::ranges::all_of(requested,[&](const auto& identity) {
        return contains_history_reference(
                   boundary.mesh.original_references,identity)||
            contains_history_reference(boundary.mesh,identity);
    });
}

void compact_history_reference_geometry(std::vector<BodyResult>& boundaries) {
    if (boundaries.size() < 2) return;
    for (auto iterator = boundaries.begin(); iterator != boundaries.end() - 1;
         ++iterator) {
        // Ordinary intermediate packets discard cumulative authored geometry.
        // An exact placement request is already a narrow boundary-only packet;
        // clearing it here would retain the coverage proof but lose the very
        // geometry that proof protects from final-body fallback.
        if(iterator->history_reference_coverage.empty())
            iterator->mesh.original_references = {};
        iterator->incremental_history.reset();
    }
}

}  // namespace

BSplineGeometry exact_path_segment_geometry(const Sweep3DRequest::PathSegment& segment) {
    const auto curve=sweep_spine_curve(segment);
    if(curve.IsNull())return {1,{segment.start,segment.end},{0,0,1,1},{1,1}};
    const auto exact=capture_bspline_geometry(curve);
    if(!exact)throw std::runtime_error("Curve-plane intersection calculation failed.");
    return *exact;
}

struct OcctKernel::LiveCache {
    const std::shared_ptr<const void> provenance=std::make_shared<const int>(0);
    std::vector<BodyResult> mark_current(std::vector<BodyResult> result,bool proven) const {
        const auto current=[&](const auto& self,const BodyResult& body)->bool {
            if(!body.calculation_errors.empty())return false;
            if(body.calculation_provenance==provenance)return true;
            if(body.body_boundaries.empty())return false;
            return std::ranges::all_of(body.body_boundaries,[&](const auto& branch) {
                return !branch.second.empty()&&std::ranges::all_of(branch.second,[&](const auto& value){return self(self,value);});
            });
        };
        proven=proven||(!result.empty()&&std::ranges::all_of(result,[&](const auto& body){return current(current,body);}));
        for(auto& boundary:result)boundary.calculation_provenance=proven?provenance:nullptr;
        return result;
    }
    std::unordered_map<std::string,sheet_form_geometry::Operands> prepared_forms;
    std::unordered_map<std::string,BodySnapshot> reference_measurements;
    std::deque<std::string> prepared_form_order;
    struct Topology {
        std::vector<OwnedFace> faces;
        std::vector<OwnedEdge> edges;
        std::vector<OwnedVertex> vertices;
        // Same-domain seams kept only to retain distinct persisted face
        // ancestry. They are topology, not visible model edges.
        std::vector<TopoDS_Shape> hidden_display_edges;
    };

    struct Boundary {
        TopoDS_Shape shape;
        std::shared_ptr<const Topology> topology;
        std::shared_ptr<const OriginalFaces> original_faces;
        sheet_state_sources::Sources sheet_sources;
        bool original_edges_retained{};
    };

    std::unordered_map<std::string, Boundary> boundaries;
    std::deque<std::string> insertion_order;
    struct CopySource {
        PrimitiveData operand;
        BodyResult solid;
        std::vector<sheet_form_geometry::Instance> forms;
    };
    std::unordered_map<std::string, CopySource> copy_sources;
    std::deque<std::string> copy_source_order;
    // Retain recently calculated continuation frames by their exact authored
    // prefix. Returning to an earlier value (including Undo/Redo) can then
    // reuse the already validated native representation instead of rebuilding
    // and reserializing equivalent OCCT topology.
    std::unordered_map<std::string, KernelHistoryFrame> history_frames;
    std::deque<std::string> history_frame_order;
    // Full reference packets are larger than the shared live topology. Keep
    // only recent copy/subtraction boundaries, rather than one full prefix per
    // long-lived cached shape. A miss simply calculates an earlier boundary.
    std::unordered_map<std::string, ViewerReferenceGeometry> copy_references;
    std::deque<std::string> copy_reference_order;
    std::unordered_map<std::string, ViewerMesh> reference_meshes;
    std::deque<std::string> reference_insertion_order;
};

struct OcctKernel::HistoryContext {
    std::vector<OwnedFace> external_faces;
    std::map<std::string,PrimitiveData> external_shell_bodies;
    std::shared_ptr<const OriginalFaces> original_faces;
    std::string dependency_key;
    bool require_original_faces{};
    std::set<std::string> requested_solids;
    std::map<std::string,BodyResult> source_solids;
    bool final_boundary_first{};
    std::set<std::size_t> retained_output_boundaries;
    std::set<std::size_t> full_output_boundaries;
    std::set<std::size_t> edge_output_boundaries;
    std::map<std::size_t,std::set<HistoryReferenceIdentity>>
        reference_output_boundaries;
    bool preview_wire{};
    TopoDS_Shape preview_input;
    bool direct_batch_active{};
    bool defer_terminal_publication{};
};

OcctKernel::OcctKernel() : live_cache_(std::make_unique<LiveCache>()) {}

SolidStraighteningPlan OcctKernel::prepare_straightening(
    const RevolutionRequest& source,double coefficient) const {
    return solid_straightening::revolution(source,coefficient);
}

SolidStraighteningPlan OcctKernel::prepare_straightening(
    const Sweep3DRequest& source,double coefficient) const {
    if(source.twist) {
        GProp_GProps area;
        auto target=expand_profile_twist(source,coefficient,true,&area);
        const auto& n=source.sections.front().profile_normal;
        gp_Trsf end;end.SetRotation(gp_Ax1(source.twist->axis_point?
            solid_straightening::point(*source.twist->axis_point):area.CentreOfMass(),gp_Dir(n.x,n.y,n.z)),
            source.twist->angle_degrees*std::numbers::pi/180.);
        end.SetTranslationPart(end.TranslationPart()+(gp_Vec(n.x,n.y,n.z).Normalized()*source.twist->length).XYZ());
        SolidStraighteningPlan result;result.primitive=std::move(target);
        result.section_area=area.Mass();result.centroid_path_length=source.twist->length;result.coefficient=coefficient;
        result.source_start_centroid=solid_straightening::vector(area.CentreOfMass().XYZ());
        result.source_end_centroid=solid_straightening::vector(area.CentreOfMass().Transformed(end).XYZ());
        result.straight_end_centroid=std::get<Sweep3DRequest>(result.primitive).path_points.back();
        result.end_transform=solid_straightening::frame_values(end);result.start_tangent=n;
        return result;
    }
    return source.transported?solid_straightening::transported_sweep(source,coefficient):
        solid_straightening::ordinary_sweep(source,coefficient);
}
SolidStraighteningPlan OcctKernel::prepare_straightening(
    const FeatureGroupRequest& source,double coefficient) const {
    auto target=source;
    std::optional<SolidStraighteningPlan> result;
    for(auto& child:target.children) {
        std::visit([&](auto& request) {
            using T=std::decay_t<decltype(request)>;
            if constexpr(std::is_same_v<T,ExtrusionRequest>) {
                if(request.surface_result)
                    throw std::invalid_argument("Straightening requires a solid with a constant cross-section.");
            }
        },child);
        if(const auto* revolution=std::get_if<RevolutionRequest>(&child)) {
            auto resolved=oriented_feature_revolution(*revolution);
            const auto plan=prepare_straightening(resolved,coefficient);
            if(!result)result=plan;
            child=std::get<ExtrusionRequest>(plan.primitive);
        } else if(const auto* sweep=std::get_if<Sweep3DRequest>(&child)) {
            const auto plan=prepare_straightening(*sweep,coefficient);
            if(!result)result=plan;
            std::visit([&](const auto& replacement) {
                using T=std::decay_t<decltype(replacement)>;
                if constexpr(std::is_same_v<T,ExtrusionRequest>||std::is_same_v<T,Sweep3DRequest>)child=replacement;
            },plan.primitive);
        }
    }
    if(!result)throw std::invalid_argument("Select at least one eligible solid element.");
    result->primitive=std::move(target);return std::move(*result);
}
OcctKernel::~OcctKernel() = default;

std::string OcctKernel::name() const {
    return "OCCT";
}

bool OcctKernel::has_current_calculation(const std::vector<BodyResult>& boundaries) const {
    return !boundaries.empty()&&std::ranges::all_of(boundaries,[&](const auto& boundary){
        return boundary.calculation_errors.empty()&&boundary.calculation_provenance==live_cache_->provenance;
    });
}

// Preserve the geometry calculation's exact ordered physical reduction when
// completing a cached compound. Each Body keeps its own area integration policy.
static void aggregate_body_properties(BodyResult& document,const std::set<std::string>& available) {
    document.volume=0.;document.surface_area=0.;document.physical_properties_current=true;
    document.solid_present=false;document.volume_integrals.reset();document.surface_centroid.reset();
    for(const auto& id:available) {
        const auto& output=document.body_outputs.at(id).get();
        document.volume += output.volume;
        document.surface_area += output.surface_area;
        document.physical_properties_current=document.physical_properties_current&&output.physical_properties_current;
        document.solid_present=document.solid_present||output.has_solid();
    }
    if(document.physical_properties_current&&document.volume>0) {
        VolumeIntegrals total;bool valid=true;
        for(const auto& id:available) {
            const auto& body=document.body_outputs.at(id).get();if(body.volume==0)continue;
            if(!body.volume_integrals){valid=false;break;}
            const auto c=body.volume_integrals->centroid;total.centroid.x+=body.volume*c.x;
            total.centroid.y+=body.volume*c.y;total.centroid.z+=body.volume*c.z;
        }
        if(valid) {
            total.centroid.x/=document.volume;total.centroid.y/=document.volume;total.centroid.z/=document.volume;
            for(const auto& id:available) {
                const auto& body=document.body_outputs.at(id).get();if(!body.volume_integrals)continue;
                const auto& p=*body.volume_integrals;for(int i=0;i<9;++i)total.inertia[i]+=p.inertia[i];
                inertia_shift(total.inertia,body.volume,{p.centroid.x-total.centroid.x,p.centroid.y-total.centroid.y,p.centroid.z-total.centroid.z});
            }document.volume_integrals=total;
        }
    }

    if(document.physical_properties_current&&document.surface_area>0) {
        Vec3 sum{};bool complete=true;
        for(const auto& id:available) {
            const auto& body=document.body_outputs.at(id).get();
            if(body.surface_area<=0)continue;
            if(!body.surface_centroid){complete=false;break;}
            const auto c=*body.surface_centroid;const double weight=body.surface_area/document.surface_area;
            sum.x+=weight*c.x;sum.y+=weight*c.y;sum.z+=weight*c.z;
        }
        if(complete)document.surface_centroid=sum;
    }
    if(!document.physical_properties_current) {
        document.volume=0.;document.surface_area=0.;document.volume_integrals.reset();document.surface_centroid.reset();
    }
}

void OcctKernel::ensure_body_properties(BodyResult& body) const {
    if(body.physical_properties_current)return;
    if(body.kernel_shape==kernel_body_compound&&body.kernel_compound_bodies.empty()&&
        (!body.mesh.triangles.empty()||body.has_solid()))
        throw std::invalid_argument("Regenerate the model before measuring body properties.");
    if(body.kernel_shape==kernel_body_compound) {
        auto analyzed=body;
        const std::set<std::string> available(body.kernel_compound_bodies.begin(),body.kernel_compound_bodies.end());
        for(const auto& id:available)if(!analyzed.body_outputs.at(id)->physical_properties_current) {
            auto output=analyzed.body_outputs.at(id).get();ensure_body_properties(output);
            analyzed.body_outputs.insert_or_assign(id,std::move(output));
        }
        aggregate_body_properties(analyzed,available);
        body=std::move(analyzed);return;
    }
    TopoDS_Shape shape;
    if(!body.kernel_shape.empty())shape=read_kernel_shape(body);
    else if(const auto found=live_cache_->boundaries.find(body.source_fingerprint);found!=live_cache_->boundaries.end())
        shape=found->second.shape;
    else throw std::invalid_argument("Regenerate the model before measuring body properties.");
    if(shape.IsNull())throw std::invalid_argument("Regenerate the model before measuring body properties.");
    BodyResult properties;
    calculate_aggregate_properties(shape,properties,
        std::ranges::any_of(body.mesh.triangle_references,[](const auto& face){return face.surface_result;})||
            std::ranges::any_of(body.kernel_faces,[](const auto& face){return face.reference.surface_result;}),
        std::ranges::any_of(body.mesh.triangle_references,[](const auto& face){return face.semantic_key.starts_with("step:face:#");}),true);
    // Publish only completed analysis. Geometry, bindings, ancestry, mesh and
    // source fingerprint remain exactly the previously calculated snapshot.
    body.volume=properties.volume;body.surface_area=properties.surface_area;
    body.volume_integrals=properties.volume_integrals;body.surface_centroid=properties.surface_centroid;
    body.solid_present=properties.solid_present;body.physical_properties_current=true;
}

std::vector<std::optional<double>> OcctKernel::measure_face_areas(
        const BodyResult& body,std::span<const FaceReference> references) const {
    std::vector<std::optional<double>> result(references.size());
    const auto from_sources=[&] {
        for(std::size_t source=body.measurement_sources.size();source>0;--source) {
            std::vector<FaceReference> missing;std::vector<std::size_t> indices;
            for(std::size_t i=0;i<references.size();++i)if(!result[i]) {missing.push_back(references[i]);indices.push_back(i);}
            if(missing.empty())break;
            const auto values=measure_face_areas(body.measurement_sources[source-1],missing);
            for(std::size_t i=0;i<values.size();++i)if(values[i])result[indices[i]]=values[i];
        }
    };
    if(body.kernel_shape==kernel_body_compound) {
        for(const auto& id:body.kernel_compound_bodies) {
            const auto values=measure_face_areas(body.body_outputs.at(id),references);
            for(std::size_t i=0;i<values.size();++i)if(values[i])result[i]=values[i];
        }
        from_sources();return result;
    }
    if(body.kernel_shape.empty()||std::ranges::none_of(body.kernel_faces,[&](const auto& binding){
        return std::ranges::any_of(references,[&](const auto& reference){return binding.reference==reference;});})) {from_sources();return result;}
    const auto native=bound_face_geometry(body);
    const TopologyReferenceIndex<FaceReference,OwnedFace> identities(native.faces);
    // Match the existing triangle publication and last-value-wins ordering.
    // Integrate only requested native faces, without meshing or regeneration.
    for(TopExp_Explorer face(native.shape,TopAbs_FACE);face.More();face.Next()) {
        const auto identity=identities.reference_for(face.Current());
        std::optional<double> area;
        for(std::size_t i=0;i<references.size();++i)if(identity==references[i]) {
            if(!area) {GProp_GProps properties;BRepGProp::SurfaceProperties(face.Current(),properties);area=std::abs(properties.Mass());}
            result[i]=area;
        }
    }
    from_sources();return result;
}

OcctEdgeTreatmentSupportInspection
OcctKernel::inspect_edge_treatment_supports(const BodyResult& body,
        std::span<const EdgeReference> references) const {
    const auto started=std::chrono::steady_clock::now();
    if(body.kernel_shape==kernel_body_compound)
        throw std::invalid_argument(
            "Edge support inspection requires one calculated Body");
    if(body.kernel_shape.empty())
        throw std::invalid_argument(
            "Edge support inspection requires a native Body snapshot");
    for(std::size_t index=0;index<references.size();++index) {
        if(!references[index].valid())
            throw std::invalid_argument(
                "Edge support inspection received an invalid reference");
        if(std::ranges::find(references.begin(),references.begin()+
                static_cast<std::ptrdiff_t>(index),references[index])!=
                references.begin()+static_cast<std::ptrdiff_t>(index))
            throw std::invalid_argument(
                "Edge support inspection received a duplicate reference");
    }
    const auto native=placed_bound_face_geometry(body,true);
    OcctEdgeTreatmentSupportInspection result;
    const auto decoded=std::chrono::steady_clock::now();
    result.native_decode_ms=std::chrono::duration<double,std::milli>(
        decoded-started).count();

    const auto curve_kind=[](const TopoDS_Edge& edge) {
        switch(BRepAdaptor_Curve(edge).GetType()) {
        case GeomAbs_Line:return OcctEdgeTreatmentCurveKind::Line;
        case GeomAbs_Circle:return OcctEdgeTreatmentCurveKind::Circle;
        case GeomAbs_Ellipse:return OcctEdgeTreatmentCurveKind::Ellipse;
        case GeomAbs_Hyperbola:return OcctEdgeTreatmentCurveKind::Hyperbola;
        case GeomAbs_Parabola:return OcctEdgeTreatmentCurveKind::Parabola;
        case GeomAbs_BezierCurve:return OcctEdgeTreatmentCurveKind::Bezier;
        case GeomAbs_BSplineCurve:return OcctEdgeTreatmentCurveKind::BSpline;
        case GeomAbs_OffsetCurve:return OcctEdgeTreatmentCurveKind::Offset;
        default:return OcctEdgeTreatmentCurveKind::Other;
        }
    };
    const auto surface_kind=[](const TopoDS_Face& face) {
        switch(BRepAdaptor_Surface(face,true).GetType()) {
        case GeomAbs_Plane:return OcctEdgeTreatmentSurfaceKind::Plane;
        case GeomAbs_Cylinder:return OcctEdgeTreatmentSurfaceKind::Cylinder;
        case GeomAbs_Cone:return OcctEdgeTreatmentSurfaceKind::Cone;
        case GeomAbs_Sphere:return OcctEdgeTreatmentSurfaceKind::Sphere;
        case GeomAbs_Torus:return OcctEdgeTreatmentSurfaceKind::Torus;
        case GeomAbs_BezierSurface:return OcctEdgeTreatmentSurfaceKind::Bezier;
        case GeomAbs_BSplineSurface:return OcctEdgeTreatmentSurfaceKind::BSpline;
        case GeomAbs_SurfaceOfRevolution:
            return OcctEdgeTreatmentSurfaceKind::Revolution;
        case GeomAbs_SurfaceOfExtrusion:
            return OcctEdgeTreatmentSurfaceKind::Extrusion;
        case GeomAbs_OffsetSurface:return OcctEdgeTreatmentSurfaceKind::Offset;
        default:return OcctEdgeTreatmentSurfaceKind::Other;
        }
    };
    const auto continuity_order=[](GeomAbs_Shape continuity) {
        switch(continuity) {
        case GeomAbs_G1:case GeomAbs_C1:return 1u;
        case GeomAbs_G2:case GeomAbs_C2:return 2u;
        case GeomAbs_C3:return 3u;
        case GeomAbs_CN:return 4u;
        default:return 0u;
        }
    };
    const auto outward_normal=[](const TopoDS_Edge& edge,
            const TopoDS_Face& face)->std::optional<gp_Dir> {
        double first{},last{};
        const auto pcurve=BRep_Tool::CurveOnSurface(
            edge,face,first,last);
        if(pcurve.IsNull()||!std::isfinite(first)||
           !std::isfinite(last)||last<=first)
            return {};
        const auto uv=pcurve->Value((first+last)/2.);
        BRepAdaptor_Surface surface(face,true);
        gp_Pnt point;gp_Vec du,dv;
        surface.D1(uv.X(),uv.Y(),point,du,dv);
        auto normal=du.Crossed(dv);
        if(normal.SquareMagnitude()<=1.0e-18)return {};
        if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
        return gp_Dir(normal);
    };
    const auto face_references=[&](const TopoDS_Face& face) {
        std::vector<FaceReference> aliases;
        for(const auto& owned:native.faces)
            if(owned.shape.IsSame(face)&&owned.reference.valid()&&
               std::ranges::find(aliases,owned.reference)==aliases.end())
                aliases.push_back(owned.reference);
        std::ranges::sort(aliases,[](const auto& first,const auto& second) {
            return std::tie(first.owner_id,first.semantic_key,
                       first.instance_path)<
                std::tie(second.owner_id,second.semantic_key,
                       second.instance_path);
        });
        return aliases;
    };

    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(native.shape,TopAbs_EDGE,
        TopAbs_FACE,edge_faces);
    result.segments.reserve(references.size());
    for(const auto& reference:references) {
        std::vector<TopoDS_Edge> matches;
        for(const auto& owned:native.edges)
            if(owned.reference==reference&&
               std::ranges::none_of(matches,[&](const auto& current) {
                   return current.IsSame(owned.shape);
               }))
                matches.push_back(TopoDS::Edge(owned.shape));
        if(matches.size()!=1)
            throw std::runtime_error(matches.empty()
                ?"Selected edge is missing from its native Body snapshot"
                :"Selected edge is ambiguous in its native Body snapshot");
        const auto& edge=matches.front();
        OcctEdgeTreatmentSupportSegment segment;
        segment.edge=reference;segment.curve_kind=curve_kind(edge);
        const int adjacency_index=edge_faces.FindIndex(edge);
        std::vector<TopoDS_Face> faces;
        if(adjacency_index)for(TopTools_ListIteratorOfListOfShape iterator(
                edge_faces.FindFromIndex(adjacency_index));
                iterator.More();iterator.Next()) {
            const auto face=TopoDS::Face(iterator.Value());
            if(std::ranges::none_of(faces,[&](const auto& current) {
                    return current.IsSame(face);
                }))faces.push_back(face);
        }
        for(const auto& face:faces)
            segment.adjacent_faces.push_back({face_references(face),
                surface_kind(face),face.Orientation()==TopAbs_REVERSED});
        if(faces.size()==2) {
            segment.continuity_order=continuity_order(
                BRepLib::ContinuityOfFaces(edge,faces[0],faces[1],1.0e-7));
            const auto first=outward_normal(edge,faces[0]);
            const auto second=outward_normal(edge,faces[1]);
            if(first&&second)
                segment.outward_normal_angle_degrees=first->Angle(*second)*
                    180./std::numbers::pi;
        }
        result.segments.push_back(std::move(segment));
    }
    result.adjacency_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-decoded).count();
    return result;
}


struct OcctKernel::NativeFaceSource {
    const BodyResult* source{};
    const BodyResult* placed_source{};
    PrimitiveData geometry(const OcctKernel& owner) const {
        const auto native=owner.live_cache_->boundaries.find(source->source_fingerprint);
        if(native!=owner.live_cache_->boundaries.end()&&native->second.topology) {
            PrimitiveData geometry;geometry.shape=native->second.shape;
            geometry.faces=native->second.topology->faces;
            geometry.edges=native->second.topology->edges;
            geometry.vertices=native->second.topology->vertices;
            return place_bound_geometry(std::move(geometry),placed_source?*placed_source:*source);
        }
        return place_bound_geometry(bound_face_geometry(*source,true),placed_source?*placed_source:*source);
    }
};

OcctKernel::NativeFaceSource OcctKernel::native_face_source(
        const BodyResult& body,const FaceReference& reference) const {
    const BodyResult* source=nullptr;
    const BodyResult* placed_source=nullptr;
    const auto find=[&](auto&& self,const BodyResult& candidate,const BodyResult* placed)->void {
        if(candidate.kernel_shape==kernel_body_compound) {
            for(const auto& id:candidate.kernel_compound_bodies) {
                const auto& output=candidate.body_outputs.at(id).get();
                const auto local=candidate.body_boundaries.find(id);
                // Body publication changes its fingerprint to include its
                // placement. The exact calculated local boundary retains the
                // cache key and native ancestry; apply the published placement.
                if(local!=candidate.body_boundaries.end()&&!local->second.empty()) {
                    const auto input=candidate.body_inputs.find(id);
                    self(self,local->second.back(),input==candidate.body_inputs.end()?&output:&input->second.get());
                }
                else self(self,output,nullptr);
            }
            return;
        }
        const auto native=live_cache_->boundaries.find(candidate.source_fingerprint);
        const bool stored=std::ranges::any_of(candidate.kernel_faces,[&](const auto& binding){return same_face_identity(binding.reference,reference);});
        const bool live=native!=live_cache_->boundaries.end()&&native->second.topology&&
            std::ranges::any_of(native->second.topology->faces,[&](const auto& face){return same_face_identity(face.reference,reference);});
        if(stored||live) {
            if(source&&source!=&candidate)throw std::runtime_error("Intersection face reference is ambiguous.");
            source=&candidate;placed_source=placed;
        }
    };
    find(find,body,nullptr);
    if(!source)throw std::runtime_error("Intersection source face is missing at its input boundary.");
    // Resolution still checks the entire source universe for ambiguity. The
    // caller may then decode this exact immutable source once for several faces.
    return {source,placed_source};
}

BodyResult OcctKernel::capture_reference_face(const BodyResult& body,const FaceReference& reference) const {
    const auto native=native_face_source(body,reference).geometry(*this);
    TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);
    std::vector<OwnedFace> faces;
    for(const auto& face:native.faces)if(same_face_identity(face.reference,reference)) {
        faces.push_back(face);builder.Add(compound,face.shape);
    }
    if(faces.empty())throw std::runtime_error("The original extrusion target is unavailable.");
    BodyResult result;persist_surface_topology(compound,faces,{}, {},result);
    result.source_fingerprint="external-target-face:"+dependency_fingerprint(result.kernel_shape.get())+":"+body.source_fingerprint+":"+
        std::to_string(reference.owner_id.size())+":"+reference.owner_id+":"+reference.semantic_key+":"+reference.instance_path;
    result.physical_properties_current=false;
    return result;
}

BodyResult OcctKernel::capture_reference_face_history(const std::vector<BodyResult>& history,const FaceReference& reference) const {
    // Find the original defining native face in document history order. A
    // later Boolean's operational fragments cannot replace that source face.
    for(const auto& boundary:history) {
        try{static_cast<void>(native_face_source(boundary,reference));}
        catch(const std::runtime_error& error) {
            if(std::string_view(error.what())=="Intersection source face is missing at its input boundary.")continue;
            throw;
        }
        // Decode only after selection. Malformed archive/binding errors must
        // propagate, never choose a different later geometry on their behalf.
        return capture_reference_face(boundary,reference);
    }
    throw std::runtime_error("The original extrusion target is unavailable.");
}

std::vector<ViewerEdge> OcctKernel::calculate_feature_preview(HistoryOperation operation,
        const BodyResult& references,KernelRigidPlacement frame_to_document,const BodyResult& local_input,
        const std::vector<OrderedUpToPatch>& external_sources) const {
    return calculate_feature_geometry(std::move(operation),references,frame_to_document,local_input,external_sources,true).mesh.edges;
}

BodyResult OcctKernel::calculate_feature_tool(HistoryOperation operation,const BodyResult& references,
        KernelRigidPlacement frame,const BodyResult& input,const std::vector<OrderedUpToPatch>& sources) const {
    return calculate_feature_geometry(std::move(operation),references,frame,input,sources,false);
}

BodyResult OcctKernel::calculate_add_block(const BodyResult& input,
        std::span<const BodyResult> tools,
        std::span<const std::size_t> tool_history_indices,
        std::span<const std::string> history_owners,
        double boolean_tolerance,double mesh_deflection,
        const std::string& source_fingerprint,
        std::span<const std::string> history_fingerprints,
        bool publish_result) const {
    if(tools.empty()||tools.size()!=tool_history_indices.size())
        throw std::invalid_argument(
            "Add block requires one history index per tool");
    if(!publish_result&&source_fingerprint.empty())
        throw std::invalid_argument(
            "A deferred Add block requires a live source fingerprint");
    const auto block_geometry=[&](const BodyResult& body) {
        if(!body.source_fingerprint.empty()) {
            const auto native=live_cache_->boundaries.find(
                body.source_fingerprint);
            if(native!=live_cache_->boundaries.end()&&
               native->second.topology) {
                PrimitiveData geometry;
                geometry.shape=native->second.shape;
                geometry.faces=native->second.topology->faces;
                geometry.edges=native->second.topology->edges;
                geometry.vertices=native->second.topology->vertices;
                return place_bound_geometry(std::move(geometry),body);
            }
        }
        return placed_bound_face_geometry(body,true);
    };
    const bool has_input=!(input.kernel_shape.empty()&&
        (input.source_fingerprint.empty()||
         !live_cache_->boundaries.contains(input.source_fingerprint)))&&
        input.kernel_shape!=kernel_body_compound;
    if(!std::isfinite(boolean_tolerance)||boolean_tolerance<=0.||
       !std::isfinite(mesh_deflection)||mesh_deflection<=0.)
        throw std::invalid_argument("Add block tolerances are invalid");

    std::map<std::string,std::size_t> owner_history;
    for(std::size_t index=0;index<history_owners.size();++index)
        owner_history.try_emplace(history_owners[index],index);
    const auto history_of=[&](const auto& reference,
            std::size_t fallback) {
        const auto found=owner_history.find(reference.owner_id);
        return found==owner_history.end()?fallback:found->second;
    };
    const auto ordered_unique=[&](auto& references,
            std::size_t fallback) {
        std::ranges::sort(references,[&](const auto& left,const auto& right) {
            return std::tuple{history_of(left,fallback),
                       encoded_topology_reference(left)}<
                std::tuple{history_of(right,fallback),
                       encoded_topology_reference(right)};
        });
        const auto duplicate=std::ranges::unique(references);
        references.erase(duplicate.begin(),duplicate.end());
    };
    const auto boundary_operand=[&](const BodyResult& body,
            std::size_t fallback) {
        const auto data=block_geometry(body);
        OcctBoundarySolid result;
        result.shape=data.shape;
        TopTools_IndexedMapOfShape faces,edges,vertices;
        TopExp::MapShapes(data.shape,TopAbs_FACE,faces);
        TopExp::MapShapes(data.shape,TopAbs_EDGE,edges);
        TopExp::MapShapes(data.shape,TopAbs_VERTEX,vertices);
        result.faces.resize(static_cast<std::size_t>(faces.Extent()));
        for(int index=1;index<=faces.Extent();++index) {
            auto& face=result.faces[static_cast<std::size_t>(index-1)];
            face.shape=TopoDS::Face(faces(index));
            face.history_index=fallback;
        }
        for(const auto& owned:data.faces) {
            const int index=faces.FindIndex(owned.shape);
            if(!index||!owned.reference.valid())continue;
            auto& face=result.faces[static_cast<std::size_t>(index-1)];
            face.references.push_back(owned.reference);
            face.history_index=std::min(face.history_index,
                history_of(owned.reference,fallback));
        }
        for(auto& face:result.faces) {
            ordered_unique(face.references,fallback);
            if(face.references.empty())
                throw std::runtime_error(
                    "Add block input face has no stable identity");
        }

        std::vector<int> edge_outputs(
            static_cast<std::size_t>(edges.Extent())+1,-1);
        for(const auto& owned:data.edges) {
            const int index=edges.FindIndex(owned.shape);
            if(!index||!owned.reference.valid())continue;
            auto& output=edge_outputs[static_cast<std::size_t>(index)];
            if(output<0) {
                output=static_cast<int>(result.edges.size());
                result.edges.push_back({TopoDS::Edge(edges(index)),{},
                    fallback});
            }
            auto& edge=result.edges[static_cast<std::size_t>(output)];
            edge.references.push_back(owned.reference);
            edge.history_index=std::min(edge.history_index,
                history_of(owned.reference,fallback));
        }
        for(auto& edge:result.edges)
            ordered_unique(edge.references,fallback);

        std::vector<int> vertex_outputs(
            static_cast<std::size_t>(vertices.Extent())+1,-1);
        for(const auto& owned:data.vertices) {
            const int index=vertices.FindIndex(owned.shape);
            if(!index||!owned.reference.valid())continue;
            auto& output=vertex_outputs[static_cast<std::size_t>(index)];
            if(output<0) {
                output=static_cast<int>(result.vertices.size());
                result.vertices.push_back({TopoDS::Vertex(vertices(index)),{},
                    fallback});
            }
            auto& vertex=result.vertices[
                static_cast<std::size_t>(output)];
            vertex.references.push_back(owned.reference);
            vertex.history_index=std::min(vertex.history_index,
                history_of(owned.reference,fallback));
        }
        for(auto& vertex:result.vertices)
            ordered_unique(vertex.references,fallback);
        return result;
    };

    std::vector<OcctBoundarySolid> operands;
    operands.reserve(tools.size()+static_cast<std::size_t>(has_input));
    if(has_input)operands.push_back(boundary_operand(input,0));
    for(std::size_t index=0;index<tools.size();++index) {
        if((tools[index].kernel_shape.empty()&&
            (tools[index].source_fingerprint.empty()||
             !live_cache_->boundaries.contains(
                 tools[index].source_fingerprint)))||
           tools[index].kernel_shape==kernel_body_compound)
            throw std::invalid_argument(
                "Add block tool has no standalone calculated solid");
        operands.push_back(boundary_operand(tools[index],
            tool_history_indices[index]));
    }
    const auto batch_fuse_boundary=[&] {
        std::vector<OwnedFace> source_faces;
        std::vector<OwnedEdge> source_edges;
        std::vector<OwnedVertex> source_vertices;
        for(const auto& operand:operands) {
            for(const auto& face:operand.faces)
                for(const auto& reference:face.references)
                    source_faces.push_back({face.shape,reference});
            for(const auto& edge:operand.edges)
                for(const auto& reference:edge.references)
                    source_edges.push_back({edge.shape,reference});
            for(const auto& vertex:operand.vertices)
                for(const auto& reference:vertex.references)
                    source_vertices.push_back({vertex.shape,reference});
        }
        TopTools_ListOfShape arguments,fuse_tools;
        arguments.Append(operands.front().shape);
        for(std::size_t index=1;index<operands.size();++index)
            fuse_tools.Append(operands[index].shape);
        BRepAlgoAPI_Fuse fuse;
        fuse.SetArguments(arguments);
        fuse.SetTools(fuse_tools);
        fuse.SetRunParallel(true);
        fuse.SetToFillHistory(true);
        fuse.SetFuzzyValue(std::max(1.0e-7,boolean_tolerance));
        fuse.Build();
        if(!fuse.IsDone()||fuse.Shape().IsNull())
            throw std::runtime_error(
                "Batch Add Fuse did not produce a result");
        auto propagated_faces=propagate_topology(
            fuse,source_faces,std::vector<OwnedFace>{});
        auto propagated_edges=propagate_topology(
            fuse,source_edges,std::vector<OwnedEdge>{});
        auto propagated_vertices=propagate_topology(
            fuse,source_vertices,std::vector<OwnedVertex>{});
        auto unified=unify_preserving_face_provenance(
            fuse.Shape(),propagated_faces,propagated_edges,
            propagated_vertices,std::max(1.0e-7,boolean_tolerance),false);
        if(unified.shape.IsNull()||(!unified.validated&&
           !BRepCheck_Analyzer(
                unified.shape,true,true,false).IsValid()))
            throw std::runtime_error(
                "Batch Add Fuse produced an invalid protected result");

        OcctBoundaryUnion result;
        result.shape=std::move(unified.shape);
        TopTools_IndexedMapOfShape faces,edges,vertices;
        TopExp::MapShapes(result.shape,TopAbs_FACE,faces);
        TopExp::MapShapes(result.shape,TopAbs_EDGE,edges);
        TopExp::MapShapes(result.shape,TopAbs_VERTEX,vertices);
        result.faces.resize(static_cast<std::size_t>(faces.Extent()));
        result.edges.resize(static_cast<std::size_t>(edges.Extent()));
        result.vertices.resize(static_cast<std::size_t>(vertices.Extent()));
        for(int index=1;index<=faces.Extent();++index) {
            auto& entry=result.faces[static_cast<std::size_t>(index-1)];
            entry.shape=TopoDS::Face(faces(index));
            entry.history_index=std::numeric_limits<std::size_t>::max();
        }
        for(int index=1;index<=edges.Extent();++index) {
            auto& entry=result.edges[static_cast<std::size_t>(index-1)];
            entry.shape=TopoDS::Edge(edges(index));
            entry.history_index=std::numeric_limits<std::size_t>::max();
        }
        for(int index=1;index<=vertices.Extent();++index) {
            auto& entry=result.vertices[static_cast<std::size_t>(index-1)];
            entry.shape=TopoDS::Vertex(vertices(index));
            entry.history_index=std::numeric_limits<std::size_t>::max();
        }
        const auto attach=[&](const auto& owned,const auto& members,
                auto& metadata) {
            for(const auto& source:owned) {
                const int index=members.FindIndex(source.shape);
                if(!index||!source.reference.valid())continue;
                auto& entry=metadata[static_cast<std::size_t>(index-1)];
                entry.references.push_back(source.reference);
                entry.history_index=std::min(entry.history_index,
                    history_of(source.reference,0));
            }
            for(auto& entry:metadata) {
                ordered_unique(entry.references,entry.history_index==
                        std::numeric_limits<std::size_t>::max()
                    ?0:entry.history_index);
                if(entry.history_index==
                        std::numeric_limits<std::size_t>::max())
                    entry.history_index=0;
            }
        };
        attach(unified.faces,faces,result.faces);
        attach(unified.edges,edges,result.edges);
        attach(unified.vertices,vertices,result.vertices);
        if(std::ranges::any_of(result.faces,[](const auto& face) {
                return face.references.empty();
            }))
            throw std::runtime_error(
                "Batch Add Fuse lost authored face ancestry");
        return result;
    };
    const auto boundary=std::getenv("ZIMA_CPP_DISABLE_DIRECT_BATCH_FUSE")
        ?occt_boundary_union(operands,
            std::max(1.0e-7,boolean_tolerance))
        :batch_fuse_boundary();
    const auto named=occt_boundary_named_topology(boundary,history_owners,
        std::max(1.0e-7,boolean_tolerance));
    std::vector<OwnedFace> faces;
    std::vector<OwnedEdge> edges;
    std::vector<OwnedVertex> vertices;
    faces.reserve(named.faces.size());
    edges.reserve(named.edges.size());
    vertices.reserve(named.vertices.size());
    for(const auto& entry:named.faces)
        faces.push_back({entry.shape,entry.reference});
    for(const auto& entry:named.edges)
        edges.push_back({entry.shape,entry.reference});
    for(const auto& entry:named.vertices)
        vertices.push_back({entry.shape,entry.reference});
    const auto live_topology=[&] {
        auto topology=std::make_shared<LiveCache::Topology>(
            LiveCache::Topology{faces,edges,vertices,{}});
        const auto append_aliases=[](const auto& metadata,
                auto& owned) {
            for(const auto& item:metadata) {
                const auto canonical=std::ranges::find_if(owned,
                    [&](const auto& value) {
                        return value.shape.IsSame(item.shape);
                    });
                if(canonical==owned.end())
                    throw std::runtime_error(
                        "Boundary topology alias has no canonical shape");
                const auto canonical_reference=canonical->reference;
                for(const auto& reference:item.references)
                    if(reference.valid()&&
                       reference!=canonical_reference)
                        owned.push_back({item.shape,reference});
            }
        };
        append_aliases(boundary.faces,topology->faces);
        append_aliases(boundary.edges,topology->edges);
        append_aliases(boundary.vertices,topology->vertices);
        return topology;
    };
    BodyResult result;
    if(publish_result) {
        const bool calculate_properties=
            aggregate_property_demand==AggregatePropertyDemand::All||
            aggregate_property_demand==AggregatePropertyDemand::Final||
            aggregate_property_demand==AggregatePropertyDemand::DeferredAdaptive;
        result=make_result(boundary.shape,faces,edges,vertices,true,true,
            false,{},mesh_deflection,calculate_properties);
    } else {
        // A deferred history row owns a live native shape but intentionally no
        // public mesh/archive or aggregate analysis. Mark its scalar values as
        // pending: the default `true` would expose the empty packet's zero
        // volume as a completed measurement.
        result.physical_properties_current=false;
        result.solid_present=TopExp_Explorer(
            boundary.shape,TopAbs_SOLID).More();
    }
    result.resolved_up_to_limits=input.resolved_up_to_limits;
    for(const auto& tool:tools)
        result.resolved_up_to_limits.insert(
            result.resolved_up_to_limits.end(),
            tool.resolved_up_to_limits.begin(),
            tool.resolved_up_to_limits.end());
    result.source_fingerprint=source_fingerprint;
    if(!source_fingerprint.empty()) {
        auto references=input.mesh.original_references;
        for(const auto& tool:tools)
            append_reference_geometry(references,
                tool.mesh.original_references);
        append_reference_geometry(references,
            result.mesh.original_references);
        result.mesh.original_references=references;
        std::shared_ptr<const OriginalFaces> originals;
        for(const auto& operand:operands) {
            std::vector<OwnedFace> operand_faces;
            std::vector<OwnedEdge> operand_edges;
            std::vector<OwnedVertex> operand_vertices;
            for(const auto& face:operand.faces)
                for(const auto& reference:face.references)
                    operand_faces.push_back({face.shape,reference});
            for(const auto& edge:operand.edges)
                for(const auto& reference:edge.references)
                    operand_edges.push_back({edge.shape,reference});
            for(const auto& vertex:operand.vertices)
                for(const auto& reference:vertex.references)
                    operand_vertices.push_back({vertex.shape,reference});
            originals=std::make_shared<OriginalFaces>(OriginalFaces{
                std::move(operand_faces),std::move(originals),
                std::move(operand_edges),std::move(operand_vertices),
                operand.shape});
        }
        live_cache_->boundaries.insert_or_assign(source_fingerprint,
            LiveCache::Boundary{boundary.shape,live_topology(),
                std::move(originals),nullptr,false});
        std::erase(live_cache_->insertion_order,source_fingerprint);
        live_cache_->insertion_order.push_back(source_fingerprint);
        live_cache_->copy_references.insert_or_assign(
            source_fingerprint,std::move(references));
        std::erase(live_cache_->copy_reference_order,
            source_fingerprint);
        live_cache_->copy_reference_order.push_back(source_fingerprint);
        if(!history_fingerprints.empty()) {
            for(std::size_t index=0;index<tools.size();++index) {
                const auto history_index=tool_history_indices[index];
                if(history_index>=history_fingerprints.size()||
                   history_index>=history_owners.size())
                    throw std::invalid_argument(
                        "Add block tool history index is outside its prefix fingerprints");
                const auto key=history_fingerprints[history_index]+
                    ":solid:"+history_owners[history_index];
                auto operand=block_geometry(tools[index]);
                auto solid=tools[index];
                solid.source_fingerprint=key;
                live_cache_->copy_sources.insert_or_assign(key,
                    LiveCache::CopySource{std::move(operand),
                        std::move(solid),{}});
                std::erase(live_cache_->copy_source_order,key);
                live_cache_->copy_source_order.push_back(key);
            }
        }
        if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))
            std::cerr<<"[add block retention] fingerprint="
                <<source_fingerprint<<" faces="<<faces.size()
                <<" edges="<<edges.size()
                <<" references="
                <<live_cache_->copy_references.contains(
                    source_fingerprint)<<'\n';
        while(live_cache_->insertion_order.size()>
                kLiveShapeCacheLimit) {
            live_cache_->boundaries.erase(
                live_cache_->insertion_order.front());
            live_cache_->insertion_order.pop_front();
        }
        while(live_cache_->copy_reference_order.size()>
                kLiveShapeCacheLimit) {
            live_cache_->copy_references.erase(
                live_cache_->copy_reference_order.front());
            live_cache_->copy_reference_order.pop_front();
        }
        while(live_cache_->copy_source_order.size()>
                kLiveShapeCacheLimit) {
            live_cache_->copy_sources.erase(
                live_cache_->copy_source_order.front());
            live_cache_->copy_source_order.pop_front();
        }
    }
    return result;
}

BodyResult OcctKernel::calculate_subtract_block(const BodyResult& input,
        std::span<const BodyResult> tools,
        std::span<const std::size_t> tool_history_indices,
        std::span<const std::string> history_owners,
        double boolean_tolerance,double mesh_deflection,
        const std::string& source_fingerprint,
        bool publish_result) const {
    if(tools.empty()||tools.size()!=tool_history_indices.size())
        throw std::invalid_argument(
            "Subtract block requires one history index per tool");
    if(!publish_result&&source_fingerprint.empty())
        throw std::invalid_argument(
            "A deferred Subtract block requires a live source fingerprint");
    const auto block_geometry=[&](const BodyResult& body) {
        if(!body.source_fingerprint.empty()) {
            const auto native=live_cache_->boundaries.find(
                body.source_fingerprint);
            if(native!=live_cache_->boundaries.end()&&
               native->second.topology) {
                PrimitiveData geometry;
                geometry.shape=native->second.shape;
                geometry.faces=native->second.topology->faces;
                geometry.edges=native->second.topology->edges;
                geometry.vertices=native->second.topology->vertices;
                return place_bound_geometry(std::move(geometry),body);
            }
        }
        return placed_bound_face_geometry(body,true);
    };
    if((input.kernel_shape.empty()&&
        (input.source_fingerprint.empty()||
         !live_cache_->boundaries.contains(input.source_fingerprint)))||
       input.kernel_shape==kernel_body_compound)
        throw std::invalid_argument(
            "Subtract block requires one calculated input body");
    if(!std::isfinite(boolean_tolerance)||boolean_tolerance<=0.||
       !std::isfinite(mesh_deflection)||mesh_deflection<=0.)
        throw std::invalid_argument(
            "Subtract block tolerances are invalid");

    std::map<std::string,std::size_t> owner_history;
    for(std::size_t index=0;index<history_owners.size();++index)
        owner_history.try_emplace(history_owners[index],index);
    const auto history_of=[&](const auto& reference,
            std::size_t fallback) {
        const auto found=owner_history.find(reference.owner_id);
        return found==owner_history.end()?fallback:found->second;
    };
    const auto ordered_unique=[&](auto& references,
            std::size_t fallback) {
        std::ranges::sort(references,[&](const auto& left,
                const auto& right) {
            return std::tuple{history_of(left,fallback),
                       encoded_topology_reference(left)}<
                std::tuple{history_of(right,fallback),
                       encoded_topology_reference(right)};
        });
        const auto duplicate=std::ranges::unique(references);
        references.erase(duplicate.begin(),duplicate.end());
    };

    auto target=block_geometry(input);
    TopTools_ListOfShape arguments,cut_shapes;
    arguments.Append(target.shape);
    std::vector<PrimitiveData> operands;
    operands.reserve(tools.size());
    for(const auto& tool:tools) {
        if((tool.kernel_shape.empty()&&
            (tool.source_fingerprint.empty()||
             !live_cache_->boundaries.contains(
                 tool.source_fingerprint)))||
           tool.kernel_shape==kernel_body_compound)
            throw std::invalid_argument(
                "Subtract block tool has no standalone calculated solid");
        operands.push_back(block_geometry(tool));
        cut_shapes.Append(operands.back().shape);
    }
    BRepAlgoAPI_BooleanOperation cut;
    cut.SetOperation(BOPAlgo_CUT);
    cut.SetArguments(arguments);
    cut.SetTools(cut_shapes);
    cut.SetRunParallel(true);
    cut.SetToFillHistory(true);
    cut.SetFuzzyValue(std::max(1.0e-7,boolean_tolerance));
    cut.Build();
    if(!cut.IsDone()||cut.Shape().IsNull()||
       !BRepCheck_Analyzer(cut.Shape(),true,true,false).IsValid())
        throw std::runtime_error("OCCT subtract block failed");

    std::vector<OwnedFace> tool_faces;
    std::vector<OwnedEdge> tool_edges;
    std::vector<OwnedVertex> tool_vertices;
    for(const auto& operand:operands) {
        tool_faces.insert(tool_faces.end(),operand.faces.begin(),
            operand.faces.end());
        tool_edges.insert(tool_edges.end(),operand.edges.begin(),
            operand.edges.end());
        tool_vertices.insert(tool_vertices.end(),operand.vertices.begin(),
            operand.vertices.end());
    }
    const auto propagated_faces=propagate_topology(cut,target.faces,
        tool_faces);
    const auto propagated_edges=propagate_topology(cut,target.edges,
        tool_edges);
    const auto propagated_vertices=propagate_topology(cut,target.vertices,
        tool_vertices);

    OcctBoundaryUnion boundary;
    boundary.shape=cut.Shape();
    TopTools_IndexedMapOfShape actual_faces,actual_edges,actual_vertices;
    TopExp::MapShapes(boundary.shape,TopAbs_FACE,actual_faces);
    TopExp::MapShapes(boundary.shape,TopAbs_EDGE,actual_edges);
    TopExp::MapShapes(boundary.shape,TopAbs_VERTEX,actual_vertices);
    boundary.faces.reserve(static_cast<std::size_t>(actual_faces.Extent()));
    for(int index=1;index<=actual_faces.Extent();++index) {
        std::vector<FaceReference> references;
        for(const auto& entry:propagated_faces)
            if(entry.shape.IsSame(actual_faces(index))&&
               entry.reference.valid())
                references.push_back(entry.reference);
        ordered_unique(references,0);
        if(references.empty())
            throw std::runtime_error(
                "Subtract block face has no persisted ancestry");
        std::size_t history=history_of(references.front(),0);
        for(const auto& reference:references)
            history=std::min(history,history_of(reference,history));
        boundary.faces.push_back({TopoDS::Face(actual_faces(index)),
            std::move(references),history});
    }
    boundary.edges.reserve(static_cast<std::size_t>(actual_edges.Extent()));
    for(int index=1;index<=actual_edges.Extent();++index) {
        std::vector<EdgeReference> references;
        for(const auto& entry:propagated_edges)
            if(entry.shape.IsSame(actual_edges(index))&&
               entry.reference.valid())
                references.push_back(entry.reference);
        ordered_unique(references,0);
        std::size_t history=std::numeric_limits<std::size_t>::max();
        for(const auto& reference:references)
            history=std::min(history,history_of(reference,history));
        boundary.edges.push_back({TopoDS::Edge(actual_edges(index)),
            std::move(references),history});
    }
    boundary.vertices.reserve(
        static_cast<std::size_t>(actual_vertices.Extent()));
    for(int index=1;index<=actual_vertices.Extent();++index) {
        std::vector<VertexReference> references;
        for(const auto& entry:propagated_vertices)
            if(entry.shape.IsSame(actual_vertices(index))&&
               entry.reference.valid())
                references.push_back(entry.reference);
        ordered_unique(references,0);
        std::size_t history=std::numeric_limits<std::size_t>::max();
        for(const auto& reference:references)
            history=std::min(history,history_of(reference,history));
        boundary.vertices.push_back({
            TopoDS::Vertex(actual_vertices(index)),
            std::move(references),history});
    }

    const auto named=occt_boundary_named_topology(boundary,history_owners,
        std::max(1.0e-7,boolean_tolerance));
    std::vector<OwnedFace> faces;
    std::vector<OwnedEdge> edges;
    std::vector<OwnedVertex> vertices;
    faces.reserve(named.faces.size());
    edges.reserve(named.edges.size());
    vertices.reserve(named.vertices.size());
    for(const auto& entry:named.faces)
        faces.push_back({entry.shape,entry.reference});
    for(const auto& entry:named.edges)
        edges.push_back({entry.shape,entry.reference});
    for(const auto& entry:named.vertices)
        vertices.push_back({entry.shape,entry.reference});
    const auto live_topology=[&] {
        auto topology=std::make_shared<LiveCache::Topology>(
            LiveCache::Topology{faces,edges,vertices,{}});
        const auto append_aliases=[](const auto& metadata,
                auto& owned) {
            for(const auto& item:metadata) {
                const auto canonical=std::ranges::find_if(owned,
                    [&](const auto& value) {
                        return value.shape.IsSame(item.shape);
                    });
                if(canonical==owned.end())
                    throw std::runtime_error(
                        "Boundary topology alias has no canonical shape");
                const auto canonical_reference=canonical->reference;
                for(const auto& reference:item.references)
                    if(reference.valid()&&
                       reference!=canonical_reference)
                        owned.push_back({item.shape,reference});
            }
        };
        append_aliases(boundary.faces,topology->faces);
        append_aliases(boundary.edges,topology->edges);
        append_aliases(boundary.vertices,topology->vertices);
        return topology;
    };
    BodyResult result;
    if(publish_result) {
        const bool calculate_properties=
            aggregate_property_demand==AggregatePropertyDemand::All||
            aggregate_property_demand==AggregatePropertyDemand::Final||
            aggregate_property_demand==AggregatePropertyDemand::DeferredAdaptive;
        result=make_result(boundary.shape,faces,edges,vertices,false,true,
            false,{},mesh_deflection,calculate_properties);
    } else {
        // See calculate_add_block(): this is a calculated native boundary,
        // not a measured empty Body.
        result.physical_properties_current=false;
        result.solid_present=TopExp_Explorer(
            boundary.shape,TopAbs_SOLID).More();
    }
    result.resolved_up_to_limits=input.resolved_up_to_limits;
    for(const auto& tool:tools)
        result.resolved_up_to_limits.insert(
            result.resolved_up_to_limits.end(),
            tool.resolved_up_to_limits.begin(),
            tool.resolved_up_to_limits.end());
    result.source_fingerprint=source_fingerprint;
    if(!source_fingerprint.empty()) {
        auto references=input.mesh.original_references;
        for(const auto& tool:tools)
            append_reference_geometry(references,
                tool.mesh.original_references);
        append_reference_geometry(references,
            result.mesh.original_references);
        result.mesh.original_references=references;
        live_cache_->boundaries.insert_or_assign(source_fingerprint,
            LiveCache::Boundary{boundary.shape,live_topology(),
                nullptr,nullptr,false});
        std::erase(live_cache_->insertion_order,source_fingerprint);
        live_cache_->insertion_order.push_back(source_fingerprint);
        live_cache_->copy_references.insert_or_assign(
            source_fingerprint,std::move(references));
        std::erase(live_cache_->copy_reference_order,
            source_fingerprint);
        live_cache_->copy_reference_order.push_back(source_fingerprint);
        while(live_cache_->insertion_order.size()>
                kLiveShapeCacheLimit) {
            live_cache_->boundaries.erase(
                live_cache_->insertion_order.front());
            live_cache_->insertion_order.pop_front();
        }
        while(live_cache_->copy_reference_order.size()>
                kLiveShapeCacheLimit) {
            live_cache_->copy_references.erase(
                live_cache_->copy_reference_order.front());
            live_cache_->copy_reference_order.pop_front();
        }
    }
    return result;
}

BodyResult OcctKernel::calculate_feature_geometry(HistoryOperation operation,
        const BodyResult& references,KernelRigidPlacement frame_to_document,const BodyResult& local_input,
        const std::vector<OrderedUpToPatch>& external_sources,bool wire_only) const {
    if(!std::holds_alternative<FeatureGroupRequest>(operation.primitive))
        throw std::invalid_argument("Invalid Feature definition.");
    try {
        HistoryContext context;context.preview_wire=wire_only;
        const auto& group=std::get<FeatureGroupRequest>(operation.primitive);
        const bool through_all=std::ranges::any_of(group.children,[](const auto& child) {
            const auto* extrusion=std::get_if<ExtrusionRequest>(&child);
            return extrusion&&(extrusion->extent==ExtrusionRequest::Extent::ThroughAll||extrusion->through_all_reverse);
        });
        if(through_all) {
            const auto native=live_cache_->boundaries.find(
                local_input.source_fingerprint);
            if(native!=live_cache_->boundaries.end())
                context.preview_input=native->second.shape;
            else if(!local_input.kernel_shape.empty())
                context.preview_input=read_kernel_shape(local_input);
        }
        auto selected=extrusion_references({operation});
        for(const auto& child:group.children)if(const auto* rotation=std::get_if<RevolutionRequest>(&child))
            if(rotation->end_limit&&!rotation->end_limit->datum)selected.push_back(rotation->end_limit->reference);
        const auto inverse=primitive_transform(frame_to_document.translation,frame_to_document.rotation_degrees).Inverted();
        std::vector<FaceReference> consumed;
        struct PreparedSource {NativeFaceSource source;PrimitiveData geometry;};
        std::vector<PreparedSource> prepared;
        for(const auto& reference:selected) {
            if(std::ranges::any_of(consumed,[&](const auto& item){return same_face_identity(item,reference);}))continue;
            consumed.push_back(reference);
            const auto supplied=std::ranges::find_if(external_sources,[&](const auto& patch){
                return patch.native_source&&same_face_identity(patch.target.reference,reference);
            });
            const auto native=supplied==external_sources.end()?native_face_source(references,reference):
                native_face_source(supplied->native_source->body.get(),supplied->native_source->reference);
            auto cached=std::ranges::find_if(prepared,[&](const auto& entry){
                return entry.source.source==native.source&&entry.source.placed_source==native.placed_source;
            });
            if(cached==prepared.end()) {prepared.push_back({native,native.geometry(*this)});cached=std::prev(prepared.end());}
            const auto& geometry=cached->geometry;
            const auto& source_reference=supplied==external_sources.end()?reference:supplied->native_source->reference;
            for(const auto& face:geometry.faces)if(same_face_identity(face.reference,source_reference)) {
                auto shape=face.shape;
                if(supplied!=external_sources.end())for(const auto& placement:supplied->native_source->placements)
                    shape=TopoDS::Face(BRepBuilderAPI_Transform(shape,primitive_transform(placement.translation,placement.rotation_degrees),true).Shape());
                shape=TopoDS::Face(BRepBuilderAPI_Transform(shape,inverse,true).Shape());
                context.external_faces.push_back({shape,reference});
            }
        }
        if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))std::fprintf(stderr,"Feature native sources: %zu preparations for %zu distinct targets\n",prepared.size(),consumed.size());
        // This private operation now owns the prepared, placed targets above.
        // Let ordered clipping consume those targets instead of decoding the
        // same Assembly source again through its borrowed native_source path.
        for(auto& child:std::get<FeatureGroupRequest>(operation.primitive).children)
            if(auto* extrusion=std::get_if<ExtrusionRequest>(&child);extrusion&&extrusion->ordered_end_limit)
                for(auto& patch:extrusion->ordered_end_limit->patches)
                    if(patch.native_source&&std::ranges::any_of(context.external_faces,[&](const auto& face){
                            return same_face_identity(face.reference,patch.target.reference);}))patch.native_source.reset();
        operation.body={};operation.operation=BooleanOperation::Add;
        operation.suppressed=false;operation.feature_copy.reset();
        ExtrusionRequest source_key;source_key.ordered_end_limit.emplace();source_key.ordered_end_limit->patches=external_sources;
        HistoryOperation frame_key{"feature-frame",std::move(source_key)};frame_key.body.id="feature-frame";frame_key.body.translation=frame_to_document.translation;
        frame_key.body.rotation_degrees=frame_to_document.rotation_degrees;
        context.dependency_key=std::string(wire_only?":preview:":":feature-tool:")+references.source_fingerprint+":"+
            local_input.source_fingerprint+":"+history_fingerprint({frame_key},1);
        auto result=evaluate_flat_history({operation},{},context);
        if(result.size()!=1)throw std::runtime_error("Invalid Feature definition.");
        return std::move(result.front());
    }catch(const Standard_Failure& error){throw std::runtime_error(error.GetMessageString());}
}

ViewerReferenceGeometry OcctKernel::intersect_face_plane(
        const BodyResult& body,const FacePlaneIntersectionRequest& request) const {
    struct QueryTiming {
        bool enabled{std::getenv("ZIMA_CPP_PROFILE_COMMAND")!=nullptr};
        std::chrono::steady_clock::time_point start{enabled?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{}};
        ~QueryTiming() {
            if(enabled)std::fprintf(stderr,"Sketch face-plane query: %.3f ms\n",
                std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        }
    } timing;
    const auto finite=[](Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
    if(!request.face.valid()||request.plane_owner_id.empty()||
        !finite(request.origin)||!finite(request.normal)||std::hypot(request.normal.x,request.normal.y,request.normal.z)<=1e-12||
        !std::isfinite(request.tolerance)||request.tolerance<=0.)
        throw std::invalid_argument("Intersection face reference is invalid.");
    auto input=native_face_source(body,request.face).geometry(*this);
    auto selected_face=request.face;
    if(!selected_face.instance_path.empty()) {
        // A calculated copy already owns its exact transformed native shapes.
        // Localize only this private query's selected occurrence metadata so
        // the shared two-surface algorithm retains its ordinary local contract.
        const auto localize=[&](auto& values) {
            std::erase_if(values,[&](const auto& value){return value.reference.instance_path!=selected_face.instance_path;});
            for(auto& value:values)value.reference.instance_path.clear();
        };
        localize(input.faces);localize(input.edges);localize(input.vertices);
        selected_face.instance_path.clear();
    }
    const FaceReference plane{request.plane_owner_id,"sketch-plane",{}};
    const auto support=BRepBuilderAPI_MakeFace(gp_Pln(
        gp_Pnt(request.origin.x,request.origin.y,request.origin.z),
        gp_Dir(request.normal.x,request.normal.y,request.normal.z))).Face();
    input.faces.push_back({support,plane});
    SurfaceIntersectionRequest intersection{{selected_face,plane},request.tolerance};
    const auto curves=make_surface_intersection_data(intersection,request.plane_owner_id,input.faces,input.edges,input.vertices,true,request.reference_branches);
    if(curves.edges.empty()&&curves.vertices.empty())return {};
    auto result=make_result(curves.shape,{},curves.edges,curves.vertices,true,false,true,{},0.,false);
    std::erase_if(result.mesh.original_references.points,[](const auto& point){return !point.reference.valid();});
    return std::move(result.mesh.original_references);
}

BodyResult OcctKernel::import_iges(const std::string& path,
        const std::string& owner_id, double mesh_deflection) const {
    if (owner_id.empty() || !std::isfinite(mesh_deflection) || mesh_deflection <= 0)
        throw std::invalid_argument("Invalid IGES import parameters");
    IGESControl_Reader reader;
    if (reader.ReadFile(path.c_str()) != IFSelect_RetDone || reader.TransferRoots() == 0)
        throw std::runtime_error("OCCT IGES import failed");
    PrimitiveData data; data.shape = reader.OneShape();
    if (data.shape.IsNull() || !BRepCheck_Analyzer(data.shape).IsValid())
        throw std::runtime_error("IGES did not produce valid geometry");
    const auto model = reader.IGESModel();
    const auto transfer = reader.WS()->TransferReader()->TransientProcess();
    // Identity is anchored in the source IGES directory entry, never in OCCT
    // traversal order. A split child is distinguished by its persisted locator.
    const auto capture = [&](auto& owned, TopAbs_ShapeEnum kind,
            StepRequest::TopologyIdentity::Kind identity_kind, const char* role) {
        struct Candidate { TopoDS_Shape shape; int source{}; int specificity{}; std::string locator; };
        std::map<std::string, Candidate> candidates;
        TopTools_IndexedMapOfShape actual; TopExp::MapShapes(data.shape, kind, actual);
        // One immutable runtime shape may be reached through many IGES source
        // parents. Compute its exact locator once, including collision counting.
        std::vector<std::optional<std::string>> locators(static_cast<std::size_t>(actual.Size())+1);
        const auto locator_for=[&](int index)->const std::string& {
            auto& cached=locators[static_cast<std::size_t>(index)];
            if(!cached)cached=step_shape_locator(actual.FindKey(index));
            return *cached;
        };
        for (int i = 1; i <= model->NbEntities(); ++i) {
            const auto entity = model->Value(i);
            const auto source_shape = TransferBRep::ShapeResult(transfer, entity);
            if (source_shape.IsNull()) continue;
            TopTools_IndexedMapOfShape children; TopExp::MapShapes(source_shape, kind, children);
            for (int j = 1; j <= children.Size(); ++j) {
                const auto runtime_index = actual.FindIndex(children.FindKey(j));
                if (runtime_index == 0) continue;
                const auto& child = actual.FindKey(runtime_index);
                const auto& locator = locator_for(runtime_index);
                const int source = 2 * model->Number(entity) - 1; // IGES directory pointer
                const int specificity = children.Size();
                const auto found = candidates.find(locator);
                if (found == candidates.end() || specificity < found->second.specificity ||
                    (specificity == found->second.specificity && source < found->second.source)) {
                    // Shared edges/vertices choose the lowest source directory
                    // pointer among equally specific parents, a source-file rule.
                    candidates[locator] = {child, source, specificity, locator};
                }
            }
        }
        std::map<std::string, int> counts;
        for (int i = 1; i <= actual.Size(); ++i) ++counts[locator_for(i)];
        for (const auto& [locator, candidate] : candidates) {
            if (counts[locator] != 1) continue;
            auto key = std::string("iges:") + role + ":de:" + std::to_string(candidate.source);
            if (candidate.specificity > 1) key += ":child:" + locator;
            owned.push_back({candidate.shape, {owner_id, key, {}}});
            data.imported_step_topology.push_back({identity_kind, key, locator});
        }
    };
    using Kind = StepRequest::TopologyIdentity::Kind;
    capture(data.faces, TopAbs_FACE, Kind::Face, "face");
    capture(data.edges, TopAbs_EDGE, Kind::Edge, "edge");
    capture(data.vertices, TopAbs_VERTEX, Kind::Vertex, "vertex");
    auto result = make_result(data.shape, data.faces, data.edges, data.vertices,
        false, true, !data.imported_step_topology.empty(), {}, mesh_deflection);
    persist_imported_topology(data, result);
    return result;
}

std::vector<BodyResult> OcctKernel::import_step_components(
    const std::vector<StepRequest>& requests, double mesh_deflection) const {
    if (!std::isfinite(mesh_deflection) || mesh_deflection <= 0)
        throw std::invalid_argument("Invalid STEP mesh deflection");
    std::unordered_map<std::string, StepDocumentCache> documents;
    std::vector<BodyResult> results;
    results.reserve(requests.size());
    for (std::size_t index = 0; index < requests.size(); ++index) {
        const std::string owner = requests[index].reference_owner_id.empty()
            ? "step-import:" + std::to_string(index)
            : requests[index].reference_owner_id;
        const auto data = make_step_data(requests[index], owner, documents);
        auto result = make_result(data.shape, data.faces, data.edges, data.vertices,
            false, true,
            !data.faces.empty() || !data.edges.empty() || !data.vertices.empty(), {}, mesh_deflection);
        persist_imported_topology(data, result);
        if (!requests[index].live_cache_fingerprint.empty()) {
            result.source_fingerprint = requests[index].live_cache_fingerprint;
            live_cache_->boundaries.insert_or_assign(
                requests[index].live_cache_fingerprint,
                LiveCache::Boundary{data.shape,
                    std::make_shared<LiveCache::Topology>(LiveCache::Topology{
                        data.faces, data.edges, data.vertices, {}})});
            live_cache_->insertion_order.push_back(
                requests[index].live_cache_fingerprint);
            while (live_cache_->insertion_order.size() > kLiveShapeCacheLimit) {
                live_cache_->boundaries.erase(
                    live_cache_->insertion_order.front());
                live_cache_->insertion_order.pop_front();
            }
        }
        results.push_back(std::move(result));
    }
    return results;
}

std::vector<FrozenStepComponent> OcctKernel::freeze_step_components(
    const std::vector<StepRequest>& requests,const StepSourceDocument* source) const {
    std::unordered_map<std::string, StepDocumentCache> documents;
    std::vector<FrozenStepComponent> results;
    results.reserve(requests.size());
    for(std::size_t index=0;index<requests.size();++index) {
        const auto& request=requests[index];
        const std::string owner=request.reference_owner_id.empty()
            ? "step-import:"+std::to_string(index):request.reference_owner_id;
        const auto data=make_step_data(request,owner,documents,source);
        BodyResult archive;
        persist_imported_topology(data,archive);
        results.push_back({archive.kernel_shape.shared_data(),
            std::move(archive.imported_step_topology)});
    }
    return results;
}

BodyResult OcctKernel::compound_bodies(
    const std::vector<PlacedBody>& bodies) const {
    const auto shape = placed_compound(bodies);
    auto result = make_result(shape, {}, {}, {});
    result.source_fingerprint = "compound";
    for (const auto& body : bodies) {
        result.source_fingerprint += ":" + body.body.source_fingerprint;
    }
    return result;
}

BodyResult OcctKernel::subtract_bodies(
    const BodyResult& target,
    const BodyResult& cutter,
    Vec3 target_translation,
    Vec3 target_rotation_degrees, double linear_tolerance, double mesh_deflection) const {
    const bool trace=std::getenv("ZIMA_CPP_PROFILE_COMMAND")!=nullptr;
    auto phase_stamp=trace?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    const auto phase=[&](const char* name) {
        if(!trace)return;
        const auto now=std::chrono::steady_clock::now();
        std::fprintf(stderr,"Assembly cut %s: %.3f ms\n",name,
            std::chrono::duration<double,std::milli>(now-phase_stamp).count());
        phase_stamp=now;
    };
    if(!std::isfinite(linear_tolerance)||linear_tolerance<=0||!std::isfinite(mesh_deflection)||mesh_deflection<=0)
        throw std::invalid_argument("Invalid assembly cut precision");
    if (target.kernel_shape.empty() || cutter.kernel_shape.empty()) {
        throw std::invalid_argument(
            "Assembly cut requires calculated solid snapshots");
    }
    const auto target_shape = read_kernel_shape(target);
    const auto cutter_shape = read_kernel_shape(cutter);
    phase("read-shapes");
    const gp_Trsf placement = primitive_transform(
        target_translation, target_rotation_degrees);
    BRepBuilderAPI_Transform placed_target(target_shape, placement, true);
    placed_target.Build();
    if (!placed_target.IsDone()) {
        throw std::runtime_error("Assembly cut target placement failed");
    }
    phase("place-target");
    BRepAlgoAPI_Cut cut;
    set_boolean_inputs(cut, placed_target.Shape(), cutter_shape);
    cut.SetFuzzyValue(linear_tolerance);
    cut.Build();
    phase("boolean");
    const bool basic_result=cut.IsDone()&&!cut.Shape().IsNull();
    const bool valid=basic_result&&BRepCheck_Analyzer(cut.Shape()).IsValid();
    phase("validate");
    if (!valid) {
        throw std::runtime_error("Assembly cut failed or produced an invalid solid");
    }
    BRepBuilderAPI_Transform local_result(cut.Shape(), placement.Inverted(), true);
    local_result.Build();
    if (!local_result.IsDone() || local_result.Shape().IsNull()) {
        throw std::runtime_error("Assembly cut result placement failed");
    }
    phase("restore-placement");
    auto result = make_result(local_result.Shape(), {}, {}, {}, false, true, false, {}, mesh_deflection);
    phase("packet");
    // Stable references remain owned by the original component objects. The
    // boolean result topology itself is deliberately not a reference owner.
    result.mesh.original_references = target.mesh.original_references;
    result.source_fingerprint = target.source_fingerprint + ":cut:" +
        cutter.source_fingerprint;
    return result;
}

void OcctKernel::export_step(
    const std::vector<PlacedBody>& bodies, const std::string& path) const {
    STEPControl_Writer writer;
    if (writer.Transfer(placed_compound(bodies), STEPControl_AsIs) !=
            IFSelect_RetDone ||
        writer.Write(path.c_str()) != IFSelect_RetDone) {
        throw std::runtime_error("STEP export failed");
    }
}

void OcctKernel::export_step(const StepProduct& root, const std::string& path) const {
    Handle(TDocStd_Document) document;
    const auto application = XCAFApp_Application::GetApplication();
    application->NewDocument("BinXCAF", document);
    try {
        XCAFDoc_DocumentTool::SetLengthUnit(document, 0.001); // ZIMA geometry is in mm.
        const auto shapes = XCAFDoc_DocumentTool::ShapeTool(document->Main());
        std::map<std::string, TDF_Label> definitions;
        std::set<std::string> visiting;
        const auto name = [](const TDF_Label& label, const std::string& text) {
            TDataStd_Name::Set(label, TCollection_ExtendedString(text.c_str(), true));
        };
        const auto add = [&](auto&& self, const StepProduct& product) -> TDF_Label {
            if (product.definition_id.empty()) throw std::invalid_argument("STEP product identity is empty");
            if (visiting.contains(product.definition_id)) throw std::invalid_argument("Cyclic STEP product structure");
            if (const auto found=definitions.find(product.definition_id);found!=definitions.end())return found->second;
            visiting.insert(product.definition_id);
            TDF_Label label;
            if (product.children.empty()) {
                if(product.body.kernel_shape.empty())throw std::runtime_error("STEP product has no calculated geometry");
                label=shapes->AddShape(read_kernel_shape(product.body),false);
            } else {
                label=shapes->NewShape();
                for(const auto& child:product.children) {
                    const auto definition=self(self,child);
                    const auto occurrence=shapes->AddComponent(label,definition,
                        TopLoc_Location(primitive_transform(child.translation,child.rotation_degrees)));
                    name(occurrence,child.name);
                }
            }
            name(label,product.name);definitions.emplace(product.definition_id,label);
            visiting.erase(product.definition_id);return label;
        };
        static_cast<void>(add(add,root));shapes->UpdateAssemblies();
        STEPCAFControl_Writer writer;writer.SetNameMode(true);
        if(!writer.Transfer(document,STEPControl_AsIs)||writer.Write(path.c_str())!=IFSelect_RetDone)
            throw std::runtime_error("STEP product export failed");
        application->Close(document);
    } catch(const Standard_Failure& failure) {
        application->Close(document);throw std::runtime_error(failure.GetMessageString());
    } catch(...) { application->Close(document);throw; }
}

namespace {
void write_iges_shape(const TopoDS_Shape& shape,const std::string& path) {
    IGESControl_Writer writer("MM",1);
    if(shape.IsNull()||!writer.AddShape(shape))throw std::runtime_error("IGES export failed");
    std::ofstream output(std::filesystem::u8path(path),std::ios::binary);
    if(!output||!writer.Write(output))throw std::runtime_error("IGES export failed");
    output.close();if(!output)throw std::runtime_error("IGES export failed");
}
}
void OcctKernel::export_iges(const std::vector<PlacedBody>& bodies,const std::string& path) const {
    write_iges_shape(placed_compound(bodies),path);
}
void OcctKernel::export_iges(const StepProduct& root, const std::string& path) const {
    // Consume the same captured visible products as STEP. Compose full rigid
    // transforms before flattening IGES geometry; never reopen dependencies.
    TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);
    std::set<std::string> visiting;bool populated=false;
    const auto add=[&](auto&& self,const StepProduct& product,const gp_Trsf& parent,std::size_t depth)->void {
        if(depth>256||!visiting.insert(product.definition_id).second)
            throw std::runtime_error("The component dependency graph is too large or too deep.");
        const auto world=parent*primitive_transform(product.translation,product.rotation_degrees);
        if(product.children.empty()) {
            if(product.body.kernel_shape.empty())
                throw std::runtime_error("An exported component has no calculated body; invoke Regenerate first.");
            BRepBuilderAPI_Transform placed(read_kernel_shape(product.body),world,false);
            placed.Build();if(!placed.IsDone()||placed.Shape().IsNull())throw std::runtime_error("IGES export failed");
            builder.Add(compound,placed.Shape());populated=true;
        }else for(const auto& child:product.children)self(self,child,world,depth+1);
        visiting.erase(product.definition_id);
    };
    add(add,root,gp_Trsf{},0);
    if(!populated)throw std::runtime_error("There are no visible calculated bodies to export.");
    // The stream overload supports native Unicode filesystem paths on Windows
    // and Linux without changing document geometry or process-wide units.
    write_iges_shape(compound,path);
}

void OcctKernel::export_stl(
    const std::vector<PlacedBody>& bodies, const std::string& path) const {
    auto shape = placed_compound(bodies);
    BRepMesh_IncrementalMesh mesher(shape, 0.1, false, 0.5, true);
    mesher.Perform();
    if (!mesher.IsDone()) {
        throw std::runtime_error("STL triangulation failed");
    }
    StlAPI_Writer writer;
    writer.ASCIIMode() = false;
#ifdef _WIN32
    // OCCT's filename overload opens a narrow std::ofstream on Windows.
    // Open the UTF-8 path through filesystem instead and use its stream API.
    std::ofstream output(std::filesystem::u8path(path), std::ios::binary);
    if (!output || !writer.Write(shape, output)) {
        throw std::runtime_error("STL export failed");
    }
    output.close();
    if (!output) throw std::runtime_error("STL export failed");
#else
    // POSIX filenames are UTF-8 bytes. OCCT 7.9 exposes the filename API only.
    if (!writer.Write(shape, path.c_str())) {
        throw std::runtime_error("STL export failed");
    }
#endif
}

std::vector<BodyResult> OcctKernel::evaluate_history(
    const std::vector<HistoryOperation>& operations) const {
    return evaluate_history_incremental(operations, {});
}

namespace {

std::vector<BodyCopySource> authored_copy_sources(
        const BodyHistoryScope& scope) {
    if(!scope.copy_sources.empty())return scope.copy_sources;
    if(scope.source_id.empty()&&scope.source_feature_id.empty())return {};
    return {{scope.source_id,scope.source_feature_id,
        BooleanOperation::Add}};
}

std::vector<std::string> authored_copy_feature_ids(
        const BodyHistoryScope& scope) {
    std::vector<std::string> result;
    for(const auto& source:authored_copy_sources(scope))
        if(!source.feature_id.empty())result.push_back(source.feature_id);
    return result;
}

void place_body_result(BodyResult& result, const gp_Trsf& placement) {
    // Archive addresses refer to the original local BRep only. A placed
    // viewer packet must not advertise bindings for rewritten geometry.
    result.kernel_faces.clear();result.kernel_edges.clear();result.kernel_vertices.clear();
    if(result.volume_integrals) {
        auto& p=*result.volume_integrals;
        const auto c=gp_Pnt(p.centroid.x,p.centroid.y,p.centroid.z).Transformed(placement);
        p.centroid={c.X(),c.Y(),c.Z()};Matrix3 r;
        for(int i=0;i<3;++i)for(int j=0;j<3;++j)r[3*i+j]=placement.Value(i+1,j+1);
        p.inertia=inertia_rotate(p.inertia,r);
    }
    auto& mesh = result.mesh;
    const auto point = [&](Vec3& value) {
        const auto p = gp_Pnt(value.x, value.y, value.z).Transformed(placement);
        value = {p.X(), p.Y(), p.Z()};
    };
    if(result.surface_centroid)point(*result.surface_centroid);
    const auto direction = [&](Vec3& value) {
        const auto v = gp_Vec(value.x, value.y, value.z).Transformed(placement);
        value = {v.X(), v.Y(), v.Z()};
    };
    // Triangles/side records share exact analytic surface inputs. Transform
    // each once for this rigid placement and retain the original key's lifetime.
    std::unordered_map<const SurfaceGeometry*,std::pair<
        std::shared_ptr<const SurfaceGeometry>,std::shared_ptr<const SurfaceGeometry>>> placed_surfaces;
    const auto face = [&](FaceReference& reference) {
        for(auto* target:{&reference.surface,&reference.contour_surface})if(*target) {
        const auto original=*target;
        if(const auto cached=placed_surfaces.find(original.get());cached!=placed_surfaces.end()) {
            *target=cached->second.second;continue;
        }
        auto surface = std::make_shared<SurfaceGeometry>(*original);
        point(surface->origin);
        direction(surface->axis);
        direction(surface->radial);
        *target = surface;
        placed_surfaces.emplace(original.get(),std::make_pair(original,std::move(surface)));
        }
    };
    const auto geometry = [&](auto& value) {
        for (auto& vertex : value.vertices) point(vertex);
        for (auto& reference : value.triangle_references) face(reference);
        for (auto& edge : value.edges) {
            for (auto& vertex : edge.points) point(vertex);
            if (edge.exact_spline) for (auto& p : edge.exact_spline->poles) point(p);
            for (auto& row : edge.edge_treatment_side_directions)
                for (auto& vector : row) direction(vector);
            for (auto& reference : edge.edge_treatment_side_references) face(reference);
        }
        for (auto& vertex : value.points) point(vertex.position);
        for (auto& axis : value.axes) {
            point(axis.point);
            direction(axis.direction);
        }
    };
    geometry(mesh);
    geometry(mesh.original_references);
    for(auto& [owner,packet]:result.solid_state_reference_views) {
        auto placed=std::make_shared<ViewerReferenceGeometry>(*packet);
        geometry(*placed);packet=std::move(placed);
    }
    for (auto& reference : result.shaft_thread_references) face(reference);
    for (auto& dimension : mesh.dimensions) {
        point(dimension.witness_first);
        point(dimension.witness_second);
        point(dimension.line_first);
        point(dimension.line_second);
        direction(dimension.plane_normal);
        if(dimension.measurement_direction)direction(*dimension.measurement_direction);
        if (dimension.label_position) point(*dimension.label_position);
    }
    for (auto& marker : mesh.constraint_markers) point(marker.position);
}

void append_body_viewer(ViewerMesh& destination, const ViewerMesh& source) {
    const auto offset = static_cast<std::uint32_t>(destination.vertices.size());
    destination.vertices.insert(destination.vertices.end(), source.vertices.begin(), source.vertices.end());
    destination.triangles.reserve(destination.triangles.size()+source.triangles.size());
    if(offset==0)destination.triangles.insert(destination.triangles.end(),source.triangles.begin(),source.triangles.end());
    else for (const auto index : source.triangles) destination.triangles.push_back(offset + index);
    destination.triangle_references.insert(destination.triangle_references.end(), source.triangle_references.begin(), source.triangle_references.end());
    destination.edges.insert(destination.edges.end(), source.edges.begin(), source.edges.end());
    destination.points.insert(destination.points.end(), source.points.begin(), source.points.end());
    destination.axes.insert(destination.axes.end(), source.axes.begin(), source.axes.end());
    destination.dimensions.insert(destination.dimensions.end(), source.dimensions.begin(), source.dimensions.end());
    destination.constraint_markers.insert(destination.constraint_markers.end(), source.constraint_markers.begin(), source.constraint_markers.end());
    append_reference_geometry(destination.original_references, source.original_references);
}

// Use the same reference derivation as the visible packet for every archived
// topology kind. A metadata-only packet keeps this bounded to owned identities;
// do not copy the complete viewer tessellation for a native binding transform.
template<class Transform,class Remap>
PrimitiveData transformed_bound_topology(const PrimitiveData& source,Transform& transform,Remap remap) {
    ViewerMesh metadata;
    metadata.triangle_references.reserve(source.faces.size());
    metadata.edges.reserve(source.edges.size());metadata.points.reserve(source.vertices.size());
    for(const auto& face:source.faces)metadata.triangle_references.push_back(face.reference);
    for(const auto& edge:source.edges){ViewerEdge value;value.reference=edge.reference;metadata.edges.push_back(std::move(value));}
    for(const auto& vertex:source.vertices)metadata.points.push_back({{},vertex.reference});
    const auto mapped=remap(std::move(metadata));
    PrimitiveData result;result.shape=transform.Shape();
    result.faces.reserve(source.faces.size());result.edges.reserve(source.edges.size());result.vertices.reserve(source.vertices.size());
    for(std::size_t i=0;i<source.faces.size();++i)
        result.faces.push_back({transform.ModifiedShape(source.faces[i].shape),mapped.triangle_references.at(i)});
    for(std::size_t i=0;i<source.edges.size();++i)
        result.edges.push_back({transform.ModifiedShape(source.edges[i].shape),mapped.edges.at(i).reference});
    for(std::size_t i=0;i<source.vertices.size();++i)
        result.vertices.push_back({transform.ModifiedShape(source.vertices[i].shape),mapped.points.at(i).reference});
    return result;
}

} // namespace

BodyResult OcctKernel::scale_body(const BodyResult& source,double factor,Vec3 center,const std::string& owner) const {
    validate_body_scale(factor,center);
    if(source.kernel_shape.empty())throw std::invalid_argument("The scale source has no calculated Body.");
    gp_Trsf scaling;scaling.SetScale(gp_Pnt(center.x,center.y,center.z),factor);
    const auto bindings=source.kernel_faces.empty()?std::optional<PrimitiveData>{}:placed_bound_face_geometry(source,true);
    BRepBuilderAPI_Transform transform(bindings?bindings->shape:read_kernel_shape(source),scaling,true);transform.Build();
    if(!transform.IsDone()||!BRepCheck_Analyzer(transform.Shape()).IsValid())
        throw std::runtime_error("Body scale did not produce valid geometry.");
    BodyResult result;result.kernel_shape=serialize_kernel_shape(transform.Shape());
    result.physical_properties_current=source.physical_properties_current;result.solid_present=source.has_solid();
    result.mesh=scaled_body_mesh(source.mesh,factor,center,owner);
    if(bindings) {
        const auto changed=transformed_bound_topology(*bindings,transform,[&](auto mesh){return scaled_body_mesh(std::move(mesh),factor,center,owner);});
        persist_surface_topology(changed.shape,changed.faces,changed.edges,changed.vertices,result);
    }
    if(!source.measurement_sources.empty()) {
        std::vector<BodySnapshot> references;
        for(const auto& snapshot:source.measurement_sources)references.emplace_back(scale_body(snapshot,factor,center,owner));
        result.measurement_sources=std::move(references);
    }
    result.volume=source.volume*factor*factor*factor;result.surface_area=source.surface_area*factor*factor;
    if(source.surface_centroid)result.surface_centroid=scaled_body_point(*source.surface_centroid,factor,center);
    if(source.volume_integrals) {
        result.volume_integrals=source.volume_integrals;
        result.volume_integrals->centroid=scaled_body_point(result.volume_integrals->centroid,factor,center);
        for(auto& value:result.volume_integrals->inertia)value*=std::pow(factor,5);
    }
    HistoryOperation key;key.owner_id=owner;key.body.id=owner;key.body.combination=BodyCombination::Scale;
    key.body.scale_factor=factor;key.body.scale_center=center;
    result.source_fingerprint=source.source_fingerprint+":scale:"+history_fingerprint({key},1);
    return result;
}

BodyResult OcctKernel::mirror_body(const BodyResult& source,MirrorPlane plane,const std::string& owner_id,
    Vec3 translation,Vec3 rotation) const {
    plane=normalized_mirror_plane(plane);
    if(source.kernel_shape.empty())throw std::invalid_argument("Zdroj Zrcadla nemá vypočtené těleso.");
    auto result=source;result.body_boundaries.clear();result.body_inputs.clear();result.body_outputs.clear();
    const auto placement=primitive_transform(translation,rotation);
    place_body_result(result,placement);
    gp_Trsf reflection;reflection.SetMirror(gp_Ax2(gp_Pnt(plane.point.x,plane.point.y,plane.point.z),gp_Dir(plane.normal.x,plane.normal.y,plane.normal.z)));
    const auto bindings=source.kernel_faces.empty()?std::optional<PrimitiveData>{}:placed_bound_face_geometry(source,true);
    BRepBuilderAPI_Transform transform(bindings?bindings->shape:read_kernel_shape(source),reflection*placement,true);transform.Build();
    if(!transform.IsDone()||!BRepCheck_Analyzer(transform.Shape()).IsValid())throw std::runtime_error("Zrcadlo nevytvořilo platné těleso.");
    result.kernel_shape=serialize_kernel_shape(transform.Shape());
    result.kernel_placements.clear();result.kernel_compound_bodies.clear();
    result.mesh=mirrored_viewer_mesh(std::move(result.mesh),plane,owner_id);
    if(bindings) {
        const auto changed=transformed_bound_topology(*bindings,transform,[&](auto mesh){return mirrored_viewer_mesh(std::move(mesh),plane,owner_id);});
        persist_surface_topology(changed.shape,changed.faces,changed.edges,changed.vertices,result);
    }
    if(!source.measurement_sources.empty()) {
        std::vector<BodySnapshot> references;
        for(const auto& snapshot:source.measurement_sources)references.emplace_back(mirror_body(snapshot,plane,owner_id,translation,rotation));
        result.measurement_sources=std::move(references);
    }
    if(result.surface_centroid) {
        const auto c=*result.surface_centroid;
        const auto p=gp_Pnt(c.x,c.y,c.z).Transformed(reflection);
        result.surface_centroid=Vec3{p.X(),p.Y(),p.Z()};
    }
    if(result.volume_integrals) {
        auto& p=*result.volume_integrals;const auto c=gp_Pnt(p.centroid.x,p.centroid.y,p.centroid.z).Transformed(reflection);
        p.centroid={c.X(),c.Y(),c.Z()};Matrix3 r;
        for(int i=0;i<3;++i)for(int j=0;j<3;++j)r[3*i+j]=reflection.Value(i+1,j+1);
        p.inertia=inertia_rotate(p.inertia,r);
    }
    result.shaft_thread_owner.clear();result.shaft_thread_references={};result.imported_step_topology.clear();
    HistoryOperation key;key.owner_id=owner_id;key.body.combination=BodyCombination::Mirror;key.body.mirror_plane=plane;
    key.body.translation=translation;key.body.rotation_degrees=rotation;
    result.source_fingerprint=source.source_fingerprint+":mirror:"+history_fingerprint({key},1);
    return result;
}

BodyResult OcctKernel::pattern_body(const BodyResult& source,const PatternRequest& request,const std::string& owner,
    Vec3 translation,Vec3 rotation,bool occurrences) const {
    const auto p=validated_pattern(request);
    if(source.kernel_shape.empty())throw std::invalid_argument("Zdroj Pole nemá vypočtené těleso.");
    const auto placement=primitive_transform(translation,rotation);auto input=source;place_body_result(input,placement);
    BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);BodyResult result;
    result.physical_properties_current=source.physical_properties_current;result.solid_present=source.has_solid();
    const auto bindings=source.kernel_faces.empty()?std::optional<PrimitiveData>{}:placed_bound_face_geometry(source,true);
    const auto shape=bindings?bindings->shape:read_kernel_shape(source);
    std::vector<OwnedFace> copy_faces;std::vector<OwnedEdge> copy_edges;std::vector<OwnedVertex> copy_vertices;
    for(unsigned index=1;index<p.count;++index) {
        gp_Trsf copy;
        if(p.circular)copy.SetRotation(gp_Ax1(gp_Pnt(p.origin.x,p.origin.y,p.origin.z),gp_Dir(p.axis.x,p.axis.y,p.axis.z)),p.angle_degrees*index*std::numbers::pi/180.0);
        else {const auto t=pattern_translation(p,index);copy.SetTranslation(gp_Vec(t.x,t.y,t.z));}
        BRepBuilderAPI_Transform transform(shape,copy*placement,true);transform.Build();
        if(!transform.IsDone()||!BRepCheck_Analyzer(transform.Shape()).IsValid())throw std::runtime_error("Pole nevytvořilo platné těleso.");
        builder.Add(compound,transform.Shape());
        append_body_viewer(result.mesh,pattern_copy_mesh(input.mesh,p,index,owner,occurrences));
        if(bindings) {
            auto changed=transformed_bound_topology(*bindings,transform,[&](auto mesh){return pattern_copy_mesh(std::move(mesh),p,index,owner,occurrences);});
            copy_faces.insert(copy_faces.end(),std::make_move_iterator(changed.faces.begin()),std::make_move_iterator(changed.faces.end()));
            copy_edges.insert(copy_edges.end(),std::make_move_iterator(changed.edges.begin()),std::make_move_iterator(changed.edges.end()));
            copy_vertices.insert(copy_vertices.end(),std::make_move_iterator(changed.vertices.begin()),std::make_move_iterator(changed.vertices.end()));
        }
        if(input.surface_centroid) {
            const auto c=*input.surface_centroid;
            const auto placed=gp_Pnt(c.x,c.y,c.z).Transformed(copy);
            if(!result.surface_centroid)result.surface_centroid=Vec3{};
            auto& sum=*result.surface_centroid;
            sum.x+=placed.X()/(p.count-1);sum.y+=placed.Y()/(p.count-1);sum.z+=placed.Z()/(p.count-1);
        }
    }
    result.kernel_shape=serialize_kernel_shape(compound);result.volume=source.volume*(p.count-1);result.surface_area=source.surface_area*(p.count-1);
    if(bindings)persist_surface_topology(compound,copy_faces,copy_edges,copy_vertices,result);
    if(!source.measurement_sources.empty()) {
        std::vector<BodySnapshot> references;
        for(const auto& snapshot:source.measurement_sources)references.emplace_back(pattern_body(snapshot,p,owner,translation,rotation,occurrences));
        result.measurement_sources=std::move(references);
    }
    // Rigid copies preserve the exact source integrals, including adaptive
    // integration of spline surfaces; only their coordinate frames change.
    if(source.volume_integrals) {
        std::vector<VolumeIntegrals> copies;Vec3 center{};
        for(unsigned index=1;index<p.count;++index) {
            gp_Trsf copy;
            if(p.circular)copy.SetRotation(gp_Ax1(gp_Pnt(p.origin.x,p.origin.y,p.origin.z),gp_Dir(p.axis.x,p.axis.y,p.axis.z)),p.angle_degrees*index*std::numbers::pi/180.0);
            else {const auto t=pattern_translation(p,index);copy.SetTranslation(gp_Vec(t.x,t.y,t.z));}
            BodyResult integral;integral.volume_integrals=input.volume_integrals;place_body_result(integral,copy);
            copies.push_back(*integral.volume_integrals);const auto c=copies.back().centroid;center.x+=c.x;center.y+=c.y;center.z+=c.z;
        }
        center.x/=copies.size();center.y/=copies.size();center.z/=copies.size();VolumeIntegrals total;total.centroid=center;
        for(const auto& value:copies) {
            for(int i=0;i<9;++i)total.inertia[i]+=value.inertia[i];
            inertia_shift(total.inertia,std::abs(source.volume),{value.centroid.x-center.x,value.centroid.y-center.y,value.centroid.z-center.z});
        }result.volume_integrals=total;
    }
    HistoryOperation key;key.owner_id=owner;key.body.id=owner;key.body.combination=BodyCombination::Pattern;key.body.pattern=p;
    key.body.translation=translation;key.body.rotation_degrees=rotation;
    result.source_fingerprint=source.source_fingerprint+":pattern:"+history_fingerprint({key},1);return result;
}

std::vector<BodyResult> OcctKernel::evaluate_history_recovering(
    const std::vector<HistoryOperation>& operations,
    const std::vector<BodyResult>& previous_boundaries,
    const HistoryOutputPolicy& output_policy) const {
    if (operations.empty()) return {};
    if (std::ranges::any_of(operations, [](const auto& operation) {
            return !(operation.body == BodyHistoryScope{});
        })) {
        try {
            return live_cache_->mark_current(evaluate_body_histories(
                    operations, previous_boundaries, true, output_policy),
                previous_boundaries.empty()||has_current_calculation(previous_boundaries));
        } catch (const Standard_Failure& failure) {
            throw std::runtime_error(failure.GetMessageString());
        }
    }
    HistoryContext context;
    context.final_boundary_first=output_policy.final_boundary_first;
    for(std::size_t index=0;index<operations.size();++index)
        if(output_policy.retained_output_owners.contains(
                operations[index].owner_id)) {
            context.retained_output_boundaries.insert(index+1);
            context.full_output_boundaries.insert(index+1);
        }
    for(std::size_t index=1;index<operations.size();++index)
        if(output_policy.input_boundary_consumers.contains(
                operations[index].owner_id)) {
            context.retained_output_boundaries.insert(index);
            context.edge_output_boundaries.insert(index);
        }
    for(std::size_t index=0;index<operations.size();++index)
        if(const auto requested=output_policy.reference_output_owners.find(
                operations[index].owner_id);
                requested!=output_policy.reference_output_owners.end()) {
            context.retained_output_boundaries.insert(index+1);
            context.reference_output_boundaries.emplace(index+1,
                requested->second);
        }
    return live_cache_->mark_current(evaluate_flat_history_recovering(operations,previous_boundaries,context),
        previous_boundaries.empty()||has_current_calculation(previous_boundaries));
}

std::vector<BodyResult> OcctKernel::evaluate_flat_history_recovering(
    const std::vector<HistoryOperation>& operations,
    const std::vector<BodyResult>& previous_boundaries,HistoryContext& context) const {
    try {
        return evaluate_flat_history(operations, previous_boundaries,context);
    } catch (const std::exception&) {
        // Locate the failing boundary only on the error path. Strict prefix
        // evaluation finalizes persisted reference packets just as normal OK does.
    }
    std::vector<BodyResult> valid;
    std::shared_ptr<const OriginalFaces> valid_originals;
    // An error in the changed suffix does not invalidate its unchanged input.
    // Finalize that prefix once through the ordinary evaluator (including all
    // ancestry/copy-source guards), then locate failures only after it. Walking
    // every earlier boundary separately rebuilt known valid history on each
    // transient reference-settling failure.
    std::size_t unchanged{};
    while(unchanged<std::min(operations.size(),previous_boundaries.size())&&
          previous_boundaries[unchanged].calculation_errors.empty()&&
          previous_boundaries[unchanged].source_fingerprint==history_fingerprint(operations,unchanged+1)+context.dependency_key)
        ++unchanged;
    if(unchanged>1)try {
        const std::vector<HistoryOperation> prefix(operations.begin(),operations.begin()+static_cast<std::ptrdiff_t>(unchanged));
        valid=evaluate_flat_history(prefix,previous_boundaries,context);
        if(!valid.back().mesh.triangles.empty()&&valid.back().kernel_shape.empty())
            valid=evaluate_flat_history(prefix,{},context);
        valid_originals=context.original_faces;
        if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))std::cerr<<"[history recovery] unchanged_prefix="<<valid.size()<<"\n";
    }catch(const std::exception&){valid.clear();}
    auto executable=operations;
    std::map<std::size_t,std::string> failed;
    std::map<std::size_t,OrderedUpToDiagnostic> ordered_failures;
    const auto publish_recovery_state=[&](std::vector<BodyResult>& values) {
        for(std::size_t boundary=0;boundary<values.size();++boundary) {
            values[boundary].source_fingerprint=
                history_fingerprint(operations,boundary+1)+
                context.dependency_key;
            values[boundary].calculation_errors.clear();
            values[boundary].ordered_up_to_failures.clear();
            for(const auto& [failure_index,message]:failed)
                if(failure_index<=boundary) {
                    values[boundary].calculation_errors[
                        operations[failure_index].owner_id]=message;
                    if(const auto ordered=ordered_failures.find(
                            failure_index);ordered!=ordered_failures.end())
                        values[boundary].ordered_up_to_failures[
                            operations[failure_index].owner_id]=
                                ordered->second;
                }
        }
    };
    for (std::size_t index = valid.size(); index < operations.size(); ++index) {
        const std::vector<HistoryOperation> prefix(executable.begin(),
            executable.begin() + static_cast<std::ptrdiff_t>(index + 1));
        try {
            const std::vector<BodyResult> no_reuse;
            const auto& reuse=failed.empty()
                ?(valid.empty()?previous_boundaries:valid):no_reuse;
            valid = evaluate_flat_history(prefix,reuse,context);
            // Ordinary intermediate caches may contain viewer data only. The
            // retained end of a failed history must also persist a real solid.
            if (!valid.back().mesh.triangles.empty() && valid.back().kernel_shape.empty())
                valid = evaluate_flat_history(prefix, {},context);
            valid_originals=context.original_faces;
            publish_recovery_state(valid);
        } catch (const std::exception& error) {
            context.original_faces=valid_originals;
            auto input = valid.empty() ? BodyResult{} : valid.back();
            if(const auto* ordered=dynamic_cast<const OrderedUpToError*>(&error))
                ordered_failures[index]={ordered->failure,ordered->row,
                    ordered->other_row};
            failed[index]=error.what();
            // A failed definition remains an authored red history node, but
            // it is a no-op for material flow. Later independent containers
            // therefore continue from the last valid Body. Any container that
            // actually references this failed output receives its own input
            // error and is retained red in the same way.
            executable[index].suppressed=true;
            input.source_fingerprint=history_fingerprint(operations,index+1)+
                context.dependency_key;
            input.calculation_errors[operations[index].owner_id]=error.what();
            if(const auto ordered=ordered_failures.find(index);
                    ordered!=ordered_failures.end())
                input.ordered_up_to_failures[operations[index].owner_id]=
                    ordered->second;
            valid.push_back(std::move(input));
            publish_recovery_state(valid);
        }
    }
    return valid;
}

std::vector<BodyResult> OcctKernel::evaluate_body_histories(
    const std::vector<HistoryOperation>& operations,
    const std::vector<BodyResult>& previous_boundaries, bool recover_errors,
    const HistoryOutputPolicy& output_policy) const {
    auto profile_start=std::chrono::steady_clock::now();
    const auto profile=[&](const char* phase) {
        const auto now=std::chrono::steady_clock::now();
        if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))std::fprintf(stderr,"Body kernel %s: %.3f ms\n",phase,
            std::chrono::duration<double,std::milli>(now-profile_start).count());
        profile_start=now;
    };
    struct Branch {
        BodyHistoryScope scope;
        std::vector<HistoryOperation> operations;
    };
    std::vector<Branch> branches;
    std::set<std::string> seen;
    std::set<std::string> owners;
    std::set<std::string> available;
    // Validate the entire graph before calculating any branch. A consumed
    // inputs are replaced by the Boolean step's own result; neither can
    // be used a second time implicitly.
    for (std::size_t index = 0; index < operations.size(); ++index) {
        const auto& operation = operations[index];
        const auto& scope = operation.body;
        if (scope.combination != BodyCombination::Separate &&
            scope.combination != BodyCombination::Add &&
            scope.combination != BodyCombination::Subtract &&
            scope.combination != BodyCombination::Intersect &&
            scope.combination != BodyCombination::Mirror && scope.combination != BodyCombination::Pattern && scope.combination != BodyCombination::Scale)
            throw std::invalid_argument("Invalid body combination");
        if (operation.owner_id.empty() || !owners.insert(operation.owner_id).second)
            throw std::invalid_argument("Body features require distinct persistent owners");
        if (scope.id.empty()) throw std::invalid_argument("Body history has no owner");
        for (const auto value : {scope.translation.x, scope.translation.y, scope.translation.z,
                scope.rotation_degrees.x, scope.rotation_degrees.y, scope.rotation_degrees.z})
            if (!std::isfinite(value)) throw std::invalid_argument("Body placement must be finite");
        if (branches.empty() || branches.back().scope.id != scope.id) {
            if (!seen.insert(scope.id).second)
                throw std::invalid_argument("Body histories must be contiguous");
            if (scope.combination == BodyCombination::Separate) {
                if (!scope.target_id.empty() || !scope.source_id.empty()||
                        !scope.copy_sources.empty())
                    throw std::invalid_argument("Independent body cannot define a Boolean");
                if(scope.linked_body&&(operation.suppressed||scope.linked_body->kernel_shape.empty()||
                    !scope.linked_body->calculation_errors.empty()))
                    throw std::invalid_argument("The linked Body source has no valid calculated geometry.");
            } else if(scope.combination==BodyCombination::Scale) {
                if(!available.contains(scope.source_id)||!scope.target_id.empty()||!scope.source_feature_id.empty()||!scope.copy_sources.empty()||operation.suppressed||
                    scope.translation!=Vec3{}||scope.rotation_degrees!=Vec3{})
                    throw std::invalid_argument("A scaled Body requires a preceding source and cannot own a modeling history or placement.");
                validate_body_scale(scope.scale_factor,scope.scale_center);
            } else if(scope.combination==BodyCombination::Mirror||scope.combination==BodyCombination::Pattern) {
                const auto sources=authored_copy_sources(scope);
                if(sources.empty()||std::ranges::any_of(sources,[&](const auto& source) {
                        return source.body_id.empty()||
                            (source.feature_id.empty()?
                                !available.contains(source.body_id):
                                !seen.contains(source.body_id));
                    })||(!scope.target_id.empty()&&
                        (sources.size()!=1||sources.front().feature_id.empty()||
                         !available.contains(scope.target_id)))||operation.suppressed)
                    throw std::invalid_argument("Zrcadlo potřebuje dostupný předcházející zdroj.");
                if(!scope.target_id.empty())available.erase(scope.target_id);
                if(scope.combination==BodyCombination::Mirror)static_cast<void>(normalized_mirror_plane(scope.mirror_plane));
                else static_cast<void>(validated_pattern(scope.pattern));
            } else {
                if (scope.target_id == scope.source_id || !available.erase(scope.target_id) || !available.erase(scope.source_id))
                    throw std::invalid_argument("Boolean requires two distinct available preceding results");
                if (operation.suppressed || operation.operation != BooleanOperation::Add ||
                    !(scope.translation == Vec3{}) || !(scope.rotation_degrees == Vec3{}))
                    throw std::invalid_argument("Boolean step cannot own body placement or feature modifiers");
            }
            available.insert(scope.id);
            branches.push_back({scope, {}});
        } else if (!(branches.back().scope == scope) ||
                !authored_copy_sources(scope).empty() || scope.linked_body) {
            throw std::invalid_argument("Inconsistent body history scope");
        }
        if(const auto* shell=std::get_if<ShellRequest>(&operation.primitive);shell&&!shell->source_body.empty())
            if(shell->thickness!=0.||shell->source_body==scope.id||!available.contains(shell->source_body)||!seen.contains(shell->source_body))
                throw std::invalid_argument("Invalid Shell parameters");
        auto local = operation;
        local.body = {};
        branches.back().operations.push_back(std::move(local));
    }
    const auto analysis_current=[](auto&& self,const BodyResult& body)->bool {
        if(!body.physical_properties_current)return false;
        for(const auto& [id,values]:body.body_boundaries)for(const auto& value:values)
            if(!self(self,value))return false;
        for(const auto* snapshots:{&body.body_inputs,&body.body_outputs})for(const auto& [id,value]:*snapshots)
            if(!self(self,value.get()))return false;
        return true;
    };
    const auto exact_reference_outputs_current=[&] {
        if(output_policy.reference_output_owners.empty())return true;
        if(previous_boundaries.empty())return false;
        const auto& previous=previous_boundaries.back();
        for(const auto& [owner,requested]:
                output_policy.reference_output_owners) {
            bool covered=false;
            for(const auto& branch:branches) {
                const auto operation=std::ranges::find(
                    branch.operations,owner,&HistoryOperation::owner_id);
                if(operation==branch.operations.end())continue;
                const auto cached=previous.body_boundaries.find(
                    branch.scope.id);
                const auto boundary=static_cast<std::size_t>(
                    operation-branch.operations.begin());
                covered=cached!=previous.body_boundaries.end()&&
                    cached->second.size()>boundary&&std::includes(
                        cached->second[boundary].history_reference_coverage.
                            begin(),
                        cached->second[boundary].history_reference_coverage.
                            end(),
                        requested.begin(),requested.end());
                break;
            }
            if(!covered)return false;
        }
        return true;
    };
    if (previous_boundaries.size() == operations.size() &&
        previous_boundaries.back().calculation_errors.empty() &&
        previous_boundaries.back().source_fingerprint == history_fingerprint(operations, operations.size()) &&
        exact_reference_outputs_current() &&
        ((aggregate_property_demand==AggregatePropertyDemand::Deferred||
            aggregate_property_demand==AggregatePropertyDemand::DeferredAdaptive) ||
            (aggregate_property_demand==AggregatePropertyDemand::Final&&previous_boundaries.back().physical_properties_current&&
                std::ranges::all_of(previous_boundaries.back().body_outputs,[](const auto& item){return item.second->physical_properties_current;})) ||
            (aggregate_property_demand==AggregatePropertyDemand::All&&std::ranges::all_of(previous_boundaries,
                [&](const auto& boundary){return analysis_current(analysis_current,boundary);}))) &&
        previous_boundaries.back().body_boundaries.size() == static_cast<std::size_t>(
            std::ranges::count_if(branches, [](const auto& branch) {
                return authored_copy_sources(branch.scope).empty();
            }))) {
        if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))
            std::cerr<<"[history graph reuse] operations="<<operations.size()<<" reused="<<operations.size()<<" calculated=0\n";
        return previous_boundaries;
    }
    const BodyResult empty;
    const auto& previous = previous_boundaries.empty() ? empty : previous_boundaries.back();
    BodyResult document;
    std::vector<BodyResult> boundaries;
    boundaries.reserve(operations.size());
    const auto requested_faces=extrusion_references(operations);
    std::vector<OwnedFace> preceding_faces;
    std::unordered_map<std::string,std::string> source_keys;
    std::map<std::string,BodyResult> source_solids;
    std::map<std::string,PrimitiveData> shell_sources;
    std::map<std::string,std::string> shell_source_keys;
    std::set<std::string> requested_shell_bodies;
    for(const auto& operation:operations)if(!operation.suppressed)
        if(const auto* shell=std::get_if<ShellRequest>(&operation.primitive);shell&&!shell->source_body.empty())requested_shell_bodies.insert(shell->source_body);
    profile("graph preparation");
    for (const auto& branch : branches) {
        const auto cached = previous.body_boundaries.find(branch.scope.id);
        const auto placement=primitive_transform(branch.scope.translation,branch.scope.rotation_degrees);
        HistoryContext context;
        context.final_boundary_first=output_policy.final_boundary_first;
        for(std::size_t index=0;index<branch.operations.size();++index)
            if(output_policy.retained_output_owners.contains(
                    branch.operations[index].owner_id)) {
                context.retained_output_boundaries.insert(index+1);
                context.full_output_boundaries.insert(index+1);
            }
        for(std::size_t index=1;index<branch.operations.size();++index)
            if(output_policy.input_boundary_consumers.contains(
                    branch.operations[index].owner_id)) {
                context.retained_output_boundaries.insert(index);
                context.edge_output_boundaries.insert(index);
            }
        for(std::size_t index=0;index<branch.operations.size();++index)
            if(const auto requested=output_policy.reference_output_owners.find(
                    branch.operations[index].owner_id);
                    requested!=output_policy.reference_output_owners.end()) {
                context.retained_output_boundaries.insert(index+1);
                context.reference_output_boundaries.emplace(index+1,
                    requested->second);
            }
        for(const auto& copy:branches)
            for(const auto& source:authored_copy_sources(copy.scope))
                if(source.body_id==branch.scope.id&&!source.feature_id.empty()) {
                    if(std::ranges::none_of(branch.operations,
                            [&](const auto& operation) {
                                return operation.owner_id==source.feature_id;
                            }))
                        throw std::invalid_argument(
                            "Copy solid does not belong to its source Body.");
                    context.requested_solids.insert(source.feature_id);
                }
        const auto branch_references=extrusion_references(branch.operations);
        for(const auto& face:preceding_faces) {
            if(!std::ranges::any_of(branch_references,[&](const auto& reference){return same_face_identity(face.reference,reference);}))continue;
            context.external_faces.push_back({face.shape.Moved(TopLoc_Location(placement.Inverted())),face.reference});
        }
        std::set<std::string> dependencies;
        for(const auto& reference:branch_references)if(source_keys.contains(reference.owner_id))dependencies.insert(reference.owner_id);
        for(const auto& owner:dependencies)context.dependency_key+=":source:"+owner+":"+source_keys.at(owner);
        for(const auto& operation:branch.operations)if(!operation.suppressed)
            if(const auto* shell=std::get_if<ShellRequest>(&operation.primitive);shell&&!shell->source_body.empty()) {
                const auto found=shell_sources.find(shell->source_body);
                if(found==shell_sources.end())throw std::runtime_error("Shell requires an input body");
                context.external_shell_bodies.emplace(shell->source_body,sheet_form_geometry::move(found->second,placement.Inverted()));
                dependencies.insert(shell->source_body);
                context.dependency_key+=":shell-source:"+shell->source_body+":"+shell_source_keys.at(shell->source_body);
            }
        HistoryOperation frame_key;frame_key.body=branch.scope;
        if(!dependencies.empty()) {
            context.dependency_key+=":frame:"+history_fingerprint({frame_key},1);
            context.dependency_key=":references:"+dependency_fingerprint(context.dependency_key);
        }
        context.require_original_faces=requested_shell_bodies.contains(branch.scope.id)||std::ranges::any_of(requested_faces,[&](const auto& reference) {
            return std::ranges::any_of(branch.operations,[&](const auto& operation){return operation.owner_id==reference.owner_id;});
        });
        const std::vector<BodyResult> empty_local;
        const auto& cached_local=cached==previous.body_boundaries.end()?empty_local:cached->second.get();
        // Pure import histories have no requested live kernel operands. Their
        // complete unchanged local calculation can stay immutable during a
        // Body move; consumers that need original faces/solids still use the
        // established evaluator and its live-operand validation.
        bool reuse_import_history=!branch.scope.linked_body&&branch.scope.source_id.empty()&&
            !context.require_original_faces&&context.requested_solids.empty()&&context.dependency_key.empty()&&
            cached_local.size()==branch.operations.size()&&
            std::ranges::all_of(branch.operations,[](const auto& operation) {
                return std::holds_alternative<StepRequest>(operation.primitive)&&!operation.feature_copy;
            });
        if(reuse_import_history) {
            const auto keys=history_fingerprints(branch.operations);
            for(std::size_t i=0;i<cached_local.size();++i)
                if(!cached_local[i].calculation_errors.empty()||cached_local[i].source_fingerprint!=keys[i+1]) {
                    reuse_import_history=false;break;
                }
        }
        ImmutableVector<BodyResult> local;
        const auto copy_sources=authored_copy_sources(branch.scope);
        if(reuse_import_history) local=cached->second;
        else if(branch.scope.linked_body) local={*branch.scope.linked_body};
        else if(copy_sources.empty()&&branch.scope.source_id.empty())
            local=recover_errors ? evaluate_flat_history_recovering(branch.operations,cached_local,context)
                : evaluate_flat_history(branch.operations,cached_local,context);
        else if(!copy_sources.empty()) {
            std::vector<PlacedBody> inputs;
            for(const auto& source:copy_sources) {
                const auto* value=source.feature_id.empty()?
                    &document.body_outputs.at(source.body_id).get():
                    source_solids.contains(source.feature_id)?
                        &source_solids.at(source.feature_id):nullptr;
                if(!value)throw std::runtime_error(
                    "Copy source has no calculated solid operand.");
                inputs.push_back({*value});
            }
            local={inputs.size()==1?inputs.front().body:
                compound_bodies(inputs)};
        } else local={document.body_outputs.at(branch.scope.source_id)};
        profile("local history");
        for(auto& [id,solid]:context.source_solids) {
            if(!(branch.scope.translation==Vec3{})||!(branch.scope.rotation_degrees==Vec3{})) {
                BRepBuilderAPI_Transform transform(read_kernel_shape(solid),placement,true);transform.Build();
                if(!transform.IsDone())throw std::runtime_error("Source solid placement failed");
                solid.kernel_shape=serialize_kernel_shape(transform.Shape());
                solid.kernel_placements.clear();solid.kernel_compound_bodies.clear();
                place_body_result(solid,placement);
            }
            solid.calculation_errors=local.back().calculation_errors;
            source_solids.emplace(id,std::move(solid));
        }
        if(context.require_original_faces) {
            const auto source_key=local.back().source_fingerprint+":placed:"+history_fingerprint({frame_key},1);
            for(const auto& operation:branch.operations)source_keys[operation.owner_id]=source_key;
            for(auto node=context.original_faces;node;node=node->previous)for(const auto& face:node->faces) {
                if(std::ranges::any_of(requested_faces,[&](const auto& reference){return same_face_identity(face.reference,reference);}))
                    preceding_faces.push_back({face.shape.Moved(TopLoc_Location(placement)),face.reference});
            }
        }
        if(requested_shell_bodies.contains(branch.scope.id)&&
            (local.back().kernel_faces.empty()||local.back().kernel_edges.empty()||local.back().kernel_vertices.empty())&&local.back().calculation_errors.empty()) {
            const auto native=live_cache_->boundaries.find(local.back().source_fingerprint);
            // A linked skin and its retained solid must be independently usable
            // after reopening, including when embedded into a FORM definition.
            // Capture existing ZIMA identities at explicit calculation only.
            if(native!=live_cache_->boundaries.end()&&!native->second.shape.IsNull()) {
                auto captured=local.get();
                persist_surface_topology(native->second.shape,native->second.topology->faces,
                    native->second.topology->edges,native->second.topology->vertices,captured.back());
                local=std::move(captured);
            }
        }
        if(!local.empty()&&((aggregate_property_demand==AggregatePropertyDemand::All&&
                std::ranges::any_of(local,[](const auto& boundary){return !boundary.physical_properties_current;})) ||
                (aggregate_property_demand==AggregatePropertyDemand::Final&&!local.back().physical_properties_current))) {
            auto analyzed=local.get();
            if(aggregate_property_demand==AggregatePropertyDemand::All) {
                for(auto& boundary:analyzed)ensure_body_properties(boundary);
            } else ensure_body_properties(analyzed.back());
            local=std::move(analyzed);
        }
        if (copy_sources.empty()&&branch.scope.source_id.empty())
            document.body_boundaries.emplace(branch.scope.id, local);
        HistoryOperation placement_key;
        placement_key.body = branch.scope;
        // Combination changes do not invalidate the raw placed branch.
        placement_key.body.combination = BodyCombination::Separate;
        placement_key.body.result_suppressed = false;
        placement_key.body.target_id.clear();
        const auto input_key = copy_sources.empty()&&branch.scope.source_id.empty()
            ? local.back().source_fingerprint + ":placed:" + history_fingerprint({placement_key}, 1)
            : local.back().source_fingerprint;
        BodySnapshot input_snapshot;
        const auto old_input = previous.body_inputs.find(branch.scope.id);
        if (old_input != previous.body_inputs.end() && old_input->second->calculation_errors.empty() &&
            local.back().calculation_errors.empty() && old_input->second->source_fingerprint == input_key &&
            (aggregate_property_demand==AggregatePropertyDemand::Deferred||
                aggregate_property_demand==AggregatePropertyDemand::DeferredAdaptive||
                old_input->second->physical_properties_current)) {
            input_snapshot = old_input->second;
        } else {
            auto placed = local.back();
            // A shortened history may end on a previously intermediate packet
            // without an archive. Pending analysis must retain its real solid,
            // rather than publish an empty compound with visible geometry.
            if(placed.kernel_shape.empty()&&(placed.has_solid()||!placed.mesh.triangles.empty())) {
                const auto native=live_cache_->boundaries.find(local.back().source_fingerprint);
                if(native!=live_cache_->boundaries.end()&&!native->second.shape.IsNull()) {
                    if(native->second.topology&&!native->second.topology->faces.empty())
                        persist_surface_topology(native->second.shape,native->second.topology->faces,
                            native->second.topology->edges,native->second.topology->vertices,placed);
                    else placed.kernel_shape=serialize_kernel_shape(native->second.shape);
                }
            }
            if (!(branch.scope.translation == Vec3{}) || !(branch.scope.rotation_degrees == Vec3{})) {
                const auto placement = primitive_transform(branch.scope.translation, branch.scope.rotation_degrees);
                if (!placed.kernel_shape.empty()) {
                    placed.kernel_placements.push_back({branch.scope.translation, branch.scope.rotation_degrees});
                }
                const auto face_bindings=placed.kernel_faces;
                const auto edge_bindings=placed.kernel_edges;
                const auto vertex_bindings=placed.kernel_vertices;
                place_body_result(placed, placement);
                // This path appends a rigid placement and leaves the archived
                // BRep unchanged. Keep every exact local topology address for
                // cold source queries; no binding is used to solve placement.
                placed.kernel_faces=face_bindings;placed.kernel_edges=edge_bindings;
                placed.kernel_vertices=vertex_bindings;
            }
            placed.source_fingerprint = input_key;
            input_snapshot = BodySnapshot(std::move(placed));
        }
        const auto& input = input_snapshot.get();
        if(requested_shell_bodies.contains(branch.scope.id)) {
            const auto native=live_cache_->boundaries.find(local.back().source_fingerprint);
            PrimitiveData original;
            if(native!=live_cache_->boundaries.end()&&local.back().calculation_errors.empty())
                original={native->second.shape,native->second.topology->faces,native->second.topology->edges,native->second.topology->vertices};
            // A failed source is an invalid operand for the dependent feature.
            // Let the ordinary recovering evaluator retain that definition and
            // report its error rather than aborting the complete Part graph.
            shell_sources.emplace(branch.scope.id,sheet_form_geometry::move(original,placement));
            shell_source_keys.emplace(branch.scope.id,input_key);
        }
        profile("placed input");
        if (copy_sources.empty()&&branch.scope.source_id.empty())
            document.body_inputs.emplace(branch.scope.id, input_snapshot);
        if (copy_sources.empty()&&branch.scope.source_id.empty())
            document.solid_state_reference_views.insert(input.solid_state_reference_views.begin(),input.solid_state_reference_views.end());
        // A Separate Body's placed input is its output. Both maps retain the
        // same immutable packet; Boolean/copy results replace only the output.
        auto output = input_snapshot;
        const auto preserve_failed_inputs = [&](const std::string& message) {
            auto retained = input;
            if(!copy_sources.empty()&&copy_sources.size()==1&&
                    !copy_sources.front().feature_id.empty()&&
                    !branch.scope.target_id.empty())
                retained=document.body_outputs.at(branch.scope.target_id);
            else if (!branch.scope.target_id.empty()) {
                const auto& target = document.body_outputs.at(branch.scope.target_id).get();
                retained = compound_bodies({PlacedBody{target}, PlacedBody{input}});
                retained.calculation_errors = input.calculation_errors;
                retained.calculation_errors.insert(target.calculation_errors.begin(), target.calculation_errors.end());
            }
            retained.calculation_errors[branch.operations.front().owner_id] = message;
            return retained;
        };
        try {
        if ((!copy_sources.empty()||!branch.scope.source_id.empty()) &&
                !input.calculation_errors.empty())
            throw std::runtime_error("Zdrojové těleso obsahuje nevypočítaný prvek.");
        if (!branch.scope.target_id.empty() &&
            !document.body_outputs.at(branch.scope.target_id)->calculation_errors.empty())
            throw std::runtime_error("Cílové těleso obsahuje nevypočítaný prvek.");
        if(branch.scope.combination==BodyCombination::Scale) {
            const auto& source=document.body_outputs.at(branch.scope.source_id).get();
            HistoryOperation key;key.owner_id=branch.scope.id;key.body.id=branch.scope.id;
            key.body.combination=BodyCombination::Scale;key.body.scale_factor=branch.scope.scale_factor;key.body.scale_center=branch.scope.scale_center;
            const auto fingerprint=source.source_fingerprint+":scale:"+history_fingerprint({key},1);
            const auto old=previous.body_outputs.find(branch.scope.id);
            if(old!=previous.body_outputs.end()&&old->second->calculation_errors.empty()&&old->second->source_fingerprint==fingerprint)output=old->second;
            else output=scale_body(source,branch.scope.scale_factor,branch.scope.scale_center,branch.scope.id);
            document.body_inputs.emplace(branch.scope.id,output);
        } else if(branch.scope.combination==BodyCombination::Mirror||branch.scope.combination==BodyCombination::Pattern) {
            // The source is already in document coordinates; only the plane
            // belongs to the Mirror container's resolved placement.
            std::vector<PlacedBody> inputs;
            for(const auto& definition:copy_sources) {
                const auto* source=definition.feature_id.empty()?
                    &document.body_outputs.at(definition.body_id).get():
                    source_solids.contains(definition.feature_id)?
                        &source_solids.at(definition.feature_id):nullptr;
                if(!source)throw std::runtime_error(
                    "Copy source has no calculated solid operand.");
                inputs.push_back({*source});
            }
            const auto source=inputs.size()==1?inputs.front().body:
                compound_bodies(inputs);
            auto copied=branch.scope.combination==BodyCombination::Mirror
                ? mirror_body(source,branch.scope.mirror_plane,branch.scope.id)
                : pattern_body(source,branch.scope.pattern,branch.scope.id);
            if(!branch.scope.target_id.empty()) {
                const auto references=copied.mesh.original_references;
                copied=subtract_bodies(document.body_outputs.at(branch.scope.target_id),copied,{},{},
                    branch.operations.front().boolean_tolerance,branch.operations.front().mesh_deflection);
                append_reference_geometry(copied.mesh.original_references,references);
                mark_copy_display(copied.mesh,branch.scope.id);
            }
            output=BodySnapshot(std::move(copied));
            document.body_inputs.emplace(branch.scope.id,output);
        } else if (branch.scope.combination != BodyCombination::Separate) {
            const auto& target = document.body_outputs.at(branch.scope.target_id).get();
            const auto output_key = input_key + ":boolean:" +
                std::to_string(static_cast<int>(branch.scope.combination)) + ":" + target.source_fingerprint + ":" +
                history_fingerprint(branch.operations, branch.operations.size());
            const auto old_output = previous.body_outputs.find(branch.scope.id);
            if (old_output != previous.body_outputs.end() && old_output->second->calculation_errors.empty() && old_output->second->source_fingerprint == output_key) {
                output = old_output->second;
            } else {
                if (input.kernel_shape.empty() || target.kernel_shape.empty())
                    throw std::invalid_argument("Boolean requires two calculated bodies");
                const auto calculate = [&](auto& algorithm) {
                    set_boolean_inputs(algorithm, read_kernel_shape(target), read_kernel_shape(input));
                    algorithm.SetFuzzyValue(branch.operations.back().boolean_tolerance);
                    algorithm.Build();
                    if (!algorithm.IsDone() || algorithm.Shape().IsNull() || !BRepCheck_Analyzer(algorithm.Shape()).IsValid())
                        throw std::runtime_error("Body Boolean failed");
                    auto result = make_result(algorithm.Shape(), {}, {}, {}, false, true, false, {}, branch.operations.back().mesh_deflection);
                    result.mesh.original_references = target.mesh.original_references;
                    append_reference_geometry(result.mesh.original_references, input.mesh.original_references);
                    result.source_fingerprint = output_key;
                    return result;
                };
                if (branch.scope.combination == BodyCombination::Add) {
                    BRepAlgoAPI_Fuse algorithm;
                    output = calculate(algorithm);
                } else if (branch.scope.combination == BodyCombination::Subtract) {
                    BRepAlgoAPI_Cut algorithm;
                    output = calculate(algorithm);
                } else {
                    BRepAlgoAPI_Common algorithm;
                    output = calculate(algorithm);
                }
            }
        }
        } catch (const Standard_Failure& failure) {
            if (!recover_errors) throw;
            output = preserve_failed_inputs(failure.GetMessageString());
        } catch (const std::exception& error) {
            if (!recover_errors) throw;
            output = preserve_failed_inputs(error.what());
        }
        if(aggregate_property_demand!=AggregatePropertyDemand::Deferred&&
                aggregate_property_demand!=AggregatePropertyDemand::DeferredAdaptive&&
                !output->physical_properties_current) {
            auto analyzed=output.get();ensure_body_properties(analyzed);output=std::move(analyzed);
        }
        document.calculation_errors.insert(output->calculation_errors.begin(), output->calculation_errors.end());
        document.ordered_up_to_failures.insert(output->ordered_up_to_failures.begin(),output->ordered_up_to_failures.end());
        if (!copy_sources.empty()||!branch.scope.source_id.empty())
            local = {output.get()};
        document.body_outputs.emplace(branch.scope.id, std::move(output));
        profile("branch output");
        for (std::size_t local_index=0;
                local_index<local.size();++local_index) {
            const auto& boundary=local[local_index];
            document.resolved_up_to_limits.insert(document.resolved_up_to_limits.end(),
                boundary.resolved_up_to_limits.begin(),boundary.resolved_up_to_limits.end());
            // The final slot is replaced by the complete document packet below.
            // Do not copy an unused complete import into that temporary slot.
            if(boundaries.size()+1==operations.size())boundaries.emplace_back();
            else {
                const bool unchanged_local=
                    local_index<cached_local.size()&&
                    cached_local[local_index].calculation_errors==
                        boundary.calculation_errors&&
                    cached_local[local_index].source_fingerprint==
                        boundary.source_fingerprint;
                if(unchanged_local&&
                   boundaries.size()<previous_boundaries.size()) {
                    // The local evaluator may temporarily restore reference
                    // coverage needed by a later changed suffix. The final
                    // document owns that live coverage in body_boundaries;
                    // keep the already persisted top-level prefix packet byte
                    // for byte unchanged.  Its ephemeral live proof is not
                    // serialized, so copy the proof from the exact local row
                    // which was just accepted by this kernel instance.
                    auto published=previous_boundaries[boundaries.size()];
                    published.calculation_provenance=
                        boundary.calculation_provenance;
                    boundaries.push_back(std::move(published));
                } else {
                    auto published=boundary;
                    published.source_fingerprint=history_fingerprint(
                        operations,boundaries.size()+1);
                    boundaries.push_back(std::move(published));
                }
            }
        }
    }
    // Keep intermediate packets for dependent operations; only final assembly,
    // export and physical properties omit suppressed Body results.
    for(const auto& branch:branches)if(branch.scope.result_suppressed&&available.erase(branch.scope.id))
        append_reference_geometry(document.mesh.original_references,
            document.body_outputs.at(branch.scope.id)->mesh.original_references);
    for (const auto& id : available) {
        const auto& output = document.body_outputs.at(id).get();
        if (!output.kernel_shape.empty()) document.kernel_compound_bodies.push_back(id);
        append_body_viewer(document.mesh, output.mesh);
        document.sheet_cuts.insert(document.sheet_cuts.end(),output.sheet_cuts.begin(),output.sheet_cuts.end());
    }
    document.kernel_shape = kernel_body_compound;
    profile("viewer aggregate");
    aggregate_body_properties(document,available);
    if(document.resolved_up_to_limits.empty())document.source_fingerprint = history_fingerprint(operations, operations.size());
    else {
        auto canonical=operations;
        for(auto& operation:canonical)resolve_ordered_up_to_operation(operation,document.resolved_up_to_limits);
        const auto keys=history_fingerprints(canonical);
        for(std::size_t i=0;i+1<boundaries.size();++i)boundaries[i].source_fingerprint=keys[i+1];
        document.source_fingerprint=keys.back();
    }
    boundaries.back() = std::move(document);
    compact_history_reference_geometry(boundaries);
    return boundaries;
}

std::vector<BodyResult> OcctKernel::evaluate_history_incremental(
    const std::vector<HistoryOperation>& operations,
    const std::vector<BodyResult>& previous_boundaries) const {
    if (operations.empty()) return {};
    if (std::any_of(operations.begin(), operations.end(), [](const auto& operation) {
            return !(operation.body == BodyHistoryScope{});
        })) {
        try {
            return live_cache_->mark_current(evaluate_body_histories(operations, previous_boundaries),
                previous_boundaries.empty()||has_current_calculation(previous_boundaries));
        } catch (const Standard_Failure& failure) {
            throw std::runtime_error(failure.GetMessageString());
        }
    }
    HistoryContext context;
    return live_cache_->mark_current(evaluate_flat_history(operations,previous_boundaries,context),
        previous_boundaries.empty()||has_current_calculation(previous_boundaries));
}

std::optional<std::vector<BodyResult>>
OcctKernel::evaluate_flat_history_batched(
        const std::vector<HistoryOperation>& operations,
        const std::vector<BodyResult>& previous_boundaries,
        HistoryContext& context) const {
    if(operations.size()<2||!context.final_boundary_first||
       context.preview_wire||context.direct_batch_active||
       aggregate_property_demand==AggregatePropertyDemand::All)
        return std::nullopt;

    const auto independent_add=[](const HistoryOperation& operation) {
        if(operation.suppressed||!operation.input_error.empty()||
           operation.replaces_body||
           operation.operation!=BooleanOperation::Add||
           !operation.sheet_regions.empty()||
           operation.solid_state_placements||
           !operation.solid_state_face_transfers.empty())
            return false;
        if(operation.feature_copy) {
            const auto sources=authored_copy_sources(
                *operation.feature_copy);
            return sources.size()==1&&
                sources.front().operation==operation.operation&&
                (operation.feature_copy->combination==
                       BodyCombination::Mirror||
                   operation.feature_copy->combination==
                       BodyCombination::Pattern);
        }
        const bool sheet=operation.sheet_operation!=SheetOperation::None||
            operation.sheet_material.has_value();
        if(const auto* group=std::get_if<FeatureGroupRequest>(
                &operation.primitive)) {
            if(group->children.empty())return false;
            return std::ranges::all_of(group->children,
                [&](const auto& child) {
                    return std::visit([&](const auto& request) {
                        using Request=std::decay_t<decltype(request)>;
                        if constexpr(std::is_same_v<Request,
                                         ExtrusionRequest>)
                            return (sheet||!request.sheet_cut)&&
                                !request.surface_result&&
                                request.extent!=
                                    ExtrusionRequest::Extent::ThroughAll&&
                                !request.through_all_reverse;
                        else if constexpr(std::is_same_v<Request,
                                              RevolutionRequest>)
                            return !request.surface_result;
                        else return request.make_solid;
                    },child);
                });
        }
        if(const auto* loft=std::get_if<LoftRequest>(
                &operation.primitive))
            return loft->make_solid;
        if(sheet) {
            if(const auto* extrusion=std::get_if<ExtrusionRequest>(
                    &operation.primitive))
                return !extrusion->surface_result&&
                    extrusion->extent!=
                        ExtrusionRequest::Extent::ThroughAll&&
                    !extrusion->through_all_reverse;
            if(const auto* revolution=std::get_if<RevolutionRequest>(
                    &operation.primitive))
                return !revolution->surface_result;
            if(const auto* sweep=std::get_if<Sweep3DRequest>(
                    &operation.primitive))
                return sweep->make_solid;
        }
        return false;
    };
    const auto direct_boundary_required=[&](std::size_t boundary) {
        if(!context.retained_output_boundaries.contains(boundary))
            return false;
        // Ordinary sheet Flats and Bends own their authored primitive
        // geometry.  A later attachment needs that exact owner's reference
        // packet, not a separately fused cumulative Body at every row.  The
        // direct block publishes those references from the standalone tool
        // below.  Full/cursor and edge-treatment boundaries still require the
        // real cumulative output and therefore split the block.
        if(context.full_output_boundaries.contains(boundary)||
           context.edge_output_boundaries.contains(boundary))
            return true;
        if(boundary==0||boundary>operations.size())return true;
        const auto& operation=operations[boundary-1];
        if(operation.sheet_operation==SheetOperation::None&&
           !operation.sheet_material)
            return true;
        const auto requested=context.reference_output_boundaries.find(
            boundary);
        return requested==context.reference_output_boundaries.end();
    };
    const auto independent_subtract=[&](const HistoryOperation& operation) {
        if(std::getenv("ZIMA_CPP_DISABLE_DIRECT_BATCH_SUBTRACT")||
           operation.operation!=BooleanOperation::Subtract)
            return false;
        auto additive=operation;
        additive.operation=BooleanOperation::Add;
        return independent_add(additive);
    };
    bool useful_block{};
    for(std::size_t index=0;index<operations.size();) {
        if(!independent_add(operations[index])) {++index;continue;}
        std::set<std::string> sources;
        const auto first=index;
        while(index<operations.size()&&
              independent_add(operations[index])&&
              !direct_boundary_required(index+1)) {
            if(operations[index].feature_copy) {
                const auto copy_sources=authored_copy_sources(
                    *operations[index].feature_copy);
                if(copy_sources.size()!=1||
                        !sources.contains(copy_sources.front().feature_id))
                    break;
            }
            sources.insert(operations[index].owner_id);
            ++index;
        }
        if(index-first>1||(first==1&&index-first==1&&
                          independent_add(operations.front())))
            useful_block=true;
        if(index==first)++index;
    }
    for(std::size_t index=0;index<operations.size();) {
        if(!independent_subtract(operations[index])) {++index;continue;}
        std::set<std::string> sources;
        const auto first=index;
        while(index<operations.size()&&
              independent_subtract(operations[index])&&
              !direct_boundary_required(index+1)) {
            if(operations[index].feature_copy) {
                const auto copy_sources=authored_copy_sources(
                    *operations[index].feature_copy);
                if(copy_sources.size()!=1||
                        !sources.contains(copy_sources.front().feature_id))
                    break;
            }
            sources.insert(operations[index].owner_id);
            ++index;
        }
        if(index-first>1)useful_block=true;
        if(index==first)++index;
    }
    if(!useful_block)return std::nullopt;

    std::vector<std::string> history_owners;
    std::vector<std::string> prefix_fingerprints;
    history_owners.reserve(operations.size());
    prefix_fingerprints.reserve(operations.size());
    for(std::size_t index=0;index<operations.size();++index) {
        history_owners.push_back(operations[index].owner_id);
        prefix_fingerprints.push_back(
            history_fingerprint(operations,index+1)+
            context.dependency_key);
    }

    auto boundaries=previous_boundaries;
    boundaries.reserve(operations.size());
    const bool trace_direct=
        std::getenv("ZIMA_CPP_PROFILE_COMMAND")!=nullptr||
        std::getenv("ZIMA_CPP_PROFILE_DIRECT_STAGES")!=nullptr;
    const auto elapsed_ms=[](const auto& start) {
        return std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-start).count();
    };
    const auto calculate_stage=[&](std::size_t operation_index) {
        const auto stage_start=std::chrono::steady_clock::now();
        std::vector<HistoryOperation> prefix(
            operations.begin(),operations.begin()+
                static_cast<std::ptrdiff_t>(operation_index+1));
        auto stage_context=context;
        stage_context.direct_batch_active=true;
        stage_context.defer_terminal_publication=
            operation_index+1<operations.size()&&
            !context.retained_output_boundaries.contains(
                operation_index+1);
        const auto expected=prefix_fingerprints[operation_index];
        const bool previous_stage_current=
            operation_index<previous_boundaries.size()&&
            previous_boundaries[operation_index].source_fingerprint==expected&&
            std::ranges::all_of(context.reference_output_boundaries,
                [&](const auto& requested) {
                    const auto boundary=requested.first;
                    return boundary>operation_index+1||(
                        boundary<=previous_boundaries.size()&&
                        std::includes(previous_boundaries[boundary-1].
                                history_reference_coverage.begin(),
                            previous_boundaries[boundary-1].
                                history_reference_coverage.end(),
                            requested.second.begin(),requested.second.end()));
                });
        const auto& stage_previous=previous_stage_current
            ?previous_boundaries:boundaries;
        boundaries=evaluate_flat_history(prefix,stage_previous,
            stage_context);
        context.original_faces=stage_context.original_faces;
        context.source_solids=std::move(stage_context.source_solids);
        if(trace_direct)
            std::cerr<<"[direct stage] index="<<operation_index
                <<" owner="<<operations[operation_index].owner_id
                <<" ms="<<elapsed_ms(stage_start)<<'\n';
    };
    const auto publish_construction=[](BodyResult& result) {
        const auto& originals=result.mesh.original_references;
        for(const auto& edge:originals.edges) {
            std::string source_key;
            if(construction_source_key(edge.reference.semantic_key,
                    source_key).starts_with("centerline:from:")&&
               std::ranges::none_of(result.mesh.edges,
                   [&](const auto& existing) {
                       return existing.reference==edge.reference;
                   }))
                result.mesh.edges.push_back(edge);
        }
        for(const auto& point:originals.points) {
            std::string source_key;
            const auto key=construction_source_key(
                point.reference.semantic_key,source_key);
            if(!axis_endpoint_parent_key(key).empty()&&
               std::ranges::none_of(result.mesh.points,
                   [&](const auto& existing) {
                       return existing.reference==point.reference;
                   })) {
                auto visible=point;
                if(visible.display_owner_id.empty())
                    visible.display_owner_id=
                        visible.reference.owner_id;
                result.mesh.points.push_back(std::move(visible));
            }
        }
        for(const auto& axis:originals.axes) {
            std::string source_key;
            const auto key=construction_source_key(
                axis.reference.semantic_key,source_key);
            if((key=="axis:primary"||
                key.starts_with("axis:profile:")||
                key.starts_with("centerline:from:")||
                key.starts_with("helical:rotation-axis:"))&&
               std::ranges::none_of(result.mesh.axes,
                   [&](const auto& existing) {
                       return existing.reference==axis.reference;
                   }))
                result.mesh.axes.push_back(axis);
        }
    };

    for(std::size_t index=0;index<operations.size();) {
        const bool subtract_block=independent_subtract(operations[index]);
        if(index==0||
           (!independent_add(operations[index])&&!subtract_block)||
           direct_boundary_required(index+1)) {
            calculate_stage(index++);
            continue;
        }

        std::set<std::string> block_owners;
        std::size_t end=index;
        while(end<operations.size()&&
              (subtract_block?independent_subtract(operations[end]):
                   independent_add(operations[end]))&&
              !direct_boundary_required(end+1)) {
            if(operations[end].feature_copy) {
                const auto copy_sources=authored_copy_sources(
                    *operations[end].feature_copy);
                if(copy_sources.size()!=1||
                        !block_owners.contains(copy_sources.front().feature_id))
                    break;
            }
            block_owners.insert(operations[end].owner_id);
            ++end;
        }
        const bool continues_initial_block=!subtract_block&&index==1&&
            independent_add(operations.front());
        if(end==index||(!continues_initial_block&&end-index<2)) {
            calculate_stage(index++);
            continue;
        }

        const auto& input=boundaries.back();
        const auto block_start=std::chrono::steady_clock::now();
        std::map<std::string,BodyResult> source_tools;
        std::vector<BodyResult> tools;
        std::vector<std::size_t> tool_indices;
        tools.reserve(end-index);
        tool_indices.reserve(end-index);
        double tolerance=1.0e-7;
        const auto calculate_tool=[&](const OcctKernel& tool_kernel,
                std::size_t tool_index,
                const std::map<std::string,BodyResult>& available) {
            const auto tool_start=std::chrono::steady_clock::now();
            const auto& operation=operations[tool_index];
            BodyResult tool;
            if(operation.feature_copy) {
                const auto& copy=*operation.feature_copy;
                const auto sources=authored_copy_sources(copy);
                if(sources.size()!=1)
                    throw std::runtime_error(
                        "Direct Add block requires one copy source");
                const auto source=available.find(
                    sources.front().feature_id);
                if(source==available.end())
                    throw std::runtime_error(
                        "Direct Add block copy source is unavailable");
                tool=copy.combination==BodyCombination::Pattern
                    ?pattern_body(source->second,copy.pattern,
                        operation.owner_id)
                    :mirror_body(source->second,copy.mirror_plane,
                        operation.owner_id);
            } else if(std::holds_alternative<FeatureGroupRequest>(
                          operation.primitive)) {
                BodyResult references;
                references.kernel_shape=kernel_body_compound;
                references.kernel_compound_bodies.push_back(
                    "direct-input");
                references.body_outputs.emplace("direct-input",input);
                for(const auto& [owner,value]:available) {
                    references.kernel_compound_bodies.push_back(owner);
                    references.body_outputs.emplace(owner,value);
                }
                references.source_fingerprint=
                    input.source_fingerprint+":direct-reference-set:"+
                    std::to_string(tool_index);
                tool=tool_kernel.calculate_feature_tool(operation,references,{},
                    input,{});
            } else {
                auto standalone=operation;
                standalone.body={};
                standalone.operation=BooleanOperation::Add;
                standalone.suppressed=false;
                standalone.feature_copy.reset();
                HistoryContext tool_context;
                tool_context.direct_batch_active=true;
                auto calculated=tool_kernel.evaluate_flat_history({standalone},{},
                    tool_context);
                if(calculated.size()!=1)
                    throw std::runtime_error(
                        "Direct Add block tool has no result");
                tool=std::move(calculated.front());
            }
            if(trace_direct)
                std::cerr<<"[direct Add tool] index="<<tool_index
                    <<" owner="<<operation.owner_id
                    <<" ms="<<elapsed_ms(tool_start)<<'\n';
            return tool;
        };
        const bool parallel_sheet_tools=!subtract_block&&end-index>3&&
            std::ranges::all_of(
                operations.begin()+static_cast<std::ptrdiff_t>(index),
                operations.begin()+static_cast<std::ptrdiff_t>(end),
                [](const auto& operation) {
                    return !operation.feature_copy&&
                        operation.sheet_material.has_value();
                });
        if(parallel_sheet_tools) {
            std::vector<std::optional<BodyResult>> ready(end-index);
            std::atomic_size_t next_tool{index};
            const auto workers=std::min<std::size_t>(8,
                std::min<std::size_t>(end-index,
                    std::max(2u,std::thread::hardware_concurrency())));
            std::vector<std::future<void>> jobs;
            jobs.reserve(workers);
            for(std::size_t worker=0;worker<workers;++worker)
                jobs.push_back(std::async(std::launch::async,[&] {
                    OcctKernel isolated;
                    AggregatePropertyScope properties(
                        AggregatePropertyDemand::Deferred);
                    FaceMeasurementScope face_measurements(false);
                    const std::map<std::string,BodyResult> none;
                    while(true) {
                        const auto tool_index=next_tool.fetch_add(1);
                        if(tool_index>=end)break;
                        ready[tool_index-index]=calculate_tool(
                            isolated,tool_index,none);
                    }
                }));
            for(auto& job:jobs)job.get();
            for(std::size_t tool_index=index;tool_index<end;++tool_index) {
                auto tool=std::move(*ready[tool_index-index]);
                source_tools.emplace(operations[tool_index].owner_id,tool);
                tools.push_back(std::move(tool));
                tool_indices.push_back(tool_index);
                tolerance=std::max(tolerance,
                    operations[tool_index].boolean_tolerance);
            }
        } else for(std::size_t tool_index=index;tool_index<end;
                ++tool_index) {
            auto tool=calculate_tool(*this,tool_index,source_tools);
            const auto& operation=operations[tool_index];
            source_tools.emplace(operation.owner_id,tool);
            tools.push_back(std::move(tool));
            tool_indices.push_back(tool_index);
            tolerance=std::max(tolerance,
                operation.boolean_tolerance);
        }
        const bool publish=end==operations.size();
        const auto union_start=std::chrono::steady_clock::now();
        auto direct=subtract_block
            ?calculate_subtract_block(input,tools,tool_indices,
                history_owners,tolerance,
                operations[end-1].mesh_deflection,
                prefix_fingerprints[end-1],publish)
            :calculate_add_block(input,tools,tool_indices,
                history_owners,tolerance,
                operations[end-1].mesh_deflection,
                prefix_fingerprints[end-1],prefix_fingerprints,publish);
        const auto union_ms=elapsed_ms(union_start);
        if(!subtract_block&&std::ranges::all_of(tool_indices,
                [&](const auto tool_index) {
                    return operations[tool_index].sheet_material.has_value()&&
                        operations[tool_index].sheet_regions.empty();
                })) {
            // The direct Add path already owns every authored sheet operand.
            // Retain those immutable operands with its final live boundary so
            // the first Unbend can transform them directly instead of first
            // replaying the complete Flat/Bend history just to rediscover the
            // same material regions.
            const auto geometry=[&](const BodyResult& body) {
                if(!body.source_fingerprint.empty()) {
                    const auto native=live_cache_->boundaries.find(
                        body.source_fingerprint);
                    if(native!=live_cache_->boundaries.end()&&
                       native->second.topology) {
                        PrimitiveData value;
                        value.shape=native->second.shape;
                        value.faces=native->second.topology->faces;
                        value.edges=native->second.topology->edges;
                        value.vertices=native->second.topology->vertices;
                        return value;
                    }
                }
                return placed_bound_face_geometry(body,true);
            };
            sheet_state_sources::Sources sources;
            if(!input.source_fingerprint.empty()) {
                const auto cached=live_cache_->boundaries.find(
                    input.source_fingerprint);
                if(cached!=live_cache_->boundaries.end())
                    sources=cached->second.sheet_sources;
            }
            bool complete=true;
            PrimitiveData input_geometry;
            const bool has_input=!(input.kernel_shape.empty()&&
                (input.source_fingerprint.empty()||
                 !live_cache_->boundaries.contains(
                     input.source_fingerprint)))&&
                input.kernel_shape!=kernel_body_compound;
            if(has_input)input_geometry=geometry(input);
            if(!sources&&!input_geometry.shape.IsNull()&&
               TopExp_Explorer(input_geometry.shape,TopAbs_SOLID).More()) {
                if(index>0&&operations[index-1].sheet_material&&
                   operations[index-1].sheet_regions.empty()) {
                    const auto regions=sheet_material::regions_before(
                        operations,index);
                    sources=sheet_state_sources::capture({}, {},
                        input_geometry,regions.regions,operations[index-1],
                        1.);
                } else complete=false;
            }
            std::vector<PrimitiveData> authored_tools;
            authored_tools.reserve(tools.size());
            std::map<std::string,std::size_t> authored_owners;
            for(std::size_t tool=0;tool<tools.size();++tool) {
                authored_tools.push_back(geometry(tools[tool]));
                authored_owners.emplace(
                    operations[tool_indices[tool]].owner_id,tool);
            }
            for(std::size_t tool=0;complete&&tool<tools.size();++tool) {
                const auto operation_index=tool_indices[tool];
                auto material=authored_tools[tool];
                const auto& definition=
                    *operations[operation_index].sheet_material;
                if(!definition.parent_owner_id.empty()) {
                    const auto parent=authored_owners.find(
                        definition.parent_owner_id);
                    if(parent!=authored_owners.end())
                        material=sheet_state_sources::difference(material,
                            authored_tools[parent->second],
                            operations[operation_index].owner_id);
                    else if(!input_geometry.shape.IsNull())
                        material=sheet_state_sources::difference(material,
                            input_geometry,
                            operations[operation_index].owner_id);
                    else complete=false;
                }
                if(!complete)break;
                const auto regions=sheet_material::regions_before(
                    operations,operation_index+1);
                sources=sheet_state_sources::capture(sources,{},
                    material,regions.regions,
                    operations[operation_index],1.);
            }
            const auto cached=live_cache_->boundaries.find(
                prefix_fingerprints[end-1]);
            if(complete&&sources&&cached!=live_cache_->boundaries.end())
                cached->second.sheet_sources=std::move(sources);
        }
        // The direct union intentionally skips the intermediate Add boundary
        // that originally named intersection edges. Resolve every later
        // treatment reference owned by this block while that exact untreated
        // boundary still exists, then carry the aliases through subsequent
        // Fillet/Chamfer history. Once an earlier treatment shortens an edge,
        // its old endpoint incidence no longer identifies the descendant.
        const auto retain_future_intersection_aliases=[&] {
            std::vector<EdgeReference> requested;
            const auto append=[&](const auto& treatment) {
                for(const auto& reference:treatment.edges) {
                    if(!block_owners.contains(reference.owner_id)||
                       !reference.semantic_key.starts_with(
                           "boolean:add:intersection:between:")||
                       std::ranges::find(requested,reference)!=
                           requested.end())
                        continue;
                    requested.push_back(reference);
                }
            };
            for(std::size_t future=end;future<operations.size();++future) {
                if(operations[future].suppressed)continue;
                if(const auto* fillet=std::get_if<FilletRequest>(
                        &operations[future].primitive))append(*fillet);
                else if(const auto* chamfer=std::get_if<ChamferRequest>(
                        &operations[future].primitive))append(*chamfer);
            }
            if(requested.empty())return;
            const auto boundary=live_cache_->boundaries.find(
                prefix_fingerprints[end-1]);
            if(boundary==live_cache_->boundaries.end()||
               !boundary->second.topology)
                throw DirectBatchReferenceFallback{};
            auto topology=std::make_shared<LiveCache::Topology>(
                *boundary->second.topology);
            TopTools_IndexedMapOfShape faces;
            TopExp::MapShapes(boundary->second.shape,TopAbs_FACE,faces);
            std::vector<OcctBoundaryFace> ancestry(
                static_cast<std::size_t>(faces.Extent()));
            for(int face=1;face<=faces.Extent();++face)
                ancestry[static_cast<std::size_t>(face-1)].shape=
                    TopoDS::Face(faces(face));
            for(const auto& owned:topology->faces) {
                const int face=faces.FindIndex(owned.shape);
                if(!face||!owned.reference.valid())continue;
                auto& references=ancestry[
                    static_cast<std::size_t>(face-1)].references;
                if(std::ranges::find(references,owned.reference)==
                   references.end())
                    references.push_back(owned.reference);
            }
            std::vector<EdgeReference> unresolved;
            unresolved.reserve(requested.size());
            for(const auto& reference:requested)
                if(std::ranges::none_of(topology->edges,
                        [&](const auto& edge) {
                            return edge.reference==reference;
                        }))
                    unresolved.push_back(reference);
            const auto resolved=occt_resolve_add_intersection_edges(
                boundary->second.shape,ancestry,unresolved,tolerance);
            if(resolved.size()!=unresolved.size())
                throw DirectBatchReferenceFallback{};
            for(std::size_t index=0;index<resolved.size();++index)
                topology->edges.push_back(
                    {resolved[index].shape,unresolved[index]});
            boundary->second.topology=std::move(topology);
        };
        if(!subtract_block) {
            try {
                retain_future_intersection_aliases();
            } catch(const DirectBatchReferenceFallback&) {
                throw;
            } catch(const std::exception&) {
                throw DirectBatchReferenceFallback{};
            }
        }
        std::vector<BodySnapshot> measurements=
            input.measurement_sources.get();
        auto block_references=input.mesh.original_references;
        for(std::size_t tool=0;tool<tools.size();++tool) {
            measurements.insert(measurements.end(),
                tools[tool].measurement_sources.begin(),
                tools[tool].measurement_sources.end());
            append_reference_geometry(block_references,
                tools[tool].mesh.original_references);
            if(tool+1<tools.size()) {
                BodyResult deferred;
                deferred.source_fingerprint=
                    prefix_fingerprints[index+tool];
                if(const auto requested=
                        context.reference_output_boundaries.find(
                            index+tool+1);
                        requested!=context.reference_output_boundaries.end()) {
                    deferred.mesh.original_references=block_references;
                    if(!contains_history_references(
                            deferred,requested->second)) {
                        if(trace_direct) {
                            std::cerr<<"[direct reference miss] boundary="
                                <<(index+tool+1)<<" tool="
                                <<operations[index+tool].owner_id<<'\n';
                            for(const auto& identity:requested->second)
                                std::cerr<<"  owner="<<std::get<0>(identity)
                                    <<" key="<<std::get<1>(identity)
                                    <<" path="<<std::get<2>(identity)<<'\n';
                        }
                        throw DirectBatchReferenceFallback{};
                    }
                    deferred.history_reference_coverage.insert(
                        requested->second.begin(),requested->second.end());
                }
                if(!measurements.empty())
                    deferred.measurement_sources=measurements;
                boundaries.push_back(std::move(deferred));
            }
        }
        if(!measurements.empty())
            direct.measurement_sources=std::move(measurements);
        if(const auto requested=context.reference_output_boundaries.find(end);
                requested!=context.reference_output_boundaries.end()) {
            if(!contains_history_references(direct,requested->second)) {
                if(trace_direct) {
                    std::cerr<<"[direct reference miss] boundary="<<end
                        <<" tool="<<operations[end-1].owner_id<<'\n';
                    for(const auto& identity:requested->second)
                        std::cerr<<"  owner="<<std::get<0>(identity)
                            <<" key="<<std::get<1>(identity)
                            <<" path="<<std::get<2>(identity)<<'\n';
                }
                throw DirectBatchReferenceFallback{};
            }
            direct.history_reference_coverage.insert(
                requested->second.begin(),requested->second.end());
        }
        if(publish)publish_construction(direct);
        boundaries.push_back(std::move(direct));
        if(trace_direct)
            std::cerr<<"[direct "<<(subtract_block?"Subtract":"Add")
                <<" block] first="<<index
                <<" count="<<(end-index)<<" publish="<<publish
                <<" union_ms="<<union_ms
                <<" total_ms="<<elapsed_ms(block_start)<<'\n';
        index=end;
    }
    if(!boundaries.empty()&&
       aggregate_property_demand==AggregatePropertyDemand::Final&&
       !boundaries.back().physical_properties_current)
        ensure_body_properties(boundaries.back());
    return boundaries;
}

std::vector<BodyResult> OcctKernel::evaluate_flat_history(
    const std::vector<HistoryOperation>& operations,
    const std::vector<BodyResult>& previous_boundaries,HistoryContext& context) const {
    context.original_faces.reset();
    context.source_solids.clear();
    // Native input validation already knows these definitions cannot produce
    // an operand. Error recovery locates their history boundary separately;
    // do not rebuild an unchanged imported body before reporting that error.
    for (const auto& operation : operations)
        if (!operation.suppressed && !operation.input_error.empty())
            throw std::runtime_error(operation.input_error);
    const bool has_sheet_material=std::ranges::any_of(
        operations,[](const auto& operation) {
            return !operation.suppressed&&(
                operation.sheet_operation!=SheetOperation::None||
                operation.sheet_material.has_value());
        });
    const bool cached_sheet_state_prefix=
        !previous_boundaries.empty()&&
        previous_boundaries.size()<operations.size()&&
        std::ranges::any_of(
            operations.begin()+static_cast<std::ptrdiff_t>(
                previous_boundaries.size()),operations.end(),
            [](const auto& operation) {
                return !operation.suppressed&&
                    std::holds_alternative<SheetStateRequest>(
                        operation.primitive);
            })&&
        live_cache_->boundaries.contains(
            previous_boundaries.back().source_fingerprint)&&
        live_cache_->boundaries.at(
            previous_boundaries.back().source_fingerprint).sheet_sources;
    std::size_t authored_reusable_prefix{};
    for(std::size_t index=0;
            index<std::min(operations.size(),
                previous_boundaries.size());++index) {
        if(!previous_boundaries[index].calculation_errors.empty()||
           previous_boundaries[index].source_fingerprint!=
               history_fingerprint(operations,index+1)+
                   context.dependency_key)
            break;
        authored_reusable_prefix=index+1;
    }
    const bool has_terminal_reusable_prefix=
        authored_reusable_prefix>0&&
        authored_reusable_prefix+1>=operations.size();
    const bool has_persisted_continuation_prefix=
        authored_reusable_prefix>0&&
        authored_reusable_prefix<operations.size()&&
        !previous_boundaries[authored_reusable_prefix-1].
            kernel_shape.empty()&&
        previous_boundaries[authored_reusable_prefix-1].kernel_shape!=
            kernel_body_compound;
    if(!context.direct_batch_active&&
       (previous_boundaries.empty()||
        (has_sheet_material&&!has_persisted_continuation_prefix))&&
       !has_terminal_reusable_prefix&&
       !cached_sheet_state_prefix&&
       !std::getenv("ZIMA_CPP_DISABLE_DIRECT_BATCH_REGEN")) {
        try {
            if(auto batched=evaluate_flat_history_batched(
                    operations,previous_boundaries,context))
                return std::move(*batched);
        } catch(const std::exception& error) {
            context.original_faces.reset();
            context.source_solids.clear();
            if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))
                std::cerr<<"[direct Add block fallback] "
                    <<error.what()<<'\n';
        }
    }
    const auto operation_needs_original_edges=[](const auto& operation) {
        if(!operation.suppressed&&(std::holds_alternative<SurfaceIntersectionRequest>(operation.primitive)||std::holds_alternative<SurfaceTrimRequest>(operation.primitive)))return true;
        if(!operation.suppressed) {
            if(const auto* fillet=std::get_if<FilletRequest>(
                    &operation.primitive))
                return fillet->calculation_method!=
                    EdgeTreatmentCalculationMethod::Occt;
            if(const auto* chamfer=std::get_if<ChamferRequest>(
                    &operation.primitive))
                return chamfer->calculation_method!=
                    EdgeTreatmentCalculationMethod::Occt;
        }
        const auto* surface=std::get_if<BoundarySurfaceRequest>(&operation.primitive);
        return !operation.suppressed&&surface&&std::ranges::any_of(surface->constraints,[](const auto& c){return c.edge.has_value();});
    };
    const bool retain_original_edges=std::ranges::any_of(
        operations,operation_needs_original_edges);
    std::optional<std::vector<HistoryOperation>> canonical_operations;
    const auto fingerprint=[&](const std::vector<HistoryOperation>& values,std::size_t count) {
        return history_fingerprint(&values==&operations&&canonical_operations?*canonical_operations:values,count)+context.dependency_key;
    };
    if(!previous_boundaries.empty()&&
       !previous_boundaries.back().resolved_up_to_limits.empty()) {
        auto canonical=operations;
        try {
            for(auto& operation:canonical)
                resolve_ordered_up_to_operation(operation,
                    previous_boundaries.back().resolved_up_to_limits);
            canonical_operations=std::move(canonical);
        } catch(const std::invalid_argument&) {
            // A changed explicit choice must invalidate ordinary prefix reuse.
            // Recalculate it from the authored operation instead of applying
            // a stale automatic resolution from the preceding result.
        }
    }
    try {
        TopoDS_Shape result_shape;
        std::shared_ptr<const LiveCache::Topology> owned_topology =
            std::make_shared<LiveCache::Topology>();
        ViewerReferenceGeometry original_references;
        struct TechnologicalSurface {
            TopoDS_Shape shape;
            std::string owner;
            std::string role;
            ThreadSurfaceBoundaries bounds;
        };
        std::vector<TechnologicalSurface> technological_surfaces;
        const auto append_technological_surfaces = [&](BodyResult& boundary, double mesh_deflection) {
            ThreadRimRegistry rims;
            for (const auto& surface : technological_surfaces) {
                append_technological_surface(boundary.mesh, surface.shape,
                    surface.owner, surface.role, surface.bounds, rims, mesh_deflection);
            }
        };
        std::unordered_map<std::string, StepDocumentCache> step_documents;
        std::vector<BodyResult> boundaries;
        boundaries.reserve(operations.size());
        // A source contribution is needed only before a later active state
        // operation. Suppressed states and edits after the final state do not
        // require extracting material differences during ordinary modeling.
        std::vector<bool> sheet_sources_needed(operations.size());
        bool retain_sheet_sources=false;
        for(std::size_t index=operations.size();index>0;--index) {
            sheet_sources_needed[index-1]=retain_sheet_sources;
            const auto& operation=operations[index-1];
            retain_sheet_sources=retain_sheet_sources||(!operation.suppressed&&
                std::holds_alternative<SheetStateRequest>(operation.primitive));
        }
        const bool has_forms=std::ranges::any_of(operations,[](const auto& operation){return std::holds_alternative<SheetFormRequest>(operation.primitive);});
        const bool has_subtractions=std::ranges::any_of(operations,[](const auto& operation){
            return !operation.suppressed&&operation.operation==BooleanOperation::Subtract;});
        // Keep the exact dependency geometry needed by later operations while
        // deferring complete intermediate viewer/persistence packets. The
        // forward OCCT shape and topology remain live and unchanged.
        const bool compact_dependency_boundaries=
            context.final_boundary_first&&
            !context.preview_wire&&!has_forms;
        // Fillet and Chamfer retain their complete owner packets until their
        // authored reference dependencies can be represented more narrowly.
        const bool final_first_boundaries=
            context.final_boundary_first&&
            std::ranges::none_of(operations,[](const auto& operation) {
                return std::holds_alternative<SheetStateRequest>(operation.primitive)||
                    std::holds_alternative<SheetRipRequest>(operation.primitive)||
                    std::holds_alternative<LineBendRequest>(operation.primitive)||
                    std::holds_alternative<SolidStateRequest>(operation.primitive);
            });
        const auto& retained_final_first_boundaries=
            context.retained_output_boundaries;
        double sheet_state_tolerance=1.;
        for(const auto& operation:operations)if(const auto* state=std::get_if<SheetStateRequest>(&operation.primitive))
            sheet_state_tolerance=std::min(sheet_state_tolerance,state->tolerance);
        sheet_state_sources::Sources sheet_sources;
        PrimitiveData sheet_input;
        std::vector<PrimitiveData> group_inputs;
        std::optional<sheet_form_geometry::Operands> form_operands;
        std::map<std::string,std::vector<sheet_form_geometry::Instance>> form_copy_operands;
        std::vector<sheet_form_geometry::Instance> copied_form_operands;
        std::size_t current_operation{};
        EdgeGuideReuse edge_guide_reuse;
        bool original_edges_history_complete=true;
#include "incremental_history_cache.inc"
        const auto remember_live_boundary = [&](
                const std::string& fingerprint, const TopoDS_Shape& shape,
                std::shared_ptr<const LiveCache::Topology> topology,
                const PrimitiveData* authored_sheet_additive=nullptr) {
            if(!fingerprint.empty())try{capture_native_frame(fingerprint,shape,*topology);}
            catch(const Standard_Failure&){native_history_enabled=false;
                if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))std::cerr<<"[native history capture] archive unavailable; keep the calculated result\n";}
            catch(const std::runtime_error&){native_history_enabled=false;
                if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))std::cerr<<"[native history capture] metadata unavailable; keep the calculated result\n";}
            if (fingerprint.empty() || shape.IsNull()) return;
            const bool trace=std::getenv("ZIMA_CPP_CALC_PROFILE")!=nullptr;
            auto start=trace?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
            const auto phase=[&](const char* name) {
                if(!trace)return;
                const auto now=std::chrono::steady_clock::now();
                std::fprintf(stderr,"Kernel retention count=%zu index=%zu owner=%s phase=%s total=%.6f s\n",
                    operations.size(),current_operation,operations[current_operation].owner_id.c_str(),name,
                    std::chrono::duration<double>(now-start).count());
                start=now;
            };
            if(!context.requested_solids.empty()||has_subtractions) {
                auto [packet,inserted]=live_cache_->copy_references.insert_or_assign(fingerprint,original_references);
                static_cast<void>(packet);
                if(inserted)live_cache_->copy_reference_order.push_back(fingerprint);
                while(live_cache_->copy_reference_order.size()>kLiveShapeCacheLimit) {
                    live_cache_->copy_references.erase(live_cache_->copy_reference_order.front());
                    live_cache_->copy_reference_order.pop_front();
                }
            }
            phase("prefix-cache");
            if(sheet_sources_needed[current_operation]) {
                PrimitiveData output;output.shape=shape;output.faces=topology->faces;
                output.edges=topology->edges;output.vertices=topology->vertices;
                const auto regions=sheet_material::regions_before(operations,current_operation+1);
                if(form_operands) {
                    sheet_sources=sheet_state_sources::capture_form(sheet_sources,sheet_input,
                        form_operands->cut,form_operands->flat,form_operands->formed,regions.regions,operations[current_operation]);
                } else if(!copied_form_operands.empty()) {
                    for(const auto& instance:copied_form_operands) {
                        auto child=operations[current_operation];child.owner_id=instance.material.owner_id;
                        sheet_sources=sheet_state_sources::capture_form(sheet_sources,sheet_input,
                            instance.operands.cut,instance.operands.flat,instance.operands.formed,regions.regions,child);
                    }
                } else if(group_inputs.empty() || operations[current_operation].sheet_regions.empty())sheet_sources=sheet_state_sources::capture(sheet_sources,sheet_input,output,
                    regions.regions,operations[current_operation],sheet_state_tolerance,
                    std::numeric_limits<double>::max(),authored_sheet_additive);
                else {
                    const auto& compound=operations[current_operation];
                    // Preserve the loft accuracy across a compound sheet's
                    // converging bend junctions. Coarser deformation tolerances
                    // can merge its closely spaced panel trims.
                    double material_tolerance=std::numeric_limits<double>::max();
                    if(const auto* group=std::get_if<FeatureGroupRequest>(&compound.primitive))
                        for(const auto& primitive:group->children)if(const auto* sweep=std::get_if<Sweep3DRequest>(&primitive))
                            material_tolerance=std::min(material_tolerance,sweep->linear_tolerance);
                    for(std::size_t i=0;i<compound.sheet_regions.size();++i) {
                        auto child=compound;child.sheet_regions.clear();child.sheet_material=compound.sheet_regions[i];
                        // Each child retains its own material domain, including
                        // the ownership needed for enclosed cuts before unfolding.
                        sheet_sources=sheet_state_sources::capture(sheet_sources,sheet_input,group_inputs[i],
                            regions.regions,child,sheet_state_tolerance,material_tolerance);
                    }
                }
            }
            phase("sheet-state-sources");
            // The current history may end at this state today and receive a
            // later Bend Back/Unbend command in the same session. Retain the
            // immutable authored contributions on the state boundary as well
            // as on prefixes which already have a later state. Otherwise the
            // next state command must replay every unchanged sheet feature.
            const bool state_boundary=
                !operations[current_operation].suppressed&&
                std::holds_alternative<SheetStateRequest>(
                    operations[current_operation].primitive);
            auto [iterator, inserted] =
                live_cache_->boundaries.insert_or_assign(fingerprint,
                    LiveCache::Boundary{shape, std::move(topology),context.original_faces,
                        sheet_sources_needed[current_operation]||state_boundary?
                            sheet_sources:nullptr,
                        retain_original_edges&&
                            original_edges_history_complete});
            static_cast<void>(iterator);
            phase("boundary-cache");
            if (!inserted) return;
            live_cache_->insertion_order.push_back(fingerprint);
            while (live_cache_->insertion_order.size() > kLiveShapeCacheLimit) {
                live_cache_->boundaries.erase(
                    live_cache_->insertion_order.front());
                live_cache_->insertion_order.pop_front();
            }
        };
        std::map<std::string,PrimitiveData> copy_operands;
        struct BoundedFilletSource {
            std::string owner;
            PrimitiveData operand;
            TopoDS_Shape base_shape;
            std::shared_ptr<const LiveCache::Topology> base_topology;
            double boolean_tolerance{};
        };
        std::optional<BoundedFilletSource> bounded_fillet_source;
        // A complete unchanged Form history can also be read from its saved
        // packet when no external caller needs a separate source operand.
        const bool reuse_complete_form_copy=has_forms&&context.requested_solids.empty()&&
            operations.size()==previous_boundaries.size()&&!previous_boundaries.empty()&&
            previous_boundaries.back().source_fingerprint==fingerprint(operations,operations.size());
        for(const auto& operation:operations)if(operation.feature_copy)
            for(const auto& source:authored_copy_feature_ids(
                    *operation.feature_copy))
                context.requested_solids.insert(source);
        std::size_t matching_prefix = 0;
        const auto available = std::min(
            operations.size(), previous_boundaries.size());
        while (matching_prefix < available &&
               previous_boundaries[matching_prefix].calculation_errors.empty() &&
               previous_boundaries[matching_prefix].source_fingerprint ==
                   fingerprint(operations, matching_prefix + 1)) {
            // Native fingerprints describe authored inputs. Keep saved Parts
            // readable without a calculation, but require the new centroid
            // datum before explicitly reusing a calculated corner packet.
            if(!operations[matching_prefix].suppressed)if(const auto* form=std::get_if<SheetFormRequest>(&operations[matching_prefix].primitive);
                form&&!form->solid_opening_faces.empty()&&form->definition) {
                const bool has_profile=std::ranges::any_of(*form->definition,[&](const auto& source) {
                    if(source.body.id!=form->shape_body)return false;
                    if(std::holds_alternative<ExtrusionRequest>(source.primitive))return true;
                    const auto* group=std::get_if<FeatureGroupRequest>(&source.primitive);
                    return group&&std::ranges::any_of(group->children,[](const auto& child){return std::holds_alternative<ExtrusionRequest>(child);});
                });
                if(has_profile&&!std::ranges::any_of(previous_boundaries.back().mesh.original_references.axes,[&](const auto& axis) {
                    return axis.reference.owner_id==operations[matching_prefix].owner_id&&
                        axis.reference.semantic_key.starts_with("centerline:from:centroid:form:");
                }))break;
            }
            // Capturing a calculated region guard must not recalculate the
            // same split. Check validation-only intent on cached packets too.
            if(!operations[matching_prefix].suppressed)if(const auto* trim=std::get_if<SurfaceTrimRequest>(&operations[matching_prefix].primitive))
                if(!trim->expected_region_key.empty()) {
                    const auto& refs=previous_boundaries.back().mesh.original_references.triangle_references;
                    if(!std::ranges::any_of(refs,[&](const auto& face){return face.owner_id==operations[matching_prefix].owner_id&&face.semantic_key==trim->expected_region_key;}))break;
                }
            ++matching_prefix;
        }
        // Coverage is disposable calculation evidence and is intentionally not
        // serialized.  An unchanged persisted boundary is nevertheless already
        // an exact immutable operation output when its fingerprint matches and
        // it contains every requested stable identity.  Preserve that authored
        // packet instead of replaying OCCT and possibly changing a face's
        // topological orientation.  A missing identity still requires replay:
        // only fresh coverage can prove the valid case where a fragment has
        // disappeared at this boundary.
        for(const auto& [boundary,requested]:
                context.reference_output_boundaries) {
            if(boundary>matching_prefix||boundary==0)continue;
            const auto& covered=previous_boundaries[boundary-1].
                history_reference_coverage;
            if(!std::includes(covered.begin(),covered.end(),
                    requested.begin(),requested.end())&&
                    !contains_history_references(
                        previous_boundaries[boundary-1],requested))
                matching_prefix=std::min(matching_prefix,boundary-1);
        }
        // A saved interactive packet may deliberately defer aggregate
        // properties.  It is a valid geometry prefix, but not a valid answer
        // to an explicit Final/All analysis request.  Resume at the smallest
        // missing suffix instead of returning the lightweight packet and then
        // trying to measure an intentionally compact intermediate snapshot.
        if(aggregate_property_demand==AggregatePropertyDemand::All) {
            for(std::size_t boundary=0;boundary<matching_prefix;++boundary)
                if(!previous_boundaries[boundary].physical_properties_current) {
                    matching_prefix=boundary;break;
                }
        } else if(aggregate_property_demand==AggregatePropertyDemand::Final&&
                matching_prefix==operations.size()&&matching_prefix>0&&
                !previous_boundaries[matching_prefix-1].physical_properties_current) {
            --matching_prefix;
        }
        const auto restore_persisted_reference_coverage=
                [&](std::vector<BodyResult>& values) {
            for(const auto& [boundary,requested]:
                    context.reference_output_boundaries) {
                if(boundary==0||boundary>values.size())continue;
                auto& value=values[boundary-1];
                if(contains_history_references(value,requested))
                    value.history_reference_coverage.insert(
                        requested.begin(),requested.end());
            }
        };
        static_cast<void>(restore_native_prefix(matching_prefix));
        // A copied feature needs its standalone operand at its actual input
        // boundary, including Through All / Up To and external dependencies.
        // Reuse that exact calculation, never the complete preceding Body.
        // A cold/missing source starts calculation at the source operation.
        if(!reuse_complete_form_copy) {
            // A copied feature that is already inside the accepted prefix is
            // already materialized in that boundary. Its standalone source is
            // needed only when that copy itself belongs to the recalculated
            // suffix. Expanding this dependency iteratively also covers a
            // source that moves into the suffix after an earlier expansion.
            while(true) {
                // Sources requested by another Body branch need the same
                // standalone operand as copies in this local suffix.  A saved
                // boundary alone contains only the accumulated Body result;
                // if its feature operand is not in the live cache, resume at
                // that source instead of accepting a prefix that cannot serve
                // the dependent Mirror/Pattern.
                std::set<std::string> suffix_copy_sources=
                    context.requested_solids;
                for(std::size_t index=matching_prefix;
                        index<operations.size();++index)
                    if(operations[index].feature_copy)
                        for(const auto& source:authored_copy_feature_ids(
                                *operations[index].feature_copy))
                            suffix_copy_sources.insert(source);
                std::size_t expanded_prefix=matching_prefix;
                for(std::size_t index=0;index<matching_prefix;++index) {
                    const auto& operation=operations[index];
                    if(suffix_copy_sources.contains(
                            operation.owner_id)&&
                       !live_cache_->copy_sources.contains(
                            fingerprint(operations,index+1)+
                            ":solid:"+operation.owner_id)) {
                        expanded_prefix=index;
                        break;
                    }
                }
                if(expanded_prefix==matching_prefix)break;
                matching_prefix=expanded_prefix;
            }
        }
        const auto restore_copy_sources=[&](std::size_t prefix) {
            std::size_t restored=0;
            for(std::size_t index=0;index<prefix;++index) {
                const auto& operation=operations[index];
                if(!context.requested_solids.contains(operation.owner_id))continue;
                const auto found=live_cache_->copy_sources.find(fingerprint(operations,index+1)+":solid:"+operation.owner_id);
                if(found==live_cache_->copy_sources.end())continue;
                copy_operands.insert_or_assign(operation.owner_id,found->second.operand);
                context.source_solids.insert_or_assign(operation.owner_id,found->second.solid);
                if(!found->second.forms.empty())form_copy_operands.insert_or_assign(operation.owner_id,found->second.forms);
                ++restored;
            }
            return restored;
        };
        const auto complete_live=matching_prefix>0&&matching_prefix==operations.size()
            ? live_cache_->boundaries.find(previous_boundaries[matching_prefix-1].source_fingerprint)
            : live_cache_->boundaries.end();
        const bool shortened_history=
            matching_prefix==operations.size()&&
            matching_prefix<previous_boundaries.size();
        const bool can_republish_shortened=shortened_history&&
            complete_live!=live_cache_->boundaries.end()&&
            complete_live->second.topology;
        if (matching_prefix == operations.size() &&
            (!shortened_history||can_republish_shortened) &&
            ((context.requested_solids.empty()&&!has_subtractions)||matching_prefix==previous_boundaries.size()||
                (matching_prefix>0&&live_cache_->copy_references.contains(previous_boundaries[matching_prefix-1].source_fingerprint)))&&
            (!context.require_original_faces || complete_live!=live_cache_->boundaries.end()) &&
            (!retain_original_edges || (complete_live!=live_cache_->boundaries.end()&&complete_live->second.original_edges_retained))) {
            const auto restored=restore_copy_sources(matching_prefix);
            if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))
                std::cerr<<"[history reuse] operations="<<operations.size()<<" reused="<<matching_prefix
                    <<" calculated=0 copy_sources="<<restored<<'\n';
            if(complete_live!=live_cache_->boundaries.end())context.original_faces=complete_live->second.original_faces;
            std::vector<BodyResult> reused{
                previous_boundaries.begin(),
                    previous_boundaries.begin() +
                        static_cast<std::ptrdiff_t>(matching_prefix)};
            if(shortened_history) {
                // Final-first evaluation deliberately keeps intermediate rows
                // lightweight. Deleting the suffix turns the last retained row
                // into the visible terminal result. Its exact live shape and
                // topology already exist, so publish a full viewer/analysis
                // packet without replaying the unchanged modeling operation.
                const auto& native=complete_live->second;
                const bool calculate_properties=
                    aggregate_property_demand==AggregatePropertyDemand::All||
                    aggregate_property_demand==AggregatePropertyDemand::Final||
                    aggregate_property_demand==AggregatePropertyDemand::DeferredAdaptive;
                auto published=make_result(native.shape,
                    native.topology->faces,native.topology->edges,
                    native.topology->vertices,false,true,false,
                    native.topology->hidden_display_edges,
                    operations.back().mesh_deflection,calculate_properties);
                auto& retained=reused.back();
                retained.mesh=std::move(published.mesh);
                retained.physical_properties_current=
                    published.physical_properties_current;
                retained.solid_present=published.solid_present;
                retained.volume=published.volume;
                retained.surface_area=published.surface_area;
                retained.volume_integrals=published.volume_integrals;
                retained.surface_centroid=published.surface_centroid;
                retained.kernel_shape=std::move(published.kernel_shape);
                retained.kernel_placements.clear();
                retained.kernel_compound_bodies.clear();
                retained.kernel_faces=std::move(published.kernel_faces);
                retained.kernel_edges=std::move(published.kernel_edges);
                retained.kernel_vertices=std::move(published.kernel_vertices);
            }
            // A recovering evaluation asks for shorter prefixes of the saved
            // history. Earlier packets were compacted; recover their original
            // references from the final packet, restricted to prefix owners.
            std::unordered_set<std::string> owners;
            for(const auto& operation:operations)if(!operation.suppressed)owners.insert(operation.owner_id);
            if(!reused.empty()&&matching_prefix<previous_boundaries.size()) {
                if(!context.requested_solids.empty()||has_subtractions)reused.back().mesh.original_references=
                    live_cache_->copy_references.at(previous_boundaries[matching_prefix-1].source_fingerprint);
                else reused.back().mesh.original_references=reference_geometry_for_owners(
                    previous_boundaries.back().mesh.original_references,owners);
            }
            restore_persisted_reference_coverage(reused);
            attach_native_history(reused);
            compact_history_reference_geometry(reused);
            return live_cache_->mark_current(std::move(reused),complete_live!=live_cache_->boundaries.end());
        }
        std::size_t reusable_prefix = matching_prefix;
        if(shortened_history&&reusable_prefix>0) {
            // Intermediate packets may deliberately omit their complete
            // display mesh while a later history row owns the final scene.
            // Once that suffix is deleted, republish the last retained
            // operation as the terminal boundary. Replaying only that row
            // preserves the valid prefix without presenting an empty Body.
            --reusable_prefix;
        }
        if(std::getenv("ZIMA_CPP_PROFILE_COMMAND")&&reusable_prefix>0) {
            const auto& candidate=
                previous_boundaries[reusable_prefix-1];
            std::cerr<<"[history prefix candidate] operations="
                <<operations.size()<<" prefix="<<reusable_prefix
                <<" live="<<live_cache_->boundaries.contains(
                    candidate.source_fingerprint)
                <<" references="<<live_cache_->copy_references.contains(
                    candidate.source_fingerprint)
                <<" retain-original-edges="<<retain_original_edges
                <<" has-subtractions="<<has_subtractions
                <<" resolutions="
                <<previous_boundaries.back().resolved_up_to_limits.size()
                <<" canonical="<<bool(canonical_operations)<<'\n';
            if(reusable_prefix<available)
                std::cerr<<"[history prefix mismatch] index="
                    <<reusable_prefix<<" owner="
                    <<operations[reusable_prefix].owner_id
                    <<" errors="
                    <<previous_boundaries[reusable_prefix].
                        calculation_errors.size()
                    <<" previous="
                    <<previous_boundaries[reusable_prefix].source_fingerprint
                    <<" expected="
                    <<fingerprint(operations,reusable_prefix+1)<<'\n';
        }
        if(std::getenv("ZIMA_CPP_CALC_PROFILE"))std::fprintf(stderr,
            "Kernel reuse matching=%zu available=%zu previous=%s expected=%s live=%d\n",matching_prefix,available,
            available?previous_boundaries[available-1].source_fingerprint.c_str():"",
            fingerprint(operations,available).c_str(),int(available&&live_cache_->boundaries.contains(previous_boundaries[available-1].source_fingerprint)));
        if (std::any_of(operations.begin(), operations.end(), [](const auto& operation) {
                return std::holds_alternative<ThreadSurfaceRequest>(
                    operation.primitive);
            })) {
            // The persisted boundary contains the display mesh, while live
            // OCCT technological sheets intentionally stay kernel-private.
            // Rebuild the short sheet chain for an explicit changed history
            // instead of pretending a solid-only prefix also restored it.
            reusable_prefix = 0;
        }
        // A compact native Part stores complete retained B-Reps together with
        // exact face/edge/vertex locators. When an independent suffix is
        // appended or replaces a suffix after reopening, those bindings are
        // sufficient to restore the live continuation boundary without
        // replaying the complete old history. Operations that need an earlier
        // standalone source or an original limit face retain the conservative
        // replay path below.
        if(reusable_prefix>0&&
                reusable_prefix<operations.size()&&
                !live_cache_->boundaries.contains(
                    previous_boundaries[reusable_prefix-1].
                        source_fingerprint)) {
            const std::vector<HistoryOperation> suffix(
                operations.begin()+static_cast<std::ptrdiff_t>(
                    reusable_prefix),operations.end());
            const bool suffix_needs_original_edges=
                std::ranges::any_of(suffix,
                    operation_needs_original_edges);
            std::set<std::string> prefix_owners;
            for(std::size_t index=0;index<reusable_prefix;++index)
                prefix_owners.insert(operations[index].owner_id);
            const bool needs_copy_source=std::ranges::any_of(
                suffix,[&](const auto& operation) {
                    if(!operation.feature_copy)return false;
                    return std::ranges::any_of(
                        authored_copy_feature_ids(
                            *operation.feature_copy),
                        [&](const auto& source) {
                            return prefix_owners.contains(source);
                        });
                });
            const auto required_original_faces=
                extrusion_references(suffix);
            const bool needs_sheet_state=std::ranges::any_of(
                suffix,[](const auto& operation) {
                    return !operation.suppressed&&
                        std::holds_alternative<SheetStateRequest>(
                            operation.primitive);
                });
            if(!suffix_needs_original_edges&&!needs_copy_source&&
                    !needs_sheet_state)try {
                const auto& persisted=
                    previous_boundaries[reusable_prefix-1];
                if(persisted.kernel_shape.empty()||
                   persisted.kernel_shape==kernel_body_compound)
                    throw std::runtime_error(
                        "Persisted continuation has no native solid");
                auto geometry=bound_face_geometry(persisted,true);
                const auto complete=[](const TopoDS_Shape& shape,
                        TopAbs_ShapeEnum kind,const auto& owned) {
                    TopTools_IndexedMapOfShape actual;
                    TopExp::MapShapes(shape,kind,actual);
                    TopTools_IndexedMapOfShape bound;
                    for(const auto& item:owned)
                        if(actual.Contains(item.shape))
                            bound.Add(item.shape);
                    return bound.Size()==actual.Extent();
                };
                if(!complete(geometry.shape,TopAbs_FACE,
                        geometry.faces)||
                   !complete(geometry.shape,TopAbs_EDGE,
                        geometry.edges)||
                   !complete(geometry.shape,TopAbs_VERTEX,
                        geometry.vertices))
                    throw std::runtime_error(
                        "Persisted continuation topology is incomplete");
                std::vector<OwnedFace> continuation_original_faces;
                continuation_original_faces.reserve(
                    required_original_faces.size());
                for(const auto& required:required_original_faces) {
                    const auto found=std::ranges::find_if(
                        geometry.faces,[&](const auto& face) {
                            return same_face_identity(
                                face.reference,required);
                        });
                    if(found==geometry.faces.end())
                        throw std::runtime_error(
                            "Persisted continuation does not own a required face identity");
                    continuation_original_faces.push_back(*found);
                }
                std::shared_ptr<const OriginalFaces>
                    continuation_originals;
                if(!continuation_original_faces.empty())
                    continuation_originals=
                        std::make_shared<OriginalFaces>(OriginalFaces{
                            std::move(continuation_original_faces),
                            nullptr,{},{}});
                auto topology=std::make_shared<LiveCache::Topology>(
                    LiveCache::Topology{std::move(geometry.faces),
                        std::move(geometry.edges),
                        std::move(geometry.vertices),{}});
                live_cache_->boundaries.insert_or_assign(
                    persisted.source_fingerprint,
                    LiveCache::Boundary{geometry.shape,
                        std::move(topology),
                        std::move(continuation_originals),nullptr,false});
                original_edges_history_complete=false;
                std::erase(live_cache_->insertion_order,
                    persisted.source_fingerprint);
                live_cache_->insertion_order.push_back(
                    persisted.source_fingerprint);
                live_cache_->copy_references.insert_or_assign(
                    persisted.source_fingerprint,
                    persisted.mesh.original_references);
                std::erase(live_cache_->copy_reference_order,
                    persisted.source_fingerprint);
                live_cache_->copy_reference_order.push_back(
                    persisted.source_fingerprint);
                while(live_cache_->insertion_order.size()>
                        kLiveShapeCacheLimit) {
                    live_cache_->boundaries.erase(
                        live_cache_->insertion_order.front());
                    live_cache_->insertion_order.pop_front();
                }
                while(live_cache_->copy_reference_order.size()>
                        kLiveShapeCacheLimit) {
                    live_cache_->copy_references.erase(
                        live_cache_->copy_reference_order.front());
                    live_cache_->copy_reference_order.pop_front();
                }
                if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))
                    std::cerr<<"[persisted continuation] prefix="
                        <<reusable_prefix<<" faces="
                        <<live_cache_->boundaries.at(
                            persisted.source_fingerprint).topology->
                                faces.size()<<'\n';
            }catch(const Standard_Failure& error) {
                // A stale/incomplete packet is never trusted for modeling.
                if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))
                    std::cerr<<"[persisted continuation rejected] "
                        <<error.GetMessageString()<<'\n';
            }catch(const std::runtime_error& error) {
                // Fall through to the established full ancestry replay.
                if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))
                    std::cerr<<"[persisted continuation rejected] "
                        <<error.what()<<'\n';
            }
        }
        while (reusable_prefix > 0) {
            const auto& boundary = previous_boundaries[reusable_prefix - 1];
            if ((!boundary.kernel_shape.empty() ||
                live_cache_->boundaries.contains(boundary.source_fingerprint))&&
                ((context.requested_solids.empty()&&!has_subtractions)||live_cache_->copy_references.contains(boundary.source_fingerprint))) {
                break;
            }
            --reusable_prefix;
        }
        const bool reusable_prefix_has_live_ancestry = reusable_prefix > 0 &&
            live_cache_->boundaries.contains(
                previous_boundaries[reusable_prefix - 1].source_fingerprint);
        const bool cold_reference_tail = reusable_prefix > 0 &&
            !reusable_prefix_has_live_ancestry && !context.require_original_faces &&
            !retain_original_edges && context.requested_solids.empty() &&
            context.dependency_key.empty() &&
            std::ranges::all_of(operations.begin(),
                operations.begin() + static_cast<std::ptrdiff_t>(reusable_prefix),
                [](const auto& operation) {
                    const auto* group = std::get_if<FeatureGroupRequest>(&operation.primitive);
                    return !operation.feature_copy &&
                        (std::holds_alternative<StepRequest>(operation.primitive) ||
                         (group && group->children.empty() && group->allow_empty));
                }) &&
            std::ranges::all_of(operations.begin() + static_cast<std::ptrdiff_t>(reusable_prefix),
                operations.end(), [](const auto& operation) {
                    const auto* group = std::get_if<FeatureGroupRequest>(&operation.primitive);
                    return !operation.suppressed && !operation.feature_copy &&
                        !operation.replaces_body && group && group->children.empty() &&
                        (group->allow_empty || !group->reference_profiles.empty());
                });
        // A persisted B-Rep is enough to reproduce geometry, but not to map
        // its runtime OCCT faces back to persisted ZIMA source identities.
        // An explicitly requested material calculation after a cold load
        // rebuilds the history once; unchanged display/open paths still reuse
        // the saved Body without invoking OCCT at all. A reference-only tail
        // can retain that packet without exposing fabricated live ancestry.
        if (reusable_prefix > 0 && !reusable_prefix_has_live_ancestry && !cold_reference_tail) {
            reusable_prefix = 0;
        }
        const bool sheet_state_only_suffix=
            std::ranges::all_of(
                operations.begin()+static_cast<std::ptrdiff_t>(
                    reusable_prefix),operations.end(),
                [](const auto& operation) {
                    return operation.suppressed||
                        std::holds_alternative<SheetStateRequest>(
                            operation.primitive);
                });
        const bool suffix_needs_original_edges=
            std::ranges::any_of(
                operations.begin()+static_cast<std::ptrdiff_t>(
                    reusable_prefix),operations.end(),
                operation_needs_original_edges);
        if(suffix_needs_original_edges&&reusable_prefix>0&&
            !sheet_state_only_suffix&&
            !live_cache_->boundaries.at(previous_boundaries[reusable_prefix-1].source_fingerprint).original_edges_retained)
            reusable_prefix=0;
        const bool remaining_sheet_state=std::ranges::any_of(
            operations.begin()+static_cast<std::ptrdiff_t>(reusable_prefix),operations.end(),
            [](const auto& operation){return !operation.suppressed&&std::holds_alternative<SheetStateRequest>(operation.primitive);});
        if(remaining_sheet_state&&reusable_prefix>0&&
            !live_cache_->boundaries.at(previous_boundaries[reusable_prefix-1].source_fingerprint).sheet_sources) {
            // A fused boundary does not retain the separate authored material
            // regions. Reconstruct them once when the first state operation
            // is added to a previously calculated ordinary feature history.
            reusable_prefix=0;
        }
        if(canonical_operations) {
            // Persisted automatic choices are valid only inside the accepted
            // unchanged prefix. Every changed suffix resolves its own sides
            // again against the newly calculated input geometry.
            for(std::size_t index=reusable_prefix;
                    index<operations.size();++index)
                (*canonical_operations)[index]=operations[index];
        }
        const auto restored_sources=restore_copy_sources(reusable_prefix);
        if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))std::cerr<<"[history reuse] operations="<<operations.size()
            <<" reused="<<reusable_prefix<<" calculated="<<std::ranges::count_if(
                operations.begin()+static_cast<std::ptrdiff_t>(reusable_prefix),operations.end(),
                [](const auto& operation){return !operation.suppressed;})
            <<" copy_sources="<<restored_sources<<'\n';
        if (reusable_prefix > 0) {
            const auto& prefix_boundary =
                previous_boundaries[reusable_prefix - 1];
            const auto cached = live_cache_->boundaries.find(
                prefix_boundary.source_fingerprint);
            if (cold_reference_tail) {
                // Datums consume persisted ZIMA geometry, never live material
                // faces. Keep the exact saved body and leave OCCT ancestry cold
                // so a later Boolean still rebuilds its real source history.
            } else if (cached == live_cache_->boundaries.end()) {
                result_shape = read_kernel_shape(prefix_boundary);
            } else {
                result_shape = cached->second.shape;
                owned_topology = cached->second.topology;
                context.original_faces = cached->second.original_faces;
                sheet_sources = cached->second.sheet_sources;
            }
            std::unordered_set<std::string> prefix_owners;
            for (std::size_t index = 0; index < reusable_prefix; ++index) {
                if (!operations[index].suppressed) {
                    prefix_owners.insert(operations[index].owner_id);
                }
            }
            if(!context.requested_solids.empty()||has_subtractions) {
                // A later operation may create descendants owned by an older
                // source. Owner filtering of the final packet cannot recover
                // their actual creation boundary or original ordered geometry.
                original_references=live_cache_->copy_references.at(prefix_boundary.source_fingerprint);
                std::erase(live_cache_->copy_reference_order,prefix_boundary.source_fingerprint);
                live_cache_->copy_reference_order.push_back(prefix_boundary.source_fingerprint);
            } else original_references = reference_geometry_for_owners(
                previous_boundaries.back().mesh.original_references,prefix_owners);
            boundaries.insert(boundaries.end(), previous_boundaries.begin(),
                previous_boundaries.begin() +
                    static_cast<std::ptrdiff_t>(reusable_prefix));
            restore_persisted_reference_coverage(boundaries);
        }
        const auto original_target=[&](const ExtrusionLimitView& limit) {
            if(limit.datum)return std::optional<TopoDS_Face>{};
            std::vector<OwnedFace> matches;
            for(auto node=context.original_faces;node;node=node->previous)
                for(const auto& face:node->faces)if(same_face_identity(face.reference,limit.reference))matches.push_back(face);
            for(const auto& face:context.external_faces)if(same_face_identity(face.reference,limit.reference))matches.push_back(face);
            return exact_extrusion_limit(limit,matches);
        };
        const auto original_ordered_targets=[&](const std::optional<OrderedUpToLimit>& limit) {
            std::vector<std::optional<TopoDS_Face>> result;
            if(limit)for(std::size_t row=0;row<limit->patches.size();++row)try {
                const auto& patch=limit->patches[row];
                if(!patch.native_source) {result.push_back(original_target(limit_view(patch.target)));continue;}
                const auto& source=*patch.native_source;
                auto geometry=native_face_source(source.body.get(),source.reference).geometry(*this);
                std::vector<OwnedFace> selected;
                for(auto face:geometry.faces)if(same_face_identity(face.reference,source.reference)) {
                    for(const auto& placement:source.placements)
                        face.shape=TopoDS::Face(BRepBuilderAPI_Transform(face.shape,
                            primitive_transform(placement.translation,placement.rotation_degrees),true).Shape());
                    face.reference=patch.target.reference;selected.push_back(std::move(face));
                }
                result.push_back(exact_extrusion_limit(limit_view(patch.target),selected));
            }catch(const OrderedUpToError&) {throw;}
            catch(const std::runtime_error&) {
                throw OrderedUpToError(OrderedUpToFailure::InvalidInput,row,row);
            }
            return result;
        };
        const auto retain_originals=[&](const std::vector<OwnedFace>& faces,const std::vector<OwnedEdge>& edges=std::vector<OwnedEdge>{},
                const std::vector<OwnedVertex>& vertices=std::vector<OwnedVertex>{},
                TopoDS_Shape source_shape={}) {
            if(!faces.empty()||(retain_original_edges&&!edges.empty()))context.original_faces=std::make_shared<OriginalFaces>(OriginalFaces{
                faces,context.original_faces,retain_original_edges?edges:std::vector<OwnedEdge>{},
                retain_original_edges?vertices:std::vector<OwnedVertex>{},
                retain_original_edges?std::move(source_shape):TopoDS_Shape{}});
        };
        std::vector<BodySnapshot> measurement_sources;
        if(!boundaries.empty())measurement_sources=boundaries.back().measurement_sources.get();
        for (std::size_t operation_index = reusable_prefix;
             operation_index < operations.size(); ++operation_index) {
            const auto& operation = operations[operation_index];
            native_original_before=context.original_faces;
            native_reference_before.emplace(original_references);
            std::vector<ResolvedOrderedUpToLimit> resolved_up_to;
            struct OperationProfile {
                const HistoryOperation& operation;
                std::size_t count,index;
                bool active=std::getenv("ZIMA_CPP_CALC_PROFILE")!=nullptr;
                std::chrono::steady_clock::time_point start=active?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
                ~OperationProfile() {
                    if(active)std::fprintf(stderr,"Kernel operation count=%zu index=%zu owner=%s primitive=%zu total=%.6f s\n",
                        count,index,operation.owner_id.c_str(),operation.primitive.index(),
                        std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());
                }
            } operation_profile{operation,operations.size(),operation_index};
            auto operation_phase_stamp=operation_profile.start;
            const auto operation_phase=[&](const char* phase) {
                if(!operation_profile.active)return;
                const auto now=std::chrono::steady_clock::now();
                std::fprintf(stderr,"Kernel operation phase count=%zu index=%zu owner=%s %s=%.6f s\n",
                    operations.size(),operation_index,operation.owner_id.c_str(),phase,
                    std::chrono::duration<double>(now-operation_phase_stamp).count());
                operation_phase_stamp=now;
            };
            const auto* feature_group=std::get_if<FeatureGroupRequest>(&operation.primitive);
            const bool reference_only=feature_group && feature_group->children.empty() &&
                (feature_group->allow_empty || !feature_group->reference_profiles.empty());
            current_operation=operation_index;
            group_inputs.clear();
            form_operands.reset();
            copied_form_operands.clear();
            if(sheet_sources_needed[operation_index]) {
                sheet_input.shape=result_shape;sheet_input.faces=owned_topology->faces;
                sheet_input.edges=owned_topology->edges;sheet_input.vertices=owned_topology->vertices;
            }
            const bool surface_operand=std::visit([](const auto& request){
                if constexpr(requires{request.surface_result;})return request.surface_result;
                else if constexpr((std::is_same_v<std::decay_t<decltype(request)>,Sweep3DRequest>||std::is_same_v<std::decay_t<decltype(request)>,LoftRequest>))return !request.make_solid;
                else if constexpr(std::is_same_v<std::decay_t<decltype(request)>,FeatureGroupRequest>)
                    return !request.children.empty()&&std::ranges::all_of(request.children,[](const auto& child){
                        return std::visit([](const auto& value){
                            if constexpr(requires{value.surface_result;})return value.surface_result;
                            else return !value.make_solid;
                        },child);
                    });
                else return false;
            },operation.primitive);
            if(surface_operand && operation.operation==BooleanOperation::Subtract)
                throw std::invalid_argument("A surface cannot subtract material.");
            if (!operation.suppressed && !operation.input_error.empty())
                throw std::runtime_error(operation.input_error);
            if (!std::isfinite(operation.mesh_deflection) || operation.mesh_deflection <= 0)
                throw std::invalid_argument("Invalid mesh deflection");
            const auto make_operation_result = [&](const TopoDS_Shape& shape,
                const std::vector<OwnedFace>& faces, const std::vector<OwnedEdge>& edges,
                const std::vector<OwnedVertex>& vertices, bool original = false,
                bool persist = true, bool collect = false,
                const std::vector<TopoDS_Shape>& hidden = {}, bool calculate_body_properties = true) {
                const bool retain_analysis_solid=calculate_body_properties;
                calculate_body_properties=calculate_body_properties&&
                    (aggregate_property_demand==AggregatePropertyDemand::All||
                    aggregate_property_demand==AggregatePropertyDemand::DeferredAdaptive||
                    (aggregate_property_demand==AggregatePropertyDemand::Final&&operation_index+1==operations.size()));
                const bool trace=std::getenv("ZIMA_CPP_CALC_PROFILE")!=nullptr;
                if(trace)std::fprintf(stderr,"Kernel packet count=%zu index=%zu owner=%s properties=%d\n",
                    operations.size(),operation_index,operation.owner_id.c_str(),int(calculate_body_properties));
                const auto start=trace?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
                auto result=make_result(shape, faces, edges, vertices, original, persist,
                    collect, hidden, operation.mesh_deflection, calculate_body_properties,
                    // Later cuts and copies retain the FORM's rational patches.
                    // Their complete properties need the same exact parallel
                    // integration as the FORM itself, not a serial reintegration.
                    has_forms,&edge_guide_reuse);
                if(!eager_face_measurements&&(original||collect)&&!result.kernel_faces.empty()) {
                    BodyResult source;source.kernel_shape=result.kernel_shape;source.kernel_faces=result.kernel_faces;
                    source.source_fingerprint=operation.owner_id;source.physical_properties_current=false;
                    source.solid_present=result.has_solid();measurement_sources.emplace_back(std::move(source));
                }
                if(!measurement_sources.empty())result.measurement_sources=measurement_sources;
                if(retain_analysis_solid&&!result.physical_properties_current&&result.kernel_shape.empty())
                    result.kernel_shape=serialize_kernel_shape(shape);
                if(trace)std::fprintf(stderr,"Kernel packet done count=%zu index=%zu owner=%s total=%.6f s\n",
                    operations.size(),operation_index,operation.owner_id.c_str(),
                    std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());
                return result;
            };
            const auto make_published_result = [&](const TopoDS_Shape& shape,
                const std::vector<OwnedFace>& faces, const std::vector<OwnedEdge>& edges,
                const std::vector<OwnedVertex>& vertices, bool original = false,
                bool persist = true, bool collect = false,
                const std::vector<TopoDS_Shape>& hidden = {}, bool calculate_body_properties = true) {
                const auto requested=context.reference_output_boundaries.find(
                    operation_index+1);
                const auto* requested_references=
                    requested==context.reference_output_boundaries.end()
                        ?nullptr:&requested->second;
                const auto complete_request=[&](BodyResult result) {
                    if(requested_references) {
                        // A compact dependency boundary still has to publish
                        // the current endpoint identities of a requested edge.
                        // OCCT can keep the edge ancestry while dropping the
                        // two vertex bindings from the compact owned-topology
                        // packet.  Reuse the bindings from this operation's
                        // current authored reference geometry only when both
                        // geometric endpoints prove that it is the same,
                        // untrimmed edge (allowing reversed parameterization).
                        const auto squared_distance=[](const Vec3& a,
                                const Vec3& b) {
                            const double x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;
                            return x*x+y*y+z*z;
                        };
                        constexpr double endpoint_tolerance_squared=1.e-12;
                        for(auto& published:
                                result.mesh.original_references.edges) {
                            if(published.points.size()<2||
                                    published.edge_treatment_endpoint_references.size()==2)
                                continue;
                            const HistoryReferenceIdentity identity{
                                published.reference.owner_id,
                                published.reference.semantic_key,
                                published.reference.instance_path};
                            if(!requested_references->contains(identity))continue;
                            const auto current=std::ranges::find_if(
                                original_references.edges,[&](const auto& edge) {
                                    return edge.points.size()>=2&&
                                        edge.edge_treatment_endpoint_references.size()==2&&
                                        HistoryReferenceIdentity{
                                            edge.reference.owner_id,
                                            edge.reference.semantic_key,
                                            edge.reference.instance_path}==identity;
                                });
                            if(current==original_references.edges.end())continue;
                            const bool forward=
                                squared_distance(published.points.front(),
                                    current->points.front())<=
                                    endpoint_tolerance_squared&&
                                squared_distance(published.points.back(),
                                    current->points.back())<=
                                    endpoint_tolerance_squared;
                            const bool reversed=
                                squared_distance(published.points.front(),
                                    current->points.back())<=
                                    endpoint_tolerance_squared&&
                                squared_distance(published.points.back(),
                                    current->points.front())<=
                                    endpoint_tolerance_squared;
                            if(forward)published.edge_treatment_endpoint_references=
                                current->edge_treatment_endpoint_references;
                            else if(reversed)published.edge_treatment_endpoint_references={
                                current->edge_treatment_endpoint_references[1],
                                current->edge_treatment_endpoint_references[0]};
                        }
                        // Axis endpoints are native authored datums, not OCCT
                        // body vertices. In particular, a cutting tool's endpoint
                        // may lie outside the retained solid. Publish its current
                        // prefix geometry with the exact requested boundary;
                        // topological face/edge fragments still come only from
                        // the calculated body above.
                        for(const auto& point:original_references.points) {
                            const HistoryReferenceIdentity identity{
                                point.reference.owner_id,point.reference.semantic_key,
                                point.reference.instance_path};
                            if(requested_references->contains(identity)&&
                                    !axis_endpoint_parent_key(point.reference.semantic_key).empty()&&
                                    !contains_history_reference(result.mesh.original_references,identity))
                                result.mesh.original_references.points.push_back(point);
                        }
                        result.history_reference_coverage=*requested_references;
                    }
                    return result;
                };
                if(((context.defer_terminal_publication&&
                        operation_index+1==operations.size())||
                       (final_first_boundaries&&
                        operation_index+1<operations.size()&&
                        !retained_final_first_boundaries.contains(
                            operation_index+1)))&&
                        !std::holds_alternative<FilletRequest>(operation.primitive)&&
                        !std::holds_alternative<ChamferRequest>(operation.primitive)) {
                    BodyResult deferred;
                    if(!measurement_sources.empty())
                        deferred.measurement_sources=measurement_sources;
                    return deferred;
                }
                if(context.full_output_boundaries.contains(operation_index+1))
                    return complete_request(make_operation_result(shape,faces,edges,vertices,
                        original,persist,collect,hidden,
                        calculate_body_properties));
                const bool compact_this_boundary=compact_dependency_boundaries&&
                    (!final_first_boundaries||retained_final_first_boundaries.contains(
                        operation_index+1));
                if(!compact_this_boundary||operation_index+1==operations.size())
                    return complete_request(make_operation_result(shape,faces,
                        edges,vertices,original,persist,collect,hidden,
                        calculate_body_properties));
                // Keep the current dependency edges and only the exact face,
                // edge or point identities requested by placement settling.
                // The live result_shape/owned_topology pair continues the
                // forward calculation; no complete intermediate B-Rep, face
                // mesh, aggregate properties or measurement snapshot remains.
                auto result=make_result(shape,faces,edges,vertices,original,false,false,
                    hidden,operation.mesh_deflection,false,has_forms,
                    &edge_guide_reuse,true,requested_references,
                    context.edge_output_boundaries.contains(operation_index+1));
                if(requested_references) {
                    result.kernel_shape.clear();
                    result.kernel_faces.clear();
                    result.kernel_edges.clear();
                    result.kernel_vertices.clear();
                }
                if(!measurement_sources.empty())
                    result.measurement_sources=measurement_sources;
                return complete_request(std::move(result));
            };
            const bool persist_boundary_shape =
                operation_index + 1 == operations.size();
            const auto publish_pass_through_result =
                    [&](BodyResult retained) {
                // Coverage describes this exact history output, never the
                // preceding row whose material packet is being retained.
                retained.history_reference_coverage.clear();
                const auto requested = context.reference_output_boundaries.find(
                    operation_index + 1);
                if (requested == context.reference_output_boundaries.end())
                    return retained;

                ViewerReferenceGeometry prefix;
                if(!result_shape.IsNull()) {
                    auto exact = make_published_result(result_shape,
                        owned_topology->faces, owned_topology->edges,
                        owned_topology->vertices, true,
                        persist_boundary_shape, false,
                        owned_topology->hidden_display_edges);
                    prefix = reference_geometry_for_history_references(
                        exact.mesh.original_references, requested->second);
                }
                auto missing = requested->second;
                std::erase_if(missing, [&](const auto& identity) {
                    return contains_history_reference(prefix, identity);
                });
                append_reference_geometry(prefix,
                    reference_geometry_for_history_references(
                        original_references, missing));

                // Native boundary storage is append-only.  Put the exact
                // packet first in the cumulative reference geometry so a
                // cold save can encode the following final boundary as a
                // valid delta without changing any persisted identity.
                auto remainder = reference_geometry_for_history_references(
                    original_references, requested->second, false);
                original_references = prefix;
                append_reference_geometry(
                    original_references, std::move(remainder));
                retained.mesh.original_references = std::move(prefix);
                retained.history_reference_coverage = requested->second;
                return retained;
            };
            const auto retain_copy_solid=[&](const PrimitiveData& operand,const std::vector<ViewerAxis>& additional_axes=std::vector<ViewerAxis>{}) {
                if(!context.requested_solids.contains(operation.owner_id))return;
                copy_operands.insert_or_assign(operation.owner_id,operand);
                auto solid=make_operation_result(operand.shape,operand.faces,operand.edges,operand.vertices,true,true,true);
                const auto centerlines=centerlines_for_operation(operation);
                const auto automatic_point_begin=solid.mesh.points.size();
                solid.mesh.axes=axes_for_operation(operation,operand.shape,centerlines,false,&solid.mesh.points);
                solid.mesh.original_references.points.insert(solid.mesh.original_references.points.end(),
                    solid.mesh.points.begin()+automatic_point_begin,solid.mesh.points.end());
                const auto endpoints = sweep_endpoints_for_operation(operation);
                solid.mesh.points.insert(solid.mesh.points.end(), endpoints.begin(), endpoints.end());
                solid.mesh.edges.insert(solid.mesh.edges.end(), centerlines.begin(), centerlines.end());
                append_reference_geometry(solid.mesh.original_references, operand.profile_references);
                solid.mesh.axes.insert(solid.mesh.axes.end(),operand.profile_references.axes.begin(),operand.profile_references.axes.end());
                solid.mesh.edges.insert(solid.mesh.edges.end(),operand.profile_references.edges.begin(),operand.profile_references.edges.end());
                solid.mesh.points.insert(solid.mesh.points.end(),operand.profile_references.points.begin(),operand.profile_references.points.end());
                solid.mesh.axes.insert(solid.mesh.axes.end(),additional_axes.begin(),additional_axes.end());
                solid.mesh.original_references.axes=solid.mesh.axes;
                solid.source_fingerprint=fingerprint(operations,operation_index+1)+":solid:"+operation.owner_id;
                const auto key=solid.source_fingerprint;
                const auto forms=form_copy_operands.find(operation.owner_id);
                auto [cached,inserted]=live_cache_->copy_sources.insert_or_assign(key,LiveCache::CopySource{
                    operand,solid,forms==form_copy_operands.end()?std::vector<sheet_form_geometry::Instance>{}:forms->second});
                static_cast<void>(cached);
                if(inserted)live_cache_->copy_source_order.push_back(key);
                while(live_cache_->copy_source_order.size()>kLiveShapeCacheLimit) {
                    live_cache_->copy_sources.erase(live_cache_->copy_source_order.front());
                    live_cache_->copy_source_order.pop_front();
                }
                context.source_solids.insert_or_assign(operation.owner_id,std::move(solid));
            };
            const auto apply_edge_treatment = [&](const auto& treatment,bool publish=true) {
                const bool trace_treatment=std::getenv("ZIMA_CPP_PROFILE_COMMAND")!=nullptr;
                auto treatment_stamp=trace_treatment?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
                const auto treatment_phase=[&](const char* phase){
                    if(!trace_treatment)return;
                    const auto now=std::chrono::steady_clock::now();
                    std::fprintf(stderr,"Edge treatment %s %s: %.3f ms\n",operation.owner_id.c_str(),phase,
                        std::chrono::duration<double,std::milli>(now-treatment_stamp).count());
                    treatment_stamp=now;
                };
                const TopoDS_Shape input_shape=result_shape;
                const auto input_topology=owned_topology;
                if (result_shape.IsNull()) {
                    throw std::invalid_argument(
                        "Fillet/Chamfer requires an input body");
                }
                if (treatment.edges.empty() ||
                    std::any_of(treatment.edges.begin(), treatment.edges.end(),
                        [](const auto& edge) {
                            return !edge.valid() || !edge.instance_path.empty();
                        })) {
                    throw std::invalid_argument(
                        "Fillet/Chamfer original edge reference is invalid");
                }
                using Treatment = std::decay_t<decltype(treatment)>;
                const bool has_authored_end_faces=std::ranges::any_of(
                    treatment.route_end_faces,[](const auto& ends) {
                        return ends.first.valid()||ends.last.valid();
                    });
                if(!treatment.route_edge_counts.empty()) {
                    if(treatment.route_edge_counts.size()!=
                            treatment.route_end_faces.size()||
                       std::accumulate(treatment.route_edge_counts.begin(),
                            treatment.route_edge_counts.end(),std::size_t{})!=
                            treatment.edges.size())
                        throw std::invalid_argument(
                            "Fillet/Chamfer route end-face data is invalid");
                } else if(!treatment.route_end_faces.empty()) {
                    throw std::invalid_argument(
                        "Fillet/Chamfer route end-face data is invalid");
                }
                if(has_authored_end_faces&&treatment.calculation_method==
                        EdgeTreatmentCalculationMethod::Occt)
                    throw std::invalid_argument(
                        "OCCT Fillet/Chamfer does not support selected end faces");
                if constexpr (std::is_same_v<Treatment, FilletRequest>) {
                    if (!std::isfinite(treatment.radius_start) ||
                        treatment.radius_start <= 0.0 ||
                        (treatment.mode == FilletRequest::Mode::Linear &&
                         (!std::isfinite(treatment.radius_end) ||
                          treatment.radius_end <= 0.0 ||
                          treatment.contour_start_vertices.size() !=
                              treatment.edges.size() ||
                          std::ranges::any_of(
                              treatment.contour_start_vertices,
                              [](const auto& vertex) {
                                  return !vertex.valid() ||
                                      !vertex.instance_path.empty();
                              })))) {
                        throw std::invalid_argument(
                            "Fillet radii must be finite and positive");
                    }
                } else {
                    if (!std::isfinite(treatment.distance_a) ||
                        treatment.distance_a <= 0.0 ||
                        (treatment.mode == ChamferRequest::Mode::TwoDistances &&
                         (!std::isfinite(treatment.distance_b) ||
                          treatment.distance_b <= 0.0)) ||
                        (treatment.mode == ChamferRequest::Mode::DistanceAngle &&
                         (!std::isfinite(treatment.angle_radians) ||
                          treatment.angle_radians <= 0.0 ||
                          treatment.angle_radians >= std::numbers::pi))) {
                        throw std::invalid_argument(
                            "Chamfer parameters are outside their valid range");
                    }
                }
                const auto connected_logical_rail=[&](
                        const std::vector<const OwnedEdge*>& values) {
                    std::vector<ViewerEdge> edges;
                    edges.reserve(values.size());
                    for(const auto* owned:values) {
                        if(!owned||owned->shape.ShapeType()!=TopAbs_EDGE)
                            return false;
                        TopoDS_Vertex first,last;
                        TopExp::Vertices(TopoDS::Edge(owned->shape),
                            first,last,true);
                        if(first.IsNull()||last.IsNull())return false;
                        const auto endpoint=[&](const TopoDS_Vertex& vertex)
                                ->std::optional<VertexReference> {
                            const auto found=std::ranges::find_if(
                                owned_topology->vertices,[&](const auto& value) {
                                    return value.shape.IsSame(vertex);
                                });
                            if(found==owned_topology->vertices.end()||
                               !found->reference.valid())return {};
                            return found->reference;
                        };
                        const auto first_reference=endpoint(first);
                        const auto last_reference=endpoint(last);
                        if(!first_reference||!last_reference)return false;
                        const auto a=BRep_Tool::Pnt(first);
                        const auto b=BRep_Tool::Pnt(last);
                        ViewerEdge edge;
                        edge.reference=owned->reference;
                        edge.points={{a.X(),a.Y(),a.Z()},
                                     {b.X(),b.Y(),b.Z()}};
                        edge.edge_treatment_endpoint_references={
                            *first_reference,*last_reference};
                        edges.push_back(std::move(edge));
                    }
                    std::vector<const ViewerEdge*> pointers;
                    pointers.reserve(edges.size());
                    for(const auto& edge:edges)pointers.push_back(&edge);
                    return edge_treatment_chain(pointers).has_value();
                };
                const auto resolved_treatment_identity=[&](
                        const EdgeReference& identity) {
                    std::vector<const OwnedEdge*> exact,descendants;
                    const bool requested_treatment_edge=
                        identity.semantic_key.starts_with(
                            "fillet:edge:from:")||
                        identity.semantic_key.starts_with(
                            "chamfer:edge:from:");
                    for(const auto& owned:owned_topology->edges) {
                        if(owned.reference.owner_id==identity.owner_id&&
                           owned.reference.semantic_key==identity.semantic_key)
                            exact.push_back(&owned);
                        else if(split_edge_descends_from(
                                    owned.reference,identity)&&
                                (requested_treatment_edge||
                                 (!owned.reference.semantic_key.starts_with(
                                      "fillet:edge:from:")&&
                                  !owned.reference.semantic_key.starts_with(
                                      "chamfer:edge:from:"))))
                            descendants.push_back(&owned);
                    }
                    const auto unique=[](auto& values) {
                        std::set<const OwnedEdge*> seen;
                        std::erase_if(values,[&](const auto* value) {
                            return !seen.insert(value).second;
                        });
                    };
                    unique(exact);unique(descendants);
                    // Explicit split descendants are the actual surviving
                    // intervals. Prefer them if a transitional topology ever
                    // carries both the old parent alias and its children;
                    // otherwise use the unchanged edge. No geometric proximity,
                    // enumeration order or measured dimension participates.
                    return descendants.empty()?exact:descendants;
                };
                const auto resolved_treatment_edges=[&](
                        const EdgeReference& requested) {
                    auto result=resolved_treatment_identity(requested);
                    if(const auto counterpart=
                            feature_side_rail_counterpart(requested);
                       counterpart&&std::ranges::find(treatment.edges,
                            *counterpart)==treatment.edges.end()) {
                        auto opposite=resolved_treatment_identity(*counterpart);
                        if(result.empty())return opposite;
                        auto combined=result;
                        combined.insert(combined.end(),opposite.begin(),
                            opposite.end());
                        std::set<const OwnedEdge*> seen;
                        std::erase_if(combined,[&](const auto* edge) {
                            return !seen.insert(edge).second;
                        });
                        if(!opposite.empty()&&connected_logical_rail(combined))
                            return combined;
                    }
                    return result;
                };
                std::vector<std::pair<TopoDS_Edge, EdgeReference>> selected;
                std::vector<std::size_t> selected_requested_indices;
                std::vector<std::size_t> requested_routes;
                requested_routes.reserve(treatment.edges.size());
                if(treatment.route_edge_counts.empty()) {
                    for(std::size_t index=0;index<treatment.edges.size();++index)
                        requested_routes.push_back(index);
                } else {
                    for(std::size_t route=0;
                            route<treatment.route_edge_counts.size();++route)
                        requested_routes.insert(requested_routes.end(),
                            treatment.route_edge_counts[route],route);
                }
                std::vector<std::size_t> resolved_per_route(
                    treatment.route_edge_counts.empty()
                        ?treatment.edges.size()
                        :treatment.route_edge_counts.size());
                // A direct Add block intentionally publishes a new canonical
                // boundary identity.  A later treatment can still persist an
                // older Boolean or treatment alias which is recovered from
                // exact face ancestry below.  Make that proven alias visible
                // to contour validation without changing the canonical input
                // topology or deriving identity from OCCT contour order.
                auto contour_available_edges=owned_topology->edges;
                std::vector<VertexReference> selected_starts;
                std::optional<std::vector<OcctBoundaryFace>>
                    structural_face_ancestry;
                const auto current_face_ancestry=[&]()
                        ->const std::vector<OcctBoundaryFace>& {
                    if(structural_face_ancestry)
                        return *structural_face_ancestry;
                    TopTools_IndexedMapOfShape faces;
                    TopExp::MapShapes(result_shape,TopAbs_FACE,faces);
                    structural_face_ancestry.emplace(
                        static_cast<std::size_t>(faces.Extent()));
                    for(int index=1;index<=faces.Extent();++index)
                        (*structural_face_ancestry)[
                            static_cast<std::size_t>(index-1)].shape=
                                TopoDS::Face(faces(index));
                    for(const auto& owned:owned_topology->faces) {
                        const int index=faces.FindIndex(owned.shape);
                        if(!index||!owned.reference.valid())continue;
                        auto& references=(*structural_face_ancestry)[
                            static_cast<std::size_t>(index-1)].references;
                        if(std::ranges::find(references,owned.reference)==
                           references.end())
                            references.push_back(owned.reference);
                    }
                    return *structural_face_ancestry;
                };
                const auto structurally_resolved_history_edge=[&](
                        const EdgeReference& requested)
                        ->std::optional<TopoDS_Edge> {
                    const bool boolean_intersection=
                        requested.semantic_key.starts_with(
                            "boolean:add:intersection:between:");
                    const bool edge_treatment=
                        requested.semantic_key.starts_with(
                            "fillet:edge:from:")||
                        requested.semantic_key.starts_with(
                            "chamfer:edge:from:");
                    if(!boolean_intersection&&!edge_treatment)
                        return {};
                    const auto& ancestry=current_face_ancestry();
                    const auto resolved=boolean_intersection
                        ?occt_resolve_add_intersection_edges(
                            result_shape,ancestry,
                            std::span(&requested,1),
                            std::max(1.0e-7,
                                operation.boolean_tolerance))
                        :occt_resolve_edge_treatment_edges(
                            result_shape,ancestry,
                            std::span(&requested,1),
                            std::max(1.0e-7,
                                operation.boolean_tolerance));
                    if(resolved.size()!=1)return {};
                    return resolved.front().shape;
                };
                std::size_t resolved_requests{};
                for (std::size_t requested_index=0;requested_index<treatment.edges.size();++requested_index) {
                    const auto& requested=treatment.edges[requested_index];
                    const auto exact=resolved_treatment_edges(requested);
                    bool resolved_request=!exact.empty();
                    for(const auto* owned:exact) {
                        selected.emplace_back(
                            TopoDS::Edge(owned->shape),owned->reference);
                        selected_requested_indices.push_back(requested_index);
                        // One persisted edge can resolve to several shape uses
                        // or split descendants. Carry its explicit ZIMA R1 with
                        // every match; runtime expansion must not shift the next
                        // route's endpoint or read past it.
                        if constexpr (std::is_same_v<Treatment, FilletRequest>) {
                            if(treatment.mode==FilletRequest::Mode::Linear)
                                selected_starts.push_back(
                                    treatment.contour_start_vertices.at(
                                        requested_index));
                        }
                    }
                    if(exact.empty())try {
                        if(auto edge=structurally_resolved_history_edge(
                                requested)) {
                            selected.emplace_back(*edge,requested);
                            selected_requested_indices.push_back(
                                requested_index);
                            if(std::ranges::none_of(contour_available_edges,
                                    [&](const auto& available) {
                                        return available.shape.IsSame(*edge)&&
                                            available.reference==requested;
                                    }))
                                contour_available_edges.push_back(
                                    {*edge,requested});
                            resolved_request=true;
                            if(trace_treatment)std::fprintf(stderr,
                                "Edge treatment %s resolved structural ancestry owner=%s\n",
                                operation.owner_id.c_str(),
                                requested.owner_id.c_str());
                            if constexpr(std::is_same_v<Treatment,
                                             FilletRequest>)
                                if(treatment.mode==FilletRequest::Mode::Linear)
                                    selected_starts.push_back(
                                        treatment.contour_start_vertices.at(
                                            requested_index));
                        }
                    } catch(const std::invalid_argument& error) {
                        // A definition outside this exact ancestry grammar
                        // retains the established absent-reference behavior.
                        if(trace_treatment)std::fprintf(stderr,
                            "Edge treatment %s structural ancestry unavailable: %s\n",
                            operation.owner_id.c_str(),error.what());
                    } catch(const std::runtime_error& error) {
                        // Zero or several structural matches are not guessed.
                        if(trace_treatment)std::fprintf(stderr,
                            "Edge treatment %s structural ancestry unresolved: %s\n",
                            operation.owner_id.c_str(),error.what());
                    }
                    if(resolved_request)
                        ++resolved_per_route[requested_routes[requested_index]];
                    resolved_requests+=resolved_request;
                }
                if(context.direct_batch_active&&
                   resolved_requests!=treatment.edges.size())
                    throw DirectBatchReferenceFallback{};
                if(resolved_requests!=treatment.edges.size()) {
                    bool retain_surviving_fragments=false;
                    if constexpr(std::is_same_v<Treatment,FilletRequest>)
                        retain_surviving_fragments=
                            treatment.mode==FilletRequest::Mode::Constant&&
                            treatment.calculation_method!=
                                EdgeTreatmentCalculationMethod::Occt&&
                            std::ranges::none_of(resolved_per_route,
                                [](const auto count) {return count==0;});
                    if(!retain_surviving_fragments)
                        throw std::invalid_argument(
                            "Fillet/Chamfer original edge reference is unresolved.");
                }
                if(trace_treatment)std::fprintf(stderr,
                    "Edge treatment %s requested=%zu resolved=%zu\n",
                    operation.owner_id.c_str(),treatment.edges.size(),
                    selected.size());
                const auto authored_selected=selected;
                const auto actual_intersection_selection=[&]() {
                    std::vector<std::pair<TopoDS_Edge,EdgeReference>> actual;
                    for(const auto& seed:selected)
                        if(is_boolean_intersection_interval(seed.second))
                            actual.push_back(seed);
                    return actual;
                };
                if constexpr (!std::is_same_v<Treatment,FilletRequest>) {
                    if(auto actual=actual_intersection_selection();
                       !actual.empty())selected=std::move(actual);
                } else if(treatment.mode==FilletRequest::Mode::Constant&&
                          treatment.calculation_method==
                              EdgeTreatmentCalculationMethod::Occt) {
                    if(auto actual=actual_intersection_selection();
                       !actual.empty())selected=std::move(actual);
                }
                std::vector<OcctPlanarEdgeEndFaces> custom_end_faces(
                    selected.size());
                if(has_authored_end_faces) {
                    const auto route_for=[&](const EdgeReference& reference)
                            ->std::optional<std::size_t> {
                        for(std::size_t index=0;
                                index<treatment.edges.size();++index)
                            if(reference==treatment.edges[index]||
                               split_edge_descends_from(reference,
                                   treatment.edges[index])||
                               same_feature_side_rail(reference,
                                   treatment.edges[index]))
                                return requested_routes[index];
                        return {};
                    };
                    for(std::size_t route=0;
                            route<treatment.route_end_faces.size();++route) {
                        for(const auto* authored:{
                                &treatment.route_end_faces[route].first,
                                &treatment.route_end_faces[route].last}) {
                            if(!authored->valid())continue;
                            if(!authored->instance_path.empty())
                                throw std::invalid_argument(
                                    "Fillet/Chamfer end face must be a local persisted reference");
                            struct Match {std::size_t edge;bool first;
                                TopoDS_Face face;};
                            std::vector<Match> matches;
                            for(const auto& owned:owned_topology->faces) {
                                if(owned.reference!=*authored)continue;
                                const auto face=TopoDS::Face(owned.shape);
                                const BRepAdaptor_Surface surface(face,true);
                                if(surface.GetType()!=GeomAbs_Plane)continue;
                                for(std::size_t edge_index=0;
                                        edge_index<selected.size();++edge_index) {
                                    const auto selected_route=route_for(
                                        selected[edge_index].second);
                                    if(!selected_route||*selected_route!=route)
                                        continue;
                                    const BRepAdaptor_Curve curve(
                                        selected[edge_index].first);
                                    for(const bool first_endpoint:{true,false}) {
                                        const auto parameter=first_endpoint
                                            ?curve.FirstParameter():
                                             curve.LastParameter();
                                        const auto endpoint=curve.Value(parameter);
                                        if(surface.Plane().Distance(endpoint)>
                                                std::max(1.0e-7,
                                                    operation.boolean_tolerance))
                                            continue;
                                        BRepClass_FaceClassifier classifier(
                                            face,endpoint,
                                            std::max(1.0e-7,
                                                operation.boolean_tolerance));
                                        if(classifier.State()==TopAbs_IN||
                                           classifier.State()==TopAbs_ON)
                                            matches.push_back({edge_index,
                                                first_endpoint,face});
                                    }
                                }
                            }
                            if(matches.size()!=1)
                                throw std::invalid_argument(
                                    "Fillet/Chamfer end face must contain exactly one endpoint of its route");
                            auto& target=custom_end_faces[matches.front().edge];
                            auto& slot=matches.front().first
                                ?target.first:target.last;
                            if(!slot.IsNull()&&!slot.IsSame(matches.front().face))
                                throw std::invalid_argument(
                                    "Fillet/Chamfer route endpoint has conflicting end faces");
                            slot=matches.front().face;
                        }
                    }
                }
                const auto accept_edge_treatment_result=[&](auto& built,
                        const auto& parents,
                        const std::string& treatment_owner,
                        std::string_view treatment_role,
                        bool preserve_exact_result,
                        const char* invalid_body_message) {
                    const auto role=[&](std::string_view topology) {
                        auto value=std::string(treatment_role);
                        value+=':';
                        value+=topology;
                        return value;
                    };
                    auto treatment_faces=propagate_topology(
                        built,owned_topology->faces,
                        std::vector<OwnedFace>{});
                    treatment_phase("validation and face propagation");
                    auto generated_faces=generated_edge_treatment_faces(
                        built,parents,treatment_owner,role("face"));
                    treatment_faces.insert(treatment_faces.end(),
                        std::make_move_iterator(generated_faces.begin()),
                        std::make_move_iterator(generated_faces.end()));
                    append_unmapped_edge_treatment_faces(
                        built.Shape(),parents,owned_topology->faces,
                        treatment_owner,role("face"),
                        std::max(1.0e-7,operation.boolean_tolerance),
                        treatment_faces);
                    treatment_phase("generated and unmapped faces");
                    classify_edge_treatment_surfaces(
                        built.Shape(),treatment_owner,treatment_faces);
                    treatment_phase("surface classification");
                    auto treatment_topology=
                        std::make_shared<LiveCache::Topology>(
                            LiveCache::Topology{
                                std::move(treatment_faces),
                                propagate_edge_treatment_topology(
                                    built,built.Shape(),
                                    owned_topology->edges),
                                propagate_edge_treatment_topology(
                                    built,built.Shape(),
                                    owned_topology->vertices),
                                propagate_display_edges(
                                    built,owned_topology
                                        ->hidden_display_edges)});
                    treatment_phase("edge and vertex propagation");
                    result_shape=built.Shape();
                    auto unified=preserve_exact_result
                        ?ProvenanceUnifyResult{
                            result_shape,
                            std::move(treatment_topology->faces),
                            std::move(treatment_topology->edges),
                            std::move(treatment_topology->vertices),
                            std::move(treatment_topology
                                ->hidden_display_edges),
                            false}
                        :unify_preserving_face_provenance(
                            result_shape,treatment_topology->faces,
                            treatment_topology->edges,
                            treatment_topology->vertices,
                            std::max(1.0e-7,
                                operation.boolean_tolerance));
                    treatment_phase("unify");
                    if(!unified.validated&&
                       (unified.shape.IsNull()||
                        !BRepCheck_Analyzer(
                            unified.shape,true,true,false).IsValid()))
                        throw std::runtime_error(invalid_body_message);
                    treatment_phase("fallback validation");
                    result_shape=std::move(unified.shape);
                    auto completed_edges=complete_edge_treatment_edges(
                        result_shape,unified.faces,unified.edges,
                        owned_topology->edges,parents,treatment_owner,
                        role("edge"));
                    treatment_phase("complete edges");
                    auto completed_vertices=
                        complete_edge_treatment_vertices(
                            result_shape,completed_edges,
                            unified.vertices,parents,treatment_owner,
                            role("vertex"));
                    treatment_phase("complete vertices");
                    owned_topology=
                        std::make_shared<LiveCache::Topology>(
                            LiveCache::Topology{
                                std::move(unified.faces),
                                std::move(completed_edges),
                                std::move(completed_vertices),
                                std::move(
                                    unified.hidden_display_edges)});
                };
                if constexpr (std::is_same_v<Treatment, FilletRequest>) {
                    treatment_phase("selection");
                    BRepFilletAPI_MakeFillet algorithm(result_shape);
                    // FORM's internal transitions use exact rational circular
                    // parameterization; ordinary Fillet keeps its established
                    // default. No radius or approximation tolerance changes.
                    if(!publish)
                        algorithm.SetFilletShape(ChFi3d_QuasiAngular);
                    // Preserve OCCT's angular, UV and marching parameters;
                    // only spatial and 3D approximation tolerances are mm.
                    const double fillet_tolerance = publish?std::max(1.0e-7, operation.boolean_tolerance):1.0e-7;
                    algorithm.SetParams(1.0e-2, fillet_tolerance, 1.0e-5,
                        fillet_tolerance, 1.0e-5, 1.0e-3);
                    const TopologyReferenceIndex<VertexReference, OwnedVertex>
                        vertex_references(feature_endpoint_vertices(result_shape,
                            owned_topology->faces,owned_topology->edges,owned_topology->vertices));
                    for (std::size_t selected_index = 0;
                         selected_index < selected.size(); ++selected_index) {
                        const auto& [edge, reference] = selected[selected_index];
                        static_cast<void>(reference);
                        // OCCT expands one seed to its tangent contour. The
                        // document nevertheless persists every stable ZIMA
                        // edge in that route so generated topology can retain
                        // all parents. Do not add the same OCCT contour twice.
                        if (algorithm.Contour(edge) == 0) {
                            if (treatment.mode == FilletRequest::Mode::Constant) {
                                algorithm.Add(treatment.radius_start, edge);
                            } else {
                                // OCCT defines R1/R2 on its internally built
                                // spine, not on TopoDS_Edge orientation. Add
                                // the contour first, query its actual ends,
                                // then map the persisted ZIMA R1 endpoint to
                                // that order before assigning the law.
                                algorithm.Add(edge);
                                const int contour = algorithm.Contour(edge);
                                if (contour <= 0) {
                                    throw std::runtime_error(
                                        "OCCT did not create a Fillet contour");
                                }
                                const auto first_reference =
                                    vertex_references.reference_for(
                                        algorithm.FirstVertex(contour));
                                const auto last_reference =
                                    vertex_references.reference_for(
                                        algorithm.LastVertex(contour));
                                if (!first_reference.valid() ||
                                    !last_reference.valid() ||
                                    first_reference == last_reference) {
                                    throw std::runtime_error(
                                        "Variable Fillet edge endpoints have no stable ZIMA identity");
                                }
                                const auto& semantic_start =
                                    selected_starts.at(selected_index);
                                const bool semantic_start_is_first =
                                    semantic_start == first_reference;
                                if (!semantic_start_is_first &&
                                    semantic_start != last_reference) {
                                    throw std::runtime_error(
                                        "Variable Fillet R1 endpoint is not on the OCCT contour");
                                }
                                const bool r1_is_occt_first =
                                    semantic_start_is_first != treatment.reverse;
                                // The indexed overload applies the law to one
                                // constituent edge. Attach radii to the actual
                                // contour endpoints so a multi-edge rail has
                                // one law over its complete physical length.
                                algorithm.SetRadius(
                                    r1_is_occt_first
                                        ? treatment.radius_start
                                        : treatment.radius_end,
                                    contour, algorithm.FirstVertex(contour));
                                algorithm.SetRadius(
                                    r1_is_occt_first
                                        ? treatment.radius_end
                                        : treatment.radius_start,
                                    contour, algorithm.LastVertex(contour));
                            }
                        }
                    }
                    const auto selected_seeds=selected;
                    const auto selected_seed_requests=
                        selected_requested_indices;
                    const auto accept_fillet_result=[&](auto& built,
                            const auto& parents,
                            const std::string& treatment_owner) {
                        accept_edge_treatment_result(built,parents,
                            treatment_owner,"fillet",true,
                            "OCCT Fillet produced an invalid body");
                    };
                    bool custom_planar_accepted=false;
                    const char* custom_planar_environment=std::getenv(
                        "ZIMA_CPP_EXPERIMENTAL_PLANAR_FILLET");
                    const bool custom_planar_environment_requested=
                        custom_planar_environment&&
                        std::string_view(custom_planar_environment)!="0"&&
                        (std::string_view(custom_planar_environment)=="1"||
                         std::string_view(custom_planar_environment)==
                            operation.owner_id);
                    const bool custom_planar_requested=
                        treatment.calculation_method!=
                            EdgeTreatmentCalculationMethod::Occt||
                        custom_planar_environment_requested;
                    const bool try_custom_planar=
                        treatment.mode==FilletRequest::Mode::Constant&&
                        custom_planar_requested;
                    // ZIMA consumes only the exact route fragments persisted
                    // by the document.  Do not make its construction depend
                    // on OCCT accepting or expanding the same seed into one
                    // of its contours.  Contour resolution belongs solely to
                    // the OCCT fallback below.
                    if(!try_custom_planar) {
                        selected=resolve_edge_treatment_contours(
                            algorithm,selected,contour_available_edges,
                            "Fillet",operation.owner_id);
                        treatment_phase("contours");
                    }
                    if(treatment.calculation_method==
                            EdgeTreatmentCalculationMethod::Zima&&
                       treatment.mode!=FilletRequest::Mode::Constant)
                        throw std::invalid_argument(
                            "ZIMA Fillet currently supports only a constant radius; use OCCT for a variable radius");
                    if(try_custom_planar) {
                        try {
                            std::vector<TopoDS_Edge> custom_edges;
                            std::vector<OcctPlanarEdgeEndFaces> custom_ends;
                            std::vector<OcctPlanarEdgeSupport>
                                custom_supports;
                            std::vector<std::size_t> custom_route_counts;
                            std::vector<std::pair<TopoDS_Edge,EdgeReference>>
                                custom_parents;
                            std::vector<std::pair<TopoDS_Edge,EdgeReference>>
                                custom_seeds;
                            std::vector<std::size_t> custom_seed_requests;
                            std::vector<OcctPlanarEdgeEndFaces>
                                custom_seed_ends;
                            std::vector<OcctPlanarEdgeSupport>
                                custom_seed_supports;
                            for(std::size_t requested_index=0;
                                    requested_index<treatment.edges.size();
                                    ++requested_index) {
                                for(std::size_t selected_index=0;
                                        selected_index<selected_seeds.size();
                                        ++selected_index)
                                    if(selected_seed_requests[
                                            selected_index]==
                                            requested_index) {
                                        custom_seeds.push_back(
                                            selected_seeds[selected_index]);
                                        custom_seed_requests.push_back(
                                            requested_index);
                                        custom_seed_ends.push_back(
                                            custom_end_faces[selected_index]);
                                        custom_seed_supports.emplace_back();
                                    }
                            }
                            std::optional<std::size_t> custom_route;
                            const auto shares_endpoint=[](
                                    const TopoDS_Edge& first,
                                    const TopoDS_Edge& second) {
                                TopTools_IndexedMapOfShape first_vertices,
                                    second_vertices;
                                TopExp::MapShapes(first,TopAbs_VERTEX,
                                    first_vertices);
                                TopExp::MapShapes(second,TopAbs_VERTEX,
                                    second_vertices);
                                for(int index=1;
                                        index<=first_vertices.Extent();++index)
                                    if(second_vertices.Contains(
                                            first_vertices.FindKey(index)))
                                        return true;
                                return false;
                            };
                            TopTools_IndexedDataMapOfShapeListOfShape
                                custom_edge_faces;
                            TopExp::MapShapesAndAncestors(result_shape,
                                TopAbs_EDGE,TopAbs_FACE,custom_edge_faces);
                            const auto is_smooth_boundary=[&](
                                    std::size_t seed_index) {
                                const auto& edge=custom_seeds[seed_index].first;
                                const auto& source=
                                    custom_seed_supports[seed_index];
                                if(!source.material.IsNull())
                                    return BRep_Tool::Continuity(edge,
                                        source.first,source.second)>=
                                            GeomAbs_G1;
                                const int index=custom_edge_faces.FindIndex(
                                    edge);
                                if(!index)return false;
                                std::vector<TopoDS_Face> faces;
                                for(const auto& value:
                                        custom_edge_faces.FindFromIndex(index)) {
                                    const auto face=TopoDS::Face(value);
                                    if(std::ranges::none_of(faces,
                                            [&](const auto& current) {
                                                return current.IsSame(face);
                                            }))faces.push_back(face);
                                }
                                return faces.size()==2&&
                                    BRep_Tool::Continuity(edge,faces[0],
                                        faces[1])>=GeomAbs_G1;
                            };
                            const bool has_nonsmooth_fragment=
                                std::ranges::any_of(
                                    std::views::iota(std::size_t{},
                                        custom_seeds.size()),
                                    [&](const auto seed_index) {
                                        return !is_smooth_boundary(
                                            seed_index);
                                    });
                            // ZIMA consumes the explicitly persisted route.
                            // OCCT contour expansion may continue through a
                            // tangent-return cusp and is reserved for the OCCT
                            // fallback below; feeding those extra edges to the
                            // custom builder changes the user's selection.
                            for(std::size_t selected_index=0;
                                    selected_index<custom_seeds.size();
                                    ++selected_index) {
                                const auto& [edge,reference]=
                                    custom_seeds[selected_index];
                                // A persisted source curve can be split by a
                                // later Boolean into a real corner and an old
                                // G1 continuation.  The smooth fragment has no
                                // material wedge and must not lengthen the
                                // selected ZIMA route past the actual corner.
                                if(has_nonsmooth_fragment&&
                                   is_smooth_boundary(selected_index))continue;
                                if(std::ranges::none_of(custom_edges,
                                        [&](const auto& current) {
                                            return current.IsSame(edge);
                                        })) {
                                    const auto requested_index=
                                        custom_seed_requests.at(
                                            selected_index);
                                    const auto route_index=requested_routes.at(
                                        requested_index);
                                    const bool continues=custom_route&&
                                        *custom_route==route_index&&
                                        !custom_edges.empty()&&
                                        shares_endpoint(custom_edges.back(),
                                            edge);
                                    if(!continues) {
                                        custom_route_counts.push_back(1);
                                        custom_route=route_index;
                                    } else {
                                        ++custom_route_counts.back();
                                    }
                                    custom_edges.push_back(edge);
                                    custom_ends.push_back(
                                        custom_seed_ends[selected_index]);
                                    custom_supports.push_back(
                                        custom_seed_supports[
                                            selected_index]);
                                    custom_parents.push_back(
                                        custom_seeds[selected_index]);
                                }
                            }
                            if(custom_edges.empty())
                                throw std::invalid_argument(
                                    "ZIMA Fillet route contains only smooth boundary fragments");
                            auto custom=occt_planar_edge_fillet(
                                result_shape,custom_edges,
                                treatment.radius_start,fillet_tolerance,
                                custom_ends,
                                treatment.calculation_method!=
                                        EdgeTreatmentCalculationMethod::Occt
                                    ?OcctPlanarEdgeFilletEndMode::
                                        AutomaticBodyEnvelope
                                    :OcctPlanarEdgeFilletEndMode::
                                        Perpendicular,
                                custom_route_counts,custom_supports);
                            if(trace_treatment)std::fprintf(stderr,
                                "Edge treatment %s custom planar Fillet edges=%zu tools=%zu preparation=%.3f ms boolean=%.3f ms validation=%.3f ms\n",
                                operation.owner_id.c_str(),custom_edges.size(),
                                custom.metrics.cutter_solids,
                                custom.metrics.preparation_ms,
                                custom.metrics.boolean_ms,
                                custom.metrics.validation_ms);
                            accept_fillet_result(custom,custom_parents,
                                operation.owner_id);
                            custom_planar_accepted=true;
                            treatment_phase("custom planar build");
                        } catch(const std::exception& error) {
                            if(trace_treatment)std::fprintf(stderr,
                                "Edge treatment %s custom planar Fillet fallback: %s\n",
                                operation.owner_id.c_str(),error.what());
                            if(treatment.calculation_method==
                                    EdgeTreatmentCalculationMethod::Zima||
                               has_authored_end_faces)
                                throw std::runtime_error(
                                    std::string("ZIMA Fillet rejected the selected geometry: ")+
                                    error.what());
                        }
                    }
                    if(!custom_planar_accepted) {
                    if(try_custom_planar) {
                        selected=resolve_edge_treatment_contours(
                            algorithm,selected_seeds,
                            contour_available_edges,"Fillet",
                            operation.owner_id);
                        treatment_phase("contours");
                    }
                    algorithm.Build();
                    treatment_phase("build");
                    if(trace_treatment&&
                       (!algorithm.IsDone()||algorithm.Shape().IsNull())) {
                        std::fprintf(stderr,
                            "Edge treatment %s Fillet failure: contours=%d faulty-contours=%d faulty-vertices=%d\n",
                            operation.owner_id.c_str(),algorithm.NbContours(),
                            algorithm.NbFaultyContours(),
                            algorithm.NbFaultyVertices());
                        for(int faulty=1;faulty<=algorithm.NbFaultyContours();
                                ++faulty) {
                            const int contour=algorithm.FaultyContour(faulty);
                            std::fprintf(stderr,
                                "Edge treatment %s Fillet faulty contour=%d status=%d edges=%d\n",
                                operation.owner_id.c_str(),contour,
                                static_cast<int>(algorithm.StripeStatus(contour)),
                                algorithm.NbEdges(contour));
                        }
                    }
                    if(!publish&&std::getenv("ZIMA_CPP_FORM_PROFILE")&&
                       (!algorithm.IsDone()||algorithm.Shape().IsNull()||!BRepCheck_Analyzer(algorithm.Shape()).IsValid()))
                    {
                        std::fprintf(stderr,"FORM transition failure: done=%d contours=%d faulty=%d vertices=%d\n",
                            int(algorithm.IsDone()),algorithm.NbContours(),algorithm.NbFaultyContours(),algorithm.NbFaultyVertices());
                        for(int i=1;i<=algorithm.NbFaultyContours();++i) {
                            const auto contour=algorithm.FaultyContour(i);
                            std::fprintf(stderr,"FORM faulty contour=%d status=%d edges=%d\n",contour,int(algorithm.StripeStatus(contour)),algorithm.NbEdges(contour));
                        }
                    }
                    if (algorithm.IsDone() && !algorithm.Shape().IsNull()) {
                        accept_fillet_result(
                            algorithm,selected,operation.owner_id);
                    } else {
                        std::vector<std::pair<TopoDS_Edge,EdgeReference>>
                            intersection_seeds;
                        for(const auto& seed:selected_seeds)
                            if(is_boolean_intersection_interval(seed.second))
                                intersection_seeds.push_back(seed);
                        if(trace_treatment)std::fprintf(stderr,
                            "Edge treatment %s bounded intersection seeds=%zu selected=%zu\n",
                            operation.owner_id.c_str(),
                            intersection_seeds.size(),selected_seeds.size());
                        bool recovered_from_bounded_source=false;
                        if(!bounded_fillet_source&&
                           treatment.mode==FilletRequest::Mode::Constant&&
                           authored_selected.size()==1&&operation_index>0) {
                            const auto source_index=operation_index-1;
                            const auto& source_operation=
                                operations[source_index];
                            if(source_operation.owner_id==
                                   authored_selected.front().second.owner_id&&
                               source_operation.operation==
                                   BooleanOperation::Add&&
                               std::holds_alternative<FeatureGroupRequest>(
                                   source_operation.primitive)) {
                                OcctKernel source_kernel;
                                BodyResult references;
                                TopoDS_Shape base_shape;
                                auto base_topology=std::make_shared<const
                                    LiveCache::Topology>();
                                if(source_index>0) {
                                    const auto base_key=fingerprint(
                                        operations,source_index);
                                    const auto cached=
                                        live_cache_->boundaries.find(base_key);
                                    if(cached!=live_cache_->boundaries.end()&&
                                       source_index<=
                                           previous_boundaries.size()) {
                                        // A live edit already owns the exact
                                        // pre-feature B-Rep and topology. Reuse
                                        // that bounded input; rebuilding its
                                        // complete history would defeat the
                                        // incremental Fillet correction.
                                        references=previous_boundaries[
                                            source_index-1];
                                        base_shape=cached->second.shape;
                                        base_topology=
                                            cached->second.topology;
                                    } else {
                                        const std::vector<HistoryOperation>
                                            prefix(operations.begin(),
                                                operations.begin()+
                                                static_cast<std::ptrdiff_t>(
                                                    source_index));
                                        auto calculated=
                                            source_kernel.evaluate_history(
                                                prefix);
                                        if(calculated.size()!=source_index)
                                            throw std::runtime_error(
                                                "Bounded Fillet source prefix did not calculate");
                                        references=
                                            std::move(calculated.back());
                                        auto base=bound_face_geometry(
                                            references,true);
                                        base_shape=base.shape;
                                        base_topology=std::make_shared<const
                                            LiveCache::Topology>(
                                                LiveCache::Topology{
                                                    std::move(base.faces),
                                                    std::move(base.edges),
                                                    std::move(base.vertices),
                                                    {}});
                                    }
                                }
                                auto source_result=
                                    source_kernel.calculate_feature_tool(
                                        source_operation,references,{},
                                        references,{});
                                auto source_operand=bound_face_geometry(
                                    source_result,true);
                                bounded_fillet_source=
                                    BoundedFilletSource{
                                        source_operation.owner_id,
                                        std::move(source_operand),
                                        std::move(base_shape),
                                        std::move(base_topology),
                                        source_operation.boolean_tolerance};
                            }
                        }
                        if(treatment.mode==FilletRequest::Mode::Constant&&
                           authored_selected.size()==1&&
                           bounded_fillet_source&&
                           bounded_fillet_source->owner==
                               authored_selected.front().second.owner_id) {
                            auto& source=*bounded_fillet_source;
                            std::vector<
                                std::pair<TopoDS_Edge,EdgeReference>>
                                local_seeds;
                            for(const auto& edge:source.operand.edges)
                                if(edge.reference==
                                   authored_selected.front().second)
                                    local_seeds.emplace_back(
                                        TopoDS::Edge(edge.shape),
                                        edge.reference);
                            if(!local_seeds.empty()) {
                                BRepFilletAPI_MakeFillet local(
                                    source.operand.shape);
                                local.SetParams(1.0e-2,fillet_tolerance,
                                    1.0e-5,fillet_tolerance,1.0e-5,1.0e-3);
                                for(const auto& [edge,reference]:local_seeds) {
                                    static_cast<void>(reference);
                                    if(local.Contour(edge)==0)
                                        local.Add(
                                            treatment.radius_start,edge);
                                }
                                auto local_parents=
                                    resolve_edge_treatment_contours(
                                        local,local_seeds,
                                        source.operand.edges,"Fillet",
                                        operation.owner_id);
                                local.Build();
                                if(local.IsDone()&&
                                   !local.Shape().IsNull()) {
                                    result_shape=source.operand.shape;
                                    owned_topology=std::make_shared<
                                        LiveCache::Topology>(
                                            LiveCache::Topology{
                                                source.operand.faces,
                                                source.operand.edges,
                                                source.operand.vertices,{}});
                                    accept_fillet_result(local,local_parents,
                                        operation.owner_id);
                                    if(!source.base_shape.IsNull()) {
                                        BRepAlgoAPI_Fuse fuse;
                                        set_boolean_inputs(fuse,
                                            source.base_shape,result_shape);
                                        fuse.SetToFillHistory(true);
                                        fuse.SetFuzzyValue(std::max(1.0e-7,
                                            source.boolean_tolerance));
                                        fuse.Build();
                                        if(!fuse.IsDone()||
                                           fuse.Shape().IsNull())
                                            throw std::runtime_error(
                                                "Bounded Fillet source Fuse failed");
                                        auto fused=std::make_shared<
                                            LiveCache::Topology>(
                                                LiveCache::Topology{
                                                    propagate_topology(fuse,
                                                        source.base_topology->faces,
                                                        owned_topology->faces),
                                                    propagate_topology(fuse,
                                                        source.base_topology->edges,
                                                        owned_topology->edges),
                                                    propagate_topology(fuse,
                                                        source.base_topology->vertices,
                                                        owned_topology->vertices),
                                                    propagate_display_edges(
                                                        fuse,source.base_topology->
                                                            hidden_display_edges)});
                                        result_shape=fuse.Shape();
                                        auto unified=
                                            unify_preserving_face_provenance(
                                                result_shape,fused->faces,
                                                fused->edges,fused->vertices,
                                                std::max(1.0e-7,
                                                    source.boolean_tolerance),
                                                true);
                                        if(!unified.validated&&
                                           (unified.shape.IsNull()||
                                            !BRepCheck_Analyzer(unified.shape,
                                                true,true,false).IsValid()))
                                            throw std::runtime_error(
                                                "Bounded Fillet source Fuse produced an invalid body");
                                        result_shape=std::move(unified.shape);
                                        unified.faces=complete_boolean_faces(
                                            result_shape,unified.faces,
                                            unified.edges,unified.vertices,
                                            source.owner,"add",operations);
                                        unified.edges=complete_boolean_edges(
                                            result_shape,unified.faces,
                                            unified.edges,source.owner,"add");
                                        unified.vertices=
                                            complete_boolean_vertices(
                                                result_shape,unified.faces,
                                                unified.edges,unified.vertices,
                                                source.owner,"add");
                                        owned_topology=std::make_shared<
                                            LiveCache::Topology>(
                                                LiveCache::Topology{
                                                    std::move(unified.faces),
                                                    std::move(unified.edges),
                                                    std::move(unified.vertices),
                                                    std::move(unified.
                                                        hidden_display_edges)});
                                    }
                                    recovered_from_bounded_source=true;
                                }
                            }
                        }
                        if(recovered_from_bounded_source) {
                            treatment_phase("bounded source recovery");
                        } else if(treatment.mode!=
                                      FilletRequest::Mode::Constant||
                                  (intersection_seeds.empty()
                                      ?selected_seeds.size()
                                      :intersection_seeds.size())<2) {
                            throw std::runtime_error("OCCT Fillet failed");
                        } else {
                            // Independent contours can intersect at their
                            // endpoint patches even though each bounded edge is
                            // valid by itself. Use a deterministic reverse
                            // authored order so a later interval is not
                            // swallowed by an earlier endpoint patch. Publish
                            // one boundary after all steps. Successful combined
                            // corner blending remains the primary path.
                            const auto& bounded_seeds=
                                intersection_seeds.empty()
                                    ?selected_seeds:intersection_seeds;
                            for(auto seed_iterator=bounded_seeds.rbegin();
                                seed_iterator!=bounded_seeds.rend();
                                ++seed_iterator) {
                                const auto& seed=*seed_iterator;
                                const auto patch_owner=operation.owner_id+
                                    ":bounded:"+compact_topology_token(
                                        encoded_topology_reference(
                                            seed.second));
                                std::vector<std::pair<
                                    TopoDS_Edge,EdgeReference>> step_seeds;
                                for(const auto* owned:
                                        resolved_treatment_edges(
                                            seed.second))
                                    step_seeds.emplace_back(
                                        TopoDS::Edge(owned->shape),
                                        owned->reference);
                                if(step_seeds.empty()) {
                                    if(trace_treatment)std::fprintf(stderr,
                                        "Edge treatment %s sequential edge consumed owner=%s key=%s\n",
                                        operation.owner_id.c_str(),
                                        seed.second.owner_id.c_str(),
                                        seed.second.semantic_key.c_str());
                                    // An earlier bounded patch can consume the
                                    // complete next edge at their common
                                    // endpoint. The authored route then has no
                                    // surviving interval to process; never
                                    // rebind it to a nearby edge.
                                    continue;
                                }
                                BRepFilletAPI_MakeFillet step(result_shape);
                                step.SetParams(1.0e-2,fillet_tolerance,
                                    1.0e-5,fillet_tolerance,1.0e-5,1.0e-3);
                                for(const auto& [edge,reference]:step_seeds) {
                                    static_cast<void>(reference);
                                    if(step.Contour(edge)==0)
                                        step.Add(
                                            treatment.radius_start,edge);
                                }
                                auto step_parents=
                                    resolve_edge_treatment_contours(
                                        step,step_seeds,
                                        owned_topology->edges,
                                        "Fillet",patch_owner);
                                step.Build();
                                if(!step.IsDone()||step.Shape().IsNull())
                                    throw std::runtime_error(
                                        "Sequential OCCT Fillet failed");
                                accept_fillet_result(
                                    step,step_parents,patch_owner);
                            }
                            auto normalized=
                                std::make_shared<LiveCache::Topology>(
                                    *owned_topology);
                            const auto normalize_owner=[&](auto& values) {
                                const auto prefix=
                                    operation.owner_id+":bounded:";
                                for(auto& value:values)
                                    if(value.reference.owner_id.starts_with(
                                            prefix))
                                        value.reference.owner_id=
                                            operation.owner_id;
                            };
                            normalize_owner(normalized->faces);
                            normalize_owner(normalized->edges);
                            normalize_owner(normalized->vertices);
                            owned_topology=std::move(normalized);
                        }
                    }
                    }
                } else {
                    const auto accept_chamfer_result=[&](auto& algorithm,
                            const auto& parents,
                            const std::string& treatment_owner,
                            bool preserve_exact_result) {
                        accept_edge_treatment_result(algorithm,parents,
                            treatment_owner,"chamfer",
                            preserve_exact_result,
                            "OCCT Chamfer produced an invalid body");
                    };
                    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
                    TopExp::MapShapesAndAncestors(result_shape,
                        TopAbs_EDGE,TopAbs_FACE,edge_faces);
                    const TopologyReferenceIndex<FaceReference,OwnedFace>
                        face_references(owned_topology->faces);
                    const auto support_face=[&](const TopoDS_Edge& edge) {
                        const int edge_index=edge_faces.FindIndex(edge);
                        if(edge_index==0)
                            throw std::runtime_error(
                                "Chamfer edge has no adjacent faces");
                        std::vector<std::pair<FaceReference,TopoDS_Face>>
                            faces;
                        for(TopTools_ListIteratorOfListOfShape iterator(
                                edge_faces.FindFromIndex(edge_index));
                            iterator.More();iterator.Next()) {
                            const auto reference=face_references.reference_for(
                                iterator.Value());
                            if(reference.valid())
                                faces.emplace_back(reference,
                                    TopoDS::Face(iterator.Value()));
                        }
                        std::ranges::sort(faces,
                            [](const auto& first,const auto& second) {
                                return std::tie(first.first.owner_id,
                                           first.first.semantic_key,
                                           first.first.instance_path)<
                                    std::tie(second.first.owner_id,
                                           second.first.semantic_key,
                                           second.first.instance_path);
                            });
                        faces.erase(std::ranges::unique(faces,
                            [](const auto& first,const auto& second) {
                                return first.first==second.first;
                            }).begin(),faces.end());
                        if(faces.size()!=2)
                            throw std::runtime_error(
                                "Chamfer A x B / A + angle requires exactly two stably named adjacent faces");
                        return treatment.flip?faces.back().second:
                            faces.front().second;
                    };
                    bool custom_chamfer_accepted=false;
                    if(treatment.calculation_method!=
                            EdgeTreatmentCalculationMethod::Occt) {
                        std::vector<OcctPlanarEdgeChamferSelection>
                            custom_edges;
                        std::vector<OcctPlanarEdgeEndFaces> custom_ends;
                        std::vector<OcctPlanarEdgeSupport> custom_supports;
                        std::vector<std::pair<TopoDS_Edge,EdgeReference>>
                            custom_parents;
                        for(std::size_t requested_index=0;
                                requested_index<treatment.edges.size();
                                ++requested_index) {
                            for(std::size_t selected_index=0;
                                    selected_index<selected.size();
                                    ++selected_index) {
                                if(selected_requested_indices[
                                        selected_index]!=requested_index)
                                    continue;
                                const auto& [edge,reference]=
                                    selected[selected_index];
                                if(std::ranges::none_of(custom_edges,
                                        [&](const auto& current) {
                                            return current.edge.IsSame(edge);
                                        })) {
                                    custom_edges.push_back({edge,
                                        treatment.mode==
                                            ChamferRequest::Mode::EqualDistance
                                            ?TopoDS_Face{}:
                                            support_face(edge)});
                                    custom_ends.push_back(
                                        custom_end_faces[selected_index]);
                                    custom_supports.emplace_back();
                                    custom_parents.emplace_back(edge,
                                        reference);
                                }
                            }
                        }
                        try {
                            const auto custom_mode=treatment.mode==
                                    ChamferRequest::Mode::EqualDistance
                                ?OcctPlanarEdgeChamferMode::EqualDistance
                                :treatment.mode==
                                    ChamferRequest::Mode::TwoDistances
                                    ?OcctPlanarEdgeChamferMode::TwoDistances
                                    :OcctPlanarEdgeChamferMode::DistanceAngle;
                            const double secondary=treatment.mode==
                                    ChamferRequest::Mode::DistanceAngle
                                ?treatment.angle_radians:
                                    treatment.distance_b;
                            auto custom=occt_planar_edge_chamfer(
                                result_shape,custom_edges,custom_mode,
                                treatment.distance_a,secondary,
                                std::max(1.0e-7,
                                    operation.boolean_tolerance),
                                custom_ends,custom_supports,
                                OcctPlanarEdgeEndMode::
                                    AutomaticBodyEnvelope);
                            accept_chamfer_result(custom,custom_parents,
                                operation.owner_id,true);
                            custom_chamfer_accepted=true;
                        } catch(const std::exception& error) {
                            if(trace_treatment)std::fprintf(stderr,
                                "Edge treatment %s custom planar Chamfer fallback: %s\n",
                                operation.owner_id.c_str(),error.what());
                            if(treatment.calculation_method==
                                    EdgeTreatmentCalculationMethod::Zima||
                               has_authored_end_faces)
                                throw std::runtime_error(
                                    std::string(
                                        "ZIMA Chamfer rejected the selected geometry: ")+
                                    error.what());
                        }
                    }
                    if(!custom_chamfer_accepted) {
                    const auto build_chamfer=[&](const auto& seeds,
                            const std::string& treatment_owner) {
                        BRepFilletAPI_MakeChamfer algorithm(result_shape);
                        for(const auto& [edge,reference]:seeds) {
                            static_cast<void>(reference);
                            if(algorithm.Contour(edge)!=0)continue;
                            if(treatment.mode==
                                    ChamferRequest::Mode::EqualDistance)
                                algorithm.Add(
                                    treatment.distance_a,edge);
                            else if(treatment.mode==
                                    ChamferRequest::Mode::TwoDistances)
                                algorithm.Add(treatment.distance_a,
                                    treatment.distance_b,edge,
                                    support_face(edge));
                            else
                                algorithm.AddDA(treatment.distance_a,
                                    treatment.angle_radians,edge,
                                    support_face(edge));
                        }
                        auto parents=resolve_edge_treatment_contours(
                            algorithm,seeds,owned_topology->edges,
                            "Chamfer",treatment_owner);
                        algorithm.Build();
                        if(!algorithm.IsDone()||
                           algorithm.Shape().IsNull())return false;
                        // BRepFilletAPI_MakeChamfer has already authored the
                        // exact treatment topology. A second same-domain
                        // rebuild can reject a valid mixed line/arc contour
                        // (for example 11.prtz / Chamfer 02) as disconnected.
                        // Match Fillet and the direct ZIMA builder: preserve
                        // the accepted shape and only complete its ancestry.
                        accept_chamfer_result(
                            algorithm,parents,treatment_owner,true);
                        return true;
                    };
                    const auto selected_seeds=selected;
                    if(!build_chamfer(
                            selected_seeds,operation.owner_id)) {
                        std::vector<std::pair<
                            TopoDS_Edge,EdgeReference>>
                                intersection_seeds;
                        for(const auto& seed:selected_seeds)
                            if(is_boolean_intersection_interval(
                                   seed.second))
                                intersection_seeds.push_back(seed);
                        if(intersection_seeds.size()<2)
                            throw std::runtime_error(
                                "OCCT Chamfer failed");
                        for(auto seed_iterator=
                                intersection_seeds.rbegin();
                            seed_iterator!=intersection_seeds.rend();
                            ++seed_iterator) {
                            const auto patch_owner=operation.owner_id+
                                ":bounded:"+
                                std::to_string(std::distance(
                                    intersection_seeds.rbegin(),
                                    seed_iterator));
                            std::vector<std::pair<
                                TopoDS_Edge,EdgeReference>> step_seeds;
                            for(const auto* owned:
                                    resolved_treatment_edges(
                                        seed_iterator->second))
                                step_seeds.emplace_back(
                                    TopoDS::Edge(owned->shape),
                                    owned->reference);
                            if(step_seeds.empty())continue;
                            if(!build_chamfer(step_seeds,patch_owner))
                                throw std::runtime_error(
                                    "Sequential OCCT Chamfer failed");
                        }
                        auto normalized=
                            std::make_shared<LiveCache::Topology>(
                                *owned_topology);
                        const auto normalize_owner=[&](auto& values) {
                            const auto prefix=
                                operation.owner_id+":bounded:";
                            for(auto& value:values)
                                if(value.reference.owner_id.starts_with(
                                       prefix))
                                    value.reference.owner_id=
                                        operation.owner_id;
                        };
                        normalize_owner(normalized->faces);
                        normalize_owner(normalized->edges);
                        normalize_owner(normalized->vertices);
                        owned_topology=std::move(normalized);
                    }
                    }
                }
                if (!technological_surfaces.empty()) {
                    // Thread sheets are separate from the solid B-Rep. Remove
                    // only material taken by this treatment, preserving their
                    // original semantic owner and the unaffected sheet area.
                    BRepAlgoAPI_Cut removed;
                    set_boolean_inputs(removed, input_shape, result_shape);
                    removed.SetFuzzyValue(std::max(1.0e-7,operation.boolean_tolerance));
                    removed.Build();
                    if (!removed.IsDone())
                        throw std::runtime_error("OCCT edge treatment removed volume failed");
                    for (auto& surface : technological_surfaces) {
                        if (surface.shape.IsNull()) continue;
                        BRepAlgoAPI_Cut trim;
                        set_boolean_inputs(trim, surface.shape, removed.Shape());
                        trim.SetFuzzyValue(std::max(1.0e-7,operation.boolean_tolerance));
                        trim.Build();
                        if (!trim.IsDone())
                            throw std::runtime_error("OCCT edge treatment thread trim failed");
                        surface.shape=trim.Shape();
                    }
                }
                if(publish&&context.requested_solids.contains(
                        operation.owner_id)) {
                    const auto material_delta=[&](const TopoDS_Shape& source,
                            const std::shared_ptr<const LiveCache::Topology>& source_topology,
                            const TopoDS_Shape& tool,
                            const std::shared_ptr<const LiveCache::Topology>& tool_topology,
                            std::string_view role) {
                        BRepAlgoAPI_Cut algorithm;
                        set_boolean_inputs(algorithm,source,tool);
                        algorithm.SetToFillHistory(true);
                        algorithm.SetNonDestructive(true);
                        algorithm.SetFuzzyValue(std::max(1.0e-7,
                            operation.boolean_tolerance));
                        algorithm.Build();
                        if(!algorithm.IsDone()||algorithm.Shape().IsNull())
                            throw std::runtime_error(
                                "Edge treatment copy delta failed");
                        PrimitiveData delta;
                        delta.shape=algorithm.Shape();
                        if(!TopExp_Explorer(delta.shape,TopAbs_SOLID).More()) {
                            delta.shape.Nullify();
                            return delta;
                        }
                        if(!BRepCheck_Analyzer(delta.shape).IsValid())
                            throw std::runtime_error(
                                "Edge treatment copy delta produced an invalid solid");
                        delta.faces=propagate_topology(algorithm,
                            source_topology->faces,tool_topology->faces);
                        delta.edges=propagate_topology(algorithm,
                            source_topology->edges,tool_topology->edges);
                        delta.vertices=propagate_topology(algorithm,
                            source_topology->vertices,tool_topology->vertices);
                        delta.faces=complete_boolean_faces(delta.shape,
                            delta.faces,delta.edges,delta.vertices,
                            operation.owner_id,role,operations);
                        delta.edges=complete_boolean_edges(delta.shape,
                            delta.faces,delta.edges,operation.owner_id,role);
                        delta.vertices=complete_boolean_vertices(delta.shape,
                            delta.faces,delta.edges,delta.vertices,
                            operation.owner_id,role);
                        return delta;
                    };
                    auto removed=material_delta(input_shape,input_topology,
                        result_shape,owned_topology,
                        "treatment-copy-removed");
                    auto added=material_delta(result_shape,owned_topology,
                        input_shape,input_topology,
                        "treatment-copy-added");
                    if(removed.shape.IsNull()&&added.shape.IsNull())
                        throw std::runtime_error(
                            "Edge treatment copy has no material change");
                    PrimitiveData source=added.shape.IsNull()?removed:added;
                    if(!removed.shape.IsNull())
                        source.copy_phases.emplace_back(
                            BooleanOperation::Subtract,
                            std::make_shared<PrimitiveData>(
                                std::move(removed)));
                    if(!added.shape.IsNull())
                        source.copy_phases.emplace_back(BooleanOperation::Add,
                            std::make_shared<PrimitiveData>(
                                std::move(added)));
                    retain_copy_solid(source);
                    treatment_phase("copy material delta");
                }
                if(!publish)return;
                treatment_phase("thread surfaces");
                boundaries.push_back(
                    make_published_result(result_shape, owned_topology->faces,
                        owned_topology->edges, owned_topology->vertices,
                        true, persist_boundary_shape, true,
                        owned_topology->hidden_display_edges));
                treatment_phase("result packet");
                append_technological_surfaces(boundaries.back(), operation.mesh_deflection);
                append_reference_geometry(original_references,
                    reference_geometry_for_owners(boundaries.back().mesh.original_references,
                        std::unordered_set<std::string>{operation.owner_id}));
                if(retain_original_edges) {
                    std::vector<OwnedEdge> authored_edges;
                    std::vector<OwnedVertex> authored_vertices;
                    for(const auto& edge:owned_topology->edges)
                        if(edge.reference.owner_id==operation.owner_id)
                            authored_edges.push_back(edge);
                    for(const auto& vertex:owned_topology->vertices)
                        if(vertex.reference.owner_id==operation.owner_id)
                            authored_vertices.push_back(vertex);
                    if(!authored_edges.empty())
                        context.original_faces=std::make_shared<OriginalFaces>(
                            OriginalFaces{owned_topology->faces,
                                context.original_faces,
                                std::move(authored_edges),
                                std::move(authored_vertices),result_shape});
                }
                boundaries.back().source_fingerprint =
                    fingerprint(operations, boundaries.size());
                remember_live_boundary(boundaries.back().source_fingerprint,
                    result_shape, owned_topology);
                treatment_phase("reference publication and cache");
            };
            if (operation.owner_id.empty()) {
                throw std::invalid_argument("History operation owner ID is required");
            }
            if (operation.suppressed) {
                auto boundary = boundaries.empty() ? BodyResult{} : boundaries.back();
                if (persist_boundary_shape && !result_shape.IsNull()) {
                    // The input row may intentionally be a deferred final-first
                    // packet. A terminal suppressed row still publishes the
                    // real unchanged input body; copying that lightweight row
                    // would otherwise leave the View and aggregate properties
                    // empty even though the live OCCT shape is valid.
                    boundary=make_operation_result(result_shape,
                        owned_topology->faces,owned_topology->edges,
                        owned_topology->vertices,true,true,true,
                        owned_topology->hidden_display_edges);
                    append_technological_surfaces(
                        boundary,operation.mesh_deflection);
                    append_reference_geometry(
                        boundary.mesh.original_references,
                        original_references);
                } else if (!persist_boundary_shape&&boundary.physical_properties_current) {
                    boundary.kernel_shape.clear();
                    boundary.kernel_placements.clear();boundary.kernel_compound_bodies.clear();
                }
                boundary = publish_pass_through_result(std::move(boundary));
                boundary.source_fingerprint = fingerprint(
                    operations, boundaries.size() + 1);
                remember_live_boundary(boundary.source_fingerprint, result_shape,
                    owned_topology);
                boundaries.push_back(std::move(boundary));
                continue;
            }
            if (operation.replaces_body && reference_only) {
                // An empty replacement is a history divider. Retain the
                // preceding authored references and boundaries for editing,
                // but start subsequent material operations with an empty Body.
                result_shape.Nullify();
                owned_topology = std::make_shared<LiveCache::Topology>();
                sheet_input = {};
                sheet_sources = {};
                technological_surfaces.clear();
                BodyResult boundary;
                boundary.measurement_sources = measurement_sources;
                boundary = publish_pass_through_result(std::move(boundary));
                boundary.source_fingerprint = fingerprint(operations, operation_index + 1);
                remember_live_boundary(boundary.source_fingerprint, result_shape, owned_topology);
                boundaries.push_back(std::move(boundary));
                continue;
            }
            if(const auto* intersection=std::get_if<SurfaceIntersectionRequest>(&operation.primitive)) {
                std::vector<OwnedFace> faces;std::vector<OwnedEdge> edges;std::vector<OwnedVertex> vertices;
                for(auto node=context.original_faces;node;node=node->previous) {
                    faces.insert(faces.end(),node->faces.begin(),node->faces.end());
                    edges.insert(edges.end(),node->edges.begin(),node->edges.end());
                    vertices.insert(vertices.end(),node->vertices.begin(),node->vertices.end());
                }
                auto derived=make_surface_intersection_data(*intersection,operation.owner_id,faces,edges,vertices);
                retain_originals({},derived.edges,derived.vertices);
                auto curves=make_operation_result(derived.shape,{},derived.edges,derived.vertices,true,false,true,{},false);
                // A periodic curve's parameter seam is not an authored point.
                std::erase_if(curves.mesh.points,[](const auto& point){return !point.reference.valid();});
                std::erase_if(curves.mesh.original_references.points,[](const auto& point){return !point.reference.valid();});
                append_reference_geometry(original_references,std::move(curves.mesh.original_references));
                auto boundary=make_published_result(result_shape,owned_topology->faces,owned_topology->edges,
                    owned_topology->vertices,true,persist_boundary_shape);
                boundary.mesh.original_references=original_references;
                boundary.source_fingerprint=fingerprint(operations,operation_index+1);
                boundaries.push_back(std::move(boundary));remember_live_boundary(boundaries.back().source_fingerprint,result_shape,owned_topology);
                continue;
            }
            if(const auto* trim=std::get_if<SurfaceTrimRequest>(&operation.primitive)) {
                std::vector<OwnedFace> faces;std::vector<OwnedEdge> edges;std::vector<OwnedVertex> vertices;
                for(auto node=context.original_faces;node;node=node->previous) {
                    faces.insert(faces.end(),node->faces.begin(),node->faces.end());
                    edges.insert(edges.end(),node->edges.begin(),node->edges.end());
                    vertices.insert(vertices.end(),node->vertices.begin(),node->vertices.end());
                }
                auto trimmed=make_trimmed_surface_data(*trim,operation.owner_id,result_shape,
                    owned_topology->faces,owned_topology->edges,owned_topology->vertices,faces,edges,vertices);
                const auto authored=[&](const auto& values) {
                    auto selected=values;std::erase_if(selected,[&](const auto& item){return item.reference.owner_id!=operation.owner_id;});return selected;
                };
                retain_originals(authored(trimmed.faces),authored(trimmed.edges),authored(trimmed.vertices));retain_copy_solid(trimmed);
                result_shape=trimmed.shape;
                owned_topology=std::make_shared<LiveCache::Topology>(LiveCache::Topology{
                    std::move(trimmed.faces),std::move(trimmed.edges),std::move(trimmed.vertices),{}});
                auto boundary=make_published_result(result_shape,owned_topology->faces,owned_topology->edges,
                    owned_topology->vertices,true,persist_boundary_shape,true);
                append_reference_geometry(original_references,reference_geometry_for_owners(boundary.mesh.original_references,
                    std::unordered_set<std::string>{operation.owner_id}));
                boundary.mesh.original_references=original_references;
                boundary.source_fingerprint=fingerprint(operations,operation_index+1);
                boundaries.push_back(std::move(boundary));remember_live_boundary(boundaries.back().source_fingerprint,result_shape,owned_topology);
                continue;
            }
            if(const auto* sewing=std::get_if<SurfaceSewingRequest>(&operation.primitive)) {
                auto sewn=make_sewn_surface_data(*sewing,operation.owner_id,result_shape,
                    owned_topology->faces,owned_topology->edges,owned_topology->vertices);
                const auto authored=[&](const auto& values) {
                    auto selected=values;std::erase_if(selected,[&](const auto& item){return item.reference.owner_id!=operation.owner_id;});return selected;
                };
                retain_originals(authored(sewn.faces),authored(sewn.edges),authored(sewn.vertices));retain_copy_solid(sewn);
                result_shape=sewn.shape;
                owned_topology=std::make_shared<LiveCache::Topology>(LiveCache::Topology{
                    std::move(sewn.faces),std::move(sewn.edges),std::move(sewn.vertices),{}});
                auto boundary=make_published_result(result_shape,owned_topology->faces,owned_topology->edges,
                    owned_topology->vertices,true,persist_boundary_shape,true);
                append_reference_geometry(original_references,reference_geometry_for_owners(boundary.mesh.original_references,
                    std::unordered_set<std::string>{operation.owner_id}));
                boundary.mesh.original_references=original_references;
                boundary.source_fingerprint=fingerprint(operations,operation_index+1);
                boundaries.push_back(std::move(boundary));remember_live_boundary(boundaries.back().source_fingerprint,result_shape,owned_topology);
                continue;
            }
            if(std::holds_alternative<SolidStateRequest>(operation.primitive)) {
                const auto replay=solid_state_replay::prepare(
                    operations,operation_index,*this);
                HistoryContext replay_context;
                replay_context.external_faces=context.external_faces;
                replay_context.dependency_key=context.dependency_key;
                replay_context.require_original_faces=true;
                const auto rebuilt=evaluate_flat_history(
                    replay,{},replay_context);
                if(rebuilt.empty())throw std::invalid_argument("Select at least one eligible solid element.");
                const auto found=live_cache_->boundaries.find(rebuilt.back().source_fingerprint);
                if(found==live_cache_->boundaries.end())throw std::runtime_error("Solid state calculation is incomplete.");
                result_shape=found->second.shape;
                auto topology=std::make_shared<LiveCache::Topology>(*found->second.topology);
                for(auto& face:topology->faces)solid_state_replay::own(face.reference,operation.owner_id);
                for(auto& edge:topology->edges)solid_state_replay::own(edge.reference,operation.owner_id);
                for(auto& vertex:topology->vertices)solid_state_replay::own(vertex.reference,operation.owner_id);
                retain_originals(topology->faces,topology->edges,topology->vertices);
                auto source=make_operation_result(result_shape,topology->faces,topology->edges,
                    topology->vertices,true,false,true);
                append_reference_geometry(original_references,std::move(source.mesh.original_references));
                owned_topology=std::move(topology);
                if(context.requested_solids.contains(operation.owner_id)) {
                    PrimitiveData operand;operand.shape=result_shape;operand.faces=owned_topology->faces;
                    operand.edges=owned_topology->edges;operand.vertices=owned_topology->vertices;
                    retain_copy_solid(operand);
                }
                auto boundary=make_published_result(result_shape,owned_topology->faces,
                    owned_topology->edges,owned_topology->vertices,true,persist_boundary_shape);
                boundary.source_fingerprint=fingerprint(operations,operation_index+1);
                boundary.mesh.original_references=original_references;
                boundary.solid_state_reference_views.emplace(
                    operation.owner_id,
                    std::make_shared<const ViewerReferenceGeometry>(
                        rebuilt.back().mesh.original_references));
                boundaries.push_back(std::move(boundary));
                remember_live_boundary(boundaries.back().source_fingerprint,result_shape,owned_topology);
                continue;
            }
            const auto copy_source_ids=operation.feature_copy?
                authored_copy_feature_ids(*operation.feature_copy):
                std::vector<std::string>{};
            const bool copies_form=std::ranges::any_of(copy_source_ids,
                [&](const auto& source) {
                    return form_copy_operands.contains(source);
                });
            if(operation.feature_copy&&copies_form) {
                if(std::ranges::any_of(copy_source_ids,[&](const auto& source) {
                        return !form_copy_operands.contains(source);
                    }))
                    throw std::runtime_error(
                        "A multi-source FORM copy cannot mix FORM and ordinary solid sources.");
                auto copy_phase_start=std::chrono::steady_clock::now();
                const auto copy_profile=[&](const char* phase) {
                    const auto now=std::chrono::steady_clock::now();
                    if(std::getenv("ZIMA_CPP_FORM_PROFILE"))
                        std::fprintf(stderr,"FORM copy %s: %.6f s\n",phase,
                            std::chrono::duration<double>(now-copy_phase_start).count());
                    copy_phase_start=now;
                };
                const auto& request=*operation.feature_copy;
                const bool patterned=request.combination==BodyCombination::Pattern;
                const auto pattern=patterned?validated_pattern(request.pattern):PatternRequest{};
                const auto state=sheet_material::regions_before(operations,operation_index+1);
                PrimitiveData changed{result_shape,owned_topology->faces,owned_topology->edges,owned_topology->vertices};
                for(const auto& source_id:copy_source_ids)
                    for(const auto& source:form_copy_operands.at(source_id))
                      for(unsigned index=1;index<(patterned?pattern.count:2);++index) {
                        const auto region=sheet_material::form_copy_region_id(operation,source.material.owner_id,index);
                        const auto material=std::ranges::find(state.regions,region,&SheetMaterialDefinition::owner_id);
                        if(material==state.regions.end())throw std::runtime_error("FORM definition calculation failed.");
                        gp_Trsf transform;
                        if(!patterned) {
                            const auto plane=normalized_mirror_plane(request.mirror_plane);
                            transform.SetMirror(gp_Ax2(gp_Pnt(plane.point.x,plane.point.y,plane.point.z),gp_Dir(plane.normal.x,plane.normal.y,plane.normal.z)));
                        } else if(pattern.circular)transform.SetRotation(gp_Ax1(gp_Pnt(pattern.origin.x,pattern.origin.y,pattern.origin.z),
                            gp_Dir(pattern.axis.x,pattern.axis.y,pattern.axis.z)),pattern.angle_degrees*index*std::numbers::pi/180.);
                        else {const auto delta=pattern_translation(pattern,index);transform.SetTranslation(gp_Vec(delta.x,delta.y,delta.z));}
                        const auto active=std::ranges::find(state.regions,source.material.owner_id,&SheetMaterialDefinition::owner_id);
                        if(active==state.regions.end())throw std::runtime_error("FORM definition calculation failed.");
                        gp_Trsf current;current.SetDisplacement(
                            sheet_form_geometry::frame(source.material.origin,source.material.radial,source.material.along),
                            sheet_form_geometry::frame(active->origin,active->radial,active->along));
                        transform.Multiply(current);
                        sheet_form_geometry::Operands tools{
                            sheet_form_geometry::copy(source.operands.cut,transform,operation,region,index),
                            sheet_form_geometry::copy(source.operands.flat,transform,operation,region,index),
                            sheet_form_geometry::copy(source.operands.formed,transform,operation,region,index),
                            source.operands.rounded_corner};
                        tools.axes=sheet_form_geometry::moved_axes(source.operands.axes,transform);
                        for(auto& axis:tools.axes) {
                            axis.reference.semantic_key="centerline:from:centroid:copy:region:"+
                                std::to_string(region.size())+":"+region+":"+
                                (patterned?"pattern:"+pattern_copy_id(pattern,index)+":" : std::string{})+
                                mirror_source_key(axis.reference.owner_id,axis.reference.semantic_key);
                            axis.reference.owner_id=operation.owner_id;axis.reference.instance_path.clear();
                        }
                        if(!tools.rounded_corner)
                            retain_originals(tools.formed.faces,tools.formed.edges,tools.formed.vertices);
                        copied_form_operands.push_back({*material,std::move(tools)});
                      }
                copy_profile("rigid transforms and ancestry");
                const auto copy_input=changed;
                const auto sequential=[&] {
                    auto result=copy_input;
                    for(const auto& instance:copied_form_operands) {
                        result=sheet_form_geometry::combine(result,
                            instance.material.unfolded?instance.operands.flat:
                                instance.operands.cut,
                            true,operation,operations);
                        if(!instance.material.unfolded)
                            result=sheet_form_geometry::combine(result,
                                instance.operands.formed,false,
                                operation,operations);
                    }
                    return result;
                };
                if(copied_form_operands.size()>1&&
                        !std::getenv("ZIMA_CPP_FORM_COPY_SEQUENTIAL")) {
                    try {
                        std::vector<const PrimitiveData*> cuts,formed;
                        cuts.reserve(copied_form_operands.size());
                        formed.reserve(copied_form_operands.size());
                        for(const auto& instance:copied_form_operands) {
                            cuts.push_back(instance.material.unfolded?
                                &instance.operands.flat:&instance.operands.cut);
                            if(!instance.material.unfolded)
                                formed.push_back(&instance.operands.formed);
                        }
                        changed=sheet_form_geometry::combine_many(copy_input,cuts,
                            true,operation,operations);
                        changed=sheet_form_geometry::combine_many(changed,formed,
                            false,operation,operations);
                    } catch(const Standard_Failure&) {
                        changed=sequential();
                    } catch(const std::runtime_error&) {
                        // Preserve the established per-instance route for
                        // kernel cases whose copied FORM tools cannot be
                        // batched safely.
                        changed=sequential();
                    }
                } else changed=sequential();
                copy_profile("batched sheet booleans");
                form_copy_operands.insert_or_assign(operation.owner_id,copied_form_operands);
                if(std::ranges::any_of(copied_form_operands,[](const auto& value){return value.operands.rounded_corner;})) {
                    changed=sheet_form_geometry::repair_parameterization(changed,operation.boolean_tolerance);
                    const auto authored=[&](const auto& values) {
                        auto result=values;std::erase_if(result,[&](const auto& value){return value.reference.owner_id!=operation.owner_id;});return result;
                    };
                    retain_originals(authored(changed.faces),authored(changed.edges),authored(changed.vertices));
                }
                copy_profile("parameterization repair");
                TopTools_IndexedMapOfShape copied_solids;TopExp::MapShapes(changed.shape,TopAbs_SOLID,copied_solids);
                if(copied_solids.Extent()!=1) {
                    if(std::getenv("ZIMA_CPP_FORM_PROFILE"))for(int index=1;index<=copied_solids.Extent();++index) {
                        GProp_GProps properties;BRepGProp::VolumeProperties(copied_solids(index),properties);
                        Bnd_Box box;BRepBndLib::AddOptimal(copied_solids(index),box,false,true);
                        double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
                        std::fprintf(stderr,"FORM disconnected solid volume=%.9g bounds=(%.9g,%.9g,%.9g)-(%.9g,%.9g,%.9g)\n",
                            properties.Mass(),x0,y0,z0,x1,y1,z1);
                    }
                    throw std::runtime_error("FORM must remain connected to its supporting sheet.");
                }
                if(context.requested_solids.contains(operation.owner_id)) {
                    PrimitiveData copied;TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);
                    for(const auto& instance:copied_form_operands) {
                        const auto& formed=instance.operands.formed;builder.Add(compound,formed.shape);
                        copied.faces.insert(copied.faces.end(),formed.faces.begin(),formed.faces.end());
                        copied.edges.insert(copied.edges.end(),formed.edges.begin(),formed.edges.end());
                        copied.vertices.insert(copied.vertices.end(),formed.vertices.begin(),formed.vertices.end());
                    }
                    copied.shape=compound;retain_copy_solid(copied);
                }
                result_shape=changed.shape;
                owned_topology=std::make_shared<LiveCache::Topology>(LiveCache::Topology{
                    std::move(changed.faces),std::move(changed.edges),std::move(changed.vertices),{}});
                copy_profile("solid validation and copy source");
                auto boundary=make_published_result(result_shape,owned_topology->faces,owned_topology->edges,owned_topology->vertices,true,persist_boundary_shape,true);
                copy_profile("result display and properties");
                for(const auto& instance:copied_form_operands)
                    original_references.axes.insert(original_references.axes.end(),instance.operands.axes.begin(),instance.operands.axes.end());
                append_reference_geometry(original_references,reference_geometry_for_owners(boundary.mesh.original_references,{operation.owner_id}));
                boundary.mesh.original_references=original_references;
                if(!boundaries.empty())boundary.sheet_cuts=boundaries.back().sheet_cuts;
                boundary.source_fingerprint=fingerprint(operations,operation_index+1);
                boundaries.push_back(std::move(boundary));remember_live_boundary(boundaries.back().source_fingerprint,result_shape,owned_topology);
                copy_profile("reference publication and cache");
                continue;
            }
            if(const auto* form=std::get_if<SheetFormRequest>(&operation.primitive)) {
                auto phase_start=std::chrono::steady_clock::now();
                const auto profile=[&](const char* phase) {
                    const auto now=std::chrono::steady_clock::now();
                    if(std::getenv("ZIMA_CPP_FORM_PROFILE"))
                        std::fprintf(stderr,"FORM %s: %.6f s\n",phase,std::chrono::duration<double>(now-phase_start).count());
                    phase_start=now;
                };
                sheet_form_geometry::validate_support(*form,result_shape,owned_topology->faces,operations,operation_index);
                profile("support validation");
                const bool outward_corner=form->outward&&sheet_form_geometry::corner_definition(*form);
                const bool native_corner=outward_corner||!form->solid_opening_faces.empty();
                auto prepared_request=*form;
                if(outward_corner&&!form->solid_cut_snapshot) {
                    using namespace sheet_material;
                    const auto regions=regions_before(operations,operation_index);
                    const auto bend=std::ranges::find_if(regions.regions,[&](const auto& region) {
                        if(region.kind!=SheetMaterialDefinition::Kind::Cylinder||region.unfolded||region.curved_source_id.empty()||
                           std::abs(region.angle-std::numbers::pi/2)>1e-10||std::abs(region.thickness-form->thickness)>1e-10)return false;
                        const auto delta=sub(form->position,region.origin);
                        return std::abs(dot(delta,region.radial))<1e-7&&std::abs(dot(delta,region.tangent)-region.radius)<1e-7;
                    });
                    if(bend==regions.regions.end())throw std::runtime_error("FORM could not create a valid sheet.");
                    auto definitions=*form->definition;
                    std::vector<ExtrusionRequest*> cutting_profiles;
                    for(auto& tool:definitions)if(tool.body.id==form->cut_body) {
                        if(auto* cut=std::get_if<ExtrusionRequest>(&tool.primitive))cutting_profiles.push_back(cut);
                        else if(auto* group=std::get_if<FeatureGroupRequest>(&tool.primitive))
                            for(auto& child:group->children)if(auto* cut=std::get_if<ExtrusionRequest>(&child))cutting_profiles.push_back(cut);
                    }
                    if(cutting_profiles.size()!=2)throw std::runtime_error("Invalid FORM definition.");
                    // Contact profiles stay on their original support planes.
                    // Cut only as far as the actual outer Bend radius.
                    for(auto* cut:cutting_profiles)
                        cut->direction=mul(unit(cut->direction),bend->radius);
                    prepared_request.definition=std::make_shared<const std::vector<HistoryOperation>>(std::move(definitions));
                }
                const auto preparation_key=sheet_form_geometry::preparation_key(prepared_request,operation.boolean_tolerance);
                auto prepared=live_cache_->prepared_forms.find(preparation_key);
                if(prepared==live_cache_->prepared_forms.end()) {
                auto tools=*prepared_request.definition;
                if(form->surface_snapshot||!form->solid_opening_faces.empty())std::erase_if(tools,[&](const auto& value){
                    return (value.body.id!=form->cut_body||form->solid_cut_snapshot)&&value.body.id!=form->flat_body;
                });
                const auto definitions=evaluate_history_incremental(tools,{});
                profile(form->surface_snapshot?"cut sketches only":"source histories");
                if((definitions.empty()&&!form->surface_snapshot&&!form->solid_cut_snapshot)||
                    (!definitions.empty()&&!definitions.back().calculation_errors.empty()))
                    throw std::runtime_error("FORM definition calculation failed.");
                std::vector<ViewerAxis> shape_axes;PrimitiveData closed_corner;
                const auto body=[&](const std::string& id,std::string_view role) {
                    if(id.empty())return PrimitiveData{};
                    PrimitiveData source;
                    if(role=="shape"&&!form->solid_opening_faces.empty()) {
                        if(!form->solid_cut_snapshot||form->solid_opening_faces.size()!=2)
                            throw std::runtime_error("FORM definition calculation failed.");
                        std::vector<HistoryOperation> shell_history;bool matched=false;
                        for(auto local:*form->definition)if(local.body.id==form->shape_body) {
                            local.body={};shell_history.push_back(std::move(local));
                            if(history_fingerprint(shell_history,shell_history.size())==form->solid_cut_snapshot->source_fingerprint) {matched=true;break;}
                        }
                        if(!matched)throw std::runtime_error("FORM definition calculation failed.");
                        // Reuse ordinary profile-centroid calculation and its
                        // exact Up To endpoints. These transient display flags
                        // do not modify the independently stored definition.
                        for(auto& local:shell_history) {
                            const auto centroid=[](auto& request) {
                                request.centerlines.centroid_enabled=true;
                                request.centerlines.origin_enabled=false;
                            };
                            if(auto* request=std::get_if<ExtrusionRequest>(&local.primitive))centroid(*request);
                            if(auto* group=std::get_if<FeatureGroupRequest>(&local.primitive))
                                for(auto& child:group->children)if(auto* request=std::get_if<ExtrusionRequest>(&child))centroid(*request);
                        }
                        const auto closed=evaluate_history_incremental(shell_history,{});
                        const auto original_solid=live_cache_->boundaries.find(closed.back().source_fingerprint);
                        if(original_solid==live_cache_->boundaries.end())throw std::runtime_error("FORM definition calculation failed.");
                        const auto original_faces=original_solid->second.topology->faces;
                        closed_corner={original_solid->second.shape,original_faces,
                            original_solid->second.topology->edges,original_solid->second.topology->vertices};
                        shape_axes=sheet_form_geometry::profile_axes(closed.back().mesh.axes);
                        HistoryOperation hollow;hollow.owner_id="FORM_SHELL";
                        // The symbolic corner skin may be thinner than stock.
                        hollow.primitive=ShellRequest{form->solid_opening_faces,.5*form->thickness};
                        hollow.boolean_tolerance=operation.boolean_tolerance;shell_history.push_back(std::move(hollow));
                        const auto walls=evaluate_history_incremental(shell_history,closed);
                        if(walls.empty()||!walls.back().calculation_errors.empty()||!walls.back().has_solid())
                            throw std::runtime_error("FORM definition calculation failed.");
                        const auto native=live_cache_->boundaries.find(walls.back().source_fingerprint);
                        if(native==live_cache_->boundaries.end())throw std::runtime_error("FORM definition calculation failed.");
                        source={native->second.shape,native->second.topology->faces,native->second.topology->edges,native->second.topology->vertices};
                        for(auto& face:source.faces) {
                            if(std::ranges::any_of(original_faces,[&](const auto& original){return original.shape.IsSame(face.shape);}))
                                face.reference.sheet_role=SheetFaceRole::SideA;
                            else {
                                const auto actual=TopoDS::Face(face.shape);BRepAdaptor_Surface surface(actual);
                                const bool rim=surface.GetType()==GeomAbs_Plane&&std::ranges::any_of(form->solid_opening_faces,[&](const auto& opening) {
                                    const auto original=std::ranges::find_if(original_faces,[&](const auto& value){return same_face_identity(value.reference,opening);});
                                    if(original==original_faces.end())return false;
                                    const auto plane=BRepAdaptor_Surface(TopoDS::Face(original->shape)).Plane();
                                    return plane.Axis().Direction().IsParallel(surface.Plane().Axis().Direction(),1e-10)&&
                                        plane.Distance(surface.Plane().Location())<1e-7;
                                });
                                face.reference.sheet_role=rim?SheetFaceRole::ThicknessFace:SheetFaceRole::SideB;
                            }
                        }
                        profile("native solid Shell");
                    }else if(role=="shape"&&form->surface_snapshot) {
                        source=sheet_form_geometry::read_surface(*form->surface_snapshot);
                        if(outward_corner)shape_axes=sheet_form_geometry::profile_axes(
                            form->solid_cut_snapshot?form->solid_cut_snapshot->mesh.axes:form->surface_snapshot->mesh.axes);
                        if(outward_corner&&form->solid_cut_snapshot&&shape_axes.empty()) {
                            // Authoring may hide its profile centerlines. Keep
                            // the existing FORM whole-profile axis contract by
                            // using the ordinary explicit centroid calculation.
                            std::vector<HistoryOperation> centroid_history;
                            for(auto local:*form->definition)if(local.body.id==form->cut_body) {
                                local.body={};
                                const auto centroid=[](auto& request){request.centerlines.centroid_enabled=true;request.centerlines.origin_enabled=false;};
                                if(auto* request=std::get_if<ExtrusionRequest>(&local.primitive))centroid(*request);
                                if(auto* group=std::get_if<FeatureGroupRequest>(&local.primitive))
                                    for(auto& child:group->children)if(auto* request=std::get_if<ExtrusionRequest>(&child))centroid(*request);
                                centroid_history.push_back(std::move(local));
                            }
                            const auto centroids=evaluate_history_incremental(centroid_history,{});
                            if(centroids.empty()||!centroids.back().calculation_errors.empty())throw std::runtime_error("FORM definition calculation failed.");
                            shape_axes=sheet_form_geometry::profile_axes(centroids.back().mesh.axes);
                        }
                    }
                    else {
                        const auto boundaries=definitions.back().body_boundaries.find(id);
                        if(boundaries==definitions.back().body_boundaries.end()||boundaries->second.empty())
                            throw std::runtime_error("FORM definition calculation failed.");
                        const auto local=live_cache_->boundaries.find(boundaries->second.back().source_fingerprint);
                        if(local==live_cache_->boundaries.end())throw std::runtime_error("FORM definition calculation failed.");
                        source.shape=local->second.shape;source.faces=local->second.topology->faces;
                        source.edges=local->second.topology->edges;source.vertices=local->second.topology->vertices;
                    }
                    if(role=="shape"&&form->solid_opening_faces.empty()) {
                        profile("tools and source extraction");
                        auto anchor=form->surface;
                        // FORM owns the complete authored skin. A symmetric
                        // surface extrusion can leave its two sides as separate
                        // shells; ordinary face thickening intentionally consumes
                        // only one shell. Sew those native patches here, using
                        // the existing ancestry-preserving surface operation.
                        TopTools_IndexedMapOfShape all_faces,anchor_faces;
                        TopExp::MapShapes(source.shape,TopAbs_FACE,all_faces);
                        for(TopExp_Explorer shell(source.shape,TopAbs_SHELL);shell.More();shell.Next()) {
                            TopTools_IndexedMapOfShape members;TopExp::MapShapes(shell.Current(),TopAbs_FACE,members);
                            if(std::ranges::any_of(source.faces,[&](const auto& f){return same_face_identity(f.reference,anchor)&&members.Contains(f.shape);})) {
                                anchor_faces=members;break;
                            }
                        }
                        if(all_faces.Extent()>1&&anchor_faces.Extent()!=all_faces.Extent()) {
                            // The sides of a native symmetric extrusion already
                            // share their junction edges. Combine those shells
                            // without renaming any of their existing topology.
                            TopTools_IndexedDataMapOfShapeListOfShape adjacency;
                            TopExp::MapShapesAndAncestors(source.shape,TopAbs_EDGE,TopAbs_FACE,adjacency);
                            std::set<int> connected{1};bool changed=true;
                            while(changed) {
                                changed=false;
                                for(int i=1;i<=adjacency.Extent();++i) {
                                    std::set<int> incident;
                                    for(TopTools_ListIteratorOfListOfShape it(adjacency.FindFromIndex(i));it.More();it.Next())incident.insert(all_faces.FindIndex(it.Value()));
                                    if(incident.size()>2)throw std::runtime_error("FORM definition calculation failed.");
                                    if(std::ranges::any_of(incident,[&](int f){return connected.contains(f);}))
                                        for(int f:incident)changed|=connected.insert(f).second;
                                }
                            }
                            if(connected.size()==static_cast<std::size_t>(all_faces.Extent())) {
                                BRepBuilderAPI_Sewing sewing(operation.boolean_tolerance,true,true,true,false);
                                for(int i=1;i<=all_faces.Extent();++i)sewing.Add(all_faces(i));
                                sewing.Perform();auto joined=sewing.SewedShape();
                                if(joined.IsNull()||joined.ShapeType()!=TopAbs_SHELL||!BRepCheck_Analyzer(joined).IsValid())throw std::runtime_error("FORM definition calculation failed.");
                                const auto selected=std::ranges::find_if(source.faces,[&](const auto& f){return same_face_identity(f.reference,anchor);});
                                TopTools_IndexedMapOfShape joined_faces;TopExp::MapShapes(joined,TopAbs_FACE,joined_faces);
                                if(selected==source.faces.end()||!joined_faces.Contains(selected->shape))throw std::runtime_error("FORM definition calculation failed.");
                                // Joining must retain the chosen anchor's side;
                                // sewing may orient the other extrusion half.
                                if(joined_faces(joined_faces.FindIndex(selected->shape)).Orientation()!=selected->shape.Orientation())joined.Reverse();
                                // No new junction is needed: every existing
                                // source identity must survive this shell join.
                                const auto rebind=[&](auto& bindings,TopAbs_ShapeEnum kind) {
                                    TopTools_IndexedMapOfShape members;TopExp::MapShapes(joined,kind,members);
                                    for(auto& item:bindings) {
                                        const auto index=members.FindIndex(item.shape);
                                        if(!index)throw std::runtime_error("FORM definition calculation failed.");
                                        item.shape=members(index);
                                    }
                                };
                                rebind(source.faces,TopAbs_FACE);rebind(source.edges,TopAbs_EDGE);rebind(source.vertices,TopAbs_VERTEX);
                                source.shape=joined;
                            }else {
                                SurfaceSewingRequest sewing;sewing.tolerance=operation.boolean_tolerance;
                                for(const auto& face:source.faces)sewing.faces.push_back(face.reference);
                                const auto sewn_key="sewn:face:from:"+encoded_topology_reference(anchor);
                                source=make_sewn_surface_data(sewing,"FORM_SKIN",source.shape,source.faces,source.edges,source.vertices);
                                const auto selected=std::ranges::find_if(source.faces,[&](const auto& f){return f.reference.semantic_key==sewn_key;});
                                if(selected==source.faces.end())throw std::runtime_error("FORM definition calculation failed.");
                                anchor=selected->reference;
                            }
                        }
                        const auto thickened=make_thickened_surface_data(
                            {anchor,form->thickness,!form->outward&&sheet_form_geometry::corner_definition(*form)?SurfaceThicknessSide::Second:SurfaceThicknessSide::First,operation.boolean_tolerance},
                            "FORM_THICKNESS",source.shape,source.faces,source.edges,source.vertices,true);
                        if(!thickened.remaining.faces.empty())throw std::runtime_error("FORM definition calculation failed.");
                        source=thickened.operand;
                        profile("shell thickening and validation");
                    }
                    const auto scope=std::ranges::find_if(*form->definition,[&](const auto& value){return value.body.id==id;});
                    if(scope==form->definition->end())throw std::runtime_error("FORM definition calculation failed.");
                    auto placed=sheet_form_geometry::move(source,primitive_transform(scope->body.translation,scope->body.rotation_degrees));
                    return placed;
                };
                const auto shape_scope=std::ranges::find_if(*form->definition,[&](const auto& value){return value.body.id==form->shape_body;});
                if(shape_scope==form->definition->end())throw std::runtime_error("FORM definition calculation failed.");
                auto formed=body(form->shape_body,"shape");
                const auto axes_scope=std::ranges::find_if(*form->definition,[&](const auto& value){return value.body.id==(outward_corner&&form->solid_cut_snapshot?form->cut_body:form->shape_body);});
                if(axes_scope==form->definition->end())throw std::runtime_error("FORM definition calculation failed.");
                shape_axes=sheet_form_geometry::moved_axes(std::move(shape_axes),
                    primitive_transform(axes_scope->body.translation,axes_scope->body.rotation_degrees));
                PrimitiveData cut;
                if(form->solid_cut_snapshot) {
                    PrimitiveData source=closed_corner;
                    if(source.shape.IsNull()) {
                        if(!form->solid_cut_snapshot->kernel_faces.empty())source=sheet_form_geometry::read_surface(*form->solid_cut_snapshot,true);
                        else {
                            const auto native=live_cache_->boundaries.find(form->solid_cut_snapshot->source_fingerprint);
                            if(native==live_cache_->boundaries.end())throw std::runtime_error("FORM definition calculation failed.");
                            source={native->second.shape,native->second.topology->faces,native->second.topology->edges,native->second.topology->vertices};
                        }
                    }
                    const auto cut_scope=outward_corner?axes_scope:shape_scope;
                    cut=sheet_form_geometry::move(source,primitive_transform(cut_scope->body.translation,cut_scope->body.rotation_degrees));
                }else cut=body(form->cut_body,"cut");
                sheet_form_geometry::Operands operands{std::move(cut),body(form->flat_body,"flat"),std::move(formed)};
                operands.axes=std::move(shape_axes);
                prepared=live_cache_->prepared_forms.emplace(preparation_key,std::move(operands)).first;
                live_cache_->prepared_form_order.push_back(preparation_key);
                while(live_cache_->prepared_form_order.size()>16) {
                    live_cache_->prepared_forms.erase(live_cache_->prepared_form_order.front());
                    live_cache_->prepared_form_order.pop_front();
                }
                }
                gp_Trsf placement;placement.SetDisplacement(
                    sheet_form_geometry::frame(form->source_origin,form->source_normal,form->source_x),
                    sheet_form_geometry::frame(form->position,form->normal,form->x_direction));
                form_operands=sheet_form_geometry::Operands{
                    sheet_form_geometry::place(prepared->second.cut,placement,*form,operation.owner_id,"cut"),
                    sheet_form_geometry::place(prepared->second.flat,placement,*form,operation.owner_id,"flat"),
                    sheet_form_geometry::place(prepared->second.formed,placement,*form,operation.owner_id,"shape"),
                    native_corner};
                form_operands->axes=sheet_form_geometry::moved_axes(prepared->second.axes,placement);
                for(auto& axis:form_operands->axes) {
                    axis.reference.semantic_key="centerline:from:centroid:form:"+
                        sheet_form_geometry::parent_key("shape",form->definition_id,axis.reference);
                    axis.reference.owner_id=operation.owner_id;axis.reference.instance_path.clear();
                    axis.label.clear();
                }
                if(!form->solid_opening_faces.empty()||(outward_corner&&form->solid_cut_snapshot)) {
                    PrimitiveData input;input.shape=result_shape;input.faces=owned_topology->faces;
                    input.edges=owned_topology->edges;input.vertices=owned_topology->vertices;
                    form_operands->formed=sheet_form_geometry::trim_corner_shell(input,form_operands->formed,*form,operation,operations,operation_index);
                    profile("outer Bend radius trim");
                }
                if(sheet_form_geometry::corner_definition(*form)&&!form->solid_cut_snapshot&&!outward_corner) {
                    PrimitiveData corner_input;corner_input.shape=result_shape;corner_input.faces=owned_topology->faces;
                    corner_input.edges=owned_topology->edges;corner_input.vertices=owned_topology->vertices;
                    form_operands->formed=sheet_form_geometry::corner_joining_material(corner_input,form_operands->formed,*form,operation,operations,operation_index);
                    profile("corner joining material");
                }
                profile("rigid transform and ancestry");
                if(form_operands->cut.shape.IsNull()||form_operands->formed.shape.IsNull())
                    throw std::runtime_error("FORM definition calculation failed.");
                PrimitiveData input;input.shape=result_shape;input.faces=owned_topology->faces;
                input.edges=owned_topology->edges;input.vertices=owned_topology->vertices;
                auto changed=sheet_form_geometry::combine(input,form_operands->cut,true,operation,operations);
                profile("sheet cut");
                changed=sheet_form_geometry::combine(changed,form_operands->formed,false,operation,operations);
                profile("sheet fuse");
                if(native_corner) {
                    auto unified=unify_preserving_face_provenance(changed.shape,changed.faces,
                        changed.edges,changed.vertices,std::max(1e-7,operation.boolean_tolerance));
                    changed={std::move(unified.shape),std::move(unified.faces),
                        std::move(unified.edges),std::move(unified.vertices)};
                    profile("corner domain unification");
                }
                TopTools_IndexedMapOfShape solids;TopExp::MapShapes(changed.shape,TopAbs_SOLID,solids);
                if(solids.Extent()!=1)throw std::runtime_error("FORM must remain connected to its supporting sheet.");
                if(!native_corner)
                    retain_originals(form_operands->formed.faces,form_operands->formed.edges,form_operands->formed.vertices);
                result_shape=changed.shape;
                owned_topology=std::make_shared<LiveCache::Topology>(LiveCache::Topology{
                    std::move(changed.faces),std::move(changed.edges),std::move(changed.vertices),{}});
                if(!form->solid_opening_faces.empty()||(outward_corner&&form->solid_cut_snapshot&&form->finish_corner)) {
                    // Resolve both real transition routes once at the Boolean
                    // boundary. Rounding reuses the ordinary native Fillet
                    // implementation without publishing unused intermediate
                    // meshes, mass properties or history transactions.
                    std::array<std::vector<EdgeReference>,2> routes;
                    TopTools_IndexedDataMapOfShapeListOfShape adjacency;
                    TopExp::MapShapesAndAncestors(result_shape,TopAbs_EDGE,TopAbs_FACE,adjacency);
                    const TopologyReferenceIndex<FaceReference,OwnedFace> face_index(owned_topology->faces);
                    using namespace sheet_material;
                    const auto regions=regions_before(operations,operation_index);
                    const auto bend=std::ranges::find_if(regions.regions,[&](const auto& region) {
                        if(region.kind!=SheetMaterialDefinition::Kind::Cylinder||region.curved_source_id.empty()||region.unfolded||
                            std::abs(region.angle-std::numbers::pi/2)>1e-10||std::abs(region.thickness-form->thickness)>1e-10)return false;
                        const auto displacement=sub(form->position,region.origin);
                        const auto r=dot(displacement,region.radial),t=dot(displacement,region.tangent);
                        return (std::abs(r)<1e-7&&std::abs(t-region.radius)<1e-7)||
                            (std::abs(r+region.thickness)<1e-7&&std::abs(t-region.radius+region.thickness)<1e-7);
                    });
                    if(bend==regions.regions.end())throw std::runtime_error("FORM could not create a valid sheet.");
                    const auto support_side=[&](const TopoDS_Face& face)->std::optional<unsigned> {
                        BRepAdaptor_Surface surface(face);
                        if(surface.GetType()==GeomAbs_Plane) {
                            const auto plane=surface.Plane();const auto direction=plane.Axis().Direction();
                            const Vec3 normal{direction.X(),direction.Y(),direction.Z()};
                            const auto point=plane.Location();const auto delta=sub({point.X(),point.Y(),point.Z()},bend->origin);
                            if(std::abs(dot(normal,bend->radial))>1.-1e-10) {
                                const auto level=dot(delta,bend->radial);
                                if(std::abs(level+bend->thickness)<1e-7)return 0;
                                if(std::abs(level)<1e-7)return 1;
                            }
                            if(std::abs(dot(normal,bend->tangent))>1.-1e-10) {
                                const auto level=dot(delta,bend->tangent);
                                if(std::abs(level-bend->radius+bend->thickness)<1e-7)return 0;
                                if(std::abs(level-bend->radius)<1e-7)return 1;
                            }
                        }else if(surface.GetType()==GeomAbs_Cylinder) {
                            const auto cylinder=surface.Cylinder();const auto axis=cylinder.Axis().Direction();
                            const Vec3 direction{axis.X(),axis.Y(),axis.Z()};
                            const auto point=cylinder.Location();const auto center=sub(bend->origin,mul(bend->radial,bend->radius));
                            const auto gap=cross(sub({point.X(),point.Y(),point.Z()},center),bend->along);
                            if(std::abs(dot(direction,bend->along))>1.-1e-10&&
                                std::hypot(std::hypot(gap.x,gap.y),gap.z)<1e-7) {
                                if(std::abs(cylinder.Radius()-bend->radius+bend->thickness)<1e-7)return 0;
                                if(std::abs(cylinder.Radius()-bend->radius)<1e-7)return 1;
                            }
                        }
                        return {};
                    };
                    for(const auto& owned:owned_topology->edges) {
                        const auto edge=TopoDS::Edge(owned.shape);const auto entry=adjacency.FindIndex(edge);
                        if(entry==0||!owned.reference.valid())continue;
                        std::vector<std::pair<FaceReference,TopoDS_Face>> sides;
                        for(TopTools_ListIteratorOfListOfShape it(adjacency.FindFromIndex(entry));it.More();it.Next()) {
                            const auto ref=face_index.reference_for(it.Value());
                            if(ref.valid()&&std::ranges::none_of(sides,[&](const auto& side){return side.first==ref;}))
                                sides.emplace_back(ref,TopoDS::Face(it.Value()));
                        }
                        if(sides.size()!=2||std::ranges::none_of(sides,[&](const auto& side){return side.first.owner_id==operation.owner_id;}))continue;
                        BRepAdaptor_Curve curve(edge);const std::vector<double> parameter{(curve.FirstParameter()+curve.LastParameter())*.5};
                        BRepClass_FaceExplorer first_explorer(sides[0].second),second_explorer(sides[1].second);
                        const auto a=sampled_inward_face_directions(edge,sides[0].second,parameter,first_explorer),
                            b=sampled_inward_face_directions(edge,sides[1].second,parameter,second_explorer);
                        if(a.empty()||b.empty()||std::abs(dot(unit(a.front()),unit(b.front())))>1.-1e-6)continue;
                        const auto first_support=support_side(sides[0].second),second_support=support_side(sides[1].second);
                        if(first_support.has_value()==second_support.has_value())continue;
                        const auto support=first_support?*first_support:*second_support;
                        const auto& wall=sides[first_support?1:0].first;
                        if(wall.owner_id!=operation.owner_id||wall.sheet_owner!=operation.owner_id)continue;
                        routes[support].push_back(owned.reference);
                    }
                    if(std::getenv("ZIMA_CPP_FORM_PROFILE"))std::fprintf(stderr,"FORM transition routes: inner=%zu outer=%zu\n",routes[0].size(),routes[1].size());
                    if(std::getenv("ZIMA_CPP_FORM_DUMP")) {
                        BRepTools::Write(result_shape,"build/form-diagnostic/native-outward-unrounded.brep");
                        for(unsigned side:{0u,1u}) {
                            TopoDS_Compound route;BRep_Builder builder;builder.MakeCompound(route);
                            for(const auto& reference:routes[side]) {
                                const auto found=std::ranges::find_if(owned_topology->edges,[&](const auto& edge){return edge.reference==reference;});
                                if(found!=owned_topology->edges.end())builder.Add(route,found->shape);
                            }
                            BRepTools::Write(route,("build/form-diagnostic/native-outward-route-"+std::to_string(side)+".brep").c_str());
                        }
                    }
                    if(routes[0].empty()||routes[1].empty())throw std::runtime_error("FORM could not create a valid sheet.");
                    for(unsigned side:{1u,0u}) {
                        // Symbolic forming permits local thickness variation:
                        // the cavity uses t and the opposite transition t/4.
                        const auto nominal=form->thickness*(side==0?.25:1.);
                        try {apply_edge_treatment(FilletRequest{routes[side],nominal},false);}
                        catch(const std::runtime_error& error) {
                            if(std::string_view(error.what())=="OCCT Fillet failed or produced an invalid body")
                                throw std::runtime_error("Corner FORM cannot fit the prescribed transition radii. Change the bend radius or the definition.");
                            throw;
                        }
                        catch(const Standard_Failure&) {
                            throw std::runtime_error("Corner FORM cannot fit the prescribed transition radii. Change the bend radius or the definition.");
                        }
                        auto tagged=std::make_shared<LiveCache::Topology>(*owned_topology);
                        for(auto& face:tagged->faces)if(face.reference.owner_id==operation.owner_id&&
                            face.reference.semantic_key.starts_with("fillet:face:")&&face.reference.sheet_owner.empty()) {
                            face.reference.sheet_owner=operation.owner_id;face.reference.sheet_thickness=form->thickness;
                            face.reference.sheet_role=side==0?SheetFaceRole::SideB:SheetFaceRole::SideA;
                        }
                        owned_topology=std::move(tagged);
                        profile(side==0?"opposite transition Fillet 0.25t":"cavity transition Fillet t");
                    }
                }
                if(native_corner) {
                    // Copies and sheet-state replay need the complete rounded
                    // change, including material removed from the stock by a
                    // transition. Derive it only when a downstream consumer
                    // requests it; an ordinary insertion needs no extra Cut.
                    const PrimitiveData final{result_shape,owned_topology->faces,
                        owned_topology->edges,owned_topology->vertices};
                    if(sheet_sources_needed[operation_index]||context.requested_solids.contains(operation.owner_id)) {
                        // The exact delta is sufficient: (S - (S - F)) union
                        // (F - S) reproduces F, without the original cutter's
                        // redundant coincident trims in the rounded copy.
                        form_operands->cut=sheet_form_geometry::combine(input,final,true,operation,operations);
                        form_operands->formed=sheet_form_geometry::combine(final,input,true,operation,operations);
                        if(std::getenv("ZIMA_CPP_FORM_PROFILE"))for(const auto* value:{&form_operands->cut,&form_operands->formed})if(!value->shape.IsNull()) {
                            Bnd_Box box;BRepBndLib::AddOptimal(value->shape,box,false,true);
                            double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
                            std::fprintf(stderr,"FORM delta %s bounds=(%.9g,%.9g,%.9g)-(%.9g,%.9g,%.9g)\n",
                                value==&form_operands->cut?"removed":"added",x0,y0,z0,x1,y1,z1);
                        }
                        form_operands->cut=sheet_form_geometry::repair_parameterization(form_operands->cut,operation.boolean_tolerance);
                        form_operands->formed=sheet_form_geometry::repair_parameterization(form_operands->formed,operation.boolean_tolerance);
                    }
                    const auto authored=[&](const auto& members) {
                        auto selected=members;std::erase_if(selected,[&](const auto& member) {
                            return member.reference.owner_id!=operation.owner_id;
                        });return selected;
                    };
                    retain_originals(authored(final.faces),authored(final.edges),authored(final.vertices));
                }
                form_copy_operands.insert_or_assign(operation.owner_id,std::vector<sheet_form_geometry::Instance>{{*operation.sheet_material,*form_operands}});
                retain_copy_solid(form_operands->formed);
                auto boundary=make_published_result(result_shape,owned_topology->faces,owned_topology->edges,
                    owned_topology->vertices,true,persist_boundary_shape,true);
                original_references.axes.insert(original_references.axes.end(),form_operands->axes.begin(),form_operands->axes.end());
                profile("result display and properties");
                append_reference_geometry(original_references,reference_geometry_for_owners(boundary.mesh.original_references,
                    std::unordered_set<std::string>{operation.owner_id}));
                boundary.mesh.original_references=original_references;
                if(!boundaries.empty())boundary.sheet_cuts=boundaries.back().sheet_cuts;
                boundary.source_fingerprint=fingerprint(operations,operation_index+1);
                boundaries.push_back(std::move(boundary));remember_live_boundary(boundaries.back().source_fingerprint,result_shape,owned_topology);
                continue;
            }
            if(const auto* rip=std::get_if<SheetRipRequest>(
                    &operation.primitive)) {
                [&] {
                    PrimitiveData input{result_shape,owned_topology->faces,
                        owned_topology->edges,owned_topology->vertices};
                    auto changed=sheet_rip_geometry::apply(input,*rip,
                        operation.owner_id);
                    retain_originals(changed.faces,changed.edges,
                        changed.vertices);
                    retain_copy_solid(changed);
                    auto source=make_operation_result(changed.shape,
                        changed.faces,changed.edges,changed.vertices,true,
                        false,true);
                    append_reference_geometry(original_references,
                        std::move(source.mesh.original_references));
                    result_shape=changed.shape;
                    auto topology=std::make_shared<LiveCache::Topology>();
                    topology->faces=std::move(changed.faces);
                    topology->edges=std::move(changed.edges);
                    topology->vertices=std::move(changed.vertices);
                    owned_topology=std::move(topology);
                    auto boundary=make_published_result(result_shape,
                        owned_topology->faces,owned_topology->edges,
                        owned_topology->vertices,true,persist_boundary_shape);
                    boundary.source_fingerprint=fingerprint(operations,
                        operation_index+1);
                    boundary.mesh.original_references=original_references;
                    if(!boundaries.empty())
                        boundary.sheet_cuts=boundaries.back().sheet_cuts;
                    boundaries.push_back(std::move(boundary));
                    remember_live_boundary(
                        boundaries.back().source_fingerprint,result_shape,
                        owned_topology);
                }();
                continue;
            }
            if(const auto* line_bend=std::get_if<LineBendRequest>(
                    &operation.primitive)) {
                [&] {
                    PrimitiveData input{result_shape,owned_topology->faces,
                        owned_topology->edges,owned_topology->vertices};
                    auto changed=sheet_state_geometry::line_bend(input,
                        *line_bend,operation.owner_id);
                    sheet_state_geometry::identify(changed,
                        operation.owner_id);
                    ViewerAxis axis;
                    axis.point=sheet_material::add(line_bend->origin,
                        sheet_material::mul(line_bend->along,
                            (line_bend->along_min+line_bend->along_max)*.5));
                    axis.direction=line_bend->along;
                    axis.display_length=
                        line_bend->along_max-line_bend->along_min+2;
                    axis.reference={operation.owner_id,"line-bend:axis",{}};
                    retain_originals(changed.faces,changed.edges,
                        changed.vertices);
                    retain_copy_solid(changed,{axis});
                    auto source=make_operation_result(changed.shape,
                        changed.faces,changed.edges,changed.vertices,true,
                        false,true);
                    source.mesh.original_references.axes.push_back(axis);
                    append_reference_geometry(original_references,
                        std::move(source.mesh.original_references));
                    result_shape=changed.shape;
                    auto topology=std::make_shared<LiveCache::Topology>();
                    topology->faces=std::move(changed.faces);
                    topology->edges=std::move(changed.edges);
                    topology->vertices=std::move(changed.vertices);
                    owned_topology=std::move(topology);
                    auto boundary=make_published_result(result_shape,
                        owned_topology->faces,owned_topology->edges,
                        owned_topology->vertices,true,persist_boundary_shape);
                    boundary.source_fingerprint=fingerprint(operations,
                        operation_index+1);
                    boundary.mesh.original_references=original_references;
                    boundary.mesh.axes.push_back(axis);
                    if(!boundaries.empty())
                        boundary.sheet_cuts=boundaries.back().sheet_cuts;
                    boundaries.push_back(std::move(boundary));
                    remember_live_boundary(
                        boundaries.back().source_fingerprint,result_shape,
                        owned_topology);
                }();
                continue;
            }
            if(const auto* state=std::get_if<SheetStateRequest>(&operation.primitive)) {
                auto regions=sheet_material::regions_before(operations,operation_index);
                static_cast<void>(sheet_material::change(regions,*state));
                std::size_t expected_solids=0;
                for(TopExp_Explorer it(result_shape,TopAbs_SOLID);it.More();it.Next())++expected_solids;
                std::optional<PrimitiveData> exact_authored_state;
                // A contiguous state-only suffix may return the sheet exactly
                // to the authored state which preceded that suffix. Reuse the
                // already validated live boundary in that one direction. This
                // is deliberately narrower than general state caching: any
                // intervening modeling feature ends the suffix, and partial or
                // different material states still rebuild their immutable
                // contributions through the ordinary calculation below.
                std::size_t authored_state_count=operation_index;
                while(authored_state_count>0) {
                    const auto& previous=operations[authored_state_count-1];
                    if(previous.suppressed||
                       std::holds_alternative<SheetStateRequest>(
                           previous.primitive)) {
                        --authored_state_count;
                        continue;
                    }
                    break;
                }
                if(authored_state_count>0&&
                   authored_state_count<=boundaries.size()&&
                   sheet_material::regions_before(operations,
                       authored_state_count).regions==regions.regions) {
                    const auto& authored_boundary=
                        boundaries[authored_state_count-1];
                    const auto cached=live_cache_->boundaries.find(
                        authored_boundary.source_fingerprint);
                    if(cached!=live_cache_->boundaries.end()&&
                       cached->second.topology&&
                       !cached->second.shape.IsNull()) {
                        std::size_t authored_solids=0;
                        for(TopExp_Explorer it(cached->second.shape,
                                TopAbs_SOLID);it.More();it.Next())
                            ++authored_solids;
                        if(authored_solids==expected_solids) {
                            PrimitiveData exact;
                            exact.shape=cached->second.shape;
                            exact.faces=cached->second.topology->faces;
                            exact.edges=cached->second.topology->edges;
                            exact.vertices=cached->second.topology->vertices;
                            sheet_state_geometry::identify(exact,
                                operation.owner_id);
                            exact_authored_state=std::move(exact);
                            if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))
                                std::cerr<<"[sheet state authored return] prefix="
                                    <<authored_state_count<<" modeling=0\n";
                        }
                    }
                }
                auto changed=exact_authored_state?
                    std::move(*exact_authored_state):
                    sheet_state_sources::calculate(sheet_sources,
                        regions.regions,state->tolerance,
                        operation.owner_id,expected_solids);
                const auto bend_lines=sheet_material::bend_lines(operations,operation_index,regions,operation.owner_id);
                // A state feature owns a new original solid. Earlier source
                // objects remain immutable; every new child carries its input
                // identity in its semantic key. Later placement uses the same
                // original-object contract as any other authored feature.
                retain_originals(changed.faces,changed.edges,changed.vertices);
                retain_copy_solid(changed,bend_lines);
                auto source=make_operation_result(changed.shape,changed.faces,
                    changed.edges,changed.vertices,true,false,true);
                source.mesh.original_references.axes=bend_lines;
                append_reference_geometry(original_references,
                    std::move(source.mesh.original_references));
                result_shape=changed.shape;
                auto topology=std::make_shared<LiveCache::Topology>();
                topology->faces=std::move(changed.faces);topology->edges=std::move(changed.edges);topology->vertices=std::move(changed.vertices);
                owned_topology=std::move(topology);
                auto boundary=make_published_result(result_shape,owned_topology->faces,
                    owned_topology->edges,owned_topology->vertices,true,persist_boundary_shape);
                boundary.source_fingerprint=fingerprint(operations,operation_index+1);
                boundary.mesh.original_references=original_references;
                boundary.mesh.axes=bend_lines;
                if(!boundaries.empty())boundary.sheet_cuts=boundaries.back().sheet_cuts;
                boundaries.push_back(std::move(boundary));
                remember_live_boundary(boundaries.back().source_fingerprint,result_shape,owned_topology);
                continue;
            }
            double group_rotation_span{};
            const auto make_group_child = [&](const FeatureGroupRequest::Child& child) {
                return std::visit([&](const auto& value)
                        -> PrimitiveData {
                    using Child = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<Child,
                                      ExtrusionRequest>) {
                        validate_extrusion(value);
                        const auto exact_target=has_forward_limit(value)?original_target(forward_limit(value)):std::nullopt;
                        const auto exact_reverse=value.reverse_limit?original_target(limit_view(*value.reverse_limit)):std::nullopt;
                        double forward_span = 2'000'000.0;
                        double reverse_span = 2'000'000.0;
                        const auto& extent_input=context.preview_wire||result_shape.IsNull()?context.preview_input:result_shape;
                        if ((value.extent ==
                                ExtrusionRequest::Extent::ThroughAll || value.through_all_reverse) &&
                            !extent_input.IsNull()) {
                            const Vec3 profile_origin = std::visit(
                                [](const auto& profile) {
                                    using Profile = std::decay_t<
                                        decltype(profile)>;
                                    if constexpr (std::is_same_v<Profile,
                                            ExtrusionRequest::PolygonProfile>) {
                                        return profile.vertices.front();
                                    } else if constexpr (std::is_same_v<Profile,
                                            ExtrusionRequest::CircleProfile> ||
                                        std::is_same_v<Profile,
                                            ExtrusionRequest::EllipseProfile>) {
                                        return profile.center;
                                    } else {
                                        return std::visit([](const auto& curve) {
                                            return curve.start;
                                        }, profile.curves.front());
                                    }
                                }, value.outer_profile);
                            const double direction_length = std::hypot(
                                std::hypot(value.direction.x,
                                    value.direction.y),
                                value.direction.z);
                            const Vec3 unit{value.direction.x /
                                    direction_length,
                                value.direction.y / direction_length,
                                value.direction.z / direction_length};
                            Bnd_Box bounds;
                            BRepBndLib::Add(extent_input, bounds);
                            Standard_Real xmin{}, ymin{}, zmin{},
                                xmax{}, ymax{}, zmax{};
                            bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
                            double forward{};
                            double reverse{};
                            for (unsigned mask = 0; mask < 8; ++mask) {
                                const Vec3 corner{mask & 1 ? xmax : xmin,
                                    mask & 2 ? ymax : ymin,
                                    mask & 4 ? zmax : zmin};
                                const double projection =
                                    (corner.x-profile_origin.x)*unit.x +
                                    (corner.y-profile_origin.y)*unit.y +
                                    (corner.z-profile_origin.z)*unit.z;
                                forward = std::max(forward, projection);
                                reverse = std::max(reverse, -projection);
                            }
                            const double diagonal = std::hypot(std::hypot(
                                xmax-xmin, ymax-ymin), zmax-zmin);
                            const double margin = std::max(
                                1.0, diagonal*1.0e-4);
                            forward_span = std::max(0.0, forward)+margin;
                            reverse_span = std::max(0.0, reverse)+margin;
                        }
                        const auto* opening = std::get_if<ThreadSurfaceRequest>(&operation.primitive);
                        // All surfaces of an opening share its radial seam plane.
                        // Ordinary extrusions retain their existing profile frame.
                        return make_extrusion_data(value,
                            operation.owner_id, exact_target,
                            forward_span, reverse_span, opening
                                ? std::optional<Vec3>{opening->radial_direction} : std::nullopt, operation.boolean_tolerance, exact_reverse,
                            original_ordered_targets(value.ordered_end_limit),&resolved_up_to);
                    } else {
                        if constexpr(std::is_same_v<Child,Sweep3DRequest>) {
                            validate_sweep3d(value);
                            return make_sweep3d_data(value,operation.owner_id);
                        } else {
                        validate_revolution(value);
                        if(const auto side=feature_side_parent(value.profile_region_id)) {
                            auto resolved=oriented_feature_revolution(value);
                            double span{};
                            auto result=make_revolution_data(resolved,operation.owner_id,
                                resolved.end_limit?original_target(limit_view(*resolved.end_limit)):std::nullopt,
                                operation.boolean_tolerance,&span,original_ordered_targets(resolved.ordered_end_limit),&resolved_up_to);
                            group_rotation_span+=span;
                            if(group_rotation_span>360.+1e-9)throw std::runtime_error("Combined rotation angle must not exceed 360 degrees.");
                            return result;
                        }
                        return make_revolution_data(value,operation.owner_id,
                            value.end_limit?original_target(limit_view(*value.end_limit)):std::nullopt,operation.boolean_tolerance,nullptr,
                            original_ordered_targets(value.ordered_end_limit),&resolved_up_to);
                        }
                    }
                }, child);
            };
            if (const auto* thread =
                    std::get_if<ThreadSurfaceRequest>(&operation.primitive)) {
                if (result_shape.IsNull()) {
                    if (!thread->shaft_face || !thread->shaft_face->surface)
                        throw std::invalid_argument("Thread surface requires a preceding solid body");
                    TopoDS_Compound empty;
                    BRep_Builder builder;builder.MakeCompound(empty);
                    result_shape=empty;
                    owned_topology=std::make_shared<LiveCache::Topology>();
                }
                auto shaft_request=*thread;
                if (thread->shaft_face) {
                    const auto resolve=[&](FaceReference& reference, bool shaft_support=false) {
                        const auto found=std::ranges::find_if(owned_topology->faces,[&](const auto& face) {
                            return face.reference==reference;
                        });
                        if (found==owned_topology->faces.end()) {
                            if (reference.surface) return;
                            throw std::runtime_error("Závit ztratil referenci bez uložené náhradní geometrie.");
                        }
                        // An ancestry entry can retain the operand orientation
                        // after a Boolean. Resolve the actual input-body face
                        // (same persisted identity/shape), including its current
                        // material orientation, during this explicit calculation.
                        for (TopExp_Explorer faces(result_shape,TopAbs_FACE);faces.More();faces.Next()) {
                            if (!faces.Current().IsSame(found->shape)) continue;
                            if (const auto surface=analytic_surface(TopoDS::Face(faces.Current()))) {
                                // The selected original cylinder defines the axial
                                // support, shared with the command preview. A
                                // chamfer shortens its visible fragment, not the
                                // authored thread. Only an explicitly selected
                                // entrance chamfer clips the root sheet. Retain
                                // the current material side for shaft validation.
                                if (shaft_support && reference.surface &&
                                    reference.surface->kind==SurfaceGeometry::Kind::Cylinder &&
                                    surface->kind==SurfaceGeometry::Kind::Cylinder) {
                                    const auto original=std::ranges::find_if(
                                        original_references.triangle_references,[&](const auto& candidate) {
                                            return candidate==reference && candidate.surface &&
                                                candidate.surface->kind==SurfaceGeometry::Kind::Cylinder;
                                        });
                                    auto support=original!=original_references.triangle_references.end()
                                        ? *original->surface : *reference.surface;
                                    support.reversed=surface->reversed;
                                    reference.surface=std::make_shared<const SurfaceGeometry>(std::move(support));
                                    break;
                                }
                                auto& target=surface->kind==SurfaceGeometry::Kind::Torus?reference.contour_surface:reference.surface;
                                target=std::make_shared<const SurfaceGeometry>(*surface);
                            }
                            break;
                        }
                    };
                    resolve(*shaft_request.shaft_face,true);resolve(shaft_request.shaft_start);
                    if (shaft_request.shaft_chamfer) resolve(*shaft_request.shaft_chamfer);
                    if (shaft_request.shaft_end) resolve(*shaft_request.shaft_end);
                    shaft_request=resolve_shaft_thread(std::move(shaft_request));
                    thread=&shaft_request;
                }
                if(thread->enabled && thread->end_plane_reference) {
                    if(!thread->end_plane_origin)throw std::runtime_error("The original opening end reference is unavailable.");
                    const std::vector<Vec3> no_triangles;
                    const ExtrusionLimitView limit{true,*thread->end_plane_reference,false,
                        *thread->end_plane_origin,thread->end_plane_normal,no_triangles};
                    const auto resolved=resolved_extrusion_limit(limit,original_target(limit));
                    validate_extrusion_limit(resolved,thread->axis_direction);
                    const auto n=resolved.normal,p=resolved.origin,o=thread->origin,d=thread->axis_direction;
                    const double denominator=n.x*d.x+n.y*d.y+n.z*d.z;
                    const double depth=(n.x*(p.x-o.x)+n.y*(p.y-o.y)+n.z*(p.z-o.z))/denominator;
                    if(!std::isfinite(depth)||depth<=0)throw std::runtime_error("Opening axis has an invalid length");
                    const double amplitude=thread->nominal_radius*std::sqrt(std::max(0.0,
                        (n.x*n.x+n.y*n.y+n.z*n.z)/(denominator*denominator)-1.0));
                    shaft_request.end_plane_origin=p;shaft_request.end_plane_normal=n;
                    shaft_request.length=depth+amplitude+1.0;thread=&shaft_request;
                }
                std::optional<ViewerAxis> opening_axis;
                std::optional<PrimitiveData> opening_reference_operand;
                const auto axial_end = [&](const TopoDS_Shape& shape) {
                    // Bounds in the opening's own axial frame avoid inflating
                    // a rotated bore's length by its radius or by world AABB corners.
                    gp_Trsf local;
                    local.SetTransformation(gp_Ax3(
                        gp_Pnt(thread->origin.x, thread->origin.y, thread->origin.z),
                        gp_Dir(thread->axis_direction.x, thread->axis_direction.y,
                            thread->axis_direction.z),
                        gp_Dir(thread->radial_direction.x, thread->radial_direction.y,
                            thread->radial_direction.z)));
                    const auto transformed = BRepBuilderAPI_Transform(shape, local, true).Shape();
                    Bnd_Box bounds;
                    BRepBndLib::AddOptimal(transformed, bounds, false, false);
                    if (bounds.IsVoid()) throw std::runtime_error("Opening axis has no extent");
                    Standard_Real xmin{}, ymin{}, zmin{}, xmax{}, ymax{}, zmax{};
                    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
                    return static_cast<double>(zmax);
                };
                const auto cut_group = [&](const std::shared_ptr<const FeatureGroupRequest>& group) {
                    if (!group) return;
                    for (const auto& child : group->children) {
                        auto operand = make_group_child(child);
                        if (!opening_axis) {
                            if (const auto* bore = std::get_if<ExtrusionRequest>(&child)) {
                                const double depth = bore->extent == ExtrusionRequest::Extent::ThroughAll
                                    ? axial_end(result_shape) : axial_end(operand.shape);
                                if (!std::isfinite(depth) || depth <= 0.0)
                                    throw std::runtime_error("Opening axis has an invalid length");
                                if(thread->enabled && bore->extent!=ExtrusionRequest::Extent::ThroughAll) {
                                    double bore_depth=depth;
                                    if(bore->extent==ExtrusionRequest::Extent::UpToPlane) {
                                        const auto limit=resolved_extrusion_limit(forward_limit(*bore),original_target(forward_limit(*bore)));
                                        const auto p=limit.origin,n=limit.normal,o=thread->origin,d=thread->axis_direction;
                                        bore_depth=(n.x*(p.x-o.x)+n.y*(p.y-o.y)+n.z*(p.z-o.z))/(n.x*d.x+n.y*d.y+n.z*d.z);
                                    }
                                    double thread_depth=thread->length;
                                    if(thread->end_plane_origin) {
                                        const auto p=*thread->end_plane_origin,n=thread->end_plane_normal,o=thread->origin,d=thread->axis_direction;
                                        const double denominator=n.x*d.x+n.y*d.y+n.z*d.z;
                                        thread_depth=(n.x*(p.x-o.x)+n.y*(p.y-o.y)+n.z*(p.z-o.z))/denominator;
                                        if(bore->extent==ExtrusionRequest::Extent::Blind)
                                            thread_depth+=thread->nominal_radius*std::sqrt(std::max(0.0,
                                                (n.x*n.x+n.y*n.y+n.z*n.z)/(denominator*denominator)-1.0));
                                    }
                                    if(!std::isfinite(thread_depth)||!std::isfinite(bore_depth)||thread_depth>bore_depth+1e-7)
                                        throw std::runtime_error("Závit včetně výběhu přesahuje válcovou hloubku otvoru");
                                }
                                opening_axis = ViewerAxis{
                                    {thread->origin.x + thread->axis_direction.x*depth*0.5,
                                     thread->origin.y + thread->axis_direction.y*depth*0.5,
                                     thread->origin.z + thread->axis_direction.z*depth*0.5},
                                    thread->axis_direction, depth,
                                    {operation.owner_id, "axis:primary", {}}, ""};
                                original_references.axes.push_back(*opening_axis);
                            }
                        }

                        // The original object of this composite feature is
                        // the joined cutting solid, not all intermediate tools.
                        // Its wire must not retain the bore's pre-chamfer rim.
                        if (!opening_reference_operand) {
                            opening_reference_operand = operand;
                        } else {
                            auto& combined = *opening_reference_operand;
                            BRepAlgoAPI_Fuse fuse;
                            set_boolean_inputs(fuse, combined.shape, operand.shape);
                            fuse.SetToFillHistory(true);
                            fuse.SetFuzzyValue(std::max(1.0e-7, operation.boolean_tolerance));
                            fuse.Build();
                            if (!fuse.IsDone() || fuse.Shape().IsNull() ||
                                !BRepCheck_Analyzer(fuse.Shape()).IsValid())
                                throw std::runtime_error("OCCT opening reference union failed");
                            combined.faces = propagate_topology(fuse, combined.faces, operand.faces);
                            combined.edges = propagate_topology(fuse, combined.edges, operand.edges);
                            combined.vertices = propagate_topology(fuse, combined.vertices, operand.vertices);
                            combined.shape = fuse.Shape();
                            combined.edges = complete_boolean_edges(combined.shape, combined.faces,
                                combined.edges, operation.owner_id, "opening");
                            combined.vertices = complete_boolean_vertices(combined.shape, combined.faces,
                                combined.edges, combined.vertices, operation.owner_id, "opening");
                        }
                        BRepAlgoAPI_Cut cut;
                        set_boolean_inputs(cut, result_shape, operand.shape);
                        cut.SetToFillHistory(true);
                        cut.SetFuzzyValue(std::max(1.0e-7, operation.boolean_tolerance));
                        cut.Build();
                        if (!cut.IsDone() || cut.Shape().IsNull() ||
                            !BRepCheck_Analyzer(cut.Shape()).IsValid())
                            throw std::runtime_error("OCCT opening cut failed");
                        auto topology = std::make_shared<LiveCache::Topology>(LiveCache::Topology{
                            propagate_topology(cut, owned_topology->faces, operand.faces),
                            propagate_topology(cut, owned_topology->edges, operand.edges),
                            propagate_topology(cut, owned_topology->vertices, operand.vertices),
                            propagate_display_edges(cut, owned_topology->hidden_display_edges)});
                        result_shape = cut.Shape();
                        topology->edges = complete_boolean_edges(result_shape,
                            topology->faces, topology->edges, operation.owner_id, "subtract");
                        topology->vertices = complete_boolean_vertices(result_shape, topology->faces,
                            topology->edges, topology->vertices, operation.owner_id, "subtract");
                        owned_topology = std::move(topology);
                        for (auto& surface : technological_surfaces) {
                            if (surface.shape.IsNull()) continue;
                            BRepAlgoAPI_Cut trim;
                            set_boolean_inputs(trim, surface.shape, operand.shape);
                            trim.SetFuzzyValue(std::max(1.0e-7, operation.boolean_tolerance));
                            trim.Build();
                            if (!trim.IsDone())
                                throw std::runtime_error("OCCT opening thread trim failed");
                            surface.shape = trim.Shape();
                        }
                    }
                };
                cut_group(thread->cuts_before);
                if (thread->enabled) {
                    const auto surface_begin=technological_surfaces.size();
                    auto resolved_thread = *thread;
                    if (thread->through_all_forward || thread->through_all_reverse) {
                        Bnd_Box bounds;
                        BRepBndLib::Add(result_shape, bounds);
                        Standard_Real xmin{}, ymin{}, zmin{}, xmax{}, ymax{}, zmax{};
                        bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
                        double minimum = std::numeric_limits<double>::infinity();
                        double maximum = -std::numeric_limits<double>::infinity();
                        for (const double x : {xmin, xmax})
                            for (const double y : {ymin, ymax})
                                for (const double z : {zmin, zmax}) {
                                    const double projection =
                                        (x-thread->origin.x)*thread->axis_direction.x +
                                        (y-thread->origin.y)*thread->axis_direction.y +
                                        (z-thread->origin.z)*thread->axis_direction.z;
                                    minimum = std::min(minimum, projection);
                                    maximum = std::max(maximum, projection);
                                }
                        if (thread->through_all_reverse)
                            resolved_thread.start_offset = minimum;
                        const double end_offset = thread->through_all_forward
                            ? maximum : thread->start_offset + thread->length;
                        resolved_thread.length = end_offset -
                            resolved_thread.start_offset;
                    }
                    const auto* thread_geometry = &resolved_thread;
                    const double start = thread_geometry->start_offset;
                    const double end = start + thread_geometry->length;
                    const double start_runout = std::clamp(
                        thread_geometry->runout_start, 0.0,
                        thread_geometry->length);
                    const double end_runout = std::clamp(
                        thread_geometry->runout_end, 0.0,
                        thread_geometry->length);
                    bool external_thread = thread_geometry->side ==
                        ThreadSurfaceRequest::Side::External;
                    if (thread_geometry->side ==
                            ThreadSurfaceRequest::Side::Automatic) {
                        const double middle = start + thread_geometry->length * 0.5;
                        const gp_Pnt axis_point(
                            thread_geometry->origin.x +
                                thread_geometry->axis_direction.x * middle,
                            thread_geometry->origin.y +
                                thread_geometry->axis_direction.y * middle,
                            thread_geometry->origin.z +
                                thread_geometry->axis_direction.z * middle);
                        BRepClass3d_SolidClassifier classifier(
                            result_shape, axis_point, 1.0e-7);
                        external_thread = classifier.State() == TopAbs_IN ||
                            classifier.State() == TopAbs_ON;
                    }
                    const double continuous_radius = external_thread
                        ? thread_geometry->nominal_radius
                        : thread_geometry->root_radius;
                    const double interrupted_radius = external_thread
                        ? thread_geometry->root_radius
                        : thread_geometry->nominal_radius;
                    const auto add_surface=[&](TopoDS_Shape shape,std::string role,double first,double last) {
                        const auto station=[&](double t) {return Vec3{thread_geometry->origin.x+thread_geometry->axis_direction.x*t,
                            thread_geometry->origin.y+thread_geometry->axis_direction.y*t,thread_geometry->origin.z+thread_geometry->axis_direction.z*t};};
                        const auto axis=thread_geometry->axis_direction;
                        auto first_key=role+":start",last_key=role+":end";
                        const std::string interrupted_role=external_thread?"root":"nominal";
                        const std::string continuous_role=external_thread?"nominal":"root";
                        const bool continuous=!thread->cuts_before&&!thread->shaft_face;
                        if(role=="runout:start") {
                            last_key=interrupted_role+":start";
                            if(continuous)first_key=continuous_role+":start";
                        } else if(role=="runout:end") {
                            first_key=interrupted_role+":end";
                            if(continuous)last_key=continuous_role+":end";
                        }
                        technological_surfaces.push_back({std::move(shape),operation.owner_id,std::move(role),
                            {station(first),axis,thread->end_plane_origin.value_or(station(last)),
                                thread->end_plane_origin?thread->end_plane_normal:axis,std::move(first_key),std::move(last_key)}});
                    };
                    if (!thread->cuts_before && !thread->shaft_face) add_surface(
                        make_thread_cylinder_face(*thread_geometry,
                            continuous_radius, start,
                            thread_geometry->length),
                        external_thread ? "nominal" : "root",start,start+thread_geometry->length);
                    const double interrupted_start = start + start_runout;
                    const double interrupted_length = std::max(
                        0.0, thread_geometry->length - start_runout - end_runout);
                    add_surface(
                        make_thread_cylinder_face(*thread_geometry,
                            interrupted_radius, interrupted_start,
                            interrupted_length),
                        external_thread ? "root" : "nominal",interrupted_start,interrupted_start+interrupted_length);
                    if (start_runout > 1.0e-9) {
                        add_surface(
                            make_thread_cone_face(*thread_geometry,
                                continuous_radius, interrupted_radius,
                                start, start_runout),
                            "runout:start",start,start+start_runout);
                    }
                    if (end_runout > 1.0e-9) {
                        add_surface(
                            make_thread_cone_face(*thread_geometry,
                                interrupted_radius, continuous_radius,
                                end - end_runout,
                                end_runout), "runout:end",end-end_runout,end);
                    }
                    // Shaft thread dimensions are independent of the solid.
                    // Keep the sheet even when a later shaft diameter change
                    // leaves it outside material; analytic end/chamfer references
                    // still define its axial boundaries.
                    if (thread->end_plane_origin) {
                        const auto& p=*thread->end_plane_origin;
                        const auto& n=thread->end_plane_normal;
                        const gp_Dir normal(n.x,n.y,n.z);
                        BRepBuilderAPI_MakeFace face(gp_Pln(gp_Pnt(p.x,p.y,p.z),normal),
                            -5e6,5e6,-5e6,5e6);
                        const double side=n.x*thread->axis_direction.x+
                            n.y*thread->axis_direction.y+n.z*thread->axis_direction.z;
                        const double sign=std::copysign(1.0,side);
                        BRepPrimAPI_MakeHalfSpace half(face.Face(),gp_Pnt(
                            p.x-sign*normal.X(),p.y-sign*normal.Y(),p.z-sign*normal.Z()));
                        for (auto index=surface_begin;index<technological_surfaces.size();++index) {
                            BRepAlgoAPI_Common clip;
                            set_boolean_inputs(clip, technological_surfaces[index].shape, half.Solid());
                            clip.SetFuzzyValue(operation.boolean_tolerance);
                            clip.Build();
                            if (!clip.IsDone()) throw std::runtime_error("OCCT thread Up To trim failed");
                            technological_surfaces[index].shape=clip.Shape();
                        }
                    }
                }
                cut_group(thread->cuts_after);
                if (opening_reference_operand) {
                    retain_copy_solid(*opening_reference_operand,
                        opening_axis
                            ? std::vector<ViewerAxis>{*opening_axis}
                            : std::vector<ViewerAxis>{});
                    const auto& combined = *opening_reference_operand;
                    const auto unified = unify_preserving_face_provenance(combined.shape,
                        combined.faces, combined.edges, combined.vertices,
                        std::max(1.0e-7, operation.boolean_tolerance));
                    retain_originals(unified.faces,unified.edges,unified.vertices);
                    auto references = make_operation_result(unified.shape, unified.faces,
                        unified.edges, unified.vertices, true, false, false,
                        unified.hidden_display_edges);
                    append_original_reference_geometry(original_references,
                        std::move(references.mesh));
                }
                auto boundary = make_published_result(result_shape,
                    owned_topology->faces, owned_topology->edges,
                    owned_topology->vertices, true, persist_boundary_shape,
                    false, owned_topology->hidden_display_edges);
                if (thread->shaft_face) {
                    boundary.shaft_thread_owner=operation.owner_id;
                    boundary.shaft_thread_references={*thread->shaft_face,thread->shaft_start,
                        thread->shaft_chamfer.value_or(FaceReference{}),thread->shaft_end.value_or(FaceReference{})};
                }
                if (opening_axis) boundary.mesh.axes.push_back(*opening_axis);
                append_technological_surfaces(boundary, operation.mesh_deflection);
                boundary.source_fingerprint = fingerprint(
                    operations, boundaries.size() + 1);
                boundaries.push_back(std::move(boundary));
                remember_live_boundary(boundaries.back().source_fingerprint,
                    result_shape, owned_topology);
                continue;
            }
            if (const auto* fillet = std::get_if<FilletRequest>(&operation.primitive)) {
                apply_edge_treatment(*fillet);
                continue;
            }
            if (const auto* chamfer = std::get_if<ChamferRequest>(&operation.primitive)) {
                apply_edge_treatment(*chamfer);
                continue;
            }
            if (const auto* shell = std::get_if<ShellRequest>(&operation.primitive)) {
                auto shell_input_shape=result_shape;
                auto shell_input_topology=owned_topology;
                const bool external=!shell->source_body.empty();
                if(external) {
                    const auto source=context.external_shell_bodies.find(shell->source_body);
                    if(shell->thickness!=0.||source==context.external_shell_bodies.end())throw std::invalid_argument("Shell parameters are invalid");
                    shell_input_shape=source->second.shape;
                    shell_input_topology=std::make_shared<LiveCache::Topology>(LiveCache::Topology{source->second.faces,source->second.edges,source->second.vertices,{}});
                }
                if (shell_input_shape.IsNull()) {
                    throw std::invalid_argument("Shell requires an input body");
                }
                if (operation.operation != BooleanOperation::Add ||
                    !std::isfinite(shell->thickness) ||
                    shell->thickness < 0.0 ||
                    std::any_of(shell->removed_faces.begin(),
                        shell->removed_faces.end(), [](const auto& face) {
                            return !face.valid() ||
                                !face.instance_path.empty();
                        })) {
                    throw std::invalid_argument("Shell parameters are invalid");
                }
                TopTools_IndexedMapOfShape solids;
                TopExp::MapShapes(shell_input_shape, TopAbs_SOLID, solids);
                if (solids.IsEmpty() || (shell->thickness == 0. && solids.Extent() != 1)) {
                    throw std::invalid_argument(
                        "Shell requires exactly one connected solid");
                }

                std::set<std::pair<std::string, std::string>> selected_ids;
                TopTools_ListOfShape selected_face_shapes;
                for (const auto& requested : shell->removed_faces) {
                    if (!selected_ids.emplace(
                            requested.owner_id, requested.semantic_key).second) {
                        throw std::invalid_argument(
                            "Shell face references must be unique");
                    }
                    std::vector<const OwnedFace*> matches;
                    for (const auto& owned : shell_input_topology->faces) {
                        if (owned.reference.owner_id == requested.owner_id &&
                            owned.reference.semantic_key ==
                                requested.semantic_key) {
                            matches.push_back(&owned);
                        }
                    }
                    if (matches.size() != 1) {
                        throw std::runtime_error(
                            "Shell face is missing or ambiguous at this history boundary");
                    }
                    selected_face_shapes.Append(matches.front()->shape);
                }

                if(shell->thickness==0.) {
                    TopoDS_Compound remaining;BRep_Builder builder;builder.MakeCompound(remaining);
                    TopTools_IndexedMapOfShape solid_members;TopExp::MapShapes(solids.FindKey(1),solid_members);
                    for(TopTools_ListIteratorOfListOfShape it(selected_face_shapes);it.More();it.Next())
                        if(!solid_members.Contains(it.Value()))
                            throw std::runtime_error("Shell face is missing or ambiguous at this history boundary");
                    for(TopExp_Explorer face(solids.FindKey(1),TopAbs_FACE);face.More();face.Next()) {
                        bool removed=false;
                        for(TopTools_ListIteratorOfListOfShape it(selected_face_shapes);it.More();it.Next())
                            removed=removed||it.Value().IsSame(face.Current());
                        if(!removed)builder.Add(remaining,face.Current());
                    }
                    TopTools_IndexedMapOfShape skin_faces;TopExp::MapShapes(remaining,TopAbs_FACE,skin_faces);
                    if(skin_faces.IsEmpty())throw std::runtime_error("Select fewer faces; the surface shell must retain a face.");
                    // A shell must be connected. Removing walls may leave
                    // separate pieces; group exact faces by shared native edges.
                    TopoDS_Compound skin;builder.MakeCompound(skin);
                    TopTools_IndexedDataMapOfShapeListOfShape neighbors;
                    TopExp::MapShapesAndAncestors(remaining,TopAbs_EDGE,TopAbs_FACE,neighbors);
                    TopTools_IndexedMapOfShape visited;
                    for(int seed=1;seed<=skin_faces.Extent();++seed) {
                        if(visited.Contains(skin_faces.FindKey(seed)))continue;
                        TopoDS_Shell piece;builder.MakeShell(piece);
                        std::vector<TopoDS_Shape> queue{skin_faces.FindKey(seed)};visited.Add(queue.front());
                        for(std::size_t index=0;index<queue.size();++index) {
                            const auto face=queue[index];builder.Add(piece,face);
                            for(TopExp_Explorer edge(face,TopAbs_EDGE);edge.More();edge.Next())
                                for(TopTools_ListIteratorOfListOfShape n(neighbors.FindFromKey(edge.Current()));n.More();n.Next())
                                    if(!visited.Contains(n.Value())){visited.Add(n.Value());queue.push_back(n.Value());}
                        }
                        builder.Add(skin,piece);
                    }
                    TopTools_IndexedMapOfShape present;TopExp::MapShapes(skin,present);
                    if(!BRepCheck_Analyzer(skin,true,false,true).IsValid())
                        throw std::runtime_error("Surface shell could not retain valid input surfaces.");
                    auto topology=std::make_shared<LiveCache::Topology>();
                    const auto copy=[&](const auto& source,auto& destination) {
                        for(const auto& item:source)if(present.Contains(item.shape)) {
                            auto child=item;child.reference.owner_id=operation.owner_id;
                            child.reference.semantic_key=surface_shell_child_key(item.reference.owner_id,item.reference.semantic_key);
                            if constexpr(requires{child.reference.surface_result;})child.reference.surface_result=true;
                            destination.push_back(std::move(child));
                        }
                    };
                    copy(shell_input_topology->faces,topology->faces);copy(shell_input_topology->edges,topology->edges);copy(shell_input_topology->vertices,topology->vertices);
                    require_complete_unique_shell_topology(skin,TopAbs_FACE,topology->faces,"faces");
                    require_complete_unique_shell_topology(skin,TopAbs_EDGE,topology->edges,"edges");
                    require_complete_unique_shell_topology(skin,TopAbs_VERTEX,topology->vertices,"vertices");
                    // Unrelated surface geometry in a mixed Body retains its
                    // shape and owner. Only the actual solid is converted.
                    TopoDS_Compound combined;builder.MakeCompound(combined);builder.Add(combined,skin);
                    for(TopExp_Explorer face(result_shape,TopAbs_FACE);face.More();face.Next())
                        if(external||!solid_members.Contains(face.Current()))builder.Add(combined,face.Current());
                    const auto retain_other=[&](const auto& source,auto& destination) {
                        for(const auto& item:source)if(external||!solid_members.Contains(item.shape))destination.push_back(item);
                    };
                    retain_other(owned_topology->faces,topology->faces);retain_other(owned_topology->edges,topology->edges);
                    retain_other(owned_topology->vertices,topology->vertices);
                    result_shape=combined;owned_topology=std::move(topology);
                    retain_originals(owned_topology->faces,owned_topology->edges,owned_topology->vertices);
                    auto output=make_published_result(result_shape,owned_topology->faces,owned_topology->edges,owned_topology->vertices,true,persist_boundary_shape,true);
                    append_reference_geometry(original_references,output.mesh.original_references);
                    output.mesh.original_references=original_references;
                    output.source_fingerprint=fingerprint(operations,operation_index+1);
                    boundaries.push_back(std::move(output));
                    remember_live_boundary(boundaries.back().source_fingerprint,result_shape,owned_topology);
                    continue;
                }
                // Reuse the existing connected-solid algorithm for every
                // affected solid. Publish only one combined history boundary.
                const auto hollow_solid = [&](const TopoDS_Shape& solid,
                    std::shared_ptr<const LiveCache::Topology> owned_topology,
                    const ShellRequest& request,
                    const TopTools_ListOfShape& selected_face_shapes)
                    -> std::pair<TopoDS_Shape, std::shared_ptr<const LiveCache::Topology>> {
                    const auto* shell = &request;
                    TopoDS_Shape result_shape = solid;
                    const double shell_tolerance =
                        std::max(1.0e-7, operation.boolean_tolerance);
                    const auto volume_of = [](const TopoDS_Shape& shape) {
                        GProp_GProps properties;
                        BRepGProp::VolumeProperties(shape, properties);
                        return properties.Mass();
                    };
                    const double input_volume = volume_of(solid);
                    const double volume_tolerance = std::max(
                        1.0e-12, std::abs(input_volume) * 1.0e-10);
                    const std::vector<std::pair<TopoDS_Edge, EdgeReference>>
                        no_selected_edges;

                    // Prefer OCCT's native open-shell operation. It offsets the
                    // body and joins all selected openings as one calculation,
                    // so adjacent faces have no artificial wall between them.
                    // A previous implementation accepted IsDone() alone, but
                    // OCCT can report success while returning the unchanged
                    // input on some treated bodies. Validate the actual solid
                    // and volume before accepting it.
                    if (!shell->removed_faces.empty()) {
                        std::unique_ptr<BRepOffsetAPI_MakeThickSolid>
                            direct_shell;
                        auto candidate = std::make_unique<
                            BRepOffsetAPI_MakeThickSolid>();
                        candidate->MakeThickSolidByJoin(
                            solid, selected_face_shapes,
                            -shell->thickness, shell_tolerance,
                            BRepOffset_Skin, false, false, GeomAbs_Arc, true);
                        if (candidate->IsDone() &&
                            !candidate->Shape().IsNull() &&
                            BRepCheck_Analyzer(candidate->Shape()).IsValid()) {
                            TopTools_IndexedMapOfShape direct_solids;
                            TopExp::MapShapes(candidate->Shape(), TopAbs_SOLID,
                                direct_solids);
                            const double direct_volume =
                                volume_of(candidate->Shape());
                            if (direct_solids.Extent() == 1 &&
                                direct_volume > volume_tolerance &&
                                direct_volume <
                                    input_volume - volume_tolerance) {
                                direct_shell = std::move(candidate);
                            }
                        }
                        if (direct_shell) {
                            auto direct_faces = shell_result_faces(*direct_shell,
                                direct_shell->Shape(), owned_topology->faces,
                                owned_topology->edges,
                                owned_topology->vertices,
                                operation.owner_id, "shell:face");
                            auto direct_edges = complete_shell_edges(
                                direct_shell->Shape(), direct_faces,
                                keep_unambiguous_topology_references(
                                    propagate_topology(*direct_shell,
                                        owned_topology->edges,
                                        std::vector<OwnedEdge>{})),
                                operation.owner_id, "shell");
                            auto direct_vertices =
                                complete_shell_vertices(
                                    direct_shell->Shape(), direct_edges,
                                    keep_unambiguous_topology_references(
                                        propagate_topology(*direct_shell,
                                            owned_topology->vertices,
                                            std::vector<OwnedVertex>{})),
                                    no_selected_edges, operation.owner_id,
                                    "shell:vertex");
                            require_complete_unique_shell_topology(
                                direct_shell->Shape(), TopAbs_FACE,
                                direct_faces, "faces");
                            require_complete_unique_shell_topology(
                                direct_shell->Shape(), TopAbs_EDGE,
                                direct_edges, "edges");
                            require_complete_unique_shell_topology(
                                direct_shell->Shape(), TopAbs_VERTEX,
                                direct_vertices, "vertices");
                            result_shape = direct_shell->Shape();
                            owned_topology =
                                std::make_shared<LiveCache::Topology>(
                                    LiveCache::Topology{
                                        std::move(direct_faces),
                                        std::move(direct_edges),
                                        std::move(direct_vertices), {}});
                            return {result_shape, std::move(owned_topology)};
                        }
                    }

                    // With an empty closing-face list OCCT produces the inward
                    // offset solid itself. The required closed Shell is the
                    // material between the original body and that inner solid.
                    // Building this explicitly also avoids MakeThickSolid's
                    // false-success result (unchanged input body) on bodies with
                    // several consecutive Fillet/Chamfer operations.
                    TopTools_ListOfShape no_closing_faces;
                    BRepOffsetAPI_MakeThickSolid inner_offset;
                    inner_offset.MakeThickSolidByJoin(
                        solid, no_closing_faces, -shell->thickness,
                        shell_tolerance, BRepOffset_Skin, false, false,
                        GeomAbs_Arc, false);
                    if (!inner_offset.IsDone() || inner_offset.Shape().IsNull() ||
                        !BRepCheck_Analyzer(inner_offset.Shape()).IsValid()) {
                        throw std::runtime_error("OCCT Shell inner offset failed");
                    }
                    TopTools_IndexedMapOfShape inner_solids;
                    TopExp::MapShapes(
                        inner_offset.Shape(), TopAbs_SOLID, inner_solids);
                    const double inner_volume = volume_of(inner_offset.Shape());
                    if (inner_solids.Extent() != 1 ||
                        inner_volume <= volume_tolerance ||
                        inner_volume >= input_volume - volume_tolerance) {
                        throw std::runtime_error(
                            "Shell thickness does not leave one valid inner cavity");
                    }

                    auto inner_faces = shell_result_faces(inner_offset,
                        inner_offset.Shape(), owned_topology->faces,
                        owned_topology->edges, owned_topology->vertices,
                        operation.owner_id, "shell:inner-face");
                    auto inner_edges = complete_shell_edges(
                        inner_offset.Shape(), inner_faces,
                        keep_unambiguous_topology_references(propagate_topology(
                            inner_offset, owned_topology->edges,
                            std::vector<OwnedEdge>{})),
                        operation.owner_id, "shell:inner");
                    auto inner_vertices = complete_shell_vertices(
                        inner_offset.Shape(), inner_edges,
                        keep_unambiguous_topology_references(propagate_topology(
                            inner_offset, owned_topology->vertices,
                            std::vector<OwnedVertex>{})),
                        no_selected_edges, operation.owner_id,
                        "shell:inner-vertex");

                    BRepAlgoAPI_Cut closed_wall;
                    set_boolean_inputs(closed_wall, solid, inner_offset.Shape());
                    closed_wall.SetToFillHistory(true);
                    closed_wall.SetFuzzyValue(shell_tolerance);
                    closed_wall.Build();
                    if (!closed_wall.IsDone() || closed_wall.Shape().IsNull() ||
                        !BRepCheck_Analyzer(closed_wall.Shape()).IsValid()) {
                        throw std::runtime_error("OCCT closed Shell failed");
                    }
                    TopTools_IndexedMapOfShape wall_solids;
                    TopExp::MapShapes(closed_wall.Shape(), TopAbs_SOLID, wall_solids);
                    const double closed_wall_volume = volume_of(closed_wall.Shape());
                    if (wall_solids.Extent() != 1 ||
                        closed_wall_volume <= volume_tolerance ||
                        closed_wall_volume >= input_volume - volume_tolerance) {
                        throw std::runtime_error(
                            "OCCT closed Shell did not create a valid wall");
                    }

                    auto wall_face_inputs = owned_topology->faces;
                    wall_face_inputs.insert(wall_face_inputs.end(),
                        inner_faces.begin(), inner_faces.end());
                    auto wall_edge_inputs = owned_topology->edges;
                    wall_edge_inputs.insert(wall_edge_inputs.end(),
                        inner_edges.begin(), inner_edges.end());
                    auto wall_vertex_inputs = owned_topology->vertices;
                    wall_vertex_inputs.insert(wall_vertex_inputs.end(),
                        inner_vertices.begin(), inner_vertices.end());
                    auto shell_faces = shell_result_faces(closed_wall,
                        closed_wall.Shape(), wall_face_inputs, wall_edge_inputs,
                        wall_vertex_inputs, operation.owner_id,
                        "shell:wall-face");
                    auto shell_edges = complete_shell_edges(closed_wall.Shape(),
                        shell_faces,
                        keep_unambiguous_topology_references(propagate_topology(
                            closed_wall, owned_topology->edges, inner_edges)),
                        operation.owner_id, "shell:wall");
                    auto shell_vertices = complete_shell_vertices(
                        closed_wall.Shape(), shell_edges,
                        keep_unambiguous_topology_references(propagate_topology(
                            closed_wall, owned_topology->vertices,
                            inner_vertices)),
                        no_selected_edges, operation.owner_id,
                        "shell:wall-vertex");
                    TopoDS_Shape shell_shape = closed_wall.Shape();

                    if (!shell->removed_faces.empty()) {
                        struct ShellOpeningTool {
                            std::unique_ptr<BRepOffsetAPI_MakeThickSolid>
                                algorithm;
                            std::vector<OwnedFace> faces;
                            std::vector<OwnedEdge> edges;
                            std::vector<OwnedVertex> vertices;
                        };
                        const auto make_opening_tool =
                            [&](const OwnedFace& source,
                                const FaceReference& conceptual_source,
                                const std::vector<OwnedEdge>& source_topology_edges,
                                const std::vector<OwnedVertex>&
                                    source_topology_vertices,
                                std::string_view tool_role) {
                            GProp_GProps face_properties;
                            BRepGProp::SurfaceProperties(
                                source.shape, face_properties);
                            const double expected_removed_volume = std::max(
                                volume_tolerance,
                                face_properties.Mass() * shell->thickness);
                            struct Candidate {
                                double score{
                                    std::numeric_limits<double>::infinity()};
                                double removed_volume{};
                                std::unique_ptr<
                                    BRepOffsetAPI_MakeThickSolid> tool;
                            };
                            Candidate best;
                            for (const bool reverse_face : {false, true}) {
                                for (const double direction : {-1.1, 1.1}) {
                                    auto tool = std::make_unique<
                                        BRepOffsetAPI_MakeThickSolid>();
                                    auto tool_face = TopoDS::Face(source.shape);
                                    if (reverse_face) tool_face.Reverse();
                                    tool->MakeThickSolidBySimple(tool_face,
                                        shell->thickness * direction);
                                    if (!tool->IsDone() ||
                                        tool->Shape().IsNull() ||
                                        !BRepCheck_Analyzer(
                                            tool->Shape()).IsValid()) {
                                        continue;
                                    }
                                    BRepAlgoAPI_Cut probe;
                                    set_boolean_inputs(probe, shell_shape, tool->Shape());
                                    probe.SetFuzzyValue(shell_tolerance);
                                    probe.Build();
                                    if (!probe.IsDone() ||
                                        probe.Shape().IsNull() ||
                                        !BRepCheck_Analyzer(
                                            probe.Shape()).IsValid()) {
                                        continue;
                                    }
                                    TopTools_IndexedMapOfShape probe_solids;
                                    TopExp::MapShapes(probe.Shape(),
                                        TopAbs_SOLID, probe_solids);
                                    const double candidate_volume =
                                        volume_of(probe.Shape());
                                    const double removed_volume =
                                        closed_wall_volume - candidate_volume;
                                    if (probe_solids.Extent() != 1 ||
                                        candidate_volume <= volume_tolerance ||
                                        removed_volume <= volume_tolerance) {
                                        continue;
                                    }
                                    const double score = std::abs(
                                        removed_volume -
                                        expected_removed_volume) /
                                        expected_removed_volume;
                                    if (score < best.score ||
                                        (score == best.score &&
                                         removed_volume <
                                             best.removed_volume)) {
                                        best.score = score;
                                        best.removed_volume = removed_volume;
                                        best.tool = std::move(tool);
                                    }
                                }
                            }
                            if (!best.tool) {
                                throw std::runtime_error(
                                    "OCCT Shell could not construct an opening tool");
                            }

                            TopTools_IndexedMapOfShape source_edges;
                            TopTools_IndexedMapOfShape source_vertices;
                            TopExp::MapShapes(
                                source.shape, TopAbs_EDGE, source_edges);
                            TopExp::MapShapes(
                                source.shape, TopAbs_VERTEX, source_vertices);
                            std::vector<OwnedEdge> face_edges;
                            std::ranges::copy_if(source_topology_edges,
                                std::back_inserter(face_edges),
                                [&](const auto& edge) {
                                    return source_edges.Contains(edge.shape);
                                });
                            std::vector<OwnedVertex> face_vertices;
                            std::ranges::copy_if(source_topology_vertices,
                                std::back_inserter(face_vertices),
                                [&](const auto& vertex) {
                                    return source_vertices.Contains(
                                        vertex.shape);
                                });
                            auto tool_faces = shell_opening_tool_faces(
                                best.tool->Shape(), source,
                                conceptual_source, face_edges,
                                operation.owner_id, tool_role);
                            auto tool_edges = complete_shell_edges(
                                best.tool->Shape(), tool_faces,
                                keep_unambiguous_topology_references(
                                    propagate_topology(*best.tool, face_edges,
                                        std::vector<OwnedEdge>{})),
                                operation.owner_id,
                                std::string(tool_role) + ":edge");
                            auto tool_vertices =
                                complete_shell_vertices(
                                    best.tool->Shape(), tool_edges,
                                    keep_unambiguous_topology_references(
                                        propagate_topology(*best.tool,
                                            face_vertices,
                                            std::vector<OwnedVertex>{})),
                                    no_selected_edges, operation.owner_id,
                                    std::string(tool_role) + ":vertex");
                            return ShellOpeningTool{
                                std::move(best.tool), std::move(tool_faces),
                                std::move(tool_edges),
                                std::move(tool_vertices)};
                        };

                        std::vector<ShellOpeningTool> inner_opening_tools;
                        std::vector<ShellOpeningTool> outer_opening_tools;
                        inner_opening_tools.reserve(
                            shell->removed_faces.size());
                        outer_opening_tools.reserve(
                            shell->removed_faces.size());
                        TopTools_ListOfShape opening_tool_shapes;
                        std::vector<OwnedFace> opening_tool_faces;
                        std::vector<OwnedEdge> opening_tool_edges;
                        std::vector<OwnedVertex> opening_tool_vertices;

                        for (const auto& requested :
                             shell->removed_faces) {
                            const auto matches_reference =
                                [&](const auto& owned) {
                                    return owned.reference.owner_id ==
                                            requested.owner_id &&
                                        owned.reference.semantic_key ==
                                            requested.semantic_key;
                                };
                            const auto input = std::ranges::find_if(
                                owned_topology->faces, matches_reference);
                            const auto outer = std::ranges::find_if(
                                shell_faces, matches_reference);
                            if (input == owned_topology->faces.end() ||
                                outer == shell_faces.end()) {
                                throw std::runtime_error(
                                    "Shell opening lost its selected source face");
                            }

                            std::vector<OwnedFace> inner_matches;
                            const auto append_inner_match =
                                [&](const TopoDS_Shape& shape) {
                                    if (shape.ShapeType() != TopAbs_FACE) return;
                                    for (const auto& inner_face : inner_faces) {
                                        if (!inner_face.shape.IsSame(shape) ||
                                            std::any_of(inner_matches.begin(),
                                                inner_matches.end(),
                                                [&](const auto& value) {
                                                    return value.shape.IsSame(
                                                        shape);
                                                })) {
                                            continue;
                                        }
                                        inner_matches.push_back(inner_face);
                                    }
                                };
                            for (const auto* descendants : {
                                    &inner_offset.Modified(input->shape),
                                    &inner_offset.Generated(input->shape)}) {
                                for (TopTools_ListIteratorOfListOfShape iterator(
                                        *descendants);
                                     iterator.More(); iterator.Next()) {
                                    append_inner_match(iterator.Value());
                                }
                            }
                            if (inner_matches.size() != 1) {
                                throw std::runtime_error(
                                    "Shell opening has no unique inner offset face");
                            }

                            auto inner_tool = make_opening_tool(
                                inner_matches.front(), requested, inner_edges,
                                inner_vertices, "shell:opening-inner-tool");
                            opening_tool_shapes.Append(
                                inner_tool.algorithm->Shape());
                            opening_tool_faces.insert(
                                opening_tool_faces.end(),
                                inner_tool.faces.begin(),
                                inner_tool.faces.end());
                            opening_tool_edges.insert(
                                opening_tool_edges.end(),
                                inner_tool.edges.begin(),
                                inner_tool.edges.end());
                            opening_tool_vertices.insert(
                                opening_tool_vertices.end(),
                                inner_tool.vertices.begin(),
                                inner_tool.vertices.end());
                            inner_opening_tools.push_back(
                                std::move(inner_tool));
                            outer_opening_tools.push_back(make_opening_tool(
                                *outer, requested, shell_edges, shell_vertices,
                                "shell:opening-outer-tool"));
                        }

                        // Inner-face tools preserve the rims of unselected
                        // neighboring walls, but two adjacent selected faces
                        // leave a corner prism between their offset boundaries.
                        // The intersection of their corresponding outer-face
                        // tools is exactly that missing shared corner. Add only
                        // this intersection, never either complete outer tool.
                        for (std::size_t first = 0;
                             first < outer_opening_tools.size(); ++first) {
                            for (std::size_t second = first + 1;
                                 second < outer_opening_tools.size(); ++second) {
                                auto& first_tool =
                                    outer_opening_tools[first];
                                auto& second_tool =
                                    outer_opening_tools[second];
                                BRepAlgoAPI_Common bridge;
                                set_boolean_inputs(bridge, first_tool.algorithm->Shape(), second_tool.algorithm->Shape());
                                bridge.SetToFillHistory(true);
                                bridge.SetFuzzyValue(shell_tolerance);
                                bridge.Build();
                                if (!bridge.IsDone() ||
                                    bridge.Shape().IsNull() ||
                                    !BRepCheck_Analyzer(
                                        bridge.Shape()).IsValid() ||
                                    volume_of(bridge.Shape()) <=
                                        volume_tolerance) {
                                    continue;
                                }
                                auto bridge_face_inputs = first_tool.faces;
                                bridge_face_inputs.insert(
                                    bridge_face_inputs.end(),
                                    second_tool.faces.begin(),
                                    second_tool.faces.end());
                                auto bridge_edge_inputs = first_tool.edges;
                                bridge_edge_inputs.insert(
                                    bridge_edge_inputs.end(),
                                    second_tool.edges.begin(),
                                    second_tool.edges.end());
                                auto bridge_vertex_inputs =
                                    first_tool.vertices;
                                bridge_vertex_inputs.insert(
                                    bridge_vertex_inputs.end(),
                                    second_tool.vertices.begin(),
                                    second_tool.vertices.end());
                                auto bridge_faces = shell_result_faces(
                                    bridge, bridge.Shape(),
                                    bridge_face_inputs, bridge_edge_inputs,
                                    bridge_vertex_inputs,
                                    operation.owner_id,
                                    "shell:opening-bridge-face");
                                auto bridge_edges = complete_shell_edges(
                                    bridge.Shape(), bridge_faces,
                                    keep_unambiguous_topology_references(
                                        propagate_topology(bridge,
                                            first_tool.edges,
                                            second_tool.edges)),
                                    operation.owner_id,
                                    "shell:opening-bridge");
                                auto bridge_vertices =
                                    complete_shell_vertices(
                                        bridge.Shape(), bridge_edges,
                                        keep_unambiguous_topology_references(
                                            propagate_topology(bridge,
                                                first_tool.vertices,
                                                second_tool.vertices)),
                                        no_selected_edges,
                                        operation.owner_id,
                                        "shell:opening-bridge-vertex");
                                opening_tool_shapes.Append(bridge.Shape());
                                opening_tool_faces.insert(
                                    opening_tool_faces.end(),
                                    bridge_faces.begin(),
                                    bridge_faces.end());
                                opening_tool_edges.insert(
                                    opening_tool_edges.end(),
                                    bridge_edges.begin(),
                                    bridge_edges.end());
                                opening_tool_vertices.insert(
                                    opening_tool_vertices.end(),
                                    bridge_vertices.begin(),
                                    bridge_vertices.end());
                            }
                        }

                        TopTools_ListOfShape opening_arguments;
                        opening_arguments.Append(shell_shape);
                        BRepAlgoAPI_Cut opening_cut;
                        opening_cut.SetArguments(opening_arguments);
                        opening_cut.SetTools(opening_tool_shapes);
                        opening_cut.SetToFillHistory(true);
                        opening_cut.SetFuzzyValue(shell_tolerance);
                        opening_cut.Build();
                        if (!opening_cut.IsDone() ||
                            opening_cut.Shape().IsNull() ||
                            !BRepCheck_Analyzer(
                                opening_cut.Shape()).IsValid()) {
                            throw std::runtime_error(
                                "OCCT Shell could not create the selected openings");
                        }
                        TopTools_IndexedMapOfShape opened_solids;
                        TopExp::MapShapes(opening_cut.Shape(),
                            TopAbs_SOLID, opened_solids);
                        const double opened_volume =
                            volume_of(opening_cut.Shape());
                        if (opened_solids.Extent() != 1 ||
                            opened_volume <= volume_tolerance ||
                            opened_volume >=
                                closed_wall_volume - volume_tolerance) {
                            throw std::runtime_error(
                                "OCCT Shell openings did not create one valid wall");
                        }

                        auto opening_face_inputs = shell_faces;
                        opening_face_inputs.insert(
                            opening_face_inputs.end(),
                            opening_tool_faces.begin(),
                            opening_tool_faces.end());
                        auto opening_edge_inputs = shell_edges;
                        opening_edge_inputs.insert(
                            opening_edge_inputs.end(),
                            opening_tool_edges.begin(),
                            opening_tool_edges.end());
                        auto opening_vertex_inputs = shell_vertices;
                        opening_vertex_inputs.insert(
                            opening_vertex_inputs.end(),
                            opening_tool_vertices.begin(),
                            opening_tool_vertices.end());
                        auto next_faces = shell_result_faces(
                            opening_cut, opening_cut.Shape(),
                            opening_face_inputs, opening_edge_inputs,
                            opening_vertex_inputs, operation.owner_id,
                            "shell:opening-face");
                        auto next_edges = complete_shell_edges(
                            opening_cut.Shape(), next_faces,
                            keep_unambiguous_topology_references(
                                propagate_topology(opening_cut, shell_edges,
                                    opening_tool_edges)),
                            operation.owner_id, "shell:opening");
                        auto next_vertices =
                            complete_shell_vertices(
                                opening_cut.Shape(), next_edges,
                                keep_unambiguous_topology_references(
                                    propagate_topology(opening_cut,
                                        shell_vertices,
                                        opening_tool_vertices)),
                                no_selected_edges, operation.owner_id,
                                "shell:opening-vertex");
                        shell_shape = opening_cut.Shape();
                        shell_faces = std::move(next_faces);
                        shell_edges = std::move(next_edges);
                        shell_vertices = std::move(next_vertices);
                    }

                    result_shape = std::move(shell_shape);
                    require_complete_unique_shell_topology(result_shape,
                        TopAbs_FACE, shell_faces, "faces");
                    require_complete_unique_shell_topology(result_shape,
                        TopAbs_EDGE, shell_edges, "edges");
                    require_complete_unique_shell_topology(result_shape,
                        TopAbs_VERTEX, shell_vertices, "vertices");
                    owned_topology = std::make_shared<LiveCache::Topology>(
                        LiveCache::Topology{std::move(shell_faces),
                            std::move(shell_edges), std::move(shell_vertices), {}});
                    return {result_shape, std::move(owned_topology)};
                };
                if (solids.Extent() == 1) {
                    std::tie(result_shape, owned_topology) = hollow_solid(
                        solids.FindKey(1), owned_topology, *shell, selected_face_shapes);
                } else {
                    std::vector<TopTools_IndexedMapOfShape> members(solids.Extent());
                    std::vector<ShellRequest> requests(solids.Extent(), *shell);
                    std::vector<TopTools_ListOfShape> openings(solids.Extent());
                    TopTools_IndexedMapOfShape all_solid_members, changed_members;
                    for (int i=1; i<=solids.Extent(); ++i) {
                        TopExp::MapShapes(solids.FindKey(i), members[i-1]);
                        TopExp::MapShapes(solids.FindKey(i), all_solid_members);
                        requests[i-1].removed_faces.clear();
                    }
                    std::size_t selected_index{};
                    for (TopTools_ListIteratorOfListOfShape it(selected_face_shapes); it.More(); it.Next(), ++selected_index) {
                        int owner=-1;
                        for (int i=0; i<solids.Extent(); ++i) if (members[i].Contains(it.Value())) {
                            if (owner!=-1) throw std::runtime_error("Shell face is missing or ambiguous at this history boundary");
                            owner=i;
                        }
                        if (owner==-1) throw std::runtime_error("Shell face is missing or ambiguous at this history boundary");
                        requests[owner].removed_faces.push_back(shell->removed_faces.at(selected_index));
                        openings[owner].Append(it.Value());
                    }
                    BRep_Builder builder;TopoDS_Compound combined;builder.MakeCompound(combined);
                    auto combined_topology=std::make_shared<LiveCache::Topology>();
                    const auto append=[](const auto& source,auto& destination) {
                        destination.insert(destination.end(),source.begin(),source.end());
                    };
                    for (int i=0; i<solids.Extent(); ++i) {
                        const auto& solid=solids.FindKey(i+1);
                        if (!shell->removed_faces.empty() && requests[i].removed_faces.empty()) {
                            builder.Add(combined,solid);continue;
                        }
                        auto input=std::make_shared<LiveCache::Topology>();
                        const auto subset=[&](const auto& source,auto& destination) {
                            for (const auto& item:source) if (members[i].Contains(item.shape)) destination.push_back(item);
                        };
                        subset(owned_topology->faces,input->faces);subset(owned_topology->edges,input->edges);
                        subset(owned_topology->vertices,input->vertices);
                        const auto [shape,topology]=hollow_solid(solid,std::move(input),requests[i],openings[i]);
                        builder.Add(combined,shape);TopExp::MapShapes(solid,changed_members);
                        append(topology->faces,combined_topology->faces);append(topology->edges,combined_topology->edges);
                        append(topology->vertices,combined_topology->vertices);
                        append(topology->hidden_display_edges,combined_topology->hidden_display_edges);
                    }
                    // Retain unrelated standalone surfaces/wires as shapes,
                    // rather than flattening them into replacement topology.
                    const auto retain_shape=[&](const auto& self,const TopoDS_Shape& shape)->void {
                        if (all_solid_members.Contains(shape)) return;
                        if (shape.ShapeType()==TopAbs_COMPOUND || shape.ShapeType()==TopAbs_COMPSOLID) {
                            for (TopoDS_Iterator it(shape);it.More();it.Next()) self(self,it.Value());
                        } else builder.Add(combined,shape);
                    };
                    retain_shape(retain_shape,shell_input_shape);
                    const auto retain=[&](const auto& source,auto& destination) {
                        for (const auto& item:source) if (!changed_members.Contains(item.shape)) destination.push_back(item);
                    };
                    retain(owned_topology->faces,combined_topology->faces);retain(owned_topology->edges,combined_topology->edges);
                    retain(owned_topology->vertices,combined_topology->vertices);
                    for (const auto& edge:owned_topology->hidden_display_edges)
                        if (!changed_members.Contains(edge)) combined_topology->hidden_display_edges.push_back(edge);
                    require_complete_unique_shell_topology(combined,TopAbs_FACE,combined_topology->faces,"faces",&shell_input_topology->faces);
                    require_complete_unique_shell_topology(combined,TopAbs_EDGE,combined_topology->edges,"edges",&shell_input_topology->edges);
                    require_complete_unique_shell_topology(combined,TopAbs_VERTEX,combined_topology->vertices,"vertices",&shell_input_topology->vertices);
                    result_shape=combined;owned_topology=std::move(combined_topology);
                }
                auto shell_result = make_published_result(result_shape,
                    owned_topology->faces, owned_topology->edges,
                    owned_topology->vertices, true, persist_boundary_shape,
                    true, owned_topology->hidden_display_edges);
                append_reference_geometry(original_references,
                    reference_geometry_for_owners(
                        shell_result.mesh.original_references,
                        std::unordered_set<std::string>{operation.owner_id}));
                boundaries.push_back(std::move(shell_result));
                boundaries.back().source_fingerprint =
                    fingerprint(operations, boundaries.size());
                remember_live_boundary(boundaries.back().source_fingerprint,
                    result_shape, owned_topology);
                continue;
            }
            if (const auto* drill =
                    std::get_if<DrillPointRequest>(&operation.primitive)) {
                const bool has_live_face = std::ranges::any_of(
                    drill->bottom_faces, [&](const auto& requested) {
                        if (requested.semantic_key.starts_with("sweep:cap:") &&
                            std::ranges::any_of(original_references.triangle_references,
                                [&](const auto& ref) { return ref==requested && ref.surface && ref.surface->radius>0; })) return true;
                        return std::ranges::any_of(owned_topology->faces,
                            [&](const auto& owned) {
                                return owned.reference.owner_id ==
                                        requested.owner_id &&
                                    owned.reference.semantic_key ==
                                        requested.semantic_key;
                            });
                    });
                if (!has_live_face) {
                    auto boundary = make_published_result(result_shape,
                        owned_topology->faces, owned_topology->edges,
                        owned_topology->vertices, true,
                        persist_boundary_shape, false,
                        owned_topology->hidden_display_edges);
                    boundary.source_fingerprint = fingerprint(
                        operations, boundaries.size() + 1);
                    boundaries.push_back(std::move(boundary));
                    remember_live_boundary(
                        boundaries.back().source_fingerprint,
                        result_shape, owned_topology);
                    continue;
                }
            }
            const bool imported_step = std::holds_alternative<StepRequest>(operation.primitive);
            // Keep drill-point lookup outside the generic visitor to avoid an MSVC C1001.
            const auto make_drill_point_operand = [&](const DrillPointRequest& primitive)
                -> PrimitiveData {
                std::vector<std::pair<FaceReference,TopoDS_Face>> matches;
                std::vector<std::pair<FaceReference,SurfaceGeometry>> sweep_ends;
                for (const auto& requested : primitive.bottom_faces) {
                    if (requested.semantic_key.starts_with("sweep:cap:")) {
                        const auto source=std::ranges::find_if(original_references.triangle_references,
                            [&](const auto& ref){return ref==requested && ref.surface && ref.surface->radius>0;});
                        if(source!=original_references.triangle_references.end()) {
                            sweep_ends.emplace_back(*source,*source->surface);
                            continue;
                        }
                    }
                    const auto found = std::find_if(
                        owned_topology->faces.begin(),
                        owned_topology->faces.end(), [&](const auto& owned) {
                            return owned.reference.owner_id ==
                                    requested.owner_id &&
                                owned.reference.semantic_key ==
                                    requested.semantic_key;
                        });
                    if (found != owned_topology->faces.end()) {
                        matches.emplace_back(requested,TopoDS::Face(found->shape));
                    }
                }
                if (matches.empty() && sweep_ends.empty()) {
                    throw std::runtime_error(
                        "Drill-point has no remaining bottom face");
                }
                return make_drill_point_data(primitive, matches,
                    result_shape, operation.owner_id, sweep_ends);
            };
            BooleanOperation feature_copy_operation=operation.operation;
            const auto copy_operand = [&]() {
                const auto& request=*operation.feature_copy;
                const bool pattern=request.combination==BodyCombination::Pattern;
                const auto p=pattern?validated_pattern(request.pattern):PatternRequest{};
                const auto plane=pattern?MirrorPlane{}:normalized_mirror_plane(request.mirror_plane);
                const auto transformed=[&](const PrimitiveData& source,unsigned index) {
                    gp_Trsf transform;
                    if(!pattern)transform.SetMirror(gp_Ax2(
                        gp_Pnt(plane.point.x,plane.point.y,plane.point.z),
                        gp_Dir(plane.normal.x,plane.normal.y,plane.normal.z)));
                    else if(p.circular)transform.SetRotation(gp_Ax1(
                        gp_Pnt(p.origin.x,p.origin.y,p.origin.z),
                        gp_Dir(p.axis.x,p.axis.y,p.axis.z)),
                        p.angle_degrees*index*std::numbers::pi/180.0);
                    else {const auto t=pattern_translation(p,index);
                        transform.SetTranslation(gp_Vec(t.x,t.y,t.z));}
                    BRepBuilderAPI_Transform moved(source.shape,transform,true);
                    moved.Build();
                    if(!moved.IsDone())throw std::runtime_error(
                        "Feature copy transformation failed.");
                    PrimitiveData copy;copy.shape=moved.Shape();
                    const auto reference=[&](auto ref) {
                        ref.semantic_key=(pattern?"pattern:"+
                            pattern_copy_id(p,index)+":":std::string{})+
                            mirror_source_key(ref.owner_id,ref.semantic_key);
                        ref.owner_id=operation.owner_id;
                        ref.instance_path.clear();return ref;
                    };
                    for(const auto& face:source.faces) {
                        auto ref=reference(face.reference);ref.surface.reset();
                        copy.faces.push_back({TopoDS::Face(
                            moved.ModifiedShape(face.shape)),std::move(ref)});
                    }
                    for(const auto& edge:source.edges)
                        copy.edges.push_back({TopoDS::Edge(
                            moved.ModifiedShape(edge.shape)),reference(edge.reference)});
                    for(const auto& vertex:source.vertices)
                        copy.vertices.push_back({TopoDS::Vertex(
                            moved.ModifiedShape(vertex.shape)),reference(vertex.reference)});
                    return copy;
                };
                const auto combine=[&](std::vector<PrimitiveData> copies) {
                    if(copies.empty())return PrimitiveData{};
                    if(copies.size()>1&&
                            !std::getenv("ZIMA_CPP_COPY_SEQUENTIAL")&&
                            sheet_form_geometry::disjoint(copies,
                                operation.boolean_tolerance))
                        return sheet_form_geometry::compound(copies);
                    PrimitiveData result;
                    for(auto& copy:copies) {
                        if(result.shape.IsNull()) {
                            result=std::move(copy);continue;
                        }
                        BRepAlgoAPI_Fuse fuse;
                        set_boolean_inputs(fuse,result.shape,copy.shape);
                        fuse.SetToFillHistory(true);
                        fuse.SetFuzzyValue(std::max(1.0e-7,
                            operation.boolean_tolerance));
                        fuse.Build();
                        if(!fuse.IsDone()||fuse.Shape().IsNull()||
                                !BRepCheck_Analyzer(fuse.Shape()).IsValid())
                            throw std::runtime_error(
                                "OCCT feature-copy fuse failed.");
                        result.faces=propagate_topology(fuse,result.faces,copy.faces);
                        result.edges=propagate_topology(fuse,result.edges,copy.edges);
                        result.vertices=propagate_topology(fuse,result.vertices,copy.vertices);
                        result.shape=fuse.Shape();
                    }
                    return result;
                };
                std::map<BooleanOperation,std::vector<PrimitiveData>> phases;
                auto definitions=authored_copy_sources(request);
                if(request.copy_sources.empty()&&definitions.size()==1)
                    definitions.front().operation=operation.operation;
                for(const auto& definition:definitions) {
                    const auto source=copy_operands.find(definition.feature_id);
                    if(source==copy_operands.end())throw std::runtime_error(
                        "Copy source must precede the copy in the same Body.");
                    const auto add_source=[&](BooleanOperation role,
                            const PrimitiveData& source_operand) {
                        if(role!=BooleanOperation::Add&&
                                role!=BooleanOperation::Subtract)
                            throw std::runtime_error(
                                "Feature copy supports additive and subtractive sources.");
                        for(unsigned index=1;index<(pattern?p.count:2);++index)
                            phases[role].push_back(
                                transformed(source_operand,index));
                    };
                    if(source->second.copy_phases.empty())
                        add_source(definition.operation,source->second);
                    else for(const auto& [role,phase]:source->second.copy_phases)
                        add_source(role,*phase);
                }
                std::vector<std::pair<BooleanOperation,PrimitiveData>> grouped;
                for(auto& [role,copies]:phases)
                    grouped.emplace_back(role,combine(std::move(copies)));
                if(grouped.empty())throw std::runtime_error(
                    "Copy has no calculated source operands.");
                const auto primary=std::ranges::find(grouped,
                    phases.contains(BooleanOperation::Add)?
                        BooleanOperation::Add:BooleanOperation::Subtract,
                    &decltype(grouped)::value_type::first);
                PrimitiveData result=primary->second;
                feature_copy_operation=primary->first;
                for(auto& [role,phase]:grouped)
                    result.copy_phases.emplace_back(role,
                        std::make_shared<PrimitiveData>(std::move(phase)));
                return result;
            };
            PrimitiveData operand = operation.feature_copy ? copy_operand() : std::visit([&](const auto& primitive)
                -> PrimitiveData {
                using Request = std::decay_t<decltype(primitive)>;
                if constexpr (std::is_same_v<Request, DrillPointRequest>) {
                    return make_drill_point_operand(primitive);
                } else if constexpr (std::is_same_v<Request,
                                         ThreadSurfaceRequest>) {
                    throw std::logic_error(
                        "Thread surface must use the technological branch");
                } else if constexpr (std::is_same_v<Request, ExtrusionRequest>) {
                    validate_extrusion(primitive);
                    const auto exact_target=has_forward_limit(primitive)?original_target(forward_limit(primitive)):std::nullopt;
                    const auto exact_reverse=primitive.reverse_limit?original_target(limit_view(*primitive.reverse_limit)):std::nullopt;
                    double through_all_forward_span = 2'000'000.0;
                    double through_all_reverse_span = 2'000'000.0;
                    if ((primitive.extent == ExtrusionRequest::Extent::ThroughAll || primitive.through_all_reverse || primitive.sheet_cut_clearance) &&
                        !result_shape.IsNull()) {
                        const Vec3 profile_origin = std::visit([](const auto& profile) {
                            using Profile = std::decay_t<decltype(profile)>;
                            if constexpr (std::is_same_v<Profile,
                                              ExtrusionRequest::PolygonProfile>) {
                                return profile.vertices.front();
                            } else if constexpr (std::is_same_v<Profile,
                                                     ExtrusionRequest::CircleProfile> ||
                                                 std::is_same_v<Profile,
                                                     ExtrusionRequest::EllipseProfile>) {
                                return profile.center;
                            } else {
                                return std::visit([](const auto& curve) {
                                    return curve.start;
                                }, profile.curves.front());
                            }
                        }, primitive.outer_profile);
                        const double direction_length = std::hypot(std::hypot(
                            primitive.direction.x, primitive.direction.y),
                            primitive.direction.z);
                        const Vec3 unit{primitive.direction.x / direction_length,
                            primitive.direction.y / direction_length,
                            primitive.direction.z / direction_length};
                        Bnd_Box bounds;
                        BRepBndLib::Add(result_shape, bounds);
                        Standard_Real xmin{}, ymin{}, zmin{}, xmax{}, ymax{}, zmax{};
                        bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
                        double maximum_forward_projection{};
                        double maximum_reverse_projection{};
                        for (unsigned mask = 0; mask < 8; ++mask) {
                            const Vec3 corner{mask & 1 ? xmax : xmin,
                                mask & 2 ? ymax : ymin, mask & 4 ? zmax : zmin};
                            const double projection =
                                (corner.x-profile_origin.x)*unit.x +
                                (corner.y-profile_origin.y)*unit.y +
                                (corner.z-profile_origin.z)*unit.z;
                            maximum_forward_projection = std::max(
                                maximum_forward_projection, projection);
                            maximum_reverse_projection = std::max(
                                maximum_reverse_projection, -projection);
                        }
                        const double diagonal = std::hypot(std::hypot(
                            xmax-xmin, ymax-ymin), zmax-zmin);
                        const double margin = std::max(1.0, diagonal * 1.0e-4);
                        through_all_forward_span =
                            std::max(0.0, maximum_forward_projection) + margin;
                        through_all_reverse_span =
                            std::max(0.0, maximum_reverse_projection) + margin;
                    }
                    auto extrusion_data = make_extrusion_data(
                        primitive,
                        operation.owner_id, exact_target,
                        through_all_forward_span, through_all_reverse_span, std::nullopt, operation.boolean_tolerance, exact_reverse,
                        original_ordered_targets(primitive.ordered_end_limit),
                        &resolved_up_to);
                    if(primitive.sheet_cut) {
                        if(operation.operation!=BooleanOperation::Subtract||primitive.surface_result)
                            throw std::runtime_error("Sheet Cut must remove material from an existing sheet.");
                        std::optional<PrimitiveData> full_projection;
                        if(primitive.sheet_cut_clearance) {
                            auto full=primitive;full.extent=ExtrusionRequest::Extent::ThroughAll;
                            full.through_all_forward=true;full.through_all_reverse=true;
                            full.reverse_limit.reset();full.symmetric_limit=false;
                            full_projection=make_extrusion_data(full,operation.owner_id,std::nullopt,
                                through_all_forward_span,through_all_reverse_span,std::nullopt,operation.boolean_tolerance);
                        }
                        return make_sheet_cut_data(extrusion_data,owned_topology->faces,owned_topology->edges,result_shape,
                            operation.owner_id,std::max(1e-7,operation.boolean_tolerance),
                            full_projection?&*full_projection:nullptr,primitive.sheet_cut_tolerance);
                    }
                    return extrusion_data;
                } else if constexpr (std::is_same_v<Request, FeatureGroupRequest>) {
                    if (primitive.children.empty() && primitive.reference_profiles.empty() && !primitive.allow_empty) {
                        throw std::invalid_argument(
                            "Feature group requires at least one child operation");
                    }
                    std::optional<PrimitiveData> grouped;
                    if(!operation.sheet_regions.empty()&&operation.sheet_regions.size()!=primitive.children.size())
                        throw std::invalid_argument("Compound sheet material must match every authored primitive.");
                    std::size_t child_index=0;
                    for (const auto& child : primitive.children) {
                        auto child_data = make_group_child(child);
                        // General Feature profile datums retain the source
                        // boundary's explicit Start/End ancestry. Other groups
                        // and existing standalone axis identities are unchanged.
                        std::visit([&](const auto& request) {
                            if constexpr(std::is_same_v<std::decay_t<decltype(request)>,ExtrusionRequest>) {
                                HistoryOperation axis_source{operation.owner_id,request};
                                auto axes=axes_for_operation(axis_source,child_data.shape,{},true,&child_data.profile_references.points);
                                child_data.profile_references.axes.insert(child_data.profile_references.axes.end(),
                                    std::make_move_iterator(axes.begin()),std::make_move_iterator(axes.end()));
                            }
                        },child);
                        if(!operation.sheet_regions.empty()) {
                            const auto& region=operation.sheet_regions[child_index];
                            const auto assign=[&](auto& faces){for(auto& face:faces){
                                auto& ref=face.reference;ref.sheet_owner=region.owner_id;ref.sheet_thickness=region.thickness;ref.sheet_role=SheetFaceRole::ThicknessFace;
                                const auto& key=ref.semantic_key;
                                if(region.kind==SheetMaterialDefinition::Kind::Plane){if(key.starts_with("start:from:"))ref.sheet_role=SheetFaceRole::SideA;if(key.starts_with("end:from:"))ref.sheet_role=SheetFaceRole::SideB;}
                                else {if(key.find(":outer:from:")!=std::string::npos)ref.sheet_role=SheetFaceRole::SideA;if(key.find(":inner:from:")!=std::string::npos)ref.sheet_role=SheetFaceRole::SideB;}
                            }};
                            assign(child_data.faces);assign(child_data.source_caps);
                        }
                        // Original references belong to each authored child,
                        // before union removes or trims its faces, rims and
                        // points. The fused topology is only the body result.
                        group_inputs.push_back(child_data);
                        ++child_index;
                        if (!grouped) {
                            grouped = std::move(child_data);
                            continue;
                        }
                        if(context.preview_wire)continue; // Authored children are displayed separately.
                        BRepAlgoAPI_Fuse fuse;
                        set_boolean_inputs(fuse, grouped->shape, child_data.shape);
                        fuse.SetToFillHistory(true);
                        fuse.SetFuzzyValue(std::max(
                            1.0e-7, operation.boolean_tolerance));
                        fuse.Build();
                        if (!fuse.IsDone() || fuse.Shape().IsNull() ||
                            !BRepCheck_Analyzer(fuse.Shape()).IsValid()) {
                            throw std::runtime_error(
                                "OCCT feature-group fuse failed");
                        }
                        grouped->faces = propagate_topology(
                            fuse, grouped->faces, child_data.faces);
                        grouped->edges = propagate_topology(
                            fuse, grouped->edges, child_data.edges);
                        grouped->vertices = propagate_topology(
                            fuse, grouped->vertices, child_data.vertices);
                        // Authored section caps remain original references even
                        // when the union makes their shared boundary internal.
                        grouped->source_caps.insert(grouped->source_caps.end(),child_data.source_caps.begin(),child_data.source_caps.end());
                        // Generated datums belong to every authored child,
                        // independently of the Boolean result and child order.
                        append_reference_geometry(grouped->profile_references,
                            std::move(child_data.profile_references));
                        grouped->shape = fuse.Shape();

                    }
                    if(!grouped)grouped.emplace();
                    grouped->profile_references.points.insert(grouped->profile_references.points.end(),
                        primitive.reference_points.begin(),primitive.reference_points.end());
                    for(const auto& plane:primitive.reference_planes) {
                        auto& refs=grouped->profile_references;
                        const auto offset=static_cast<std::uint32_t>(refs.vertices.size());
                        refs.vertices.insert(refs.vertices.end(),plane.corners.begin(),plane.corners.end());
                        refs.triangles.insert(refs.triangles.end(),{offset,offset+1,offset+2,offset,offset+2,offset+3});
                        refs.triangle_references.insert(refs.triangle_references.end(),2,plane.reference);
                    }
                    for(const auto& profile:primitive.reference_profiles)std::visit([&](const auto& request) {
                        auto datums=stationary_profile_data(request,operation.owner_id);
                        group_inputs.insert(group_inputs.end(),std::make_move_iterator(datums.begin()),std::make_move_iterator(datums.end()));
                        append_reference_geometry(grouped->profile_references,
                            profile_centerlines::stationary(request,operation.owner_id));
                    },profile);
                    return std::move(*grouped);
                } else if constexpr (std::is_same_v<Request, RevolutionRequest>) {
                    validate_revolution(primitive);
                    return make_revolution_data(primitive,operation.owner_id,
                        primitive.end_limit?original_target(limit_view(*primitive.end_limit)):std::nullopt,operation.boolean_tolerance,nullptr,
                        original_ordered_targets(primitive.ordered_end_limit),&resolved_up_to);
                } else if constexpr (std::is_same_v<Request, Sweep3DRequest>) {
                    validate_sweep3d(primitive);
                    auto swept=make_sweep3d_data(primitive, operation.owner_id);
                    if(!primitive.make_solid)for(auto& face:swept.faces)face.reference.surface_result=true;
                    return swept;
                } else if constexpr (std::is_same_v<Request, SurfaceThickenRequest>) {
                    auto thickened=make_thickened_surface_data(primitive,operation.owner_id,result_shape,
                        owned_topology->faces,owned_topology->edges,owned_topology->vertices);
                    result_shape=std::move(thickened.remaining.shape);
                    owned_topology=std::make_shared<LiveCache::Topology>(LiveCache::Topology{
                        std::move(thickened.remaining.faces),std::move(thickened.remaining.edges),
                        std::move(thickened.remaining.vertices),owned_topology->hidden_display_edges});
                    return std::move(thickened.operand);
                } else if constexpr (std::is_same_v<Request, LoftRequest>) {
                    return make_loft_data(primitive,operation.owner_id);
                } else if constexpr (std::is_same_v<Request, BoundarySurfaceRequest>) {
                    std::vector<OwnedFace> sources;std::vector<OwnedEdge> edges;std::vector<OwnedVertex> vertices;
                    for(auto node=context.original_faces;node;node=node->previous) {
                        for(const auto& face:node->faces)if(std::ranges::any_of(primitive.constraints,[&](const auto& c){return c.support&&same_face_identity(face.reference,*c.support);}))sources.push_back(face);
                        for(const auto& edge:node->edges)if(std::ranges::any_of(primitive.constraints,[&](const auto& c){return c.edge&&edge.reference==*c.edge;}))edges.push_back(edge);
                        for(const auto& vertex:node->vertices)if(std::ranges::any_of(primitive.constraints,[&](const auto& c){return c.edge&&vertex.reference.owner_id==c.edge->owner_id;}))vertices.push_back(vertex);
                    }
                    return make_boundary_surface_data(primitive,operation.owner_id,sources,edges,vertices);
                } else if constexpr (std::is_same_v<Request, StepRequest>) {
                    return make_step_data(primitive, operation.owner_id, step_documents);
                } else {
                    throw std::logic_error("Body treatment reached primitive builder");
                }
            }, operation.primitive);
            const auto apply_subtract_operand=[&](const PrimitiveData& tool) {
                if(result_shape.IsNull())return;
                // OCCT Boolean operands must have homogeneous dimensions.
                // Trim independent sheets and the solid collection separately.
                TopoDS_Compound solid_input,surface_input,combined;
                BRep_Builder builder;builder.MakeCompound(solid_input);
                builder.MakeCompound(surface_input);builder.MakeCompound(combined);
                bool solids=false,surfaces=false;
                for(TopExp_Explorer it(result_shape,TopAbs_SOLID);it.More();it.Next()) {
                    builder.Add(solid_input,it.Current());solids=true;
                }
                for(TopExp_Explorer it(result_shape,TopAbs_FACE,TopAbs_SOLID);
                        it.More();it.Next()) {
                    builder.Add(surface_input,it.Current());surfaces=true;
                }
                LiveCache::Topology cut_topology;
                std::vector<OwnedFace> sheet_intersection_supports;
                const auto trim=[&](const TopoDS_Shape& input) {
                    BRepAlgoAPI_Cut algorithm;
                    set_boolean_inputs(algorithm,input,tool.shape);
                    algorithm.SetToFillHistory(true);
                    algorithm.SetFuzzyValue(std::max(1.0e-7,
                        operation.boolean_tolerance));
                    algorithm.Build();
                    if(!algorithm.IsDone()||algorithm.Shape().IsNull())
                        throw std::runtime_error("OCCT cut failed");
                    builder.Add(combined,algorithm.Shape());
                    TopTools_IndexedMapOfShape members;
                    TopExp::MapShapes(algorithm.Shape(),members);
                    const auto append=[&](auto& target,const auto& source) {
                        for(const auto& entry:source)
                            if(members.Contains(entry.shape))target.push_back(entry);
                    };
                    append(cut_topology.faces,propagate_topology(algorithm,
                        owned_topology->faces,tool.faces));
                    append(cut_topology.edges,propagate_topology(algorithm,
                        owned_topology->edges,tool.edges));
                    append(cut_topology.vertices,propagate_topology(algorithm,
                        owned_topology->vertices,tool.vertices));
                    if(surfaces)for(const auto& face:tool.faces) {
                        for(TopTools_ListIteratorOfListOfShape it(
                                algorithm.Generated(face.shape));it.More();it.Next())
                            if(it.Value().ShapeType()==TopAbs_EDGE&&
                                    members.Contains(it.Value()))
                                sheet_intersection_supports.push_back(
                                    {it.Value(),face.reference});
                    }
                    for(const auto& edge:propagate_display_edges(algorithm,
                            owned_topology->hidden_display_edges))
                        if(members.Contains(edge))
                            cut_topology.hidden_display_edges.push_back(edge);
                    return algorithm.Shape();
                };
                if(solids&&surfaces) {
                    trim(solid_input);trim(surface_input);result_shape=combined;
                } else result_shape=trim(result_shape);
                cut_topology.faces=complete_boolean_faces(result_shape,
                    cut_topology.faces,cut_topology.edges,
                    cut_topology.vertices,operation.owner_id,"subtract",operations);
                cut_topology.edges=complete_boolean_edges(result_shape,
                    cut_topology.faces,cut_topology.edges,operation.owner_id,
                    "subtract",sheet_intersection_supports,
                    cut_topology.vertices);
                cut_topology.vertices=complete_boolean_vertices(result_shape,
                    cut_topology.faces,cut_topology.edges,
                    cut_topology.vertices,operation.owner_id,"subtract");
                owned_topology=std::make_shared<LiveCache::Topology>(
                    std::move(cut_topology));
                for(auto& surface:technological_surfaces) {
                    if(surface.shape.IsNull())continue;
                    BRepAlgoAPI_Cut trim_surface;
                    set_boolean_inputs(trim_surface,surface.shape,tool.shape);
                    trim_surface.SetFuzzyValue(std::max(1.0e-7,
                        operation.boolean_tolerance));
                    trim_surface.Build();
                    if(!trim_surface.IsDone())throw std::runtime_error(
                        "OCCT technological thread trim failed");
                    surface.shape=trim_surface.Shape();
                }
            };
            if(operation.feature_copy&&
                    operand.copy_phases.size()>1&&
                    feature_copy_operation==BooleanOperation::Add) {
                const auto subtract=std::ranges::find(
                    operand.copy_phases,BooleanOperation::Subtract,
                    &decltype(operand.copy_phases)::value_type::first);
                if(subtract!=operand.copy_phases.end())
                    apply_subtract_operand(*subtract->second);
            }
            operation_phase("operand");
            if(const auto* profile_owner=
                    std::getenv("ZIMA_CPP_PROFILE_OPERAND_OWNER");
               profile_owner&&operation.owner_id==profile_owner) {
                TopTools_IndexedMapOfShape operand_faces,operand_solids;
                TopExp::MapShapes(operand.shape,TopAbs_FACE,operand_faces);
                TopExp::MapShapes(operand.shape,TopAbs_SOLID,operand_solids);
                GProp_GProps operand_properties;
                BRepGProp::VolumeProperties(operand.shape,
                    operand_properties);
                std::cerr<<std::setprecision(17)
                    <<"[operand profile] owner="<<operation.owner_id
                    <<" group_children="<<(feature_group
                        ?feature_group->children.size():0)
                    <<" group_inputs="<<group_inputs.size()
                    <<" faces="<<operand_faces.Extent()
                    <<" solids="<<operand_solids.Extent()
                    <<" volume="<<std::abs(operand_properties.Mass())
                    <<" input_empty="<<result_shape.IsNull()<<'\n';
            }
            bounded_fillet_source.reset();
            if(feature_copy_operation==BooleanOperation::Add&&
               std::holds_alternative<FeatureGroupRequest>(
                   operation.primitive)&&
               operation_index+1<operations.size()&&
               !operations[operation_index+1].suppressed) {
                if(const auto* next=std::get_if<FilletRequest>(
                        &operations[operation_index+1].primitive);
                   next&&next->mode==FilletRequest::Mode::Constant&&
                   next->edges.size()==1&&
                   next->edges.front().owner_id==operation.owner_id&&
                   std::ranges::any_of(operand.edges,[&](const auto& edge) {
                       return edge.reference==next->edges.front();
                   })) {
                    // Keep only the immediately consumed standalone operand
                    // and its real input. If the ordinary post-Fuse Fillet
                    // cannot construct its endpoint patch, the same authored
                    // edge can be rounded before this Add and then fused once.
                    // This is operation-local and never becomes a sidecar or
                    // a persisted alternative document representation.
                    bounded_fillet_source=BoundedFilletSource{
                        operation.owner_id,operand,result_shape,
                        owned_topology,operation.boolean_tolerance};
                }
            }
            if(operation.sheet_operation!=SheetOperation::None) {
                // Roles follow authored semantic ancestry. OCCT only locates
                // the descendants; neither face order nor curvature names them.
                const auto assign=[&](auto& faces) {
                    for(auto& face:faces) {
                        auto& reference=face.reference;const auto& key=reference.semantic_key;
                        reference.sheet_thickness=operation.sheet_thickness;
                        reference.sheet_owner=operation.owner_id;
                        reference.sheet_role=SheetFaceRole::ThicknessFace;
                        if(operation.sheet_operation==SheetOperation::Flat) {
                            if(key.starts_with("start:from:"))reference.sheet_role=SheetFaceRole::SideA;
                            if(key.starts_with("end:from:"))reference.sheet_role=SheetFaceRole::SideB;
                        } else if(operation.sheet_operation==SheetOperation::Revolution) {
                            if(key.find("thin:first:from:")!=std::string::npos)reference.sheet_role=SheetFaceRole::SideA;
                            if(key.find("thin:second:from:")!=std::string::npos)reference.sheet_role=SheetFaceRole::SideB;
                        } else {
                            if(key.find(":outer:from:")!=std::string::npos)reference.sheet_role=SheetFaceRole::SideA;
                            if(key.find(":inner:from:")!=std::string::npos)reference.sheet_role=SheetFaceRole::SideB;
                        }
                    }
                };
                assign(operand.faces);assign(operand.source_caps);
                // Original child faces need the same material-side metadata
                // as the result. Sheet attachment must work before union too.
                for(auto& child:group_inputs) {
                    assign(child.faces);assign(child.source_caps);
                }
            }
            if(context.preview_wire) {
                BodyResult preview;
                const auto append=[&](const PrimitiveData& data) {
                    if(data.reference_only)return;
                    auto sampled=make_result(data.shape,data.faces,data.edges,data.vertices,true,false,false,{},operation.mesh_deflection,false);
                    for(auto& edge:sampled.mesh.edges)if(!edge.parameter_seam)
                        preview.mesh.edges.push_back(std::move(edge));
                };
                if(group_inputs.empty())append(operand);else for(const auto& child:group_inputs)append(child);
                boundaries.push_back(std::move(preview));return boundaries;
            }
            auto source_faces=operand.faces;
            auto source_edges=operand.edges;
            auto source_vertices=operand.vertices;
            TopoDS_Shape source_shape=operand.shape;
            if(!operand.copy_phases.empty()) {
                source_faces.clear();source_edges.clear();source_vertices.clear();
                TopoDS_Compound phases;BRep_Builder phase_builder;
                phase_builder.MakeCompound(phases);
                for(const auto& [role,phase]:operand.copy_phases) {
                    static_cast<void>(role);
                    phase_builder.Add(phases,phase->shape);
                    source_faces.insert(source_faces.end(),phase->faces.begin(),phase->faces.end());
                    source_edges.insert(source_edges.end(),phase->edges.begin(),phase->edges.end());
                    source_vertices.insert(source_vertices.end(),phase->vertices.begin(),phase->vertices.end());
                }
                source_shape=phases;
            }
            if(!group_inputs.empty()) {
                source_faces.clear();
                for(const auto& child:group_inputs)
                    source_faces.insert(source_faces.end(),child.faces.begin(),child.faces.end());
            }
            if(!operand.source_caps.empty()) {
                std::erase_if(source_faces,[&](const auto& face) {
                    return std::ranges::any_of(operand.source_caps,[&](const auto& cap){return same_face_identity(face.reference,cap.reference);});
                });
                source_faces.insert(source_faces.end(),operand.source_caps.begin(),operand.source_caps.end());
            }
            if(operation.feature_copy) {
                // Copy the source's authored construction presentation, rather
                // than attempting to infer it from the Boolean result faces.
                // Visibility remains governed by the ordinary viewer switches.
                const auto& request=*operation.feature_copy;
                for(const auto& definition:authored_copy_sources(request)) {
                    if(definition.feature_id.empty())continue;
                    const auto source=context.source_solids.find(definition.feature_id);
                    const auto topology_source=copy_operands.find(definition.feature_id);
                    if(source==context.source_solids.end()||
                            topology_source==copy_operands.end())continue;
                    ViewerMesh auxiliary;
                    auxiliary.axes=source->second.mesh.axes;
                    for(const auto& edge:source->second.mesh.edges)
                        if(edge.construction)auxiliary.edges.push_back(edge);
                    const auto& topology=topology_source->second.vertices;
                    const auto point_key=[](const auto& ref){return std::tuple{
                        std::string_view(ref.owner_id),std::string_view(ref.semantic_key),std::string_view(ref.instance_path)};};
                    std::set<decltype(point_key(VertexReference{}))> vertices;
                    for(const auto& vertex:topology)vertices.insert(point_key(vertex.reference));
                    for(const auto& point:source->second.mesh.points)
                        if(!vertices.contains(point_key(point.reference)))
                            auxiliary.points.push_back(point);
                    const bool patterned=request.combination==BodyCombination::Pattern;
                    const auto pattern=patterned?validated_pattern(request.pattern):PatternRequest{};
                    for(unsigned index=1;index<(patterned?pattern.count:2);++index) {
                        auto copy=patterned?pattern_copy_mesh(auxiliary,pattern,index,operation.owner_id):
                            mirrored_viewer_mesh(auxiliary,request.mirror_plane,operation.owner_id);
                        for(auto& point:copy.points)point.display_owner_id=operation.owner_id;
                        append_original_reference_geometry(operand.profile_references,std::move(copy));
                    }
                }
            }
            if(!reference_only)retain_copy_solid(operand);
            if(group_inputs.empty())retain_originals(source_faces,
                source_edges,source_vertices,source_shape);
            else if(retain_original_edges) {
                std::vector<OwnedEdge> edges;std::vector<OwnedVertex> vertices;
                for(const auto& child:group_inputs) {
                    edges.insert(edges.end(),child.edges.begin(),child.edges.end());
                    vertices.insert(vertices.end(),child.vertices.begin(),child.vertices.end());
                }
                retain_originals(source_faces,edges,vertices,operand.shape);
            } else retain_originals(source_faces);
            if(!resolved_up_to.empty()) {
                if(!canonical_operations)canonical_operations=operations;
                resolve_ordered_up_to_operation((*canonical_operations)[operation_index],resolved_up_to);
            }
            operation_phase("source-retention");
            const std::string reference_cache_key = fingerprint(
                std::vector<HistoryOperation>{operation}, 1);
            const auto* extrusion_request =
                std::get_if<ExtrusionRequest>(&operation.primitive);
            const auto* boundary_request=std::get_if<BoundarySurfaceRequest>(&operation.primitive);
            const bool referenced_boundary=boundary_request&&std::ranges::any_of(boundary_request->constraints,[](const auto& c){return c.edge.has_value();});
            const bool cache_reference_mesh =
                !std::holds_alternative<SurfaceThickenRequest>(operation.primitive) &&
                !referenced_boundary && !operation.feature_copy && !imported_step && !std::holds_alternative<FeatureGroupRequest>(operation.primitive) &&
                (extrusion_request == nullptr || (!extrusion_request->sheet_cut &&
                 (extrusion_request->extent == ExtrusionRequest::Extent::Blind &&
                  !extrusion_request->reverse_limit && !extrusion_request->ordered_end_limit && !extrusion_request->through_all_reverse)));
            const bool standalone_import = imported_step && result_shape.IsNull();
            std::optional<BodyResult> standalone_import_result;
            ViewerMesh operand_mesh;
            const auto cached_reference = cache_reference_mesh
                ? live_cache_->reference_meshes.find(reference_cache_key)
                : live_cache_->reference_meshes.end();
            if (cached_reference != live_cache_->reference_meshes.end()) {
                operand_mesh = cached_reference->second;
                if(!eager_face_measurements)if(const auto source=live_cache_->reference_measurements.find(reference_cache_key);
                    source!=live_cache_->reference_measurements.end())measurement_sources.push_back(source->second);
            } else if (imported_step) {
                auto operand_result = make_operation_result(
                    operand.shape, operand.faces, operand.edges,
                    operand.vertices, standalone_import,
                    standalone_import && persist_boundary_shape, true);
                append_reference_geometry(original_references,
                    std::move(operand_result.mesh.original_references));
                if (standalone_import) {
                    standalone_import_result = std::move(operand_result);
                }
            } else if(!group_inputs.empty()) {
                // Adjacent authored Sweep children share canonical station
                // points. Their independent OCCT evaluations can differ by
                // roundoff. Keep one position for that authored identity, and
                // reject inconsistent geometry instead of hiding ambiguity.
                std::map<std::tuple<std::string,std::string,std::string>,Vec3> station_points;
                for(const auto& child:group_inputs) {
                    auto original=make_operation_result(child.shape,child.faces,
                        child.edges,child.vertices,true,false,false,{},false);
                    std::erase_if(original.mesh.points,[&](const auto& point) {
                        const auto& ref=point.reference;
                        if(!ref.semantic_key.starts_with("sweep:vertex:at:"))return false;
                        const auto [found,inserted]=station_points.emplace(
                            std::tuple{ref.owner_id,ref.semantic_key,ref.instance_path},point.position);
                        if(inserted)return false;
                        const auto& first=found->second;
                        if(std::hypot(first.x-point.position.x,first.y-point.position.y,first.z-point.position.z)>Precision::Confusion())
                            throw std::runtime_error("Shared Sweep station has inconsistent point geometry");
                        return true;
                    });
                    append_original_reference_geometry(original_references,std::move(original.mesh));
                }
                operand_mesh.axes=axes_for_operation(operation,operand.shape,{},false,&operand_mesh.points);
            } else {
                if(operation.feature_copy&&operand.copy_phases.size()>1) {
                    // Each Boolean role owns an authored source packet.  A
                    // mixed Pattern/Mirror must retain the subtractive source
                    // as well as the additive primary operand for later
                    // references, inspection and nested copies.
                    for(const auto& [role,phase]:operand.copy_phases) {
                        static_cast<void>(role);
                        auto source=make_operation_result(phase->shape,
                            phase->faces,phase->edges,phase->vertices,
                            true,false,false,{},false);
                        append_original_reference_geometry(
                            original_references,std::move(source.mesh));
                    }
                } else {
                    auto operand_result = make_operation_result(
                        operand.shape, operand.faces, operand.edges,
                        operand.vertices, true, false, false, {}, false);
                    const auto centerlines=centerlines_for_operation(operation);
                    operand_result.mesh.axes = axes_for_operation(operation, operand.shape,centerlines,false,&operand_result.mesh.points);
                    operand_result.mesh.edges.insert(operand_result.mesh.edges.end(),centerlines.begin(),centerlines.end());
                    const auto endpoints = sweep_endpoints_for_operation(operation);
                    operand_result.mesh.points.insert(operand_result.mesh.points.end(), endpoints.begin(), endpoints.end());
                    if(cache_reference_mesh&&!eager_face_measurements&&!operand_result.kernel_faces.empty()) {
                        BodyResult source;source.kernel_shape=operand_result.kernel_shape;source.kernel_faces=operand_result.kernel_faces;
                        source.physical_properties_current=false;source.solid_present=operand_result.has_solid();
                        source.source_fingerprint=operation.owner_id;
                        live_cache_->reference_measurements.insert_or_assign(reference_cache_key,std::move(source));
                    }
                    operand_mesh = std::move(operand_result.mesh);
                    if (cache_reference_mesh) {
                        live_cache_->reference_meshes.emplace(
                            reference_cache_key, operand_mesh);
                        live_cache_->reference_insertion_order.push_back(
                            reference_cache_key);
                        while (live_cache_->reference_insertion_order.size() >
                               kLiveShapeCacheLimit) {
                            live_cache_->reference_meshes.erase(
                                live_cache_->reference_insertion_order.front());
                            live_cache_->reference_measurements.erase(live_cache_->reference_insertion_order.front());
                            live_cache_->reference_insertion_order.pop_front();
                        }
                    }
                }
            }
            operand_mesh.axes.insert(operand_mesh.axes.end(),operand.profile_references.axes.begin(),operand.profile_references.axes.end());
            operand_mesh.edges.insert(operand_mesh.edges.end(),operand.profile_references.edges.begin(),operand.profile_references.edges.end());
            operand_mesh.points.insert(operand_mesh.points.end(),operand.profile_references.points.begin(),operand.profile_references.points.end());
            const auto datum_offset=static_cast<std::uint32_t>(operand_mesh.vertices.size());
            operand_mesh.vertices.insert(operand_mesh.vertices.end(),operand.profile_references.vertices.begin(),operand.profile_references.vertices.end());
            for(auto index:operand.profile_references.triangles)operand_mesh.triangles.push_back(datum_offset+index);
            operand_mesh.triangle_references.insert(operand_mesh.triangle_references.end(),operand.profile_references.triangle_references.begin(),operand.profile_references.triangle_references.end());
            if (!imported_step) {
                append_original_reference_geometry(
                    original_references, std::move(operand_mesh));
            }
            if (!operand.source_caps.empty()) {
                // Persist the complete original endpoint faces, even where
                // another segment consumed their visible fragments.
                std::vector<std::uint32_t> kept_triangles;
                std::vector<FaceReference> kept_references;
                for (std::size_t t=0;t<original_references.triangle_references.size();++t) {
                    const auto& ref=original_references.triangle_references[t];
                    if (ref.owner_id==operation.owner_id && ref.semantic_key.starts_with("sweep:cap:")) continue;
                    kept_references.push_back(ref);
                    for (std::size_t j=0;j<3;++j) kept_triangles.push_back(original_references.triangles[3*t+j]);
                }
                original_references.triangles=std::move(kept_triangles);
                original_references.triangle_references=std::move(kept_references);
                TopoDS_Compound caps;
                BRep_Builder cap_builder;cap_builder.MakeCompound(caps);
                for (const auto& cap:operand.source_caps) cap_builder.Add(caps,cap.shape);
                auto cap_mesh=make_operation_result(caps,operand.source_caps,{}, {},true,false,false,{},false).mesh;
                for (auto& ref:cap_mesh.triangle_references) {
                    const auto source=std::ranges::find_if(operand.source_caps,[&](const auto& cap){return cap.reference==ref;});
                    if(source!=operand.source_caps.end() && source->reference.surface)
                        ref.surface=source->reference.surface;
                }
                append_original_reference_geometry(original_references,std::move(cap_mesh));
            }
            operation_phase("reference-packets");
            if(reference_only || operand.empty_material_operand) {
                // Reference-only history boundaries leave material untouched.
            } else if (result_shape.IsNull() &&
                    feature_copy_operation == BooleanOperation::Subtract) {
                // The operand has already been calculated and validated. A cut
                // against an empty input retains its tool references and history,
                // but cannot introduce material into the Body.
            } else if (result_shape.IsNull() || operation.replaces_body) {
                if(operation.replaces_body){sheet_input={};sheet_sources={};}
                result_shape = operand.shape;
                owned_topology = std::make_shared<LiveCache::Topology>(
                    LiveCache::Topology{
                        operand.faces, operand.edges, operand.vertices, {}});
            } else if ((imported_step || surface_operand) &&
                    feature_copy_operation == BooleanOperation::Add) {
                TopoDS_Compound compound;
                BRep_Builder builder;
                builder.MakeCompound(compound);
                builder.Add(compound, result_shape);
                builder.Add(compound, operand.shape);
                result_shape = compound;
                auto combined_topology = std::make_shared<LiveCache::Topology>(
                    *owned_topology);
                combined_topology->faces.insert(combined_topology->faces.end(),
                    operand.faces.begin(), operand.faces.end());
                combined_topology->edges.insert(combined_topology->edges.end(),
                    operand.edges.begin(), operand.edges.end());
                combined_topology->vertices.insert(
                    combined_topology->vertices.end(), operand.vertices.begin(),
                    operand.vertices.end());
                owned_topology = std::move(combined_topology);
            } else if (feature_copy_operation == BooleanOperation::Add) {
                BRepAlgoAPI_Fuse algorithm;
                TopoDS_Compound solid_input,surface_input;BRep_Builder split_builder;
                split_builder.MakeCompound(solid_input);split_builder.MakeCompound(surface_input);
                bool has_surfaces=false;
                for(TopExp_Explorer it(result_shape,TopAbs_SOLID);it.More();it.Next()){split_builder.Add(solid_input,it.Current());}
                for(TopExp_Explorer it(result_shape,TopAbs_FACE,TopAbs_SOLID);it.More();it.Next()){split_builder.Add(surface_input,it.Current());has_surfaces=true;}
                set_boolean_inputs(algorithm,has_surfaces?TopoDS_Shape(solid_input):result_shape,operand.shape);
                algorithm.SetToFillHistory(true);
                algorithm.SetFuzzyValue(
                    std::max(1.0e-7, operation.boolean_tolerance));
                algorithm.Build();
                operation_phase("add-boolean-build");
                if (!algorithm.IsDone() || algorithm.Shape().IsNull()) {
                    throw std::runtime_error("OCCT fuse failed");
                }
                // The same-domain result below is the shape that is actually
                // published and it performs its own complete B-Rep check.
                // Retain an A/B baseline for representative performance and
                // exact-packet tests, but do not validate this discarded
                // intermediate by default.
                if(std::getenv("ZIMA_CPP_TEST_VALIDATE_ADD_BEFORE_UNIFY")) {
                    if(!BRepCheck_Analyzer(algorithm.Shape()).IsValid())
                        throw std::runtime_error("OCCT fuse failed");
                    operation_phase("add-boolean-validate");
                }
                auto fused_topology = std::make_shared<LiveCache::Topology>(
                    LiveCache::Topology{
                        propagate_topology(algorithm, owned_topology->faces,
                            operand.faces),
                        propagate_topology(algorithm, owned_topology->edges,
                            operand.edges),
                        propagate_topology(algorithm, owned_topology->vertices,
                            operand.vertices),
                        propagate_display_edges(
                            algorithm, owned_topology->hidden_display_edges)});
                operation_phase("add-propagate");
                result_shape = algorithm.Shape();
                if(has_surfaces) {
                    TopoDS_Compound combined;split_builder.MakeCompound(combined);
                    split_builder.Add(combined,result_shape);split_builder.Add(combined,surface_input);result_shape=combined;
                    TopTools_IndexedMapOfShape members;TopExp::MapShapes(surface_input,members);
                    for(const auto& face:owned_topology->faces)if(members.Contains(face.shape))fused_topology->faces.push_back(face);
                    for(const auto& edge:owned_topology->edges)if(members.Contains(edge.shape))fused_topology->edges.push_back(edge);
                    for(const auto& vertex:owned_topology->vertices)if(members.Contains(vertex.shape))fused_topology->vertices.push_back(vertex);
                }
                const auto* angular_group=std::get_if<FeatureGroupRequest>(&operation.primitive);
                const auto exact_angular_child=[](const auto& child) {
                    const auto* rotation=std::get_if<RevolutionRequest>(&child);
                    return rotation&&(rotation->end_limit.has_value()||rotation->ordered_end_limit.has_value());
                };
                const auto* angular_request=std::get_if<RevolutionRequest>(&operation.primitive);
                const bool exact_angular_end=(angular_request&&(angular_request->end_limit||angular_request->ordered_end_limit))||
                    (angular_group&&std::ranges::any_of(angular_group->children,exact_angular_child));
                // OCCT same-domain unification can turn joined clipped angular
                // sectors into the complementary cylinder across its seam.
                // The exact Boolean is already valid; retain its native trims.
                auto unified = exact_angular_end ? ProvenanceUnifyResult{result_shape,
                    fused_topology->faces,fused_topology->edges,fused_topology->vertices,{}} :
                    unify_preserving_face_provenance(result_shape,
                    fused_topology->faces, fused_topology->edges,
                    fused_topology->vertices,
                    std::max(1.0e-7, operation.boolean_tolerance),true);
                operation_phase("add-unify");
                // A failed/unavailable unifier deliberately returns the exact
                // Boolean input. Validate that fallback here. Successful
                // unification has already validated this exact output.
                if(!unified.validated) {
                    if(unified.shape.IsNull()||
                        !BRepCheck_Analyzer(
                            unified.shape,true,true,false).IsValid())
                        throw std::runtime_error("OCCT fuse failed");
                    operation_phase("add-result-validate");
                }
                result_shape = std::move(unified.shape);
                unified.faces = complete_boolean_faces(result_shape, unified.faces,
                    unified.edges, unified.vertices, operation.owner_id, "add", operations);
                unified.edges = complete_boolean_edges(
                    result_shape, unified.faces, unified.edges,
                    operation.owner_id, "add");
                unified.vertices = complete_boolean_vertices(result_shape, unified.faces,
                    unified.edges, unified.vertices, operation.owner_id, "add");
                owned_topology = std::make_shared<LiveCache::Topology>(
                    LiveCache::Topology{std::move(unified.faces),
                        std::move(unified.edges), std::move(unified.vertices),
                        std::move(unified.hidden_display_edges)});
                operation_phase("add-complete");
            } else {
                apply_subtract_operand(operand);
            }
            operation_phase("body-boolean");
            if (reference_only) {
                auto retained = boundaries.empty()?BodyResult{}:boundaries.back();
                // A preceding final-first packet may intentionally have no
                // display mesh. The requested output belongs to this row;
                // publish the live unchanged solid rather than that packet.
                if (!result_shape.IsNull() && (persist_boundary_shape ||
                        context.full_output_boundaries.contains(operation_index + 1) ||
                        retained_final_first_boundaries.contains(operation_index + 1)))
                    retained = make_published_result(result_shape,
                        owned_topology->faces, owned_topology->edges,
                        owned_topology->vertices, true, persist_boundary_shape,
                        false, owned_topology->hidden_display_edges);
                boundaries.push_back(
                    publish_pass_through_result(std::move(retained)));
            } else if (standalone_import_result) {
                boundaries.push_back(std::move(*standalone_import_result));
            } else if(result_shape.IsNull()) {
                boundaries.emplace_back();
            } else {
                boundaries.push_back(make_published_result(
                    result_shape, owned_topology->faces, owned_topology->edges,
                    owned_topology->vertices,
                    true, persist_boundary_shape, false,
                    owned_topology->hidden_display_edges));
            }
            append_technological_surfaces(boundaries.back(), operation.mesh_deflection);
            if(boundaries.size()>1&&!operation.replaces_body)boundaries.back().sheet_cuts=boundaries[boundaries.size()-2].sheet_cuts;
            boundaries.back().sheet_cuts.insert(boundaries.back().sheet_cuts.end(),operand.sheet_cuts.begin(),operand.sheet_cuts.end());
            if (imported_step) {
                if (standalone_import && persist_boundary_shape) {
                    persist_imported_topology(operand, boundaries.back());
                }
            }
            boundaries.back().source_fingerprint =
                fingerprint(operations, boundaries.size());
            boundaries.back().resolved_up_to_limits=std::move(resolved_up_to);
            if (!cold_reference_tail)
                remember_live_boundary(boundaries.back().source_fingerprint,
                    result_shape, owned_topology,
                    operation.sheet_material&&
                            operation.operation==BooleanOperation::Add&&
                            !operation.feature_copy&&operation.sheet_regions.empty()&&
                            !form_operands&&!std::holds_alternative<FilletRequest>(operation.primitive)&&
                            !std::holds_alternative<ChamferRequest>(operation.primitive)&&
                            !std::holds_alternative<ShellRequest>(operation.primitive)?
                        &operand:nullptr);
            operation_phase("final-packet");
        }
        if (!boundaries.empty()) {
            if(compact_dependency_boundaries&&!final_first_boundaries) {
                // Intermediate dependency packets deliberately omit complete
                // face meshes.  Recover any still-visible identities that are
                // introduced by body treatments (notably Fillet/Chamfer) from
                // the one complete final packet.  Authored source extents
                // already collected above remain authoritative; only missing
                // identities are added here.
                using ReferenceKey=std::tuple<std::string,std::string,std::string>;
                const auto key=[](const auto& reference) {
                    return ReferenceKey{reference.owner_id,reference.semantic_key,
                        reference.instance_path};
                };
                std::set<ReferenceKey> existing_faces,missing_faces;
                for(const auto& reference:original_references.triangle_references)
                    if(reference.valid())existing_faces.insert(key(reference));
                for(const auto& reference:boundaries.back().mesh.triangle_references)
                    if(reference.valid()&&!existing_faces.contains(key(reference)))
                        missing_faces.insert(key(reference));
                for(std::size_t triangle=0;
                        triangle<boundaries.back().mesh.triangle_references.size();++triangle) {
                    const auto& reference=boundaries.back().mesh.triangle_references[triangle];
                    if(!reference.valid()||!missing_faces.contains(key(reference)))continue;
                    const auto base=static_cast<std::uint32_t>(original_references.vertices.size());
                    for(std::size_t corner=0;corner<3;++corner)
                        original_references.vertices.push_back(boundaries.back().mesh.vertices.at(
                            boundaries.back().mesh.triangles.at(3*triangle+corner)));
                    original_references.triangles.insert(original_references.triangles.end(),
                        {base,base+1,base+2});
                    original_references.triangle_references.push_back(reference);
                }
                const auto append_missing=[&](auto& target,const auto& values) {
                    std::set<ReferenceKey> existing;
                    for(const auto& value:target)if(value.reference.valid())
                        existing.insert(key(value.reference));
                    for(const auto& value:values)if(value.reference.valid()&&
                            existing.insert(key(value.reference)).second)
                        target.push_back(value);
                };
                append_missing(original_references.edges,boundaries.back().mesh.edges);
                append_missing(original_references.points,boundaries.back().mesh.points);
                append_missing(original_references.axes,boundaries.back().mesh.axes);
            }
            std::set<std::string> subtract_owner_ids;
            for (const auto& operation : operations) {
                if (operation.operation == BooleanOperation::Subtract||
                        (operation.feature_copy&&std::ranges::any_of(
                            authored_copy_sources(*operation.feature_copy),
                            [](const auto& source) {
                                return source.operation==BooleanOperation::Subtract;
                            })))
                    subtract_owner_ids.insert(operation.owner_id);
            }
            // Reference geometry for a subtractive tool initially describes
            // its standalone start/end rims. After the Boolean, the visible
            // opening can be either a modified descendant of that same rim or
            // a new explicit intersection child. Persist the actual surviving
            // descendant geometry under its already-defined ZIMA identity;
            // otherwise the picker sees the correct circular opening in the
            // display packet but can only offer an obsolete rim outside the
            // retained body from the reference packet.
            for (const auto& edge :
                 boundaries.back().mesh.edges) {
                if (!subtract_owner_ids.contains(edge.reference.owner_id))
                    continue;
                const auto existing = std::find_if(
                    original_references.edges.begin(),
                    original_references.edges.end(), [&](const auto& existing) {
                        return existing.reference == edge.reference;
                    });
                if (existing == original_references.edges.end()) {
                    original_references.edges.push_back(edge);
                } else {
                    *existing = edge;
                }
            }
            // Boolean intersection endpoints have their own persisted ancestry,
            // just like the surviving edges above. Expose those exact vertices
            // to reference consumers instead of only retaining tool corners.
            for (const auto& point : boundaries.back().mesh.points) {
                if (!subtract_owner_ids.contains(point.reference.owner_id)) continue;
                const auto existing = std::ranges::find_if(original_references.points,
                    [&](const auto& value) { return value.reference == point.reference; });
                if (existing == original_references.points.end()) original_references.points.push_back(point);
                else *existing = point;
            }
            ViewerMesh persisted_surfaces;
            ThreadRimRegistry persisted_rims;
            for (const auto& surface : technological_surfaces) {
                append_technological_surface(persisted_surfaces, surface.shape,
                    surface.owner, surface.role, surface.bounds, persisted_rims, operations.back().mesh_deflection);
            }
            append_reference_geometry(original_references,
                std::move(persisted_surfaces.original_references));
            // Original geometry retains the source extents, but material
            // sidedness belongs to the calculated body. A subtractive tool
            // has the opposite material side from its standalone operand.
            using FaceIdentity = std::tuple<std::string,std::string,std::string>;
            std::map<FaceIdentity,bool> material_sides;
            for (const auto& ref : boundaries.back().mesh.triangle_references)
                if (ref.surface) material_sides[{ref.owner_id,ref.semantic_key,ref.instance_path}]
                    = ref.surface->reversed;
            std::map<FaceIdentity,std::shared_ptr<const SurfaceGeometry>> corrected_surfaces;
            for (auto& ref : original_references.triangle_references) {
                if (!ref.surface) continue;
                const FaceIdentity key{ref.owner_id,ref.semantic_key,ref.instance_path};
                const auto side=material_sides.find(key);
                if (side==material_sides.end() || side->second==ref.surface->reversed) continue;
                auto& corrected=corrected_surfaces[key];
                if (!corrected) {
                    auto value=*ref.surface;value.reversed=side->second;
                    corrected=std::make_shared<const SurfaceGeometry>(std::move(value));
                }
                ref.surface=corrected;
            }
            // A non-OCCT treatment may deliberately use one exact authored
            // source edge after later Booleans replaced it by current-body
            // fragments. Preserve only those explicitly needed source wires
            // on the treatment's rollback boundary. This keeps Properties,
            // cyan preview and validation byte-stable after save/reopen
            // without copying the complete reference packet to every row.
            for(std::size_t treatment_index=1;
                    treatment_index<std::min(operations.size(),
                        boundaries.size());++treatment_index) {
                std::span<const EdgeReference> requested;
                EdgeTreatmentCalculationMethod method=
                    EdgeTreatmentCalculationMethod::Occt;
                if(const auto* fillet=std::get_if<FilletRequest>(
                        &operations[treatment_index].primitive);
                        fillet&&fillet->mode==FilletRequest::Mode::Constant) {
                    requested=fillet->edges;
                    method=fillet->calculation_method;
                } else if(const auto* chamfer=std::get_if<ChamferRequest>(
                        &operations[treatment_index].primitive)) {
                    requested=chamfer->edges;
                    method=chamfer->calculation_method;
                }
                if(method==EdgeTreatmentCalculationMethod::Occt)continue;
                auto& rollback=boundaries[treatment_index-1].mesh;
                for(const auto& reference:requested) {
                    if(is_boolean_intersection_interval(reference)||
                       std::ranges::any_of(rollback.edges,
                            [&](const auto& edge) {
                                return edge.reference==reference;
                            })||
                       std::ranges::any_of(rollback.original_references.edges,
                            [&](const auto& edge) {
                                return edge.reference==reference;
                            }))continue;
                    if(const auto source=std::ranges::find_if(
                            original_references.edges,[&](const auto& edge) {
                                return edge.reference==reference;
                            });source!=original_references.edges.end())
                        rollback.original_references.edges.push_back(*source);
                }
            }
            boundaries.back().mesh.original_references =
                std::move(original_references);
            for(const auto& edge:boundaries.back().mesh.original_references.edges) {
                std::string source_key;
                if(construction_source_key(edge.reference.semantic_key,source_key).starts_with("centerline:from:") &&
                    std::none_of(boundaries.back().mesh.edges.begin(),boundaries.back().mesh.edges.end(),
                        [&](const auto& existing){return existing.reference==edge.reference;}))
                    boundaries.back().mesh.edges.push_back(edge);
            }
            for (const auto& point : boundaries.back().mesh.original_references.points) {
                std::string source_key;
                const auto key=construction_source_key(point.reference.semantic_key,source_key);
                if (!axis_endpoint_parent_key(key).empty() &&
                    std::none_of(boundaries.back().mesh.points.begin(), boundaries.back().mesh.points.end(),
                        [&](const auto& existing) { return existing.reference == point.reference; })) {
                    auto visible=point;
                    if(visible.display_owner_id.empty())
                        visible.display_owner_id=visible.reference.owner_id;
                    boundaries.back().mesh.points.push_back(std::move(visible));
                }
            }
            // Feature-generated axes are both persisted references and
            // ordinary visible construction geometry. Keeping them only in
            // original_references made the kernel contract pass while the
            // user saw no axis in normal View.
            for (const auto& axis :
                 boundaries.back().mesh.original_references.axes) {
                std::string source_key;
                const auto key=construction_source_key(axis.reference.semantic_key,source_key);
                if ((key == "axis:primary" || key.starts_with("axis:profile:") ||
                     key.starts_with("centerline:from:") || key.starts_with("helical:rotation-axis:")) &&
                    std::none_of(boundaries.back().mesh.axes.begin(),
                        boundaries.back().mesh.axes.end(), [&](const auto& existing) {
                            return existing.reference == axis.reference;
                        })) {
                    boundaries.back().mesh.axes.push_back(axis);
                }
            }
        }
        std::vector<SheetCutRegion> material_cuts;
        std::vector<ViewerAxis> active_bend_lines;
        std::map<std::string,ViewerAxis> rotation_sources;
        if(!boundaries.empty())for(const auto& axis:boundaries.back().mesh.original_references.axes)
            if(axis.reference.semantic_key=="axis:primary"&&std::ranges::any_of(operations,[&](const auto& op) {
                return op.owner_id==axis.reference.owner_id&&op.sheet_material&&std::holds_alternative<RevolutionRequest>(op.primitive);
            }))rotation_sources[axis.reference.owner_id]=axis;
        sheet_material::History display_material;
        std::string sheet_display_state;
        std::map<std::string,std::vector<SheetMaterialDefinition>> cut_axis_frames;
        std::map<std::string,std::size_t> cut_axis_creation;
        std::size_t sheet_display_index{};
        const bool has_sheet_display_state=std::ranges::any_of(operations,[](const auto& op){return !op.suppressed&&std::holds_alternative<SheetStateRequest>(op.primitive);});
        std::map<std::string,std::shared_ptr<const ViewerReferenceGeometry>> solid_state_views;
        std::string solid_display_state;
        std::vector<HistoryOperation> calculated_form_history;
        for (std::size_t index=0;index<boundaries.size();++index) {
            auto& boundary=boundaries[index];
            if(has_forms&&!boundary.calculation_errors.contains(operations[index].owner_id))
                calculated_form_history.push_back(operations[index]);
            if(operations[index].replaces_body&&!operations[index].suppressed&&
                !boundary.calculation_errors.contains(operations[index].owner_id)) {
                solid_display_state.clear();sheet_display_state.clear();display_material={};
                active_bend_lines.clear();cut_axis_frames.clear();cut_axis_creation.clear();
            }
            for(const auto& [owner,packet]:boundary.solid_state_reference_views)solid_state_views[owner]=packet;
            boundary.solid_state_reference_views=solid_state_views;
            if(!operations[index].suppressed&&std::holds_alternative<SolidStateRequest>(operations[index].primitive))
                solid_display_state=operations[index].owner_id;
            if(!solid_display_state.empty())solid_state_replay::publish_display(boundary,solid_display_state,boundaries.back().mesh.original_references);
            if(has_sheet_display_state&&!operations[index].suppressed)
                if(const auto* cut=std::get_if<ExtrusionRequest>(&operations[index].primitive);cut&&cut->sheet_cut) {
                    cut_axis_frames[operations[index].owner_id]=sheet_material::regions_before(operations,index).regions;
                    cut_axis_creation[operations[index].owner_id]=index;
                }
            if(!operations[index].suppressed&&std::holds_alternative<SheetStateRequest>(operations[index].primitive)) {
                display_material=sheet_material::regions_before(operations,index+1);
                sheet_display_state=operations[index].owner_id;
                sheet_display_index=index;
                active_bend_lines.clear();
                for(const auto& axis:boundary.mesh.axes)if(sheet_material::is_bend_line(axis.reference))active_bend_lines.push_back(axis);
            }
            std::erase_if(boundary.mesh.axes,[](const auto& axis){return sheet_material::is_bend_line(axis.reference);});
            boundary.mesh.axes.insert(boundary.mesh.axes.end(),active_bend_lines.begin(),active_bend_lines.end());
            for(std::size_t i=0;i<display_material.regions.size();++i) {
                const auto& region=display_material.regions[i];const auto source=rotation_sources.find(region.owner_id);
                if(source==rotation_sources.end())continue;
                std::erase_if(boundary.mesh.axes,[&](const auto& axis){return axis.reference==source->second.reference;});
                if(!region.unfolded) {
                    auto axis=source->second;
                    if(region!=display_material.sources[i]) {
                        const sheet_material::Transition transition{display_material.sources[i],region};
                        axis.point=transition.map(axis.point);axis.direction=transition.rigid_vector(axis.direction);
                    }
                    boundary.mesh.axes.push_back(std::move(axis));
                }
            }
            // Treatments with their own calculation branch must retain the
            // earlier cut definitions just like ordinary additive operations.
            std::set<std::string> present_cut_owners;
            for(const auto& cut:boundary.sheet_cuts)present_cut_owners.insert(cut.cut_owner);
            for(const auto& cut:material_cuts)
                if(!present_cut_owners.contains(cut.cut_owner))boundary.sheet_cuts.push_back(cut);
            material_cuts=boundary.sheet_cuts;
            // A circular cut's authored axis belongs to its creation state.
            // Publish a separate state-owned axis for each rigid material
            // carrier; never move the source axis or the container Origin.
            if(!sheet_display_state.empty())for(const auto& [owner,frames]:cut_axis_frames) {
                if(cut_axis_creation.at(owner)>=sheet_display_index)continue;
                std::vector<ViewerAxis> sources;
                for(const auto& axis:boundary.mesh.original_references.axes)
                    if(axis.reference.owner_id==owner&&(axis.reference.semantic_key=="axis:primary"||axis.reference.semantic_key.starts_with("axis:profile:")))
                        sources.push_back(axis);
                if(sources.empty())continue;
                std::erase_if(boundary.mesh.axes,[&](const auto& axis) {
                    return axis.reference.owner_id==owner||axis.reference.semantic_key.starts_with("sheet-state:from:"+owner+":axis:");
                });
                std::set<std::string> carriers;
                for(const auto& cut:material_cuts)if(cut.cut_owner==owner)
                    carriers.insert(cut.source.sheet_owner.empty()?cut.source.owner_id:cut.source.sheet_owner);
                for(const auto& carrier:carriers) {
                    const auto before=std::ranges::find(frames,carrier,&SheetMaterialDefinition::owner_id);
                    const auto after=std::ranges::find(display_material.regions,carrier,&SheetMaterialDefinition::owner_id);
                    if(before==frames.end()||after==display_material.regions.end())continue;
                    // Unrolling a curved carrier deforms the cut footprint;
                    // it no longer has a single straight cylinder axis.
                    if(before->kind!=SheetMaterialDefinition::Kind::Plane&&before->unfolded!=after->unfolded)continue;
                    for(auto axis:sources) {
                        const sheet_material::Transition transition{*before,*after};
                        axis.point=transition.map(axis.point);axis.direction=transition.rigid_vector(axis.direction);
                        axis.reference={sheet_display_state,"sheet-state:from:"+owner+":"+axis.reference.semantic_key+":region:"+carrier,axis.reference.instance_path};
                        if(std::ranges::none_of(boundary.mesh.original_references.axes,[&](const auto& existing){return existing.reference==axis.reference;}))
                            boundary.mesh.original_references.axes.push_back(axis);
                        boundary.mesh.axes.push_back(std::move(axis));
                    }
                }
            }
            std::erase_if(boundary.mesh.edges, [](const auto& edge) {
                return edge.reference.semantic_key == "seam" ||
                    edge.reference.semantic_key.starts_with("seam:");
            });
            if(has_forms) {
                // Symbols belong to the displayed material state, rather than
                // an operand or its persisted placement-reference packet.
                std::erase_if(boundary.mesh.edges,[](const auto& edge) {
                    return edge.overlay&&edge.reference.semantic_key.starts_with("form:symbol:");
                });
                const auto state=sheet_material::regions_before(calculated_form_history,calculated_form_history.size());
                const auto form_axis=[](const auto& axis) {
                    return sheet_material::is_form_centroid_axis(axis.reference);
                };
                std::erase_if(boundary.mesh.axes,form_axis);
                // Authored axes remain in their creation frame. Publish them
                // only in the current formed frame, including copies created
                // while unfolded; never transform the persisted source axis.
                for(auto axis:boundary.mesh.original_references.axes)if(form_axis(axis)) {
                    const auto region=std::ranges::find(state.regions,sheet_form_geometry::axis_region(axis.reference),&SheetMaterialDefinition::owner_id);
                    if(region==state.regions.end()||region->unfolded)continue;
                    const auto index=static_cast<std::size_t>(region-state.regions.begin());
                    if(state.sources[index]!=*region) {
                        const sheet_material::Transition transition{state.sources[index],*region};
                        axis.point=transition.map(axis.point);axis.direction=transition.rigid_vector(axis.direction);
                    }
                    boundary.mesh.axes.push_back(std::move(axis));
                }
                auto symbols=sheet_material::form_symbol_edges(calculated_form_history,state);
                boundary.mesh.edges.insert(boundary.mesh.edges.end(),std::make_move_iterator(symbols.begin()),std::make_move_iterator(symbols.end()));
            }
        }
        if(aggregate_property_demand==AggregatePropertyDemand::All) {
            for(auto& boundary:boundaries)ensure_body_properties(boundary);
        } else if(aggregate_property_demand==AggregatePropertyDemand::Final&&!boundaries.empty())
            ensure_body_properties(boundaries.back());
        // Pending analysis at any editable history position must also work
        // after native reopen, when this kernel's live shape cache is absent.
        for(auto& boundary:boundaries)if(!boundary.physical_properties_current&&
                boundary.kernel_shape.empty()&&
                boundary.history_reference_coverage.empty()) {
            const auto native=live_cache_->boundaries.find(boundary.source_fingerprint);
            if(native!=live_cache_->boundaries.end()&&!native->second.shape.IsNull())
                boundary.kernel_shape=serialize_kernel_shape(native->second.shape);
        }
        attach_native_history(boundaries);
        compact_history_reference_geometry(boundaries);
        return live_cache_->mark_current(std::move(boundaries),!cold_reference_tail);
    } catch (const Standard_Failure& failure) {
        throw std::runtime_error(failure.GetMessageString());
    }
}

TopoDS_Shape read_calculated_body_shape(const BodyResult& body) {
    if (body.kernel_shape.empty())
        throw std::invalid_argument("Calculated solid snapshot is missing");
    BRep_Builder builder;
    TopoDS_Shape shape;
    if (body.kernel_shape == kernel_body_compound) {
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        for (const auto& id : body.kernel_compound_bodies) {
            const auto child = body.body_outputs.find(id);
            if (child == body.body_outputs.end())
                throw std::runtime_error("Calculated solid snapshot is invalid");
            builder.Add(compound, read_calculated_body_shape(child->second));
        }
        shape = compound;
    } else {
        std::istringstream stream(body.kernel_shape);
        const bool has_topology_archive = !body.kernel_faces.empty() ||
            !body.kernel_edges.empty() || !body.kernel_vertices.empty() ||
            !body.imported_step_topology.empty();
        if (has_topology_archive) {
            BRepTools_ShapeSet archive(builder);
            archive.Read(stream);
            archive.Read(shape, stream);
        } else {
            BRepTools::Read(shape, stream, builder);
        }
    }
    if (shape.IsNull()) throw std::runtime_error("Calculated solid snapshot is invalid");
    for (const auto& placement : body.kernel_placements) {
        BRepBuilderAPI_Transform transform(shape,
            primitive_transform(placement.translation, placement.rotation_degrees), true);
        transform.Build();
        if (!transform.IsDone()) throw std::runtime_error("Body placement failed");
        shape = transform.Shape();
    }
    return shape;
}

}  // namespace zima::kernel
