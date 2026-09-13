#include <zima/command_host/host.hpp>
#include <zima/workspace/shell_operations.hpp>
#include <zima/workspace/operation_input.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <set>
using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(double actual,double expected){if(std::abs(actual-expected)>1e-5)throw std::runtime_error("Expected volume "+std::to_string(expected)+", got "+std::to_string(actual));}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings settings;settings.templates={fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return settings;};
    command_host::Host host(live,kernel,directory,options);
    require(host.execute_text("shell.get missing").code=="unsupported_document"&&host.execute_text("shell.faces").code=="unsupported_document","Shell queries without Part failed incorrectly");
    run(host,"new",{{"type","part"},{"name","shell-command"}});
    auto* state=live.open_part(live.active_document_id());const auto document_id=live.active_document_id();
    const auto body=state->session.document().body_history.active_body_id();
    require(host.execute_text("shell.create").code=="missing_input","Shell accepted an empty input");
    const auto box=run(host,"box.create",{{"length_mm","10"},{"width_mm","10"},{"height_mm","10"}}).data.at("container").get<std::string>();
    const auto face=[&](const char* key,const std::string& owner=std::string{}){return Json{{"owner",owner.empty()?box:owner},{"key",key}};};
    const auto top=face("z_max"),side=face("x_max"),bottom=face("z_min");
    const auto rev=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    const auto list=run(host,"shell.faces").data;require(list.at("total")==6&&list.at("items").size()==6,"Shell did not list six input faces");
    require(run(host,"shell.faces",{{"limit",2},{"offset",2}}).data.at("items").size()==2&&run(host,"shell.faces",{{"offset",6}}).data.at("items").empty(),"Shell face pagination failed");
    require(run(host,"shell.faces",{{"owner","missing"}}).data.at("total")==0&&state->session.revision()==rev&&state->session.calculated_boundaries().data()==cache,"Shell face query recalculated or ignored owner filter");
    for(const auto args:std::vector<Json>{{{"limit",0}},{{"limit",5001}},{{"offset",-1}},{{"container",box}}})
        require(!host.execute({{"command","shell.faces"},{"arguments",args}}).ok,"Invalid Shell face query accepted");
    const auto id=run(host,"shell.create",{{"faces",Json::array({top})}}).data.at("container").get<std::string>();
    const auto original=*state->session.document().find_container(id);near(state->session.calculated_boundaries().back().volume,424);
    run(host,"undo");require(!state->session.document().find_container(id),"Shell create did not Undo in one step");near(state->session.calculated_boundaries().back().volume,1000);
    run(host,"redo");require(*state->session.document().find_container(id)==original,"Shell Redo changed identities");
    const auto unchanged=state->session.revision();const auto* saved_cache=state->session.calculated_boundaries().data();
    run(host,"shell.get",{{"container",id}});require(!run(host,"shell.set",{{"container",id},{"thickness_mm",1}}).data.at("changed").get<bool>(),"Shell equal properties committed");
    const auto edit_faces=run(host,"shell.faces",{{"container",id}}).data;
    require(edit_faces.at("total")==6&&std::ranges::any_of(edit_faces.at("items"),[&](const auto& item){return item.at("owner")==box&&item.at("key")=="z_max";}),"Shell edit listed its final result instead of the removed input face");
    require(state->session.revision()==unchanged&&saved_cache==state->session.calculated_boundaries().data(),"Shell query/no-op recalculated");
    for(const auto& patch:std::vector<Json>{{{"thickness_mm",0}},{{"thickness_mm",-.1}},{{"thickness_mm",1000001}},{{"thickness_mm",20}},
        {{"faces",Json::array({top,top})}},{{"faces",Json::array({face("missing")})}},{{"faces",Json::array({face("z_max",id)})}},
        {{"faces",Json::array({Json{{"owner",box},{"key","z_max"},{"instance_path","other"}}})}},{{"faces",Json::array({Json{{"owner",box},{"key","z_max"},{"surface",Json::object()}}})}}}) {
        auto args=patch;args["container"]=id;require(!host.execute({{"command","shell.set"},{"arguments",args}}).ok,"Invalid Shell edit accepted");
        require(state->session.revision()==unchanged&&saved_cache==state->session.calculated_boundaries().data()&&*state->session.document().find_container(id)==original,"Invalid Shell edit partly committed");
    }
    run(host,"shell.set",{{"container",id},{"thickness_mm",2}});near(state->session.calculated_boundaries().back().volume,712);
    run(host,"undo");near(state->session.calculated_boundaries().back().volume,424);run(host,"redo");near(state->session.calculated_boundaries().back().volume,712);
    run(host,"shell.set",{{"container",id},{"thickness_mm",1},{"faces",Json::array({top,side})}});near(state->session.calculated_boundaries().back().volume,352);
    run(host,"shell.set",{{"container",id},{"faces",Json::array({top,bottom})}});near(state->session.calculated_boundaries().back().volume,360);
    run(host,"shell.set",{{"container",id},{"faces",Json::array()}});near(state->session.calculated_boundaries().back().volume,488);
    // A changed source is used by the existing Shell after explicit calculation.
    run(host,"box.set",{{"container",box},{"length_mm","12"}});near(state->session.calculated_boundaries().back().volume,560);
    auto locked=*state->session.document().find_container(id);locked.value_locks.insert("thickness");
    static_cast<void>(workspace::commit_shell(live,kernel,document_id,locked,workspace::ShellEditMode::Replace));
    require(host.execute({{"command","shell.set"},{"arguments",{{"container",id},{"thickness_mm",2}}}}).code=="value_locked","Shell bypassed its Properties lock");
    run(host,"save");std::vector<kernel::BodyResult> saved;const auto loaded=document::PartDocument::load(directory/"shell-command.prtz",&saved);
    require(*loaded.find_container(id)==locked,"Shell native save lost properties/references");near(saved.back().volume,560);near(kernel.evaluate_history(loaded.kernel_operations()).back().volume,560);
    // Select only the owning Body, including when another Body has an identical shape.
    const auto other=run(host,"body.create",{{"name","Other"}}).data.at("body").get<std::string>();
    const auto other_box=run(host,"box.create",{{"length_mm","20"},{"width_mm","20"},{"height_mm","20"}}).data.at("container").get<std::string>();
    const auto other_faces=run(host,"shell.faces").data;
    require(other_faces.at("total")==6&&std::ranges::all_of(other_faces.at("items"),[&](const auto& value){return value.at("owner")==other_box;}),"Shell mixed different Body inputs");
    require(host.execute({{"command","shell.set"},{"arguments",{{"container",id},{"faces",Json::array({top})}}}}).code=="inactive_body","Shell edited an inactive Body");
    require(host.execute({{"command","shell.create"},{"arguments",{{"faces",Json::array({top})}}}}).code=="invalid_reference","Shell accepted a different Body's face");
    run(host,"body.activate",{{"body",body}});
    // Insertion before the existing Shell must expose the box, not its final cavity.
    run(host,"body.cursor",{{"body",body},{"index",1}});
    require(run(host,"shell.faces").data.at("total")==6,"Shell insertion ignored the history cursor");
    const auto* input=workspace::calculated_operation_input(state->session);
    require(input&&std::abs(input->volume-1200)<1e-6,"Borrowed input is not the real insertion boundary");
    const auto pointer=input;
    require(workspace::calculated_operation_input(state->session)==pointer,"Reading operation input copied the body");
    // Current non-planar source: cylinder with one open end.
    run(host,"new",{{"type","part"},{"name","shell-cylinder"}});
    const auto cylinder=run(host,"cylinder.create",{{"radius_mm","10"},{"height_mm","20"}}).data.at("container").get<std::string>();
    run(host,"shell.create",{{"faces",Json::array({face("z_max",cylinder)})}});
    state=live.open_part(live.active_document_id());near(state->session.calculated_boundaries().back().volume,461*std::numbers::pi);
    // Closed shell: the kernel must preserve both boundaries of a spherical wall.
    run(host,"new",{{"type","part"},{"name","shell-sphere"}});run(host,"sphere.create",{{"radius_mm","10"}});run(host,"shell.create");
    state=live.open_part(live.active_document_id());near(state->session.calculated_boundaries().back().volume,4*std::numbers::pi*271/3);
    const auto sphere_shell=state->session.document().history.back().id;
    const auto sphere_faces=[&](const kernel::BodyResult& result) {
        std::set<std::pair<std::string,std::string>> ids;
        for(const auto& ref:result.mesh.triangle_references)ids.emplace(ref.owner_id,ref.semantic_key);
        require(ids.size()==2,"Spherical wall lost its distinct inner/outer faces");
        require(std::ranges::none_of(result.mesh.edges,[&](const auto& edge){return edge.reference.owner_id==sphere_shell;})&&
            std::ranges::none_of(result.mesh.points,[&](const auto& point){return point.reference.owner_id==sphere_shell;}),"Spherical Shell invented reference entities for parameter seams/poles");
        for(const auto& point:result.mesh.vertices) {
            const auto radius=std::hypot(point.x,point.y,point.z);
            require(std::min(std::abs(radius-9),std::abs(radius-10))<1e-5,"Spherical Shell has a wrong inner/outer radius");
        }
        return ids;
    };
    const auto sphere_ids=sphere_faces(state->session.calculated_boundaries().back());run(host,"save");
    auto sphere_loaded=document::PartDocument::load(directory/"shell-sphere.prtz");const auto sphere_cold=kernel.evaluate_history(sphere_loaded.kernel_operations());
    near(sphere_cold.back().volume,4*std::numbers::pi*271/3);require(sphere_faces(sphere_cold.back())==sphere_ids,"Spherical Shell changed face identities after native reload");
    // A treatment owns new real faces, which the Shell input query must retain.
    run(host,"new",{{"type","part"},{"name","shell-filleted"}});state=live.open_part(live.active_document_id());
    auto treated=state->session.document();auto block=document::PartDocument::create_box_container();block.box={100,80,50};
    auto fillet=document::PartDocument::create_fillet_container({
        {block.id,"edge:x_max:y_min:z_max--x_max:y_min:z_min",{}},
        {block.id,"edge:x_min:y_min:z_max--x_min:y_min:z_min",{}}});fillet.edge_treatment.primary_size=5;
    for(const auto& feature:{block,fillet}){treated.insert_history_entry(document::PartHistoryKind::Feature,feature.id);treated.history.push_back(feature);}
    auto treated_results=kernel.evaluate_history(treated.kernel_operations());state->session.commit(std::move(treated),std::move(treated_results));
    const auto treated_faces=run(host,"shell.faces",{{"owner",fillet.id}}).data;
    require(treated_faces.at("total")==2,"Shell did not offer the real Fillet faces of its input body");
    run(host,"shell.create",{{"thickness_mm",2},{"faces",Json::array({face("z_max",block.id)})}});
    near(state->session.calculated_boundaries().back().volume,(7950+12.5*std::numbers::pi)*50-(7278+4.5*std::numbers::pi)*48);
    // Trimming a sphere introduces a real circular rim. It must survive the
    // seam/pole policy, including its new inward-offset circle at radius 9.
    run(host,"new",{{"type","part"},{"name","shell-hemisphere"}});state=live.open_part(live.active_document_id());
    auto hemisphere=state->session.document();auto ball=document::PartDocument::create_sphere_container();ball.sphere.radius=10;
    auto cutter=document::PartDocument::create_box_container();cutter.box={40,40,20};cutter.placement.z=10;cutter.combine_mode=document::CombineMode::Subtract;
    for(const auto& feature:{ball,cutter}){hemisphere.insert_history_entry(document::PartHistoryKind::Feature,feature.id);hemisphere.history.push_back(feature);}
    auto hemisphere_results=kernel.evaluate_history(hemisphere.kernel_operations());near(hemisphere_results.back().volume,2000*std::numbers::pi/3);
    state->session.commit(std::move(hemisphere),std::move(hemisphere_results));
    run(host,"shell.create",{{"faces",Json::array({face("z_min",cutter.id)})}});
    const auto& hemisphere_result=state->session.calculated_boundaries().back();near(hemisphere_result.volume,542*std::numbers::pi/3);
    for(const double radius:{9.,10.})require(std::ranges::any_of(hemisphere_result.mesh.edges,[&](const auto& edge){
        return edge.reference.valid()&&edge.points.size()>2&&std::ranges::all_of(edge.points,[&](const auto& point){
            return std::abs(point.z)<1e-5&&std::abs(std::hypot(point.x,point.y)-radius)<1e-5;});
    }),"Shell discarded a real circular rim of a trimmed spherical face");
    // Two disconnected solids are not a valid single Shell input.
    run(host,"new",{{"type","part"},{"name","shell-disconnected"}});state=live.open_part(live.active_document_id());
    auto split=state->session.document();auto first=document::PartDocument::create_box_container();first.box={10,10,10};
    auto second=document::PartDocument::create_box_container();second.box={10,10,10};second.placement.x=30;
    for(const auto& feature:{first,second}){split.insert_history_entry(document::PartHistoryKind::Feature,feature.id);split.history.push_back(feature);}
    auto split_results=kernel.evaluate_history(split.kernel_operations());state->session.commit(std::move(split),std::move(split_results));
    const auto split_revision=state->session.revision();const auto* split_cache=state->session.calculated_boundaries().data();
    require(!host.execute_text("shell.create").ok&&state->session.revision()==split_revision&&split_cache==state->session.calculated_boundaries().data(),"Disconnected Shell partly committed");
}
}
int main(){try {
    kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());const auto directory=root/("zima-shell-commands-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create test directory");verify(kernel,directory);
    require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);
    std::cout<<"Shell commands: independent wall volumes, real input faces, multiple bodies, history, locks, Undo and native round trip passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
