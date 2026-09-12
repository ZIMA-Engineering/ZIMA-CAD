#include <zima/workspace/component_source_operations.hpp>
#include <zima/workspace/native_documents.hpp>
namespace zima::workspace {
namespace {
std::filesystem::path source_path(const Workspace& workspace,const std::string& id) {
    if(const auto* part=workspace.open_part(id))return part->path;
    if(const auto* assembly=workspace.open_assembly(id))return assembly->path;
    throw ComponentOperationError("source_not_open","The component source must be an open Part or Assembly.");
}
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
    const auto file=workspace.occurrence_source_file(top_id,source);
    if(!file || file->empty())throw ComponentOperationError("source_unavailable","The selected component has no available native source file.");
    const auto path=std::filesystem::absolute(*file).lexically_normal();
    if(const auto same_path=workspace.document_id_for_path(path);same_path && *same_path!=id)
        throw ComponentOperationError("dependency_identity","The component source file belongs to a different document.");
    const auto active=workspace.active_document_id(),displayed=workspace.displayed_document_id();
    const auto runtime=top->runtime_identity;const auto revision=top->session.revision(),generation=top->session.data_generation();
    if(before_read)before_read(path);
    std::optional<PreparedNativeDocument> prepared;
    const auto read=[&]{prepared=read_native_document(path);};if(runner)runner(read);else read();
    const auto* current=workspace.open_assembly(top_id);
    if(workspace.active_document_id()!=active || workspace.displayed_document_id()!=displayed || !current || current->runtime_identity!=runtime || current->session.revision()!=revision || current->session.data_generation()!=generation)
        throw ComponentOperationError("document_changed","The owning Assembly changed while its component source was being opened.");
    if(!prepared)throw ComponentOperationError("read_incomplete","Native source reading did not complete.");
    if(prepared->id()!=id || prepared->type()!=expected)
        throw ComponentOperationError("dependency_identity","The component source file belongs to a different document.");
    // An editor may have opened the source while the synchronous GUI runner
    // pumped events. Its current in-memory state remains authoritative.
    if(already_open())return {id,source_path(workspace,id),source,false};
    if(const auto same_path=workspace.document_id_for_path(path);same_path && *same_path!=id)
        throw ComponentOperationError("dependency_identity","The component source file belongs to a different document.");
    const auto inserted=insert_native_document(workspace,std::move(*prepared));
    return {inserted,path,source,true};
}
}
