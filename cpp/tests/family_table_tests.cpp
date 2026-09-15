#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/document/physical_properties.hpp>
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
    auto revised=table;revised.instances.front().values[length.name]="22";
    static_cast<void>(workspace::set_family_table(live,id,revised));
    require(workspace::open_family_instance(live,kernel,id,"Long")==variant&&std::abs(volume(live,variant)-1040)<1e-8,"Opening an updated row did not regenerate its existing tab");
    auto edited=live.open_part(variant)->session.document();edited.name="Own edit";
    live.open_part(variant)->session.commit(edited,live.open_part(variant)->session.calculated_boundaries());
    static_cast<void>(workspace::set_family_table(live,id,table));bool protected_edits=false;
    try{static_cast<void>(workspace::open_family_instance(live,kernel,id,"Long"));}catch(const std::exception&){protected_edits=true;}
    require(protected_edits&&live.open_part(variant)->session.document().name=="Own edit","Family regeneration overwrote user edits");
    require(live.remove(variant),"Cannot close generated test instance");
    require(workspace::open_family_instance(live,kernel,id,"Long")==variant&&std::abs(volume(live,variant)-944)<1e-8,"Reopening did not preserve instance identity");
    const auto no_cut=workspace::open_family_instance(live,kernel,id,"No cut");require(std::abs(volume(live,no_cut)-968)<1e-8,"Suppressed subtractive feature still removed material");
    const auto one=workspace::open_family_instance(live,kernel,id,"One body");require(std::abs(volume(live,one)-936)<1e-8,"Absent Body remained in variant geometry");
    const auto& state=*live.open_part(variant);state.session.document().save(directory/"long.prtz",state.session.calculated_boundaries());
    auto cold=document::PartDocument::load(directory/"long.prtz");auto recalculated=workspace::calculate_part_with_resolved_references(kernel,cold,nullptr,{true});
    require(std::abs(recalculated.back().volume-944)<1e-8,"Native variant required the generic or a sidecar to regenerate");
    auto drawing=drawing::DrawingDocument::create_default();drawing.source_document_id=variant;drawing.source_path=directory/"long.prtz";
    drawing.sheets.front().views.push_back(drawing::DrawingDocument::create_view(variant,drawing.source_path,state.session.calculated_boundaries().back().mesh));
    live.open_part(variant)->path=directory/"long.prtz";
    workspace::select_family_drawing_source(drawing,live,id,directory/"long.drwz");
    require(drawing.source_document_id==id&&drawing.sheets.front().views.front().source_document_id==id,"Drawing variant did not return to generic");
    workspace::select_family_drawing_source(drawing,live,variant,directory/"long.drwz");
    require(drawing.source_document_id==variant,"Drawing variant selector lost generated identity");
    drawing.save(directory/"long.drwz");const auto drw=drawing::DrawingDocument::load(directory/"long.drwz");
    require(drw.source_document_id==variant && drw.sheets.front().views.front().source_document_id==variant,"Drawing did not retain exact variant identity");
    auto bad=table;bad.instances.push_back({"Invalid",{{length.name,"-2"}}});static_cast<void>(workspace::set_family_table(live,id,bad));
    const auto count=live.size();bool rejected=false;try{workspace::open_family_instance(live,kernel,id,"Invalid");}catch(const std::exception&){rejected=true;}
    require(rejected&&live.size()==count&&std::abs(volume(live,id)-464)<1e-8,"Failed variant left a partial document or changed generic");
    bad=table;bad.bindings[length.name].owner_id="missing";rejected=false;try{static_cast<void>(workspace::set_family_table(live,id,bad));}catch(const std::exception&){rejected=true;}require(rejected,"Dangling family reference accepted");
    auto assembly=assembly::AssemblyDocument::create_default();assembly.name="Assembly";const auto aid=assembly.document_id;live.add_assembly(assembly,directory/"base.asmz");
    const auto component=live.insert_open_part(aid,id,"Block");
    document::FamilyTable assembly_table;assembly_table.columns={"Block"};assembly_table.bindings["Block"]={"component",component,{}};assembly_table.instances={{"Empty",{{"Block","no"}}}};
    static_cast<void>(workspace::set_family_table(live,aid,assembly_table));const auto av=workspace::open_family_instance(live,kernel,aid,"Empty");
    require(live.open_assembly(av)->session.document().find_occurrence(component)->suppressed&&!live.open_assembly(aid)->session.document().find_occurrence(component)->suppressed,"Family component presence mutated owning Assembly");
    live.display_top_level(id);live.activate(id);auto cwd=directory;command_host::Host host(live,kernel,cwd);
    auto query=host.execute({{"command","document.family.references"},{"arguments",{{"document",id}}}});require(query.ok&&!query.data.at("references").empty(),"CLI family reference catalog failed");
    auto opened=host.execute({{"command","document.family.open"},{"arguments",{{"document",id},{"instance","Long"}}}});if(!opened.ok)throw std::runtime_error(opened.code+": "+opened.message);require(opened.data.at("document")==variant,"CLI did not open the same family instance");
}
}
int main(){try{kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-family-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);test(kernel,dir);require(dir.parent_path()==root,"Unsafe test cleanup");fs::remove_all(dir);std::cout<<"Family Table dimensions, presence, identities, native regeneration, drawings, CLI and atomic rejection passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
