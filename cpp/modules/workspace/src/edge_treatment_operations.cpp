#include <zima/workspace/edge_treatment_operations.hpp>
#include <zima/workspace/operation_input.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
namespace zima::workspace {
namespace {
using Key=std::pair<std::string,std::string>;
template<class Ref> Key key(const Ref& ref){return {ref.owner_id,ref.semantic_key};}
using Groups=std::map<Key,std::vector<const kernel::ViewerEdge*>>;
Groups group_edges(const std::vector<kernel::ViewerEdge>& edges) {
    Groups groups;for(const auto& edge:edges)if(edge.reference.valid()&&edge.reference.instance_path.empty())groups[key(edge.reference)].push_back(&edge);
    return groups;
}
std::vector<kernel::VertexReference> endpoints(const Groups& available,const std::vector<kernel::EdgeReference>& route) {
    std::map<Key,std::pair<kernel::VertexReference,std::vector<std::size_t>>> vertices;
    std::vector<std::vector<Key>> connections;std::set<Key> unique;
    for(const auto& ref:route) {
        const auto found=available.find(key(ref));
        if(!ref.valid()||!ref.instance_path.empty()||!unique.insert(key(ref)).second||found==available.end()||found->second.size()!=1)return {};
        const auto& ends=found->second.front()->edge_treatment_endpoint_references;
        if(ends.size()!=2)return {};
        connections.emplace_back();
        for(const auto& end:ends) {
            if(!end.valid()||!end.instance_path.empty())return {};
            auto& vertex=vertices[key(end)];vertex.first=end;vertex.second.push_back(connections.size()-1);
            if(vertex.second.size()>2)return {};
            connections.back().push_back(key(end));
        }
    }
    if(connections.empty())return {};
    std::set<std::size_t> visited{0};std::vector<std::size_t> pending{0};
    while(!pending.empty()) {
        const auto edge=pending.back();pending.pop_back();
        for(const auto& end:connections[edge])for(const auto next:vertices.at(end).second)
            if(visited.insert(next).second)pending.push_back(next);
    }
    if(visited.size()!=route.size())return {};
    std::vector<kernel::VertexReference> result;
    for(const auto& [id,vertex]:vertices)if(vertex.second.size()==1)result.push_back(vertex.first);
    return result.size()==2?result:std::vector<kernel::VertexReference>{};
}
}
std::vector<kernel::VertexReference> edge_treatment_route_endpoints(const std::vector<kernel::ViewerEdge>& edges,const std::vector<kernel::EdgeReference>& route) {
    return endpoints(group_edges(edges),route);
}
bool commit_edge_treatment(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,
    document::HistoryContainer feature,EdgeTreatmentEditMode mode) {
    using Error=EdgeTreatmentOperationError;using Parameters=document::EdgeTreatmentParameters;
    auto* state=live.open_part(id);
    if(!state)throw Error("unsupported_document","Edge treatment operations require an open Part.");
    const bool fillet=feature.feature_kind==document::FeatureKind::Fillet;
    if((!fillet&&feature.feature_kind!=document::FeatureKind::Chamfer)||feature.combine_mode!=document::CombineMode::Add)
        throw Error("wrong_feature","This container is not a Fillet or Chamfer.");
    const auto& before=state->session.document();const auto* stored=before.find_container(feature.id);
    if(mode==EdgeTreatmentEditMode::Replace) {
        if(!stored)throw Error("container_not_found","The requested container does not exist.");
        if(stored->feature_kind!=feature.feature_kind)throw Error("wrong_feature","The requested edge treatment type does not match the container.");
        if(stored->feature_id!=feature.feature_id||stored->feature_parent_id!=feature.feature_parent_id||stored->container_origin!=feature.container_origin)
            throw Error("identity_changed","Editing must preserve the container identity.");
        const auto locked=[&](const char* name,double original,double requested) {
            if(stored->value_locks.contains(name)&&feature.value_locks.contains(name)&&original!=requested)
                throw Error("value_locked","Unlock the dimension before changing it.");
        };
        locked("primary",stored->edge_treatment.primary_size,feature.edge_treatment.primary_size);
        locked("secondary",stored->edge_treatment.secondary_size,feature.edge_treatment.secondary_size);
        locked("treatment_angle",stored->edge_treatment.angle_degrees,feature.edge_treatment.angle_degrees);
    }else if(stored||feature.id.empty()||feature.feature_id.empty())
        throw Error("identity_changed","A new container must have a new nonempty identity.");
    const auto* body=stored?before.body_owner_for_object(feature.id):before.body_history.find(before.body_history.active_body_id());
    if(body&&body->derived_copy)throw Error("read_only_body","A derived Body cannot be edited directly.");
    const auto& parameters=feature.edge_treatment;
    for(const double value:{parameters.primary_size,parameters.secondary_size})
        if(!std::isfinite(value)||value<.001||value>1e6)
            throw Error("invalid_arguments","Edge treatment sizes must be between 0.001 and 1000000 mm.");
    if(!std::isfinite(parameters.angle_degrees)||parameters.angle_degrees<.1||parameters.angle_degrees>89.9)
        throw Error("invalid_arguments","Chamfer angle must be between 0.1 and 89.9 degrees.");
    if((fillet&&parameters.fillet_mode!=Parameters::FilletMode::Constant&&parameters.fillet_mode!=Parameters::FilletMode::Linear)||
        (!fillet&&parameters.chamfer_mode!=Parameters::ChamferMode::EqualDistance&&parameters.chamfer_mode!=Parameters::ChamferMode::TwoDistances&&parameters.chamfer_mode!=Parameters::ChamferMode::DistanceAngle))
        throw Error("invalid_arguments","Unsupported edge treatment mode.");
    const auto* input=calculated_operation_input(state->session,stored?feature.id:std::string{});
    if(!input||input->mesh.triangles.empty())throw Error("missing_input","Edge treatment requires a calculated input body.");
    if(parameters.routes.empty())throw Error("invalid_reference","Select at least one input edge route.");
    if(parameters.route_start_vertices.size()>parameters.routes.size())throw Error("invalid_reference","Each edge route may have only one R1 endpoint.");
    const auto available=group_edges(input->mesh.edges);std::set<Key> unique;
    for(std::size_t i=0;i<parameters.routes.size();++i) {
        const auto& route=parameters.routes[i];
        if(route.empty())throw Error("invalid_reference","Select at least one input edge route.");
        for(const auto& ref:route) {
            const auto found=available.find(key(ref));
            if(!ref.valid()||!ref.instance_path.empty()||found==available.end()||found->second.size()!=1)
                throw Error("invalid_reference","Select an unambiguous edge of the calculated input body.");
            if(!unique.insert(key(ref)).second)throw Error("invalid_reference","An input edge may belong to only one selected route.");
        }
        const auto start=i<parameters.route_start_vertices.size()?parameters.route_start_vertices[i]:kernel::VertexReference{};
        if(!start.instance_path.empty())throw Error("invalid_reference","Select a local persisted input reference.");
        if(fillet&&parameters.fillet_mode==Parameters::FilletMode::Linear) {
            const auto ends=endpoints(available,route);
            if(!start.valid()||!start.instance_path.empty()||std::ranges::find(ends,start)==ends.end())
                throw Error("invalid_reference","R1 must identify an endpoint of one connected open input route.");
        }
    }
    if(stored&&*stored==feature)return false;
    const auto feature_id=feature.id;auto next=before;
    if(stored)*next.find_container(feature.id)=std::move(feature);
    else {next.insert_history_entry(document::PartHistoryKind::Feature,feature.id);next.history.push_back(std::move(feature));}
    const auto& previous=state->session.calculated_boundaries();
    // Same construction-resolution sequence as the existing Properties OK.
    // Fillet/Chamfer do not expose an independent placement operation.
    auto references=construction_reference_source_geometry(previous);
    append_reference_geometry(references,next.origin_viewer_mesh().original_references);
    append_reference_geometry(references,next.construction_viewer_mesh().original_references);
    next.resolve_constructions(references);
    PartCalculationPolicy policy;policy.reject_errors=true;
    if(stored){policy.edited_document_id=id;policy.edited_history_limit=before.history_index(feature_id);}
    // Preserve the Tree route-edit contract: dependent placements and Sketch
    // projections must describe the same explicitly calculated result.
    auto calculated=calculate_part_with_resolved_references(kernel,next,&previous,policy);
    state->session.commit(std::move(next),std::move(calculated));return true;
}
} // namespace zima::workspace
