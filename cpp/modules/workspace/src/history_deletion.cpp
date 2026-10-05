#include <zima/workspace/history_deletion.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/workspace/solid_state_operations.hpp>
#include <zima/workspace/sheet_state_operations.hpp>
#include <nlohmann/json.hpp>
namespace zima::workspace {
namespace {
HistoryDependencyCollector dependencies(const document::PartDocument& doc) {
    auto graph=part_history_dependency_graph(doc);
    for(const auto& f:doc.history) {
        graph.use(f.id,f.holes.sketch_id);graph.use(f.id,f.flat.sketch_id);graph.use(f.id,f.bend.sketch_id);
        if(f.feature_kind==document::FeatureKind::DerivedCopy){graph.use(f.id,f.derived_copy.source_id);graph.reference(f.id,f.derived_copy.reference);}
        for(const auto& owner:f.sheet_state.owners)graph.use(f.id,owner);
        for(const auto& owner:f.solid_state.owners)graph.use(f.id,owner);
    }
    return graph;
}
std::string signature(const HistoryDependencyCollector& graph,const std::string& consumer) {
    nlohmann::json refs=nlohmann::json::array(),edges=nlohmann::json::array();
    for(const auto& [target,path,owner,key]:graph.references)if(target==consumer)refs.push_back({path,owner,key});
    for(const auto& [source,target]:graph.edges)if(target==consumer)edges.push_back(source);
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
    const auto graph=dependencies(doc);auto reached=plan.removed;
    for(bool changed=true;changed;) {
        changed=false;
        for(const auto& [source,target]:graph.edges)if(reached.contains(source))changed|=reached.insert(target).second;
    }
    for(const auto& id:reached)if(!plan.removed.contains(id))plan.affected.insert(id);
    return plan;
}
void detach_deleted_history_references(document::PartDocument& doc,const HistoryDeletionPlan& plan) {
    const auto graph=dependencies(doc);
    const auto lost=[&](const std::string& owner) {
        const auto alias=graph.owners.find(owner);const auto& root=alias==graph.owners.end()?owner:alias->second;
        return plan.removed.contains(root)||plan.affected.contains(root);
    };
    const auto local=[&](const auto& ref){return ref.instance_path.empty()&&lost(ref.owner_id);};
    const auto refs=[&](auto& list){std::erase_if(list,local);};
    const auto id=[&](std::string& value){if(lost(value))value.clear();};
    const auto reference=[&](auto& ref){if(local(ref))ref={};};
    const auto targets=[&](auto& values){std::erase_if(values,[&](const auto& value){return local(value.reference);});};
    const auto construction=[&](const auto& self,auto& value)->void {
        refs(value.references);
        for(auto& end:value.axis_ends)reference(end.target);
        for(auto& point:value.curve_points)self(self,point);
    };
    for(auto& f:doc.history)if(plan.affected.contains(f.id)) {
        refs(f.placement.references);
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
        for(auto& tool:f.surface_trim.tools)reference(tool.reference);
        if(f.sweep2d.path_plane&&local(*f.sweep2d.path_plane))f.sweep2d.path_plane.reset();
        construction(construction,f.sweep3d.path);
        if(f.feature_kind==document::FeatureKind::GeneralSurface)
            for(auto& boundary:f.general_surface.boundaries) {
                if(boundary.curve)construction(construction,*boundary.curve);
                else refs(boundary.sketch_feature->placement.references);
            }
        for(auto& profile:f.sweep3d.profiles)id(profile.sketch_id);
        for(auto& boundary:f.boundary_surface.boundaries) {
            if(lost(boundary.owner_id))boundary={};
            else if(boundary.support&&lost(boundary.support->owner_id))boundary.support.reset();
        }
        id(f.derived_copy.source_id);reference(f.derived_copy.reference);
        std::erase_if(f.sheet_state.owners,lost);
        std::erase_if(f.solid_state.owners,lost);
    }
    for(auto& object:doc.constructions)if(plan.affected.contains(object.id))construction(construction,object);
    static_cast<void>(update_document_sketches(doc,[&](auto& sketch) {
        const auto owner=sketch.owner_container_id.empty()?sketch.id:sketch.owner_container_id;
        if(!plan.affected.contains(owner))return false;
        bool changed=false;
        if(lost(sketch.plane_reference_owner_id)){sketch.plane_reference_owner_id.clear();changed=true;}
        std::vector<std::string> removed;
        for(const auto& ref:sketch.external_references)
            if((ref.source_document_id.empty()||ref.source_document_id==doc.document_id)&&ref.source_instance_path.empty()&&lost(ref.source_owner_id))removed.push_back(ref.id);
        for(const auto& id:removed){sketch.remove_geometry(id);changed=true;}
        return changed;
    }));
    const auto after=dependencies(doc);
    for(const auto& owner:plan.affected)doc.removed_reference_states[owner]=signature(after,owner);
    for(const auto& owner:plan.removed){doc.removed_reference_states.erase(owner);doc.reference_errors.erase(owner);}
}
bool refresh_removed_reference_states(document::PartDocument& doc) {
    if(doc.removed_reference_states.empty())return false;
    const auto graph=dependencies(doc);bool changed=false;
    for(auto it=doc.removed_reference_states.begin();it!=doc.removed_reference_states.end();) {
        if(signature(graph,it->first)!=it->second) {
            doc.reference_errors.erase(it->first);it=doc.removed_reference_states.erase(it);changed=true;
        } else {if(auto* object=doc.find_construction(it->first))object->reference_valid=false;++it;}
    }
    return changed;
}
}
