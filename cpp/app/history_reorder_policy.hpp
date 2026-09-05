#pragma once
#include <zima/assembly/assembly_document.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace zima::app {
using HistoryDependencies=std::set<std::pair<std::string,std::string>>; // source, consumer
struct HistoryDependencyCollector {
    std::map<std::string,std::string> owners;
    HistoryDependencies edges;
    std::set<std::tuple<std::string,std::string,std::string,std::string>> references;
    void alias(const std::string& id,const std::string& root) {
        if (!id.empty()) owners[id]=root;
    }
    void use(const std::string& consumer,const std::string& owner) {
        const auto source=owners.find(owner);
        if (source!=owners.end() && source->second!=consumer)
            edges.emplace(source->second,consumer);
    }
    template<class Ref> void reference(const std::string& consumer,const Ref& ref) {
        use(consumer,ref.owner_id);
        references.emplace(consumer,ref.instance_path,ref.owner_id,ref.semantic_key);
    }
    void origin(const document::ContainerOrigin& origin,const std::string& root) {
        alias(origin.id,root);
        for (const auto& child : origin.children) alias(child.id,root);
    }
    void register_construction(const document::ConstructionObject& object,const std::string& root) {
        alias(object.id,root);alias(object.entity_id,root);origin(object.container_origin,root);
        for (const auto& connection : object.curve_connections) { alias(connection.id,root);alias(connection.generator_id,root); }
        for (const auto& point : object.curve_points) register_construction(point,root);
    }
    void construction(const document::ConstructionObject& object,const std::string& root) {
        for (const auto& ref : object.references) reference(root,ref);
        for (const auto& point : object.curve_points) construction(point,root);
        for (const auto& connection : object.curve_connections) {
            use(root,connection.sketch_plane_reference_owner_id);
            if (!connection.sketch_serialized.empty()) sketch(sketcher::Sketch::from_serialized(connection.sketch_serialized),root);
        }
    }
    void sketch(const sketcher::Sketch& sketch,const std::string& root) {
        use(root,sketch.plane_reference_owner_id);
        for (const auto& ref : sketch.external_references) {
            use(root,ref.source_owner_id);
            references.emplace(root,ref.source_instance_path,ref.source_owner_id,ref.source_semantic_key);
        }
    }
    void feature(const document::HistoryContainer& feature) {
        const auto& root=feature.id;
        for (const auto& ref : feature.placement.references) reference(root,ref);
        const auto targets=[&](document::EndCondition condition,const auto& values) {
            if (condition==document::EndCondition::UpTo)
                for (const auto& value : values) reference(root,value.reference);
        };
        using document::FeatureKind;
        switch(feature.feature_kind) {
        case FeatureKind::Extrusion:
            use(root,feature.extrusion.sketch_id);
            targets(feature.extrusion.end_condition_forward,feature.extrusion.end_targets_forward);
            if (feature.extrusion.extent_mode==document::ProfileExtentMode::TwoSides)
                targets(feature.extrusion.end_condition_reverse,feature.extrusion.end_targets_reverse);
            break;
        case FeatureKind::Revolution: use(root,feature.revolution.sketch_id);break;
        case FeatureKind::ShaftThread:
            reference(root,feature.shaft_thread.cylinder);reference(root,feature.shaft_thread.start);
            if (feature.shaft_thread.chamfer) reference(root,*feature.shaft_thread.chamfer);
            if (feature.shaft_thread.end_condition==document::EndCondition::UpTo && feature.shaft_thread.end)
                reference(root,*feature.shaft_thread.end);
            break;
        case FeatureKind::Thread:
            targets(feature.thread.end_condition_forward,feature.thread.end_targets_forward);
            if (feature.thread.enabled) targets(feature.thread.length_end_condition,feature.thread.length_end_targets);
            break;
        case FeatureKind::Fillet: case FeatureKind::Chamfer:
            for (const auto& ref : feature.edge_treatment.flattened_edges()) reference(root,ref);
            for (const auto& ref : feature.edge_treatment.route_start_vertices) reference(root,ref);
            break;
        case FeatureKind::Shell:
            for (const auto& ref : feature.shell.removed_faces) reference(root,ref);
            break;
        case FeatureKind::DrillPoint:
            for (const auto& ref : feature.drill_point.bottom_faces) reference(root,ref);
            break;
        case FeatureKind::Sweep2D:
            for(const auto& ref:feature.sweep2d.planes)if(ref)reference(root,*ref);
            for(const auto& data:feature.sweep2d.sketches){auto owned=sketcher::Sketch::from_serialized(data);owned.plane_reference_owner_id.clear();sketch(owned,root);}
            break;
        case FeatureKind::HelicalSweep:
            if(feature.helical.base_plane)reference(root,*feature.helical.base_plane);
            for(const auto& data:feature.helical.sketches){auto owned=sketcher::Sketch::from_serialized(data);owned.plane_reference_owner_id.clear();sketch(owned,root);}
            break;
        case FeatureKind::Sweep3D:
            construction(feature.sweep3d.path,root);
            for (const auto& profile : feature.sweep3d.profiles) {
                use(root,profile.sketch_id);
                if (!profile.sketch_serialized.empty()) sketch(sketcher::Sketch::from_serialized(profile.sketch_serialized),root);
            }
            break;
        default:break;
        }
    }
};
inline HistoryDependencyCollector part_history_dependency_graph(const document::PartDocument& document) {
    HistoryDependencyCollector graph;
    for (const auto& feature : document.history) {
        graph.alias(feature.id,feature.id);graph.alias(feature.feature_id,feature.id);graph.origin(feature.container_origin,feature.id);
    }
    for (const auto& object : document.constructions) graph.register_construction(object,object.id);
    for (const auto& sketch : document.sketches)
        graph.alias(sketch.id,sketch.owner_container_id.empty() ? sketch.id : sketch.owner_container_id);
    for (const auto& feature : document.history) graph.feature(feature);
    for (const auto& object : document.constructions) graph.construction(object,object.id);
    for (const auto& sketch : document.sketches)
        graph.sketch(sketch,sketch.owner_container_id.empty() ? sketch.id : sketch.owner_container_id);
    return graph;
}
inline HistoryDependencies part_history_dependencies(const document::PartDocument& document) {
    return part_history_dependency_graph(document).edges;
}
inline HistoryDependencies assembly_component_dependencies(const assembly::AssemblyDocument& document) {
    HistoryDependencyCollector graph;
    for (const auto& component : document.components) graph.alias(component.occurrence_id,component.occurrence_id);
    for (const auto& object : document.constructions) graph.register_construction(object,object.id);
    const auto reference=[&](const std::string& consumer,const std::string& owner,const assembly::InstancePath& path) {
        graph.use(consumer,path.occurrence_ids.empty() ? owner : path.occurrence_ids.front());
    };
    const auto construction=[&](auto&& self,const document::ConstructionObject& object,const std::string& root) -> void {
        for (const auto& ref : object.references) reference(root,ref.owner_id,assembly::InstancePath::decode(ref.instance_path));
        for (const auto& point : object.curve_points) self(self,point,root);
    };
    for (const auto& object : document.constructions) construction(construction,object,object.id);
    for (const auto& dependency : document.dependencies)
        graph.edges.emplace(dependency.prerequisite_occurrence_id,dependency.dependent_occurrence_id);
    for (const auto& component : document.components)
        for (const auto& row : component.placement_references)
            reference(component.occurrence_id,row.target_reference.owner_id,row.target_reference.instance_path);
    // Include chains through Assembly datum containers, not just direct mates.
    auto edges=graph.edges;
    for (;;) {
        auto expanded=edges;
        for (const auto& [source,middle] : edges)
            for (const auto& [other,consumer] : edges)
                if (middle==other && source!=consumer) expanded.emplace(source,consumer);
        if (expanded==edges) return edges;
        edges=std::move(expanded);
    }
}

inline std::vector<std::string> reordered_history(std::vector<std::string> order,
        const std::string& moved,const std::string& before) {
    const auto source=std::ranges::find(order,moved);
    if (source==order.end() || moved==before) return order;
    if (!before.empty() && std::ranges::find(order,before)==order.end()) return order;
    order.erase(source);
    order.insert(before.empty() ? order.end() : std::ranges::find(order,before),moved);
    return order;
}
inline bool history_order_preserves_dependencies(const std::vector<std::string>& before,
        const std::vector<std::string>& after,const HistoryDependencies& edges) {
    std::map<std::string,std::size_t> old_positions,new_positions;
    for (std::size_t i=0;i<before.size();++i) old_positions[before[i]]=i;
    for (std::size_t i=0;i<after.size();++i) new_positions[after[i]]=i;
    if (old_positions.size()!=before.size() || new_positions.size()!=after.size() || before.size()!=after.size()) return false;
    for (const auto& [id,position] : old_positions) if (!new_positions.contains(id)) return false;
    for (const auto& [source,consumer] : edges) {
        if (!old_positions.contains(source) || !old_positions.contains(consumer)) continue;
        // Existing broken relationships do not prevent unrelated repairs.
        if (old_positions.at(source)<old_positions.at(consumer) &&
            new_positions.at(source)>=new_positions.at(consumer)) return false;
    }
    return true;
}
template<class T,class Id> void sort_history_records(std::vector<T>& values,
        const std::vector<std::string>& order,Id id) {
    std::map<std::string,std::size_t> positions;
    for (std::size_t i=0;i<order.size();++i) positions[order[i]]=i;
    std::stable_sort(values.begin(),values.end(),[&](const auto& a,const auto& b) {
        const auto left=positions.find(id(a)),right=positions.find(id(b));
        return (left==positions.end() ? order.size() : left->second)<
               (right==positions.end() ? order.size() : right->second);
    });
}
}
