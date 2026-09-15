#include <zima/workspace/document_dependencies.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <map>
#include <set>
namespace zima::workspace {
namespace {
struct SourceFile {std::filesystem::path path;assembly::ComponentSourceKind kind;};
class DocumentDependencies {
public:
    DocumentDependencies(const Workspace& live,std::string owner):live_(live),owner_(std::move(owner)) {
        for(const auto& state:live.documents()) {
            if(const auto* part=std::get_if<PartState>(&state))files_.try_emplace(part->session.document().document_id,SourceFile{part->path,assembly::ComponentSourceKind::Part});
            if(const auto* assembly=std::get_if<AssemblyState>(&state)) {
                files_.try_emplace(assembly->session.document().document_id,SourceFile{assembly->path,assembly::ComponentSourceKind::Assembly});
                register_components(assembly->session.document(),assembly->path);
            }
        }
    }
    void visit(const std::string& id) {
        const auto family_id=id.substr(0,id.find(":family:"));
        if(family_id==owner_.substr(0,owner_.find(":family:")) || visiting_.contains(family_id))throw DocumentDependencyError("dependency_cycle","The document dependency would create a cycle.");
        if(done_.contains(id))return;
        if(visiting_.size()>=256)throw DocumentDependencyError("dependency_limit","The component dependency graph is too large or too deep.");
        visiting_.insert(family_id);
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
        visiting_.erase(family_id);done_.insert(id);
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
    void discover_source_file(const std::string& id) {
        const auto scan=[&](const Workspace& workspace) {
            for(const auto& state:workspace.documents())if(const auto* top=std::get_if<AssemblyState>(&state)) {
                const auto inspect=[&](const auto& self,const auto& nodes,const assembly::InstancePath& parent,std::size_t depth)->bool {
                    if(depth>256)throw DocumentDependencyError("dependency_limit","The component dependency graph is too large or too deep.");
                    for(const auto& node:nodes) {
                        const auto path=parent.child(node.occurrence_id);
                        if(node.source_document_id==id&&node.source_kind!=assembly::ComponentSourceKind::Pattern) {
                            const auto file=workspace.occurrence_source_file(top->session.document().document_id,path);
                            if(file&&!file->empty()){files_[id]={*file,node.source_kind};return true;}
                        }
                        if constexpr(requires{node.nested_snapshot;}) {
                            if(self(self,node.nested_snapshot,path,depth+1))return true;
                        }else if(self(self,node.children,path,depth+1))return true;
                    }
                    return false;
                };
                if(inspect(inspect,top->session.document().components,{},0))return true;
            }
            return false;
        };
        if(!scan(live_))static_cast<void>(scan(loaded_));
    }
    const Workspace& source_workspace(const std::string& id) {
        if(live_.open_part(id) || live_.open_assembly(id))return live_;
        if(loaded_.open_part(id) || loaded_.open_assembly(id))return loaded_;
        if(!files_.contains(id)||files_.at(id).path.empty())discover_source_file(id);
        const auto found=files_.find(id);
        if(found==files_.end() || found->second.path.empty())throw DocumentDependencyError("dependency_unavailable","A native source document required to verify the dependency is unavailable.");
        const auto source=found->second;
        try { if(source.kind==assembly::ComponentSourceKind::Part) {
            std::vector<kernel::BodyResult> calculated;auto document=read_family_part(&live_,source.path,id,calculated);
            if(document.document_id!=id)throw DocumentDependencyError("dependency_identity","The component source file belongs to a different document.");
            loaded_.add_part(std::move(document),std::move(calculated),source.path);
        }else {
            auto document=read_family_assembly(&live_,source.path,id);
            if(document.document_id!=id)throw DocumentDependencyError("dependency_identity","The component source file belongs to a different document.");
            loaded_.add_assembly(std::move(document),source.path);
        }} catch(const DrawingOperationError&) {throw DocumentDependencyError("dependency_identity","The component source file belongs to a different document.");}
        return loaded_;
    }
};
}
void require_acyclic_document_dependency(const Workspace& live,const std::string& owner,const std::string& source) {
    if(owner.empty()||source.empty())throw DocumentDependencyError("invalid_dependency","Document dependency identities are required.");
    DocumentDependencies(live,owner).visit(source);
}
} // namespace zima::workspace
