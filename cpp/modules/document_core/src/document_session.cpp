#include <zima/document/document_session.hpp>

#include <utility>

namespace zima::document {

DocumentSession::DocumentSession(PartDocument document)
    : current_{std::move(document), 0} {}

const PartDocument& DocumentSession::document() const { return current_.document; }
std::uint64_t DocumentSession::revision() const { return current_.revision; }
bool DocumentSession::is_dirty() const { return current_.revision != saved_revision_; }
bool DocumentSession::can_undo() const { return !undo_.empty(); }
bool DocumentSession::can_redo() const { return !redo_.empty(); }

void DocumentSession::replace(PartDocument document) {
    current_ = {std::move(document), 0};
    undo_.clear();
    redo_.clear();
    next_revision_ = 1;
    saved_revision_ = 0;
}

void DocumentSession::commit(PartDocument document) {
    undo_.push_back(std::move(current_));
    current_ = {std::move(document), next_revision_++};
    redo_.clear();
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

void DocumentSession::mark_saved() { saved_revision_ = current_.revision; }

}  // namespace zima::document
