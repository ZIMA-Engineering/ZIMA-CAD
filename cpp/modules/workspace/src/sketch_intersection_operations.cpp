#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/document/general_surface.hpp>
#include <zima/document/precision.hpp>
#include <zima/workspace/reference_sources.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/kernel/face_plane_intersection.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace zima::workspace {
namespace {
const document::HistoryContainer* general_surface_parent(const document::PartDocument& document,
        const sketcher::Sketch& sketch) {
    for(const auto& feature:document.history)if(feature.feature_kind==document::FeatureKind::GeneralSurface)
        for(const auto& boundary:feature.general_surface.boundaries)
            if(boundary.sketch_feature&&boundary.sketch_feature->id==sketch.owner_container_id)return &feature;
    return nullptr;
}
kernel::ViewerMesh sketch_plane_basis(const sketcher::Sketch& sketch) {
    kernel::ViewerMesh mesh;kernel::ViewerEdge edge;
    edge.points={sketch.world_point(0,0),sketch.world_point(1,0),sketch.world_point(0,1)};
    mesh.edges.push_back(std::move(edge));return mesh;
}
kernel::FacePlaneIntersectionRequest plane_request(const sketcher::Sketch& sketch,
        const kernel::FaceReference& face,const kernel::ViewerReferenceGeometry& basis,double tolerance) {
    const auto& points=basis.edges.front().points;const auto& p=points[0];
    const kernel::Vec3 x{points[1].x-p.x,points[1].y-p.y,points[1].z-p.z};
    const kernel::Vec3 y{points[2].x-p.x,points[2].y-p.y,points[2].z-p.z};
    return {face,sketch.id,p,{x.y*y.z-x.z*y.y,x.z*y.x-x.x*y.z,x.x*y.y-x.y*y.x},tolerance};
}
bool contains_face(const kernel::BodyResult& source,const kernel::FaceReference& face) {
    if(std::ranges::any_of(source.kernel_faces,[&](const auto& binding){return binding.reference==face;}))return true;
    if(source.kernel_shape==kernel::kernel_body_compound)
        for(const auto& id:source.kernel_compound_bodies)if(contains_face(source.body_outputs.at(id).get(),face))return true;
    return false;
}
std::size_t body_operation_count(const document::PartDocument& document,const document::BodyHistory& body) {
    std::size_t count{};
    for(std::size_t i=0;i<body.cursor;++i)if(body.entries[i].kind==document::PartHistoryKind::Feature)
        if(const auto* feature=document.find_container(body.entries[i].id);feature&&feature->feature_kind!=document::FeatureKind::Sketch)++count;
    return count;
}
}

kernel::ViewerReferenceGeometry part_sketch_face_intersection(const kernel::OcctKernel& kernel,
        const document::DocumentSession& session,const sketcher::Sketch& sketch,
        const kernel::FaceReference& face,const std::string& draft_body_id,SketchIntersectionFrame frame) {
    const auto& document=session.document();const auto& boundaries=session.calculated_boundaries();
    if(boundaries.empty())throw SketchOperationError("intersection_source","Intersection source geometry is unavailable.");
    auto context=document.body_history;auto boundary_id=sketch.owner_container_id.empty()?sketch.id:sketch.owner_container_id;
    const auto* surface=general_surface_parent(document,sketch);
    if(surface)boundary_id=surface->id;
    const auto* target=document.body_owner_for_object(boundary_id);
    if(target) {
        context.activate(target->scope.id);
        context.set_history_cursor(target->scope.id,context.rollback_before(boundary_id).entry_count);
    } else if(!draft_body_id.empty())context.activate(draft_body_id);

    auto basis=sketch_plane_basis(sketch);
    if(surface&&frame==SketchIntersectionFrame::Definition)basis=document::general_surface_place_definition_mesh(surface->placement,std::move(basis));
    const auto body_id=target?target->scope.id:draft_body_id;
    if(!body_id.empty())basis=document.place_body_mesh(std::move(basis),body_id);
    kernel::ViewerReferenceGeometry world_basis;world_basis.edges=std::move(basis.edges);
    std::optional<kernel::ViewerReferenceGeometry> result;
    const auto consume=[&](const kernel::BodyResult& source,const std::string& local_body) {
        if(!contains_face(source,face))return;
        if(result)throw SketchOperationError("intersection_source","Intersection face reference is ambiguous.");
        auto plane=world_basis;
        if(!local_body.empty())plane=document.construction_reference_geometry_for(local_body,std::move(plane));
        auto curves=kernel.intersect_face_plane(source,plane_request(sketch,face,plane,document::precision_value(document.document_precision,"linear_tolerance",.001)));
        if(!local_body.empty()) {
            kernel::ViewerMesh mesh;mesh.original_references=std::move(curves);
            curves=document.place_body_mesh(std::move(mesh),local_body).original_references;
        }
        result=std::move(curves);
    };
    if(context.bodies().empty()) {
        if(const auto rollback=session.rollback_boundary(boundary_id);rollback&&rollback->input_body)consume(*rollback->input_body,{});
        else consume(boundaries.back(),{});
    } else {
        const auto target_step=std::ranges::find(context.order(),body_id);
        const auto limit=target_step==context.order().end()?context.order().size():
            static_cast<std::size_t>(target_step-context.order().begin())+1;
        for(const auto& id:context.available_before(limit)) {
            if(const auto* body=context.find(id);body&&body->suppressed)continue;
            if(const auto* operation=context.find_boolean(id);operation&&operation->suppressed)continue;
            if(id==context.active_body_id()) {
                const auto* body=context.find(id);
                if(const auto local=session.calculated_body_boundary(id,body_operation_count(document,*body)))consume(*local,id);
            } else if(const auto found=boundaries.back().body_outputs.find(id);found!=boundaries.back().body_outputs.end())
                consume(found->second.get(),{});
        }
    }
    if(!result)throw SketchOperationError("intersection_source","Intersection source face is missing at its input boundary.");
    auto local=!body_id.empty()&&!target?document.construction_reference_geometry_for(body_id,std::move(*result)):
        document.sketch_reference_geometry_for(sketch,std::move(*result));
    if(surface&&frame==SketchIntersectionFrame::Definition)local=document::general_surface_local_reference_geometry(*surface,std::move(local));
    return local;
}

kernel::ViewerReferenceGeometry assembly_sketch_face_intersection(const Workspace& live,
        const kernel::OcctKernel& kernel,const std::string& document_id,const sketcher::Sketch& sketch,
        const sketcher::SketchExternalReference& reference,const kernel::FaceReference& offered,
        const document::PartDocument* candidate_part,const std::string& draft_body_id,SketchIntersectionFrame frame_space) {
    const auto top_id=reference.context_assembly_document_id.empty()?document_id:reference.context_assembly_document_id;
    const auto* state=live.open_assembly(top_id);
    const auto path=assembly::InstancePath::decode(reference.source_instance_path);
    if(!state||path.occurrence_ids.empty())throw SketchOperationError("intersection_source","Intersection source occurrence is unavailable.");
    const auto address=live.resolve_occurrence(top_id,path);
    if(!address||address->source_document_id!=reference.source_document_id)
        throw SketchOperationError("intersection_source","Intersection source document identity changed.");
    const auto& assembly_document=state->session.document();
    const auto* root=assembly_document.find_occurrence(path.occurrence_ids.front());
    if(!root||root->suppressed)
        throw SketchOperationError("intersection_source","Intersection source occurrence is unavailable.");
    bool derived=root->derived_copy.has_value();
    const auto* metadata=&root->nested_snapshot;
    for(std::size_t i=1;i<path.occurrence_ids.size();++i) {
        const auto child=std::ranges::find(*metadata,path.occurrence_ids[i],&assembly::OccurrenceSnapshot::occurrence_id);
        if(child==metadata->end()||child->manually_suppressed||child->dependency_suppressed)
            throw SketchOperationError("intersection_source","Intersection source occurrence is unavailable.");
        derived=derived||!child->derived_source_id.empty();metadata=&child->children;
    }
    const kernel::BodyResult* source=&root->calculated_source.get();
    if(reference.context_assembly_document_id.empty()) {
        const auto* cut=assembly_document.find_cut(sketch.owner_container_id);
        if(!cut&&assembly_document.find_sketch_container(sketch.owner_container_id)) {
            const auto order=assembly_document.complete_history_order();
            const auto owner=std::ranges::find(order,sketch.owner_container_id,&assembly::AssemblyDocument::HistoryEntry::id);
            if(owner==order.end())throw SketchOperationError("intersection_source","Intersection input boundary is unavailable.");
            for(auto next=std::next(owner);next!=order.end();++next)
                if(next->kind==assembly::AssemblyDocument::HistoryKind::Cut){cut=assembly_document.find_cut(next->id);break;}
        }
        if(cut) {
            const auto input=cut->input_component_bodies.find(root->occurrence_id);
            if(input==cut->input_component_bodies.end())throw SketchOperationError("intersection_source","Intersection input boundary is unavailable.");
            source=&input->second;
        }
    }

    auto frame_path=assembly::InstancePath{}.child(root->occurrence_id);
    const auto* children=&root->nested_snapshot;
    std::size_t depth=1;
    // Ordinary nested snapshots share their leaf native packets. A derived or
    // cut snapshot already owns transformed native geometry in its root frame.
    while(source->kernel_shape.empty()&&depth<path.occurrence_ids.size()) {
        const auto child=std::ranges::find(*children,path.occurrence_ids[depth],&assembly::OccurrenceSnapshot::occurrence_id);
        if(child==children->end()||child->manually_suppressed||child->dependency_suppressed)
            throw SketchOperationError("intersection_source","Intersection source occurrence is unavailable.");
        const auto body=source->body_outputs.find(child->occurrence_id);
        if(body==source->body_outputs.end())throw SketchOperationError("intersection_source","Intersection native source is unavailable.");
        source=&body->second.get();children=&child->children;frame_path=frame_path.child(child->occurrence_id);++depth;
    }
    if(source->kernel_shape.empty())throw SketchOperationError("intersection_source","Intersection native source is unavailable.");
    if(!reference.context_assembly_document_id.empty()&&!derived) {
        // A Part dependency cannot acquire Assembly-owned cuts. Require the
        // actual offered snapshot to be that Part's current native result;
        // never replace a cut fragment with the original untrimmed Part face.
        const kernel::BodyResult* expected=nullptr;
        std::vector<kernel::BodyResult> loaded;
        if(const auto* part=live.open_part(reference.source_document_id)) {
            if(!part->session.calculated_boundaries().empty())expected=&part->session.calculated_boundaries().back();
        } else if(const auto file=live.occurrence_source_file(top_id,path);file&&!file->empty()) {
            const auto document=read_family_part(&live,*file,reference.source_document_id,loaded);
            if(document.document_id==reference.source_document_id&&!loaded.empty())expected=&loaded.back();
        }
        if(!expected||depth!=path.occurrence_ids.size()||expected->source_fingerprint!=source->source_fingerprint)
            throw SketchOperationError("intersection_source","Intersection would depend on Assembly-owned modified geometry.");
    }
    auto face=offered;assembly::InstancePath relative;
    for(std::size_t i=depth;i<path.occurrence_ids.size();++i)relative=relative.child(path.occurrence_ids[i]);
    face.instance_path=relative.encoded();
    auto basis=sketch_plane_basis(sketch);
    const document::PartDocument* part=candidate_part;
    if(!part)if(const auto* state=live.open_part(document_id))part=&state->session.document();
    const document::HistoryContainer* surface=part?general_surface_parent(*part,sketch):nullptr;
    std::string body_id;
    if(part) {
        if(surface&&frame_space==SketchIntersectionFrame::Definition)basis=document::general_surface_place_definition_mesh(surface->placement,std::move(basis));
        const auto* body=part->body_owner_for_object(surface?surface->id:sketch.owner_container_id);
        body_id=body?body->scope.id:draft_body_id;
        if(!body_id.empty())basis=part->place_body_mesh(std::move(basis),body_id);
    }
    const auto dependent=assembly::InstancePath::decode(reference.context_instance_path);
    const auto to_scene=[&](auto point){return part?live.occurrence_point_to_scene(top_id,dependent,point):point;};
    for(auto& point:basis.edges.front().points)point=live.occurrence_point_from_scene(top_id,frame_path,to_scene(point));
    kernel::ViewerReferenceGeometry plane;plane.edges=std::move(basis.edges);
    const auto tolerance=document::precision_value(part?part->document_precision:assembly_document.document_precision,"linear_tolerance",.001);
    auto result=kernel.intersect_face_plane(*source,plane_request(sketch,face,plane,tolerance));
    ReferenceFrame frame;
    frame.point=[&](auto p){p=live.occurrence_point_to_scene(top_id,frame_path,p);return part?live.occurrence_point_from_scene(top_id,dependent,p):p;};
    frame.direction=[&](auto p){p=live.occurrence_direction_to_scene(top_id,frame_path,p);return part?live.occurrence_direction_from_scene(top_id,dependent,p):p;};
    kernel::ViewerReferenceGeometry local;append_original_reference_geometry(local,result,frame,[](auto,const auto&,const auto&,const auto&){return true;});
    if(part) {
        local=!body_id.empty()&&!part->body_owner_for_object(sketch.owner_container_id)?
            part->construction_reference_geometry_for(body_id,std::move(local)):part->sketch_reference_geometry_for(sketch,std::move(local));
        if(surface&&frame_space==SketchIntersectionFrame::Definition)local=document::general_surface_local_reference_geometry(*surface,std::move(local));
    }
    return local;
}

// Only native isolated contacts become independent Point references. Open
// branch endpoints belong to their branch and reuse its EdgeStart/EdgeEnd path.
std::vector<sketcher::SketchExternalReference> intersection_branch_references(
        const sketcher::Sketch& sketch,const sketcher::SketchExternalReference& source,
        const kernel::FaceReference& offered,const kernel::ViewerReferenceGeometry& geometry) {
    using Kind=sketcher::ExternalReferenceKind;
    std::vector<sketcher::SketchExternalReference> branches;
    std::set<std::tuple<std::string,std::string,std::string>> endpoints;
    const auto append=[&](Kind kind,const auto& native) {
        auto reference=source;reference.id=sketcher::Sketch::create_external_reference(kind).id;
        reference.kind=kind;reference.intersection=sketcher::SketchIntersectionBinding{
            offered.owner_id,offered.semantic_key,native.semantic_key};
        auto projection=reference;projection.source_owner_id=native.owner_id;
        projection.source_semantic_key=native.semantic_key;projection.source_instance_path=native.instance_path;
        projection.cached_points.clear();projection.cached_paths.clear();projection.exact_spline.reset();
        populate_external_reference_cache(sketch,projection,geometry);
        reference.cached_points=std::move(projection.cached_points);reference.exact_spline=std::move(projection.exact_spline);
        reference.cached_paths.clear();reference.infinite=false;reference.broken=false;
        branches.push_back(std::move(reference));
    };
    for(const auto& edge:geometry.edges) {
        append(Kind::Edge,edge.reference);
        for(const auto& point:edge.edge_treatment_endpoint_references)
            endpoints.emplace(point.owner_id,point.semantic_key,point.instance_path);
    }
    for(const auto& point:geometry.points)
        if(!endpoints.contains({point.reference.owner_id,point.reference.semantic_key,point.reference.instance_path}))
            append(Kind::Point,point.reference);
    return branches;
}

std::vector<sketcher::SketchExternalReference> prepare_sketch_intersection_branches(
        const Workspace& live,const kernel::OcctKernel& kernel,const std::string& document,
        const sketcher::Sketch& sketch,const kernel::FaceReference& offered,const kernel::FaceReference& original,
        const std::string& draft_body_id,bool draft_section) {
    auto source=prepare_sketch_intersection_source(live,document,sketch,original,draft_body_id,draft_section);
    if(offered.instance_path!=original.instance_path)
        throw SketchOperationError("intersection_source","Intersection source occurrence changed.");
    auto exact=offered;exact.instance_path=source.source_instance_path;
    const auto* part=live.open_part(document);
    if(part&&source.source_instance_path.empty())
        return intersection_branch_references(sketch,source,exact,
            part_sketch_face_intersection(kernel,part->session,sketch,exact,draft_body_id,SketchIntersectionFrame::Body));
    return intersection_branch_references(sketch,source,exact,
        assembly_sketch_face_intersection(live,kernel,document,sketch,source,exact,nullptr,draft_body_id,SketchIntersectionFrame::Body));
}

namespace {
template<class Predicate,class Query>
std::vector<sketcher::SketchExternalReference> calculate_intersections(
        const sketcher::Sketch& sketch,const Predicate& accepts,const Query& query) {
    using Kind=sketcher::ExternalReferenceKind;
    using Key=std::tuple<std::string,std::string,std::string,std::string>;
    std::map<Key,std::vector<sketcher::SketchExternalReference>> calculated;
    std::vector<sketcher::SketchExternalReference> result;
    for(const auto& reference:sketch.external_references) {
        if(!reference.intersection||!accepts(reference))continue;
        const auto& binding=*reference.intersection;
        const Key key{reference.source_document_id,reference.source_instance_path,binding.face_owner_id,binding.face_semantic_key};
        auto [group,inserted]=calculated.try_emplace(key);
        if(inserted) {
            try {
                const kernel::FaceReference face{binding.face_owner_id,binding.face_semantic_key,reference.source_instance_path};
                group->second=intersection_branch_references(sketch,reference,face,query(reference,face));
            } catch(const std::exception&) {
                // A missing, split or ambiguous branch freezes its own previous
                // cache. Never substitute another branch or projected outline.
            }
        }
        auto updated=reference;updated.broken=true;
        const auto found=std::ranges::find_if(group->second,[&](const auto& branch) {
            return branch.intersection==reference.intersection &&
                (branch.kind==reference.kind || (branch.kind==Kind::Edge&&sketcher::is_external_endpoint_kind(reference.kind)));
        });
        if(found!=group->second.end()) {
            updated.broken=false;updated.cached_paths.clear();updated.cached_points=found->cached_points;
            updated.exact_spline=found->exact_spline;
            if(sketcher::is_external_endpoint_kind(reference.kind)) {
                updated.cached_points={reference.kind==Kind::EdgeStart?found->cached_points.front():found->cached_points.back()};
                updated.exact_spline.reset();
            }
        }
        result.push_back(std::move(updated));
    }
    return result;
}
}
std::vector<sketcher::SketchExternalReference> calculate_part_sketch_intersections(
        const kernel::OcctKernel& kernel,const document::DocumentSession& session,const sketcher::Sketch& sketch) {
    return calculate_intersections(sketch,[&](const auto& reference) {
        return reference.source_document_id==session.document().document_id && reference.source_instance_path.empty() && reference.context_assembly_document_id.empty();
    },[&](const auto&,const auto& face){return part_sketch_face_intersection(kernel,session,sketch,face);});
}
std::vector<sketcher::SketchExternalReference> calculate_assembly_sketch_intersections(
        const kernel::OcctKernel& kernel,const assembly::AssemblyDocument& document,const sketcher::Sketch& sketch) {
    Workspace live;live.add_assembly(document);
    return calculate_intersections(sketch,[](const auto& reference){return reference.context_assembly_document_id.empty()&&!reference.source_instance_path.empty();},
        [&](const auto& reference,const auto& face){return assembly_sketch_face_intersection(live,kernel,document.document_id,sketch,reference,face);});
}
bool refresh_context_sketch_intersections(const Workspace& live,const kernel::OcctKernel& kernel,document::PartDocument& document) {
    return update_document_sketches(document,[&](auto& sketch) {
        const auto values=calculate_intersections(sketch,[](const auto& reference){return !reference.context_assembly_document_id.empty();},
            [&](const auto& reference,const auto& face){return assembly_sketch_face_intersection(live,kernel,document.document_id,sketch,reference,face,&document);});
        return !values.empty()&&sketch.refresh_external_references({}, {},false,nullptr,false,&values);
    });
}
}
