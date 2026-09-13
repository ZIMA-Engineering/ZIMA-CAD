#include <zima/command_host/host.hpp>
#include <zima/workspace/derived_copy_operations.hpp>
#include <zima/workspace/value_lock_operations.hpp>
#include <iostream>
#include <set>
#include <limits>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
void near(double a,double b){if(std::abs(a-b)>1e-6)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
commands::Result run(command_host::Host& host,const char* command,Json args=Json::object()) {
    auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.code+": "+result.message);return result;
}
std::pair<double,double> extent(const kernel::BodyResult& body,unsigned axis) {
    double low=std::numeric_limits<double>::infinity(),high=-low;
    for(auto p:body.mesh.vertices){const double v=axis==0?p.x:axis==1?p.y:p.z;low=std::min(low,v);high=std::max(high,v);}return {low,high};
}
std::set<std::string> y_copy_faces(const kernel::BodyResult& body) {
    std::set<std::string> result;for(const auto& ref:body.mesh.original_references.triangle_references)
        if(ref.semantic_key.starts_with("pattern:copy-x0-y1-z0:"))result.insert(ref.semantic_key);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;command_host::Interaction interaction;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    options.interaction=[&]{return interaction;};command_host::Host host(live,kernel,dir,options);
    run(host,"new",{{"type","part"},{"name","copy-commands"}});const auto id=live.active_document_id();
    const auto box=run(host,"box.create",{{"length_mm","6"},{"width_mm","4"},{"height_mm","2"}}).data.at("container").get<std::string>();
    run(host,"placement.set",{{"object",box},{"values",{{"x",20}}}});
    auto* part=live.open_part(id);const auto source=part->session.document().body_history.active_body_id();
    const auto body=[&](const std::string& object)->const kernel::BodyResult& {return part->session.calculated_boundaries().back().body_outputs.at(object).get();};
    const auto lock=[&](const std::string& object,const std::string& key,bool locked){return run(host,"value_lock.set",{{"object",object},{"key",key},{"locked",locked}});};
    const auto mirror=run(host,"mirror.create",{{"source",source},{"local_plane","yz"},{"placement",{{"x",-2}}}}).data.at("object").get<std::string>();
    near(body(mirror).volume,48);near(extent(body(mirror),0).first,-27);near(extent(body(mirror),0).second,-21);
    const auto initial=workspace::derived_copy_definition(live,id,mirror);
    run(host,"undo");require(!part->session.document().body_history.find(mirror),"Mirror create required more than one Undo");
    run(host,"redo");require(workspace::derived_copy_definition(live,id,mirror)==initial,"Mirror Redo changed identity or properties");
    auto revision=part->session.revision();const auto* cache=part->session.calculated_boundaries().data();
    require(run(host,"mirror.set",{{"object",mirror},{"name",initial.name}}).data.at("changed")==false&&
        part->session.revision()==revision&&part->session.calculated_boundaries().data()==cache,"No-op Mirror edit recalculated or committed");
    run(host,"mirror.set",{{"object",mirror},{"placement",{{"x",-5}}}});near(extent(body(mirror),0).first,-33);
    run(host,"undo");near(extent(body(mirror),0).first,-27);run(host,"redo");near(extent(body(mirror),0).first,-33);run(host,"undo");
    lock(mirror,"placement:x",true);
    require(host.execute({{"command","mirror.set"},{"arguments",{{"object",mirror},{"placement",{{"x",3}}}}}}).code=="parameter_not_editable","Mirror placement lock was bypassed");
    lock(mirror,"placement:x",false);
    const auto reject=[&](const char* command,Json args) {
        const auto rev=part->session.revision();const auto* data=part->session.calculated_boundaries().data();
        require(!host.execute({{"command",command},{"arguments",std::move(args)}}).ok,"Invalid copy command accepted");
        require(part->session.revision()==rev&&part->session.calculated_boundaries().data()==data,"Rejected copy changed document or calculated data");
    };
    reject("mirror.create",{{"source",source}});
    reject("mirror.create",{{"source",source},{"local_plane","bad"}});
    reject("mirror.create",{{"source",source},{"local_plane","yz"},{"reference",{{"owner",box},{"key","x_min"}}}});
    reject("mirror.set",{{"object",mirror},{"source",mirror}});
    reject("mirror.set",{{"object",mirror},{"source","missing"}});
    reject("pattern.set",{{"object",mirror},{"mode","linear"}});
    reject("mirror.set",{{"object",mirror},{"reference",{{"owner",box},{"key","missing"}}}});
    reject("mirror.set",{{"object",mirror},{"reference",{{"owner",box},{"key","x_min"},{"instance_path","invalid"}}}});
    run(host,"body.create",{{"name","Later"}});const auto later=part->session.document().body_history.active_body_id();
    run(host,"box.create",{{"length_mm","2"},{"width_mm","2"},{"height_mm","2"}});
    reject("mirror.set",{{"object",mirror},{"source",later}});
    run(host,"body.activate",{{"body",source}});
    const auto original_plane=run(host,"mirror.create",{{"source",source},{"reference",{{"owner",box},{"key","x_min"}}}}).data.at("object").get<std::string>();
    near(extent(body(original_plane),0).first,11);near(extent(body(original_plane),0).second,17);
    run(host,"body.activate",{{"body",source}});
    const auto directions=Json::array({Json{{"axis","x"},{"spacing_mm",30},{"count",3},{"distribution","symmetric"}},
        Json{{"axis","y"},{"spacing_mm",20},{"count",2},{"reverse_count",2},{"distribution","both"}}});
    const auto pattern=run(host,"pattern.create",{{"source",source},{"linear",directions}}).data.at("object").get<std::string>();
    near(body(pattern).volume,11*48);near(extent(body(pattern),0).first,-13);near(extent(body(pattern),0).second,53);
    near(extent(body(pattern),1).first,-42);near(extent(body(pattern),1).second,22);
    const auto keys=y_copy_faces(body(pattern));require(!keys.empty(),"Grid has no source-derived Y copy faces");
    const auto saved_linear=workspace::derived_copy_definition(live,id,pattern).parameters.pattern->linear;
    lock(pattern,"pattern:spacing:1",true);auto spacing=directions;spacing[1]["spacing_mm"]=21;
    reject("pattern.set",{{"object",pattern},{"linear",spacing}});lock(pattern,"pattern:spacing:1",false);
    lock(pattern,"pattern:angle",true);
    reject("pattern.set",{{"object",pattern},{"mode","circular"},{"count",6}});
    run(host,"pattern.set",{{"object",pattern},{"mode","circular"},{"count",4}});near(body(pattern).volume,3*48);
    reject("pattern.set",{{"object",pattern},{"count",6}});lock(pattern,"pattern:angle",false);
    run(host,"pattern.set",{{"object",pattern},{"count",6}});near(body(pattern).volume,5*48);
    run(host,"pattern.set",{{"object",pattern},{"mode","linear"}});near(body(pattern).volume,11*48);
    require(workspace::derived_copy_definition(live,id,pattern).parameters.pattern->linear==saved_linear,"Mode change discarded inactive directions");
    auto larger=directions;larger[0]["count"]=5;
    run(host,"pattern.set",{{"object",pattern},{"linear",larger}});near(body(pattern).volume,19*48);
    require(y_copy_faces(body(pattern))==keys,"Changing X count renamed the existing Y copy");
    for(auto bad:{Json(-1),Json(1),Json(1001),Json(3.5),Json("4"),Json(std::uint64_t(-1))}) {
        auto invalid=directions;invalid[0]["count"]=bad;reject("pattern.set",{{"object",pattern},{"linear",invalid}});
    }
    auto invalid=directions;invalid[1]["axis"]="x";reject("pattern.set",{{"object",pattern},{"linear",invalid}});
    invalid=directions;invalid[0]["count"]=4;reject("pattern.set",{{"object",pattern},{"linear",invalid}});
    invalid=directions;invalid[0]["count"]=999;reject("pattern.set",{{"object",pattern},{"linear",invalid}});
    reject("pattern.set",{{"object",pattern},{"count",4}});reject("pattern.set",{{"object",pattern},{"linear",Json::array()}});
    interaction.editing=true;reject("pattern.set",{{"object",pattern},{"mode","circular"}});interaction={};
    const auto stale=workspace::prepare_derived_copy_edit(live,id,mirror);lock(mirror,"placement:y",true);
    bool rejected=false;try{static_cast<void>(workspace::commit_derived_copy(live,kernel,stale,stale.initial));}
    catch(const workspace::DerivedCopyError& error){rejected=std::string(error.code)=="document_changed";}require(rejected,"Stale property snapshot committed");
    run(host,"save");std::vector<kernel::BodyResult> native_cache;const auto native=document::PartDocument::load(dir/"copy-commands.prtz",&native_cache);
    const auto cold=kernel.evaluate_history(native.kernel_operations());near(cold.back().body_outputs.at(pattern)->volume,19*48);
    require(native.body_history.find(pattern)->derived_copy==part->session.document().body_history.find(pattern)->derived_copy&&
        y_copy_faces(cold.back().body_outputs.at(pattern))==keys,"Native calculation lost Pattern settings or stable parents");
    run(host,"new",{{"type","part"},{"name","simple-source"}});const auto simple_id=live.active_document_id();
    const auto simple_box=run(host,"box.create",{{"length_mm","10"},{"width_mm","10"},{"height_mm","10"}}).data.at("container").get<std::string>();run(host,"save");
    const auto simple_revision=live.open_part(simple_id)->session.revision();
    run(host,"new",{{"type","assembly"},{"name","copy-assembly"}});const auto assembly_id=live.active_document_id();
    const auto first=run(host,"component.insert",{{"source",simple_id}}).data.at("occurrence").get<std::string>();
    const auto second=run(host,"component.insert",{{"source",simple_id}}).data.at("occurrence").get<std::string>();
    auto* assembly=live.open_assembly(assembly_id);auto next=assembly->session.document();next.find_occurrence(first)->placement.x=20;assembly->session.commit(std::move(next));
    const auto copied=run(host,"mirror.create",{{"source",first},{"local_plane","yz"},{"placement",{{"x",-2}}}}).data.at("object").get<std::string>();
    near(assembly->session.document().find_occurrence(copied)->calculated_source->volume,1000);
    near(extent(assembly->session.document().find_occurrence(copied)->calculated_source,0).first,-29);
    next=assembly->session.document();next.find_occurrence(copied)->visible=false;next.find_occurrence(copied)->body_color_override="#123456";assembly->session.commit(std::move(next));
    lock(copied,"placement:z",true);
    require(assembly->session.document().find_occurrence(copied)->copy_placement.value_locks.contains("z")&&
        host.execute({{"command","mirror.set"},{"arguments",{{"object",copied},{"placement",{{"z",3}}}}}}).code=="parameter_not_editable","Assembly copy used ordinary component locks");
    run(host,"mirror.set",{{"object",copied},{"source",second},{"name","Jiné jméno"}});
    const auto* retained=assembly->session.document().find_occurrence(copied);
    require(!retained->visible&&retained->body_color_override=="#123456","Editing a copy replaced its own presentation with source properties");
    near(extent(retained->calculated_source,0).first,-9);
    const auto ring=run(host,"pattern.create",{{"source",first},{"mode","circular"},{"count",3}}).data.at("object").get<std::string>();
    near(assembly->session.document().find_occurrence(ring)->calculated_source->volume,2000);
    require(assembly->session.document().find_occurrence(ring)->nested_snapshot.size()==2&&live.open_part(simple_id)->session.revision()==simple_revision,
        "Assembly Pattern lost exact copies or edited its source Part");
    run(host,"undo");require(!assembly->session.document().find_occurrence(ring),"Assembly Pattern did not undo in one step");run(host,"redo");
    run(host,"save");const auto saved=assembly::AssemblyDocument::load(dir/"copy-assembly.asmz");
    require(saved.find_occurrence(ring)->derived_copy->source_id==first&&saved.find_occurrence(copied)->copy_placement.value_locks.contains("z")&&
        !saved.find_occurrence(copied)->visible&&saved.find_occurrence(copied)->body_color_override=="#123456","Native Assembly lost copy-specific data");
    // Pure CLI has no GUI scene refresh. Calculating a copy must nevertheless
    // use the current open source, without saving or regenerating that Part.
    run(host,"activate",{{"document",simple_id}});
    run(host,"box.set",{{"container",simple_box},{"length_mm","20"}});
    const auto unsaved_revision=live.open_part(simple_id)->session.revision();
    run(host,"activate",{{"document",assembly_id}});
    const auto last_copy=assembly->session.document().find_occurrence(copied)->calculated_source;
    run(host,"mirror.get",{{"object",copied}});
    require(assembly->session.document().find_occurrence(copied)->calculated_source.shares_with(last_copy),"Copy query regenerated after a source edit");
    near(last_copy->volume,1000);
    run(host,"mirror.set",{{"object",copied},{"placement",{{"x",-3}}}});
    near(assembly->session.document().find_occurrence(copied)->calculated_source->volume,2000);
    near(extent(assembly->session.document().find_occurrence(copied)->calculated_source,0).first,-16);
    require(assembly->session.document().find_occurrence(first)->calculated_source.shares_with(
        assembly->session.document().find_occurrence(second)->calculated_source),"Repeated sources lost geometry sharing on explicit copy calculation");
    run(host,"undo");
    near(assembly->session.document().find_occurrence(copied)->calculated_source->volume,1000);
    near(assembly->session.document().find_occurrence(first)->calculated_source->volume,2000);
    run(host,"redo");
    near(assembly->session.document().find_occurrence(copied)->calculated_source->volume,2000);
    run(host,"pattern.set",{{"object",ring},{"count",4}});
    near(assembly->session.document().find_occurrence(ring)->calculated_source->volume,6000);
    require(live.open_part(simple_id)->session.revision()==unsaved_revision&&live.open_part(simple_id)->session.is_dirty()&&
        document::PartDocument::load(dir/"simple-source.prtz").find_container(simple_box)->box.length==10,
        "Copy calculation saved, regenerated or edited the authoritative source Part");
}
}
int main(){try{const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-copy-command-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create copy command directory");kernel::OcctKernel kernel;verify(kernel,dir);
    require(dir.parent_path()==root,"Unexpected copy command cleanup path");fs::remove_all(dir);
    std::cout<<"Derived copy commands: independent geometry, references, modes, locks, boundaries, atomic errors, native files and Undo passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
