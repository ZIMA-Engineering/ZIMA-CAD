#include <zima/workspace/drawing_sources.hpp>
#include <zima/workspace/metadata_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/document/bend.hpp>
#include <zima/workspace/bend_operations.hpp>
#include <zima/workspace/document_operations.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <zima/workspace/component_operations.hpp>
#include <zima/workspace/component_source_operations.hpp>
#include <zima/command_host/host.hpp>
#include <cmath>
#include <iostream>
using namespace zima;
namespace fs=std::filesystem;
namespace {
void require(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
double volume(const workspace::Workspace& live,const std::string& id){return live.open_part(id)->session.calculated_boundaries().back().volume;}
void bend_state_test(const kernel::OcctKernel& kernel,const fs::path& directory) {
    workspace::Workspace live;auto part=document::PartDocument::create_default();
    auto bend=document::PartDocument::create_sketch_container();bend.feature_kind=document::FeatureKind::Bend;
    auto start=sketcher::Sketch::create_default();start.owner_container_id=bend.id;
    document::initialize_bend_start_profile(start,30);bend.bend.sketch_id=start.id;
    part.history={bend};part.sketches={start};part.resolve_constructions();
    const auto id=part.document_id;live.add_part(part,kernel.evaluate_history(part.kernel_operations()));
    const auto references=workspace::family_references(live,id);
    const auto state=std::ranges::find_if(references,[&](const auto& r){return r.binding.owner_id==bend.id&&r.binding.semantic_key=="parameter:unbend";});
    require(state!=references.end()&&state->value=="0","Bend state is absent from Family references");
    document::FamilyTable table;table.columns={state->name};table.bindings[state->name]=state->binding;
    table.instances={{"Folded",{{state->name,"0"}}},{"Developed",{{state->name,"1"}}},{"Inherited",{}}};
    static_cast<void>(workspace::set_family_table(live,id,table));
    const auto folded=workspace::open_family_instance(live,kernel,id,"Folded",false);
    const auto developed=workspace::open_family_instance(live,kernel,id,"Developed",false);
    const auto inherited=workspace::open_family_instance(live,kernel,id,"Inherited",false);
    const auto unbend=[&](const std::string& owner){return live.open_part(owner)->session.document().find_container(bend.id)->bend.unbend;};
    require(!unbend(id)&&!unbend(folded)&&unbend(developed)&&!unbend(inherited),"Family Bend states were not independent");
    auto* member=live.open_part(developed);auto edited=*member->session.document().find_container(bend.id);edited.bend.unbend=false;
    static_cast<void>(workspace::commit_bend(live,kernel,developed,edited,member->session.document().sketches.front()));
    require(!unbend(id)&&!unbend(developed),"Editing instance state changed the generic Bend");
    auto* generic=live.open_part(id);edited=*generic->session.document().find_container(bend.id);edited.bend.unbend=true;
    static_cast<void>(workspace::commit_bend(live,kernel,id,edited,generic->session.document().sketches.front()));
    require(unbend(id)&&!unbend(folded)&&!unbend(developed)&&unbend(inherited),"Bend overrides or inheritance were lost");
    const auto path=directory/"bend-state.prtz";generic=live.open_part(id);
    generic->session.document().save(path,generic->session.calculated_boundaries());
    std::vector<kernel::BodyResult> cache;auto cold=document::PartDocument::load(path,&cache);
    const auto reopened=workspace::family_part_source(cold,cache,developed);
    require(!reopened.find_container(bend.id)->bend.unbend&&cold.find_container(bend.id)->bend.unbend,"Cold Family state changed");
    auto bad=table;bad.instances.front().values[state->name]="2";bool rejected=false;
    try {static_cast<void>(workspace::set_family_table(live,id,bad));}catch(const std::exception&){rejected=true;}
    require(rejected,"Invalid Bend state accepted");
}
void component_test(const kernel::OcctKernel& kernel,const fs::path& directory) {
    workspace::Workspace live;auto part=document::PartDocument::create_default();part.name="Family component";
    const auto root=part.document_id;auto box=document::PartDocument::create_box_container();box.box={10,8,6};part.history={box};
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Body"));graph.insert({document::PartHistoryKind::Feature,box.id});part.set_body_history(graph);part.synchronize_dimension_identifiers();
    const auto file=directory/"component-family.prtz";live.add_part(part,kernel.evaluate_history(part.kernel_operations()),file);
    document::FamilyTable table;table.columns={"Length","Stock"};table.bindings["Length"]={"dimension",box.id,"parameter:length"};table.bindings["Stock"]={"feature",box.id,{}};
    table.instances={{"Long",{{"Length","20"}}},{"Short",{{"Length","5"}}},{"Empty",{{"Stock","no"}}}};
    static_cast<void>(workspace::set_family_table(live,root,table));
    auto assembly=assembly::AssemblyDocument::create_default();const auto owner=assembly.document_id;const auto assembly_file=directory/"component-owner.asmz";live.add_assembly(assembly,assembly_file);live.display_top_level(owner);live.activate(owner);
    const auto long_id=workspace::open_family_instance(live,kernel,root,"Long",false);
    const auto short_id=workspace::open_family_instance(live,kernel,root,"Short",false);
    const auto empty_id=workspace::open_family_instance(live,kernel,root,"Empty",false);
    require(live.active_document_id()==owner&&live.displayed_document_id()==owner,"Variant selection changed the insertion context");
    const auto first=workspace::insert_component(live,owner,root),second=workspace::insert_component(live,owner,root,"Custom occurrence");
    auto placed=live.open_assembly(owner)->session.document();auto* moving=placed.find_occurrence(first);moving->placement.x=25;moving->value_locks.insert("x");
    assembly::ComponentPlacementReference mate;mate.component_reference={assembly::MateReferenceKind::Face,assembly::InstancePath{}.child(first),root+":origin","origin:plane:xy"};mate.target_reference={assembly::MateReferenceKind::Face,assembly::InstancePath{}.child(second),root+":origin","origin:plane:xy"};moving->placement_references.push_back(mate);placed.calculate_placement_references();live.open_assembly(owner)->session.commit(placed);
    require(workspace::replace_component(live,kernel,owner,first,long_id),"Replace failed");
    const auto& replaced=live.open_assembly(owner)->session.document();const auto* changed=replaced.find_occurrence(first);
    require(changed->source_document_id==long_id&&changed->source_path==file&&std::abs(changed->calculated_source->volume-960)<1e-8,"Replace selected the wrong variant");
    require(std::abs(changed->placement.x-placed.find_occurrence(first)->placement.x)<1e-8,"Replace moved an unconstrained coordinate");
    require(changed->visible==placed.find_occurrence(first)->visible&&changed->value_locks==placed.find_occurrence(first)->value_locks,"Replace lost occurrence flags or locks");
    require(changed->placement_references.size()==1&&replaced.resolve_plane(changed->placement_references[0].component_reference).status==assembly::MateStatus::Valid,"Replace lost the origin mate");
    require(replaced.find_occurrence(second)->source_document_id==root&&replaced.find_occurrence(second)->name=="Custom occurrence","Replace changed another occurrence");
    require(workspace::step_document_history(live,owner,workspace::HistoryDirection::Undo)&&live.open_assembly(owner)->session.document().find_occurrence(first)->source_document_id==root,"Replace is not one Undo step");
    require(workspace::step_document_history(live,owner,workspace::HistoryDirection::Redo)&&live.open_assembly(owner)->session.document().find_occurrence(first)->source_document_id==long_id,"Replace Redo lost identity");
    auto cwd=directory;command_host::Host host(live,kernel,cwd);
    const auto cli=host.execute({{"command","component.replace"},{"arguments",{{"source",root},{"instance_path",assembly::InstancePath{}.child(first).encoded()}}}});
    require(cli.ok&&live.open_assembly(owner)->session.document().find_occurrence(first)->source_document_id==root,"CLI cannot replace an instance by the generic");
    static_cast<void>(workspace::replace_component(live,kernel,owner,first,long_id));
    static_cast<void>(workspace::replace_component(live,kernel,owner,second,short_id));
    // Missing feature topology keeps the new variant and the repairable old reference.
    auto face_mate=live.open_assembly(owner)->session.document();auto& row=face_mate.find_occurrence(first)->placement_references.front();
    for(const auto& ref:live.open_part(long_id)->session.calculated_boundaries().back().mesh.original_references.triangle_references)
        if(ref.owner_id==box.id){row.component_reference.owner_id=ref.owner_id;row.component_reference.semantic_key=ref.semantic_key;break;}
    require(row.component_reference.owner_id==box.id&&face_mate.resolve_plane(row.component_reference).status==assembly::MateStatus::Valid,"Missing-reference fixture lacks an original face");
    face_mate.calculate_placement_references();live.open_assembly(owner)->session.commit(face_mate);const auto before_failure=live.open_assembly(owner)->session.revision();
    require(workspace::replace_component(live,kernel,owner,first,empty_id),"Missing mate geometry blocked Replace");
    const auto& missing=live.open_assembly(owner)->session.document();const auto* unresolved=missing.find_occurrence(first);
    require(missing.resolve_plane(unresolved->placement_references.front().component_reference).status==assembly::MateStatus::MissingReference&&unresolved->placement_references==face_mate.find_occurrence(first)->placement_references&&live.open_assembly(owner)->session.revision()>before_failure,"Replace discarded or rebound the missing mate reference");
    require(workspace::step_document_history(live,owner,workspace::HistoryDirection::Undo)&&live.open_assembly(owner)->session.document().find_occurrence(first)->source_document_id==long_id,"Unresolved Replace cannot be undone");
    const auto saved=workspace::prepare_document_save(live,root,file).write();static_cast<void>(workspace::complete_document_save(live,saved));
    const auto saved_assembly=workspace::prepare_document_save(live,owner,assembly_file).write();static_cast<void>(workspace::complete_document_save(live,saved_assembly));
    workspace::Workspace cold;cold.add_assembly(assembly::AssemblyDocument::load(assembly_file),assembly_file);cold.refresh_source_geometry();
    const auto& reopened=cold.open_assembly(owner)->session.document();
    require(std::abs(reopened.find_occurrence(first)->calculated_source->volume-960)<1e-8&&std::abs(reopened.find_occurrence(second)->calculated_source->volume-240)<1e-8,"Cold Assembly confused two members in one file");
    const auto snapshot=reopened.find_occurrence(first)->calculated_source;cold.refresh_source_geometry();require(snapshot.shares_with(cold.open_assembly(owner)->session.document().find_occurrence(first)->calculated_source),"Unchanged display rebuilt the variant source");
    const auto opened=workspace::open_component_source(cold,owner,assembly::InstancePath{}.child(first));
    require(opened.document_id==long_id&&cold.open_part(root)&&cold.open_part(long_id),"Cold component Open did not load its parent and selected member");
    auto edited=cold.open_part(root)->session.document();edited.find_container(box.id)->box.width=9;cold.open_part(root)->session.commit(edited,kernel.evaluate_history(edited.kernel_operations()));
    cold.refresh_source_geometry();require(std::abs(cold.open_assembly(owner)->session.document().find_occurrence(second)->calculated_source->volume-270)<1e-8,"Closed member ignored unsaved open-parent geometry");
    // A family Assembly can itself contain Part variants and be inserted cold.
    document::FamilyTable assembly_table;assembly_table.columns={"Second"};assembly_table.bindings["Second"]={"component",second,{}};assembly_table.instances={{"One component",{{"Second","no"}}}};
    static_cast<void>(workspace::set_family_table(live,owner,assembly_table));const auto assembly_variant=workspace::open_family_instance(live,kernel,owner,"One component",false);
    const auto family_saved=workspace::prepare_document_save(live,owner,assembly_file).write();static_cast<void>(workspace::complete_document_save(live,family_saved));
    auto top=assembly::AssemblyDocument::create_default();const auto top_id=top.document_id;const auto top_file=directory/"family-top.asmz";live.add_assembly(top,top_file);live.display_top_level(top_id);live.activate(top_id);
    const auto nested=workspace::insert_component(live,top_id,assembly_variant);const auto native_nested=workspace::insert_component(live,top_id,owner);
    const auto top_saved=workspace::prepare_document_save(live,top_id,top_file).write();static_cast<void>(workspace::complete_document_save(live,top_saved));
    workspace::Workspace nested_cold;nested_cold.add_assembly(assembly::AssemblyDocument::load(top_file),top_file);nested_cold.refresh_source_geometry();
    const auto nested_path=assembly::InstancePath{}.child(nested).child(second);
    require(nested_cold.resolve_occurrence(top_id,nested_path)->source_document_id==short_id&&nested_cold.occurrence_source_file(top_id,nested_path)==file,"Nested member lost its occurrence or native path");
    const auto opened_nested=workspace::open_component_source(nested_cold,top_id,assembly::InstancePath{}.child(nested));
    require(opened_nested.document_id==assembly_variant&&nested_cold.open_assembly(owner),"Cold Assembly member did not open with its parent");
    nested_cold.activate(top_id);static_cast<void>(workspace::replace_component(nested_cold,kernel,top_id,nested,owner));
    require(nested_cold.open_assembly(top_id)->session.document().find_occurrence(native_nested)->source_document_id==owner,"Nested replacement changed its sibling");
    bool cycle=false;try{nested_cold.activate(owner);static_cast<void>(workspace::insert_component(nested_cold,owner,assembly_variant));}catch(const std::exception&){cycle=true;}
    require(cycle,"Assembly accepted an instance of its own family as a child");
}
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
int main(){try{kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-family-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);test(kernel,dir);component_test(kernel,dir);bend_state_test(kernel,dir);require(dir.parent_path()==root,"Unsafe test cleanup");fs::remove_all(dir);std::cout<<"Linked Family Table, component insertion/replacement, cold nested sources, shared history, drawings and CLI passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
