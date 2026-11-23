#include <zima/workspace/render_operations.hpp>
#include <zima/workspace/calculated_body_source.hpp>
#include <zima/document/placement_reference_geometry.hpp>
#include <zima/kernel/stable_id.hpp>
#include <algorithm>

namespace zima::workspace {
bool commit_render_images(Workspace& live,const std::string& document_id,const std::string& body_id,
        std::vector<document::RenderImage> images) {
    auto* state=live.open_part(document_id);
    const auto* body=state?state->session.document().body_history.find(body_id):nullptr;
    if(!body||!body->render)throw std::invalid_argument("The requested Render Body does not exist.");
    if(body->render->images==images)return false;
    document::validate_render_images(images);
    auto next=state->session.document();auto value=*body;value.render->images=std::move(images);
    auto graph=next.body_history;graph.update_body(std::move(value));next.set_body_history(std::move(graph));
    state->session.commit_metadata(std::move(next));return true;
}
BodyGraphEdit prepare_render_edit(const document::PartDocument& doc,const std::string& id) {
    if(!id.empty()&&(!doc.body_history.find(id)||!doc.body_history.find(id)->render))
        throw std::invalid_argument("The requested Render Body does not exist.");
    return {doc.document_id,id.empty()?kernel::make_stable_id():id,doc.body_history,doc.body_history};
}
std::vector<std::string> render_source_candidates(const document::DocumentSession& session,const BodyGraphEdit& edit) {
    return calculated_material_candidates(session,edit);
}
std::vector<std::pair<std::string,kernel::BodySnapshot>> read_render_sources(const document::DocumentSession& session,const std::string& id) {
    const auto& graph=session.document().body_history;
    if(graph.render_source_state(id)!=document::RenderSourceState::Available)
        throw std::invalid_argument("The Render source is missing or suppressed.");
    std::vector<std::pair<std::string,kernel::BodySnapshot>> result;
    for(const auto& source_id:graph.render_sources(id)) {
        const auto* source=calculated_material_source(session,source_id);
        if(!source)throw std::invalid_argument("Render requires valid calculated Bodies. Regenerate the sources first.");
        result.emplace_back(source_id,*source);
    }
    return result;
}
kernel::ViewerReferenceGeometry render_placement_geometry(const document::DocumentSession& session,const std::string& id) {
    kernel::ViewerReferenceGeometry geometry;
    // References remain valid when a material Body is hidden or excluded from
    // the image. Only preceding calculated material can position scene objects.
    for(const auto& source_id:calculated_material_candidates(session,prepare_render_edit(session.document(),id))) {
        const auto source=calculated_material_source(session,source_id);
        auto add=material_source_placement_geometry(session.document(),*source,source_id);
        const auto offset=static_cast<std::uint32_t>(geometry.vertices.size());
        geometry.vertices.insert(geometry.vertices.end(),add.vertices.begin(),add.vertices.end());
        for(auto t:add.triangles)geometry.triangles.push_back(t+offset);
        geometry.triangle_references.insert(geometry.triangle_references.end(),add.triangle_references.begin(),add.triangle_references.end());
        geometry.edges.insert(geometry.edges.end(),add.edges.begin(),add.edges.end());
        geometry.points.insert(geometry.points.end(),add.points.begin(),add.points.end());
        geometry.axes.insert(geometry.axes.end(),add.axes.begin(),add.axes.end());
    }
    return geometry;
}
const document::RenderObject* find_render_object(const document::PartDocument& doc,const std::string& body_id,const std::string& id) {
    const auto* body=doc.body_history.find(body_id);if(!body||!body->render)return nullptr;
    const auto it=std::ranges::find(body->render->objects,id,&document::RenderObject::id);
    return it==body->render->objects.end()?nullptr:&*it;
}
bool commit_render(Workspace& live,const BodyGraphEdit& edit,document::BodyHistory value) {
    auto* state=live.open_part(edit.document_id);
    if(!state)throw BodyOperationError("body_history_changed","Body history changed while properties were open.");
    auto& session=state->session;validate_body_graph_edit(session,edit);
    if(value.scope.id!=edit.object_id||!value.render)throw std::invalid_argument("The requested Render Body does not exist.");
    const auto* initial=session.document().body_history.find(edit.object_id);
    if(initial&&!initial->render)throw std::invalid_argument("Cannot change the kind of an existing Body.");
    if(initial&&*initial==value)return false;
    if(!initial||initial->render->source_ids!=value.render->source_ids) {
        const auto candidates=render_source_candidates(session,edit);
        for(const auto& id:value.render->source_ids) {
            const bool retained=initial&&std::ranges::find(initial->render->source_ids,id)!=initial->render->source_ids.end();
            if(!retained&&std::ranges::find(candidates,id)==candidates.end())
                throw std::invalid_argument("Render requires valid calculated Bodies. Regenerate the sources first.");
        }
    }
    document::validate_render_definition(*value.render);
    const auto geometry=render_placement_geometry(session,initial?edit.object_id:std::string{});
    for(auto& object:value.render->objects) {
        const auto* old=find_render_object(session.document(),edit.object_id,object.id);
        if(old&&old->kind!=object.kind)throw std::invalid_argument("Invalid Render definition.");
        if(old&&old->placement==object.placement)continue;
        auto resolved=object.placement;
        if(document::resolve_placement(resolved,geometry))object.placement=std::move(resolved);
        else {
            if(!old)throw std::invalid_argument("The Render object placement is unresolved.");
            object.placement=old->placement;object.placement.references=std::move(resolved.references);
            object.placement.reference_valid=false;
        }
    }
    auto next=session.document();auto graph=next.body_history;
    if(initial)graph.update_body(std::move(value));else static_cast<void>(graph.create_render(std::move(value)));
    if(graph==next.body_history)return false;
    next.set_body_history(std::move(graph));session.commit_metadata(std::move(next));return true;
}
bool commit_render_object(Workspace& live,const BodyGraphEdit& edit,document::RenderObject object) {
    const auto* state=live.open_part(edit.document_id);
    const auto* body=state?state->session.document().body_history.find(edit.object_id):nullptr;
    if(!body||!body->render)throw std::invalid_argument("The requested Render Body does not exist.");
    auto value=*body;auto& objects=value.render->objects;
    const auto found=std::ranges::find(objects,object.id,&document::RenderObject::id);
    if(found==objects.end())objects.push_back(std::move(object));else *found=std::move(object);
    return commit_render(live,edit,std::move(value));
}
bool erase_render_object(Workspace& live,const BodyGraphEdit& edit,const std::string& id) {
    const auto* state=live.open_part(edit.document_id);
    const auto* body=state?state->session.document().body_history.find(edit.object_id):nullptr;
    if(!body||!body->render)throw std::invalid_argument("The requested Render Body does not exist.");
    auto value=*body;std::erase_if(value.render->objects,[&](const auto& object){return object.id==id;});
    return commit_render(live,edit,std::move(value));
}
} // namespace zima::workspace
