#include <zima/workspace/component_operations.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/document/metadata.hpp>
#include <set>
namespace zima::workspace {
namespace {
struct SourceFile {std::filesystem::path path;assembly::ComponentSourceKind kind;};
class InsertionDependencies {
public:
    InsertionDependencies(const Workspace& live,std::string owner):live_(live),owner_(std::move(owner)) {
        for(const auto& state:live.documents()) {
            if(const auto* part=std::get_if<PartState>(&state))files_.try_emplace(part->session.document().document_id,SourceFile{part->path,assembly::ComponentSourceKind::Part});
            if(const auto* assembly=std::get_if<AssemblyState>(&state)) {
                files_.try_emplace(assembly->session.document().document_id,SourceFile{assembly->path,assembly::ComponentSourceKind::Assembly});
                register_components(assembly->session.document(),assembly->path);
            }
        }
    }
    void visit(const std::string& id) {
        if(id==owner_ || visiting_.contains(id))throw ComponentOperationError("dependency_cycle","Component insertion would create a dependency cycle.");
        if(done_.contains(id))return;
        if(visiting_.size()>=256)throw ComponentOperationError("dependency_limit","The component dependency graph is too large or too deep.");
        visiting_.insert(id);
        const auto& workspace=source_workspace(id);
        std::vector<std::string> dependencies;
        if(const auto* assembly=workspace.open_assembly(id)) {
            register_components(assembly->session.document(),assembly->path);
            for(const auto& component:assembly->session.document().components)
                if(!component.derived_copy && component.source_kind!=assembly::ComponentSourceKind::Pattern)dependencies.push_back(component.source_document_id);
        }
        visit_document_sketches(workspace,id,[&](const auto& sketch){
            for(const auto& reference:sketch.external_references)if(!reference.source_document_id.empty() && reference.source_document_id!=id)dependencies.push_back(reference.source_document_id);
            return true;
        });
        // Recursion may grow the private document vector. Keep no borrowed state
        // pointers across it; only the collected stable document IDs survive.
        for(const auto& dependency:dependencies)visit(dependency);
        visiting_.erase(id);done_.insert(id);
        if(loaded_.find(id))static_cast<void>(loaded_.remove(id));
    }
private:
    const Workspace& live_;Workspace loaded_;std::string owner_;
    std::map<std::string,SourceFile> files_;std::set<std::string> visiting_,done_;
    void register_components(const assembly::AssemblyDocument& doc,const std::filesystem::path& path) {
        for(const auto& component:doc.components) {
            if(component.derived_copy || component.source_kind==assembly::ComponentSourceKind::Pattern)continue;
            auto source=component.source_path;if(!source.empty() && source.is_relative())source=path.parent_path()/source;
            files_.try_emplace(component.source_document_id,SourceFile{std::move(source),component.source_kind});
        }
    }
    const Workspace& source_workspace(const std::string& id) {
        if(live_.open_part(id) || live_.open_assembly(id))return live_;
        if(loaded_.open_part(id) || loaded_.open_assembly(id))return loaded_;
        const auto found=files_.find(id);
        if(found==files_.end() || found->second.path.empty())throw ComponentOperationError("dependency_unavailable","Open the referenced source documents before inserting this component.");
        const auto source=found->second;
        if(source.kind==assembly::ComponentSourceKind::Part) {
            std::vector<kernel::BodyResult> calculated;auto document=document::PartDocument::load(source.path,&calculated);
            if(document.document_id!=id)throw ComponentOperationError("dependency_identity","The component source file belongs to a different document.");
            loaded_.add_part(std::move(document),std::move(calculated),source.path);
        }else {
            auto document=assembly::AssemblyDocument::load(source.path);
            if(document.document_id!=id)throw ComponentOperationError("dependency_identity","The component source file belongs to a different document.");
            loaded_.add_assembly(std::move(document),source.path);
        }
        return loaded_;
    }
};
}
std::string insert_component(Workspace& live,const std::string& owner,const std::string& source,const std::optional<std::string>& requested_name) {
    if(!live.open_assembly(owner))throw ComponentOperationError("unsupported_document","Component insertion requires an open owning Assembly.");
    if(live.active_document_id()!=owner || live.displayed_document_id()!=owner)
        throw ComponentOperationError("unsupported_context","Open the owning Assembly in its own tab before inserting a component.");
    const auto* part=live.open_part(source);const auto* assembly=live.open_assembly(source);
    if(!part && !assembly)throw ComponentOperationError("source_not_open","The component source must be an open Part or Assembly.");
    const auto name=requested_name.value_or(part?part->session.document().name:assembly->session.document().name);
    document::validate_native_metadata_text(name);
    if(name.empty() || name.size()>256)throw ComponentOperationError("invalid_arguments","A component name must contain 1 to 256 bytes.");
    InsertionDependencies(live,owner).visit(source);
    return part?live.insert_open_part(owner,source,name):live.insert_open_assembly(owner,source,name);
}
}
