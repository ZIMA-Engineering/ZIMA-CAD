#include <zima/document/document_session.hpp>

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace zima::document {
namespace {

void retain_shaft_reference_geometry(PartDocument& document,
        const std::vector<zima::kernel::BodyResult>& boundaries) {
    if (boundaries.empty()) return;
    for (auto& feature : document.history) {
        if (feature.feature_kind!=FeatureKind::ShaftThread) continue;
        const auto snapshot=std::ranges::find_if(boundaries,[&](const auto& body) {
            return body.shaft_thread_owner==feature.id;
        });
        auto& p=feature.shaft_thread;
        const std::array<zima::kernel::FaceReference*,4> refs{
            &p.cylinder,&p.start,p.chamfer ? &*p.chamfer : nullptr,p.end ? &*p.end : nullptr};
        for (std::size_t i=0;i<refs.size();++i) {
            if (!refs[i]) continue;
            if (snapshot!=boundaries.end() && snapshot->shaft_thread_references[i]==*refs[i] &&
                snapshot->shaft_thread_references[i].surface) {
                refs[i]->surface=snapshot->shaft_thread_references[i].surface;
            } else {
                const auto& geometry=boundaries.back().mesh.original_references;
                const auto source=std::ranges::find_if(geometry.triangle_references,[&](const auto& ref) {
                    return ref==*refs[i] && ref.surface;
                });
                if (source!=geometry.triangle_references.end()) refs[i]->surface=source->surface;
            }
        }
    }
}

zima::kernel::ViewerReferenceGeometry references_for_owners(
    const zima::kernel::ViewerReferenceGeometry& source,
    const std::unordered_set<std::string>& owners) {
    zima::kernel::ViewerReferenceGeometry result;
    for (std::size_t triangle = 0;
         triangle < source.triangle_references.size(); ++triangle) {
        const auto& reference = source.triangle_references[triangle];
        if (!owners.contains(reference.owner_id)) continue;
        const auto offset = static_cast<std::uint32_t>(result.vertices.size());
        for (int corner = 0; corner < 3; ++corner) {
            result.vertices.push_back(source.vertices.at(
                source.triangles.at(triangle * 3 + corner)));
            result.triangles.push_back(offset + corner);
        }
        result.triangle_references.push_back(reference);
    }
    std::ranges::copy_if(source.edges, std::back_inserter(result.edges),
        [&](const auto& value) { return owners.contains(value.reference.owner_id); });
    std::ranges::copy_if(source.points, std::back_inserter(result.points),
        [&](const auto& value) { return owners.contains(value.reference.owner_id); });
    std::ranges::copy_if(source.axes, std::back_inserter(result.axes),
        [&](const auto& value) { return owners.contains(value.reference.owner_id); });
    return result;
}

}  // namespace

DocumentSession::DocumentSession(
    PartDocument document,
    std::vector<zima::kernel::BodyResult> calculated_boundaries)
    : current_{std::move(document), std::move(calculated_boundaries), 0, false} {
    retain_shaft_reference_geometry(current_.document,current_.calculated_boundaries);
    current_.document.synchronize_dimension_identifiers();
    saved_dimension_allocations_ = current_.document.dimension_identifiers.allocation_count();
}

const PartDocument& DocumentSession::document() const { return current_.document; }
std::uint64_t DocumentSession::revision() const { return current_.revision; }
bool DocumentSession::is_dirty() const {
    return current_.revision != saved_revision_ || current_.calculated_state_dirty ||
        current_.document.dimension_identifiers.allocation_count() != saved_dimension_allocations_;
}
bool DocumentSession::can_undo() const { return !undo_.empty(); }
bool DocumentSession::can_redo() const { return !redo_.empty(); }
const std::vector<zima::kernel::BodyResult>&
DocumentSession::calculated_boundaries() const {
    return current_.calculated_boundaries;
}

std::optional<zima::kernel::BodyResult> DocumentSession::calculated_boundary(
    const std::size_t operation_count) const {
    if (operation_count == 0 ||
        current_.calculated_boundaries.size() < operation_count) {
        return std::nullopt;
    }
    auto result = current_.calculated_boundaries[operation_count - 1];
    const auto operations = current_.document.kernel_operations();
    std::unordered_set<std::string> owners;
    for (std::size_t operation = 0;
         operation < operation_count && operation < operations.size();
         ++operation) {
        if (!operations[operation].suppressed) {
            owners.insert(operations[operation].owner_id);
        }
    }
    result.mesh.original_references = references_for_owners(
        current_.calculated_boundaries.back().mesh.original_references, owners);
    // Feature axes are persisted reference geometry, but they are also part
    // of the ordinary Part presentation. A loaded or fully reused calculated
    // boundary can legitimately contain them only in original_references;
    // publish them into the display packet without invoking OCCT.
    for (const auto& axis : result.mesh.original_references.axes) {
        if (axis.reference.semantic_key != "axis:primary" &&
            !axis.reference.semantic_key.starts_with("axis:profile:")) {
            continue;
        }
        const bool already_visible = std::ranges::any_of(result.mesh.axes,
            [&](const auto& visible) {
                return visible.reference == axis.reference;
            });
        if (!already_visible) result.mesh.axes.push_back(axis);
    }
    return result;
}

zima::kernel::ViewerMesh DocumentSession::body_context_mesh(const BodyHistoryGraph* context) const {
    zima::kernel::ViewerMesh result;
    if (current_.calculated_boundaries.empty()) return result;
    const auto& document = current_.document;
    const auto& graph = context ? *context : document.body_history;
    const auto& outputs = current_.calculated_boundaries.back().body_outputs;
    for (const auto& id : graph.visible_context()) {
        zima::kernel::ViewerMesh mesh;
        if (id == graph.active_body_id()) {
            const auto& body = *graph.find(id);
            std::size_t count{};
            for (std::size_t index = 0; index < body.cursor; ++index) {
                const auto& entry = body.entries[index];
                const auto* feature = entry.kind == PartHistoryKind::Feature ? document.find_container(entry.id) : nullptr;
                if (feature && feature->feature_kind != FeatureKind::Sketch) ++count;
            }
            const auto local = calculated_body_boundary(id, count);
            if (!local) continue;
            mesh = document.place_body_mesh(local->mesh, id);
        } else {
            const auto found = outputs.find(id);
            if (found == outputs.end()) continue;
            mesh = found->second.mesh;
        }
        const auto offset = static_cast<std::uint32_t>(result.vertices.size());
        result.vertices.insert(result.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        for (const auto index : mesh.triangles) result.triangles.push_back(offset + index);
        const auto append = [](auto& target, const auto& source) { target.insert(target.end(), source.begin(), source.end()); };
        append(result.triangle_references, mesh.triangle_references);
        append(result.edges, mesh.edges); append(result.points, mesh.points); append(result.axes, mesh.axes);
        append(result.dimensions, mesh.dimensions); append(result.constraint_markers, mesh.constraint_markers);
        auto& refs = result.original_references;
        const auto ref_offset = static_cast<std::uint32_t>(refs.vertices.size());
        append(refs.vertices, mesh.original_references.vertices);
        for (const auto index : mesh.original_references.triangles) refs.triangles.push_back(ref_offset + index);
        append(refs.triangle_references, mesh.original_references.triangle_references);
        append(refs.edges, mesh.original_references.edges); append(refs.points, mesh.original_references.points);
        append(refs.axes, mesh.original_references.axes);
    }
    return result;
}

std::optional<BooleanEditInputs> DocumentSession::boolean_edit_inputs(const std::string& id) const {
    const auto* operation = current_.document.body_history.find_boolean(id);
    if (!operation || current_.calculated_boundaries.empty()) return std::nullopt;
    const auto& outputs = current_.calculated_boundaries.back().body_outputs;
    const auto target = outputs.find(operation->target_id);
    const auto tool = outputs.find(operation->tool_id);
    if (target == outputs.end() || tool == outputs.end()) return std::nullopt;
    return BooleanEditInputs{operation->target_id, operation->tool_id, target->second, tool->second};
}

std::optional<zima::kernel::BodyResult> DocumentSession::calculated_body_boundary(
    const std::string& body_id, const std::size_t operation_count) const {
    if (operation_count == 0 || current_.calculated_boundaries.empty()) return std::nullopt;
    const auto& caches = current_.calculated_boundaries.back().body_boundaries;
    const auto found = caches.find(body_id);
    if (found == caches.end() || found->second.size() < operation_count) return std::nullopt;
    auto result = found->second[operation_count - 1];
    std::unordered_set<std::string> owners;
    std::size_t count{};
    const auto* body = current_.document.body_history.find(body_id);
    if (!body) return std::nullopt;
    for (const auto& entry : body->entries) {
        if (entry.kind != PartHistoryKind::Feature) continue;
        const auto* feature = current_.document.find_container(entry.id);
        if (!feature || feature->feature_kind == FeatureKind::Sketch) continue;
        if (count++ == operation_count) break;
        if (!feature->suppressed) owners.insert(feature->id);
    }
    result.mesh.original_references = references_for_owners(
        found->second.back().mesh.original_references, owners);
    return result;
}

std::optional<HistoryRollbackBoundary> DocumentSession::rollback_boundary(
    const std::string& container_id) const {
    const auto index = current_.document.history_index(container_id);
    if (!index) return std::nullopt;
    if (!current_.document.body_history.bodies().empty()) {
        const auto boundary = current_.document.body_history.rollback_before(container_id);
        const auto* body = current_.document.body_history.find(boundary.body_id);
        std::size_t operations{};
        for (std::size_t entry = 0; entry < boundary.entry_count; ++entry) {
            if (body->entries[entry].kind != PartHistoryKind::Feature) continue;
            const auto* container = current_.document.find_container(body->entries[entry].id);
            if (container && container->feature_kind != FeatureKind::Sketch) ++operations;
        }
        return HistoryRollbackBoundary{*index, calculated_body_boundary(boundary.body_id, operations)};
    }
    // Sketch containers occupy real history positions but do not produce an
    // OCCT boundary.  The input boundary index therefore counts only body
    // operations preceding the edited container, while history_index keeps
    // the actual Tree/history position used to suppress downstream items.
    std::size_t calculated_before{};
    if (current_.document.history_order.empty()) {
        for (std::size_t history_index = 0; history_index < *index;
             ++history_index) {
            if (current_.document.history[history_index].feature_kind !=
                    FeatureKind::Sketch) {
                ++calculated_before;
            }
        }
    } else {
        const auto ordered = std::find_if(
            current_.document.history_order.begin(),
            current_.document.history_order.end(), [&](const auto& entry) {
                return entry.kind == PartHistoryKind::Feature &&
                    entry.id == container_id;
            });
        if (ordered == current_.document.history_order.end()) {
            return std::nullopt;
        }
        for (auto entry = current_.document.history_order.begin();
             entry != ordered; ++entry) {
            if (entry->kind != PartHistoryKind::Feature) continue;
            const auto* container =
                current_.document.find_container(entry->id);
            if (container != nullptr &&
                container->feature_kind != FeatureKind::Sketch) {
                ++calculated_before;
            }
        }
    }
    if (current_.calculated_boundaries.size() < calculated_before) {
        return std::nullopt;
    }
    HistoryRollbackBoundary result{*index, std::nullopt};
    if (calculated_before > 0) {
        result.input_body = calculated_boundary(calculated_before);
    }
    return result;
}

void DocumentSession::replace(
    PartDocument document,
    std::vector<zima::kernel::BodyResult> calculated_boundaries) {
    retain_shaft_reference_geometry(document,calculated_boundaries);
    current_ = {std::move(document), std::move(calculated_boundaries), 0, false};
    undo_.clear();
    redo_.clear();
    next_revision_ = 1;
    saved_revision_ = 0;
    current_.document.synchronize_dimension_identifiers();
    saved_dimension_allocations_ = current_.document.dimension_identifiers.allocation_count();
}

void DocumentSession::commit(
    PartDocument document,
    std::vector<zima::kernel::BodyResult> calculated_boundaries) {
    retain_shaft_reference_geometry(document,calculated_boundaries);
    document.dimension_identifiers.retain(current_.document.dimension_identifiers);
    document.synchronize_dimension_identifiers();
    undo_.push_back(std::move(current_));
    current_ = {
        std::move(document), std::move(calculated_boundaries), next_revision_++, false};
    redo_.clear();
}

void DocumentSession::update_calculated_boundaries(
    std::vector<zima::kernel::BodyResult> calculated_boundaries) {
    retain_shaft_reference_geometry(current_.document,calculated_boundaries);
    current_.calculated_boundaries = std::move(calculated_boundaries);
    current_.calculated_state_dirty = true;
}

bool DocumentSession::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(std::move(current_));
    current_ = std::move(undo_.back());
    undo_.pop_back();
    current_.document.dimension_identifiers.retain(redo_.back().document.dimension_identifiers);
    return true;
}

bool DocumentSession::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(std::move(current_));
    current_ = std::move(redo_.back());
    redo_.pop_back();
    current_.document.dimension_identifiers.retain(undo_.back().document.dimension_identifiers);
    return true;
}

void DocumentSession::mark_saved() {
    saved_revision_ = current_.revision;
    saved_dimension_allocations_ = current_.document.dimension_identifiers.allocation_count();
    current_.calculated_state_dirty = false;
}

}  // namespace zima::document
