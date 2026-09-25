#include "profile_command_fixture.hpp"
#include "profile_solid_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/value_lock_operations.hpp>
#include <algorithm>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;command_host::Interaction interaction;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
    options.interaction=[&]{return interaction;};command_host::Host host(live,kernel,dir,options);
    require(host.execute_text("value_lock.list missing").code=="unsupported_document","Lock query accepted no document");
    run(host,"new",{{"type","part"},{"name","value-locks"}});const auto id=live.active_document_id();
    const auto box=zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}}).data.at("container").get<std::string>();
    auto* state=live.open_part(id);const auto body=state->session.document().body_history.active_body_id();
    const auto get=[&](const std::string& object){return run(host,"value_lock.list",{{"object",object}}).data;};
    const auto set=[&](const std::string& object,const std::string& key,bool locked){return run(host,"value_lock.set",{{"object",object},{"key",key},{"locked",locked}}).data;};
    const auto locked=[&](const std::string& object,const std::string& key){return workspace::value_locked(live,live.active_document_id(),object,key).value_or(false);};
    const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    const auto before=get(box);require(before.at("items").size()==13&&state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache&&!host.change(),"Lock query calculated or mutated geometry");
    const auto shape=state->session.calculated_boundaries().back().kernel_shape;const auto fingerprint=state->session.calculated_boundaries().back().source_fingerprint;
    require(set(box,"length_forward",true).at("changed")==true&&host.change()->kind==command_host::ChangeKind::Metadata,"Lock did not use metadata notification");
    require(locked(box,"parameter:length_forward")&&zima::test::profile_dimension(state->session.document(),*state->session.document().find_container(box),0)==10&&
        state->session.calculated_boundaries().back().kernel_shape==shape&&state->session.calculated_boundaries().back().source_fingerprint==fingerprint,"Lock changed a value or recalculated its solid");
    const auto lock_revision=state->session.revision();const auto* lock_cache=state->session.calculated_boundaries().data();
    require(set(box,"parameter:length_forward",true).at("changed")==false&&state->session.revision()==lock_revision&&state->session.calculated_boundaries().data()==lock_cache&&!host.change(),"Repeated lock created an Undo step");
    require(host.execute({{"command","extrusion.set"},{"arguments",{{"container",box},{"length_forward_mm",16.5}}}}).code=="value_locked","Locked length remained editable through CLI");
    run(host,"undo");require(!locked(box,"length_forward"),"Undo did not unlock");run(host,"redo");require(locked(box,"length_forward"),"Redo lost lock");
    set(box,"x",true);require(locked(box,"parameter:placement:x"),"Placement alias lost stored slot");
    require(host.execute({{"command","placement.set"},{"arguments",{{"object",box},{"values",{{"x",4}}}}}}).code=="parameter_not_editable","Zero-valued placement lock was bypassed");
    set(body,"placement:rotation_offset_z",true);
    require(host.execute({{"command","placement.set"},{"arguments",{{"object",body},{"values",{{"rotation_z",20}}}}}}).code=="parameter_not_editable","Reference-corrected angle bypassed its real lock");
    set(body,"placement:reference_offset:2",true);
    require(host.execute({{"command","placement.set"},{"arguments",{{"object",body},{"values",{{"reference_offset:2",10}}}}}}).code=="parameter_not_editable","Body reference offset lock was bypassed");
    const auto stable=state->session.revision();
    for(const auto* key:{"unknown","placement:reference_offset:-1","placement:reference_offset:184467440737095516160","placement:reference_offset:0junk","placement:reference_offset:00","pitch"}) {
        require(host.execute({{"command","value_lock.set"},{"arguments",{{"object",box},{"key",key},{"locked",true}}}}).code=="unknown_parameter","Invalid lock key wrote arbitrary metadata");
        require(state->session.revision()==stable,"Invalid key changed revision");
    }
    for(auto value:{Json(1),Json("false"),Json(nullptr)})require(!host.execute({{"command","value_lock.set"},{"arguments",{{"object",box},{"key","length_forward"},{"locked",value}}}}).ok,"Non-boolean lock value accepted");
    interaction.editing=true;require(host.execute({{"command","value_lock.set"},{"arguments",{{"object",box},{"key","length_forward"},{"locked",false}}}}).code=="editing_in_progress","CLI overrode pending GUI locks");interaction={};
    run(host,"body.create",{{"name","Other"}});
    require(host.execute({{"command","value_lock.set"},{"arguments",{{"object",box},{"key","length_forward"},{"locked",false}}}}).code=="inactive_body","Lock edited another Body");
    require(get(box).at("object")==box,"Read-only locks cannot inspect another Body");run(host,"body.activate",{{"body",body}});
    const auto plane=run(host,"construction.create",{{"kind","plane"},{"name","Plane"}}).data.at("construction").get<std::string>();
    set(plane,"offset",true);require(host.execute({{"command","construction.set"},{"arguments",{{"construction",plane},{"offset_mm",1}}}}).code=="value_locked","Datum offset lock was bypassed");
    const auto point=[](double x,double y){return Json{{"values",{{"x",x},{"y",y},{"z",0}}}};};
    const auto curve=run(host,"construction.create",{{"kind","curve3d"},{"name","Curve"},{"rounding_enabled",true},{"points",Json::array({point(0,0),point(10,0),point(10,10)})}}).data;
    const auto child=curve.at("children")[1].get<std::string>();set(child,"radius",true);set(child,"placement:y",true);
    require(host.execute({{"command","construction.set"},{"arguments",{{"construction",child},{"radius_mm",2}}}}).code=="value_locked","Curve point radius lock was bypassed");
    require(host.execute({{"command","placement.set"},{"arguments",{{"object",child},{"values",{{"y",2}}}}}}).code=="parameter_not_editable","Local point lock was bypassed");
    set(child,"radius",false);run(host,"construction.set",{{"construction",child},{"radius_mm",2}});
    set(box,"length_forward",false);run(host,"extrusion.set",{{"container",box},{"length_forward_mm",16.5}});set(box,"length_forward",true);
    run(host,"save");std::vector<kernel::BodyResult> saved;const auto native=document::PartDocument::load(dir/"value-locks.prtz",&saved);
    require(native.find_container(box)->value_locks.contains("length_forward")&&native.find_container(box)->placement.value_locks.contains("x")&&
        std::ranges::any_of(native.body_history.find(body)->scope.placement.references,[](const auto& ref){return ref.semantic_key=="origin:plane:yz"&&ref.offset_locked;})&&native.find_construction(child)->value_locks.contains("placement:y"),"Native Part lost a lock");
    require(std::abs(saved.back().volume-6600)<1e-6,"Locks corrupted the calculated solid");
    run(host,"new",{{"type","assembly"},{"name","assembly-locks"}});const auto assembly_id=live.active_document_id();
    const auto first=run(host,"component.insert",{{"source",id}}).data.at("occurrence").get<std::string>();
    const auto second=run(host,"component.insert",{{"source",id}}).data.at("occurrence").get<std::string>();
    auto* assembly=live.open_assembly(assembly_id);const auto geometry=assembly->session.document().find_occurrence(first)->calculated_source;
    require(get(first).at("items").size()==6,"Component offered nonexistent angle-correction locks");
    require(host.execute({{"command","value_lock.set"},{"arguments",{{"object",first},{"key","placement:rotation_offset_x"},{"locked",true}}}}).code=="unknown_parameter","Component accepted an inert correction lock");
    set(first,"placement:x",true);require(locked(first,"placement:x")&&!locked(second,"placement:x"),"Lock leaked into another occurrence");
    auto draft=assembly->session.document();assembly::ComponentPlacementReference ref;
    ref.component_reference.owner_id=box;ref.component_reference.semantic_key="z_max";
    ref.target_reference.owner_id=box;ref.target_reference.semantic_key="z_min";ref.target_reference.instance_path=assembly::InstancePath{}.child(second);ref.offset=25;
    draft.find_occurrence(first)->placement_references={ref};assembly->session.commit(std::move(draft));
    const auto placement_before=assembly->session.document().find_occurrence(first)->placement;
    set(assembly_id,"placement-reference:"+first+":0",true);
    require(locked(first,"placement:reference_offset:0")&&assembly->session.document().find_occurrence(first)->placement==placement_before&&
        assembly->session.document().find_occurrence(first)->calculated_source.shares_with(geometry),"Lock solved mates or duplicated source geometry");
    run(host,"undo");require(!locked(first,"placement:reference_offset:0"),"Mate lock Undo failed");run(host,"redo");
    const auto assembly_plane=run(host,"construction.create",{{"kind","plane"},{"name","Plane"}}).data.at("construction").get<std::string>();set(assembly_plane,"offset",true);
    const auto at=assembly->session.revision();require(run(host,"value_lock.list",{{"document",id},{"object",box}}).data.at("object")==box&&assembly->session.revision()==at&&live.active_document_id()==assembly_id,"Inactive-document query changed activation");
    require(host.execute({{"command","value_lock.set"},{"arguments",{{"object",box},{"key","length_forward"},{"locked",false}}}}).code=="object_not_found","Assembly edited source Part lock");
    run(host,"save");const auto native_assembly=assembly::AssemblyDocument::load(dir/"assembly-locks.asmz");
    require(native_assembly.find_occurrence(first)->value_locks.contains("placement:x")&&native_assembly.find_occurrence(first)->placement_references[0].offset_locked&&
        !native_assembly.find_occurrence(second)->value_locks.contains("placement:x")&&native_assembly.find_construction(assembly_plane)->value_locks.contains("offset"),"Native Assembly lost occurrence-specific locks");
    run(host,"new",{{"type","part"},{"name","opening-locks"}});
    zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","60"},{"width_mm","60"},{"height_mm","60"}});
    const auto opening=run(host,"opening.create",{{"type","metric"},{"designation","M10"},{"bore_length_mm",20},{"thread_length_mm",10},
        {"chamfer_enabled",false},{"drill_point_enabled",false},{"placement",{{"z",-30}}}}).data.at("container").get<std::string>();
    auto* opening_state=live.open_part(live.active_document_id());
    set(opening,"parameter:thread_designation",true);
    require(locked(opening,"nominal_diameter"),"Catalog dimension and Properties used different locks");
    const auto opening_revision=opening_state->session.revision();
    const auto* opening_cache=opening_state->session.calculated_boundaries().data();
    require(host.execute({{"command","opening.set"},{"arguments",{{"container",opening},{"designation","M12"}}}}).code=="value_locked"&&
        opening_state->session.revision()==opening_revision&&opening_state->session.calculated_boundaries().data()==opening_cache,
        "Catalog size bypassed locked nominal diameter or changed geometry on rejection");
    set(opening,"parameter:thread_designation",false);run(host,"opening.set",{{"container",opening},{"designation","M12"}});
    require(opening_state->session.document().find_container(opening)->thread.nominal_diameter==12,"Unlocked catalog size remained blocked");
    set(opening,"parameter:thread_pitch",true);require(locked(opening,"pitch"),"Pitch dimension address lost its actual field lock");
}
}
int main(){try{const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-value-locks-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create lock test directory");kernel::OcctKernel kernel;verify(kernel,dir);
    require(dir.parent_path()==root,"Unexpected lock test cleanup path");fs::remove_all(dir);
    std::cout<<"Value locks: model metadata, no-op, numeric guards, original reference offsets, local points, occurrences, Undo and native files passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
