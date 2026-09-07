#include <zima/document/derived_copy_json.hpp>
#include <zima/document/body_history.hpp>
#include <zima/document/placement_json.hpp>
#include <zima/kernel/stable_id.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace zima::document {

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

void BodyHistoryGraph::sort_bodies() {
    std::ranges::sort(bodies_, [&](const auto& a, const auto& b) {
        return std::ranges::find(order_, a.scope.id) < std::ranges::find(order_, b.scope.id);
    });
}

void BodyHistoryGraph::validate() const {
    std::set<std::string> identities, preceding, available, entries;
    for (const auto& body : bodies_)
        if (body.scope.id.empty() || !identities.insert(body.scope.id).second)
            throw std::invalid_argument("Body identity is empty or duplicated");
    for (const auto& op : booleans_)
        if (op.id.empty() || !identities.insert(op.id).second)
            throw std::invalid_argument("Boolean identity is empty or duplicated");
    if (order_.size() != identities.size() || cursor_ > order_.size())
        throw std::invalid_argument("Invalid document body history order or cursor");
    for (const auto& id : order_) {
        if (!identities.contains(id) || preceding.contains(id))
            throw std::invalid_argument("Every body or Boolean must occur once in history");
        if (const auto* body = find(id)) {
            if (body->name.empty() || body->cursor > body->entries.size())
                throw std::invalid_argument("Invalid body name or history cursor");
            if(body->derived_copy) {
                if(!body->entries.empty()||body->cursor||!available.contains(body->derived_copy->source_id))
                    throw std::invalid_argument("Kopie potřebuje předcházející zdroj a nemůže mít vlastní historii.");
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
                if (!preceding.contains(dependency)) throw std::invalid_argument("Body reference must precede its dependent body");
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
        available.insert(id);
    }
    if (!active_.empty() && (!find(active_) || find(active_)->derived_copy)) throw std::invalid_argument("Active body does not exist");
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
    auto next = *this;
    const auto found = std::ranges::find_if(next.bodies_, [&](const auto& value) { return value.scope.id == body.scope.id; });
    if (found == next.bodies_.end()) throw std::invalid_argument("Body does not exist");
    *found = std::move(body);
    next.validate();
    *this = std::move(next);
}

void BodyHistoryGraph::activate(const std::string& id) {
    if (!id.empty() && !find(id)) throw std::invalid_argument("Body does not exist");
    if(!id.empty()&&find(id)->derived_copy)throw std::invalid_argument("Geometrie kopie se upravuje u zdroje.");
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

std::vector<std::string> BodyHistoryGraph::available_before(std::size_t boundary) const {
    if (boundary > order_.size()) throw std::invalid_argument("Boundary is outside body history");
    std::set<std::string> available;
    for (std::size_t index = 0; index < boundary; ++index) {
        const auto& id = order_[index];
        if (const auto* operation = find_boolean(id)) {
            available.erase(operation->target_id); available.erase(operation->tool_id);
        }
        available.insert(id);
    }
    std::vector<std::string> result;
    for (const auto& id : order_) if (available.contains(id)) result.push_back(id);
    return result;
}

std::vector<std::string> BodyHistoryGraph::visible_context() const {
    std::set<std::string> available;
    for (const auto& id : order_) {
        if (const auto* op = find_boolean(id)) {
            available.erase(op->target_id);
            available.erase(op->tool_id);
        }
        available.insert(id);
        if (id == active_) break;
    }
    std::vector<std::string> result;
    for (const auto& id : order_)
        if (available.contains(id) && (id == active_ || (find(id) ? find(id)->visible : find_boolean(id)->visible)))
            result.push_back(id);
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
            result.push_back(std::move(operation));
            calculated.insert(id);
            continue;
        }
        const auto& body = *find(id);
        if(body.derived_copy) {
            if(!calculated.contains(body.derived_copy->source_id)||!body.derived_copy->reference_valid)
                throw std::invalid_argument("Kopie nemá platný zdroj nebo referenci.");
            zima::kernel::HistoryOperation operation;operation.owner_id=id;operation.body.id=id;
            operation.body.combination=zima::kernel::BodyCombination::Mirror;operation.body.source_id=body.derived_copy->source_id;
            operation.body.mirror_plane=body.derived_copy->resolved_plane;
            if(body.derived_copy->pattern){operation.body.combination=zima::kernel::BodyCombination::Pattern;operation.body.pattern=*body.derived_copy->pattern;}
            result.push_back(std::move(operation));calculated.insert(id);continue;
        }
        const auto start = result.size();
        for (const auto& entry : body.entries) {
            if (auto operation = compiler(entry)) {
                if (!(operation->body == zima::kernel::BodyHistoryScope{}))
                    throw std::invalid_argument("Feature compiler must return a local operation");
                operation->body.id = id;
                operation->body.translation = body.scope.translation();
                operation->body.rotation_degrees = body.scope.rotation_degrees();
                result.push_back(std::move(*operation));
            }
        }
        const auto first_active = std::find_if(result.begin() + static_cast<std::ptrdiff_t>(start), result.end(),
            [](const auto& operation) { return !operation.suppressed; });
        if (first_active != result.end() && first_active->operation == zima::kernel::BooleanOperation::Subtract)
            throw std::invalid_argument("The first feature of a body cannot subtract");
        if (result.size() != start) calculated.insert(id);
    }
    return result;
}

std::string BodyHistoryGraph::serialized() const {
    validate();
    auto bodies = nlohmann::json::array();
    for (const auto& body : bodies_) {
        auto entries = nlohmann::json::array();
        for (const auto& entry : body.entries) entries.push_back({{"kind", static_cast<int>(entry.kind)}, {"id", entry.id}});
        const auto& scope = body.scope;
        bodies.push_back({{"id", scope.id}, {"name", body.name}, {"visible", body.visible},
            {"placement", scope.placement},{"derived_copy",body.derived_copy?nlohmann::json(*body.derived_copy):nlohmann::json(nullptr)},
            {"entries", std::move(entries)}, {"cursor", body.cursor}, {"dependencies", body.dependencies}});
    }
    auto booleans = nlohmann::json::array();
    for (const auto& op : booleans_)
        booleans.push_back({{"id", op.id}, {"name", op.name}, {"operation", static_cast<int>(op.operation)},
            {"target", op.target_id}, {"tool", op.tool_id}, {"visible", op.visible}});
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
            row.at("target").get<std::string>(), row.at("tool").get<std::string>(), row.at("visible").get<bool>()});
    graph.sort_bodies();
    graph.validate();
    return graph;
}

} // namespace zima::document
