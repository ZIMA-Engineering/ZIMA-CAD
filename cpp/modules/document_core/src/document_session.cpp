#include <zima/document/document_session.hpp>

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace zima::document {
namespace {

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
    : current_{std::move(document), std::move(calculated_boundaries), 0, false} {}

const PartDocument& DocumentSession::document() const { return current_.document; }
std::uint64_t DocumentSession::revision() const { return current_.revision; }
bool DocumentSession::is_dirty() const {
    return current_.revision != saved_revision_ || current_.calculated_state_dirty;
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

std::optional<HistoryRollbackBoundary> DocumentSession::rollback_boundary(
    const std::string& container_id) const {
    const auto index = current_.document.history_index(container_id);
    if (!index) return std::nullopt;
    // Sketch containers occupy real history positions but do not produce an
    // OCCT boundary.  The input boundary index therefore counts only body
    // operations preceding the edited container, while history_index keeps
    // the actual Tree/history position used to suppress downstream items.
    std::size_t calculated_before{};
    for (std::size_t history_index = 0; history_index < *index; ++history_index) {
        if (current_.document.history[history_index].feature_kind !=
                FeatureKind::Sketch) {
            ++calculated_before;
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
    current_ = {std::move(document), std::move(calculated_boundaries), 0, false};
    undo_.clear();
    redo_.clear();
    next_revision_ = 1;
    saved_revision_ = 0;
}

void DocumentSession::commit(
    PartDocument document,
    std::vector<zima::kernel::BodyResult> calculated_boundaries) {
    undo_.push_back(std::move(current_));
    current_ = {
        std::move(document), std::move(calculated_boundaries), next_revision_++, false};
    redo_.clear();
}

void DocumentSession::update_calculated_boundaries(
    std::vector<zima::kernel::BodyResult> calculated_boundaries) {
    current_.calculated_boundaries = std::move(calculated_boundaries);
    current_.calculated_state_dirty = true;
}

bool DocumentSession::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(std::move(current_));
    current_ = std::move(undo_.back());
    undo_.pop_back();
    return true;
}

bool DocumentSession::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(std::move(current_));
    current_ = std::move(redo_.back());
    redo_.pop_back();
    return true;
}

void DocumentSession::mark_saved() {
    saved_revision_ = current_.revision;
    current_.calculated_state_dirty = false;
}

}  // namespace zima::document
