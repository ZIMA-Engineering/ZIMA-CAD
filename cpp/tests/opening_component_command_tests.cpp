#include <zima/command_host/host.hpp>
#include <zima/workspace/opening_component_operations.hpp>
#include <cmath>
#include <numbers>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(double actual,double expected){if(std::abs(actual-expected)>1e-5)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
void verify(const kernel::OcctKernel& kernel,fs::path directory,bool native) {
    workspace::Workspace live;command_host::Interaction interaction;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    options.interaction=[&]{return interaction;};command_host::Host host(live,kernel,directory,options);
    const auto request=[](const std::string& command,Json args=Json::object()){return Json{{"command",command},{"arguments",std::move(args)}};};
    const auto run=[&](const std::string& command,Json args=Json::object()) {
        auto result=host.execute(request(command,std::move(args)));
        if(!result.ok)throw std::runtime_error(command+": "+result.code+": "+result.message);return result.data;
    };
    const std::string kind=native?"hole":"opening",stem="components-"+kind;
    run("new",{{"type","part"},{"name",stem}});run("box.create",{{"length_mm","60"},{"width_mm","60"},{"height_mm","60"}});
    const auto document_id=live.active_document_id();auto* state=live.open_part(document_id);
    Json args={{"type","metric"},{"bore_length_mm",20},{"thread_length_mm",10},{"drill_point_enabled",true},{"drill_point_angle_degrees",118},{"placement",{{"z",-30}}}};
    if(native){args["diameter_mm"]=8;args["thread_diameter_mm"]=10;args["thread_pitch_mm"]=1.5;args["entrance_chamfer_mm"]=2;}
    else {args["designation"]="M10";args["chamfer_enabled"]=true;args["chamfer_depth_mm"]=2;args["chamfer_angle_degrees"]=90;}
    const auto created=run(kind+".create",args);const std::string id=created.at("container");
    const auto original=*state->session.document().find_container(id);
    const auto volume=[&](){return state->session.calculated_boundaries().back().volume;};
    const double r=(native?original.hole.diameter:original.thread.profile_diameter)/2,tip=r/std::tan(118*std::numbers::pi/360);
    const double cylinder=216000-std::numbers::pi*r*r*20,chamfer=std::numbers::pi*(r*4+8.0/3),tip_volume=std::numbers::pi*r*r*tip/3;
    near(volume(),cylinder-chamfer-tip_volume);
    const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    const auto rows=run(kind+".components",{{"container",id}});
    require(rows.at("total")== (native?10:4)&&state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache,"Component query changed model or omitted roles");
    for(const auto& row:rows.at("items"))require(row.at("removable")== (native?row.at("role")=="thread":row.at("role")!="bore"),"Incorrect component removal capability");
    const auto rejected=[&](const std::string& role,const char* code) {
        const auto before=*state->session.document().find_container(id);const auto rev=state->session.revision();const auto gen=state->session.data_generation();
        const auto result=host.execute(request(kind+".component.remove",{{"container",id},{"role",role}}));
        if(result.ok||result.code!=code)throw std::runtime_error("Expected "+std::string(code)+", got "+result.code);
        require(*state->session.document().find_container(id)==before&&state->session.revision()==rev&&state->session.data_generation()==gen&&!host.change(),"Rejected removal changed model");
    };
    rejected("bore","component_not_removable");rejected("not-a-role","component_not_removable");
    if(native){rejected("bore-sketch","component_not_removable");rejected("chamfer","component_not_removable");}
    interaction.editing=true;rejected("thread","editing_in_progress");interaction={};
    const auto body=state->session.document().body_history.active_body_id();run("body.create",{{"name","Other"}});rejected("thread","inactive_body");run("body.activate",{{"body",body}});
    for(const std::string role:native?std::vector<std::string>{"thread"}:std::vector<std::string>{"thread","chamfer","tip"}) {
        const auto rev=state->session.revision();const auto fingerprint=state->session.calculated_boundaries().back().source_fingerprint;
        const auto removed=run(kind+".component.remove",{{"container",id},{"role",role}});
        if(removed.at("changed")!=true||removed.at("body_calculated")!=!native||state->session.revision()==rev)
            throw std::runtime_error(kind+" "+role+" removal revision "+std::to_string(rev)+" -> "+std::to_string(state->session.revision())+": "+removed.dump());
        const auto committed_revision=state->session.revision();
        const auto edited=*state->session.document().find_container(id);
        require(edited.feature_id==original.feature_id&&edited.hole.sketch_id==original.hole.sketch_id&&edited.hole.circle_id==original.hole.circle_id&&edited.hole.chamfer_sketch_id==original.hole.chamfer_sketch_id&&edited.hole.tip_sketch_id==original.hole.tip_sketch_id,"Removal replaced owned profile identity");
        near(volume(),cylinder-(role=="chamfer"?0:chamfer)-(role=="tip"?0:tip_volume));
        if(native)require(state->session.document().hole_thread_edges(edited).empty()&&state->session.calculated_boundaries().back().source_fingerprint==fingerprint,"Native Hole retained thread wire or recalculated body");
        else if(role=="thread") {
            near(edited.thread.nominal_diameter,original.thread.profile_diameter);
            require(!std::ranges::any_of(state->session.calculated_boundaries().back().mesh.triangle_references,[](const auto& ref){return ref.is_thread_surface();}),"Opening retained its thread sheet");
        }
        const auto* unchanged=state->session.calculated_boundaries().data();const auto gen=state->session.data_generation();
        require(run(kind+".component.remove",{{"container",id},{"role",role}}).at("changed")==false&&state->session.revision()==committed_revision&&state->session.data_generation()==gen&&state->session.calculated_boundaries().data()==unchanged&&!host.change(),"No-op removal changed history or geometry");
        run("undo");require(*state->session.document().find_container(id)==original&&state->session.revision()==rev,"Removal Undo did not restore original state in one step");
        run("redo");require(*state->session.document().find_container(id)==edited&&state->session.revision()==committed_revision,"Removal Redo changed identities");
        run("save");std::vector<kernel::BodyResult> saved_cache;const auto saved=document::PartDocument::load(directory/(stem+".prtz"),&saved_cache);
        require(*saved.find_container(id)==edited,"Native file lost removed component state");near(saved_cache.back().volume,volume());
        run("undo");
    }
}
}
int main(){try{const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-opening-components-"+document::PartDocument::create_default().document_id);require(fs::create_directory(dir),"Cannot create test directory");kernel::OcctKernel kernel;verify(kernel,dir,false);verify(kernel,dir,true);require(dir.parent_path()==root,"Unexpected cleanup path");fs::remove_all(dir);std::cout<<"Opening components, exact volume, source identities, native files, no-op and Undo/Redo passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
