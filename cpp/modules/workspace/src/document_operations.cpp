#include <zima/workspace/document_operations.hpp>
#include <stdexcept>
#include <type_traits>

namespace zima::workspace {
DocumentSave prepare_document_save(const Workspace& workspace,
    const std::string& id, const std::filesystem::path& target) {
    const auto* state=workspace.find(id);
    if(!state)throw std::invalid_argument("Document is not open");
    if(target.empty())throw std::invalid_argument("Save path is empty");
    DocumentSave job;
    job.receipt_.id_=id;job.receipt_.target_=target;job.receipt_.kind_=state->index();
    std::visit([&](const auto& value) {
        using State=std::decay_t<decltype(value)>;
        job.receipt_.original_path_=value.path;
        job.receipt_.runtime_identity_=value.runtime_identity;
        if constexpr(std::is_same_v<State, DrawingState>) {
            job.snapshot_=value.document(); job.receipt_.revision_=value.revision();
        }
        else {
            job.receipt_.revision_=value.session.revision();
            job.receipt_.generation_=value.session.data_generation();
            job.receipt_.allocations_=value.session.document().dimension_identifiers.allocation_count();
            if constexpr(std::is_same_v<State, PartState>)
                job.snapshot_=DocumentSave::Part{value.session.document(),value.session.calculated_boundaries()};
            else job.snapshot_=value.session.document();
        }
    },*state);
    return job;
}

SavedDocument DocumentSave::write() const {
    std::visit([&](const auto& snapshot) {
        using Snapshot=std::decay_t<decltype(snapshot)>;
        if constexpr(std::is_same_v<Snapshot, Part>)snapshot.document.save(receipt_.target_,snapshot.boundaries);
        else snapshot.save(receipt_.target_);
    },snapshot_);
    return receipt_;
}

bool complete_document_save(Workspace& workspace, const SavedDocument& saved) {
    auto* state=workspace.find(saved.id_);
    if(!state || state->index()!=saved.kind_)return false;
    return std::visit([&](auto& value) {
        if(value.path!=saved.original_path_ || value.runtime_identity!=saved.runtime_identity_)return false;
        value.path=saved.target_;
        using State=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<State, DrawingState>) {
            if(value.revision()==saved.revision_)value.mark_saved();
        } else {
            if(value.session.revision()==saved.revision_ &&
               value.session.data_generation()==saved.generation_ &&
               value.session.document().dimension_identifiers.allocation_count()==saved.allocations_)
                value.session.mark_saved();
        }
        return true;
    },*state);
}

bool document_needs_save(const Workspace& workspace, const std::string& id) {
    const auto* state=workspace.find(id);
    if(!state)throw std::invalid_argument("Document is not open");
    return std::visit([](const auto& value) {
        using State=std::decay_t<decltype(value)>;
        const bool dirty=[&] {
            if constexpr(std::is_same_v<State,DrawingState>)return value.is_dirty();
            else return value.session.is_dirty();
        }();
        if(dirty || value.path.empty())return true;
        std::error_code error;
        return !std::filesystem::is_regular_file(value.path,error);
    },*state);
}
CloseDocumentResult close_document(Workspace& workspace, const std::string& id, bool discard) {
    if(!workspace.find(id))return CloseDocumentResult::NotOpen;
    if(!discard && document_needs_save(workspace,id))return CloseDocumentResult::UnsavedChanges;
    return workspace.remove(id)?CloseDocumentResult::Closed:CloseDocumentResult::NotOpen;
}

bool can_step_document_history(const Workspace& workspace,
    const std::string& id, HistoryDirection direction) {
    const bool redo=direction==HistoryDirection::Redo;
    if(const auto* part=workspace.open_part(id))return redo?part->session.can_redo():part->session.can_undo();
    if(const auto* assembly=workspace.open_assembly(id))return redo?assembly->session.can_redo():assembly->session.can_undo();
    return false;
}

bool step_document_history(Workspace& workspace,
    const std::string& id, HistoryDirection direction) {
    const bool redo=direction==HistoryDirection::Redo;
    if(auto* part=workspace.open_part(id)) {
        const bool changed=redo?part->session.redo():part->session.undo();
        if(changed)workspace.synchronize_external_sketch_dependencies();
        return changed;
    }
    if(auto* assembly=workspace.open_assembly(id))return redo?assembly->session.redo():assembly->session.undo();
    return false;
}
} // namespace zima::workspace
