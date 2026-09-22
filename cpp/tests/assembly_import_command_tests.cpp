#include <zima/command_host/host.hpp>
#include <zima/workspace/assembly_import_operations.hpp>
#include <zima/workspace/component_source_operations.hpp>
#include <zima/interchange/step_model.hpp>
#include <zima/interchange/dxf.hpp>
#include <zima/document/file_path.hpp>
#include <zima/document/precision.hpp>
#include <fstream>
#include <cmath>
#include <iostream>
#include <set>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;
    double configured_cut_tolerance=.025;
    options.settings=[&] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body",configured_cut_tolerance},{{"Length","cm"}}};};
    command_host::Host host(live,kernel,dir,options);
    kernel::StepProduct part;part.definition_id="part";part.name="Block";part.body=kernel.make_box({10,20,30});
    auto second=part;second.name="Block 2";second.translation={50,0,0};
    kernel::StepProduct group;group.definition_id="group";group.name="Subassembly";group.children={part,second};
    auto repeated=group;repeated.name="Repeated subassembly";repeated.translation={0,50,0};
    kernel::StepProduct root;root.definition_id="root";root.name="Test assembly";root.children={group,repeated};
    const auto source=dir/fs::path(u8"sestava česká.step");kernel.export_step(root,document::path_to_utf8(source));
    run(host,"new",{{"type","assembly"},{"name","owner"}});const auto owner=live.active_document_id();
    const auto before_size=live.size();const auto revision=live.open_assembly(owner)->session.revision();
    const auto result=run(host,"import.step",{{"path",document::path_to_utf8(source)},{"output_directory","step-native"},{"mesh_deflection_mm",2.0}}).data;
    require(live.size()==before_size+3 && result.at("parts").size()==1 && result.at("assemblies").size()==2,"STEP import duplicated repeated source definitions");
    auto* target=live.open_assembly(owner);require(target->session.revision()==revision+1 && target->session.document().components.size()==1,"Import did not produce one owner transaction");
    const auto occurrence=target->session.document().components.front();
    const auto owner_packet=target->session.document().serialized();
    require(owner_packet.at("operation_geometries").empty() && !owner_packet.contains("source_geometries") && owner_packet.at("components")[0].at("operation_geometry").is_null() && owner_packet.at("components")[0].at("nested_snapshot").empty(),"Plain Assembly persisted imported source geometry or nested structure");
    require(std::abs(occurrence.calculated_source->volume-24000)<1e-5 && occurrence.calculated_source->kernel_shape.empty(),"Assembly import duplicated a compound or lost exact volume");
    require(live.active_document_id()==owner && live.displayed_document_id()==owner,"Import switched the editing/displayed document");
    require(!target->background_import_source,"Import hid its owning Assembly tab");
    for(const auto& id:result.at("parts"))require(live.open_part(id.get<std::string>())->background_import_source,"Imported Part requested an automatic tab");
    for(const auto& id:result.at("assemblies"))require(live.open_assembly(id.get<std::string>())->background_import_source,"Imported subassembly requested an automatic tab");
    for(const auto& path:result.at("files")) {
        const auto file=fs::u8path(path.get<std::string>());require(fs::is_regular_file(file),"Imported source was not saved");
        require(file.extension()==".prtz" || file.extension()==".asmz","Import introduced a sidecar format");
        if(file.extension()==".prtz") {
            const auto saved=document::PartDocument::load(file);
            require(live.open_part(saved.document_id)!=nullptr,"Saved STEP Part cannot be reopened with its source identity");
            require(document::sheet_cut_tolerance(saved.document_precision)==.025,"Saved STEP Part lost its configured Sheet Cut tolerance");
        } else {
            const auto saved=assembly::AssemblyDocument::load(file);
            require(live.open_assembly(saved.document_id)!=nullptr,"Saved STEP subassembly cannot be reopened with its source identity");
            require(saved.serialized().at("operation_geometries").empty(),"Imported subassembly duplicates native source geometry");
        }
    }
    const auto part_id=result.at("parts")[0].get<std::string>();
    auto* imported_part=live.open_part(part_id);require(imported_part->session.document().document_units.at("Length")=="cm","Imported Part lost owner units");
    require(document::sheet_cut_tolerance(imported_part->session.document().document_precision)==.025,
        "STEP Part did not snapshot the configured Sheet Cut tolerance");
    for(const auto& [key,value]:target->session.document().document_precision)
        require(imported_part->session.document().document_precision.at(key)==value,"STEP import changed inherited Assembly precision");
    for(const auto& operation:imported_part->session.document().kernel_operations())require(operation.mesh_deflection==2,"Import lost selected mesh precision");
    const auto* native_root=live.open_assembly(result.at("source_document").get<std::string>());
    require(native_root && native_root->session.document().components.size()==2,"STEP hierarchy was flattened");
    const auto& repeats=native_root->session.document().components;
    require(repeats[0].source_document_id==repeats[1].source_document_id && repeats[0].occurrence_id!=repeats[1].occurrence_id && repeats[0].calculated_source.shares_with(repeats[1].calculated_source),"Repeated occurrence identity or shared snapshot was lost");
    run(host,"undo");require(target->session.document().components.empty() && live.size()==before_size+3,"Undo must detach the insertion while preserving independent source documents");
    run(host,"redo");require(target->session.document().components.front().occurrence_id==occurrence.occurrence_id,"Redo changed occurrence identity");
    run(host,"save");fs::remove(source);const auto restored=assembly::AssemblyDocument::load(dir/"owner.asmz");
    require(restored.components.front().occurrence_id==occurrence.occurrence_id && std::abs(restored.components.front().calculated_source->volume-24000)<1e-5,"Native Assembly depends on the deleted STEP");
    const auto root_source=fs::u8path(result.at("files").back().get<std::string>());
    // A missing dependency retains its tree occurrence, with no embedded fallback.
    const auto hidden=root_source.string()+".missing";fs::rename(root_source,hidden);
    const auto missing=assembly::AssemblyDocument::load(dir/"owner.asmz");
    require(missing.components.front().source_missing && missing.components.front().calculated_source->mesh.triangles.empty() && missing.components.front().occurrence_id==occurrence.occurrence_id,"Missing source did not retain an unresolved tree occurrence");
    const auto from_open=assembly::AssemblyDocument::load(dir/"owner.asmz",workspace::native_source_resolver(live));
    require(std::abs(from_open.components.front().calculated_source->volume-24000)<1e-5,"Open source document was not authoritative over a missing disk file");
    workspace::Workspace repair;repair.add_assembly(missing,dir/"owner.asmz");
    const auto wrong_path=dir/"wrong-source.asmz";assembly::AssemblyDocument::create_default().save(wrong_path);
    bool wrong=false;
    try {workspace::relink_component_source(repair,owner,occurrence.occurrence_id,wrong_path);}catch(const workspace::ComponentOperationError& error){wrong=std::string(error.code)=="source_identity";}
    require(wrong && repair.open_assembly(owner)->session.revision()==0,"Source repair accepted a different document or changed history on failure");
    const auto relocated_dir=dir/"relocated";fs::create_directory(relocated_dir);
    const auto relocated=relocated_dir/root_source.filename();fs::rename(hidden,relocated);
    workspace::relink_component_source(repair,owner,occurrence.occurrence_id,relocated);
    const auto& repaired=repair.open_assembly(owner)->session.document().components.front();
    require(!repaired.source_missing && repaired.source_path==relocated && repaired.occurrence_id==occurrence.occurrence_id && std::abs(repaired.calculated_source->volume-24000)<1e-5,"Source relocation lost geometry or occurrence identity");
    require(repair.open_assembly(owner)->session.undo() && repair.open_assembly(owner)->session.document().components.front().source_missing,"Source relocation Undo lost unresolved state");
    require(repair.open_assembly(owner)->session.redo() && !repair.open_assembly(owner)->session.document().components.front().source_missing,"Source relocation Redo failed");
    fs::rename(relocated,root_source);
    run(host,"regenerate");require(std::abs(live.open_assembly(owner)->session.document().components.front().calculated_source->volume-24000)<1e-5,"Explicit regeneration lost native imported sources");
    auto parent_document=assembly::AssemblyDocument::create_default();const auto parent_id=parent_document.document_id;
    live.add_assembly(std::move(parent_document),dir/"parent.asmz");static_cast<void>(live.insert_open_assembly(parent_id,owner,"Passive owner"));
    const auto parent_revision=live.open_assembly(parent_id)->session.revision();
    auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(0,0,20,10));
    const auto dxf=dir/fs::path(u8"obrys český.dxf");interchange::export_dxf(dxf,sketch);
    const auto reject=[&](const char* command,Json args) {
        const auto size=live.size(),rev=live.open_assembly(owner)->session.revision();
        require(!host.execute({{"command",command},{"arguments",std::move(args)}}).ok,"Invalid Assembly import succeeded");
        require(live.size()==size && live.open_assembly(owner)->session.revision()==rev,"Failed Assembly import left partial documents");
    };
    reject("import.dxf",{{"path",document::path_to_utf8(dxf)},{"output_directory","step-native"}});
    reject("import.dxf",{{"path",document::path_to_utf8(dxf)},{"output_directory","limited"},{"maximum_entities",1}});
    require(!fs::exists(dir/"limited"),"Failed calculation left its destination directory");
    reject("import.dxf",{{"path",document::path_to_utf8(dxf)},{"sketch","existing"}});
    const auto dxf_result=run(host,"import.dxf",{{"path",document::path_to_utf8(dxf)}}).data;
    const auto dxf_part=dxf_result.at("parts")[0].get<std::string>();const auto* dxf_state=live.open_part(dxf_part);
    require(dxf_result.at("assemblies").empty() && dxf_result.at("imported_entities")==4 && dxf_state->session.document().sketches.back().segments.size()==4,"Assembly DXF did not create one Part with native sketch geometry");
    require(dxf_state->session.calculated_boundaries().empty(),"DXF unnecessarily calculated a solid");
    const auto iges=dir/fs::path(u8"kostka česká.igs");fs::copy_file("cpp/tests/fixtures/import/cube-10mm.igs",iges);
    configured_cut_tolerance=.0125;
    const auto iges_result=run(host,"import.iges",{{"path",document::path_to_utf8(iges)},{"mesh_deflection_mm",1.5}}).data;
    require(iges_result.at("parts").size()==1 && iges_result.at("assemblies").empty(),"IGES did not create exactly one Part");
    require(iges_result.at("files").size()==1,"IGES did not report its native source file");
    const auto iges_native=fs::u8path(iges_result.at("files")[0].get<std::string>());
    require(fs::is_regular_file(iges_native) && iges_native.extension()==".prtz" && iges_native.parent_path()==dir,"IGES native Part was not saved directly in the working directory");
    const auto saved_iges=document::PartDocument::load(iges_native);
    require(saved_iges.document_id==iges_result.at("parts")[0].get<std::string>(),"Saved IGES Part cannot be reopened with its source identity");
    require(document::sheet_cut_tolerance(saved_iges.document_precision)==.0125&&
        document::sheet_cut_tolerance(live.open_part(saved_iges.document_id)->session.document().document_precision)==.0125,
        "IGES Part did not snapshot and save the changed Sheet Cut default");
    require(document::sheet_cut_tolerance(live.open_part(part_id)->session.document().document_precision)==.025,
        "Changing the import default rewrote an existing STEP Part");
    for(const auto& [key,value]:live.open_assembly(owner)->session.document().document_precision)
        require(saved_iges.document_precision.at(key)==value,"IGES import changed inherited Assembly precision");
    require(std::abs(live.open_part(iges_result.at("parts")[0].get<std::string>())->session.calculated_boundaries().back().volume-1000)<1e-4,"Assembly IGES changed source scale");
    require(live.open_assembly(parent_id)->session.revision()==parent_revision &&
        std::abs(live.open_assembly(parent_id)->session.document().components.front().calculated_source->volume-24000)<1e-5,
        "Import implicitly regenerated a parent Assembly");
    // Complete late after the target changed, both after calculation and after saving.
    for(int phase:{1,2}) {
        workspace::AssemblyImportOptions settings;settings.output_directory=dir/("stale-"+std::to_string(phase));
        const auto size=live.size();int calls=0;bool rejected=false;
        try {static_cast<void>(workspace::import_assembly(live,owner,dxf,settings,[&](auto task){
            task();if(++calls==phase){auto next=live.open_assembly(owner)->session.document();next.name="Later edit";live.open_assembly(owner)->session.commit(std::move(next));}
        }));}catch(const workspace::ImportOperationError& e){rejected=std::string(e.code)=="document_changed";}
        require(rejected && live.size()==size && live.open_assembly(owner)->session.document().name=="Later edit" && !fs::exists(settings.output_directory),"Stale import overwrote the target or left native files");
    }
    // An error during the second (save) task must not publish model changes.
    workspace::AssemblyImportOptions failed;failed.output_directory=dir/"failed-write";int calls=0;const auto size=live.size();bool rejected=false;
    try {static_cast<void>(workspace::import_assembly(live,owner,dxf,failed,[&](auto task){if(++calls==2)throw std::runtime_error("simulated write failure");task();}));}catch(const std::exception&){rejected=true;}
    require(rejected && live.size()==size && !fs::exists(failed.output_directory),"Write failure left a partial import");
    workspace::AssemblyImportOptions reopened;reopened.output_directory=dir/"reopened-import";rejected=false;
    try {static_cast<void>(workspace::import_assembly(live,owner,dxf,reopened,[&](auto task){
        task();auto document=live.open_assembly(owner)->session.document();require(live.remove(owner),"Cannot close target fixture");
        live.add_assembly(std::move(document),dir/"reopened.asmz");live.activate(owner);live.display_top_level(owner);
    }));}catch(const workspace::ImportOperationError& e){rejected=std::string(e.code)=="document_changed";}
    require(rejected && !fs::exists(reopened.output_directory),"Import committed into a reopened Assembly");
    workspace::AssemblyImportOptions foreign;foreign.output_directory=dir/"foreign-marker";calls=0;rejected=false;
    try {static_cast<void>(workspace::import_assembly(live,owner,dxf,foreign,[&](auto task){
        if(++calls==2){std::ofstream(foreign.output_directory/"foreign.txt")<<"foreign data";throw std::runtime_error("stop before writing");}task();
    }));}catch(const std::exception&){rejected=true;}
    require(rejected && fs::is_regular_file(foreign.output_directory/"foreign.txt") &&
        std::distance(fs::directory_iterator(foreign.output_directory),fs::directory_iterator{})==1,
        "Failed import recursively removed a foreign file");

    // Default destination follows the working directory even for an owner saved elsewhere.
    kernel.export_step(root,document::path_to_utf8(source));
    workspace::Workspace direct;
    auto owner_doc=assembly::AssemblyDocument::create_default();const auto direct_id=owner_doc.document_id;
    direct.add_assembly(std::move(owner_doc),dir/"elsewhere"/"owner.asmz");
    workspace::AssemblyImportOptions direct_options;direct_options.working_directory=dir;
    std::set<fs::path> saved_paths;
    for(int pass=0;pass<2;++pass) {
        const auto report=workspace::import_assembly(direct,direct_id,source,direct_options);
        require(report.directory==dir && report.files.size()==3,"Default STEP import created an unexpected destination");
        for(const auto& file:report.files) {
            require(file.parent_path()==dir && fs::is_regular_file(file) && saved_paths.insert(file).second,"Repeated STEP import overwrote a source or created a subdirectory");
            if(file.extension()==".asmz") {
                const auto saved=assembly::AssemblyDocument::load(file);
                for(const auto& component:saved.components)
                    require(component.source_path.parent_path()==dir && fs::is_regular_file(component.source_path),"Saved STEP dependency points outside the working directory or to a missing file");
            }
        }
    }
    for(const auto& file:saved_paths)require(fs::is_regular_file(file),"Repeated import removed an earlier native source");
    auto cycle_a=assembly::AssemblyDocument::create_default(),cycle_b=assembly::AssemblyDocument::create_default();
    const auto cycle_a_path=dir/"cycle-a.asmz",cycle_b_path=dir/"cycle-b.asmz";
    cycle_a.components.push_back(assembly::AssemblyDocument::create_assembly_occurrence("B",cycle_b.document_id,cycle_b_path,cycle_b));
    cycle_b.components.push_back(assembly::AssemblyDocument::create_assembly_occurrence("A",cycle_a.document_id,cycle_a_path,cycle_a));
    cycle_a.save(cycle_a_path);cycle_b.save(cycle_b_path);
    bool cyclic=false;
    try {static_cast<void>(assembly::AssemblyDocument::load(cycle_a_path));}
    catch(const std::exception& error){cyclic=std::string(error.what()).find("Cyclic")!=std::string::npos;}
    require(cyclic,"Recursive native dependency cycle was not rejected");

}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto root=parent/("zima-assembly-import-"+document::PartDocument::create_default().document_id);fs::create_directory(root);const auto dir=root/fs::path(u8"český projekt");fs::create_directory(dir);verify(kernel,dir);require(root.parent_path()==parent,"Unsafe cleanup");fs::remove_all(root);std::cout<<"Assembly STEP/IGES/DXF, repeated products, exact volume, units, atomic insertion, Undo and stale workers passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
