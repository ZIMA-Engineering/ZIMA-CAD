#include <zima/workspace/drawing_sources.hpp>
#include <zima/workspace/metadata_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/workspace/document_operations.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <zima/command_host/host.hpp>
#include <cmath>
#include <iostream>
using namespace zima;
namespace fs=std::filesystem;
namespace {
void require(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
double volume(const workspace::Workspace& live,const std::string& id){return live.open_part(id)->session.calculated_boundaries().back().volume;}
void test(const kernel::OcctKernel& kernel,const fs::path& directory) {
    auto base=document::PartDocument::create_default();base.name="Block";
    auto box=document::PartDocument::create_box_container();box.box={10,8,6};box.name="Stock";
    auto cut=document::PartDocument::create_box_container();cut.box={2,2,6};cut.placement.x=2;cut.name="Cut";cut.combine_mode=document::CombineMode::Subtract;
    auto other=document::PartDocument::create_box_container();other.box={2,2,2};other.placement.x=30;
    base.history={box,cut,other};document::BodyHistoryGraph graph;const auto first=graph.create_body("Main");
    graph.insert({document::PartHistoryKind::Feature,box.id});graph.insert({document::PartHistoryKind::Feature,cut.id});
    const auto second=graph.create_body("Second");graph.insert({document::PartHistoryKind::Feature,other.id});graph.activate({});base.set_body_history(graph);base.synchronize_dimension_identifiers();
    workspace::Workspace live;const auto id=base.document_id;
    auto cache=kernel.evaluate_history(base.kernel_operations());
    live.add_part(base,cache,directory/"base.prtz");require(std::abs(volume(live,id)-464)<1e-8,"Generic volume must be 480 - 24 + 8");
    const auto refs=workspace::family_references(live,id);
    const auto find=[&](const std::string& owner,const std::string& key){for(const auto& r:refs)if(r.binding.owner_id==owner&&r.binding.semantic_key==key)return r;throw std::runtime_error("Missing family reference");};
    const auto length=find(box.id,"parameter:length"),presence=find(cut.id,""),body=find(second,"");
    require(length.name==base.dimension_identifiers.identifier(box.id,"parameter:length"),"Family dimension must use its stable secondary name");
    document::FamilyTable table;table.columns={length.name,presence.name,body.name};
    for(const auto& r:{length,presence,body})table.bindings[r.name]=r.binding;
    table.instances={{"Long",{{length.name,"20"}}},{"No cut",{{length.name,"20"},{presence.name,"no"}}},{"One body",{{length.name,"20"},{body.name,"no"}}}};
    const auto original=live.open_part(id)->session.calculated_boundaries().back().kernel_shape;
    require(workspace::set_family_table(live,id,table),"Family set failed");
    table=workspace::family_table(live,id);
    require(!workspace::set_family_table(live,id,table),"Family no-op created history");
    require(live.open_part(id)->session.calculated_boundaries().back().kernel_shape==original,"Table editing calculated geometry");
    live.open_part(id)->session.undo();require(workspace::family_table(live,id).instances.empty(),"Family Undo failed");live.open_part(id)->session.redo();
    live.open_part(id)->session.document().save(directory/"base.prtz",cache);
    const auto loaded=document::PartDocument::load(directory/"base.prtz");require(document::parse_family_table(loaded.family_table)==table,"Family identities and references did not persist");
    const auto variant=workspace::open_family_instance(live,kernel,id,"Long");
    require(std::abs(volume(live,variant)-944)<1e-8 && std::abs(volume(live,id)-464)<1e-8,"Variant dimensions changed the generic or copied the wrong operand");
    require(workspace::open_family_instance(live,kernel,id,"Long")==variant && live.size()==2,"Opening a row duplicated its tab");
    auto* held_member=live.open_part(variant);
    for(const auto& name:{"First rename","Second rename"}) {auto model=held_member->session.document();model.name=name;held_member->session.commit(std::move(model),held_member->session.calculated_boundaries());}
    require(workspace::family_table(live,id).instances.front().name=="Second rename","Repeated edits through the same open session lost family ownership");
    static_cast<void>(workspace::set_family_table(live,id,table));
    const auto previous_generation=live.open_part(variant)->session.data_generation();
    const auto previous_revision=live.open_part(variant)->session.revision();
    auto observing=assembly::AssemblyDocument::create_default();const auto observing_id=observing.document_id;live.add_assembly(observing);
    const auto observing_occurrence=live.insert_open_part(observing_id,variant,"Variant");
    auto revised=table;revised.instances.front().values[length.name]="22";
    static_cast<void>(workspace::set_family_table(live,id,revised));
    require(workspace::open_family_instance(live,kernel,id,"Long")==variant&&std::abs(volume(live,variant)-1040)<1e-8,"Opening an updated row did not regenerate its existing tab");
    bool nested_rejected=false;try {auto child=live.open_part(variant)->session.document();child.family_table=document::serialize_family_table(table);live.open_part(variant)->session.commit(std::move(child),live.open_part(variant)->session.calculated_boundaries());}catch(const std::exception&){nested_rejected=true;}
    require(nested_rejected,"An instance accepted its own nested Family Table");
    require(live.open_part(variant)->session.data_generation()>previous_generation&&live.open_part(variant)->session.revision()>previous_revision,"Family edit reset the instance revision or viewer generation");
    live.refresh_source_geometry();
    require(std::abs(live.open_assembly(observing_id)->session.document().find_occurrence(observing_occurrence)->calculated_source->volume-1040)<1e-8,"Assembly retained stale geometry after a family edit");
    static_cast<void>(workspace::close_document(live,observing_id,true));
    const auto no_cut=workspace::open_family_instance(live,kernel,id,"No cut");
    const auto one=workspace::open_family_instance(live,kernel,id,"One body");
    require(std::abs(volume(live,no_cut)-968)<1e-8,"Absent cut remains");
    require(std::abs(volume(live,one)-936)<1e-8,"Absent Body remains");
    auto edited=live.open_part(variant)->session.document();edited.find_container(box.id)->box.length=24;
    auto changed=workspace::calculate_part_with_resolved_references(kernel,edited,nullptr,{true});
    live.open_part(variant)->session.commit(edited,std::move(changed));
    require(workspace::family_table(live,id).instances.front().values.at(length.name)=="24","Instance edit did not update its parent table row");
    require(live.open_part(id)->session.document().find_container(box.id)->box.length==10,"Row override modified base dimension");
    require(live.open_part(no_cut)->session.document().find_container(box.id)->box.length==20,"Row override changed sibling");
    edited=live.open_part(variant)->session.document();edited.find_container(box.id)->box.width=9;
    changed=workspace::calculate_part_with_resolved_references(kernel,edited,nullptr,{true});
    live.open_part(variant)->session.commit(edited,std::move(changed));
    for(const auto& member:{id,variant,no_cut,one})require(live.open_part(member)->session.document().find_container(box.id)->box.width==9,"Shared dimension did not update every family member");
    require(workspace::step_document_history(live,variant,workspace::HistoryDirection::Undo),"Instance Undo failed");
    for(const auto& member:{id,variant,no_cut,one})require(live.open_part(member)->session.document().find_container(box.id)->box.width==8,"Family Undo was not shared");
    require(workspace::step_document_history(live,id,workspace::HistoryDirection::Redo),"Parent Redo failed");
    require(live.open_part(no_cut)->session.document().find_container(box.id)->box.width==9,"Family Redo was not shared");
    // A structural edit is common history, including a new subtractive feature.
    edited=live.open_part(variant)->session.document();
    auto added=document::PartDocument::create_box_container();added.box={1,1,1};added.placement={1,1,1};added.combine_mode=document::CombineMode::Subtract;
    edited.history.push_back(added);auto added_graph=edited.body_history;added_graph.activate(first);added_graph.set_insertion_cursor(added_graph.find(first)->entries.size());added_graph.insert({document::PartHistoryKind::Feature,added.id});added_graph.activate({});edited.set_body_history(added_graph);
    changed=workspace::calculate_part_with_resolved_references(kernel,edited,nullptr,{true});
    live.open_part(variant)->session.commit(edited,std::move(changed));
    for(const auto& member:{id,variant,no_cut,one})require(live.open_part(member)->session.document().find_container(added.id)!=nullptr,"Instance feature was not shared with parent and siblings");
    require(workspace::step_document_history(live,variant,workspace::HistoryDirection::Undo),"Structural family Undo failed");
    for(const auto& member:{id,variant,no_cut,one})require(!live.open_part(member)->session.document().find_container(added.id),"Structural Undo left a member changed");
    require(workspace::step_document_history(live,id,workspace::HistoryDirection::Undo),"Width Undo failed");
    require(workspace::step_document_history(live,id,workspace::HistoryDirection::Undo),"Row Undo failed");
    static_cast<void>(workspace::set_family_table(live,id,table));
    require(std::abs(volume(live,variant)-944)<1e-8,"Restored family differs");
    bool separate=false;try{live.open_part(variant)->session.document().save(directory/"separate.prtz",live.open_part(variant)->session.calculated_boundaries());}catch(const std::exception&){separate=true;}
    require(separate&&!fs::exists(directory/"separate.prtz"),"Instance was saved as an independent native file");
    static_cast<void>(live.save_copy(variant,directory/"copy.prtz"));
    const auto independent=document::PartDocument::load(directory/"copy.prtz");
    require(independent.family.parent_id.empty()&&independent.document_id!=variant&&independent.find_container(box.id)->box.length==20,"Save As did not create a detached copy of the current variant");
    const auto saved=workspace::prepare_document_save(live,variant,directory/"base.prtz").write();require(workspace::complete_document_save(live,saved),"Saving through instance failed");
    require(!workspace::document_needs_save(live,variant)&&!workspace::document_needs_save(live,id),"Family Save left inconsistent dirty state");
    std::vector<kernel::BodyResult> cold_cache;auto cold=document::PartDocument::load(directory/"base.prtz",&cold_cache);
    auto cold_variant=workspace::family_part_source(cold,cold_cache,variant);
    require(cold_variant.family.parent_id==id&&std::abs(cold_cache.back().volume-944)<1e-8,"Single native file did not retain evaluated instance");
    const auto [source_id,source_mesh]=workspace::read_drawing_source(nullptr,directory/"base.prtz",variant);
    require(source_id==variant&&!source_mesh.triangles.empty(),"Cold Drawing source did not resolve parent plus row");
    auto drawing=drawing::DrawingDocument::create_default();drawing.source_document_id=id;drawing.source_path=directory/"base.prtz";
    drawing.sheets.front().views.push_back(drawing::DrawingDocument::create_view(id,drawing.source_path,cache.back().mesh));
    workspace::select_family_drawing_source(drawing,live,variant,directory/"long.drwz");
    require(drawing.source_document_id==variant&&drawing.source_path==directory/"base.prtz","Drawing did not use variant in parent's file");
    drawing.save(directory/"long.drwz");const auto drw=drawing::DrawingDocument::load(directory/"long.drwz");
    require(drw.source_document_id==variant,"Drawing lost instance identity");
    auto parameters=workspace::user_parameters(live,variant);parameters.flat["name"]="Long renamed";parameters.values["name"][""]="Long renamed";
    const auto shape=live.open_part(variant)->session.calculated_boundaries().back().kernel_shape;
    static_cast<void>(workspace::set_user_parameters(live,variant,std::move(parameters)));
    require(workspace::family_table(live,id).instances.front().name=="Long renamed"&&live.open_part(variant)->session.document().name=="Long renamed","Instance rename did not update its stable row");
    require(live.open_part(variant)->session.calculated_boundaries().back().kernel_shape==shape,"Renaming a family member recalculated geometry");
    require(workspace::open_family_instance(live,kernel,variant,"No cut")==no_cut,"Opening a sibling from an instance created a nested family");
    const auto rename_saved=workspace::prepare_document_save(live,id,directory/"base.prtz").write();require(workspace::complete_document_save(live,rename_saved),"Rename Save failed");
    const auto renamed=workspace::build_title_block_context_for_source(drw.source_document_id,drw.source_path,nullptr);
    require(renamed.parameters.at("name")=="Long renamed"&&renamed.file_stem=="Long renamed","Closed Drawing did not resolve renamed instance from stable identity");
    require(workspace::read_drawing_source(nullptr,drw.source_path,drw.source_document_id).first==variant,"Rename broke the saved Drawing source");
    const auto member_files=live.save_copy(variant,directory/"renamed-copy.prtz");require(member_files.size()==1,"Instance Save As must create exactly one independent native file");
    auto root_copy=live.save_copy(id,directory/"family-copy.prtz");
    auto copied_family=document::PartDocument::load(directory/"family-copy.prtz");const auto copied_id=copied_family.document_id;
    live.add_part(std::move(copied_family),{},directory/"family-copy.prtz");
    const auto copied_variant=workspace::open_family_instance(live,kernel,copied_id,"Long renamed");
    require(copied_variant.starts_with(copied_id+":family:")&&!copied_variant.starts_with(id+":family:"),"Generic Save As retained old family ownership");
    static_cast<void>(workspace::close_document(live,copied_id,true));
    auto bad=table;bad.instances.push_back({"Invalid",{{length.name,"-2"}}});static_cast<void>(workspace::set_family_table(live,id,bad));
    const auto count=live.size();bool rejected=false;try{workspace::open_family_instance(live,kernel,id,"Invalid");}catch(const std::exception&){rejected=true;}
    require(rejected&&live.size()==count&&std::abs(volume(live,id)-464)<1e-8,"Failed variant left a partial document or changed generic");
    bad=table;bad.bindings[length.name].owner_id="missing";rejected=false;try{static_cast<void>(workspace::set_family_table(live,id,bad));}catch(const std::exception&){rejected=true;}require(rejected,"Dangling family reference accepted");
    auto assembly=assembly::AssemblyDocument::create_default();assembly.name="Assembly";const auto aid=assembly.document_id;live.add_assembly(assembly,directory/"base.asmz");
    const auto component=live.insert_open_part(aid,id,"Block");
    document::FamilyTable assembly_table;assembly_table.columns={"Block"};assembly_table.bindings["Block"]={"component",component,{}};assembly_table.instances={{"Empty",{{"Block","no"}}}};
    static_cast<void>(workspace::set_family_table(live,aid,assembly_table));const auto av=workspace::open_family_instance(live,kernel,aid,"Empty");
    require(live.open_assembly(av)->session.document().find_occurrence(component)->suppressed&&!live.open_assembly(aid)->session.document().find_occurrence(component)->suppressed,"Family component presence mutated owning Assembly");
    auto* held_assembly=live.open_assembly(av);
    for(const auto& name:{"First Assembly rename","Second Assembly rename"}) {auto model=held_assembly->session.document();model.name=name;held_assembly->session.commit(std::move(model));}
    require(workspace::family_table(live,aid).instances.front().name=="Second Assembly rename","Repeated Assembly edits lost family ownership");
    auto changed_assembly=live.open_assembly(av)->session.document();changed_assembly.find_occurrence(component)->name="Shared component";
    live.open_assembly(av)->session.commit(std::move(changed_assembly));
    require(live.open_assembly(aid)->session.document().find_occurrence(component)->name=="Shared component","Assembly member edit did not propagate to parent");
    const auto assembly_saved=workspace::prepare_document_save(live,av,directory/"base.asmz").write();require(workspace::complete_document_save(live,assembly_saved),"Assembly member Save failed");
    const auto loaded_assembly=workspace::read_family_assembly(nullptr,directory/"base.asmz",av);
    require(loaded_assembly.family.parent_id==aid&&loaded_assembly.find_occurrence(component)->suppressed,"Single Assembly file lost evaluated variant");
    auto empty_drawing=drawing::DrawingDocument::create_default();workspace::select_family_drawing_source(empty_drawing,live,av,directory/"assembly.drwz");
    require(empty_drawing.source_document_id==av,"Empty Drawing cannot select an Assembly variant before view insertion");
    static_cast<void>(live.save_copy(av,directory/"assembly-copy.asmz"));require(assembly::AssemblyDocument::load(directory/"assembly-copy.asmz").family.parent_id.empty(),"Assembly Save As copy is still linked");
    live.display_top_level(id);live.activate(id);auto cwd=directory;command_host::Host host(live,kernel,cwd);
    auto query=host.execute({{"command","document.family.references"},{"arguments",{{"document",id}}}});require(query.ok&&!query.data.at("references").empty(),"CLI family reference catalog failed");
    auto opened=host.execute({{"command","document.family.open"},{"arguments",{{"document",id},{"instance","Long"}}}});if(!opened.ok)throw std::runtime_error(opened.code+": "+opened.message);require(opened.data.at("document")==variant,"CLI did not open the same family instance");
}
}
int main(){try{kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-family-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);test(kernel,dir);require(dir.parent_path()==root,"Unsafe test cleanup");fs::remove_all(dir);std::cout<<"Linked Family Table edits, shared history, single-file persistence, drawings, CLI and atomic rejection passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
