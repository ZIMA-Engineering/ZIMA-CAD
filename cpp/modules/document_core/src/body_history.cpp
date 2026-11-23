#include <zima/document/derived_copy_json.hpp>
#include <zima/document/body_history.hpp>
#include <zima/document/file_relocation.hpp>
#include <zima/document/placement_json.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/kernel/scale_geometry.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/document/file_path.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace zima::document {
void BodyHistoryGraph::prepare_link_file_rebase(FileRelocationEdits& edits,const std::filesystem::path& owning_file) {
    for(auto& body:bodies_)if(body.link)
        edits.source_reference(body.link->document_id,body.link->source_path,owning_file);
}

const BodyHistory* BodyHistoryGraph::find(const std::string& id) const {
    const auto found = std::ranges::find_if(bodies_, [&](const auto& body) { return body.scope.id == id; });
    return found == bodies_.end() ? nullptr : &*found;
}

const BodyHistory* BodyHistoryGraph::owner(const std::string& entry_id) const {
    for (const auto& body : bodies_)
        if (std::ranges::any_of(body.entries, [&](const auto& entry) { return entry.id == entry_id; })) return &body;
    return nullptr;
}

const BodyBoolean* BodyHistoryGraph::find_boolean(const std::string& id) const {
    const auto found = std::ranges::find_if(booleans_, [&](const auto& value) { return value.id == id; });
    return found == booleans_.end() ? nullptr : &*found;
}

void BodyHistoryGraph::erase_step(const std::string& id) {
    auto next=*this;
    const auto found=std::ranges::find(next.order_,id);
    if(found==next.order_.end())throw std::invalid_argument("Body history object does not exist");
    for(const auto& body:next.bodies_)if(body.scope.id!=id&&
        (std::ranges::find(body.dependencies,id)!=body.dependencies.end() ||
         (body.scale&&body.scale->source_id==id) ||
         (body.derived_copy&&std::ranges::any_of(
             body.derived_copy->source_ids,[&](const auto& source) {
                 const auto* source_owner=owner(source);
                 return source==id||(source_owner&&source_owner->scope.id==id);
             }))))
        throw std::invalid_argument("Objekt používá navazující těleso: "+body.name);
    for(const auto& op:next.booleans_)if(op.target_id==id||op.tool_id==id)
        throw std::invalid_argument("Objekt používá navazující Boolean: "+op.name);
    std::vector<std::string> released;
    if(const auto* op=next.find_boolean(id))released={op->target_id,op->tool_id};
    else if(const auto* body=next.find(id);body&&body->derived_copy)
        for(const auto& source:body->derived_copy->source_ids) {
            const auto* source_owner=next.owner(source);
            released.push_back(source_owner?source_owner->scope.id:source);
        }
    const auto position=static_cast<std::size_t>(found-next.order_.begin());
    next.order_.erase(found);
    if(position<next.cursor_)--next.cursor_;
    if(next.active_==id)next.active_.clear();
    std::erase_if(next.bodies_,[&](const auto& body){return body.scope.id==id;});
    std::erase_if(next.booleans_,[&](const auto& op){return op.id==id;});
    const auto available=next.available_before(next.order_.size());
    for(const auto& source:released)if(std::ranges::find(available,source)!=available.end()) {
        for(auto& body:next.bodies_)if(body.scope.id==source)body.visible=true;
        for(auto& op:next.booleans_)if(op.id==source)op.visible=true;
    }
    next.validate();
    *this=std::move(next);
}

void BodyHistoryGraph::sort_bodies() {
    std::ranges::sort(bodies_, [&](const auto& a, const auto& b) {
        return std::ranges::find(order_, a.scope.id) < std::ranges::find(order_, b.scope.id);
    });
}

void BodyHistoryGraph::validate() const {
    std::set<std::string> identities, preceding, available, entries, non_material_identities;
    for (const auto& body : bodies_) {
        if (body.scope.id.empty() || !identities.insert(body.scope.id).second)
            throw std::invalid_argument("Body identity is empty or duplicated");
        if(body.kind()!=BodyKind::Material)non_material_identities.insert(body.scope.id);
        if(body.fem&&body.render)throw std::invalid_argument("Invalid Render definition.");
        if(body.cam&&(body.fem||body.render))throw std::invalid_argument("Invalid CAM definition.");
    }
    for (const auto& op : booleans_)
        if (op.id.empty() || !identities.insert(op.id).second)
            throw std::invalid_argument("Boolean identity is empty or duplicated");
    if (order_.size() != identities.size() || cursor_ > order_.size())
        throw std::invalid_argument("Invalid document body history order or cursor");
    auto condition_ids=identities;
    for(const auto& body:bodies_)for(const auto& entry:body.entries)condition_ids.insert(entry.id);
    for(const auto& body:bodies_)if(body.fem)for(const auto& condition:body.fem->conditions)
        if(!condition_ids.insert(condition.id).second)
            throw std::invalid_argument("Invalid FEM condition definition.");
    for(const auto& body:bodies_)if(body.render)for(const auto& object:body.render->objects)
        if(!condition_ids.insert(object.id).second)
            throw std::invalid_argument("Invalid Render definition.");
    for(const auto& body:bodies_)if(body.cam)for(const auto& operation:body.cam->operations)
        if(!condition_ids.insert(cam_operation_id(operation)).second)
            throw std::invalid_argument("Invalid CAM definition.");
    for (const auto& id : order_) {
        if (!identities.contains(id) || preceding.contains(id))
            throw std::invalid_argument("Every body or Boolean must occur once in history");
        if (const auto* body = find(id)) {
            if (body->name.empty() || body->cursor > body->entries.size())
                throw std::invalid_argument("Invalid body name or history cursor");
            if(body->fem) {
                validate_fem_definition(*body->fem);
                if(body->link||body->scale||body->derived_copy||!body->entries.empty()||
                        body->cursor||!body->dependencies.empty()||body->scope.placement!=Placement{})
                    throw std::invalid_argument("A FEM Body cannot own material history or placement.");
                const auto& source=body->fem->source_id;
                const auto* source_body=find(source);
                if(owner(source)||(identities.contains(source)&&!preceding.contains(source))||
                        (source_body&&source_body->kind()!=BodyKind::Material))
                    throw std::invalid_argument("FEM requires a preceding material Body or Boolean result.");
                // A deleted source remains an explicit broken reference. Keep
                // the analysis definition for repair; never replace its source.
            }
            if(body->render) {
                validate_render_definition(*body->render);
                if(body->link||body->scale||body->derived_copy||!body->entries.empty()||
                        body->cursor||!body->dependencies.empty()||body->scope.placement!=Placement{})
                    throw std::invalid_argument("A Render Body cannot own material history or placement.");
                for(const auto& source:body->render->source_ids) {
                    const auto* source_body=find(source);
                    if(owner(source)||(identities.contains(source)&&!preceding.contains(source))||
                            (source_body&&source_body->kind()!=BodyKind::Material))
                        throw std::invalid_argument("Render requires preceding material Bodies or Boolean results.");
                }
                for(const auto& object:body->render->objects)for(const auto& reference:object.placement.references) {
                    auto reference_id=reference.owner_id;
                    if(reference_id.ends_with(":origin"))reference_id.resize(reference_id.size()-7);
                    const auto* reference_owner=owner(reference_id);
                    const auto& branch=reference_owner?reference_owner->scope.id:reference_id;
                    if(non_material_identities.contains(branch)||
                            (condition_ids.contains(branch)&&!identities.contains(branch))||
                            (identities.contains(branch)&&!preceding.contains(branch)))
                        throw std::invalid_argument("Invalid Render definition.");
                }
            }
            if(body->cam) {
                validate_cam_definition(*body->cam);
                if(body->link||body->scale||body->derived_copy||!body->entries.empty()||body->cursor||
                   !body->dependencies.empty()||body->scope.placement!=Placement{})
                    throw std::invalid_argument("A CAM Body cannot own material history or Body placement.");
                const auto check_source=[&](const std::string& source,bool stock) {
                    const auto* source_body=find(source);
                    if(owner(source)||(condition_ids.contains(source)&&!identities.contains(source))||
                       (identities.contains(source)&&!preceding.contains(source))||
                       (source_body&&source_body->kind()!=BodyKind::Material&&!(stock&&source_body->cam)))
                        throw std::invalid_argument("CAM requires preceding material sources or preceding CAM stock.");
                };
                check_source(body->cam->stock_id,true);check_source(body->cam->target_id,false);
                for(const auto& source:body->cam->fixture_ids)check_source(source,false);
                for(const auto& reference:body->cam->work_frame.references) {
                    auto reference_id=reference.owner_id;
                    if(reference_id.ends_with(":origin"))reference_id.resize(reference_id.size()-7);
                    const auto* reference_owner=owner(reference_id);
                    const auto& branch=reference_owner?reference_owner->scope.id:reference_id;
                    if(non_material_identities.contains(branch)||
                       (condition_ids.contains(branch)&&!identities.contains(branch))||
                       (identities.contains(branch)&&!preceding.contains(branch)))
                        throw std::invalid_argument("Invalid CAM definition.");
                }
            }
            if(body->link&&(body->derived_copy||body->scale||!body->entries.empty()||body->cursor||
                body->link->document_id.empty()||body->link->body_id.empty()||body->link->source_path.empty()||
                !body->link->calculated||body->link->calculated->kernel_shape.empty()||
                body->link->calculated->source_fingerprint.empty()||!body->link->calculated->calculation_errors.empty()))
                throw std::invalid_argument("A linked Body requires a valid native source and cannot own a modeling history.");
            if(body->scale) {
                if(body->derived_copy||!body->entries.empty()||body->cursor||
                    !available.contains(body->scale->source_id)||body->scope.placement!=Placement{})
                    throw std::invalid_argument("A scaled Body requires a preceding source and cannot own a modeling history or placement.");
                zima::kernel::validate_body_scale(body->scale->factor,body->scale->center);
            }
            if(body->derived_copy) {
                const auto& sources=body->derived_copy->source_ids;
                if(sources.empty()||std::set<std::string>(sources.begin(),sources.end()).size()!=sources.size()||
                    std::ranges::any_of(sources,[&](const auto& source) {
                        const auto* source_owner=owner(source);
                        const auto source_body=source_owner?source_owner->scope.id:source;
                        return source.empty()||!(source_owner?
                            preceding.contains(source_body):available.contains(source_body));
                    })||!body->entries.empty()||body->cursor)
                    throw std::invalid_argument("Kopie potřebuje předcházející zdroj a nemůže mít vlastní historii.");
                if(body->derived_copy->subtract_source) {
                    if(sources.size()!=1)throw std::invalid_argument(
                        "Subtractive Body copy requires exactly one source solid.");
                    const auto& source=sources.front();
                    const auto* source_owner=owner(source);
                    if(!source_owner)throw std::invalid_argument("Subtractive copy requires a source solid.");
                    if(!available.erase(copy_target_before(source,preceding.size())))
                        throw std::invalid_argument("Subtractive copy requires an available target Body.");
                }
                if(body->derived_copy->pattern)static_cast<void>(zima::kernel::validated_pattern(*body->derived_copy->pattern));
                else static_cast<void>(zima::kernel::normalized_mirror_plane(body->derived_copy->resolved_plane));
            }
            const auto& scope = body->scope;
            for (const auto value : {scope.translation().x, scope.translation().y, scope.translation().z,
                    scope.rotation_degrees().x, scope.rotation_degrees().y, scope.rotation_degrees().z})
                if (!std::isfinite(value)) throw std::invalid_argument("Body placement must be finite");
            const auto& placement = scope.placement;
            for (const auto value : {placement.absolute_rotation_x, placement.absolute_rotation_y, placement.absolute_rotation_z,
                    placement.rotation_offset_x, placement.rotation_offset_y, placement.rotation_offset_z})
                if (!std::isfinite(value)) throw std::invalid_argument("Body orientation must be finite");
            for (const auto& reference : placement.references)
                if (!std::isfinite(reference.offset)) throw std::invalid_argument("Body reference offset must be finite");
            for (const auto& dependency : body->dependencies)
                if (!preceding.contains(dependency)||non_material_identities.contains(dependency)) throw std::invalid_argument("Body reference must precede its dependent body");
            for (const auto& entry : body->entries) {
                if (entry.id.empty() || identities.contains(entry.id) || !entries.insert(entry.id).second)
                    throw std::invalid_argument("Every history entry must have exactly one body owner");
                if (entry.kind != PartHistoryKind::Feature && entry.kind != PartHistoryKind::Sketch && entry.kind != PartHistoryKind::Construction)
                    throw std::invalid_argument("Unknown body history entry kind");
            }
        } else {
            const auto& op = *find_boolean(id);
            if (op.name.empty() || (op.operation != zima::kernel::BodyCombination::Add &&
                    op.operation != zima::kernel::BodyCombination::Subtract && op.operation != zima::kernel::BodyCombination::Intersect))
                throw std::invalid_argument("Invalid Boolean name or operation");
            if (op.target_id == op.tool_id || !available.erase(op.target_id) || !available.erase(op.tool_id))
                throw std::invalid_argument("Boolean requires two distinct available preceding results");
        }
        preceding.insert(id);
        if(!non_material_identities.contains(id))available.insert(id);
    }
    if (!active_.empty() && (!find(active_) || find(active_)->derived_copy || find(active_)->scale || find(active_)->link)) throw std::invalid_argument("Active body does not exist");
    if(!active_.empty() && find(active_)->suppressed)
        throw std::invalid_argument("An active Body cannot be suppressed.");
}

std::string BodyHistoryGraph::create_boolean(std::string name, zima::kernel::BodyCombination operation,
    std::string target_id, std::string tool_id) {
    auto next = *this;
    const auto id = zima::kernel::make_stable_id();
    next.booleans_.push_back({id, std::move(name), operation, std::move(target_id), std::move(tool_id)});
    next.order_.insert(next.order_.begin() + static_cast<std::ptrdiff_t>(cursor_), id);
    ++next.cursor_;
    next.active_.clear();
    next.validate();
    *this = std::move(next);
    return id;
}

void BodyHistoryGraph::update_boolean(BodyBoolean operation) {
    auto next = *this;
    const auto found = std::ranges::find_if(next.booleans_, [&](const auto& value) { return value.id == operation.id; });
    if (found == next.booleans_.end()) throw std::invalid_argument("Boolean does not exist");
    *found = std::move(operation);
    next.validate();
    *this = std::move(next);
}

std::string BodyHistoryGraph::create_body(std::string name) {
    auto next = *this;
    BodyHistory body;
    body.scope.id = zima::kernel::make_stable_id();
    body.name = std::move(name);
    next.active_ = body.scope.id;
    next.order_.insert(next.order_.begin() + static_cast<std::ptrdiff_t>(cursor_), body.scope.id);
    next.bodies_.push_back(std::move(body));
    next.sort_bodies();
    ++next.cursor_;
    next.validate();
    *this = std::move(next);
    return active_;
}

std::string BodyHistoryGraph::create_derived_copy(BodyHistory body) {
    if(!body.derived_copy)throw std::invalid_argument("Chybí parametry kopie.");
    if(body.scope.id.empty())body.scope.id=zima::kernel::make_stable_id();
    const auto id=body.scope.id;auto next=*this;
    next.order_.insert(next.order_.begin()+static_cast<std::ptrdiff_t>(cursor_),id);
    next.bodies_.push_back(std::move(body));next.sort_bodies();++next.cursor_;next.active_.clear();
    next.validate();*this=std::move(next);return id;
}

void BodyHistoryGraph::update_body(BodyHistory body) {
    if(body.fem)refresh_fem_distances(*body.fem);
    auto next = *this;
    const auto found = std::ranges::find_if(next.bodies_, [&](const auto& value) { return value.scope.id == body.scope.id; });
    if (found == next.bodies_.end()) throw std::invalid_argument("Body does not exist");
    if(found->kind()!=body.kind())throw std::invalid_argument("Cannot change the kind of an existing Body.");
    *found = std::move(body);
    next.validate();
    *this = std::move(next);
}

std::string BodyHistoryGraph::create_scale(BodyHistory body) {
    if(!body.scale)throw std::invalid_argument("Body scale parameters are missing.");
    if(body.scope.id.empty())body.scope.id=zima::kernel::make_stable_id();
    const auto id=body.scope.id;auto next=*this;
    next.order_.insert(next.order_.begin()+static_cast<std::ptrdiff_t>(cursor_),id);
    next.bodies_.push_back(std::move(body));next.sort_bodies();++next.cursor_;next.active_.clear();
    next.validate();*this=std::move(next);return id;
}

std::string BodyHistoryGraph::create_link(BodyHistory body) {
    if(!body.link)throw std::invalid_argument("A linked Body requires a valid native source and cannot own a modeling history.");
    if(body.scope.id.empty())body.scope.id=zima::kernel::make_stable_id();
    const auto id=body.scope.id;auto next=*this;
    next.order_.insert(next.order_.begin()+static_cast<std::ptrdiff_t>(cursor_),id);
    next.bodies_.push_back(std::move(body));next.sort_bodies();++next.cursor_;next.active_.clear();
    next.validate();*this=std::move(next);return id;
}

std::string BodyHistoryGraph::create_fem(BodyHistory body) {
    if(body.fem)refresh_fem_distances(*body.fem);
    if(!body.fem||(!find(body.fem->source_id)&&!find_boolean(body.fem->source_id)))
        throw std::invalid_argument("FEM requires a source Body or Boolean result.");
    if(body.scope.id.empty())body.scope.id=zima::kernel::make_stable_id();
    const auto id=body.scope.id;auto next=*this;
    next.order_.insert(next.order_.begin()+static_cast<std::ptrdiff_t>(cursor_),id);
    next.bodies_.push_back(std::move(body));next.sort_bodies();++next.cursor_;next.active_=id;
    next.validate();*this=std::move(next);return id;
}

FemSourceState BodyHistoryGraph::fem_source_state(const std::string& body_id) const {
    const auto* analysis=find(body_id);
    if(!analysis||!analysis->fem)throw std::invalid_argument("The requested FEM Body does not exist.");
    if(const auto* source=find(analysis->fem->source_id))
        return source->suppressed?FemSourceState::Suppressed:FemSourceState::Available;
    if(const auto* source=find_boolean(analysis->fem->source_id))
        return source->suppressed?FemSourceState::Suppressed:FemSourceState::Available;
    return FemSourceState::Missing;
}

std::string BodyHistoryGraph::create_render(BodyHistory body) {
    if(!body.render)throw std::invalid_argument("Invalid Render definition.");
    for(const auto& id:body.render->source_ids)if(!find(id)&&!find_boolean(id))
        throw std::invalid_argument("Select at least one Render source Body.");
    if(body.scope.id.empty())body.scope.id=zima::kernel::make_stable_id();
    const auto id=body.scope.id;auto next=*this;
    next.order_.insert(next.order_.begin()+static_cast<std::ptrdiff_t>(cursor_),id);
    next.bodies_.push_back(std::move(body));next.sort_bodies();++next.cursor_;next.active_=id;
    next.validate();*this=std::move(next);return id;
}

RenderSourceState BodyHistoryGraph::render_source_state(const std::string& body_id) const {
    const auto* scene=find(body_id);
    if(!scene||!scene->render)throw std::invalid_argument("The requested Render Body does not exist.");
    const auto sources=render_sources(body_id);if(sources.empty())return RenderSourceState::Missing;
    for(const auto& id:sources) {
        if(const auto* source=find(id)){if(source->suppressed)return RenderSourceState::Suppressed;}
        else if(const auto* source=find_boolean(id)){if(source->suppressed)return RenderSourceState::Suppressed;}
        else return RenderSourceState::Missing;
    }
    return RenderSourceState::Available;
}

std::vector<std::string> BodyHistoryGraph::render_sources(const std::string& body_id) const {
    const auto* scene=find(body_id);
    if(!scene||!scene->render)throw std::invalid_argument("The requested Render Body does not exist.");
    if(!scene->render->visible_bodies)return scene->render->source_ids;
    const auto boundary=std::ranges::find(order_,body_id)-order_.begin();
    auto sources=available_before(static_cast<std::size_t>(boundary));
    std::erase_if(sources,[&](const auto& id){
        if(const auto* body=find(id))return !body->visible||body->suppressed;
        const auto* boolean=find_boolean(id);return !boolean||!boolean->visible||boolean->suppressed;
    });
    return sources;
}

std::string BodyHistoryGraph::create_cam(BodyHistory body) {
    if(!body.cam)throw std::invalid_argument("Invalid CAM definition.");
    if(body.scope.id.empty())body.scope.id=zima::kernel::make_stable_id();
    const auto id=body.scope.id;auto next=*this;
    next.order_.insert(next.order_.begin()+static_cast<std::ptrdiff_t>(cursor_),id);
    next.bodies_.push_back(std::move(body));next.sort_bodies();++next.cursor_;next.active_=id;
    next.validate();
    if(next.cam_source_state(id)!=CamSourceState::Available)
        throw std::invalid_argument("The CAM source is missing or suppressed.");
    *this=std::move(next);return id;
}
std::vector<std::string> BodyHistoryGraph::cam_sources(const std::string& body_id) const {
    const auto* setup=find(body_id);
    if(!setup||!setup->cam)throw std::invalid_argument("The requested CAM Body does not exist.");
    std::vector<std::string> result;
    const auto add=[&](const auto& id){if(std::ranges::find(result,id)==result.end())result.push_back(id);};
    add(setup->cam->stock_id);add(setup->cam->target_id);
    for(const auto& id:setup->cam->fixture_ids)add(id);return result;
}
CamSourceState BodyHistoryGraph::cam_source_state(const std::string& body_id) const {
    for(const auto& id:cam_sources(body_id)) {
        if(const auto* source=find(id)){if(source->suppressed)return CamSourceState::Suppressed;}
        else if(const auto* source=find_boolean(id)){if(source->suppressed)return CamSourceState::Suppressed;}
        else return CamSourceState::Missing;
    }
    return CamSourceState::Available;
}

void BodyHistoryGraph::activate(const std::string& id) {
    if (!id.empty() && !find(id)) throw std::invalid_argument("Body does not exist");
    if(!id.empty()&&find(id)->suppressed)throw std::invalid_argument("Restore the Body before activating it.");
    if(!id.empty()&&find(id)->derived_copy)throw std::invalid_argument("Geometrie kopie se upravuje u zdroje.");
    if(!id.empty()&&find(id)->scale)throw std::invalid_argument("A scaled Body cannot be activated for modeling.");
    if(!id.empty()&&find(id)->link)throw std::invalid_argument("A linked Body is edited in its source Part.");
    active_ = id;
}

void BodyHistoryGraph::set_insertion_cursor(std::size_t cursor) {
    if (cursor > order_.size()) throw std::invalid_argument("Body insertion cursor is outside history");
    cursor_ = cursor;
}

void BodyHistoryGraph::set_history_cursor(const std::string& body_id, std::size_t cursor) {
    const auto* source = find(body_id);
    if (!source) throw std::invalid_argument("Body does not exist");
    auto body = *source;
    body.cursor = cursor;
    update_body(std::move(body));
}

void BodyHistoryGraph::insert(PartHistoryEntry entry) {
    const auto* source = find(active_);
    if (!source) throw std::invalid_argument("Activate a body before inserting a feature");
    auto body = *source;
    body.entries.insert(body.entries.begin() + static_cast<std::ptrdiff_t>(body.cursor), std::move(entry));
    ++body.cursor;
    update_body(std::move(body));
}

void BodyHistoryGraph::move_body(const std::string& id, std::size_t destination) {
    if (!find(id)) throw std::invalid_argument("Body does not exist");
    move_step(id, destination);
}

void BodyHistoryGraph::move_step(const std::string& id, std::size_t destination) {
    if (destination >= order_.size()) throw std::invalid_argument("Destination is outside history");
    auto next = *this;
    const auto found = std::ranges::find(next.order_, id);
    if (found == next.order_.end()) throw std::invalid_argument("History step does not exist");
    const auto source = static_cast<std::size_t>(std::distance(next.order_.begin(), found));
    if (source == destination) return;
    next.order_.erase(found);
    next.order_.insert(next.order_.begin() + static_cast<std::ptrdiff_t>(destination), id);
    if (source < next.cursor_) --next.cursor_;
    if (destination < next.cursor_) ++next.cursor_;
    if (cursor_ == order_.size()) next.cursor_ = next.order_.size();
    next.sort_bodies();
    next.validate();
    *this = std::move(next);
}

BodyHistoryBoundary BodyHistoryGraph::rollback_before(const std::string& entry_id) const {
    const auto* body = owner(entry_id);
    if (!body) throw std::invalid_argument("History entry has no body owner");
    const auto found = std::ranges::find_if(body->entries, [&](const auto& entry) { return entry.id == entry_id; });
    return {body->scope.id, static_cast<std::size_t>(std::distance(body->entries.begin(), found))};
}

std::string BodyHistoryGraph::copy_target_before(const std::string& source,std::size_t boundary) const {
    const auto* source_owner=owner(source);
    if(!source_owner)return source;
    auto target=source_owner->scope.id;
    for(std::size_t i=0;i<boundary;++i) {
        const auto* body=find(order_.at(i));
        if(body&&body->derived_copy&&body->derived_copy->subtract_source&&
                body->derived_copy->source_ids.size()==1&&
                owner(body->derived_copy->source_ids.front())==source_owner)
            target=body->scope.id;
    }
    return target;
}

std::vector<std::string> BodyHistoryGraph::available_before(std::size_t boundary) const {
    if (boundary > order_.size()) throw std::invalid_argument("Boundary is outside body history");
    std::set<std::string> available;
    for (std::size_t index = 0; index < boundary; ++index) {
        const auto& id = order_[index];
        const auto* body=find(id);
        if(body&&body->kind()!=BodyKind::Material)continue;
        if (const auto* operation = find_boolean(id)) {
            available.erase(operation->target_id); available.erase(operation->tool_id);
        }
        if(body&&body->derived_copy&&body->derived_copy->subtract_source)
            available.erase(copy_target_before(
                primary_derived_copy_source(*body->derived_copy),index));
        available.insert(id);
    }
    std::vector<std::string> result;
    for (const auto& id : order_) if (available.contains(id)) result.push_back(id);
    return result;
}

std::vector<std::string> BodyHistoryGraph::visible_context() const {
    if(const auto* body=find(active_);body&&body->cam) {
        auto sources=cam_sources(active_);
        std::erase_if(sources,[&](const auto& id) {
            if(const auto* source=find(id))return source->suppressed;
            const auto* source=find_boolean(id);return !source||source->suppressed;
        });
        return sources;
    }
    if(const auto* body=find(active_);body&&body->render) {
        if(render_source_state(active_)!=RenderSourceState::Available)return {};
        return render_sources(active_);
    }
    if(const auto* body=find(active_);body&&body->fem) {
        if(fem_source_state(active_)!=FemSourceState::Available)return {};
        return {body->fem->source_id};
    }
    const auto available = available_before(order_.size());
    const bool editing_consumed = !active_.empty() &&
        std::ranges::find(available, active_) == available.end();
    std::set<std::string> editing_branch;
    if (editing_consumed) {
        editing_branch.insert(active_);
        for (std::size_t index = 0; index < order_.size(); ++index) {
            const auto& id = order_[index];
            if (const auto* op = find_boolean(id)) {
                if (editing_branch.contains(op->target_id) ||
                    editing_branch.contains(op->tool_id))
                    editing_branch.insert(id);
            } else if (const auto* body = find(id)) {
                if (body->scale && editing_branch.contains(body->scale->source_id))
                    editing_branch.insert(id);
                if (body->derived_copy) {
                    const auto& copy = *body->derived_copy;
                    const bool input_in_branch = std::ranges::any_of(copy.source_ids, [&](const auto& source) {
                        const auto* source_body = owner(source);
                        return editing_branch.contains(source_body ? source_body->scope.id : source);
                    });
                    if (input_in_branch || (copy.subtract_source && editing_branch.contains(
                            copy_target_before(primary_derived_copy_source(copy), index))))
                        editing_branch.insert(id);
                }
            }
        }
    }
    // A consumed active Body uses its local editing boundary. Its downstream
    // results give way to that input while independent results remain visible.
    std::vector<std::string> result;
    auto available_id = available.begin();
    for (const auto& id : order_) {
        const bool is_available = available_id != available.end() && *available_id == id;
        if (is_available) ++available_id;
        if (editing_consumed && id == active_) {
            result.push_back(id);
        } else if (is_available && !editing_branch.contains(id)) {
            const auto* body = find(id);
            if (body ? body->visible && !body->suppressed :
                       find_boolean(id)->visible && !find_boolean(id)->suppressed)
                result.push_back(id);
        }
    }
    return result;
}

std::vector<zima::kernel::HistoryOperation> BodyHistoryGraph::compile(const CompileEntry& compiler) const {
    validate();
    std::vector<zima::kernel::HistoryOperation> result;
    std::set<std::string> calculated;
    for (const auto& id : order_) {
        if (const auto* boolean = find_boolean(id)) {
            if (!calculated.contains(boolean->target_id) || !calculated.contains(boolean->tool_id))
                throw std::invalid_argument("Boolean requires two body geometry histories");
            zima::kernel::HistoryOperation operation;
            operation.owner_id = id;
            operation.body.id = id;
            operation.body.combination = boolean->operation;
            operation.body.target_id = boolean->target_id;
            operation.body.source_id = boolean->tool_id;
            operation.body.result_suppressed = boolean->suppressed;
            result.push_back(std::move(operation));
            calculated.insert(id);
            continue;
        }
        const auto& body = *find(id);
        if(body.kind()!=BodyKind::Material)continue;
        if(body.link) {
            zima::kernel::HistoryOperation operation;operation.owner_id=id;operation.body.id=id;
            operation.body.result_suppressed=body.suppressed;operation.body.linked_body=body.link->calculated;
            operation.body.translation=body.scope.translation();operation.body.rotation_degrees=body.scope.rotation_degrees();
            result.push_back(std::move(operation));calculated.insert(id);continue;
        }
        if(body.scale) {
            if(!calculated.contains(body.scale->source_id))throw std::invalid_argument("The scale source has no calculated Body.");
            zima::kernel::HistoryOperation operation;operation.owner_id=id;operation.body.id=id;
            operation.body.result_suppressed=body.suppressed;
            operation.body.combination=zima::kernel::BodyCombination::Scale;
            operation.body.source_id=body.scale->source_id;operation.body.scale_factor=body.scale->factor;
            operation.body.scale_center=body.scale->center;result.push_back(std::move(operation));calculated.insert(id);continue;
        }
        if(body.derived_copy) {
            if(!body.derived_copy->reference_valid||
                    std::ranges::any_of(body.derived_copy->source_ids,
                        [&](const auto& source) {
                            const auto* source_owner=owner(source);
                            return !calculated.contains(source_owner?
                                source_owner->scope.id:source);
                        }))
                throw std::invalid_argument("Kopie nemá platný zdroj nebo referenci.");
            zima::kernel::HistoryOperation operation;operation.owner_id=id;operation.body.id=id;
            operation.body.result_suppressed=body.suppressed;
            operation.body.combination=zima::kernel::BodyCombination::Mirror;
            for(const auto& source:body.derived_copy->source_ids) {
                const auto* source_owner=owner(source);
                operation.body.copy_sources.push_back({
                    source_owner?source_owner->scope.id:source,
                    source_owner?source:std::string{}});
            }
            operation.body.source_id=operation.body.copy_sources.front().body_id;
            if(operation.body.copy_sources.size()==1)
                operation.body.source_feature_id=
                    operation.body.copy_sources.front().feature_id;
            if(body.derived_copy->subtract_source)operation.body.target_id=copy_target_before(primary_derived_copy_source(*body.derived_copy),
                static_cast<std::size_t>(std::ranges::find(order_,id)-order_.begin()));
            operation.body.mirror_plane=body.derived_copy->resolved_plane;
            if(body.derived_copy->pattern){operation.body.combination=zima::kernel::BodyCombination::Pattern;operation.body.pattern=*body.derived_copy->pattern;}
            result.push_back(std::move(operation));calculated.insert(id);continue;
        }
        const auto start = result.size();
        std::set<std::string> preceding_features;
        for (const auto& entry : body.entries) {
            if (auto operation = compiler(entry)) {
                if (!(operation->body == zima::kernel::BodyHistoryScope{}))
                    throw std::invalid_argument("Feature compiler must return a local operation");
                if(operation->feature_copy) {
                    const auto& copy=*operation->feature_copy;
                    const bool missing=copy.copy_sources.empty()?
                        !preceding_features.contains(copy.source_feature_id):
                        std::ranges::any_of(copy.copy_sources,[&](const auto& source) {
                            return source.feature_id.empty()||
                                !preceding_features.contains(source.feature_id);
                        });
                    if(missing)throw std::invalid_argument(
                        "Feature copy requires preceding sources in the same Body.");
                }
                preceding_features.insert(operation->owner_id);
                operation->body.id = id;
                operation->body.result_suppressed = body.suppressed;
                operation->body.translation = body.scope.translation();
                operation->body.rotation_degrees = body.scope.rotation_degrees();
                result.push_back(std::move(*operation));
            }
        }
        if (result.size() != start) calculated.insert(id);
    }
    return result;
}

std::string BodyHistoryGraph::serialized() const {
    validate();
    auto bodies = nlohmann::json::array();
    for (const auto& body : bodies_) {
        if(body.cam) {
            bodies.push_back({{"kind","cam"},{"id",body.scope.id},{"name",body.name},
                {"visible",body.visible},{"suppressed",body.suppressed},{"definition",*body.cam}});
            continue;
        }
        if(body.render) {
            bodies.push_back({{"kind","render"},{"id",body.scope.id},{"name",body.name},
                {"visible",body.visible},{"suppressed",body.suppressed},{"definition",*body.render}});
            continue;
        }
        if(body.fem) {
            bodies.push_back({{"kind","fem"},{"id",body.scope.id},{"name",body.name},
                {"visible",body.visible},{"suppressed",body.suppressed},{"definition",*body.fem}});
            continue;
        }
        auto entries = nlohmann::json::array();
        for (const auto& entry : body.entries) entries.push_back({{"kind", static_cast<int>(entry.kind)}, {"id", entry.id}});
        const auto& scope = body.scope;
        bodies.push_back({{"id", scope.id}, {"name", body.name}, {"visible", body.visible}, {"suppressed",body.suppressed},
            {"link",body.link?nlohmann::json{{"document",body.link->document_id},{"body",body.link->body_id},
                {"path",path_to_utf8(body.link->source_path)},{"calculated",serialize_body_result(*body.link->calculated)}}:nlohmann::json(nullptr)},
            {"scale",body.scale?nlohmann::json{{"source",body.scale->source_id},{"factor",body.scale->factor},
                {"center",{body.scale->center.x,body.scale->center.y,body.scale->center.z}}}:nlohmann::json(nullptr)},
            {"placement", scope.placement},{"derived_copy",body.derived_copy?nlohmann::json(*body.derived_copy):nlohmann::json(nullptr)},
            {"entries", std::move(entries)}, {"cursor", body.cursor}, {"dependencies", body.dependencies}});
    }
    auto booleans = nlohmann::json::array();
    for (const auto& op : booleans_)
        booleans.push_back({{"id", op.id}, {"name", op.name}, {"operation", static_cast<int>(op.operation)},
            {"target", op.target_id}, {"tool", op.tool_id}, {"visible", op.visible}, {"suppressed",op.suppressed}});
    return nlohmann::json{{"bodies", std::move(bodies)}, {"booleans", std::move(booleans)},
        {"order", order_}, {"active", active_}, {"cursor", cursor_}}.dump();
}

BodyHistoryGraph BodyHistoryGraph::from_serialized(std::string_view source) {
    const auto root = nlohmann::json::parse(source);
    BodyHistoryGraph graph;
    graph.active_ = root.at("active").get<std::string>();
    graph.cursor_ = root.at("cursor").get<std::size_t>();
    for (const auto& row : root.at("bodies")) {
        BodyHistory body;
        body.scope.id = row.at("id").get<std::string>();
        body.name = row.at("name").get<std::string>();
        body.visible = row.at("visible").get<bool>();
        body.suppressed = row.at("suppressed").get<bool>();
        if(row.contains("kind")) {
            if(row.at("kind")=="fem")body.fem=row.at("definition").get<FemDefinition>();
            else if(row.at("kind")=="render")body.render=row.at("definition").get<RenderDefinition>();
            else if(row.at("kind")=="cam")body.cam=row.at("definition").get<CamDefinition>();
            else throw std::invalid_argument("Unsupported Body kind.");
            graph.bodies_.push_back(std::move(body));
            continue;
        }
        if(!row.at("link").is_null()) {
            const auto& link=row.at("link");body.link=BodyLink{link.at("document").get<std::string>(),
                link.at("body").get<std::string>(),std::filesystem::u8path(link.at("path").get<std::string>()),
                std::make_shared<const zima::kernel::BodyResult>(load_body_result(link.at("calculated")))};
        }
        if(!row.at("scale").is_null()) {
            const auto& scale=row.at("scale");const auto& center=scale.at("center");
            body.scale=BodyScale{scale.at("source").get<std::string>(),scale.at("factor").get<double>(),
                {center.at(0).get<double>(),center.at(1).get<double>(),center.at(2).get<double>()}};
        }
        body.scope.placement = row.at("placement").get<Placement>();
        if(!row.at("derived_copy").is_null())body.derived_copy=row.at("derived_copy").get<DerivedCopyParameters>();
        body.cursor = row.at("cursor").get<std::size_t>();
        body.dependencies = row.at("dependencies").get<std::vector<std::string>>();
        for (const auto& entry : row.at("entries"))
            body.entries.push_back({static_cast<PartHistoryKind>(entry.at("kind").get<int>()), entry.at("id").get<std::string>()});
        graph.bodies_.push_back(std::move(body));
    }
    graph.order_ = root.at("order").get<std::vector<std::string>>();
    for (const auto& row : root.at("booleans"))
        graph.booleans_.push_back({row.at("id").get<std::string>(), row.at("name").get<std::string>(),
            static_cast<zima::kernel::BodyCombination>(row.at("operation").get<int>()),
            row.at("target").get<std::string>(), row.at("tool").get<std::string>(), row.at("visible").get<bool>(),row.at("suppressed").get<bool>()});
    graph.sort_bodies();
    graph.validate();
    return graph;
}

} // namespace zima::document
