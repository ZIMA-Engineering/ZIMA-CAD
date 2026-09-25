#include <zima/workspace/document_operations.hpp>
#include <zima/workspace/symbol_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <stdexcept>
#include <cctype>
#include <type_traits>

namespace zima::workspace {
DocumentSave prepare_document_save(const Workspace& workspace,
    const std::string& requested, const std::filesystem::path& target) {
    const auto id=family_owner(workspace,requested);
    if(id!=requested) {
        const auto* parent=workspace.find(id);
        if(!parent)throw std::invalid_argument("The owning family document is not open.");
        const auto path=std::visit([](const auto& value){return value.path;},*parent);
        if(!path.empty()&&std::filesystem::absolute(path).lexically_normal()!=std::filesystem::absolute(target).lexically_normal())
            throw std::invalid_argument("A family variant must be saved in its owning file.");
    }
    const auto* state=workspace.find(id);
    if(!state)throw std::invalid_argument("Document is not open");
    if(target.empty())throw std::invalid_argument("Save path is empty");
    workspace.reserve_file(target);
    DocumentSave job;
    job.receipt_.id_=id;job.receipt_.target_=target;job.receipt_.kind_=state->index();
    std::visit([&](const auto& value) {
        using State=std::decay_t<decltype(value)>;
        job.receipt_.original_path_=value.path;
        job.receipt_.runtime_identity_=value.runtime_identity;
        if constexpr(std::is_same_v<State, DrawingState>) {
            job.snapshot_=value.document(); job.receipt_.revision_=value.revision();job.receipt_.generation_=value.data_generation();
        }
        else {
            job.receipt_.revision_=value.session.revision();
            job.receipt_.generation_=value.session.data_generation();
            job.receipt_.allocations_=value.session.document().dimension_identifiers.allocation_count();
            if constexpr(std::is_same_v<State, PartState>) {
                if(value.symbol_definition) {
                    auto extension=target.extension().string();
                    std::ranges::transform(extension,extension.begin(),[](unsigned char c){return std::tolower(c);});
                    if(extension!=".symz")throw std::invalid_argument("A symbol document requires the symz extension");
                    job.snapshot_=edited_symbol_definition(workspace,id);
                } else job.snapshot_=DocumentSave::Part{value.session.document(),value.session.calculated_boundaries()};
            }
            else job.snapshot_=value.session.document();
        }
    },*state);
    if(std::holds_alternative<DrawingState>(*state)) {
        if(const auto pending=workspace.drawing_edited_sources.find(id);pending!=workspace.drawing_edited_sources.end()) {
            std::set<std::string> included;
            for(const auto& source:pending->second) {
                const auto source_id=family_owner(workspace,source);
                if(!included.insert(source_id).second)continue;
                const auto* model=workspace.find(source_id);
                if(!model||std::holds_alternative<DrawingState>(*model))
                    throw std::invalid_argument("Edited Drawing source is no longer open. Save the source model first.");
                const auto source_path=std::visit([](const auto& value){return value.path;},*model);
                if(source_path.empty())throw std::invalid_argument("Edited Drawing source has no native filename. Save the source model first.");
                job.sources_.push_back(prepare_document_save(workspace,source_id,source_path));
            }
        }
    }
    return job;
}

std::optional<DocumentSave> prepare_document_save_if_needed(
    const Workspace& workspace,const std::string& document_id,
    const std::filesystem::path& target) {
    if(!document_needs_save(workspace,document_id))return std::nullopt;
    return prepare_document_save(workspace,document_id,target);
}

SavedDocument DocumentSave::write() const {
    auto result=receipt_;
    // A source failure must prevent publication of the dependent Drawing.
    for(const auto& source:sources_)result.sources_.push_back(source.write());
    std::visit([&](const auto& snapshot) {
        using Snapshot=std::decay_t<decltype(snapshot)>;
        if constexpr(std::is_same_v<Snapshot, Part>)snapshot.document.save(receipt_.target_,snapshot.boundaries);
        else snapshot.save(receipt_.target_);
    },snapshot_);
    return result;
}

bool complete_document_save(Workspace& workspace, const SavedDocument& saved) {
    bool sources_current=true;
    for(const auto& source:saved.sources_) {
        if(!complete_document_save(workspace,source)||document_needs_save(workspace,source.id_))sources_current=false;
    }
    auto* state=workspace.find(saved.id_);
    if(!state || state->index()!=saved.kind_)return false;
    return std::visit([&](auto& value) {
        if(value.path!=saved.original_path_ || value.runtime_identity!=saved.runtime_identity_)return false;
        value.path=saved.target_;
        for(auto& member:workspace.documents())std::visit([&](auto& other){
            if constexpr(requires{other.session;})if(other.session.document().family.parent_id==saved.id_)other.path=saved.target_;
        },member);
        using State=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<State, DrawingState>) {
            if(sources_current&&value.revision()==saved.revision_ && value.data_generation()==saved.generation_) {
                value.mark_saved();workspace.drawing_edited_sources.erase(saved.id_);
            }
        } else {
            if(value.session.revision()==saved.revision_ &&
               value.session.data_generation()==saved.generation_ &&
               value.session.document().dimension_identifiers.allocation_count()==saved.allocations_) {
                value.session.mark_saved();
                for(auto& [drawing,sources]:workspace.drawing_edited_sources)
                    std::erase_if(sources,[&](const auto& source){return family_owner(workspace,source)==saved.id_;});
            }
        }
        return true;
    },*state);
}

bool document_needs_save(const Workspace& workspace, const std::string& requested) {
    const auto id=family_owner(workspace,requested);
    const auto* state=workspace.find(id);
    if(!state)throw std::invalid_argument("Document is not open");
    if(const auto pending=workspace.drawing_edited_sources.find(id);pending!=workspace.drawing_edited_sources.end()&&!pending->second.empty())return true;
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
    if(family_owner(workspace,id)!=id)return workspace.remove(id)?CloseDocumentResult::Closed:CloseDocumentResult::NotOpen;
    if(!discard && document_needs_save(workspace,id))return CloseDocumentResult::UnsavedChanges;
    std::vector<std::string> members;
    for(const auto& state:workspace.documents())std::visit([&](const auto& value){
        if constexpr(requires{value.session;})if(value.session.document().family.parent_id==id)members.push_back(value.session.document().document_id);
    },state);
    for(const auto& member:members)static_cast<void>(workspace.remove(member));
    workspace.drawing_edited_sources.erase(id);
    return workspace.remove(id)?CloseDocumentResult::Closed:CloseDocumentResult::NotOpen;
}

bool can_step_document_history(const Workspace& workspace,
    const std::string& requested, HistoryDirection direction) {
    const auto id=family_owner(workspace,requested);
    const bool redo=direction==HistoryDirection::Redo;
    if(const auto* part=workspace.open_part(id))return redo?part->session.can_redo():part->session.can_undo();
    if(const auto* assembly=workspace.open_assembly(id))return redo?assembly->session.can_redo():assembly->session.can_undo();
    if(const auto* drawing=workspace.open_drawing(id))return redo?drawing->can_redo():drawing->can_undo();
    return false;
}

bool step_document_history(Workspace& workspace,
    const std::string& requested, HistoryDirection direction) {
    const auto id=family_owner(workspace,requested);
    const bool redo=direction==HistoryDirection::Redo;
    if(workspace.open_part(id)){const auto changed=step_part_document_history(workspace,id,redo);if(changed)restore_family_tabs(workspace,id);return changed;}
    if(workspace.open_assembly(id)){const auto changed=step_assembly_document_history(workspace,id,redo);if(changed)restore_family_tabs(workspace,id);return changed;}
    if(auto* drawing=workspace.open_drawing(id))return redo?drawing->redo():drawing->undo();
    return false;
}
} // namespace zima::workspace
