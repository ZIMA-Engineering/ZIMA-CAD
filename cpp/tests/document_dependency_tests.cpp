#include <zima/workspace/document_dependencies.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <iostream>
#include "sweep_test_support.hpp"
using namespace zima;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void rejected(F&& f,const char* code) {
    try{f();}catch(const workspace::DocumentDependencyError& error){require(std::string(error.code)==code,"Wrong dependency error");return;}
    throw std::runtime_error("Invalid document dependency was accepted");
}
void verify(const fs::path& dir) {
    workspace::Workspace live;
    auto dependent=document::PartDocument::create_default(),source=document::PartDocument::create_default(),middle=document::PartDocument::create_default();
    auto inner=assembly::AssemblyDocument::create_default(),top=assembly::AssemblyDocument::create_default();
    auto a=assembly::AssemblyDocument::create_part_occurrence("Dependent",dependent.document_id,dir/"dependent.prtz",{});
    auto b=assembly::AssemblyDocument::create_part_occurrence("Source",source.document_id,"source.prtz",{});
    auto c=assembly::AssemblyDocument::create_part_occurrence("Middle",middle.document_id,"middle.prtz",{});
    inner.components={b};inner.save(dir/"inner.asmz");
    auto group=assembly::AssemblyDocument::create_assembly_occurrence("Group",inner.document_id,"inner.asmz",inner);
    top.components={a,group,c};
    const auto a_path=assembly::InstancePath{}.child(a.occurrence_id),b_path=assembly::InstancePath{{group.occurrence_id,b.occurrence_id}},c_path=assembly::InstancePath{}.child(c.occurrence_id);
    const auto reference=[&](const auto& owner,const auto& from,const auto& to) {
        auto r=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Point);
        r.source_document_id=owner;r.source_owner_id=owner+":origin";r.source_semantic_key="origin:point";
        r.context_assembly_document_id=top.document_id;r.context_instance_path=from.encoded();r.source_instance_path=to.encoded();r.cached_points={{0,0}};
        return r;
    };
    auto helix=test_support::sweep_fixture(document::FeatureKind::HelicalSweep);auto embedded=sketcher::Sketch::from_serialized(helix.helical.sketches[2]);
    embedded.add_external_reference(reference(middle.document_id,b_path,c_path));helix.helical.sketches[2]=embedded.serialized();source.history={helix};
    auto sketch=sketcher::Sketch::create_default();sketch.add_external_reference(reference(dependent.document_id,c_path,a_path));middle.sketches={sketch};
    dependent.save(dir/"dependent.prtz",{});source.save(dir/"source.prtz",{});middle.save(dir/"middle.prtz",{});
    live.add_part(dependent,{},dir/"dependent.prtz");live.add_assembly(top,dir/"top.asmz");
    require(live.activate_occurrence(top.document_id,a_path).has_value(),"Cannot activate dependency test context");
    const auto before=live.open_assembly(top.document_id)->session.revision(),generation=live.open_assembly(top.document_id)->session.data_generation(),count=live.size();
    const auto context=live.active_document_id(),displayed=live.displayed_document_id();
    const auto check=[&]{workspace::require_acyclic_document_dependency(live,dependent.document_id,source.document_id);};
    // Both source Parts and the intermediate Assembly are closed. The reference
    // is in an embedded Helical profile, not the root Sketch list.
    rejected(check,"dependency_cycle");
    auto draft=dependent;auto pending=sketcher::Sketch::create_default();pending.add_external_reference(reference(source.document_id,a_path,b_path));draft.sketches.push_back(pending);
    rejected([&]{workspace::commit_part_document(live,dependent.document_id,draft,{});},"dependency_cycle");
    require(live.size()==count&&live.active_document_id()==context&&live.displayed_document_id()==displayed&&
        live.open_assembly(top.document_id)->session.revision()==before&&live.open_assembly(top.document_id)->session.data_generation()==generation&&
        live.open_assembly(top.document_id)->session.document().dependencies.empty(),"Rejected dependency changed live documents or Assembly state");
    // Open unsaved source data is authoritative over the cyclic native file.
    auto changed=source;changed.history.clear();live.add_part(changed,{},dir/"source.prtz");check();
    require(live.open_part(source.document_id)->session.document().history.empty(),"Validation replaced the unsaved source");
    static_cast<void>(live.remove(source.document_id));rejected(check,"dependency_cycle");
    // A missing transitive source must not be silently treated as acyclic.
    auto unknown=document::PartDocument::create_default().document_id;
    auto missing=middle;missing.sketches.front().external_references.front().source_document_id=unknown;missing.save(dir/"middle.prtz",{});
    rejected(check,"dependency_unavailable");middle.save(dir/"middle.prtz",{});
    auto acyclic=middle;acyclic.sketches.front().external_references.clear();acyclic.save(dir/"middle.prtz",{});check();
    require(live.size()==count&&!live.open_part(source.document_id)&&!live.open_part(middle.document_id)&&!live.open_assembly(inner.document_id),
        "Successful closed-source validation opened live documents");
    middle.save(dir/"middle.prtz",{});
    auto wrong=document::PartDocument::create_default();wrong.save(dir/"source.prtz",{});rejected(check,"dependency_identity");
    source.save(dir/"source.prtz",{});
    rejected([&]{workspace::require_acyclic_document_dependency(live,source.document_id,source.document_id);},"dependency_cycle");
    rejected([&]{workspace::require_acyclic_document_dependency(live,{},source.document_id);},"invalid_dependency");
    // Insertion and external references share the same document graph. A Part
    // cannot depend on an Assembly that already contains that Part.
    rejected([&]{workspace::require_acyclic_document_dependency(live,dependent.document_id,top.document_id);},"dependency_cycle");
}
}
int main(){try{const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-dependency-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create test folder");verify(dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);
    std::cout<<"Closed nested sources, embedded profile cycles, unsaved authority and atomic dependency rejection passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
