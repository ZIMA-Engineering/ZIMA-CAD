#include <zima/assembly/assembly_session.hpp>

#include <utility>

namespace zima::assembly {

AssemblySession::AssemblySession(AssemblyDocument document)
    : current_{std::move(document), 0, false} {}

const AssemblyDocument& AssemblySession::document() const { return current_.document; }
std::uint64_t AssemblySession::revision() const { return current_.revision; }
bool AssemblySession::is_dirty() const {
    return current_.revision != saved_revision_ || current_.dependency_state_dirty;
}
bool AssemblySession::can_undo() const { return !undo_.empty(); }
bool AssemblySession::can_redo() const { return !redo_.empty(); }

void AssemblySession::replace(AssemblyDocument document) {
    current_ = {std::move(document), 0, false};
    undo_.clear();
    redo_.clear();
    next_revision_ = 1;
    saved_revision_ = 0;
}

void AssemblySession::commit(AssemblyDocument document) {
    undo_.push_back(std::move(current_));
    current_ = {std::move(document), next_revision_++, false};
    redo_.clear();
}

void AssemblySession::update_dependency_snapshots(AssemblyDocument document) {
    current_.document = std::move(document);
    current_.dependency_state_dirty = true;
}

bool AssemblySession::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(std::move(current_));
    current_ = std::move(undo_.back());
    undo_.pop_back();
    return true;
}

bool AssemblySession::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(std::move(current_));
    current_ = std::move(redo_.back());
    redo_.pop_back();
    return true;
}

void AssemblySession::mark_saved() {
    saved_revision_ = current_.revision;
    current_.dependency_state_dirty = false;
}

}  // namespace zima::assembly
