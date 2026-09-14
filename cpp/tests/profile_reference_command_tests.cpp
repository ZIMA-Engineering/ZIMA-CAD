#include <zima/command_host/host.hpp>
#include <cmath>
#include <numbers>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool v,const char* text){if(!v)throw std::runtime_error(text);}
void near(double a,double b){if(!std::isfinite(a)||std::abs(a-b)>1e-6)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
void verify(const kernel::OcctKernel& kernel,fs::path directory,bool extrusion) {
    const std::string prefix=extrusion?"extrusion":"revolution";workspace::Workspace live;command_host::Interaction interaction;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};options.interaction=[&]{return interaction;};command_host::Host host(live,kernel,directory,options);
    const auto run=[&](const std::string& command,Json args=Json::object()){const auto input=args.dump();const auto result=host.execute({{"command",command},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(command+" "+input+": "+result.code+": "+result.message);return result.data;};
    run("new",{{"type","part"},{"name",prefix+"-reference"}});const auto id=live.active_document_id();auto* state=live.open_part(id);
    const auto source=run("construction.create",{{"kind","plane"},{"name","Source plane"},{"base_plane","xy"},{"values",{{"z",5}}}}).at("construction").get<std::string>();
    const auto source_entity=state->session.document().find_construction(source)->entity_id;
    const auto sketch=run("sketch.create",{{"name","Profile"},{"plane","XY"}}).at("sketch").get<std::string>();
    const auto line=[&](double x1,double y1,double x2,double y2){return run("sketch.segment.create",{{"sketch",sketch},{"first",{x1,y1}},{"second",{x2,y2}},{"snap_mm",0.000001}}).at("geometry").get<std::string>();};
    line(2,1,4,1);line(4,1,4,4);line(4,4,2,4);line(2,4,2,1);
    if(!extrusion){const auto axis=line(0,0,0,5);run("sketch.segment.centerline",{{"sketch",sketch},{"segment",axis},{"centerline",true}});}
    Json create={{"sketch",sketch}};if(extrusion)create["length_forward_mm"]=5;
    const auto target=run(prefix+".create",create).at("container").get<std::string>();
    const auto feature=[&](){return *state->session.document().find_container(target);};
    const auto owned=[&](){const auto& sketches=state->session.document().sketches;return *std::ranges::find(sketches,sketch,&sketcher::Sketch::id);};
    const auto volume=[&](){return state->session.calculated_boundaries().back().volume;};const double expected=extrusion?30:36*std::numbers::pi;near(volume(),expected);
    const auto request=[&](int index,const std::string& owner,const std::string& key,double offset=0){return Json{{"container",target},{"index",index},{"reference",{{"owner",owner},{"key",key}}},{"offset_mm",offset}};};
    const auto set=[&](Json args){return run(prefix+".reference.set",std::move(args));};
    const auto reject=[&](const std::string& command,Json args,const char* code){const auto before=feature();const auto sketch_before=owned().serialized();const auto rev=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();const auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
        if(result.ok||result.code!=code)throw std::runtime_error(command+" expected "+code+", got "+result.code+": "+result.message);
        require(feature()==before&&owned().serialized()==sketch_before&&state->session.revision()==rev&&state->session.calculated_boundaries().data()==cache&&!host.change(),"Rejected reference altered profile, owned Sketch or body");};
    const auto before=feature();const auto original_sketch=owned();set(request(0,source_entity,"plane",2));near(feature().placement.z,7);near(volume(),expected);
    const auto assigned=feature();const auto assigned_sketch=owned();require(assigned_sketch.id==sketch&&assigned_sketch.owner_container_id==target&&assigned_sketch.segments==original_sketch.segments,"Reference changed profile ownership or curve identities");
    run("undo");require(feature()==before&&owned().serialized()==original_sketch.serialized(),"Reference Undo did not restore owned Sketch and feature together");run("redo");require(feature()==assigned&&owned().serialized()==assigned_sketch.serialized(),"Reference Redo changed native identities");
    const auto rev=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();require(!set(request(0,source_entity,"plane",2)).at("changed").get<bool>()&&state->session.revision()==rev&&state->session.calculated_boundaries().data()==cache&&!host.change(),"Repeated profile reference recalculated or added history");
    const auto first_mesh=state->session.calculated_boundaries().back().mesh;set(request(0,source_entity,"plane",4));near(feature().placement.z,9);near(volume(),expected);
    const auto min_z=[](const auto& mesh){return std::ranges::min(mesh.vertices,{},&kernel::Vec3::z).z;};near(min_z(state->session.calculated_boundaries().back().mesh)-min_z(first_mesh),2);
    auto facade=request(0,source_entity,"plane",6);facade.erase("container");facade["object"]=target;run("placement.reference.set",facade);near(feature().placement.z,11);near(volume(),expected);
    reject(prefix+".reference.set",request(0,sketch,"anything"),"reference_not_available");reject(prefix+".reference.set",request(0,target,"anything"),"reference_not_available");
    reject(extrusion?"revolution.reference.set":"extrusion.reference.set",request(0,source_entity,"plane"),"wrong_feature");
    reject(prefix+".reference.set",request(9,source_entity,"plane"),"invalid_arguments");reject(prefix+".reference.set",request(0,source_entity,"missing"),"reference_not_found");
    interaction.editing=true;reject(prefix+".reference.set",request(0,source_entity,"plane"),"editing_in_progress");interaction={};
    run("save");std::vector<kernel::BodyResult> saved_cache;const auto saved=document::PartDocument::load(directory/(prefix+"-reference.prtz"),&saved_cache);
    require(saved.find_container(target)&&*saved.find_container(target)==feature(),"Native profile definition changed on reopen");
    require(std::ranges::find(saved.sketches,sketch,&sketcher::Sketch::id)->serialized()==owned().serialized(),"Native save lost owned profile data");near(saved_cache.back().volume,expected);
}
}
int main(){try{const auto root=fs::canonical(fs::temp_directory_path()),directory=root/("zima-profile-reference-"+document::PartDocument::create_default().document_id);require(fs::create_directory(directory),"Cannot create test directory");kernel::OcctKernel kernel;verify(kernel,directory,true);verify(kernel,directory,false);require(fs::canonical(directory).parent_path()==root,"Invalid cleanup path");fs::remove_all(directory);std::cout<<"Profile references: ownership, volume, exact displacement, no-op and native Undo/Redo passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
