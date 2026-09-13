#include <zima/command_host/host.hpp>
#include <zima/workspace/edge_treatment_operations.hpp>
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
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double value,double expected,double tolerance=1e-5){if(std::abs(value-expected)>tolerance)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(value));}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings settings;settings.templates={fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return settings;};
    command_host::Host host(live,kernel,directory,options);
    require(host.execute_text("fillet.get missing").code=="unsupported_document","Fillet query accepted no document");
    run(host,"new",{{"type","part"},{"name","edge-treatment"}});
    auto* state=live.open_part(live.active_document_id());const auto doc_id=live.active_document_id();
    const auto body=state->session.document().body_history.active_body_id();
    require(host.execute({{"command","fillet.create"},{"arguments",{{"routes",Json::array({Json{{"edges",Json::array({Json{{"owner","missing"},{"key","edge"}}})}}})}}}}).code=="missing_input","Fillet accepted an empty input body");
    const auto box=run(host,"box.create",{{"length_mm","100"},{"width_mm","80"},{"height_mm","50"}}).data.at("container").get<std::string>();
    const std::string edge_key="edge:x_max:y_min:z_max--x_max:y_min:z_min";
    const Json edge={{"owner",box},{"key",edge_key}};
    const auto route=run(host,"edge_treatment.route",{{"seed",edge}}).data;
    require(route.at("endpoints").size()==2,"Missing real route endpoints");
    auto start=route.at("endpoints")[0],end=route.at("endpoints")[1];
    const double start_z=start.at("position_mm")[2],end_z=end.at("position_mm")[2];start.erase("position_mm");end.erase("position_mm");
    const auto routes=[&](Json reference=Json(nullptr)){return Json::array({Json{{"edges",Json::array({edge})},{"start",reference}}});};
    const auto id=run(host,"fillet.create",{{"routes",routes()},{"radius_mm",2}}).data.at("container").get<std::string>();
    const double corner=1-std::numbers::pi/4;
    near(state->session.calculated_boundaries().back().volume,400000-corner*4*50);
    auto original=*state->session.document().find_container(id);run(host,"undo");require(!state->session.document().find_container(id),"Fillet create needs multiple Undo steps");run(host,"redo");
    require(*state->session.document().find_container(id)==original,"Fillet Redo lost identity");
    const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    run(host,"fillet.get",{{"container",id}});require(!run(host,"fillet.set",{{"container",id},{"radius_mm",2}}).data.at("changed").get<bool>(),"Fillet equal patch committed");
    require(state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache,"Fillet read/no-op recalculated");
    for(const auto& patch:std::vector<Json>{{{"radius_mm",0}},{{"radius_mm",1000001}},{{"radius_mm",500}},{{"mode","nonsense"}},{{"mode","linear"}},
        {{"routes",Json::array()}},{{"routes",Json::array({Json{{"edges",Json::array({edge,edge})}}})}},
        {{"routes",Json::array({Json{{"edges",Json::array({Json{{"owner",box},{"key","missing"}}})}}})}},
        {{"routes",Json::array({Json{{"edges",Json::array({edge})},{"start",Json{{"owner",box},{"key","missing"}}}}})},{"mode","linear"}},
        {{"routes",Json::array({Json{{"edges",Json::array({Json{{"owner",box},{"key",edge_key},{"instance_path","foreign"}}})}}})}},
        {{"routes",Json::array({Json{{"edges",Json::array({edge})},{"geometry_index",0}}})}}}) {
        auto args=patch;args["container"]=id;require(!host.execute({{"command","fillet.set"},{"arguments",args}}).ok,"Invalid Fillet accepted");
        require(state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache&&*state->session.document().find_container(id)==original,"Invalid Fillet partly committed");
    }
    const Json opposite={{"owner",box},{"key","edge:x_min:y_max:z_max--x_min:y_max:z_min"}};
    const auto disconnected=Json::array({Json{{"edges",Json::array({edge,opposite})},{"start",end}}});
    require(host.execute({{"command","fillet.set"},{"arguments",{{"container",id},{"mode","linear"},{"routes",disconnected}}}}).code=="invalid_reference","Linear Fillet accepted disconnected routes under one R1");
    require(host.execute({{"command","chamfer.get"},{"arguments",{{"container",id}}}}).code=="wrong_feature","Chamfer query accepted a Fillet");
    run(host,"fillet.set",{{"container",id},{"mode","linear"},{"radius_end_mm",5},{"routes",routes(end)}});
    const auto radius_at=[&](double z) {
        double maximum=-1;
        for(const auto& value:state->session.calculated_boundaries().back().mesh.edges)
            if(value.reference.owner_id==id&&!value.points.empty()&&std::ranges::all_of(value.points,[&](const auto& p){return std::abs(p.z-z)<1e-6;}))
                for(const auto& p:value.points)maximum=std::max(maximum,std::hypot(p.x-50,p.y+40));
        return maximum;
    };
    near(radius_at(end_z),2,.1);near(radius_at(start_z),5,.1);
    // A variable rolling-ball fillet is not a stack of planar quarter-circles
    // with radius interpolated in world Z. Bound it by the two constant-radius
    // solids and verify mirrored R1/R2 produce equal volume on this symmetric box.
    const auto linear_volume=state->session.calculated_boundaries().back().volume;
    require(linear_volume>400000-corner*25*50&&linear_volume<400000-corner*4*50,"Variable Fillet volume is outside constant-radius bounds");
    run(host,"fillet.set",{{"container",id},{"reverse",true}});near(radius_at(end_z),5,.1);near(radius_at(start_z),2,.1);
    near(state->session.calculated_boundaries().back().volume,linear_volume,1e-4);
    run(host,"undo");near(radius_at(end_z),2,.1);run(host,"redo");near(radius_at(end_z),5,.1);
    auto locked=*state->session.document().find_container(id);locked.value_locks={"primary","secondary"};
    static_cast<void>(workspace::commit_edge_treatment(live,kernel,doc_id,locked,workspace::EdgeTreatmentEditMode::Replace));
    for(const auto* field:{"radius_mm","radius_end_mm"})require(host.execute({{"command","fillet.set"},{"arguments",{{"container",id},{field,3}}}}).code=="value_locked","Fillet bypassed dimension lock");
    run(host,"save");std::vector<kernel::BodyResult> saved;const auto loaded=document::PartDocument::load(directory/"edge-treatment.prtz",&saved);
    require(*loaded.find_container(id)==locked,"Native Fillet lost its routes/R1/locks");
    near(kernel.evaluate_history(loaded.kernel_operations()).back().volume,saved.back().volume,1e-5);
    run(host,"box.set",{{"container",box},{"length_mm","110"}});require(state->session.document().find_container(id)->edge_treatment.route_start_vertices==locked.edge_treatment.route_start_vertices,"Source change replaced R1 ancestry");
    const auto other=run(host,"body.create",{{"name","Other"}}).data.at("body");run(host,"box.create",{{"length_mm","10"},{"width_mm","10"},{"height_mm","10"}});
    require(host.execute({{"command","fillet.set"},{"arguments",{{"container",id},{"reverse",false}}}}).code=="inactive_body","Fillet edited inactive Body");
    require(host.execute({{"command","fillet.create"},{"arguments",{{"routes",routes()}}}}).code=="invalid_reference","Fillet accepted another Body edge");
    run(host,"body.activate",{{"body",body}});run(host,"body.cursor",{{"body",body},{"index",1}});
    near(workspace::calculated_operation_input(state->session)->volume,440000);
    require(run(host,"edge_treatment.edges",{{"container",id}}).data.at("total")==12,"Fillet edit lost consumed input edge");
    // Three independent Chamfer modes; FLIP swaps the two support faces.
    run(host,"new",{{"type","part"},{"name","chamfer-command"}});
    const auto block=run(host,"box.create",{{"length_mm","100"},{"width_mm","80"},{"height_mm","50"}}).data.at("container");state=live.open_part(live.active_document_id());
    const auto chamfer_routes=Json::array({Json{{"edges",Json::array({Json{{"owner",block},{"key",edge_key}}})}}});
    const auto chamfer=run(host,"chamfer.create",{{"routes",chamfer_routes},{"distance_a_mm",2}}).data.at("container").get<std::string>();
    near(state->session.calculated_boundaries().back().volume,399900);
    run(host,"chamfer.set",{{"container",chamfer},{"mode","two_distances"},{"distance_b_mm",5}});near(state->session.calculated_boundaries().back().volume,399750);
    const auto side_distances=[&]() {
        std::pair<double,double> result{};
        for(const auto& e:state->session.calculated_boundaries().back().mesh.edges)if(e.reference.owner_id==chamfer)
            for(const auto& p:e.points)if(std::abs(p.z-25)<1e-6){result.first=std::max(result.first,50-p.x);result.second=std::max(result.second,p.y+40);}
        return result;
    };
    const auto first=side_distances();near(std::min(first.first,first.second),2);near(std::max(first.first,first.second),5);
    run(host,"chamfer.set",{{"container",chamfer},{"flip",true}});const auto flipped=side_distances();near(flipped.first,first.second);near(flipped.second,first.first);
    run(host,"chamfer.set",{{"container",chamfer},{"mode","distance_angle"},{"distance_a_mm",3},{"angle_degrees",30}});
    near(state->session.calculated_boundaries().back().volume,400000-.5*9*std::tan(std::numbers::pi/6)*50);
    auto chamfer_locked=*state->session.document().find_container(chamfer);chamfer_locked.value_locks={"primary","secondary","treatment_angle"};
    static_cast<void>(workspace::commit_edge_treatment(live,kernel,live.active_document_id(),chamfer_locked,workspace::EdgeTreatmentEditMode::Replace));
    const auto unchanged=state->session.revision();
    for(const auto* field:{"distance_a_mm","distance_b_mm","angle_degrees"})require(host.execute({{"command","chamfer.set"},{"arguments",{{"container",chamfer},{field,4}}}}).code=="value_locked","Chamfer bypassed dimension lock");
    require(run(host,"chamfer.get",{{"container",chamfer}}).data.at("angle_degrees")==30&&state->session.revision()==unchanged,"Chamfer query or rejected edit mutated state");
    run(host,"save");const auto chamfer_saved=document::PartDocument::load(directory/"chamfer-command.prtz");require(*chamfer_saved.find_container(chamfer)==chamfer_locked,"Native Chamfer lost state");
    // Four independent contours use alternating explicit R1 ends. The runtime
    // topology may contain repeated shape uses for one persisted input edge.
    run(host,"new",{{"type","part"},{"name","four-linear-routes"}});
    run(host,"box.create",{{"length_mm","100"},{"width_mm","80"},{"height_mm","50"}});state=live.open_part(live.active_document_id());
    auto four_routes=Json::array();std::vector<std::pair<kernel::Vec3,kernel::Vec3>> corners;
    const auto many_input=run(host,"edge_treatment.edges").data;
    for(const auto& item:many_input.at("items")) {
        const auto& values=item.at("segments")[0].at("endpoints");if(values.size()!=2)continue;
        const auto& a=values[0].at("position_mm");const auto& b=values[1].at("position_mm");
        if(a[0]!=b[0]||a[1]!=b[1]||a[2]==b[2])continue;
        const auto first=four_routes.size()%2;auto anchor=values[first];anchor.erase("position_mm");
        const auto& p=values[first].at("position_mm");const auto& q=values[1-first].at("position_mm");
        corners.push_back({{p[0],p[1],p[2]},{q[0],q[1],q[2]}});
        four_routes.push_back({{"edges",Json::array({Json{{"owner",item.at("owner")},{"key",item.at("key")}}})},{"start",anchor}});
    }
    require(four_routes.size()==4,"Missing independent vertical input contours");
    const auto many=run(host,"fillet.create",{{"routes",four_routes},{"mode","linear"},{"radius_mm",2},{"radius_end_mm",5}}).data.at("container").get<std::string>();
    const auto check_radii=[&](const kernel::BodyResult& result,bool reversed) {
        for(const auto& ends:corners)for(const bool first:{true,false}) {
            const auto& corner=first?ends.first:ends.second;double radius=-1;
            for(const auto& e:result.mesh.edges)if(e.reference.owner_id==many&&!e.points.empty()&&std::ranges::all_of(e.points,[&](const auto& p){
                return std::abs(p.z-corner.z)<1e-6&&std::hypot(p.x-corner.x,p.y-corner.y)<10;}))
                for(const auto& p:e.points)radius=std::max(radius,std::hypot(p.x-corner.x,p.y-corner.y));
            near(radius,first!=reversed?2:5,.1);
        }
    };
    const auto face_ids=[](const kernel::BodyResult& result) {
        std::set<std::pair<std::string,std::string>> ids;
        for(const auto& face:result.mesh.triangle_references)ids.emplace(face.owner_id,face.semantic_key);return ids;
    };
    const auto many_volume=state->session.calculated_boundaries().back().volume;check_radii(state->session.calculated_boundaries().back(),false);
    require(many_volume>400000-4*corner*25*50&&many_volume<400000-4*corner*4*50,"Four-contour Fillet has an invalid volume");
    run(host,"fillet.set",{{"container",many},{"reverse",true}});check_radii(state->session.calculated_boundaries().back(),true);near(state->session.calculated_boundaries().back().volume,many_volume,.001);
    const auto ids=face_ids(state->session.calculated_boundaries().back());std::reverse(four_routes.begin(),four_routes.end());
    run(host,"fillet.set",{{"container",many},{"routes",four_routes}});check_radii(state->session.calculated_boundaries().back(),true);
    require(face_ids(state->session.calculated_boundaries().back())==ids,"Reordering Fillet routes replaced generated face ancestry");
    run(host,"save");const auto four_saved=document::PartDocument::load(directory/"four-linear-routes.prtz");kernel::OcctKernel cold;
    const auto four_cold=cold.evaluate_history(four_saved.kernel_operations());check_radii(four_cold.back(),true);
    near(four_cold.back().volume,many_volume,.001);require(face_ids(four_cold.back())==ids,"Cold calculation lost Fillet ancestry");
    // A closed circular edge needs no invented R1 endpoint for constant radius.
    run(host,"new",{{"type","part"},{"name","circular-fillet"}});run(host,"cylinder.create",{{"radius_mm","10"},{"height_mm","20"}});state=live.open_part(live.active_document_id());
    const auto& input=state->session.calculated_boundaries().back();
    const auto ring=std::ranges::find_if(input.mesh.edges,[](const auto& e){return e.reference.valid()&&e.points.size()>2&&std::ranges::all_of(e.points,[](const auto& p){return std::abs(p.z-20)<1e-6;});});
    require(ring!=input.mesh.edges.end(),"Cylinder top ring is missing");
    const auto ring_routes=Json::array({Json{{"edges",Json::array({Json{{"owner",ring->reference.owner_id},{"key",ring->reference.semantic_key}}})}}});
    run(host,"fillet.create",{{"routes",ring_routes}});
    near(state->session.calculated_boundaries().back().volume,2000*std::numbers::pi-2*std::numbers::pi*(55./6-9*std::numbers::pi/4),1e-4);
}
}
int main(){try{kernel::OcctKernel kernel;const auto directory=fs::temp_directory_path()/("zima-edge-commands-"+document::PartDocument::create_box_container().id);
    require(fs::create_directory(directory),"Cannot create test directory");verify(kernel,directory);fs::remove_all(directory);
    std::cout<<"Edge treatment commands: radii, section distances, volumes, reference ownership, locks, rollback and native files passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
