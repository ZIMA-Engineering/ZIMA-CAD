#include <zima/command_host/host.hpp>
#include <zima/workspace/body_reference_operations.hpp>
#include <cmath>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void near(double a,double b){if(std::abs(a-b)>1e-7)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
commands::Result run(command_host::Host& host,const char* command,Json args=Json::object()) {auto result=host.execute({{"command",command},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.code+": "+result.message);return result;}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);run(host,"new",{{"type","part"},{"name","body-refs"}});const auto id=live.active_document_id();auto* state=live.open_part(id);
    const auto first=state->session.document().body_history.active_body_id();const auto first_feature=run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}}).data.at("container").get<std::string>();
    run(host,"placement.set",{{"object",first},{"values",{{"reference_offset:0",11}}}});
    const auto target=run(host,"body.create",{{"name","Follower"}}).data.at("body").get<std::string>();run(host,"box.create",{{"length_mm","2"},{"width_mm","3"},{"height_mm","4"}});
    const auto source_origin=state->session.document().body_history.find(first)->origin().id;
    const auto placement=[&]() -> const document::Placement& {return state->session.document().body_history.find(target)->scope.placement;};
    const auto set=[&](const std::string& body,int index,std::string owner,std::string key,double offset=0) {return run(host,"body.reference.set",{{"body",body},{"index",index},{"reference",{{"owner",std::move(owner)},{"key",std::move(key)}}},{"offset_mm",offset}}).data;};
    const auto initial=state->session.document().body_history;
    auto result=set(target,0,source_origin,"origin:plane:xy",2);require(result.at("changed")==true,"Body reference did not commit");near(placement().z,13);
    require(placement().references[0].owner_id==source_origin&&placement().references[0].semantic_key=="origin:plane:xy"&&placement().references[0].offset==2&&state->session.document().body_history.active_body_id()==target,"Body reference lost native source or editing context");
    const auto calculated=state->session.calculated_body_boundary(target,1);require(calculated.has_value(),"Body reference lost calculated target");near(calculated->volume,24);
    const auto placed=state->session.document().place_body_mesh(calculated->mesh,target);
    const auto min_z=[](const auto& mesh){return std::ranges::min(mesh.vertices,{},&kernel::Vec3::z).z;};near(min_z(placed)-min_z(calculated->mesh),13);
    const auto updated=state->session.document().body_history;run(host,"undo");require(state->session.document().body_history==initial,"Reference Undo did not restore Body graph");run(host,"redo");require(state->session.document().body_history==updated,"Reference Redo changed identity");
    const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    require(set(target,0,source_origin,"origin:plane:xy",2).at("changed")==false&&state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache,"Repeated Body reference recalculated or added history");
    const auto reject=[&](Json args,const char* code) {const auto graph=state->session.document().body_history;const auto rev=state->session.revision();const auto* shape=state->session.calculated_boundaries().data();const auto response=host.execute({{"command","body.reference.set"},{"arguments",std::move(args)}});require(!response.ok&&response.code==code,"Invalid Body reference returned wrong error");require(state->session.revision()==rev&&state->session.document().body_history==graph&&state->session.calculated_boundaries().data()==shape,"Rejected reference changed history or geometry");};
    reject({{"body",target},{"index",1},{"reference",{{"owner",source_origin},{"key","origin:plane:xy"}}}},"duplicate_reference");
    reject({{"body",first},{"index",0},{"reference",{{"owner",state->session.document().body_history.find(target)->origin().id},{"key","origin:plane:xy"}}}},"reference_not_available");
    reject({{"body",target},{"index",0},{"reference",{{"owner",state->session.document().body_history.find(target)->origin().id},{"key","origin:plane:xy"}}}},"reference_not_available");
    reject({{"body",target},{"index",0},{"reference",{{"owner",source_origin},{"key","missing"}}}},"reference_not_found");
    reject({{"body",target},{"index",5},{"reference",{{"owner",source_origin},{"key","origin:plane:xy"}}}},"invalid_arguments");
    reject({{"body",target},{"index",0},{"reference",{{"owner",source_origin},{"key","origin:plane:xy"},{"instance_path","another-occurrence"}}}},"invalid_arguments");
    reject({{"body",target},{"index",0},{"reference",{{"owner",source_origin},{"key","origin:plane:xy"},{"supports_offset",true}}}},"invalid_arguments");
    run(host,"value_lock.set",{{"object",target},{"key","placement:reference_offset:0"},{"locked",true}});
    set(target,0,id+":origin","origin:plane:xy",77);near(placement().z,13);near(placement().references[0].offset,13);require(placement().references[0].offset_locked,"Replacing a locked reference released its lock");
    run(host,"value_lock.set",{{"object",target},{"key","placement:reference_offset:0"},{"locked",false}});set(target,0,source_origin,"origin:plane:xy",2);
    run(host,"placement.set",{{"object",first},{"values",{{"reference_offset:0",17}}}});near(placement().z,19);
    std::string top_face;
    const auto faces=run(host,"reference.list",{{"owner",first_feature},{"kind","face"}}).data;
    for(const auto& item:faces.at("items")) {
        const auto face=run(host,"reference.get",item).data;
        if(!face.at("surface").is_null()&&face.at("surface").at("kind")=="plane"&&face.at("surface").at("axis")[2].get<double>()>.99&&!face.at("surface").at("reversed").get<bool>()) {
            top_face=face.at("key").get<std::string>();near(face.at("surface").at("origin")[2].get<double>(),32);break;
        }
    }
    require(!top_face.empty(),"Original Box top face not available");set(target,0,first_feature,top_face,1);near(placement().z,33);require(placement().references[0].owner_id==first_feature,"Body plane reference lost original feature owner");
    set(target,0,source_origin,"origin:point");near(placement().z,17);require(!placement().references[0].supports_offset&&placement().references[0].offset_locked,"Point reference retained an editable plane offset");
    reject({{"body",target},{"index",0},{"reference",{{"owner",source_origin},{"key","origin:point"}}},{"offset_mm",1}},"parameter_not_editable");
    reject({{"body",target},{"index",3},{"reference",{{"owner",source_origin},{"key","origin:axis:y"}}},{"offset_mm",1}},"parameter_not_editable");
    set(target,3,source_origin,"origin:axis:y");require(placement().references[3].orientation_only&&placement().references[3].orientation_role=="front","Explicit FRONT lost semantic slot");
    run(host,"save");const auto loaded=document::PartDocument::load(dir/"body-refs.prtz");require(loaded.body_history.find(target)->scope.placement==placement(),"Native Part lost Body references");
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path()),dir=parent/("zima-body-reference-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);require(fs::canonical(dir).parent_path()==parent,"Unexpected cleanup directory");fs::remove_all(dir);std::cout<<"Body reference command placement, source updates, locks, native identities and history passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
