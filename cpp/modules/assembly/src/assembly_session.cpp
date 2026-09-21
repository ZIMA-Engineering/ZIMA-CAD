#include <zima/assembly/assembly_session.hpp>
#include <zima/assembly/file_relocation.hpp>
#include <zima/assembly/physical_properties.hpp>
#include <type_traits>
#include <utility>

namespace zima::assembly {
void AssemblySession::rebase_native_files(std::span<const document::FileRelocation> files,
        const std::filesystem::path& owning_file) {
    document::FileRelocationEdits edits(files);
    prepare_native_file_rebase(edits, owning_file);
    edits.apply();
}
void AssemblySession::prepare_native_file_rebase(document::FileRelocationEdits& edits,
        const std::filesystem::path& owning_file) {
    const auto before = edits.edit_count();
    const auto collect = [&](AssemblyDocument& value) {
        collect_file_relocation_edits(value, edits, owning_file);
    };
    collect(current_->document);
    for (auto& state : undo_) collect(state->document);
    for (auto& state : redo_) collect(state->document);
    if (edits.edit_count() != before) edits.track_generation(data_generation_);
}

AssemblySession::AssemblySession(AssemblyDocument document)
    : current_(std::make_unique<State>(State{std::move(document),0,false})) {
    zima::document::refresh_physical_relations(current_->document,physical_values(current_->document));
    current_->document.synchronize_dimension_identifiers();
    saved_dimension_allocations_=current_->document.dimension_identifiers.allocation_count();
}
AssemblySession::States AssemblySession::copy_states(const States& states) {
    States result;result.reserve(states.size());
    for(const auto& state:states)result.push_back(std::make_unique<State>(*state));
    return result;
}
AssemblySession::AssemblySession(const AssemblySession& other)
    : data_generation_(other.data_generation_),current_(std::make_unique<State>(*other.current_)),
      undo_(copy_states(other.undo_)),redo_(copy_states(other.redo_)),next_revision_(other.next_revision_),
      saved_revision_(other.saved_revision_),saved_dimension_allocations_(other.saved_dimension_allocations_) {}
AssemblySession& AssemblySession::operator=(const AssemblySession& other) {
    if(this!=&other){AssemblySession copy(other);*this=std::move(copy);}return *this;
}
const AssemblyDocument& AssemblySession::document() const {return current_->document;}
std::uint64_t AssemblySession::revision() const {return current_->revision;}
bool AssemblySession::is_dirty() const {
    return current_->revision!=saved_revision_ || current_->dependency_state_dirty ||
        current_->document.dimension_identifiers.allocation_count()!=saved_dimension_allocations_;
}
bool AssemblySession::can_undo() const {return !undo_.empty();}
bool AssemblySession::can_redo() const {return !redo_.empty();}
void AssemblySession::replace(AssemblyDocument document) {
    if (std::ranges::count_if(document.components, [](const auto& value) { return is_skeleton(value); }) > 1)
        throw std::runtime_error("An Assembly can contain only one Skeleton.");
    zima::document::refresh_physical_relations(document,physical_values(document));
    document.synchronize_dimension_identifiers();
    auto next=std::make_unique<State>(State{std::move(document),0,false});
    const auto allocations=next->document.dimension_identifiers.allocation_count();
    current_=std::move(next);undo_.clear();redo_.clear();next_revision_=1;saved_revision_=0;
    saved_dimension_allocations_=allocations;++data_generation_;
}
void AssemblySession::commit(AssemblyDocument document) {
    const auto intercept=commit_interceptor;if(intercept&&intercept(document))return;
    if (std::ranges::count_if(document.components, [](const auto& value) { return is_skeleton(value); }) > 1)
        throw std::runtime_error("An Assembly can contain only one Skeleton.");
    zima::document::refresh_physical_relations(document,physical_values(document));
    document.dimension_identifiers.retain(current_->document.dimension_identifiers);
    document.synchronize_dimension_identifiers();
    // Validation and allocation finish before any live state changes.
    auto next=std::make_unique<State>(State{std::move(document),next_revision_,false});
    undo_.push_back(std::move(current_));current_=std::move(next);++next_revision_;
    redo_.clear();++data_generation_;
}
void AssemblySession::update_dependency_snapshots(AssemblyDocument document) {
    if (std::ranges::count_if(document.components, [](const auto& value) { return is_skeleton(value); }) > 1)
        throw std::runtime_error("An Assembly can contain only one Skeleton.");
    zima::document::refresh_physical_relations(document,physical_values(document));
    document.dimension_identifiers.retain(current_->document.dimension_identifiers);
    document.synchronize_dimension_identifiers();
    static_assert(std::is_nothrow_move_assignable_v<AssemblyDocument>);
    current_->document=std::move(document);current_->dependency_state_dirty=true;++data_generation_;
}
void AssemblySession::update_source_geometry(AssemblyDocument document) {
    // Display-only refresh preserves dirty/history state and does not evaluate
    // physical relations, dimensions, mates or body operations.
    static_assert(std::is_nothrow_move_assignable_v<AssemblyDocument>);
    current_->document=std::move(document);++data_generation_;
}
bool AssemblySession::step(States& from,States& to) {
    if(from.empty())return false;
    static_assert(std::is_nothrow_move_assignable_v<zima::document::DimensionIdentifiers>);
    auto identifiers=from.back()->document.dimension_identifiers;
    identifiers.retain(current_->document.dimension_identifiers);
    to.push_back(std::move(current_));current_=std::move(from.back());
    current_->document.dimension_identifiers=std::move(identifiers);from.pop_back();++data_generation_;return true;
}
bool AssemblySession::undo(){return step(undo_,redo_);}
bool AssemblySession::redo(){return step(redo_,undo_);}
void AssemblySession::update_family_evaluated(zima::document::FamilyDocument value) {
    current_->document.family=std::move(value);current_->dependency_state_dirty=true;++data_generation_;
}
void AssemblySession::mark_saved(){
    saved_revision_=current_->revision;saved_dimension_allocations_=current_->document.dimension_identifiers.allocation_count();
    current_->dependency_state_dirty=false;
}
} // namespace zima::assembly
