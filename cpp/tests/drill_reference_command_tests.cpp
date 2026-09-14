#include <zima/command_host/host.hpp>
#include <zima/workspace/drill_reference_operations.hpp>
#include <cmath>
#include <numbers>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b){if(!std::isfinite(a)||std::abs(a-b)>1e-5)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
std::array<kernel::Vec3,2> feature_bounds(const kernel::ViewerMesh& mesh,const std::string& id) {
    kernel::Vec3 lo{1e100,1e100,1e100},hi{-1e100,-1e100,-1e100};bool found=false;
    for(std::size_t i=0;i<mesh.triangle_references.size();++i)if(mesh.triangle_references[i].owner_id==id)for(int n=0;n<3;++n) {
        const auto p=mesh.vertices.at(mesh.triangles.at(3*i+n));found=true;
        lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
    }
    require(found,"Calculated mesh has no original Hole/Opening faces");return {lo,hi};
}
void verify(const kernel::OcctKernel& kernel,fs::path directory,const std::string& kind) {
    const bool hole=kind=="hole";const std::string prefix=hole?"hole":"opening";workspace::Workspace live;command_host::Interaction interaction;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};options.interaction=[&]{return interaction;};
    command_host::Host host(live,kernel,directory,options);
    const auto run=[&](const std::string& name,Json args=Json::object()){auto r=host.execute({{"command",name},{"arguments",std::move(args)}});if(!r.ok)throw std::runtime_error(kind+" "+name+": "+r.code+": "+r.message);return r.data;};
    run("new",{{"type","part"},{"name",kind+"-reference"}});run("box.create",{{"length_mm","40"},{"width_mm","40"},{"height_mm","40"}});
    const auto doc_id=live.active_document_id(),origin=doc_id+":origin";auto* state=live.open_part(doc_id);
    Json create={{"bore_length_mm",10},{"placement",{{"z",-20}}}};
    if(hole)create["diameter_mm"]=10;else {create["type"]=kind;create["thread_length_mm"]=5;create["chamfer_enabled"]=false;create["drill_point_enabled"]=false;
        if(kind=="plain")create["nominal_diameter_mm"]=10;else create["designation"]=kind=="metric"?"M10":kind=="pipe"?"G 1/2":"W 1/2";}
    const auto target=run(prefix+".create",create).at("container").get<std::string>();
    const auto feature=[&]()->const document::HistoryContainer&{return *state->session.document().find_container(target);};
    const auto volume=[&]{return state->session.calculated_boundaries().back().volume;};const auto original=feature();
    const auto r=hole?5.:(kind=="plain"?original.thread.nominal_diameter:original.thread.profile_diameter)/2;
    const auto expected=64000-std::numbers::pi*r*r*10;near(volume(),expected);
    const auto request=[&](int index,const std::string& owner,const std::string& key,double offset){return Json{{"container",target},{"index",index},{"reference",{{"owner",owner},{"key",key}}},{"offset_mm",offset}};};
    const auto set=[&](Json args){return run(prefix+".reference.set",std::move(args));};
    const auto fail=[&](const std::string& name,Json args,const char* code) {
        const auto old=feature();const auto rev=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
        const auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
        if(result.ok||result.code!=code)throw std::runtime_error(name+" expected "+code+", got "+result.code+": "+result.message);
        require(feature()==old&&state->session.revision()==rev&&state->session.calculated_boundaries().data()==cache&&!host.change(),"Rejected reference changed model or calculated cache");
    };
    // The existing referenced Hole uses local +Y; Opening uses local -Y.
    // With FRONT=XY they enter this block from its opposite Z faces.
    const auto entry_z=hole?-20.:20.;
    const auto first=request(0,origin,"origin:plane:xy",entry_z);set(first);near(feature().placement.z,entry_z);near(volume(),expected);
    require(feature().placement.references.front().orientation_role=="front"&&feature().placement.reference_valid,"Reference did not use GUI FRONT normalization");
    const auto assigned=feature();run("undo");require(feature()==original,"Reference Undo lost Hole/Opening identity");run("redo");require(feature()==assigned,"Reference Redo changed Hole/Opening identity");
    const auto rev=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();require(set(first).at("changed")==false&&state->session.revision()==rev&&state->session.calculated_boundaries().data()==cache&&!host.change(),"Repeated reference recalculated");
    set(request(1,origin,"origin:plane:yz",3));near(feature().placement.x,3);near(feature().placement.z,entry_z);near(volume(),expected);
    const auto moved=feature();const auto old_bounds=feature_bounds(state->session.calculated_boundaries().back().mesh,target);
    near(old_bounds[0].z,hole?-20.:10.);near(old_bounds[1].z,hole?-10.:20.);
    auto facade=request(1,origin,"origin:plane:yz",7);facade.erase("container");facade["object"]=target;run("placement.reference.set",facade);
    near(feature().placement.x,7);near(volume(),expected);const auto new_bounds=feature_bounds(state->session.calculated_boundaries().back().mesh,target);
    near(new_bounds[0].x-old_bounds[0].x,4);near(new_bounds[1].x-old_bounds[1].x,4);near(new_bounds[0].y,old_bounds[0].y);near(new_bounds[1].z,old_bounds[1].z);
    require(feature().hole.sketch_id==original.hole.sketch_id&&feature().hole.circle_id==original.hole.circle_id&&feature().hole.chamfer_point_ids==original.hole.chamfer_point_ids&&feature().hole.tip_point_ids==original.hole.tip_point_ids&&feature().feature_id==original.feature_id,"Moving the opening replaced persisted profile identities");
    if(!hole&&kind!="plain")require(std::ranges::any_of(state->session.calculated_boundaries().back().mesh.triangle_references,[](const auto& ref){return ref.is_thread_surface();}),"Reference edit dropped the thread sheet");
    run("undo");require(feature()==moved,"Facade Undo did not restore complete reference placement");run("redo");
    fail(prefix+".reference.set",request(0,target,"missing",0),"reference_not_available");
    fail(prefix+".reference.set",request(0,original.hole.sketch_id,"curve",0),"reference_not_available");
    fail(prefix+".reference.set",request(5,origin,"origin:plane:xy",0),"invalid_arguments");
    fail(prefix+".reference.set",request(0,origin,"missing",0),"reference_not_found");
    fail(hole?"opening.reference.set":"hole.reference.set",first,"wrong_feature");
    auto wrong=first;wrong["reference"]["instance_path"]="foreign";fail(prefix+".reference.set",wrong,"invalid_arguments");
    wrong=first;wrong["reference"]["extra"]=true;fail(prefix+".reference.set",wrong,"invalid_arguments");
    interaction.editing=true;fail(prefix+".reference.set",first,"editing_in_progress");interaction={};
    const auto bound=feature();
    require(run("placement.reference.remove",{{"object",target},{"index",1}}).at("body_calculated")==true,"Hole/Opening removal did not calculate through Properties");
    require(std::ranges::none_of(feature().placement.references,[&](const auto& ref){return ref.semantic_key=="origin:plane:yz";}),"Hole/Opening removal retained a paired source");near(volume(),expected);
    const auto freed=feature();run("undo");require(feature()==bound,"Hole/Opening removal Undo lost profile identity");run("redo");require(feature()==freed,"Hole/Opening removal Redo changed profile identity");
    run("save");std::vector<kernel::BodyResult> saved_cache;const auto saved=document::PartDocument::load(directory/(kind+"-reference.prtz"),&saved_cache);
    require(saved.find_container(target)&&*saved.find_container(target)==feature(),"Native save changed Hole/Opening placement or source identities");near(saved_cache.back().volume,expected);
}
}
int main(){try{const auto root=fs::canonical(fs::temp_directory_path()),dir=root/("zima-drill-reference-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);kernel::OcctKernel kernel;for(const auto* kind:{"hole","plain","metric","whitworth","pipe"})verify(kernel,dir,kind);require(fs::canonical(dir).parent_path()==root,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Hole and Opening references: exact wall displacement, thread sheets, volumes, identity, Undo and persistence passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
