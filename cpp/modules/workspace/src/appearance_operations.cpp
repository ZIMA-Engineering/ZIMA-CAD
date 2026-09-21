#include <zima/workspace/appearance_operations.hpp>
#include <zima/document/metadata.hpp>
#include <zima/document/component_source.hpp>
#include <algorithm>
namespace zima::workspace {
namespace {
void original_colors(kernel::Appearance& a,const std::string& body,const std::map<std::string,std::string>& faces) {
    a.body.color=body;
    for(const auto& [key,color]:faces)if(std::ranges::none_of(a.groups,[&](const auto& group){return std::ranges::find(group.faces,key)!=group.faces.end();}))
        a.groups.push_back({"face-"+key,"Plochy "+std::to_string(a.groups.size()+1),{},kernel::SurfaceStyle{color,.55,0},{key}});
}
const assembly::PartOccurrence& occurrence(const Workspace& live,const AppearanceEdit& edit) {
    const auto* state=live.open_assembly(edit.document_id);
    const auto* value=state?state->session.document().find_occurrence(edit.occurrence_id):nullptr;
    if(!value)throw AppearanceError("occurrence_not_found","The requested component occurrence does not exist.");
    return *value;
}
void require_current(const Workspace& live,const AppearanceEdit& edit) {
    if(live.active_document_id()!=edit.document_id)throw AppearanceError("document_changed","Activate the document before changing its appearance.");
    const auto current=prepare_appearance_edit(live,edit.document_id,edit.occurrence_id,edit.body_id);
    if(current.runtime_identity!=edit.runtime_identity||current.revision!=edit.revision||current.initial!=edit.initial)
        throw AppearanceError("stale_edit","The appearance changed while its properties were open.");
    if(const auto* part=live.open_part(edit.document_id);part&&!edit.body_id.empty()&&edit.body_id!=part->session.document().body_history.active_body_id()) {
        const auto* body=part->session.document().body_history.find(edit.body_id);
        // A derived Body is never a writable geometry editor. Its own appearance
        // metadata can be addressed explicitly without activating its geometry.
        if(!body||!body->derived_copy)
            throw AppearanceError("inactive_body","Activate the owning Body before changing its appearance.");
    }
}
void validate_edit(const Workspace& live,const AppearanceEdit& edit,const kernel::Appearance& value) {
    document::validate_appearance(value);
    auto before=edit.initial,after=value;
    const auto protect=[&](kernel::Appearance& appearance) {
        if(edit.body_id.empty())appearance.body={};else appearance.bodies.erase(edit.body_id);
        std::erase_if(appearance.groups,[&](const auto& group){return group.body_id==edit.body_id;});
    };
    protect(before);protect(after);
    if(before!=after)throw AppearanceError("wrong_appearance_scope","Change appearance only in the requested body scope.");
    std::optional<std::set<std::pair<std::string,std::string>>> available;
    for(const auto& group:value.groups) {
        document::validate_native_metadata_text(group.id);document::validate_native_metadata_text(group.name);
        if(group.body_id!=edit.body_id)continue;
        const auto old=std::ranges::find(edit.initial.groups,group.id,&kernel::SurfaceGroup::id);
        for(const auto& face:group.faces) {
            // An unrelated style edit must preserve an unresolved stored face.
            if(old!=edit.initial.groups.end()&&std::ranges::find(old->faces,face)!=old->faces.end())continue;
            if(!available)available=appearance_faces(live,edit);
            const auto split=face.find("::");
            if(split==std::string::npos||!available->contains({face.substr(0,split),face.substr(split+2)}))
                throw AppearanceError("missing_reference","Select a persisted result face in this appearance scope.");
        }
    }
}
}
kernel::Appearance part_appearance(const document::PartDocument& doc) {
    return document::component_appearance(doc);
}
kernel::Appearance occurrence_appearance(const Workspace& live,const assembly::PartOccurrence& component) {
    auto value=component.appearance;
    if(const auto* part=live.open_part(component.source_document_id))value=part_appearance(part->session.document());
    else original_colors(value,component.body_color,component.face_colors);
    if(component.appearance_override) {
        auto overridden=*component.appearance_override;
        overridden.owner_bodies=value.owner_bodies;
        return overridden;
    }
    if(assembly::is_skeleton(component)) { value.body={"#4D8C78AE",.55,0}; value.bodies.clear(); value.groups.clear(); }
    if(component.body_color_override)value.body.color=*component.body_color_override;
    return value;
}
AppearanceEdit prepare_appearance_edit(const Workspace& live,const std::string& id,const std::string& instance,const std::optional<std::string>& body) {
    if(const auto* part=live.open_part(id)) {
        if(!instance.empty())throw AppearanceError("unsupported_context","A Part appearance does not accept a component path.");
        const auto scope=body.value_or(part->session.document().body_history.active_body_id());
        if(!scope.empty()&&!part->session.document().body_history.find(scope))throw AppearanceError("body_not_found","The requested Body does not exist.");
        return {id,{},scope,part->session.revision(),part->runtime_identity,part_appearance(part->session.document())};
    }
    const auto* assembly=live.open_assembly(id);
    if(!assembly)throw AppearanceError("unsupported_document","Appearance requires an open Part or an owned Part occurrence.");
    const auto* component=assembly->session.document().find_occurrence(instance);
    if(!component)throw AppearanceError("occurrence_not_found","The requested component occurrence does not exist.");
    if(component->source_kind!=assembly::ComponentSourceKind::Part)throw AppearanceError("unsupported_context","Select a Part in its immediate owning Assembly.");
    auto initial=occurrence_appearance(live,*component);const auto scope=body.value_or(std::string{});
    if(!scope.empty()&&!initial.bodies.contains(scope)&&std::ranges::none_of(initial.owner_bodies,[&](const auto& entry){return entry.second==scope;}))
        throw AppearanceError("body_not_found","The requested Body does not exist.");
    return {id,instance,scope,assembly->session.revision(),assembly->runtime_identity,std::move(initial)};
}
std::set<std::pair<std::string,std::string>> appearance_faces(const Workspace& live,const AppearanceEdit& edit) {
    std::set<std::pair<std::string,std::string>> result;
    const auto collect=[&](const kernel::ViewerMesh& mesh) {
        for(const auto& ref:mesh.triangle_references) {
            if(ref.owner_id.empty()||ref.semantic_key.empty()||ref.semantic_key=="plane"||ref.semantic_key.starts_with("origin:"))continue;
            if(!edit.body_id.empty()) {
                const auto owner=edit.initial.owner_bodies.find(ref.owner_id);
                if(owner==edit.initial.owner_bodies.end()||owner->second!=edit.body_id)continue;
            }
            result.emplace(ref.owner_id,ref.semantic_key);
        }
    };
    const auto part_faces=[&](const PartState& part) {
        const auto& boundaries=part.session.calculated_boundaries();
        if(boundaries.empty())return;
        collect(boundaries.back().mesh);
        for(const auto& [id,body]:boundaries.back().body_outputs)collect(body->mesh);
    };
    if(const auto* part=live.open_part(edit.document_id))part_faces(*part);
    else {
        const auto& component=occurrence(live,edit);
        if(const auto* source=live.open_part(component.source_document_id))part_faces(*source);
        else {collect(component.calculated_source->mesh);for(const auto& [id,body]:component.calculated_source->body_outputs)collect(body->mesh);}
    }
    return result;
}
bool commit_appearance(Workspace& live,const AppearanceEdit& edit,const kernel::Appearance& value) {
    require_current(live,edit);validate_edit(live,edit,value);
    if(value==edit.initial)return false;
    if(auto* part=live.open_part(edit.document_id)) {
        auto next=part->session.document();next.appearance=value;next.body_color=value.body.color;next.face_colors.clear();
        for(const auto& group:value.groups)for(const auto& face:group.faces)next.face_colors[face]=group.style.color;
        part->session.commit(std::move(next),part->session.calculated_boundaries());
    } else {
        auto* owner=live.open_assembly(edit.document_id);auto next=owner->session.document();
        next.find_occurrence(edit.occurrence_id)->appearance_override=value;owner->session.commit(std::move(next));
    }
    return true;
}
void reset_appearance(kernel::Appearance& value,const std::string& body) {
    std::erase_if(value.groups,[&](const auto& group){return group.body_id==body;});
    if(body.empty())value.body={};else value.bodies.erase(body);
}
bool inherit_occurrence_appearance(Workspace& live,const AppearanceEdit& edit) {
    require_current(live,edit);
    if(edit.occurrence_id.empty()||!edit.body_id.empty())throw AppearanceError("unsupported_context","Source appearance inheritance requires the whole Part occurrence.");
    const auto& component=occurrence(live,edit);
    if(!component.appearance_override&&!component.body_color_override)return false;
    auto* owner=live.open_assembly(edit.document_id);auto next=owner->session.document();
    auto* target=next.find_occurrence(edit.occurrence_id);target->appearance_override.reset();target->body_color_override.reset();
    owner->session.commit(std::move(next));return true;
}
}
