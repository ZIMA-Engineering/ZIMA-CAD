#include <zima/document/document_session.hpp>

#include <utility>

namespace zima::document {

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

std::optional<HistoryRollbackBoundary> DocumentSession::rollback_boundary(
    const std::string& container_id) const {
    const auto index = current_.document.history_index(container_id);
    if (!index || (*index > 0 && current_.calculated_boundaries.size() < *index)) {
        return std::nullopt;
    }
    HistoryRollbackBoundary result{*index, std::nullopt};
    if (*index > 0) {
        result.input_body = current_.calculated_boundaries[*index - 1];
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
