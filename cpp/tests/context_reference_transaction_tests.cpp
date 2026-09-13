#include "context_reference_test_support.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/document_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <iostream>
using namespace zima;namespace fs=std::filesystem;using commands::Json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const std::string& name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(name+": "+result.code+": "+result.message);return result;
}
void verify(const fs::path& dir) {
    kernel::OcctKernel kernel;test_support::ContextReferenceFixture fixture(kernel,dir);
    auto& sketch=fixture.target.sketches.front();
    const auto references=sketch.external_references;
    for(const auto& reference:references)sketch.remove_geometry(reference.id);
    fixture.inner.dependencies.clear();fixture.inner.save(dir/"context-inner.asmz");
    workspace::Workspace live;fixture.load(live);auto working_directory=dir;
    command_host::Host host(live,kernel,working_directory);
    run(host,"component.activate",{{"instance_path",fixture.target_path.encoded()}});
    const auto created=run(host,"sketch.reference.create",{{"sketch",fixture.sketch_id},{"kind","edge"},
        {"owner",fixture.edge.reference.owner_id},{"key",fixture.edge.reference.semantic_key},
        {"instance_path",fixture.source_path.encoded()},{"profile",true}}).data;
    const auto current=[&]{return workspace::document_sketch(live,fixture.target.document_id,fixture.sketch_id);};
    require(current().external_references.size()==1,"Context reference was not created");
    const auto& owner=live.open_assembly(fixture.inner.document_id)->session;
    require(owner.document().dependencies.size()==1,"Reference and owner dependency were not committed together");
    require(!owner.can_undo(),"Derived reference summary created an independent Assembly Undo item");
    const auto geometry=current().serialized();run(host,"undo");
    require(current().external_references.empty()&&live.open_assembly(fixture.inner.document_id)->session.document().dependencies.empty(),"Undo left a dangling dependency");
    run(host,"redo");require(current().serialized()==geometry&&live.open_assembly(fixture.inner.document_id)->session.document().dependencies.size()==1,"Redo lost the reference transaction");
    run(host,"component.activate",{{"instance_path",fixture.other_target_path.encoded()}});
    const auto wrong=host.execute({{"command","sketch.reference.create"},{"arguments",{{"sketch",fixture.sketch_id},{"kind","edge"},
        {"owner",fixture.edge.reference.owner_id},{"key",fixture.edge.reference.semantic_key},{"instance_path",fixture.source_path.encoded()}}}});
    require(!wrong.ok&&wrong.code=="context_reference"&&current().serialized()==geometry,"Repeated occurrence changed the stored reference context");
    run(host,"component.activate",{{"instance_path",fixture.target_path.encoded()}});
    const auto points=current().points;
    run(host,"sketch.reference.delete",{{"sketch",fixture.sketch_id},{"reference",created.at("reference")}});
    require(current().external_references.empty()&&current().points==points,"Detach destroyed projected geometry");
    require(live.open_assembly(fixture.inner.document_id)->session.document().dependencies.empty(),"Detach left a stale dependency");
    // All preparation can succeed yet the final Part validation must reject.
    // Even a closed owner must remain closed when that happens.
    auto candidate=live.open_part(fixture.target.document_id)->session.document();
    candidate.sketches.front().add_external_reference(workspace::prepare_sketch_external_reference(live,fixture.target.document_id,
        candidate.sketches.front(),sketcher::ExternalReferenceKind::Edge,fixture.edge.reference.owner_id,fixture.edge.reference.semantic_key,fixture.source_path.encoded()));
    auto invalid=candidate;invalid.document_units["Length"]="unknown";
    require(live.remove(fixture.inner.document_id),"Cannot close common owner");
    const auto count=live.size(),revision=live.open_part(fixture.target.document_id)->session.revision(),generation=live.open_part(fixture.target.document_id)->session.data_generation();
    const auto retained=current().serialized();bool rejected=false;
    try{workspace::commit_part_document(live,fixture.target.document_id,std::move(invalid),{});}catch(const std::exception&){rejected=true;}
    require(rejected&&live.size()==count&&!live.open_assembly(fixture.inner.document_id)&&current().serialized()==retained&&
        live.open_part(fixture.target.document_id)->session.revision()==revision&&live.open_part(fixture.target.document_id)->session.data_generation()==generation,
        "Rejected Part commit published an owner, reference or revision");
    workspace::commit_part_document(live,fixture.target.document_id,std::move(candidate),{});
    require(live.size()==count+1&&live.open_assembly(fixture.inner.document_id)->session.document().dependencies.size()==1&&
        live.active_document_id()==fixture.target.document_id&&live.active_occurrence_path()==fixture.target_path.encoded(),
        "Closed owner publication lost exact editing focus or dependency");
    run(host,"undo");
    // A later source edit can make restoring an old reference cyclic. Reject
    // before moving Part history or its owner's dependency summary.
    auto source=live.open_part(fixture.source.document_id)->session.document();auto cycle=sketcher::Sketch::create_default();
    auto back=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Point);
    back.source_document_id=fixture.target.document_id;back.source_owner_id=fixture.target.history.front().id;
    back.source_semantic_key="origin";back.cached_points={{0,0}};cycle.add_external_reference(back);source.sketches.push_back(cycle);
    live.open_part(fixture.source.document_id)->session.commit(std::move(source),fixture.calculated);
    const auto before_redo=current().serialized();const auto redo_revision=live.open_part(fixture.target.document_id)->session.revision();
    const auto failed=host.execute({{"command","redo"}});
    require(!failed.ok&&current().serialized()==before_redo&&live.open_part(fixture.target.document_id)->session.can_redo()&&
        live.open_part(fixture.target.document_id)->session.revision()==redo_revision&&live.open_assembly(fixture.inner.document_id)->session.document().dependencies.empty(),
        "Cyclic Redo partially changed history or dependencies");
 }
void create_all_kinds(const fs::path& dir) {
    kernel::OcctKernel kernel;test_support::ContextReferenceFixture f(kernel,dir);
    const auto expected=f.target.sketches.front().external_references;
    for(const auto& r:expected)f.target.sketches.front().remove_geometry(r.id);
    f.inner.dependencies.clear();workspace::Workspace live;f.load(live);auto cwd=dir;command_host::Host host(live,kernel,cwd);
    run(host,"component.activate",{{"instance_path",f.target_path.encoded()}});
    const auto generation=live.open_assembly(f.inner.document_id)->session.data_generation();
    for(const auto& r:expected) {
        using Kind=sketcher::ExternalReferenceKind;
        const auto name=r.kind==Kind::Edge?"edge":r.kind==Kind::Point?"point":r.kind==Kind::Axis?"axis":"face";
        const auto added=run(host,"sketch.reference.create",{{"sketch",f.sketch_id},{"kind",name},{"owner",r.source_owner_id},
            {"key",r.source_semantic_key},{"instance_path",r.source_instance_path},{"profile",r.kind==Kind::Edge}}).data;
        const auto sketch=workspace::document_sketch(live,f.target.document_id,f.sketch_id);const auto& actual=sketch.external_references.back();
        require(!added.at("body_calculated").get<bool>()&&!actual.broken&&actual.kind==r.kind&&actual.source_owner_id==r.source_owner_id&&
            actual.source_semantic_key==r.source_semantic_key&&actual.source_document_id==f.source.document_id&&actual.context_instance_path==f.target_path.encoded(),
            "Context creation lost the reference kind, original identity or exact occurrence");
        require(actual.cached_points.size()==r.cached_points.size(),"Context projection changed the reference representation");
        for(std::size_t i=0;i<r.cached_points.size();++i)require(std::hypot(actual.cached_points[i][0]-r.cached_points[i][0],actual.cached_points[i][1]-r.cached_points[i][1])<1e-8,
            "Context projection disagrees with the independently transformed source");
        require(live.open_assembly(f.inner.document_id)->session.document().dependencies.size()==1&&
            live.open_assembly(f.inner.document_id)->session.data_generation()==generation+1,"Shared source references created repeated owner transactions");
    }
    run(host,"save");const auto saved=document::PartDocument::load(dir/"context-target.prtz");
    require(saved.sketches.front().external_references.size()==4,"Native Part did not retain all four created reference kinds");
    for(const auto& r:saved.sketches.front().external_references)run(host,"sketch.reference.delete",{{"sketch",f.sketch_id},{"reference",r.id}});
    require(live.open_assembly(f.inner.document_id)->session.document().dependencies.empty(),"Last shared reference did not remove its owner summary");
}
void closed_sibling(const fs::path& dir) {
    kernel::OcctKernel kernel;test_support::ContextReferenceFixture f(kernel,dir);
    auto sibling=document::PartDocument::create_default();
    auto container=document::PartDocument::create_sketch_container();auto sketch=sketcher::Sketch::create_default();
    sketch.owner_container_id=container.id;sibling.history={container};
    const auto sibling_occurrence=assembly::AssemblyDocument::create_part_occurrence("Closed sibling",sibling.document_id,"sibling.prtz",{});
    const auto source=f.inner.components.front(),target=f.inner.components.back();
    f.inner.components={target,sibling_occurrence};f.inner.dependencies.clear();f.inner.cuts.clear();f.inner.sketches.clear();
    auto group=assembly::AssemblyDocument::create_assembly_occurrence("Dependent branch",f.inner.document_id,"context-inner.asmz",f.inner);
    f.top.components={group,source};f.target_path={{group.occurrence_id,target.occurrence_id}};f.source_path={{source.occurrence_id}};
    const assembly::InstancePath sibling_path{{group.occurrence_id,sibling_occurrence.occurrence_id}};
    auto r=f.target.sketches.front().external_references.front();r.source_instance_path=f.source_path.encoded();r.context_instance_path=f.target_path.encoded();
    f.target.sketches.front().external_references={r};r.id=sketcher::Sketch::create_external_reference(r.kind).id;r.context_instance_path=sibling_path.encoded();
    sketch.add_external_reference(r);sibling.sketches={sketch};sibling.save(dir/"sibling.prtz",{});
    f.top.add_dependency(assembly::AssemblyDocument::create_dependency(group.occurrence_id,source.occurrence_id,assembly::ComponentDependencyKind::ExternalSketchReference));
    f.inner.save(dir/"context-inner.asmz");workspace::Workspace live;f.load(live);
    auto cwd=dir;command_host::Host host(live,kernel,cwd);run(host,"component.activate",{{"instance_path",f.target_path.encoded()}});
    run(host,"sketch.reference.delete",{{"sketch",f.sketch_id},{"reference",f.reference_id}});
    require(!live.open_part(sibling.document_id)&&live.open_assembly(f.top.document_id)->session.document().dependencies.size()==1,
        "Deleting one Part reference erased the closed sibling's branch dependency");
    live.synchronize_external_sketch_dependencies();
    require(live.open_assembly(f.top.document_id)->session.document().dependencies.size()==1,"Reconciliation ignored the closed sibling");
    const auto stored=sibling.sketches.front().external_references.front().id;sibling.sketches.front().remove_geometry(stored);sibling.save(dir/"sibling.prtz",{});
    live.synchronize_external_sketch_dependencies();
    require(live.open_assembly(f.top.document_id)->session.document().dependencies.empty(),"Reconciliation retained a provably unused dependency");
    // A missing sibling is unknown, not proof that an existing edge is unused.
    auto top=live.open_assembly(f.top.document_id)->session.document();
    top.add_dependency(assembly::AssemblyDocument::create_dependency(group.occurrence_id,source.occurrence_id,assembly::ComponentDependencyKind::ExternalSketchReference));
    live.open_assembly(f.top.document_id)->session.commit(std::move(top));
    require(fs::remove(dir/"sibling.prtz"),"Cannot remove owned fixture");live.synchronize_external_sketch_dependencies();
    require(live.open_assembly(f.top.document_id)->session.document().dependencies.size()==1,"Missing source erased an unverifiable dependency");
    auto reordered=live.open_assembly(f.inner.document_id)->session.document();
    std::reverse(reordered.components.begin(),reordered.components.end());
    live.open_assembly(f.inner.document_id)->session.commit(std::move(reordered));
    auto empty=live.open_assembly(f.top.document_id)->session.document();empty.dependencies.clear();
    live.open_assembly(f.top.document_id)->session.commit(std::move(empty));
    run(host,"sketch.reference.create",{{"sketch",f.sketch_id},{"kind","edge"},{"owner",f.edge.reference.owner_id},
        {"key",f.edge.reference.semantic_key},{"instance_path",f.source_path.encoded()}});
    require(live.open_assembly(f.top.document_id)->session.document().dependencies.size()==1,
        "Missing first sibling hid the new candidate Part dependency");
}
}
int main(){try{const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-context-transaction-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create test folder");verify(dir);create_all_kinds(dir);closed_sibling(dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);
    std::cout<<"Context reference transactions passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
