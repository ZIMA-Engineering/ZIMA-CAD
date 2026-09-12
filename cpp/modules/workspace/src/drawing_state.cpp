#include <zima/workspace/workspace.hpp>
namespace zima::workspace {
DrawingState::DrawingState(drawing::DrawingDocument document,std::filesystem::path file):path(std::move(file)),current_{std::move(document),0}{current_.document.synchronize_dimension_identifiers();mark_saved();}
void DrawingState::commit(drawing::DrawingDocument next){
    if(next.document_id!=current_.document.document_id)throw std::invalid_argument("Drawing edit cannot change document identity");
    next.dimension_identifiers.retain(current_.document.dimension_identifiers);next.synchronize_dimension_identifiers();
    undo_.push_back(std::move(current_));current_={std::move(next),next_revision_++};redo_.clear();++generation_;
}
bool DrawingState::step(std::vector<State>& from,std::vector<State>& to){
    if(from.empty())return false;
    auto identifiers=from.back().document.dimension_identifiers;identifiers.retain(current_.document.dimension_identifiers);
    to.push_back(std::move(current_));current_=std::move(from.back());from.pop_back();current_.document.dimension_identifiers=std::move(identifiers);++generation_;return true;
}
bool DrawingState::undo(){return step(undo_,redo_);}
bool DrawingState::redo(){return step(redo_,undo_);}
bool DrawingState::is_dirty()const{return current_.revision!=saved_revision_||current_.document.dimension_identifiers.allocation_count()!=saved_allocations_;}
void DrawingState::mark_saved(){saved_revision_=current_.revision;saved_allocations_=current_.document.dimension_identifiers.allocation_count();}
}
