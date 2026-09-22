#include <zima/command_host/host.hpp>
#include <zima/workspace/primitive_operations.hpp>
#include <cmath>
#include <zima/workspace/placement_edit.hpp>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){if(std::abs(actual-expected)>1e-6)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
void verify(const kernel::OcctKernel& kernel,fs::path directory,const workspace::PrimitiveDefinition& definition) {
    workspace::Workspace live;auto doc=document::PartDocument::create_default();const auto id=doc.document_id;const auto file=directory/(std::string(definition.command_name)+"-references.prtz");live.add_part(std::move(doc),{},file);live.activate(id);
    command_host::Interaction interaction;command_host::Options options;options.interaction=[&]{return interaction;};command_host::Host host(live,kernel,directory,options);
    const auto run=[&](const char* command,Json args=Json::object()){const auto input=args.dump();const auto result=host.execute({{"command",command},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(definition.command_name)+" "+command+" "+input+": "+result.code+": "+result.message);return result.data;};
    const auto plane=run("construction.create",{{"kind","plane"},{"name","Source plane"},{"base_plane","xy"},{"values",{{"z",5}}}}).at("construction").get<std::string>();
    const auto point=run("construction.create",{{"kind","point"},{"name","Source point"},{"values",{{"x",12},{"y",7},{"z",3}}}}).at("construction").get<std::string>();
    const auto source_plane=live.open_part(id)->session.document().find_construction(plane)->entity_id;
    const auto source_point=live.open_part(id)->session.document().find_construction(point)->container_origin.id;
    auto value=definition.create();const auto target=value.id;require(workspace::commit_primitive(live,kernel,id,value,workspace::PrimitiveEditMode::Create),"Cannot create primitive fixture");
    const auto state=[&]() -> const workspace::PartState& {return *live.open_part(id);};
    const auto feature=[&](){return *state().session.document().find_container(target);};
    const auto volume=state().session.calculated_boundaries().back().volume;
    const auto request=[&](const std::string& object,int index,const std::string& owner,const std::string& key,double offset=0){return Json{{"object",object},{"index",index},{"reference",{{"owner",owner},{"key",key}}},{"offset_mm",offset}};};
    const auto set=[&](Json args){return run("placement.reference.set",std::move(args));};
    const auto reject=[&](Json args,const char* code){const auto before=feature();const auto rev=state().session.revision();const auto cache=state().session.calculated_boundaries();const auto result=host.execute({{"command","placement.reference.set"},{"arguments",std::move(args)}});
        if(result.ok||result.code!=code)throw std::runtime_error(std::string("Expected ")+code+", got "+result.code+": "+result.message);
        require(feature()==before&&state().session.revision()==rev&&!host.change()&&state().session.calculated_boundaries().back().source_fingerprint==cache.back().source_fingerprint,"Invalid primitive reference committed partial state");};
    const auto initial=feature();
    set(request(target,0,source_plane,"plane",7));near(feature().placement.z,12);near(state().session.calculated_boundaries().back().volume,volume);
    require(feature().placement.references.size()==2&&feature().placement.references.back().orientation_role=="front","Plane did not populate shared FRONT field");
    const auto assigned=feature();run("undo");require(feature()==initial,"Reference assignment was not one Undo step");run("redo");require(feature()==assigned,"Redo changed original primitive identity or references");
    const auto rev=state().session.revision();require(!set(request(target,0,source_plane,"plane",7)).at("changed").get<bool>()&&state().session.revision()==rev&&!host.change(),"No-op primitive reference created history");
    run("placement.set",{{"object",target},{"values",{{"reference_offset:0",9}}}});near(feature().placement.z,14);
    set(request(target,0,source_point,"point"));near(feature().placement.x,12);near(feature().placement.y,7);near(feature().placement.z,3);
    set(request(target,0,source_plane,"plane",99));near(feature().placement.z,3);near(feature().placement.references.front().offset,-2);require(feature().placement.references.front().offset_locked,"Replacing a point reference discarded its distance lock");
    reject(request(target,1,source_plane,"plane"),"duplicate_reference");reject(request(target,0,target,"anything"),"reference_not_available");
    const auto later=run("construction.create",{{"kind","point"},{"name","Later"}}).at("construction").get<std::string>();
    reject(request(target,0,live.open_part(id)->session.document().find_construction(later)->container_origin.id,"point"),"reference_not_available");
    reject(request(target,7,source_plane,"plane"),"invalid_arguments");reject(request(target,0,source_plane,"missing"),"reference_not_found");
    reject(request(target,1,source_point,"point",2),"parameter_not_editable");
    auto bad=request(target,0,source_plane,"plane");bad["reference"]["instance_path"]="nonlocal";reject(bad,"invalid_arguments");
    bad=request(target,0,source_plane,"plane");bad["reference"]["extra"]=true;reject(bad,"invalid_arguments");
    interaction.editing=true;reject(request(target,0,source_plane,"plane"),"editing_in_progress");interaction={};
    // The same facade consumes the existing construction transaction.
    set(request(later,0,source_point,"point"));const auto datum=*state().session.document().find_construction(later);near(datum.origin.x,12);near(datum.origin.z,3);
    const auto bound=feature();
    require(run("placement.reference.remove",{{"object",target},{"index",0}}).at("body_calculated")==true,"Primitive removal did not calculate through Properties");
    require(feature().placement.references.empty(),"Primitive removal retained its paired orientation");near(state().session.calculated_boundaries().back().volume,volume);
    const auto freed=feature();run("undo");require(feature()==bound,"Primitive reference removal Undo failed");run("redo");require(feature()==freed,"Primitive reference removal Redo failed");
    run("save");std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(file,&cache);
    require(saved.find_container(target)&&*saved.find_container(target)==feature(),"Native Part changed primitive identity or reference definition");near(cache.back().volume,volume);
    const auto operations=saved.kernel_operations();require(cache.back().source_fingerprint==kernel::history_fingerprint(operations,operations.size()),"Native save used stale body calculation");
}
void verify_bodies(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
    command_host::Host host(live,kernel,directory,options);
    const auto run=[&](const char* command,Json args=Json::object()){const auto result=host.execute({{"command",command},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.code+": "+result.message);return result.data;};
    run("new",{{"type","part"},{"name","primitive-body-references"}});const auto id=live.active_document_id();auto* state=live.open_part(id);
    const auto first=state->session.document().body_history.active_body_id();const auto source=run("box.create",{{"length_mm","10"},{"width_mm","10"},{"height_mm","10"}}).at("container").get<std::string>();
    run("placement.set",{{"object",first},{"values",{{"reference_offset:0",11}}}});
    const auto body=run("body.create",{{"name","Target body"}}).at("body").get<std::string>();
    run("placement.set",{{"object",body},{"values",{{"reference_offset:0",30}}}});
    const auto target=run("cylinder.create",{{"radius_mm","1"},{"height_mm","2"}}).at("container").get<std::string>();
    const auto geometry=workspace::part_construction_dimension_geometry(state->session.document(),state->session.calculated_boundaries());
    std::string key;double plane_z{},normal_z{};
    for(std::size_t i=0;i<geometry.triangle_references.size();++i) {
        const auto& ref=geometry.triangle_references[i];if(ref.owner_id!=source)continue;
        const auto a=geometry.vertices[geometry.triangles[i*3]],b=geometry.vertices[geometry.triangles[i*3+1]],c=geometry.vertices[geometry.triangles[i*3+2]];
        const double nz=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);if(std::abs(nz)<1e-8||std::abs(a.z-b.z)>1e-8||std::abs(a.z-c.z)>1e-8)continue;
        key=ref.semantic_key;plane_z=a.z;normal_z=nz>0?1:-1;break;
    }
    require(!key.empty(),"Missing original Box plane for cross-Body reference");
    const auto set=[&](const std::string& object,const std::string& owner,const std::string& semantic,double offset){return run("placement.reference.set",{{"object",object},{"index",0},{"reference",{{"owner",owner},{"key",semantic}}},{"offset_mm",offset}});};
    set(target,source,key,2);const auto world_z=[&]{const auto& doc=state->session.document();return doc.find_container(target)->placement.z+doc.body_history.find(body)->scope.translation().z;};
    near(world_z(),plane_z+2*normal_z);near(state->session.document().find_container(target)->placement.z,plane_z+2*normal_z-30);
    const auto snapshot=state->session.document();const auto revision=state->session.revision();
    const auto invalid=host.execute({{"command","placement.reference.set"},{"arguments",{{"object",source},{"index",0},{"reference",{{"owner",id+":origin"},{"key","origin:plane:xy"}}}}}});
    require(!invalid.ok&&invalid.code=="inactive_body"&&state->session.revision()==revision&&state->session.document().history==snapshot.history,"Inactive primitive accepted mutation");
    run("placement.set",{{"object",first},{"values",{{"reference_offset:0",17}}}});near(world_z(),plane_z+6+2*normal_z);
    // The facade delegates Body references to the existing Body transaction.
    set(body,id+":origin","origin:plane:xy",40);near(state->session.document().body_history.find(body)->scope.translation().z,40);near(world_z(),plane_z+6+2*normal_z);
    run("save");const auto loaded=document::PartDocument::load(directory/"primitive-body-references.prtz");require(loaded.find_container(target)->placement.references.front().owner_id==source,"Cross-Body native reference lost original source");
    near(loaded.find_container(target)->placement.z+loaded.body_history.find(body)->scope.translation().z,plane_z+6+2*normal_z);
}
}
int main(){try{const auto root=fs::canonical(fs::temp_directory_path()),directory=root/("zima-primitive-reference-"+document::PartDocument::create_default().document_id);require(fs::create_directory(directory),"Cannot create test directory");kernel::OcctKernel kernel;for(const auto& definition:workspace::primitive_definitions())verify(kernel,directory,definition);verify_bodies(kernel,directory);require(fs::canonical(directory).parent_path()==root,"Invalid cleanup path");fs::remove_all(directory);std::cout<<"Original placement references: all primitive kinds, orientation, locks, atomic errors and native Undo/Redo passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
