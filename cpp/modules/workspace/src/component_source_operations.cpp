#include <zima/workspace/component_source_operations.hpp>
#include <zima/workspace/native_documents.hpp>
#include <zima/workspace/family_operations.hpp>
namespace zima::workspace {
void relink_component_source(Workspace& live,const std::string& owner,const std::string& occurrence,
    const std::filesystem::path& requested,const std::function<void(std::function<void()>)>& runner) {
    const auto* state=live.open_assembly(owner);
    if(!state || live.active_document_id()!=owner)throw ComponentOperationError("inactive_owner","Activate the owning Assembly before changing a source file.");
    const auto* selected=state->session.document().find_occurrence(occurrence);
    if(!selected || selected->derived_copy)throw ComponentOperationError("invalid_occurrence","Select an original immediate component.");
    const auto source_id=selected->source_document_id;
    const auto parent_id=source_id.substr(0,source_id.find(":family:"));
    const auto expected=selected->source_kind==assembly::ComponentSourceKind::Part?NativeDocumentType::Part:NativeDocumentType::Assembly;
    const auto path=std::filesystem::absolute(requested).lexically_normal();
    const auto identity=state->runtime_identity;
    const auto revision=state->session.revision(),generation=state->session.data_generation();
    auto candidate=state->session.document();const auto owner_path=state->path;
    const auto sources=native_source_resolver(live);
    std::optional<PreparedNativeDocument> prepared;
    const auto read=[&] {
        prepared=read_native_document(path,sources);
        if(prepared->type()!=expected || prepared->id()!=parent_id)
            throw ComponentOperationError("source_identity","The selected file is not the original source document. Use Replace to choose a different component.");
        for(auto& component:candidate.components)if(component.source_document_id==source_id)component.source_path=path;
        candidate.hydrate_sources(owner_path,sources);
    };
    if(runner)runner(read);else read();
    const auto* current=live.open_assembly(owner);
    if(!prepared || !current || current->runtime_identity!=identity || current->session.revision()!=revision || current->session.data_generation()!=generation || live.active_document_id()!=owner)
        throw ComponentOperationError("document_changed","The owning Assembly changed while locating its source file.");
    if(live.open_part(parent_id)||live.open_assembly(parent_id))live.reserve_file(path);
    live.open_assembly(owner)->session.commit(std::move(candidate));
    // The same open native document follows its explicitly selected location.
    if(auto* part=live.open_part(parent_id))part->path=path;
    if(auto* assembly=live.open_assembly(parent_id))assembly->path=path;
}
namespace {
std::filesystem::path source_path(const Workspace& workspace,const std::string& id) {
    if(const auto* part=workspace.open_part(id))return part->path;
    if(const auto* assembly=workspace.open_assembly(id))return assembly->path;
    throw ComponentOperationError("source_not_open","The component source must be an open Part or Assembly.");
}
}
ComponentSourceOpen activate_component_source(Workspace& workspace,const std::string& top_id,const assembly::InstancePath& requested,
    const std::function<void(std::function<void()>)>& runner,const std::function<void(const std::filesystem::path&)>& before_read) {
    auto source=open_component_source(workspace,top_id,requested,runner,before_read);
    if(!workspace.activate_occurrence(top_id,source.source_instance_path))
        throw ComponentOperationError("occurrence_not_found","The requested component occurrence does not exist.");
    return source;
}
bool deactivate_component_source(Workspace& workspace) {
    if(workspace.active_occurrence_path().empty())return false;
    const auto top=workspace.displayed_document_id();
    if(!workspace.open_assembly(top))throw ComponentOperationError("unsupported_document","Component commands require an open Assembly.");
    workspace.activate(top);return true;
}
ComponentSourceOpen open_component_source(Workspace& workspace,const std::string& top_id,const assembly::InstancePath& requested,
    const std::function<void(std::function<void()>)>& runner,const std::function<void(const std::filesystem::path&)>& before_read) {
    const auto* top=workspace.open_assembly(top_id);
    if(!top)throw ComponentOperationError("unsupported_document","Component commands require an open Assembly.");
    const auto source=workspace.derived_source_path(top_id,requested);
    const auto address=workspace.resolve_occurrence(top_id,source);
    if(!address)throw ComponentOperationError("occurrence_not_found","The requested component occurrence does not exist.");
    const auto id=address->source_document_id;
    const auto expected=address->source_kind==assembly::ComponentSourceKind::Assembly?NativeDocumentType::Assembly:NativeDocumentType::Part;
    const auto already_open=[&] {
        if((workspace.open_part(id) && expected!=NativeDocumentType::Part) || (workspace.open_assembly(id) && expected!=NativeDocumentType::Assembly) || workspace.open_drawing(id))
            throw ComponentOperationError("dependency_identity","The component source file belongs to a different document.");
        return workspace.open_part(id)!=nullptr || workspace.open_assembly(id)!=nullptr;
    };
    if(already_open())return {id,source_path(workspace,id),source,false};
    const auto parent_id=id.substr(0,id.find(":family:"));
    const auto open_member=[&](const std::filesystem::path& path) {
        if(expected==NativeDocumentType::Part) {
            std::vector<kernel::BodyResult> cache;auto member=read_family_part(&workspace,path,id,cache);
            workspace.add_part(std::move(member),std::move(cache),path);
        } else workspace.add_assembly(read_family_assembly(&workspace,path,id),path);
    };
    if(parent_id!=id&&(workspace.open_part(parent_id)||workspace.open_assembly(parent_id))) {
        const auto path=source_path(workspace,parent_id);open_member(path);
        return {id,path,source,true};
    }
    const auto file=workspace.occurrence_source_file(top_id,source);
    if(!file || file->empty())throw ComponentOperationError("source_unavailable","The selected component has no available native source file.");
    const auto path=std::filesystem::absolute(*file).lexically_normal();
    if(const auto same_path=workspace.document_id_for_path(path);same_path && *same_path!=parent_id)
        throw ComponentOperationError("dependency_identity","The component source file belongs to a different document.");
    const auto active=workspace.active_document_id(),displayed=workspace.displayed_document_id(),active_path=workspace.active_occurrence_path();
    const auto runtime=top->runtime_identity;const auto revision=top->session.revision(),generation=top->session.data_generation();
    if(before_read)before_read(path);
    std::optional<PreparedNativeDocument> prepared;
    const auto sources=expected==NativeDocumentType::Assembly ? native_source_resolver(workspace) : assembly::AssemblyDocument::SourceResolver{};
    const auto read=[&]{prepared=read_native_document(path,sources);};if(runner)runner(read);else read();
    const auto* current=workspace.open_assembly(top_id);
    if(workspace.active_document_id()!=active || workspace.displayed_document_id()!=displayed || workspace.active_occurrence_path()!=active_path || !current || current->runtime_identity!=runtime || current->session.revision()!=revision || current->session.data_generation()!=generation)
        throw ComponentOperationError("document_changed","The owning Assembly changed while its component source was being opened.");
    if(!prepared)throw ComponentOperationError("read_incomplete","Native source reading did not complete.");
    if(prepared->id()!=parent_id || prepared->type()!=expected)
        throw ComponentOperationError("dependency_identity","The component source file belongs to a different document.");
    // An editor may have opened the source while the synchronous GUI runner
    // pumped events. Its current in-memory state remains authoritative.
    if(already_open())return {id,source_path(workspace,id),source,false};
    if(const auto same_path=workspace.document_id_for_path(path);same_path && *same_path!=parent_id)
        throw ComponentOperationError("dependency_identity","The component source file belongs to a different document.");
    // Validate the stored row before publishing its parent. Reading a member is
    // not an implicit regeneration and never opens an unrelated generic instead.
    if(parent_id!=id) {
        Workspace checked;static_cast<void>(insert_native_document(checked,*prepared));
        if(expected==NativeDocumentType::Part){std::vector<kernel::BodyResult> cache;static_cast<void>(read_family_part(&checked,path,id,cache));}
        else static_cast<void>(read_family_assembly(&checked,path,id));
    }
    static_cast<void>(insert_native_document(workspace,std::move(*prepared)));
    if(parent_id!=id)open_member(path);
    return {id,path,source,true};
}
}
