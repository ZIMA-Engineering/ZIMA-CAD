#include <zima/assembly/assembly_session.hpp>

#include <utility>

namespace zima::assembly {

AssemblySession::AssemblySession(AssemblyDocument document)
    : current_{std::move(document), 0, false} {
    current_.document.synchronize_dimension_identifiers();
    saved_dimension_allocations_ = current_.document.dimension_identifiers.allocation_count();
}

const AssemblyDocument& AssemblySession::document() const { return current_.document; }
std::uint64_t AssemblySession::revision() const { return current_.revision; }
bool AssemblySession::is_dirty() const {
    return current_.revision != saved_revision_ || current_.dependency_state_dirty ||
        current_.document.dimension_identifiers.allocation_count() != saved_dimension_allocations_;
}
bool AssemblySession::can_undo() const { return !undo_.empty(); }
bool AssemblySession::can_redo() const { return !redo_.empty(); }

void AssemblySession::replace(AssemblyDocument document) {
    current_ = {std::move(document), 0, false};
    undo_.clear();
    redo_.clear();
    next_revision_ = 1;
    saved_revision_ = 0;
    current_.document.synchronize_dimension_identifiers();
    saved_dimension_allocations_ = current_.document.dimension_identifiers.allocation_count();
}

void AssemblySession::commit(AssemblyDocument document) {
    document.dimension_identifiers.retain(current_.document.dimension_identifiers);
    document.synchronize_dimension_identifiers();
    undo_.push_back(std::move(current_));
    current_ = {std::move(document), next_revision_++, false};
    redo_.clear();
}

void AssemblySession::update_dependency_snapshots(AssemblyDocument document) {
    document.dimension_identifiers.retain(current_.document.dimension_identifiers);
    document.synchronize_dimension_identifiers();
    current_.document = std::move(document);
    current_.dependency_state_dirty = true;
}

bool AssemblySession::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(std::move(current_));
    current_ = std::move(undo_.back());
    undo_.pop_back();
    current_.document.dimension_identifiers.retain(redo_.back().document.dimension_identifiers);
    return true;
}

bool AssemblySession::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(std::move(current_));
    current_ = std::move(redo_.back());
    redo_.pop_back();
    current_.document.dimension_identifiers.retain(undo_.back().document.dimension_identifiers);
    return true;
}

void AssemblySession::mark_saved() {
    saved_revision_ = current_.revision;
    saved_dimension_allocations_ = current_.document.dimension_identifiers.allocation_count();
    current_.dependency_state_dirty = false;
}

}  // namespace zima::assembly
