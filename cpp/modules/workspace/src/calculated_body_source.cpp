#include <zima/workspace/calculated_body_source.hpp>
#include <algorithm>

namespace zima::workspace {
void validate_body_graph_edit(const document::DocumentSession& session,const BodyGraphEdit& edit) {
    if(session.document().document_id!=edit.document_id||session.document().body_history!=edit.original)
        throw BodyOperationError("body_history_changed","Body history changed while properties were open.");
}
const kernel::BodySnapshot* calculated_material_source(const document::DocumentSession& session,const std::string& id) {
    const auto& graph=session.document().body_history;
    const auto* body=graph.find(id);const auto* operation=graph.find_boolean(id);
    if(body?(body->kind()!=document::BodyKind::Material||body->suppressed):(!operation||operation->suppressed))return nullptr;
    if(session.calculated_boundaries().empty())return nullptr;
    const auto& outputs=session.calculated_boundaries().back().body_outputs;
    const auto found=outputs.find(id);if(found==outputs.end())return nullptr;
    const auto& result=found->second.get();
    if(!result.solid_present||result.kernel_shape.empty()||result.source_fingerprint.empty()||
            !result.calculation_errors.empty())return nullptr;
    return &found->second;
}
std::vector<std::string> calculated_material_candidates(const document::DocumentSession& session,const BodyGraphEdit& edit) {
    validate_body_graph_edit(session,edit);
    const auto& graph=session.document().body_history;
    const auto own=std::ranges::find(graph.order(),edit.object_id);
    const auto boundary=own==graph.order().end()?graph.insertion_cursor():static_cast<std::size_t>(own-graph.order().begin());
    std::vector<std::string> result;
    for(std::size_t i=0;i<boundary;++i)if(calculated_material_source(session,graph.order()[i]))result.push_back(graph.order()[i]);
    return result;
}
std::string material_source_frame_owner(const document::BodyHistoryGraph& graph,std::string source) {
    for(std::size_t i=0;i<graph.order().size();++i) {
        if(const auto* body=graph.find(source))return body->kind()==document::BodyKind::Material?source:std::string{};
        const auto* boolean=graph.find_boolean(source);if(!boolean)return {};source=boolean->target_id;
    }
    return {};
}
kernel::ViewerReferenceGeometry material_source_placement_geometry(const document::PartDocument& doc,
        const kernel::BodySnapshot& source,const std::string& source_id) {
    auto geometry=source->mesh.original_references;
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
    append(doc.origin_viewer_mesh().original_references);
    auto origins=doc.body_origin_reference_geometry();
    const auto frame=material_source_frame_owner(doc.body_history,source_id);
    const auto belongs=[&](const auto& item){return item.reference.owner_id!=frame+":origin";};
    std::erase_if(origins.edges,belongs);std::erase_if(origins.points,belongs);std::erase_if(origins.axes,belongs);append(origins);
    return geometry;
}
} // namespace zima::workspace
