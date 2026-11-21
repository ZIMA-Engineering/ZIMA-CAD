#include <zima/workspace/fem_condition_operations.hpp>
#include <zima/document/fem_presentation.hpp>
#include <zima/document/placement_reference_geometry.hpp>
#include <algorithm>

namespace zima::workspace {
std::string fem_source_frame_owner(const document::BodyHistoryGraph& graph,const std::string& id) {
    const auto* body=graph.find(id);if(!body||!body->fem)return {};
    auto source=body->fem->source_id;
    for(std::size_t i=0;i<graph.order().size();++i) {
        if(const auto* material=graph.find(source))return material->fem?std::string{}:source;
        const auto* boolean=graph.find_boolean(source);if(!boolean)return {};source=boolean->target_id;
    }
    return {};
}
const document::FemCondition* find_fem_condition(const document::PartDocument& doc,const std::string& body,const std::string& id){
    const auto* b=doc.body_history.find(body);if(!b||!b->fem)return nullptr;
    const auto it=std::ranges::find(b->fem->conditions,id,&document::FemCondition::id);return it==b->fem->conditions.end()?nullptr:&*it;
}
kernel::ViewerReferenceGeometry fem_placement_geometry(const document::DocumentSession& session,const std::string& body){
    auto source=read_fem_source(session,body);auto geometry=source->mesh.original_references;
    if(geometry.triangle_references.empty()){geometry.vertices=source->mesh.vertices;geometry.triangles=source->mesh.triangles;geometry.triangle_references=source->mesh.triangle_references;}
    if(geometry.edges.empty())geometry.edges=source->mesh.edges;
    if(geometry.points.empty())geometry.points=source->mesh.points;
    const auto append=[&](const auto& add){
        const auto offset=static_cast<std::uint32_t>(geometry.vertices.size());
        geometry.vertices.insert(geometry.vertices.end(),add.vertices.begin(),add.vertices.end());
        for(auto t:add.triangles)geometry.triangles.push_back(t+offset);
        geometry.triangle_references.insert(geometry.triangle_references.end(),add.triangle_references.begin(),add.triangle_references.end());
        geometry.edges.insert(geometry.edges.end(),add.edges.begin(),add.edges.end());geometry.points.insert(geometry.points.end(),add.points.begin(),add.points.end());geometry.axes.insert(geometry.axes.end(),add.axes.begin(),add.axes.end());
    };
    append(session.document().origin_viewer_mesh().original_references);
    // Only the material source's frame is offered; another analysis cannot
    // become a hidden placement dependency or introduce a cycle.
    auto origins=session.document().body_origin_reference_geometry();
    const auto source_id=fem_source_frame_owner(session.document().body_history,body);
    const auto belongs=[&](const auto& item){return item.reference.owner_id!=source_id+":origin";};
    std::erase_if(origins.edges,belongs);std::erase_if(origins.points,belongs);std::erase_if(origins.axes,belongs);append(origins);
    return geometry;
}
bool commit_fem_condition(Workspace& live,const BodyGraphEdit& edit,document::FemCondition value){
    auto* state=live.open_part(edit.document_id);if(!state)throw std::invalid_argument("The requested FEM Body does not exist.");
    const auto* body=state->session.document().body_history.find(edit.object_id);
    if(!body||!body->fem)throw std::invalid_argument("The requested FEM Body does not exist.");
    const auto* old=find_fem_condition(state->session.document(),edit.object_id,value.id);
    if(old&&*old==value){return commit_fem(live,edit,*body);}
    document::validate_fem_condition(value);
    const auto source=read_fem_source(state->session,edit.object_id);
    for(const auto& r:value.references)if(!document::fem_region_reference_available(r,value.region,source->mesh)) {
        const bool retained=old&&old->region==value.region&&std::ranges::find(old->references,r)!=old->references.end();
        if(!retained)throw std::invalid_argument("The condition geometry is unavailable on the FEM source.");
    }
    const auto geometry=fem_placement_geometry(state->session,edit.object_id);
    auto resolved=value.placement;
    if(document::resolve_placement(resolved,geometry))value.placement=std::move(resolved);
    else {if(!old)throw std::invalid_argument("The condition placement is unresolved.");value.placement=old->placement;value.placement.references=resolved.references;value.placement.reference_valid=false;}
    auto updated=*body;auto& conditions=updated.fem->conditions;
    const auto item=std::ranges::find(conditions,value.id,&document::FemCondition::id);
    if(item==conditions.end())conditions.push_back(std::move(value));else *item=std::move(value);
    return commit_fem(live,edit,std::move(updated));
}
bool erase_fem_condition(Workspace& live,const BodyGraphEdit& edit,const std::string& id){
    const auto* state=live.open_part(edit.document_id);const auto* body=state?state->session.document().body_history.find(edit.object_id):nullptr;
    if(!body||!body->fem)throw std::invalid_argument("The requested FEM Body does not exist.");
    auto value=*body;std::erase_if(value.fem->conditions,[&](const auto& c){return c.id==id;});return commit_fem(live,edit,std::move(value));
}
} // namespace zima::workspace
