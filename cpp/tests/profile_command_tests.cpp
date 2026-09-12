#include <zima/command_host/host.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/document/file_path.hpp>
#include <iostream>
#include <numbers>
#include <cmath>
#include <algorithm>
using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(double actual,double expected){if(!std::isfinite(actual)||std::abs(actual-expected)>1e-5)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
struct Fixture {
    workspace::Workspace live;const kernel::OcctKernel& kernel;fs::path directory;command_host::Interaction interaction;command_host::Host host;
    Fixture(const kernel::OcctKernel& kernel,fs::path directory):kernel(kernel),directory(std::move(directory)),host(live,kernel,this->directory,options()){}
    command_host::Options options(){command_host::Options o;o.settings=[]{return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};o.interaction=[this]{return interaction;};return o;}
    Json run(const char* command,Json args=Json::object()){
        const auto r=host.execute({{"command",command},{"arguments",std::move(args)}});
        if(!r.ok)throw std::runtime_error((live.open_part(live.active_document_id())?part().session.document().name:live.active_document_id())+" "+std::string(command)+": "+r.code+": "+r.message);return r.data;
    }
    workspace::PartState& part(){return *live.open_part(live.active_document_id());}
    const document::PartDocument& doc(){return part().session.document();}
    void reject(const char* command,Json args,const char* code){
        const auto before=doc();const auto revision=part().session.revision();const auto* cache=part().session.calculated_boundaries().data();
        const auto r=host.execute({{"command",command},{"arguments",std::move(args)}});
        if(r.ok||r.code!=code)throw std::runtime_error(std::string(command)+" expected "+code+", got "+r.code+": "+r.message);
        require(doc().history==before.history&&doc().history_order==before.history_order&&doc().history_cursor==before.history_cursor&&
            doc().body_history==before.body_history&&doc().constructions==before.constructions&&doc().sketches.size()==before.sketches.size()&&
            part().session.revision()==revision&&part().session.calculated_boundaries().data()==cache&&!host.change(),"Rejected profile changed document/history/cache");
        for(std::size_t i=0;i<before.sketches.size();++i)require(before.sketches[i].serialized()==doc().sketches[i].serialized(),"Rejected profile changed owned Sketch");
    }
    std::string sketch(){return run("sketch.create",{{"name","Profil"},{"plane","XY"}}).at("sketch").get<std::string>();}
    std::string line(const std::string& sketch,double x1,double y1,double x2,double y2){return run("sketch.segment.create",{{"sketch",sketch},{"first",{x1,y1}},{"second",{x2,y2}},{"snap_mm",0.000001}}).at("geometry").get<std::string>();}
    std::string rectangle(double x,double y,double width,double height){const auto id=sketch();line(id,x,y,x+width,y);line(id,x+width,y,x+width,y+height);line(id,x+width,y+height,x,y+height);line(id,x,y+height,x,y);return id;}
    double volume(){require(!part().session.calculated_boundaries().empty(),"Missing calculated body");return part().session.calculated_boundaries().back().volume;}
};
void front_reference() {
    document::ConstructionReference stale, front, top;
    stale.owner_id="obsolete";stale.orientation_only=true;stale.supports_offset=true;
    front.owner_id="origin";front.semantic_key="origin:plane:xy";front.supports_offset=true;front.offset=12;
    top.owner_id="origin";top.semantic_key="origin:plane:yz";top.supports_offset=true;
    std::vector<document::ConstructionReference> references{stale,front,top};
    workspace::normalize_owned_profile_front_references(references,true);
    require(references.size()==3 && references[0].semantic_key==front.semantic_key &&
        references[1].orientation_role=="top" && references[2].orientation_only &&
        references[2].semantic_key==front.semantic_key && references[2].offset==12 &&
        references[2].orientation_role=="front","Removing a stale row lost the explicit profile Front reference");
}
void extrusion(const kernel::OcctKernel& kernel,fs::path directory){
    Fixture f(kernel,directory);f.run("new",{{"type","part"},{"name","profile-extrusion"}});
    const auto sketch=f.rectangle(0,0,10,20);const auto profile=*std::ranges::find(f.doc().sketches,sketch,&sketcher::Sketch::id);
    const auto source=*f.doc().find_container(profile.owner_container_id);const auto history_count=f.doc().history.size();
    const auto created=f.run("extrusion.create",{{"sketch",sketch},{"length_forward_mm",5},{"name","Vytažení CLI"}});
    const auto id=created.at("container").get<std::string>();const auto feature=created.at("feature").get<std::string>();
    require(id==source.id&&feature!=source.feature_id&&f.doc().history.size()==history_count,"Sketch conversion changed container or history position");
    require(f.doc().find_container(id)->container_origin==source.container_origin&&f.doc().find_container(id)->feature_parent_id==id,"Conversion lost native ancestry");
    require(created.at("sketch")==sketch&&created.at("profile_source")=="internal","Conversion lost owned profile");near(f.volume(),1000);
    const auto get_revision=f.part().session.revision();const auto* get_cache=f.part().session.calculated_boundaries().data();f.run("extrusion.get",{{"container",id}});
    require(f.part().session.revision()==get_revision&&f.part().session.calculated_boundaries().data()==get_cache&&!f.host.change(),"Profile query changed state");
    f.run("undo");require(*f.doc().find_container(id)==source,"Conversion Undo lost original Sketch container");f.run("redo");near(f.volume(),1000);require(f.doc().find_container(id)->feature_id==feature,"Conversion Redo changed feature identity");
    f.run("extrusion.set",{{"container",id},{"length_forward_mm",10}});near(f.volume(),2000);f.run("undo");near(f.volume(),1000);f.run("redo");
    f.run("extrusion.set",{{"container",id},{"extent","two_sides"},{"length_forward_mm",3},{"length_reverse_mm",7}});near(f.volume(),2000);
    f.run("extrusion.set",{{"container",id},{"extent","symmetric"},{"length_forward_mm",5}});near(f.volume(),2000);require(f.doc().find_container(id)->extrusion.length_reverse==5,"Symmetric reverse extent not synchronized");
    f.run("extrusion.set",{{"container",id},{"extent","one_side"},{"direction","reverse"},{"length_forward_mm",6}});near(f.volume(),1200);
    f.run("extrusion.set",{{"container",id},{"profile_offset_mm",2},{"placement",{{"x",3},{"rotation_z",90}}}});near(f.volume(),1200);
    require(std::ranges::find(f.doc().sketches,sketch,&sketcher::Sketch::id)->plane_offset==2,"Profile offset not synchronized to owned Sketch");
    for(const Json bad:{Json(-1),Json(0),Json(1000001),Json(true),Json("4")})f.reject("extrusion.set",{{"container",id},{"name","partial"},{"length_forward_mm",bad}},"invalid_arguments");
    f.reject("extrusion.set",{{"container",id},{"extent","symmetric"},{"length_forward_mm",3},{"length_reverse_mm",4}},"invalid_arguments");
    f.reject("extrusion.set",{{"container",id},{"end_forward","through_all"}},"invalid_arguments");
    f.reject("extrusion.set",{{"container",id},{"end_forward","up_to"}},"missing_reference");
    f.reject("extrusion.set",{{"container",id},{"extent","wrong"}},"invalid_arguments");
    f.reject("revolution.get",{{"container",id}},"wrong_feature");
    f.reject("extrusion.create",{{"sketch",sketch}},"profile_owned");
    auto locked=f.doc();locked.find_container(id)->value_locks.insert("length_forward");locked.find_container(id)->value_locks.insert("profile_offset");
    f.part().session.commit(std::move(locked),f.part().session.calculated_boundaries());
    f.reject("extrusion.set",{{"container",id},{"length_forward_mm",8}},"value_locked");
    f.reject("extrusion.set",{{"container",id},{"profile_offset_mm",8}},"value_locked");
    f.interaction.editing=true;f.reject("extrusion.set",{{"container",id},{"name","blocked"}},"editing_in_progress");f.interaction={};
    f.run("body.activate");f.reject("extrusion.set",{{"container",id},{"name","blocked"}},"inactive_body");f.run("body.activate",{{"body",created.at("body")}});
    f.run("save");std::vector<kernel::BodyResult> cache;const auto reopened=document::PartDocument::load(directory/"profile-extrusion.prtz",&cache);
    require(*reopened.find_container(id)==*f.doc().find_container(id),"Native profile parameters or identities lost");near(cache.back().volume,1200);
    // GUI can commit a changed owned Sketch with unchanged feature parameters.
    auto draft=*std::ranges::find(f.doc().sketches,sketch,&sketcher::Sketch::id);
    for(auto& point:draft.points)point.x*=2;
    workspace::commit_profile(f.live,kernel,f.doc().document_id,*f.doc().find_container(id),workspace::ProfileEditMode::Replace,draft);
    near(f.volume(),2400);f.run("undo");near(f.volume(),1200);
    // An owned draft cannot conceal that the existing Sketch belongs elsewhere.
    auto stolen=*std::ranges::find(f.doc().sketches,sketch,&sketcher::Sketch::id);
    auto other=document::PartDocument::create_extrusion_container(sketch);stolen.owner_container_id=other.id;
    const auto original=f.doc();const auto revision=f.part().session.revision();
    bool rejected=false;
    try {workspace::commit_profile(f.live,kernel,f.doc().document_id,other,workspace::ProfileEditMode::Create,stolen);}
    catch(const workspace::ProfileOperationError& error){rejected=std::string(error.code)=="profile_owned";}
    require(rejected && f.part().session.revision()==revision && f.doc().history==original.history && f.doc().sketches.front().serialized()==original.sketches.front().serialized(),"Owned draft transferred another feature's Sketch");
}
void thin_and_cut(const kernel::OcctKernel& kernel,fs::path directory){
    Fixture f(kernel,directory);f.run("new",{{"type","part"},{"name","profile-thin"}});
    const auto sketch=f.sketch();f.run("sketch.circle.create",{{"sketch",sketch},{"center",{0,0}},{"radius_mm",5}});
    f.reject("extrusion.create",{{"sketch",sketch},{"result_type","thin"},{"thin_thickness_mm",1},{"length_forward_mm",3}},"unsupported_operation");
    const auto made=f.run("extrusion.create",{{"sketch",sketch},{"length_forward_mm",3}});
    near(f.volume(),75*std::numbers::pi);
    f.reject("extrusion.set",{{"container",made.at("container")},{"result_type","thin"}},"unsupported_operation");
    f.run("new",{{"type","part"},{"name","profile-cut"}});f.run("box.create",{{"length_mm","10"},{"width_mm","10"},{"height_mm","10"}});
    const auto cut_sketch=f.rectangle(2,2,2,2);
    const auto cut=f.run("extrusion.create",{{"sketch",cut_sketch},{"combine","subtract"},{"end_forward","through_all"}});near(f.volume(),980);
    // The native Box is centred at the Origin; one side cuts only half its depth.
    f.run("extrusion.set",{{"container",cut.at("container")},{"extent","two_sides"},{"end_reverse","through_all"}});near(f.volume(),960);
    f.run("undo");near(f.volume(),980);f.run("redo");near(f.volume(),960);
}
void revolution(const kernel::OcctKernel& kernel,fs::path directory){
    Fixture f(kernel,directory);f.run("new",{{"type","part"},{"name","profile-revolution"}});
    const auto sketch=f.rectangle(2,0,2,10);
    f.reject("revolution.create",{{"sketch",sketch}},"profile_rejected");
    const auto axis=f.line(sketch,0,0,0,10);f.run("sketch.segment.centerline",{{"sketch",sketch},{"segment",axis},{"centerline",true}});
    const auto made=f.run("revolution.create",{{"sketch",sketch},{"axis",axis}});const auto id=made.at("container").get<std::string>();near(f.volume(),120*std::numbers::pi);
    require(made.at("axis")==axis,"Revolution did not use exact native centerline identity");
    f.reject("revolution.set",{{"container",id},{"result_type","thin"}},"unsupported_operation");
    f.run("revolution.set",{{"container",id},{"angle_degrees",90}});near(f.volume(),30*std::numbers::pi);
    f.run("revolution.set",{{"container",id},{"extent","two_sides"},{"angle_degrees",60},{"angle_reverse_degrees",30}});near(f.volume(),30*std::numbers::pi);
    f.run("revolution.set",{{"container",id},{"extent","symmetric"},{"angle_degrees",45}});near(f.volume(),30*std::numbers::pi);
    f.run("revolution.set",{{"container",id},{"direction","reverse"}});near(f.volume(),30*std::numbers::pi);
    f.reject("revolution.set",{{"container",id},{"angle_degrees",361}},"invalid_arguments");
    f.reject("revolution.set",{{"container",id},{"axis","missing"}},"invalid_reference");
    auto locked=f.doc();locked.find_container(id)->value_locks.insert("angle");f.part().session.commit(std::move(locked),f.part().session.calculated_boundaries());
    f.reject("revolution.set",{{"container",id},{"angle_degrees",30}},"value_locked");
    f.run("save");std::vector<kernel::BodyResult> cache;const auto reopened=document::PartDocument::load(directory/"profile-revolution.prtz",&cache);
    require(reopened.find_container(id)->revolution==f.doc().find_container(id)->revolution,"Revolution native persistence lost axis or parameters");near(cache.back().volume,30*std::numbers::pi);
}
}
int main(){try{const auto root=fs::canonical(fs::temp_directory_path());const auto directory=root/("zima-profile-commands-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create fixture directory");kernel::OcctKernel kernel;front_reference();extrusion(kernel,directory);thin_and_cut(kernel,directory);revolution(kernel,directory);
    require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);std::cout<<"Profile commands: native ownership, exact solid volumes, rejected unsupported Thin, cuts, dimensions, locks, atomic errors and Undo/Redo passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
