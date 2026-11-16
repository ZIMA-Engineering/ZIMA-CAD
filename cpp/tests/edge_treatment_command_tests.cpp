#include "profile_solid_fixture.hpp"
#include "profile_command_fixture.hpp"
#include "../app/workspace/edge_treatment_preview.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/edge_treatment_operations.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/document/edge_treatment_selection.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <BRepClass3d_SolidClassifier.hxx>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <limits>
#include <fstream>
#include <set>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
// Explicit calculation oracle for regression checks. Product interaction has
// no disposable body-preview API: it uses only the persisted input wire data.
kernel::ViewerMesh calculate_candidate_body(const document::DocumentSession& session,
        const kernel::OcctKernel& engine,const document::HistoryContainer& feature,
        kernel::BodyResult* body=nullptr,const fs::path& save_path={}) {
    workspace::Workspace trial;
    trial.add_part(session.document(),session.calculated_boundaries());
    const auto& id=session.document().document_id;
    static_cast<void>(workspace::commit_edge_treatment(trial,engine,id,feature,
        session.document().find_container(feature.id)?workspace::EdgeTreatmentEditMode::Replace:
            workspace::EdgeTreatmentEditMode::Create));
    auto& trial_session=trial.open_part(id)->session;
    const auto* output=workspace::calculated_operation_output(trial_session,feature.id);
    require(output,"Calculated treatment has no output");
    if(body&&output->kernel_shape.empty()) {
        // An unchanged candidate deliberately performs no calculation. A
        // compact saved output then needs a separate explicit oracle build.
        workspace::PartCalculationPolicy policy;
        policy.publish_complete_history=true;
        policy.history_reference_baseline=&trial_session.calculated_boundaries();
        auto calculated=workspace::calculate_part(engine,
            trial_session.document(),nullptr,policy);
        trial_session.update_calculated_cache_only(std::move(calculated));
        output=workspace::calculated_operation_output(trial_session,feature.id);
        require(output,"Rebuilt treatment has no output");
    }
    if(body)*body=*output;
    if(!save_path.empty()) {
        const auto& saved=trial.open_part(id)->session;
        saved.document().save(save_path,saved.calculated_boundaries());
    }
    return output->mesh;
}
struct EndCutChoice {std::vector<kernel::FaceReference> faces;};
std::vector<EndCutChoice> calculate_end_choices(const document::DocumentSession& session,
        const kernel::OcctKernel& engine,const document::HistoryContainer& feature,
        const std::vector<std::pair<std::size_t,bool>>& ends) {
    const auto* input=workspace::calculated_operation_input(session,
        session.document().find_container(feature.id)?feature.id:std::string{});
    require(input,"Terminal oracle has no input");
    std::vector<std::vector<kernel::ViewerEdge>> groups;
    for(std::size_t i=0;i<feature.edge_treatment.routes.size();++i)
        groups.push_back(document::current_treatment_selection_wire(feature.edge_treatment,i,{},input->mesh));
    std::vector<EndCutChoice> choices{{}};
    for(const auto [route,first]:ends) {
        std::vector<EndCutChoice> next;
        for(const auto& face:app::workspace_detail::edge_treatment_preview_end_choices(
                groups,feature.edge_treatment,&input->mesh,route,first))
            for(auto choice:choices){choice.faces.push_back(face);next.push_back(std::move(choice));}
        choices=std::move(next);
    }
    std::vector<EndCutChoice> accepted;
    std::vector<kernel::ViewerMesh> geometry;
    for(const auto& choice:choices)try {
        auto candidate=feature;
        for(std::size_t i=0;i<ends.size();++i) {
            const auto [route,first]=ends[i];auto& end=candidate.edge_treatment.route_end_faces[route];
            (first?end.first_mode:end.last_mode)=document::EdgeTreatmentParameters::RouteEndMode::Cut;
            (first?end.first:end.last)=choice.faces[i];
        }
        auto mesh=calculate_candidate_body(session,engine,candidate);
        if(std::ranges::none_of(geometry,[&](const auto& other){return mesh.vertices==other.vertices&&mesh.triangles==other.triangles;})) {
            accepted.push_back(choice);geometry.push_back(std::move(mesh));
        }
    }catch(const std::exception&){}
    return accepted;
}
void near(double value,double expected,double tolerance=1e-5){if(std::abs(value-expected)>tolerance)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(value));}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",args}});
    if(!result.ok)throw std::runtime_error(std::string(name)+" "+args.dump()+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings settings;settings.templates={fs::absolute("config/templates"),"START_PART_mm.prtz","START_ASSEMBLY_mm.asmz","Body"};return settings;};
    command_host::Host host(live,kernel,directory,options);
    require(host.execute_text("fillet.get missing").code=="unsupported_document","Fillet query accepted no document");
    run(host,"new",{{"type","part"},{"name","edge-treatment"}});
    auto* state=live.open_part(live.active_document_id());const auto doc_id=live.active_document_id();
    const auto measured_volume=[&]{auto body=state->session.calculated_boundaries().back();kernel.ensure_body_properties(body);return body.volume;};
    const auto body=state->session.document().body_history.active_body_id();
    require(host.execute({{"command","fillet.create"},{"arguments",{{"routes",Json::array({Json{{"edges",Json::array({Json{{"owner","missing"},{"key","edge"}}})}}})}}}}).code=="missing_input","Fillet accepted an empty input body");
    const auto box=zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","100"},{"width_mm","80"},{"one_sided",true},{"height_mm","50"}}).data.at("container").get<std::string>();
    const std::string edge_key=test::profile_key(state->session.document(),box,"edge:x_max:y_min:z_max--x_max:y_min:z_min");
    const Json edge={{"owner",box},{"key",edge_key}};
    const auto route=run(host,"edge_treatment.route",{{"seed",edge}}).data;
    require(route.at("endpoints").size()==2,"Missing real route endpoints");
    auto start=route.at("endpoints")[0],end=route.at("endpoints")[1];
    const double start_z=start.at("position_mm")[2],end_z=end.at("position_mm")[2];start.erase("position_mm");end.erase("position_mm");
    const auto routes=[&](Json reference=Json(nullptr)){return Json::array({Json{{"edges",Json::array({edge})},{"start",reference}}});};
    const auto id=run(host,"fillet.create",{{"routes",routes()},{"radius_mm",2}}).data.at("container").get<std::string>();
    require(run(host,"fillet.get",{{"container",id}}).data.at(
                "calculation_method")=="zima",
        "Fillet command exposed the removed Automatic method name");
    const auto default_route=run(host,"fillet.get",{{"container",id}}).
        data.at("routes").at(0);
    require(default_route.at("first_mode")=="no_cut"&&
            default_route.at("last_mode")=="no_cut"&&
            default_route.at("first_vertex").is_null()&&
            default_route.at("last_vertex").is_null(),
        "A new command Fillet did not default both endpoints to No-cut");
    const auto cut_routes=Json::array({Json{
        {"edges",Json::array({edge})},{"start",Json(nullptr)},
        {"first_vertex",start},{"last_vertex",end},
        {"first_mode","cut"},{"last_mode","no_cut"}}});
    run(host,"fillet.set",{{"container",id},{"routes",cut_routes}});
    const auto cut_route=run(host,"fillet.get",{{"container",id}}).
        data.at("routes").at(0);
    require(cut_route.at("first_vertex")==start&&
            cut_route.at("last_vertex")==end&&
            cut_route.at("first_mode")=="cut"&&
            cut_route.at("last_mode")=="no_cut",
        "Fillet command did not retain independent endpoint modes");
    run(host,"undo");
    require(run(host,"fillet.get",{{"container",id}}).data.at("routes").
            at(0)==default_route,
        "Endpoint-mode Undo did not restore the complete route definition");
    run(host,"redo");
    require(run(host,"fillet.get",{{"container",id}}).data.at("routes").
            at(0)==cut_route,
        "Endpoint-mode Redo lost its stable vertex choice");
    run(host,"fillet.set",{{"container",id},{"routes",routes()}});
    const double corner=1-std::numbers::pi/4;
    near(measured_volume(),400000-corner*4*50);
    auto original=*state->session.document().find_container(id);
    const auto* choice_input=workspace::calculated_operation_input(
        state->session,id);
    require(choice_input,
        "Endpoint-cut choice test has no exact operation input");
    const auto choice_endpoints=workspace::edge_treatment_route_endpoints(
        choice_input->mesh.edges,original.edge_treatment.routes.front());
    require(choice_endpoints.size()==2,
        "Endpoint-cut choice test did not resolve two route endpoints");
    auto choice_definition=original;
    auto& choice_end=choice_definition.edge_treatment.route_end_faces.front();
    choice_end.first_vertex=choice_endpoints.front();
    choice_end.first_mode=
        document::EdgeTreatmentParameters::RouteEndMode::Cut;
    const auto choice_revision=state->session.revision();
    const auto choice_generation=state->session.data_generation();
    const auto* choice_cache=state->session.calculated_boundaries().data();
    const auto choice_history=state->session.document().history;
    const auto cut_choices=calculate_end_choices(
        state->session,kernel,choice_definition,{{0,true}});
    require(cut_choices.size()==1&&cut_choices.front().faces.size()==1,
        "Equivalent local endpoint cuts were not reduced to one exact choice");
    auto replay_choice=choice_definition;
    replay_choice.edge_treatment.route_end_faces.front().first=
        cut_choices.front().faces.front();
    const auto choice_preview=calculate_candidate_body(
        state->session,kernel,replay_choice);
    require(!choice_preview.edges.empty(),
        "An offered endpoint cut could not replay its exact preview");
    require(state->session.revision()==choice_revision&&
            state->session.data_generation()==choice_generation&&
            state->session.calculated_boundaries().data()==choice_cache&&
            state->session.document().history==choice_history,
        "Endpoint-cut choice enumeration mutated the live document");
    run(host,"undo");
    require(run(host,"fillet.get",{{"container",id}}).data.at("routes").
            at(0)==cut_route,
        "Fillet route-reset Undo did not restore the endpoint choices");
    run(host,"undo");
    require(run(host,"fillet.get",{{"container",id}}).data.at("routes").
            at(0)==default_route,
        "Fillet endpoint edit needed more than one Undo step");
    run(host,"undo");
    require(!state->session.document().find_container(id),
        "Fillet creation needed more than one Undo step");
    run(host,"redo");run(host,"redo");run(host,"redo");
    require(*state->session.document().find_container(id)==original,"Fillet Redo lost identity");
    const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    require(host.execute({{"command","fillet.set"},{"arguments",
                {{"container",id},{"calculation_method","automatic"}}}}).
                code=="invalid_arguments"&&
            state->session.revision()==revision,
        "Fillet command accepted the removed Automatic method name");
    run(host,"fillet.get",{{"container",id}});require(!run(host,"fillet.set",{{"container",id},{"radius_mm",2}}).data.at("changed").get<bool>(),"Fillet equal patch committed");
    require(state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache,"Fillet read/no-op recalculated");
    auto recovery_preview=original;
    recovery_preview.edge_treatment.primary_size=3;
    const auto recovery_expected=calculate_candidate_body(
        state->session,kernel,recovery_preview);
    const auto strict_failure=host.execute({{"command","fillet.set"},
        {"arguments",{{"container",id},{"radius_mm",500},
            {"calculation_method","zima"}}}});
    require(!strict_failure.ok&&state->session.revision()==revision&&
            *state->session.document().find_container(id)==original,
        "Failed explicit ZIMA calculation changed the authored Fillet");
    const auto recovery_actual=calculate_candidate_body(
        state->session,kernel,recovery_preview);
    require(recovery_actual.vertices==recovery_expected.vertices&&
            recovery_actual.triangles==recovery_expected.triangles&&
            state->session.revision()==revision,
        "Rejected Fillet poisoned a subsequent valid preview");
    for(const auto& patch:std::vector<Json>{{{"radius_mm",0}},{{"radius_mm",1000001}},{{"mode","nonsense"}},{{"mode","linear"}},
        {{"routes",Json::array()}},{{"routes",Json::array({Json{{"edges",Json::array({edge,edge})}}})}},
        {{"routes",Json::array({Json{{"edges",Json::array({Json{{"owner",box},{"key","missing"}}})}}})}},
        {{"routes",Json::array({Json{{"edges",Json::array({edge})},{"start",Json{{"owner",box},{"key","missing"}}}}})},{"mode","linear"}},
        {{"routes",Json::array({Json{{"edges",Json::array({edge})},{"first_mode","cut"}}})}},
        {{"routes",Json::array({Json{{"edges",Json::array({edge})},{"first_mode","other"}}})}},
        {{"routes",Json::array({Json{{"edges",Json::array({edge})},{"first_vertex",Json{{"owner",box},{"key","missing"}}}}})}},
        {{"routes",Json::array({Json{{"edges",Json::array({Json{{"owner",box},{"key",edge_key},{"instance_path","foreign"}}})}}})}},
        {{"routes",Json::array({Json{{"edges",Json::array({edge})},{"geometry_index",0}}})}}}) {
        auto args=patch;args["container"]=id;require(!host.execute({{"command","fillet.set"},{"arguments",args}}).ok,"Invalid Fillet accepted");
        require(state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache&&*state->session.document().find_container(id)==original,"Invalid Fillet partly committed");
    }
    // A well-formed definition that the geometry kernel cannot currently
    // construct remains authored and red, so its exact references can be
    // inspected and corrected instead of being discarded by OK.
    run(host,"fillet.set",{{"container",id},{"radius_mm",500}});
    require(state->session.document().find_container(id)->edge_treatment.primary_size==500&&
            state->session.calculated_boundaries().back().calculation_errors.contains(id),
        "Failed Fillet geometry was rejected instead of being retained as a history error");
    const auto failed_definition=*state->session.document().find_container(id);
    const auto failed_path=directory/"failed-fillet.prtz";
    state->session.document().save(failed_path,
        state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> failed_saved;
    const auto failed_reopened=document::PartDocument::load(
        failed_path,&failed_saved);
    require(*failed_reopened.find_container(id)==failed_definition&&
            !failed_saved.empty()&&
            failed_saved.back().calculation_errors.contains(id),
        "Failed Fillet definition/error was lost after save and reopen");
    run(host,"undo");
    require(*state->session.document().find_container(id)==original&&
            state->session.calculated_boundaries().back().calculation_errors.empty(),
        "Undo did not restore the valid Fillet after a failed edit");
    run(host,"redo");
    require(*state->session.document().find_container(id)==failed_definition&&
            state->session.calculated_boundaries().back().calculation_errors.contains(id),
        "Redo lost the failed Fillet definition/error");
    run(host,"fillet.set",{{"container",id},{"radius_mm",2}});
    require(*state->session.document().find_container(id)==original&&
            state->session.calculated_boundaries().back().calculation_errors.empty(),
        "Corrected Fillet did not recover from its stored error");
    const Json opposite={{"owner",box},{"key",test::profile_key(state->session.document(),box,"edge:x_min:y_max:z_max--x_min:y_max:z_min")}};
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
    const auto linear_volume=measured_volume();
    require(linear_volume>400000-corner*25*50&&linear_volume<400000-corner*4*50,"Variable Fillet volume is outside constant-radius bounds");
    run(host,"fillet.set",{{"container",id},{"reverse",true}});near(radius_at(end_z),5,.1);near(radius_at(start_z),2,.1);
    near(measured_volume(),linear_volume,1e-4);
    run(host,"undo");near(radius_at(end_z),2,.1);run(host,"redo");near(radius_at(end_z),5,.1);
    auto locked=*state->session.document().find_container(id);locked.value_locks={"primary","secondary"};
    static_cast<void>(workspace::commit_edge_treatment(live,kernel,doc_id,locked,workspace::EdgeTreatmentEditMode::Replace));
    for(const auto* field:{"radius_mm","radius_end_mm"})require(host.execute({{"command","fillet.set"},{"arguments",{{"container",id},{field,3}}}}).code=="value_locked","Fillet bypassed dimension lock");
    run(host,"save");std::vector<kernel::BodyResult> saved;const auto loaded=document::PartDocument::load(directory/"edge-treatment.prtz",&saved);
    require(*loaded.find_container(id)==locked,"Native Fillet lost its routes/R1/locks");
    auto saved_analysis=saved.back();kernel.ensure_body_properties(saved_analysis);
    near(kernel.evaluate_history(loaded.kernel_operations()).back().volume,saved_analysis.volume,1e-5);
    zima::test::resize_rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"container",box},{"length_mm","110"}});require(state->session.document().find_container(id)->edge_treatment.route_start_vertices==locked.edge_treatment.route_start_vertices,"Source change replaced R1 ancestry");
    const auto other=run(host,"body.create",{{"name","Other"}}).data.at("body");zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","10"},{"width_mm","10"},{"one_sided",true},{"height_mm","10"}});
    require(host.execute({{"command","fillet.set"},{"arguments",{{"container",id},{"reverse",false}}}}).code=="inactive_body","Fillet edited inactive Body");
    require(host.execute({{"command","fillet.create"},{"arguments",{{"routes",routes()}}}}).code=="invalid_reference","Fillet accepted another Body edge");
    run(host,"body.activate",{{"body",body}});run(host,"body.cursor",{{"body",body},{"index",1}});
    auto cursor_input=*workspace::calculated_operation_input(state->session);kernel.ensure_body_properties(cursor_input);
    near(cursor_input.volume,440000);
    const auto input_revision=state->session.revision();const auto* input_cache=state->session.calculated_boundaries().data();
    require(run(host,"edge_treatment.edges",{{"container",id}}).data.at("total")==12,"Fillet edit lost consumed input edge");
    require(state->session.revision()==input_revision&&state->session.calculated_boundaries().data()==input_cache,
        "Fillet input query recalculated or modified the document");
    // Three independent Chamfer modes; FLIP swaps the two support faces.
    run(host,"new",{{"type","part"},{"name","chamfer-command"}});
    const auto block=zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","100"},{"width_mm","80"},{"one_sided",true},{"height_mm","50"}}).data.at("container");state=live.open_part(live.active_document_id());
    const auto chamfer_routes=Json::array({Json{{"edges",Json::array({Json{{"owner",block},{"key",test::profile_key(state->session.document(),block,"edge:x_max:y_min:z_max--x_max:y_min:z_min")}}})}}});
    const auto chamfer=run(host,"chamfer.create",{{"routes",chamfer_routes},{"distance_a_mm",2}}).data.at("container").get<std::string>();
    near(measured_volume(),399900);
    const Json disconnected_chamfer_edge={{"owner",block},{"key",
        test::profile_key(state->session.document(),block,
            "edge:x_min:y_max:z_max--x_min:y_max:z_min")}};
    const auto separate_chamfer_routes=Json::array({
        chamfer_routes.front(),
        Json{{"edges",Json::array({disconnected_chamfer_edge})}}});
    const auto disconnected_chamfer_route=Json::array({Json{{"edges",
        Json::array({chamfer_routes.front().at("edges").front(),
            disconnected_chamfer_edge})}}});
    const auto before_asymmetric_rejection=state->session.revision();
    for(const auto& invalid_routes:{separate_chamfer_routes,
            disconnected_chamfer_route}) {
        const auto rejected=host.execute({{"command","chamfer.set"},
            {"arguments",{{"container",chamfer},
                {"mode","two_distances"},{"distance_b_mm",5},
                {"routes",invalid_routes}}}});
        require(!rejected.ok&&rejected.code=="invalid_reference"&&
                state->session.revision()==before_asymmetric_rejection,
            "A x B Chamfer accepted more than one continuous line");
    }
    run(host,"chamfer.set",{{"container",chamfer},{"mode","two_distances"},{"distance_b_mm",5}});near(measured_volume(),399750);
    const auto side_distances=[&]() {
        std::pair<double,double> result{};
        for(const auto& e:state->session.calculated_boundaries().back().mesh.edges)if(e.reference.owner_id==chamfer)
            for(const auto& p:e.points)if(std::abs(p.z-25)<1e-6){result.first=std::max(result.first,50-p.x);result.second=std::max(result.second,p.y+40);}
        return result;
    };
    const auto first=side_distances();near(std::min(first.first,first.second),2);near(std::max(first.first,first.second),5);
    run(host,"chamfer.set",{{"container",chamfer},{"flip",true}});const auto flipped=side_distances();near(flipped.first,first.second);near(flipped.second,first.first);
    run(host,"chamfer.set",{{"container",chamfer},{"mode","distance_angle"},{"distance_a_mm",3},{"angle_degrees",30}});
    near(measured_volume(),400000-.5*9*std::tan(std::numbers::pi/6)*50);
    auto chamfer_locked=*state->session.document().find_container(chamfer);chamfer_locked.value_locks={"primary","secondary","treatment_angle"};
    static_cast<void>(workspace::commit_edge_treatment(live,kernel,live.active_document_id(),chamfer_locked,workspace::EdgeTreatmentEditMode::Replace));
    const auto unchanged=state->session.revision();
    for(const auto* field:{"distance_a_mm","distance_b_mm","angle_degrees"})require(host.execute({{"command","chamfer.set"},{"arguments",{{"container",chamfer},{field,4}}}}).code=="value_locked","Chamfer bypassed dimension lock");
    require(run(host,"chamfer.get",{{"container",chamfer}}).data.at("angle_degrees")==30&&state->session.revision()==unchanged,"Chamfer query or rejected edit mutated state");
    run(host,"save");const auto chamfer_saved=document::PartDocument::load(directory/"chamfer-command.prtz");require(*chamfer_saved.find_container(chamfer)==chamfer_locked,"Native Chamfer lost state");
    // A complete circular edge is one closed A x B route. Exercise the same
    // create/edit/flip/Undo/Redo/native-save lifecycle as an open line route.
    run(host,"new",{{"type","part"},{"name","chamfer-circular-command"}});
    constexpr double circular_radius=20.;
    constexpr double circular_height=30.;
    const auto cylinder=zima::test::circular_commands(
        [&](const char* n,commands::Json a){return run(host,n,std::move(a));},
        {{"radius_mm",circular_radius},{"height_mm",circular_height}}).
        data.at("container").get<std::string>();
    state=live.open_part(live.active_document_id());
    const Json circular_edge={{"owner",cylinder},{"key",test::profile_key(
        state->session.document(),cylinder,"circle:z_max")}};
    const auto circular_routes=Json::array({Json{{"edges",
        Json::array({circular_edge})}}});
    const auto circular_chamfer=run(host,"chamfer.create",
        {{"routes",circular_routes},{"mode","two_distances"},
         {"distance_a_mm",2.},{"distance_b_mm",5.}}).data
        .at("container").get<std::string>();
    const auto circular_volume=[=](double radial,double axial) {
        return std::numbers::pi*circular_radius*circular_radius*
                circular_height-
            std::numbers::pi*axial*(circular_radius*radial-
                radial*radial/3.);
    };
    const auto first_circular_volume=measured_volume();
    const double radial_a=circular_volume(2.,5.);
    const double axial_a=circular_volume(5.,2.);
    require(std::min(std::abs(first_circular_volume-radial_a),
                std::abs(first_circular_volume-axial_a))<1.0e-5,
        "Circular A x B Chamfer has the wrong independent volume");
    run(host,"chamfer.set",{{"container",circular_chamfer},{"flip",true}});
    const auto flipped_circular_volume=measured_volume();
    require(std::abs(flipped_circular_volume-first_circular_volume)>1.&&
            std::min(std::abs(flipped_circular_volume-radial_a),
                std::abs(flipped_circular_volume-axial_a))<1.0e-5,
        "Circular A x B Chamfer FLIP did not exchange its support distances");
    run(host,"undo");near(measured_volume(),first_circular_volume);
    run(host,"redo");near(measured_volume(),flipped_circular_volume);
    run(host,"chamfer.set",{{"container",circular_chamfer},
        {"distance_a_mm",3.},{"distance_b_mm",4.}});
    const auto edited_circular_volume=measured_volume();
    require(std::min(std::abs(edited_circular_volume-circular_volume(3.,4.)),
                std::abs(edited_circular_volume-circular_volume(4.,3.)))<
            1.0e-5,
        "Circular A x B Chamfer edit has the wrong independent volume");
    const auto circular_definition=
        *state->session.document().find_container(circular_chamfer);
    run(host,"save");
    std::vector<kernel::BodyResult> circular_saved_results;
    const auto circular_saved=document::PartDocument::load(
        directory/"chamfer-circular-command.prtz",
        &circular_saved_results);
    require(*circular_saved.find_container(circular_chamfer)==
                circular_definition&&!circular_saved_results.empty(),
        "Native file lost the circular A x B Chamfer definition");
    auto circular_saved_body=circular_saved_results.back();
    kernel.ensure_body_properties(circular_saved_body);
    near(circular_saved_body.volume,edited_circular_volume);
    auto circular_cold=kernel.evaluate_history(
        circular_saved.kernel_operations()).back();
    kernel.ensure_body_properties(circular_cold);
    near(circular_cold.volume,edited_circular_volume);
    // The common three-branch outer node must survive the complete command
    // lifecycle, not only the direct exact-builder fixture.
    for(const bool fillet_node:{true,false}) {
        const std::string prefix=fillet_node?"fillet":"chamfer";
        const std::string name=prefix+"-three-branch-node";
        run(host,"new",{{"type","part"},{"name",name}});
        const auto node_box=zima::test::rectangular_commands(
            [&](const char* n,commands::Json a){return run(host,n,std::move(a));},
            {{"length_mm","20"},{"width_mm","20"},
             {"one_sided",true},{"height_mm","20"}}).data.at("container");
        state=live.open_part(live.active_document_id());
        const auto node_edge=[&](const char* key) {
            return Json{{"owner",node_box},{"key",test::profile_key(
                state->session.document(),node_box,key)}};
        };
        const auto node_routes=Json::array({
            Json{{"edges",Json::array({node_edge(
                "edge:x_max:y_min:z_max--x_max:y_min:z_min")})}},
            Json{{"edges",Json::array({node_edge(
                "edge:x_max:y_min:z_min--x_max:y_max:z_min")})}},
            Json{{"edges",Json::array({node_edge(
                "edge:x_min:y_min:z_min--x_max:y_min:z_min")})}}});
        const auto created=run(host,(prefix+".create").c_str(),
            {{"routes",node_routes},
             {fillet_node?"radius_mm":"distance_a_mm",2}}).data
            .at("container").get<std::string>();
        require(state->session.calculated_boundaries().back().
                    calculation_errors.empty(),
            "Three-branch edge-treatment command retained a calculation error");
        const auto authored=*state->session.document().find_container(created);
        const double node_volume=measured_volume();
        const auto path=directory/(name+".prtz");
        state->session.document().save(path,
            state->session.calculated_boundaries());
        std::vector<kernel::BodyResult> node_saved;
        const auto reopened=document::PartDocument::load(path,&node_saved);
        require(*reopened.find_container(created)==authored&&
                !node_saved.empty(),
            "Native file lost the three-branch treatment definition");
        auto saved_node=node_saved.back();kernel.ensure_body_properties(saved_node);
        near(saved_node.volume,node_volume);
        const auto cold=kernel.evaluate_history(reopened.kernel_operations());
        auto cold_node=cold.back();kernel.ensure_body_properties(cold_node);
        near(cold_node.volume,node_volume);
        run(host,"undo");
        require(!state->session.document().find_container(created),
            "Three-branch treatment create took more than one Undo step");
        run(host,"redo");
        require(*state->session.document().find_container(created)==authored&&
                state->session.calculated_boundaries().back().
                    calculation_errors.empty(),
            "Three-branch treatment Redo lost its definition or result");
        near(measured_volume(),node_volume);
        zima::test::resize_rectangular_commands(
            [&](const char* n,commands::Json a){return run(host,n,std::move(a));},
            {{"container",node_box},{"length_mm","24"}});
        require(*state->session.document().find_container(created)==authored&&
                state->session.calculated_boundaries().back().
                    calculation_errors.empty(),
            "Upstream resize lost the three-branch treatment anchors");
        const double resized_node_volume=measured_volume();
        require(std::abs(resized_node_volume-node_volume)>1.,
            "Upstream resize did not regenerate the three-branch treatment");
        run(host,"undo");near(measured_volume(),node_volume);
        run(host,"redo");near(measured_volume(),resized_node_volume);
        require(*state->session.document().find_container(created)==authored,
            "Resize Undo/Redo changed the three-branch treatment definition");
    }
    // Four independent contours use alternating explicit R1 ends. The runtime
    // topology may contain repeated shape uses for one persisted input edge.
    run(host,"new",{{"type","part"},{"name","four-linear-routes"}});
    zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","100"},{"width_mm","80"},{"one_sided",true},{"height_mm","50"}});state=live.open_part(live.active_document_id());
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
    const auto many_volume=measured_volume();check_radii(state->session.calculated_boundaries().back(),false);
    require(many_volume>400000-4*corner*25*50&&many_volume<400000-4*corner*4*50,"Four-contour Fillet has an invalid volume");
    run(host,"fillet.set",{{"container",many},{"reverse",true}});check_radii(state->session.calculated_boundaries().back(),true);near(measured_volume(),many_volume,.001);
    const auto ids=face_ids(state->session.calculated_boundaries().back());std::reverse(four_routes.begin(),four_routes.end());
    run(host,"fillet.set",{{"container",many},{"routes",four_routes}});check_radii(state->session.calculated_boundaries().back(),true);
    require(face_ids(state->session.calculated_boundaries().back())==ids,"Reordering Fillet routes replaced generated face ancestry");
    run(host,"save");const auto four_saved=document::PartDocument::load(directory/"four-linear-routes.prtz");kernel::OcctKernel cold;
    const auto four_cold=cold.evaluate_history(four_saved.kernel_operations());check_radii(four_cold.back(),true);
    near(four_cold.back().volume,many_volume,.001);require(face_ids(four_cold.back())==ids,"Cold calculation lost Fillet ancestry");
    const auto before_remove=*state->session.document().find_container(many);
    auto removal=run(host,"edge_treatment.remove",{{"container",many},{"route",0},{"edge",four_routes[0].at("edges")[0]}}).data;
    require(!removal.at("removed").get<bool>()&&removal.at("routes").size()==3,"Removing one linear route removed its feature");
    const auto& surviving=state->session.document().find_container(many)->edge_treatment;
    for(std::size_t i=0;i<surviving.routes.size();++i)require(surviving.routes[i]==before_remove.edge_treatment.routes[i+1]&&
        surviving.route_start_vertices[i]==before_remove.edge_treatment.route_start_vertices[i+1],"Removing an earlier linear route moved a surviving R1");
    run(host,"undo");require(*state->session.document().find_container(many)==before_remove,"Undo did not restore linear routes and explicit R1");
    // Remove an exact member, a whole route, then the last member for each type.
    for(const bool fillet:{true,false}) {
        const std::string prefix=fillet?"fillet":"chamfer",name=prefix+"-remove";
        run(host,"new",{{"type","part"},{"name",name}});
        const auto block=zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","10"},{"width_mm","10"},{"one_sided",true},{"height_mm","10"}}).data.at("container").get<std::string>();
        state=live.open_part(live.active_document_id());const auto owning_body=state->session.document().body_history.active_body_id();
        const Json a={{"owner",block},{"key",test::profile_key(state->session.document(),block,"edge:x_max:y_min:z_max--x_max:y_min:z_min")}},b={{"owner",block},{"key",test::profile_key(state->session.document(),block,"edge:x_min:y_max:z_max--x_min:y_max:z_min")}},
            c={{"owner",block},{"key",test::profile_key(state->session.document(),block,"edge:x_min:y_min:z_max--x_min:y_min:z_min")}};
        const auto grouped=Json::array({Json{{"edges",Json::array({a,b})}},Json{{"edges",Json::array({c})}}});
        const auto treatment=run(host,(prefix+".create").c_str(),{{"routes",grouped}}).data.at("container").get<std::string>();
        const auto full=*state->session.document().find_container(treatment);const double cut=10*(fillet?corner:.5);
        near(measured_volume(),1000-3*cut);
        const auto unchanged=state->session.revision();const auto* original_cache=state->session.calculated_boundaries().data();
        for(auto args:std::vector<Json>{{{"route",-1}},{{"route",Json(std::numeric_limits<std::uint64_t>::max())}},{{"route",3}},{{"route",.5}},
            {{"route",0},{"edge",c}},{{"route",0},{"edge",Json{{"owner",block},{"key",test::profile_key(state->session.document(),block,"edge:x_max:y_min:z_max--x_max:y_min:z_min")},{"instance_path","foreign"}}}},
            {{"route",0},{"edge",Json{{"owner",block},{"key",test::profile_key(state->session.document(),block,"edge:x_max:y_min:z_max--x_max:y_min:z_min")},{"geometry_index",0}}}},{{"route",0},{"edge",nullptr}}}) {
            args["container"]=treatment;require(!host.execute({{"command","edge_treatment.remove"},{"arguments",args}}).ok,"Invalid route removal succeeded");
            require(state->session.revision()==unchanged&&state->session.calculated_boundaries().data()==original_cache&&*state->session.document().find_container(treatment)==full,"Rejected removal mutated the document");
        }
        require(host.execute({{"command","edge_treatment.remove"},{"arguments",{{"container",block},{"route",0}}}}).code=="wrong_feature","Removal accepted a primitive");
        run(host,"body.create",{{"name","Inactive check"}});
        require(host.execute({{"command","edge_treatment.remove"},{"arguments",{{"container",treatment},{"route",0}}}}).code=="inactive_body","Removal edited another Body");
        run(host,"body.activate",{{"body",owning_body}});
        removal=run(host,"edge_treatment.remove",{{"container",treatment},{"route",0},{"edge",a}}).data;
        require(!removal.at("removed").get<bool>()&&removal.at("routes").size()==2&&removal.at("routes")[0].at("edges")[0].at("key")==b.at("key"),"Exact member deletion removed another edge");
        near(measured_volume(),1000-2*cut);
        run(host,"undo");require(*state->session.document().find_container(treatment)==full,"Member removal took multiple Undo steps");run(host,"redo");
        removal=run(host,"edge_treatment.remove",{{"container",treatment},{"route",1}}).data;
        require(!removal.at("removed").get<bool>()&&removal.at("routes").size()==1,"Route deletion removed the wrong selection");
        near(measured_volume(),1000-cut);
        run(host,"save");const auto remaining=document::PartDocument::load(directory/(name+".prtz"));
        require(*remaining.find_container(treatment)==*state->session.document().find_container(treatment),"Native file lost remaining route identity");
        near(kernel.evaluate_history(remaining.kernel_operations()).back().volume,1000-cut);
        const auto single=*state->session.document().find_container(treatment);
        removal=run(host,"edge_treatment.remove",{{"container",treatment},{"route",0},{"edge",b}}).data;
        require(removal.at("removed").get<bool>()&&removal.at("calculation_errors").empty()&&!state->session.document().find_container(treatment),"Last edge retained an empty treatment");
        near(measured_volume(),1000);
        run(host,"undo");require(*state->session.document().find_container(treatment)==single,"Undo lost deleted feature identity");run(host,"redo");
        require(!state->session.document().find_container(treatment),"Redo failed to delete the last route");
    }
    // The last route follows history deletion: retain a dependent feature with
    // its error, expose changed=true, and allow one Undo to restore both bodies.
    run(host,"new",{{"type","part"},{"name","dependent-route-removal"}});
    const auto source=zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","10"},{"width_mm","10"},{"one_sided",true},{"height_mm","10"}}).data.at("container");
    state=live.open_part(live.active_document_id());
    const auto parent=run(host,"fillet.create",{{"radius_mm",2},{"routes",Json::array({Json{{"edges",Json::array({Json{{"owner",source},{"key",test::profile_key(state->session.document(),source,"edge:x_max:y_min:z_max--x_max:y_min:z_min")}}})}}})}}).data.at("container").get<std::string>();
    const auto& parent_body=state->session.calculated_boundaries().back();
    const auto generated=std::ranges::find_if(parent_body.mesh.edges,[&](const auto& e){return e.reference.owner_id==parent&&e.points.size()>2&&
        std::ranges::all_of(e.points,[](const auto& p){return std::abs(p.z-5)<1e-6;});});
    require(generated!=parent_body.mesh.edges.end(),"Missing generated rim for dependency fixture");
    const Json generated_edge={{"owner",generated->reference.owner_id},{"key",generated->reference.semantic_key}};
    const auto child=run(host,"fillet.create",{{"radius_mm",.2},{"routes",Json::array({Json{{"edges",Json::array({generated_edge})}}})}}).data.at("container").get<std::string>();
    const auto intact_volume=measured_volume();
    const auto deletion=host.execute({{"command","edge_treatment.remove"},{"arguments",{{"container",parent},{"route",0}}}});
    require(!deletion.ok&&deletion.code=="calculation_errors"&&deletion.data.at("changed")==true&&deletion.data.at("removed")==true,
        "Dependent last-route removal did not report its committed history change");
    require(!state->session.document().find_container(parent)&&state->session.document().find_container(child)&&
        !deletion.data.at("calculation_errors").empty(),"Dependent feature or its error was lost");
    run(host,"undo");require(state->session.document().find_container(parent)&&state->session.calculated_boundaries().back().calculation_errors.empty(),"Undo failed to restore dependency geometry");
    near(measured_volume(),intact_volume);
    // A closed circular edge needs no invented R1 endpoint for constant radius.
    run(host,"new",{{"type","part"},{"name","circular-fillet"}});zima::test::circular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"radius_mm","10"},{"height_mm","20"}});state=live.open_part(live.active_document_id());
    const auto& input=state->session.calculated_boundaries().back();
    const auto ring=std::ranges::find_if(input.mesh.edges,[](const auto& e){return e.reference.valid()&&e.points.size()>2&&std::ranges::all_of(e.points,[](const auto& p){return std::abs(p.z-20)<1e-6;});});
    require(ring!=input.mesh.edges.end(),"Cylinder top ring is missing");
    const auto ring_routes=Json::array({Json{{"edges",Json::array({Json{{"owner",ring->reference.owner_id},{"key",ring->reference.semantic_key}}})}}});
    run(host,"fillet.create",{{"routes",ring_routes}});
    near(measured_volume(),2000*std::numbers::pi-2*std::numbers::pi*(55./6-9*std::numbers::pi/4),1e-4);
    // Properties commits pending geometry and annotation placement together;
    // a layout-only edit reuses the current calculated body and has one Undo.
    const auto treatment=state->session.document().history.back();
    const auto volume=measured_volume();
    kernel::DimensionLayout layout;layout.text_along=4;layout.text_outward=3;
    const std::vector<kernel::DimensionLayoutEntry> layouts{{treatment.id,"parameter:primary",layout}};
    const auto layout_revision=state->session.revision();
    for(const auto& invalid:std::vector<kernel::DimensionLayoutEntry>{{"foreign","parameter:primary",layout},{treatment.id,"parameter:diameter",layout}}) {
        bool rejected=false;
        try {static_cast<void>(workspace::commit_edge_treatment(live,kernel,live.active_document_id(),treatment,workspace::EdgeTreatmentEditMode::Replace,{invalid}));}
        catch(const workspace::EdgeTreatmentOperationError& error){rejected=std::string(error.code)=="invalid_reference";}
        require(rejected&&state->session.revision()==layout_revision&&state->session.document().dimension_layouts.empty(),"Invalid annotation layout changed the document");
    }
    require(workspace::commit_edge_treatment(live,kernel,live.active_document_id(),treatment,workspace::EdgeTreatmentEditMode::Replace,layouts),"Layout-only edit was ignored");
    require(state->session.document().dimension_layouts==layouts,"Layout-only edit was not stored");
    near(measured_volume(),volume);
    require(!workspace::commit_edge_treatment(live,kernel,live.active_document_id(),treatment,workspace::EdgeTreatmentEditMode::Replace,layouts),"Unchanged layout created an Undo step");
    run(host,"undo");require(state->session.document().dimension_layouts.empty(),"Layout-only edit did not undo in one step");
    auto changed=treatment;changed.edge_treatment.primary_size=.75;
    require(workspace::commit_edge_treatment(live,kernel,live.active_document_id(),changed,workspace::EdgeTreatmentEditMode::Replace,layouts),"Combined treatment edit was ignored");
    require(state->session.document().dimension_layouts==layouts&&std::abs(measured_volume()-volume)>.1,"Combined edit lost annotation or geometry");
    run(host,"undo");require(state->session.document().dimension_layouts.empty()&&*state->session.document().find_container(treatment.id)==treatment,"Combined geometry/layout edit took multiple Undo steps");
    near(measured_volume(),volume);

    // Automatic/ZIMA may deliberately use an authored source edge after a
    // later Boolean split it into current-body fragments. OCCT remains limited
    // to exact current-body topology and must reject that explicit source.
    run(host,"new",{{"type","part"},{"name","original-edge-treatment"}});
    const auto source_box=zima::test::rectangular_commands(
        [&](const char* n,commands::Json a){return run(host,n,std::move(a));},
        {{"length_mm","20"},{"width_mm","20"},{"one_sided",true},
         {"height_mm","30"}}).data.at("container").get<std::string>();
    state=live.open_part(live.active_document_id());
    const kernel::EdgeReference source_edge{source_box,
        test::profile_key(state->session.document(),source_box,
            "edge:x_max:y_min:z_max--x_max:y_min:z_min"),{}};
    zima::test::rectangular_commands(
        [&](const char* n,commands::Json a){return run(host,n,std::move(a));},
        {{"length_mm","5"},{"width_mm","5"},{"one_sided",true},
         {"height_mm","10"},{"combine","subtract"},
         {"placement",{{"x",8},{"y",-8}}}});
    const auto& split_input=state->session.calculated_boundaries().back();
    require(std::ranges::none_of(split_input.mesh.edges,
                [&](const auto& edge){return edge.reference==source_edge;})&&
            std::ranges::any_of(split_input.mesh.original_references.edges,
                [&](const auto& edge){return edge.reference==source_edge;}),
        "Original-edge command fixture did not split and retain its source edge");
    auto original_args=Json{{"distance_a_mm",1},
        {"routes",Json::array({Json{{"edges",Json::array({Json{
            {"owner",source_edge.owner_id},{"key",source_edge.semantic_key}}})}}})}};
    const auto split_volume=measured_volume();
    const auto source_treatment=run(host,"chamfer.create",original_args).data
        .at("container").get<std::string>();
    require(state->session.calculated_boundaries().back().calculation_errors.empty()&&
            std::abs(measured_volume()-split_volume)>.001,
        "Automatic Chamfer did not use the explicitly selected original edge");
    const auto treated_volume=measured_volume();
    const auto original_path=directory/"original-edge-treatment.prtz";
    state->session.document().save(original_path,
        state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> restored_boundaries;
    auto restored_document=document::PartDocument::load(original_path,
        &restored_boundaries);
    kernel::OcctKernel restored_kernel;
    workspace::Workspace restored_live;
    const auto restored_id=restored_document.document_id;
    restored_live.add_part(std::move(restored_document),
        std::move(restored_boundaries));
    auto* restored_state=restored_live.open_part(restored_id);
    auto edited=*restored_state->session.document()
        .find_container(source_treatment);
    edited.edge_treatment.primary_size=.75;
    const auto preview_revision=restored_state->session.revision();
    const auto preview_generation=restored_state->session.data_generation();
    const auto* preview_cache=restored_state->session.calculated_boundaries().data();
    const auto preview_history=restored_state->session.document().history;
    const auto exact_preview=calculate_candidate_body(
        restored_state->session,restored_kernel,edited);
    require(!exact_preview.edges.empty(),
        "Changed original-edge treatment produced no exact preview wire");
    require(restored_state->session.revision()==preview_revision&&
            restored_state->session.data_generation()==preview_generation&&
            restored_state->session.calculated_boundaries().data()==preview_cache&&
            restored_state->session.document().history==preview_history,
        "Exact edge-treatment preview mutated the live document or calculation");
    require(workspace::commit_edge_treatment(restored_live,restored_kernel,
            restored_id,std::move(edited),
            workspace::EdgeTreatmentEditMode::Replace)&&
            restored_live.open_part(restored_id)->session
                .calculated_boundaries().back().calculation_errors.empty(),
        "Save/reopen lost the native support for an original-edge treatment");
    auto restored_result=restored_live.open_part(restored_id)->session
        .calculated_boundaries().back();
    restored_kernel.ensure_body_properties(restored_result);
    require(std::abs(restored_result.volume-treated_volume)>.001,
        "Editing a reopened original-edge treatment reused stale geometry");
    run(host,"undo");near(measured_volume(),split_volume);
    original_args["calculation_method"]="occt";
    const auto original_revision=state->session.revision();
    const auto rejected_original=host.execute(
        {{"command","chamfer.create"},{"arguments",original_args}});
    require(!rejected_original.ok&&rejected_original.code=="invalid_reference"&&
            state->session.revision()==original_revision,
        "OCCT accepted an original edge absent from the calculated Body");
}
void verify_preview_recovery_script(const kernel::OcctKernel& kernel,
        const fs::path& script) {
    workspace::Workspace live;
    command_host::Options options;
    auto directory=script.parent_path();
    command_host::Host host(live,kernel,directory,options);
    std::ifstream commands(script);
    std::string line,owner;
    while(std::getline(commands,line)) {
        const auto result=host.execute_text(line);
        require(result.ok,"Recovery fixture command failed");
        if(result.data.contains("container"))owner=result.data.at("container");
    }
    auto* state=live.open_part(live.active_document_id());
    require(state&&!owner.empty(),"Recovery fixture has no treatment");
    auto pending=*state->session.document().find_container(owner);
    run(host,"undo");
    pending.edge_treatment.primary_size=1.;
    using Mode=document::EdgeTreatmentParameters::RouteEndMode;
    for(auto& ends:pending.edge_treatment.route_end_faces)
        ends.first_mode=ends.last_mode=Mode::Cut;
    const auto revision=state->session.revision();
    const auto generation=state->session.data_generation();
    std::vector<std::pair<std::size_t,bool>> joined;
    const auto& ends=pending.edge_treatment.route_end_faces;
    require(ends.size()==2,"Recovery fixture requires two joined routes");
    for(bool first:{true,false})for(bool second:{true,false})
        if((first?ends[0].first_vertex:ends[0].last_vertex)==
           (second?ends[1].first_vertex:ends[1].last_vertex))
            joined={{0,first},{1,second}};
    require(joined.size()==2,"Recovery fixture has no shared node");
    for(const auto [route,first]:joined) {
        auto& ends=pending.edge_treatment.route_end_faces[route];
        (first?ends.first_mode:ends.last_mode)=Mode::RoundedJoin;
    }
    const auto expected=calculate_candidate_body(
        state->session,kernel,pending);
    const auto choices=calculate_end_choices(
        state->session,kernel,pending,joined);
    std::cout<<"Recovery terminal alternatives="<<choices.size()<<'\n';
    const auto actual=calculate_candidate_body(
        state->session,kernel,pending);
    require(actual.vertices==expected.vertices&&
            actual.triangles==expected.triangles,
        "Terminal probes changed the next identical preview");
    require(state->session.revision()==revision&&
            state->session.data_generation()==generation,
        "Terminal probes changed the live revision/generation");
    std::cout<<"Terminal probe recovery passed\n";
}
double wire_boundary_deviation(const app::workspace_detail::EdgeTreatmentPreviewGeometry& wire,
        const kernel::BodyResult& body,bool allow_merged_boundaries=false) {
    const auto shape=kernel::read_calculated_body_shape(body);
    require(!shape.IsNull(),"Wire comparison has no native result");
    struct Curve {Handle(Geom_Curve) curve;double first{},last{};};
    std::vector<Curve> curves;
    for(TopExp_Explorer item(shape,TopAbs_EDGE);item.More();item.Next()) {
        Curve curve;curve.curve=BRep_Tool::Curve(TopoDS::Edge(item.Current()),curve.first,curve.last);
        if(!curve.curve.IsNull())curves.push_back(std::move(curve));
    }
    double worst{};
    for(const auto& edge:wire.edges)for(const auto& p:edge.points) {
        const gp_Pnt point(p.x,p.y,p.z);
        double closest=std::numeric_limits<double>::max();
        for(const auto& boundary:curves) {
            GeomAPI_ProjectPointOnCurve projection(point,boundary.curve,boundary.first,boundary.last);
            if(projection.NbPoints())closest=std::min(closest,projection.LowerDistance());
            for(const auto parameter:{boundary.first,boundary.last})
                closest=std::min(closest,boundary.curve->Value(parameter).Distance(point));
        }
        // Original authored wires can remain on a support face after the
        // Body Boolean unifies that boundary with an existing coplanar face.
        // ON is acceptable here; a point outside/inside the result is not.
        if(allow_merged_boundaries&&closest>1.e-5&&
                BRepClass3d_SolidClassifier(shape,point,1.e-7).State()==TopAbs_ON)closest=0.;
        worst=std::max(worst,closest);
    }
    return worst;
}
void verify_node_wires(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={fs::absolute("config/templates"),"START_PART_mm.prtz","START_ASSEMBLY_mm.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","wire-nodes"}});
    const auto block=[&](double x,double y,double z,double elevation,bool subtract=false) {
        return test::rectangular_commands([&](const char* n,Json a){return run(host,n,std::move(a));},
            {{"length_mm",x},{"width_mm",y},{"height_mm",z},{"one_sided",true},
             {"placement",{{"z",elevation}}},{"combine",subtract?"subtract":"add"}});
    };
    block(60,60,10,0);
    const auto check_node=[&](kernel::Vec3 node,std::size_t count,bool arc,bool chamfer=false) {
        const auto id=live.active_document_id();const auto& session=live.open_part(id)->session;
        const auto* input=workspace::calculated_operation_input(session,{});
        require(input,"Node wire fixture has no calculated input");
        std::vector<kernel::ViewerEdge> selected;
        const auto distance=[](kernel::Vec3 a,kernel::Vec3 b){return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);};
        for(const auto& edge:input->mesh.edges)if(!edge.parameter_seam&&edge.points.size()==2&&
            (distance(edge.points.front(),node)<1e-7||distance(edge.points.back(),node)<1e-7)) {
            if(arc&&std::abs(edge.points.front().z-edge.points.back().z)>1e-7)continue;
            selected.push_back(edge);
        }
        require(selected.size()>=count,"Node fixture has too few branches");selected.resize(count);
        std::optional<double> spike_volume;
        for(bool reverse:{false,true})for(const auto mode:arc?
                std::vector{document::EdgeTreatmentParameters::RouteEndMode::RoundedJoin,
                    document::EdgeTreatmentParameters::RouteEndMode::RoundedArc}:
                std::vector{document::EdgeTreatmentParameters::RouteEndMode::RoundedJoin}) {
            auto ordered=selected;
            if(reverse)std::ranges::reverse(ordered);
            auto feature=chamfer?document::PartDocument::create_chamfer_container({ordered.front().reference}):
                document::PartDocument::create_fillet_container({ordered.front().reference});
            feature.edge_treatment.routes.clear();feature.edge_treatment.route_end_faces.clear();
            feature.edge_treatment.primary_size=2.;
            feature.edge_treatment.calculation_method=document::EdgeTreatmentParameters::CalculationMethod::Zima;
            std::vector<std::vector<kernel::ViewerEdge>> groups;
            for(const auto& edge:ordered) {
                feature.edge_treatment.routes.push_back({edge.reference});
                feature.edge_treatment.route_end_faces.emplace_back();
                auto& ends=feature.edge_treatment.route_end_faces.back();
                ends.first_mode=distance(edge.points.front(),node)<1e-7?mode:
                    document::EdgeTreatmentParameters::RouteEndMode::Cut;
                ends.last_mode=distance(edge.points.back(),node)<1e-7?mode:
                    document::EdgeTreatmentParameters::RouteEndMode::Cut;
                require(edge.edge_treatment_endpoint_references.size()==2,"Node branch lost endpoint identities");
                ends.first_vertex=edge.edge_treatment_endpoint_references.front();
                ends.last_vertex=edge.edge_treatment_endpoint_references.back();
                groups.push_back({edge});
            }
            const auto calculations=kernel::body_calculation_count.load();
            std::vector<std::pair<std::size_t,bool>> node_ends;
            const auto wire=app::workspace_detail::edge_treatment_preview_wire(groups,
                feature.edge_treatment,feature.feature_kind,feature.id,&input->mesh);
            for(const auto& terminal:wire.terminal_labels)
                if(distance(terminal.point,node)<1.e-7)
                    node_ends.emplace_back(terminal.route,terminal.first);
            const auto nodes=app::workspace_detail::edge_treatment_preview_node_solutions(groups,feature,input->mesh,node_ends);
            require(nodes.rounding==arc,"Selectable/passive node classification is incorrect");
            auto automatic=feature;
            automatic.edge_treatment.route_end_faces.clear();
            require(app::workspace_detail::edge_treatment_preview_node_solutions(
                    groups,automatic,input->mesh,node_ends).rounding==arc,
                "Inspection without optional end overrides lost its node choices");
            require(kernel::body_calculation_count.load()==calculations,"Node wire calculated a Body");
            kernel::BodyResult body;
            const auto result=calculate_candidate_body(session,kernel,feature,&body);
            if(arc) {
                kernel.ensure_body_properties(body);
                if(mode==document::EdgeTreatmentParameters::RouteEndMode::RoundedJoin)
                    spike_volume=body.volume;
                else {
                    require(spike_volume&&std::abs(*spike_volume-body.volume)>1.e-5,
                        "Rounded node did not change the spike geometry");
                    require(std::ranges::any_of(wire.edges,[](const auto& e){return e.points.size()==65;}),
                        "Rounded node omitted its swept contact arc");
                    require(std::ranges::any_of(body.mesh.triangle_references,[&](const auto& face) {
                        const auto surface=face.surface?face.surface:face.contour_surface;
                        return face.owner_id==feature.id&&surface&&
                            surface->kind==(chamfer?kernel::SurfaceGeometry::Kind::Cone:kernel::SurfaceGeometry::Kind::Torus);
                    }),"Rounded node face lost its treatment ownership");
                }
            }
            const double worst=wire_boundary_deviation(wire,body);
            std::cout<<"Node wire chamfer="<<chamfer<<" branches="<<count<<" arc="<<arc<<" mode="<<static_cast<int>(mode)
                <<" reverse="<<reverse<<" boundary deviation="<<worst<<'\n';
            require(worst<1e-5,"Lightweight node wire differs from exact calculated Body boundaries");
            if(arc&&mode==document::EdgeTreatmentParameters::RouteEndMode::RoundedArc)
                {
                    const auto* fixture_path=std::getenv("ZIMA_WIRE_FIXTURE_DIRECTORY");
                    const auto target=fixture_path?fs::u8path(fixture_path):directory;
                    workspace::Workspace saved;saved.add_part(session.document(),session.calculated_boundaries());
                    require(workspace::commit_edge_treatment(saved,kernel,id,feature,workspace::EdgeTreatmentEditMode::Create),
                        "Rounded fixture did not commit");
                    const auto& output=saved.open_part(id)->session;
                    output.document().save(target/"rounded-node.prtz",output.calculated_boundaries());
                    std::ofstream(target/"rounded-node-owner.txt")<<feature.id;
                    const auto fixture="rounded-node-"+session.document().name+(chamfer?"-chamfer":"-fillet");
                    output.document().save(target/(fixture+".prtz"),output.calculated_boundaries());
                    std::ofstream(target/(fixture+"-owner.txt"))<<feature.id;
                    std::vector<kernel::BodyResult> boundaries;
                    auto document=document::PartDocument::load(target/"rounded-node.prtz",&boundaries);
                    document::DocumentSession reopened(document,boundaries);
                    const auto* restored=workspace::calculated_operation_input(reopened,feature.id);
                    require(restored,"Reopened node has no rollback input");
                    std::vector<std::vector<kernel::ViewerEdge>> restored_groups;
                    for(std::size_t r=0;r<feature.edge_treatment.routes.size();++r)
                        restored_groups.push_back(document::current_treatment_selection_wire(feature.edge_treatment,r,{},restored->mesh));
                    const auto restored_wire=app::workspace_detail::edge_treatment_preview_wire(restored_groups,
                        feature.edge_treatment,feature.feature_kind,feature.id,&restored->mesh);
                    std::cout<<"Reopened node edges="<<restored_wire.edges.size()<<" expected="<<wire.edges.size()<<'\n';
                    require(restored_wire.edges.size()==wire.edges.size(),"Save/reopen lost the rounded node preview");
                    for(std::size_t e=0;e<wire.edges.size();++e) {
                        require(restored_wire.edges[e].points.size()==wire.edges[e].points.size(),"Reopened node changed wire sampling");
                        for(std::size_t p=0;p<wire.edges[e].points.size();++p)
                            require(distance(restored_wire.edges[e].points[p],wire.edges[e].points[p])<1e-9,
                                "Reopened node changed its exact wire");
                    }
                }
        }
    };
    check_node({30,30,5},2,false);check_node({30,30,5},3,false);
    block(20,20,10,10);
    check_node({-10,-10,5},2,true);check_node({-10,-10,5},3,false);
    check_node({-10,-10,5},2,true,true);
    run(host,"new",{{"type","part"},{"name","pocket-wire-nodes"}});
    block(60,60,10,0);block(20,20,10,5,true);
    check_node({-10,-10,5},2,true);
    check_node({-10,-10,5},2,true,true);
}
void verify_saved_treatment(const kernel::OcctKernel& kernel,const fs::path& path,
        const std::string& owner) {
    std::vector<kernel::BodyResult> boundaries;
    auto document=document::PartDocument::load(path,&boundaries);
    const auto* found=document.find_container(owner);
    require(found,"Saved treatment owner is missing");
    const auto feature=*found;
    while(document.history.back().id!=owner)
        document.erase_history_object(document.history.back().id);
    workspace::Workspace prepared;
    prepared.add_part(document,boundaries);
    // Properties explicitly prepares its real rollback input before any
    // analytic draft edits. Exercise the same input, including terminal faces
    // omitted from a compact persisted history packet.
    static_cast<void>(workspace::materialize_part_history_input_boundary(
        prepared,kernel,document.document_id,owner));
    const auto& session=prepared.open_part(document.document_id)->session;
    const auto* input=workspace::calculated_operation_input(session,owner);
    require(input,"Saved treatment has no calculated input");
    std::vector<std::vector<kernel::ViewerEdge>> groups;
    for(std::size_t r=0;r<feature.edge_treatment.routes.size();++r)
        groups.push_back(document::current_treatment_selection_wire(feature.edge_treatment,r,{},input->mesh));
    for(const double radius:{1.,3.,4.,5.,6.,8.}) {
        auto candidate=feature;candidate.edge_treatment.primary_size=radius;
        const auto count=kernel::body_calculation_count.load();
        const auto start=std::chrono::steady_clock::now();
        const auto wire=app::workspace_detail::edge_treatment_preview_wire(groups,
            candidate.edge_treatment,candidate.feature_kind,owner,&input->mesh);
        const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        require(kernel::body_calculation_count.load()==count,"Saved-model preview calculated a Body");
        kernel::BodyResult body;
        calculate_candidate_body(session,kernel,candidate,&body);
        const auto deviation=wire_boundary_deviation(wire,body,radius>6.);
        std::cout<<"Saved treatment R="<<radius<<" wire="<<wire.edges.size()<<" ms="<<elapsed
            <<" deviation="<<deviation<<std::endl;
        require(!wire.edges.empty()&&deviation<1.e-5,"Saved-model preview disagrees with the calculated treatment");
    }
}
void verify_saved_nodes(const kernel::OcctKernel& kernel,const fs::path& path,
        const std::string& owner) {
    using Mode=document::EdgeTreatmentParameters::RouteEndMode;
    std::vector<kernel::BodyResult> boundaries;
    auto document=document::PartDocument::load(path,&boundaries);
    const auto* found=document.find_container(owner);require(found,"Node fixture owner is missing");
    const auto source=*found;
    while(document.history.back().id!=owner)document.erase_history_object(document.history.back().id);
    document.erase_history_object(owner);
    document::DocumentSession session(document,boundaries);
    const auto* input=workspace::calculated_operation_input(session,{});
    require(input,"Node fixture has no calculated input");
    std::vector<std::vector<kernel::ViewerEdge>> groups;
    for(std::size_t r=0;r<source.edge_treatment.routes.size();++r)
        groups.push_back(document::current_treatment_selection_wire(source.edge_treatment,r,{},input->mesh));
    for(const auto kind:{document::FeatureKind::Fillet,document::FeatureKind::Chamfer}) {
        std::optional<double> spike_volume;
        for(const auto mode:{Mode::RoundedJoin,Mode::RoundedArc}) {
            auto candidate=source;candidate.feature_kind=kind;
            const auto original_wire=app::workspace_detail::edge_treatment_preview_wire(groups,candidate.edge_treatment,kind,owner,&input->mesh);
            std::size_t joints{};
            for(const auto& node:app::workspace_detail::edge_treatment_terminal_nodes(original_wire.terminal_labels)) {
                if(node.route_ends.size()!=2)continue;
                const auto solution=app::workspace_detail::edge_treatment_preview_node_solutions(groups,candidate,input->mesh,node.route_ends);
                if(node.text=="1.1-2.1"||node.text=="2.2-3.1")
                    require(solution.rounding,"The requested saved pocket corner has no rounded alternative");
                if(!solution.rounding)continue;
                for(const auto [route,first]:node.route_ends) {
                    auto& end=candidate.edge_treatment.route_end_faces[route];
                    (first?end.first_mode:end.last_mode)=mode;(first?end.first:end.last)={};
                }
                ++joints;
            }
            require(joints>=2,"Saved pocket did not exercise its connected corners");
            const auto count=kernel::body_calculation_count.load();
            const auto start=std::chrono::steady_clock::now();
            const auto wire=app::workspace_detail::edge_treatment_preview_wire(groups,candidate.edge_treatment,kind,owner,&input->mesh);
            const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            require(kernel::body_calculation_count.load()==count,"Saved node preview calculated a Body");
            kernel::BodyResult body;
            const auto file=path.parent_path()/(kind==document::FeatureKind::Fillet?"user-nodes-fillet.prtz":"user-nodes-chamfer.prtz");
            calculate_candidate_body(session,kernel,candidate,&body,mode==Mode::RoundedArc?file:fs::path{});
            kernel.ensure_body_properties(body);
            if(mode==Mode::RoundedJoin)spike_volume=body.volume;
            else require(std::abs(body.volume-*spike_volume)>1.e-5,"Saved node rounding did not change the spike result");
            const auto deviation=wire_boundary_deviation(wire,body);
            std::cout<<"Saved nodes kind="<<static_cast<int>(kind)<<" mode="<<static_cast<int>(mode)
                <<" nodes="<<joints<<" ms="<<ms<<" deviation="<<deviation<<std::endl;
            require(deviation<1.e-5,"Saved pocket node wire differs from the calculated Body");
        }
    }
}
}
int main(){try{kernel::OcctKernel kernel;
    if(const auto* path=std::getenv("ZIMA_VERIFY_SAVED_TREATMENT")) {
        const auto* owner=std::getenv("ZIMA_VERIFY_SAVED_TREATMENT_OWNER");
        require(owner,"Saved treatment verification requires its owner");
        if(std::getenv("ZIMA_VERIFY_SAVED_NODES"))verify_saved_nodes(kernel,fs::u8path(path),owner);
        else verify_saved_treatment(kernel,fs::u8path(path),owner);
        return 0;
    }
    if(const auto* script=std::getenv("ZIMA_VERIFY_TREATMENT_PREVIEW_SCRIPT")) {
        verify_preview_recovery_script(kernel,fs::u8path(script));return 0;
    }
    const auto directory=fs::temp_directory_path()/("zima-edge-commands-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create test directory");verify(kernel,directory);verify_node_wires(kernel,directory);fs::remove_all(directory);
    std::cout<<"Edge treatment commands: radii, section distances, volumes, reference ownership, locks, rollback and native files passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
