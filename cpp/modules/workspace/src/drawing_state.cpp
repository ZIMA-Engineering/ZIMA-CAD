#include <zima/workspace/workspace.hpp>
#include <type_traits>
namespace zima::workspace {
DrawingState::DrawingState(drawing::DrawingDocument document,std::filesystem::path file)
    :path(std::move(file)),current_(std::make_unique<State>(State{std::move(document),0})) {
    current_->document.synchronize_dimension_identifiers();mark_saved();
}
DrawingState::States DrawingState::copy_states(const States& states) {
    States result;result.reserve(states.size());for(const auto& state:states)result.push_back(std::make_unique<State>(*state));return result;
}
DrawingState::DrawingState(const DrawingState& other)
    :path(other.path),runtime_identity(other.runtime_identity),current_(std::make_unique<State>(*other.current_)),
     undo_(copy_states(other.undo_)),redo_(copy_states(other.redo_)),generation_(other.generation_),next_revision_(other.next_revision_),
     saved_revision_(other.saved_revision_),saved_allocations_(other.saved_allocations_) {}
DrawingState& DrawingState::operator=(const DrawingState& other) {
    if(this!=&other){DrawingState copy(other);*this=std::move(copy);}return *this;
}
void DrawingState::commit(drawing::DrawingDocument document) {
    if(document.document_id!=current_->document.document_id)throw std::invalid_argument("Drawing edit cannot change document identity");
    document.dimension_identifiers.retain(current_->document.dimension_identifiers);document.synchronize_dimension_identifiers();
    auto next=std::make_unique<State>(State{std::move(document),next_revision_});
    undo_.push_back(std::move(current_));current_=std::move(next);++next_revision_;redo_.clear();++generation_;
}
bool DrawingState::step(States& from,States& to) {
    if(from.empty())return false;
    static_assert(std::is_nothrow_move_assignable_v<document::DimensionIdentifiers>);
    auto identifiers=from.back()->document.dimension_identifiers;identifiers.retain(current_->document.dimension_identifiers);
    to.push_back(std::move(current_));current_=std::move(from.back());from.pop_back();
    current_->document.dimension_identifiers=std::move(identifiers);++generation_;return true;
}
bool DrawingState::undo(){return step(undo_,redo_);}
bool DrawingState::redo(){return step(redo_,undo_);}
bool DrawingState::is_dirty()const{return current_->revision!=saved_revision_||current_->document.dimension_identifiers.allocation_count()!=saved_allocations_;}
void DrawingState::mark_saved(){saved_revision_=current_->revision;saved_allocations_=current_->document.dimension_identifiers.allocation_count();}
} // namespace zima::workspace
