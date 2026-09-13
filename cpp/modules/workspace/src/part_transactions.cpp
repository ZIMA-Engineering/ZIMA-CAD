#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/document_dependencies.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/workspace/sketch_reference_operations.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <type_traits>

namespace zima::workspace {
namespace {
using Path=assembly::InstancePath;
using Kind=assembly::ComponentSourceKind;
using Pair=std::pair<std::string,std::string>;
using Signature=std::tuple<std::string,std::string,std::string,std::string>;
using Signatures=std::set<Signature>;
constexpr auto external=assembly::ComponentDependencyKind::ExternalSketchReference;
Signatures signatures(const document::PartDocument& doc) {
    Signatures result;
    visit_document_sketches(doc,[&](const auto& sketch) {
        for(const auto& r:sketch.external_references)if(!r.context_assembly_document_id.empty())
            result.emplace(r.context_assembly_document_id,r.context_instance_path,r.source_document_id,r.source_instance_path);
        return true;
    });
    return result;
}
struct Scope { std::string owner;Path owner_path;Pair branches; };
// The Top ID and exact paths are authoritative. If that context is closed or
// structurally unavailable, a detach may proceed but cannot guess an owner.
std::optional<Scope> scope(const Workspace& live,const Signature& reference) {
    const auto& [top,dependent,source,prerequisite]=reference;
    if(!live.open_assembly(top))return std::nullopt;
    const auto a=Path::decode(dependent),b=Path::decode(prerequisite);
    std::size_t depth=0;
    while(depth<a.occurrence_ids.size()&&depth<b.occurrence_ids.size()&&a.occurrence_ids[depth]==b.occurrence_ids[depth])++depth;
    if(depth==a.occurrence_ids.size()||depth==b.occurrence_ids.size())return std::nullopt;
    Scope result{top,{}, {a.occurrence_ids[depth],b.occurrence_ids[depth]}};
    result.owner_path.occurrence_ids.assign(a.occurrence_ids.begin(),a.occurrence_ids.begin()+static_cast<std::ptrdiff_t>(depth));
    if(depth) {
        const auto owner=live.resolve_occurrence(top,result.owner_path);
        if(!owner||owner->source_kind!=Kind::Assembly)return std::nullopt;
        result.owner=owner->source_document_id;
    }
    return result;
}
void validate_new_references(const Workspace& live,const std::string& id,
    const Signatures& before,const Signatures& after,bool history) {
    std::optional<Pair> context;
    for(const auto& signature:after) {
        const auto& [top,dependent,source,prerequisite]=signature;
        const Pair candidate{top,dependent};
        if(context&&*context!=candidate)throw SketchOperationError("context_reference","Part already owns external references from another occurrence context");
        context=candidate;
        if(before.contains(signature))continue;
        if(!history) {
            auto reference=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
            reference.context_assembly_document_id=top;reference.context_instance_path=dependent;
            require_sketch_reference_context(live,id,reference);
        }
        if(live.open_assembly(top)) {
            const auto target=live.resolve_occurrence(top,Path::decode(dependent));
            const auto origin=live.resolve_occurrence(top,Path::decode(prerequisite));
            if(!target||!origin||target->source_kind!=Kind::Part||origin->source_kind!=Kind::Part||
                target->source_document_id!=id||origin->source_document_id!=source||!scope(live,signature))
                throw DocumentDependencyError("invalid_dependency","External Sketch dependency requires two exact Part occurrences");
        }
        require_acyclic_document_dependency(live,id,source);
    }
}
struct NativeAssembly {assembly::AssemblyDocument document;std::filesystem::path path;};
class Sources {
public:
    Sources(const Workspace& live,const document::PartDocument* candidate):live_(live),candidate_(candidate){}
    const NativeAssembly& assembly_source(const std::string& id,const std::filesystem::path& path) {
        if(auto found=assemblies_.find(id);found!=assemblies_.end())return found->second;
        if(const auto* open=live_.open_assembly(id))return assemblies_.emplace(id,NativeAssembly{open->session.document(),open->path}).first->second;
        if(path.empty()||!std::filesystem::is_regular_file(path))throw DocumentDependencyError("dependency_unavailable","A native source document required to verify the dependency is unavailable.");
        auto doc=assembly::AssemblyDocument::load(path);
        if(doc.document_id!=id)throw DocumentDependencyError("dependency_identity","The component source file belongs to a different document.");
        return assemblies_.emplace(id,NativeAssembly{std::move(doc),path}).first->second;
    }
    // Cache only small dependency signatures. Repeated occurrences do not
    // repeatedly load a large native Part, and no calculated bodies stay here.
    bool visit_branch(const assembly::PartOccurrence& component,const std::filesystem::path& owner_file,
        const std::function<void(const Signatures&)>& inspect,std::set<std::string>& visited,std::size_t depth=0) {
        if(depth>=256)throw DocumentDependencyError("dependency_limit","The component dependency graph is too large or too deep.");
        if(component.derived_copy||component.source_kind==Kind::Pattern)return true;
        const auto& id=component.source_document_id;
        if(!visited.insert(id).second)return true;
        try {
            auto file=component.source_path;if(!file.empty()&&file.is_relative())file=owner_file.parent_path()/file;
            if(component.source_kind==Kind::Part) {
                if(const auto known=references_.find(id);known!=references_.end()){inspect(known->second);return true;}
                const auto remember=[&](const auto& doc){inspect(references_.emplace(id,signatures(doc)).first->second);};
                if(candidate_&&candidate_->document_id==id){remember(*candidate_);return true;}
                if(const auto* open=live_.open_part(id)){remember(open->session.document());return true;}
                if(file.empty()||!std::filesystem::is_regular_file(file))return false;
                const auto doc=document::PartDocument::load(file);
                if(doc.document_id!=id)return false;
                remember(doc);return true;
            }
            const auto& source=assembly_source(id,file);bool known=true;
            for(const auto& child:source.document.components)known=visit_branch(child,source.path,inspect,visited,depth+1)&&known;
            return known;
        }catch(const DocumentDependencyError& error) {
            if(std::string_view(error.code)!="dependency_unavailable"&&std::string_view(error.code)!="dependency_identity")throw;
            return false;
        }catch(const std::filesystem::filesystem_error&){return false;}
    }

private:
    const Workspace& live_;const document::PartDocument* candidate_;
    std::map<std::string,NativeAssembly> assemblies_;
    std::map<std::string,Signatures> references_;
};
class Publication {
public:
    Publication(Workspace& live,const document::PartDocument* candidate):live_(live),sources_(live,candidate){}
    void affect(const Signature& signature,bool required,bool added=false) {
        const auto resolved=scope(live_,signature);
        if(!resolved) {
            if(required)throw DocumentDependencyError("invalid_dependency","External Sketch dependency has no common Assembly owner");
            return;
        }
        auto& owner=owners_[resolved->owner];owner.branches.insert(resolved->branches.first);
        if(added)owner.required.insert(resolved->branches);
        if(owner.path.empty()&&!resolved->owner_path.occurrence_ids.empty()) {
            const auto file=live_.occurrence_source_file(std::get<0>(signature),resolved->owner_path);
            if(file)owner.path=*file;
        }
    }
    void affect_open_owners() {
        for(const auto& state:live_.documents())if(const auto* owner=std::get_if<AssemblyState>(&state)) {
            auto& affected=owners_[owner->session.document().document_id];affected.path=owner->path;
            for(const auto& component:owner->session.document().components)affected.branches.insert(component.occurrence_id);
        }
    }
    void prepare() {
        for(const auto& [id,affected]:owners_) {
            const auto& source=sources_.assembly_source(id,affected.path);auto next=source.document;
            for(const auto& branch:affected.branches) {
                const auto found=std::ranges::find(next.components,branch,&assembly::PartOccurrence::occurrence_id);
                std::set<Pair> required;
                for(const auto& pair:affected.required)if(pair.first==branch)required.insert(pair);
                if(found==next.components.end()) {
                    if(!required.empty())throw DocumentDependencyError("invalid_dependency","External Sketch dependency branches are invalid");
                    continue;
                }
                bool unknown=false;std::set<std::string> visited;
                const bool complete=sources_.visit_branch(*found,source.path,[&](const auto& references) {
                    for(const auto& signature:references) {
                        const auto owner=scope(live_,signature);
                        if(!owner){unknown=true;continue;}
                        if(owner->owner==id&&owner->branches.first==branch)required.insert(owner->branches);
                    }
                },visited);
                unknown=unknown||!complete;
                std::erase_if(next.dependencies,[&](const auto& dependency) {
                    return !unknown&&dependency.kind==external&&dependency.dependent_occurrence_id==branch&&
                        !required.contains({dependency.dependent_occurrence_id,dependency.prerequisite_occurrence_id});
                });
                for(const auto& pair:required)if(!std::ranges::any_of(next.dependencies,[&](const auto& d) {
                    return d.kind==external&&Pair{d.dependent_occurrence_id,d.prerequisite_occurrence_id}==pair;
                }))next.add_dependency(assembly::AssemblyDocument::create_dependency(pair.first,pair.second,external));
            }
            if(next.dependencies==source.document.dependencies)continue;
            if(const auto* open=live_.open_assembly(id)) {
                auto session=open->session;session.update_dependency_snapshots(std::move(next));
                const auto index=static_cast<std::size_t>(std::find_if(live_.documents().begin(),live_.documents().end(),[&](const auto& state) {
                    return std::get_if<AssemblyState>(&state)==open;
                })-live_.documents().begin());
                updates_.push_back({index,std::move(session)});
            }else {
                AssemblyState state{assembly::AssemblySession(source.document),source.path};
                state.session.update_dependency_snapshots(std::move(next));added_.emplace_back(std::move(state));
            }
        }
        // DocumentState's noexcept move keeps existing Part caches in place.
        // All allocation precedes the strongly exception-safe Part commit.
        static_assert(std::is_nothrow_move_constructible_v<DocumentState>);
        live_.documents().reserve(live_.size()+added_.size());
    }
    void publish() noexcept {
        static_assert(std::is_nothrow_move_assignable_v<assembly::AssemblySession>);
        for(auto& update:updates_)std::get<AssemblyState>(live_.documents()[update.index]).session=std::move(update.session);
        for(auto& state:added_)live_.documents().push_back(std::move(state));
    }
private:
    struct Owner {std::filesystem::path path;std::set<std::string> branches;std::set<Pair> required;};
    struct Update {std::size_t index;assembly::AssemblySession session;};
    Workspace& live_;Sources sources_;std::map<std::string,Owner> owners_;
    std::vector<Update> updates_;std::vector<DocumentState> added_;
};
void prepare_change(Publication& publication,const Signatures& before,const Signatures& after,bool history) {
    for(const auto& signature:before)if(!after.contains(signature))publication.affect(signature,false);
    for(const auto& signature:after)if(!before.contains(signature))publication.affect(signature,!history,true);
    publication.prepare();
}
}
void commit_part_document(Workspace& live,const std::string& document_id,document::PartDocument next,
    std::vector<kernel::BodyResult> calculated) {
    // Copy the ID: callers may pass a string borrowed from a relocating state.
    const auto id=document_id;const auto* part=live.open_part(id);
    if(!part)throw std::invalid_argument("Part document is not open");
    const auto before=signatures(part->session.document()),after=signatures(next);
    if(before==after){live.open_part(id)->session.commit(std::move(next),std::move(calculated));return;}
    validate_new_references(live,id,before,after,false);
    Publication publication(live,&next);prepare_change(publication,before,after,false);
    live.open_part(id)->session.commit(std::move(next),std::move(calculated));publication.publish();
}
bool step_part_document_history(Workspace& live,const std::string& document_id,bool redo) {
    const auto id=document_id;const auto* part=live.open_part(id);if(!part)return false;
    const auto* candidate=part->session.history_document(redo);if(!candidate)return false;
    const auto before=signatures(part->session.document()),after=signatures(*candidate);
    validate_new_references(live,id,before,after,true);
    Publication publication(live,candidate);prepare_change(publication,before,after,true);
    auto& session=live.open_part(id)->session;const bool changed=redo?session.redo():session.undo();
    if(changed)publication.publish();return changed;
}
void reconcile_external_sketch_dependencies(Workspace& live) {
    Publication publication(live,nullptr);publication.affect_open_owners();publication.prepare();publication.publish();
}
}
