#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/workspace/reference_sources.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/document/general_surface.hpp>
#include <zima/document/placement_orientation.hpp>
#include <zima/document/precision.hpp>
#include <zima/kernel/dimension_layout.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <algorithm>
#include <map>
#include <tuple>

namespace zima::workspace {
namespace {
using Reference=sketcher::SketchExternalReference;
struct SourceCurve {kernel::BSplineGeometry geometry;bool closed{};std::string parameterization;};
constexpr auto error="Curve-plane intersection calculation failed.";
const document::HistoryContainer* surface_parent(const document::PartDocument& doc,const sketcher::Sketch& sketch) {
    for(const auto& feature:doc.history)if(feature.feature_kind==document::FeatureKind::GeneralSurface)
        for(const auto& boundary:feature.general_surface.boundaries)
            if(boundary.sketch_feature&&boundary.sketch_feature->id==sketch.owner_container_id)return &feature;
    return nullptr;
}
std::string identities(const std::vector<std::string>& ids) {
    std::string result;for(const auto& id:ids)result+=std::to_string(id.size())+":"+id;return result;
}
SourceCurve from_edge(const kernel::ViewerEdge& edge) {
    if(!edge.exact_spline)throw SketchOperationError("intersection_source",error);
    SourceCurve result{*edge.exact_spline,false,"native-edge:"};
    for(const auto& endpoint:edge.edge_treatment_endpoint_references)
        result.parameterization+=identities({endpoint.owner_id,endpoint.semantic_key});
    if(edge.edge_treatment_endpoint_references.size()==2)
        result.closed=edge.edge_treatment_endpoint_references.front()==edge.edge_treatment_endpoint_references.back();
    return result;
}
SourceCurve part_source(const document::DocumentSession& session,const Reference& source) {
    const auto& doc=session.document();
    std::optional<SourceCurve> native;
    visit_document_sketches(doc,[&](const auto& sketch) {
        if(sketch.id!=source.source_owner_id)return true;
        if(sketch.suppressed)throw SketchOperationError("intersection_source",error);
        const auto colon=source.source_semantic_key.find(':');
        const auto id=colon==std::string::npos?std::string{}:source.source_semantic_key.substr(colon+1);
        SourceCurve result;result.parameterization=source.source_semantic_key;
        bool valid=false;
        const auto find=[&](const auto& collection,const std::string& prefix,const auto& append) {
            if(!source.source_semantic_key.starts_with(prefix))return;
            const auto item=std::ranges::find_if(collection,[&](const auto& value){return value.id==id;});
            if(item!=collection.end()){valid=true;append(*item);}
        };
        find(sketch.bsplines,"bspline:",[&](const auto& curve) {
            result.closed=curve.closed;result.parameterization+=identities(curve.control_point_ids);
        });
        find(sketch.segments,"segment:",[&](const auto& line){result.parameterization+=identities({line.first_point_id,line.second_point_id});});
        find(sketch.circles,"circle:",[&](const auto& curve){result.closed=true;result.parameterization+=identities({curve.center_point_id});});
        find(sketch.arcs,"arc:",[&](const auto& curve){result.parameterization+=identities({curve.center_point_id,curve.start_point_id,curve.end_point_id});});
        find(sketch.ellipses,"ellipse:",[&](const auto& curve){result.closed=true;result.parameterization+=identities({curve.center_point_id,curve.major_point_id,curve.minor_point_id});});
        find(sketch.elliptical_arcs,"elliptical_arc:",[&](const auto& curve){result.parameterization+=identities({curve.center_point_id,curve.start_point_id,curve.end_point_id,curve.major_point_id,curve.minor_point_id});});
        find(sketch.corner_radii,"corner_radius:",[&](const auto& corner){
            if(corner.suppressed||corner.radius<=1e-9)throw SketchOperationError("intersection_source",error);
            result.parameterization+=identities({corner.vertex_id,corner.first_segment_id,corner.second_segment_id});
        });
        if(!valid)throw SketchOperationError("intersection_source",error);
        if(const auto* offset=sketch.find_offset(id);offset&&offset->broken)throw SketchOperationError("intersection_source",error);
        for(const auto& trim:sketch.curve_trims)if(trim.id==id&&trim.broken)throw SketchOperationError("intersection_source",error);
        const bool rounded=std::ranges::any_of(sketch.corner_radii,[&](const auto& corner){return !corner.suppressed&&corner.radius>1e-9&&
            (corner.id==id||corner.first_segment_id==id||corner.second_segment_id==id);});
        // Query the stored bounded curve, not an offset/trim's complete hidden
        // support. Rounded legs/arcs use the same exact native profile as View.
        if(rounded)result.geometry=sketcher::sketch_curve_geometry(sketch.evaluated_profile_sketch(),id);
        else result.geometry=sketcher::sketch_curve_geometry(sketch,id);
        for(auto& pole:result.geometry.poles)pole=sketch.world_point(pole.x,pole.y);
        kernel::ViewerMesh packet;kernel::ViewerEdge edge;edge.exact_spline=std::move(result.geometry);packet.edges.push_back(std::move(edge));
        const auto* body=doc.body_owner_for_object(sketch.owner_container_id);
        if(!body)if(const auto* parent=surface_parent(doc,sketch))body=doc.body_owner_for_object(parent->id);
        if(body)packet=doc.place_body_mesh(std::move(packet),body->scope.id);
        result.geometry=std::move(*packet.edges.front().exact_spline);native=std::move(result);return false;
    });
    if(native)return std::move(*native);
    for(const auto& object:doc.constructions)if(object.entity_id==source.source_owner_id&&object.kind==document::ConstructionKind::Curve3D) {
        if(object.suppressed||!object.reference_valid)throw SketchOperationError("intersection_source",error);
        const auto route=document::curve3d_route(object);
        const auto segment=std::ranges::find(route.segments,source.source_semantic_key,&kernel::Sweep3DRequest::PathSegment::source_id);
        if(segment==route.segments.end())throw SketchOperationError("intersection_source",error);
        auto curve=kernel::exact_path_segment_geometry(*segment);
        const auto frame=kernel::annotation_frame(object.origin,object.rotation);
        for(auto& pole:curve.poles)pole=frame.world(pole);
        kernel::ViewerMesh mesh;kernel::ViewerEdge edge;edge.exact_spline=std::move(curve);mesh.edges.push_back(std::move(edge));
        if(const auto* body=doc.body_owner_for_object(object.id))mesh=doc.place_body_mesh(std::move(mesh),body->scope.id);
        std::vector<std::string> ids;for(const auto& point:object.curve_points)ids.push_back(point.id);
        return {*mesh.edges.front().exact_spline,false,"authored-3d:"+identities(ids)};
    }
    if(session.calculated_boundaries().empty())throw SketchOperationError("intersection_source",error);
    const auto& edges=session.calculated_boundaries().back().mesh.original_references.edges;
    const kernel::ViewerEdge* selected{};
    for(const auto& edge:edges)if(edge.reference.owner_id==source.source_owner_id&&edge.reference.semantic_key==source.source_semantic_key&&edge.reference.instance_path.empty()) {
        if(selected)throw SketchOperationError("intersection_source","Persisted source reference is ambiguous");selected=&edge;
    }
    if(!selected)throw SketchOperationError("intersection_source",error);return from_edge(*selected);
}
void into_sketch_frame(SourceCurve& source,const document::PartDocument& doc,const sketcher::Sketch& sketch,
        const std::string& draft,SketchIntersectionFrame frame) {
    kernel::ViewerReferenceGeometry geometry;kernel::ViewerEdge edge;edge.exact_spline=std::move(source.geometry);geometry.edges.push_back(std::move(edge));
    geometry=!draft.empty()&&!doc.body_owner_for_object(sketch.owner_container_id)?doc.construction_reference_geometry_for(draft,std::move(geometry)):
        doc.sketch_reference_geometry_for(sketch,std::move(geometry));
    if(frame==SketchIntersectionFrame::Definition)if(const auto* surface=surface_parent(doc,sketch))
        geometry=document::general_surface_local_reference_geometry(*surface,std::move(geometry));
    source.geometry=std::move(*geometry.edges.front().exact_spline);
}
bool derived_source(const assembly::AssemblyDocument& top,const assembly::InstancePath& path) {
    if(path.occurrence_ids.empty())throw SketchOperationError("intersection_source",error);
    const auto root=std::ranges::find(top.components,path.occurrence_ids.front(),&assembly::PartOccurrence::occurrence_id);
    if(root==top.components.end())throw SketchOperationError("intersection_source",error);
    bool derived=root->derived_copy.has_value();const auto* children=&root->nested_snapshot;
    for(std::size_t i=1;i<path.occurrence_ids.size();++i) {
        const auto node=std::ranges::find_if(*children,[&](const auto& item){return item.occurrence_id==path.occurrence_ids[i];});
        if(node==children->end())throw SketchOperationError("intersection_source",error);
        derived=derived||!node->derived_source_id.empty();children=&node->children;
    }
    return derived;
}
SourceCurve context_source(const Workspace& live,const std::string& document,const sketcher::Sketch& sketch,
        const Reference& reference,const document::PartDocument* candidate,const std::string& draft,SketchIntersectionFrame frame,
        const Workspace* authoritative_sources=nullptr) {
    const auto top=reference.context_assembly_document_id.empty()?document:reference.context_assembly_document_id;
    const auto path=assembly::InstancePath::decode(reference.source_instance_path);
    const auto address=live.resolve_occurrence(top,path);
    if(!address||address->source_kind!=assembly::ComponentSourceKind::Part||address->source_document_id!=reference.source_document_id)
        throw SketchOperationError("intersection_source","Intersection source document identity changed.");
    const auto dependent=assembly::InstancePath::decode(reference.context_instance_path);
    const auto& key=reference.source_semantic_key;
    const bool authored=key.starts_with("segment:")||key.starts_with("circle:")||key.starts_with("arc:")||
        key.starts_with("ellipse:")||key.starts_with("elliptical_arc:")||key.starts_with("bspline:")||key.starts_with("corner_radius:")||key.starts_with("curve:");
    const auto* target=candidate;if(!target)if(const auto* state=live.open_part(document))target=&state->session.document();
    const auto* top_state=live.open_assembly(top);if(!top_state)throw SketchOperationError("intersection_source",error);
    const bool derived=derived_source(top_state->session.document(),path);
    std::optional<SourceCurve> native;
    if(authored) {
        // Read the selected native source once. Its ordered authored points
        // certify parameterization; a display polyline cannot supply that data.
        const auto& sources=authoritative_sources?*authoritative_sources:live;
        if(const auto* state=sources.open_part(reference.source_document_id))native=part_source(state->session,reference);
        else {
            const auto file=live.occurrence_source_file(top,path);if(!file)throw SketchOperationError("intersection_source",error);
            std::vector<kernel::BodyResult> calculated;auto loaded=read_family_part(&sources,*file,reference.source_document_id,calculated);
            if(loaded.document_id!=reference.source_document_id)throw SketchOperationError("intersection_source","Intersection source document identity changed.");
            native=part_source(document::DocumentSession(std::move(loaded),std::move(calculated)),reference);
        }
        if(!derived) {
            for(auto& pole:native->geometry.poles) {
                pole=live.occurrence_point_to_scene(top,path,pole);
                if(!reference.context_instance_path.empty())pole=live.occurrence_point_from_scene(top,dependent,pole);
            }
            if(target)into_sketch_frame(*native,*target,sketch,draft,frame);
            return std::move(*native);
        }
    }
    const auto matches=[&](OriginalReferenceKind kind,const auto& owner,const auto& key,const auto& path_key){return kind==OriginalReferenceKind::Edge&&
        owner==reference.source_owner_id&&key==reference.source_semantic_key&&path_key==reference.source_instance_path;};
    kernel::ViewerReferenceGeometry packet;
    if(reference.context_instance_path.empty())visit_original_references(live,top,[&](const auto& geometry,const ReferenceFrame& frame) {
        append_original_reference_geometry(packet,geometry,frame,matches);return true;
    });
    else packet=context_original_reference_geometry(live,top,dependent,reference.source_document_id,matches);
    if(packet.edges.empty())throw SketchOperationError("intersection_source",error);
    if(packet.edges.size()>1)throw SketchOperationError("intersection_source","Persisted source reference is ambiguous");
    const auto& selected=packet.edges.front();
    auto curve=from_edge(selected);
    if(native){native->geometry=std::move(curve.geometry);curve=std::move(*native);}
    if(target)into_sketch_frame(curve,*target,sketch,draft,frame);
    return curve;
}
double query_tolerance(const Workspace& live,const std::string& id,const document::PartDocument* candidate=nullptr) {
    if(candidate)return document::precision_value(candidate->document_precision,"linear_tolerance",.001);
    if(const auto* part=live.open_part(id))return document::precision_value(part->session.document().document_precision,"linear_tolerance",.001);
    if(const auto* assembly=live.open_assembly(id))return document::precision_value(assembly->session.document().document_precision,"linear_tolerance",.001);
    throw SketchOperationError("intersection_source",error);
}
kernel::CurvePlaneIntersectionResult query(const SourceCurve& source,const sketcher::Sketch& sketch,double tolerance) {
    return kernel::intersect_curve_plane({source.geometry,sketch.world_point(0,0),sketch.normal(),tolerance,source.closed});
}
std::vector<Reference> branches(const SourceCurve& source,const sketcher::Sketch& sketch,const Reference& definition,double tolerance) {
    const auto result=query(source,sketch,tolerance);std::vector<Reference> values;
    if(result.points.empty()&&!result.coincident_intervals.empty())throw SketchOperationError("non_unique_intersection",
        "The source curve lies in the Sketch plane over an interval; there is no unique intersection point.");
    const double period=result.last_parameter-result.first_parameter;
    for(std::size_t i=0;i<result.points.size();++i) {
        const auto& point=result.points[i];const double previous=i?result.points[i-1].parameter:
            source.closed?result.points.back().parameter-period:result.first_parameter;
        const double next=i+1<result.points.size()?result.points[i+1].parameter:
            source.closed?result.points.front().parameter+period:result.last_parameter;
        auto reference=definition;reference.id=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Point).id;
        reference.kind=sketcher::ExternalReferenceKind::Point;reference.intersection.reset();
        reference.curve_intersection=sketcher::SketchCurveIntersectionBinding{definition.source_owner_id,definition.source_semantic_key,
            source.geometry.degree,source.geometry.knots,(previous+point.parameter)*.5,(point.parameter+next)*.5,
            point.kind,source.closed,source.parameterization,{}};
        auto& binding=*reference.curve_intersection;
        binding.orientation=point.orientation;binding.root_count=result.points.size();
        for(const auto& interval:result.coincident_intervals)
            for(double shift:source.closed?std::vector<double>{-period,0,period}:std::vector<double>{0}) {
                const auto first=interval.first+shift,last=interval.second+shift;
                if(last<point.parameter&&last>previous)binding.parameter_first=std::max(binding.parameter_first,(last+point.parameter)*.5);
                if(first>point.parameter&&first<next)binding.parameter_last=std::min(binding.parameter_last,(first+point.parameter)*.5);
            }
        // Open endpoints retain a nonempty one-sided native domain.
        if(binding.parameter_first==binding.parameter_last) {
            binding.parameter_first=result.first_parameter;binding.parameter_last=result.last_parameter;
        }
        binding.branch_key=sketcher::curve_intersection_branch_key(binding);
        reference.cached_points={sketch.local_point(point.position)};reference.cached_paths.clear();reference.exact_spline.reset();
        reference.infinite=false;reference.broken=false;values.push_back(std::move(reference));
    }
    return values;
}
Reference refresh(const SourceCurve& source,const kernel::CurvePlaneIntersectionResult& result,
        const sketcher::Sketch& sketch,const Reference& before) {
    auto updated=before;updated.broken=true;const auto& binding=*before.curve_intersection;
    if(binding.degree!=source.geometry.degree||binding.knots!=source.geometry.knots||binding.closed!=source.closed||
            binding.parameterization_key!=source.parameterization)return updated;
    const double period=result.last_parameter-result.first_parameter;
    const auto in_domain=[&](double parameter) {
        if(source.closed&&parameter<binding.parameter_first)parameter+=period;
        if(source.closed&&parameter>binding.parameter_last)parameter-=period;
        return parameter>=binding.parameter_first&&parameter<=binding.parameter_last;
    };
    for(const auto& interval:result.coincident_intervals)for(double shift:source.closed?std::vector<double>{-period,0,period}:std::vector<double>{0})
        if(interval.first+shift<=binding.parameter_last&&interval.second+shift>=binding.parameter_first)return updated;
    const kernel::CurvePlaneContact* selected{};
    for(const auto& point:result.points)if(in_domain(point.parameter)) {if(selected)return updated;selected=&point;}
    if(!selected||selected->kind!=binding.contact||selected->orientation!=binding.orientation||result.points.size()!=binding.root_count)return updated;
    updated.cached_points={sketch.local_point(selected->position)};updated.cached_paths.clear();updated.exact_spline.reset();updated.broken=false;return updated;
}
template<class Accept,class Source> std::vector<Reference> recalculate(const sketcher::Sketch& sketch,double tolerance,
        const Accept& accepts,const Source& source) {
    using Key=std::tuple<std::string,std::string,std::string,std::string>;
    std::map<Key,std::optional<std::pair<SourceCurve,kernel::CurvePlaneIntersectionResult>>> queries;
    std::vector<Reference> result;
    for(const auto& reference:sketch.external_references)if(reference.curve_intersection&&accepts(reference)) {
        const Key key{reference.source_document_id,reference.source_instance_path,reference.source_owner_id,reference.source_semantic_key};
        auto [entry,inserted]=queries.try_emplace(key);
        if(inserted)try {auto curve=source(reference);auto roots=query(curve,sketch,tolerance);entry->second.emplace(std::move(curve),std::move(roots));}catch(const std::exception&){}
        auto updated=reference;updated.broken=true;
        if(entry->second)updated=refresh(entry->second->first,entry->second->second,sketch,reference);
        result.push_back(std::move(updated));
    }
    return result;
}
}
std::vector<Reference> prepare_sketch_curve_intersection_branches(const Workspace& live,const std::string& document,
        const sketcher::Sketch& sketch,const kernel::EdgeReference& offered,const std::string& draft,bool section) {
    const auto source=prepare_sketch_curve_intersection_source(live,document,sketch,offered,draft,section);
    if(source.source_instance_path.empty()) {
        const auto* state=live.open_part(document);if(!state)throw SketchOperationError("intersection_source",error);
        auto curve=part_source(state->session,source);into_sketch_frame(curve,state->session.document(),sketch,draft,SketchIntersectionFrame::Body);
        return branches(curve,sketch,source,document::precision_value(state->session.document().document_precision,"linear_tolerance",.001));
    }
    return branches(context_source(live,document,sketch,source,nullptr,draft,SketchIntersectionFrame::Body),sketch,source,query_tolerance(live,document));
}
std::vector<Reference> calculate_part_sketch_curve_intersections(const document::DocumentSession& session,const sketcher::Sketch& sketch) {
    const auto& doc=session.document();const auto allowed=sketch_external_reference_source_owners(doc,sketch.id);
    return recalculate(sketch,document::precision_value(doc.document_precision,"linear_tolerance",.001),[&](const auto& r){
        return r.source_document_id==doc.document_id&&r.source_instance_path.empty()&&r.context_assembly_document_id.empty();
    },[&](const auto& r){
        if(!allowed.contains(r.source_owner_id))throw SketchOperationError("intersection_source",error);
        auto curve=part_source(session,r);into_sketch_frame(curve,doc,sketch,{},SketchIntersectionFrame::Definition);return curve;
    });
}
std::vector<Reference> calculate_context_sketch_curve_intersections(const Workspace& live,const std::string& document,
        const sketcher::Sketch& sketch,const document::PartDocument* candidate,const Workspace* authoritative_sources) {
    return recalculate(sketch,query_tolerance(live,document,candidate),[](const auto& r){return !r.source_instance_path.empty();},
        [&](const auto& r){return context_source(live,document,sketch,r,candidate,{},SketchIntersectionFrame::Definition,authoritative_sources);});
}
} // namespace zima::workspace
