#include <zima/workspace/history_deletion.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/workspace/solid_state_operations.hpp>
#include <zima/workspace/sheet_state_operations.hpp>
#include <nlohmann/json.hpp>
namespace zima::workspace {
namespace {
HistoryDependencyCollector dependencies(const document::PartDocument& doc,bool include_bodies=false) {
    auto graph=part_history_dependency_graph(doc);
    if(include_bodies) {
        for(const auto& body:doc.body_history.bodies())graph.alias(body.scope.id,body.scope.id);
        for(const auto& body:doc.body_history.bodies()) {
            for(const auto& reference:body.scope.placement.references)
                graph.reference(body.scope.id,reference);
            for(const auto& dependency:body.dependencies)
                graph.use(body.scope.id,dependency);
        }
    }
    for(const auto& f:doc.history) {
        graph.use(f.id,f.holes.sketch_id);graph.use(f.id,f.flat.sketch_id);graph.use(f.id,f.bend.sketch_id);
        if(f.feature_kind==document::FeatureKind::DerivedCopy){
            for(const auto& source:f.derived_copy.source_ids)
                graph.use(f.id,source);
            graph.reference(f.id,f.derived_copy.reference);
        }
        for(const auto& owner:f.sheet_state.owners)graph.use(f.id,owner);
        for(const auto& owner:f.solid_state.owners)graph.use(f.id,owner);
    }
    return graph;
}
std::string signature(const HistoryDependencyCollector& graph,const std::string& consumer,
        const std::set<std::string>& removed_sources = {}) {
    nlohmann::json refs=nlohmann::json::array(),edges=nlohmann::json::array();
    for(const auto& [target,path,owner,key]:graph.references)if(target==consumer)refs.push_back({path,owner,key});
    for(const auto& [source,target]:graph.edges)
        if(target==consumer && !removed_sources.contains(source))edges.push_back(source);
    return nlohmann::json{{"references",refs},{"sources",edges}}.dump();
}
}
std::set<std::string> part_history_dependency_closure(const document::PartDocument& doc,const std::string& id,bool consumers) {
    auto graph=dependencies(doc);
    if(std::ranges::any_of(doc.history,[](const auto& feature) {
        return document::is_solid_state(feature.feature_kind)||document::is_sheet_state(feature.feature_kind);
    })) {
        // State commands also require the preceding state of each selected
        // source. Inspect authored definitions even while those predecessors
        // are suppressed; restoration must be able to reach them again.
        auto authored=doc;
        for(auto& feature:authored.history)feature.suppressed=false;
        for(const auto& body:authored.body_history.bodies()) {
            std::map<std::string,std::string> previous_states;
            for(const auto& entry:body.entries) {
                const auto* feature=authored.find_container(entry.id);
                if(!feature)continue;
                std::vector<std::string> sources;
                if(document::is_solid_state(feature->feature_kind)) {
                    sources=feature->solid_state.all?solid_state_sources(authored,
                        feature->feature_kind==document::FeatureKind::RestoreShape,feature->id):feature->solid_state.owners;
                } else if(document::is_sheet_state(feature->feature_kind)) {
                    sources=feature->sheet_state.owners;
                    if(feature->sheet_state.all)for(const auto& region:sheet_state_regions(authored,feature->id))
                        sources.push_back(region.owner_id);
                } else continue;
                for(const auto& source:sources) {
                    graph.use(feature->id,source);
                    if(const auto prior=previous_states.find(source);prior!=previous_states.end())graph.use(feature->id,prior->second);
                    previous_states[source]=feature->id;
                }
            }
        }
    }
    std::set<std::string> reached{id};
    for(bool changed=true;changed;) {
        changed=false;
        for(const auto& [source,target]:graph.edges) {
            const auto& from=consumers?source:target;
            const auto& to=consumers?target:source;
            if(reached.contains(from))changed|=reached.insert(to).second;
        }
    }
    return reached;
}
HistoryDeletionPlan plan_history_deletion(const document::PartDocument& doc,const std::string& id) {
    HistoryDeletionPlan plan;plan.removed.insert(id);
    if(const auto* body=doc.body_history.find(id))for(const auto& entry:body->entries)plan.removed.insert(entry.id);
    if(const auto* selected=doc.find_container(id)) {
        if(!selected->sheet_profile_binding.parent_id.empty())
            throw std::invalid_argument(
                "Edit or delete this generated feature through its parent Profile.");
        if(selected->feature_kind==document::FeatureKind::SheetProfile)
            for(const auto& feature:doc.history)
                if(feature.sheet_profile_binding.parent_id==selected->id)
                    plan.removed.insert(feature.id);
    }
    const auto graph=dependencies(doc,true);
    // A self-contained retained feature is a dependency boundary.  It loses
    // only the direct reference to the deleted source and keeps publishing its
    // own profile/primitive from the last valid persisted frame.  Descendants
    // therefore continue to reference that retained feature normally.  Only
    // operations whose geometry is inherently an edit of their input cannot
    // publish an independent result and carry the unavailable chain onward.
    const auto propagates_loss=[&](const std::string& owner) {
        const auto* feature=doc.find_container(owner);
        return feature&&(
            feature->feature_kind==document::FeatureKind::Fillet||
            feature->feature_kind==document::FeatureKind::Chamfer||
            feature->feature_kind==document::FeatureKind::Shell);
    };
    auto unavailable=plan.removed;
    for(bool changed=true;changed;) {
        changed=false;
        for(const auto& [source,target]:graph.edges) {
            if(!unavailable.contains(source)||plan.removed.contains(target))
                continue;
            plan.affected.insert(target);
            if(propagates_loss(target))
                changed|=unavailable.insert(target).second;
        }
    }
    return plan;
}
void detach_deleted_history_references(document::PartDocument& doc,const HistoryDeletionPlan& plan) {
    const auto graph=dependencies(doc,true);
    const auto root_owner=[&](const std::string& owner) -> const std::string& {
        const auto alias=graph.owners.find(owner);
        return alias==graph.owners.end()?owner:alias->second;
    };
    const auto lost=[&](const std::string& owner) {
        const auto& root=root_owner(owner);
        return plan.removed.contains(root)||plan.affected.contains(root);
    };
    const auto local=[&](const auto& ref){return ref.instance_path.empty()&&lost(ref.owner_id);};
    const auto refs=[&](auto& list){std::erase_if(list,local);};
    const auto reference=[&](auto& ref){if(local(ref))ref={};};
    const auto targets=[&](auto& values){std::erase_if(values,[&](const auto& value){return local(value.reference);});};
    const auto construction=[&](const auto& self,auto& value)->void {
        refs(value.references);
        for(auto& end:value.axis_ends)reference(end.target);
        for(auto& point:value.curve_points)self(self,point);
    };
    for(auto& f:doc.history)if(plan.affected.contains(f.id)) {
        // An owned Sketch is still part of the retained feature definition.
        const auto id=[&](std::string& value){if(root_owner(value)!=f.id&&lost(value))value.clear();};
        const bool lost_placement=std::ranges::any_of(
            f.placement.references,local);
        refs(f.placement.references);
        if(lost_placement)f.placement.reference_valid=false;
        id(f.feature.sketch_id);id(f.extrusion.sketch_id);id(f.revolution.sketch_id);
        id(f.holes.sketch_id);id(f.flat.sketch_id);id(f.bend.sketch_id);
        for(auto& side:f.feature.sides)targets(side.targets);
        targets(f.extrusion.end_targets_forward);targets(f.extrusion.end_targets_reverse);
        if(local(f.extrusion.target_face)){f.extrusion.target_face={};f.extrusion.target_surface_triangles.clear();}
        targets(f.thread.end_targets_forward);targets(f.thread.length_end_targets);
        reference(f.shaft_thread.cylinder);reference(f.shaft_thread.start);
        if(f.shaft_thread.chamfer&&local(*f.shaft_thread.chamfer))f.shaft_thread.chamfer.reset();
        if(f.shaft_thread.end&&local(*f.shaft_thread.end))f.shaft_thread.end.reset();
        for(auto& route:f.edge_treatment.routes)refs(route);
        for(auto& start:f.edge_treatment.route_start_vertices)reference(start);
        // An empty route has no selectable input. Retain the feature, while
        // letting its ordinary Properties editor start a new route normally.
        for(std::size_t i=f.edge_treatment.routes.size();i-->0;)
            if(f.edge_treatment.routes[i].empty()) {
                f.edge_treatment.routes.erase(f.edge_treatment.routes.begin()+i);
                if(i<f.edge_treatment.route_start_vertices.size())
                    f.edge_treatment.route_start_vertices.erase(f.edge_treatment.route_start_vertices.begin()+i);
            }
        refs(f.shell.removed_faces);refs(f.drill_point.bottom_faces);refs(f.surface_sewing.faces);
        for(auto& face:f.surface_intersection.faces)reference(face);
        reference(f.surface_trim.target);
        reference(f.surface_thicken.face);
        reference(f.sheet_form.support);
        for(auto& tool:f.surface_trim.tools)reference(tool.reference);
        if(f.sweep2d.path_plane&&local(*f.sweep2d.path_plane))f.sweep2d.path_plane.reset();
        construction(construction,f.sweep3d.path);
        if((f.feature_kind==document::FeatureKind::GeneralSurface||f.feature_kind==document::FeatureKind::Loft))
            for(auto& boundary:f.feature_kind==document::FeatureKind::Loft?f.loft.sketches:f.general_surface.boundaries) {
                if(boundary.curve)construction(construction,*boundary.curve);
                else refs(boundary.sketch_feature->placement.references);
            }
        for(auto& profile:f.sweep3d.profiles)id(profile.sketch_id);
        for(auto& boundary:f.boundary_surface.boundaries) {
            if(lost(boundary.owner_id))boundary={};
            else if(boundary.support&&lost(boundary.support->owner_id))boundary.support.reset();
        }
        std::erase_if(f.derived_copy.source_ids,[&](const auto& source) {
            return root_owner(source)!=f.id&&lost(source);
        });
        reference(f.derived_copy.reference);
        std::erase_if(f.sheet_state.owners,lost);
        std::erase_if(f.solid_state.owners,lost);
    }
    auto bodies=doc.body_history;
    bool bodies_changed=false;
    for(auto body:bodies.bodies())if(plan.affected.contains(body.scope.id)) {
        const auto before=body;
        refs(body.scope.placement.references);
        std::erase_if(body.dependencies,lost);
        if(body!=before) {
            bodies.update_body(std::move(body));
            bodies_changed=true;
        }
    }
    if(bodies_changed)doc.set_body_history(std::move(bodies));
    for(auto& object:doc.constructions)if(plan.affected.contains(object.id))construction(construction,object);
    static_cast<void>(update_document_sketches(doc,[&](auto& sketch) {
        const auto owner=sketch.owner_container_id.empty()?sketch.id:sketch.owner_container_id;
        if(!plan.affected.contains(owner))return false;
        bool changed=false;
        // An unowned Sketch uses its persisted frame while a Plane reference
        // is present, even when that Plane no longer resolves.
        if(!sketch.owner_container_id.empty() && lost(sketch.plane_reference_owner_id)) {
            sketch.plane_reference_owner_id.clear();changed=true;
        }
        std::vector<std::string> removed;
        for(auto& ref:sketch.external_references) {
            if ((!ref.source_document_id.empty() && ref.source_document_id != doc.document_id) ||
                    !ref.source_instance_path.empty() || !lost(ref.source_owner_id))
                continue;
            // The sheet attachment editor replaces these endpoints in place.
            // Keep their identities, endpoint dimensions and last coordinates.
            if (ref.id.starts_with(sketch.id + ":attachment:") ||
                    ref.id.starts_with(sketch.id + ":flat-attachment:")) {
                ref.broken = true;
                changed = true;
            } else removed.push_back(ref.id);
        }
        for(const auto& id:removed){sketch.remove_geometry(id);changed=true;}
        return changed;
    }));
    const auto after=dependencies(doc,true);
    for(const auto& owner:plan.affected)doc.removed_reference_states[owner]=signature(after,owner,plan.removed);
    for(const auto& owner:plan.removed){doc.removed_reference_states.erase(owner);doc.reference_errors.erase(owner);}
}
bool acknowledge_retained_history_edit(document::PartDocument& doc,
        const std::string& owner) {
    const auto missing=doc.removed_reference_states.find(owner);
    if(missing==doc.removed_reference_states.end())return false;
    if(auto* feature=doc.find_container(owner)) {
        if(feature->feature_kind==document::FeatureKind::Fillet||
                feature->feature_kind==document::FeatureKind::Chamfer||
                feature->feature_kind==document::FeatureKind::Shell)
            return false;
        // An attached Flat is otherwise a complete authored profile. Once its
        // source edge has been deleted and Properties acknowledges the retained
        // feature, its persisted numeric placement and resolved Sketch frame
        // become the independent definition. Leaving sheet_attachment enabled
        // would reinterpret the intentionally empty reference list as a new
        // malformed attachment and discard that last valid frame.
        if(feature->feature_kind==document::FeatureKind::Flat&&
                feature->flat.sheet_attachment&&
                feature->placement.references.empty())
            feature->flat.sheet_attachment=false;
    } else if(!doc.body_history.find(owner))return false;
    doc.removed_reference_states.erase(missing);
    doc.reference_errors.erase(owner);
    return true;
}
bool refresh_removed_reference_states(document::PartDocument& doc) {
    if(doc.removed_reference_states.empty())return false;
    const auto graph=dependencies(doc,true);bool changed=false;
    for(auto it=doc.removed_reference_states.begin();it!=doc.removed_reference_states.end();) {
        if(signature(graph,it->first)!=it->second) {
            doc.reference_errors.erase(it->first);it=doc.removed_reference_states.erase(it);changed=true;
        } else {if(auto* object=doc.find_construction(it->first))object->reference_valid=false;++it;}
    }
    return changed;
}
}
