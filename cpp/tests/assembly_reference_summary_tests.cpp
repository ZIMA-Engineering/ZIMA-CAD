#include "context_reference_test_support.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <iostream>
using namespace zima;namespace fs=std::filesystem;using commands::Json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const std::string& name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(name+": "+result.code+": "+result.message);return result;
}
void detach_all(document::PartDocument& doc) {
    static_cast<void>(workspace::update_document_sketches(doc,[](auto& sketch) {
        const auto references=sketch.external_references;
        for(const auto& reference:references)sketch.remove_geometry(reference.id);
        return !references.empty();
    }));
}
void assembly_history(const fs::path& dir) {
    kernel::OcctKernel kernel;test_support::ContextReferenceFixture f(kernel,dir);
    detach_all(f.target);f.inner.dependencies.clear();f.inner.cuts.clear();f.inner.sketches.clear();
    workspace::Workspace live;f.load(live);auto cwd=dir;command_host::Host host(live,kernel,cwd);
    auto renamed=live.open_assembly(f.inner.document_id)->session.document();renamed.name="First rename";
    live.open_assembly(f.inner.document_id)->session.commit(std::move(renamed));
    run(host,"component.activate",{{"instance_path",f.target_path.encoded()}});
    const auto reference=run(host,"sketch.reference.create",{{"sketch",f.sketch_id},{"kind","edge"},
        {"owner",f.edge.reference.owner_id},{"key",f.edge.reference.semantic_key},{"instance_path",f.source_path.encoded()}}).data.at("reference");
    const auto stored=workspace::document_sketch(live,f.target.document_id,f.sketch_id).serialized();
    const auto part_generation=live.open_part(f.target.document_id)->session.data_generation();
    const auto source_generation=live.open_part(f.source.document_id)->session.data_generation();
    const auto source_body=live.open_part(f.source.document_id)->session.calculated_boundaries().data();
    const auto owner_path=assembly::InstancePath{{f.target_path.occurrence_ids.front()}}.encoded();
    run(host,"component.activate",{{"instance_path",owner_path}});run(host,"undo");
    require(live.open_assembly(f.inner.document_id)->session.document().name==f.inner.name&&
        live.open_assembly(f.inner.document_id)->session.document().dependencies.size()==1,
        "Assembly Undo removed the summary of a still-existing Part reference");
    require(workspace::document_sketch(live,f.target.document_id,f.sketch_id).serialized()==stored&&
        live.open_part(f.target.document_id)->session.data_generation()==part_generation&&
        live.open_part(f.source.document_id)->session.data_generation()==source_generation&&
        live.open_part(f.source.document_id)->session.calculated_boundaries().data()==source_body,
        "Assembly Undo changed or recalculated a source Part");
    run(host,"redo");require(live.open_assembly(f.inner.document_id)->session.document().dependencies.size()==1,"Assembly Redo lost the current Part dependency");
    renamed=live.open_assembly(f.inner.document_id)->session.document();renamed.name="Second rename";
    live.open_assembly(f.inner.document_id)->session.commit(std::move(renamed));
    run(host,"component.activate",{{"instance_path",f.target_path.encoded()}});
    run(host,"sketch.reference.delete",{{"sketch",f.sketch_id},{"reference",reference}});
    run(host,"component.activate",{{"instance_path",owner_path}});run(host,"undo");
    require(live.open_assembly(f.inner.document_id)->session.document().name=="First rename"&&
        live.open_assembly(f.inner.document_id)->session.document().dependencies.empty(),"Assembly Undo restored a deleted Part reference summary");
    run(host,"redo");require(live.open_assembly(f.inner.document_id)->session.document().dependencies.empty(),"Assembly Redo restored a deleted dependency");
}
void closed_context_regeneration(const fs::path& dir) {
    kernel::OcctKernel kernel;test_support::ContextReferenceFixture f(kernel,dir);
    f.inner.cuts.clear();f.inner.sketches.clear();f.inner.save(dir/"context-inner.asmz");
    workspace::Workspace live;f.load(live);
    require(live.remove(f.top.document_id)&&live.remove(f.inner.document_id),"Cannot close original context");
    auto detached=live.open_part(f.target.document_id)->session.document();detach_all(detached);
    workspace::commit_part_document(live,f.target.document_id,std::move(detached),{});
    live.open_part(f.target.document_id)->session.document().save(dir/"context-target.prtz",{});
    require(live.remove(f.target.document_id)&&live.remove(f.source.document_id),"Cannot close source Parts");
    live.add_assembly(f.top,dir/"context-top.asmz");
    workspace::regenerate_assembly(live,kernel,f.top.document_id);
    const auto* inner=live.open_assembly(f.inner.document_id);
    require(inner&&inner->session.document().dependencies.empty()&&inner->session.is_dirty(),
        "Explicit regeneration did not reconcile the closed native dependency owner");
    require(!live.open_part(f.target.document_id)&&!live.open_part(f.source.document_id),"Reference summary inspection opened source Part tabs");
    inner->session.document().save(dir/"context-inner.asmz");
    require(assembly::AssemblyDocument::load(dir/"context-inner.asmz").dependencies.empty(),"Reconciled native owner did not persist");
}
void reverse_after_detach(const fs::path& dir) {
    kernel::OcctKernel kernel;test_support::ContextReferenceFixture f(kernel,dir);workspace::Workspace live;f.load(live);
    require(live.remove(f.top.document_id)&&live.remove(f.inner.document_id),"Cannot close reverse-reference context");
    auto detached=live.open_part(f.target.document_id)->session.document();detach_all(detached);
    workspace::commit_part_document(live,f.target.document_id,std::move(detached),{});
    live.add_assembly(f.inner,dir/"context-inner.asmz");live.add_assembly(f.top,dir/"context-top.asmz");
    live.activate(f.top.document_id);live.display_top_level(f.top.document_id);
    auto source=live.open_part(f.source.document_id)->session.document();auto sketch=sketcher::Sketch::create_default();
    auto container=document::PartDocument::create_sketch_container();sketch.owner_container_id=container.id;
    const auto sketch_id=sketch.id;workspace::insert_new_sketch(source,std::move(sketch),std::move(container));
    workspace::commit_part_document(live,f.source.document_id,std::move(source),f.calculated);
    auto cwd=dir;command_host::Host host(live,kernel,cwd);run(host,"component.activate",{{"instance_path",f.source_path.encoded()}});
    const auto origin=f.target.origin_viewer_mesh().original_references.points.front().reference;
    run(host,"sketch.reference.create",{{"sketch",sketch_id},{"kind","point"},{"owner",origin.owner_id},
        {"key",origin.semantic_key},{"instance_path",f.target_path.encoded()}});
    const auto& dependencies=live.open_assembly(f.inner.document_id)->session.document().dependencies;
    require(dependencies.size()==1&&dependencies.front().dependent_occurrence_id==f.source_path.occurrence_ids.back()&&
        dependencies.front().prerequisite_occurrence_id==f.target_path.occurrence_ids.back(),"Stale forward summary blocked or survived a valid reverse reference");
}
void unknown_history(const fs::path& dir) {
    kernel::OcctKernel kernel;test_support::ContextReferenceFixture f(kernel,dir);
    const auto forward=f.inner.dependencies.front();f.inner.dependencies.clear();
    workspace::Workspace live;f.load(live);
    auto current=f.inner;current.name="Changed";current.dependencies={forward};
    live.open_assembly(f.inner.document_id)->session.commit(current);
    require(live.remove(f.target.document_id)&&fs::remove(dir/"context-target.prtz"),"Cannot hide target Part fixture");
    require(workspace::step_assembly_document_history(live,f.inner.document_id,false),"Missing Part prevented conservative Undo");
    require(live.open_assembly(f.inner.document_id)->session.document().dependencies==current.dependencies,
        "Unknown source lost a current dependency absent from the historical candidate");
    require(workspace::step_assembly_document_history(live,f.inner.document_id,true),"Missing Part prevented conservative Redo");

    auto reverse=f.inner;reverse.dependencies={assembly::AssemblyDocument::create_dependency(
        forward.prerequisite_occurrence_id,forward.dependent_occurrence_id,assembly::ComponentDependencyKind::ExternalSketchReference)};
    live.open_assembly(f.inner.document_id)->session.replace(reverse);
    live.open_assembly(f.inner.document_id)->session.commit(current);
    require(live.remove(f.source.document_id)&&fs::remove(dir/"context-source.prtz"),"Cannot hide source Part fixture");
    const auto generation=live.open_assembly(f.inner.document_id)->session.data_generation();
    const auto revision=live.open_assembly(f.inner.document_id)->session.revision();
    const auto count=live.size();bool rejected=false;
    try{static_cast<void>(workspace::step_assembly_document_history(live,f.inner.document_id,false));}
    catch(const std::invalid_argument&){rejected=true;}
    const auto& after=live.open_assembly(f.inner.document_id)->session;
    require(rejected&&after.data_generation()==generation&&after.revision()==revision&&after.can_undo()&&!after.can_redo()&&
        after.document().dependencies==current.dependencies&&after.document().name==current.name&&live.size()==count,
        "A cyclic unverified history union partially published the Assembly or history");
    // Open data is authoritative even though both native files are unavailable.
    live.add_part(f.source,f.calculated,dir/"context-source.prtz");live.add_part(f.target,{},dir/"context-target.prtz");
    require(workspace::step_assembly_document_history(live,f.inner.document_id,false)&&
        live.open_assembly(f.inner.document_id)->session.document().dependencies.size()==1&&
        live.open_assembly(f.inner.document_id)->session.document().dependencies.front().dependent_occurrence_id==forward.dependent_occurrence_id,
        "Retry with verifiable Parts did not reconcile and step the same history item");
}
void restored_nested_owner(const fs::path& dir) {
    kernel::OcctKernel kernel;test_support::ContextReferenceFixture f(kernel,dir);
    auto child=f.inner;child.document_id=assembly::AssemblyDocument::create_default().document_id;
    child.dependencies.clear();child.cuts.clear();child.sketches.clear();child.save(dir/"nested-owner.asmz");
    const auto nested=assembly::AssemblyDocument::create_assembly_occurrence("Nested",child.document_id,"nested-owner.asmz",child);
    f.inner.components={nested};f.inner.dependencies.clear();f.inner.cuts.clear();f.inner.sketches.clear();
    for(auto& occurrence:f.top.components)occurrence.nested_snapshot=f.inner.occurrence_snapshot();
    f.source_path.occurrence_ids.insert(f.source_path.occurrence_ids.begin()+1,nested.occurrence_id);
    f.target_path.occurrence_ids.insert(f.target_path.occurrence_ids.begin()+1,nested.occurrence_id);
    for(auto& r:f.target.sketches.front().external_references) {
        r.context_instance_path=f.target_path.encoded();r.source_instance_path=f.source_path.encoded();
    }
    workspace::Workspace live;f.load(live);
    auto removed=f.inner;removed.components.clear();live.open_assembly(f.inner.document_id)->session.commit(removed);
    live.refresh_source_geometry();
    require(!live.resolve_occurrence(f.top.document_id,f.target_path),"Displayed context did not reflect removed nested owner");
    const auto original=live.open_part(f.source.document_id)->session.calculated_boundaries().data();
    require(workspace::step_assembly_document_history(live,f.inner.document_id,false),"Cannot restore nested owner");
    const auto* owner=live.open_assembly(child.document_id);
    require(owner&&owner->session.is_dirty()&&owner->session.document().dependencies.size()==1&&
        owner->session.document().dependencies.front().dependent_occurrence_id==f.target_path.occurrence_ids.back(),
        "Undo resolved references against the stale display instead of the candidate hierarchy");
    require(live.open_part(f.source.document_id)->session.calculated_boundaries().data()==original,
        "Restoring the dependency owner copied or recalculated the source Part");
    require(workspace::step_assembly_document_history(live,f.inner.document_id,true)&&
        live.open_assembly(f.inner.document_id)->session.document().components.empty(),"Reference repair consumed Assembly Redo");
}

}
int main(){try{const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-assembly-reference-summary-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create test folder");assembly_history(dir);closed_context_regeneration(dir);reverse_after_detach(dir);unknown_history(dir);restored_nested_owner(dir);
    require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);
    std::cout<<"Assembly reference summary history and native regeneration passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
