#include "derived_copy_query_test_support.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/derived_copy_operations.hpp>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* command,Json args=Json::object()) {
    auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.code+": "+result.message);
    return result;
}
Json source_ids(const Json& data) {auto result=Json::array();for(const auto& item:data.at("items"))result.push_back(item.at("id"));return result;}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Host host(live,kernel,dir);
    require(host.execute_text("derived_copy.sources").code=="unsupported_document","Source query accepted no document");
    const auto fixture=test::copy_query_fixture();const auto id=fixture.document.document_id;
    const auto calculated=kernel.evaluate_history(fixture.document.kernel_operations());
    require(std::abs(calculated.back().body_outputs.at(fixture.mirror)->volume-48)<1e-7&&
        std::abs(calculated.back().body_outputs.at(fixture.pattern)->volume-11*48)<1e-7,"Query fixture has wrong independent copy volumes");
    const auto file=dir/"copies.prtz";fixture.document.save(file,calculated);
    std::vector<kernel::BodyResult> native_cache;auto native=document::PartDocument::load(file,&native_cache);
    live.add_part(std::move(native),std::move(native_cache),file);live.activate(id);auto* state=live.open_part(id);
    const auto original_graph=state->session.document().body_history;
    const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    const auto* source_shape=&state->session.calculated_boundaries().back().body_outputs.at(fixture.source).get();
    auto data=run(host,"derived_copy.sources",{{"object",fixture.mirror}}).data;
    const auto solid=fixture.document.history[0].id,other_solid=fixture.document.history[1].id;
    require(data.at("boundary")==2&&source_ids(data)==Json::array({fixture.source,solid,fixture.other,other_solid}),"Mirror boundary offered itself or downstream geometry");
    data=run(host,"derived_copy.sources",{{"object",fixture.pattern}}).data;
    require(source_ids(data)==Json::array({fixture.source,solid,fixture.other,other_solid,fixture.mirror}),"Pattern boundary lost a preceding copy");
    data=run(host,"derived_copy.sources").data;
    require(source_ids(data)==Json::array({solid,other_solid,fixture.mirror,fixture.pattern,fixture.combined,fixture.copy_of_boolean})&&
        data.at("items")[4].at("kind")=="boolean","Copy source list failed Boolean consumption/order");
    auto mirror=run(host,"mirror.get",{{"object",fixture.mirror}}).data;
    require(mirror.at("source")==fixture.source&&mirror.at("reference").at("owner")==fixture.mirror+":origin"&&
        mirror.at("placement").at("x")==-2&&mirror.at("placement").at("value_locks")==Json::array({"x"})&&
        mirror.at("resolved_plane").at("point")[0]==-2,"Mirror query lost persisted placement, source or plane");
    const auto pattern=run(host,"pattern.get",{{"object",fixture.pattern}}).data;
    const auto& p=pattern.at("pattern");
    require(pattern.at("name")=="Pole žluťoučké"&&p.at("instance_count")==12&&p.at("count")==4&&p.at("linear").size()==3&&
        p.at("linear")[0].at("distribution")=="symmetric"&&p.at("linear")[1].at("reverse_count")==2&&
        p.at("linear")[1].at("spacing_mm")==20&&p.at("linear")[2].at("axis").is_null()&&
        pattern.at("value_locks")==Json::array({"pattern:spacing:1"}),"Pattern query lost independent directions, inactive rows or locks");
    require(run(host,"mirror.get",{{"object",fixture.copy_of_boolean}}).data.at("source")==fixture.combined,"Mirror cannot query its Boolean source");
    for(const auto* command:{"mirror.get","pattern.get"}) {
        require(host.execute({{"command",command},{"arguments",{{"object",fixture.source}}}}).code=="wrong_feature","Copy query accepted a normal Body");
        require(host.execute({{"command",command},{"arguments",{{"object","missing"}}}}).code=="object_not_found","Copy query guessed a missing object");
    }
    require(host.execute({{"command","mirror.get"},{"arguments",{{"object",fixture.pattern}}}}).code=="wrong_feature"&&
        host.execute({{"command","pattern.get"},{"arguments",{{"object",fixture.mirror}}}}).code=="wrong_feature","Copy query accepted the wrong operation kind");
    require(host.execute({{"command","derived_copy.sources"},{"arguments",{{"object",fixture.source}}}}).code=="wrong_feature","Source query accepted an invalid edit boundary");
    require(state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache&&
        &state->session.calculated_boundaries().back().body_outputs.at(fixture.source).get()==source_shape&&
        state->session.document().body_history==original_graph&&!host.change(),"Read-only copy queries calculated or mutated document/cache");
    auto active=state->session.document();active.body_history.activate(fixture.source);state->session.commit(std::move(active),state->session.calculated_boundaries());
    require(source_ids(run(host,"derived_copy.sources").data)==Json::array({solid}),"Active Body must offer its solid, not the whole Body");
    active=state->session.document();active.body_history.activate({});active.body_history.set_insertion_cursor(0);state->session.commit(std::move(active),state->session.calculated_boundaries());
    require(run(host,"derived_copy.sources").data.at("items").empty(),"Creation ignored the root insertion cursor");
    auto assembly=assembly::AssemblyDocument::create_default();
    auto first=assembly::AssemblyDocument::create_part_occurrence("Opakovaný díl",id,file,calculated.back());first.visible=false;
    auto second=assembly::AssemblyDocument::create_part_occurrence("Opakovaný díl",id,file,calculated.back());second.suppressed=true;
    auto reflected=first;reflected.occurrence_id=kernel::make_stable_id();reflected.visible=true;reflected.placement={};reflected.copy_placement.x=9;
    reflected.derived_copy=document::DerivedCopyParameters{first.occurrence_id,{{},reflected.occurrence_id+":origin","origin:plane:yz"}};
    auto patterned=first;patterned.occurrence_id=kernel::make_stable_id();patterned.copy_placement.rotation_z=15;
    patterned.derived_copy=document::DerivedCopyParameters{first.occurrence_id,{{},patterned.occurrence_id+":origin","origin:axis:z"}};
    patterned.derived_copy->pattern=kernel::PatternRequest{};patterned.derived_copy->pattern->circular=true;
    assembly.components={first,second,reflected,patterned};assembly.calculate_derived_copies(kernel);
    // Deliberately leave changed source placement and unresolved reference data
    // uncalculated. Queries must report stored inputs, retaining calculated copies.
    assembly.components.front().placement.x=100;
    assembly.components[2].derived_copy->reference={{assembly::InstancePath{}.child(first.occurrence_id).encoded()},fixture.document.history.front().id,"x_min"};
    assembly.components[2].derived_copy->reference_valid=false;
    const auto assembly_id=assembly.document_id;live.add_assembly(std::move(assembly));live.activate(assembly_id);
    auto* group=live.open_assembly(assembly_id);const auto copied=group->session.document().components[2].calculated_source;
    const auto assembly_revision=group->session.revision();
    data=run(host,"derived_copy.sources",{{"object",reflected.occurrence_id}}).data;
    require(data.at("boundary")==2&&source_ids(data)==Json::array({first.occurrence_id})&&data.at("items")[0].at("visible")==false&&
        data.at("items")[0].at("instance_path")==assembly::InstancePath{}.child(first.occurrence_id).encoded(),"Assembly lost immediate occurrence identity or confused visibility with suppression");
    mirror=run(host,"mirror.get",{{"object",reflected.occurrence_id}}).data;
    require(mirror.at("placement").at("x")==9&&mirror.at("reference_valid")==false&&
        mirror.at("reference").at("instance_path")==assembly::InstancePath{}.child(first.occurrence_id).encoded(),"Copy query solved its stale reference or returned ordinary component placement");
    require(run(host,"pattern.get",{{"object",patterned.occurrence_id}}).data.at("pattern").at("mode")=="circular","Assembly circular Pattern could not be read");
    require(run(host,"pattern.get",{{"object",fixture.pattern},{"document",id}}).data.at("object")==fixture.pattern&&
        live.active_document_id()==assembly_id,"Inactive Part query changed active document");
    require(host.execute({{"command","mirror.get"},{"arguments",{{"object",fixture.mirror}}}}).code=="object_not_found","Assembly reached into a source Part");
    require(group->session.revision()==assembly_revision&&group->session.document().components[2].calculated_source.shares_with(copied)&&
        group->session.document().components.front().placement.x==100&&!host.change(),"Assembly queries regenerated copies or solved placement");
}
}
int main(){try {
    const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-copy-query-"+kernel::make_stable_id());
    require(fs::create_directory(dir),"Cannot create query test directory");kernel::OcctKernel kernel;verify(kernel,dir);
    require(dir.parent_path()==root,"Unexpected query test cleanup path");fs::remove_all(dir);
    std::cout<<"Derived copy queries: boundaries, Boolean sources, exact occurrences, native parameters and no calculation passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
