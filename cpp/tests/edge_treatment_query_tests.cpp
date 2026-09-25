#include "profile_command_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* command,Json args=Json::object()) {
    auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings settings;settings.templates={fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"};return settings;};
    command_host::Host host(live,kernel,directory,options);
    require(host.execute_text("edge_treatment.edges").code=="unsupported_document","Edge query without a Part failed incorrectly");
    run(host,"new",{{"type","part"},{"name","edge-queries"}});
    require(host.execute_text("edge_treatment.edges").code=="missing_input","Empty body exposed treatment input edges");
    const auto box=zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","10"},{"width_mm","10"},{"height_mm","10"}}).data.at("container").get<std::string>();
    auto* state=live.open_part(live.active_document_id());const auto body=state->session.document().body_history.active_body_id();
    const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    const auto listed=run(host,"edge_treatment.edges").data;
    require(listed.at("total")==12&&listed.at("body")==body&&listed.at("coordinate_system")=="body"&&listed.at("length_unit")=="mm","Edge query lost real input edges or their local frame");
    for(const auto& edge:listed.at("items")) {
        require(edge.at("owner")==box&&!edge.at("ambiguous").get<bool>()&&edge.at("segments").size()==1,"Box input edge has a wrong identity");
        const auto& segment=edge.at("segments")[0];require(segment.at("endpoints").size()==2&&std::abs(segment.at("length_mm").get<double>()-10)<1e-6,"Box input edge lost exact endpoints or length");
        for(const auto& endpoint:segment.at("endpoints")) {
            require(endpoint.at("owner")==box,"Endpoint lost its persisted source owner");
            for(const auto& value:endpoint.at("position_mm"))require(std::abs(value.get<double>())==5,"Endpoint is not in the expected Body-local frame");
        }
    }
    const auto first=listed.at("items")[0];const Json seed={{"owner",first.at("owner")},{"key",first.at("key")}};
    const auto route=run(host,"edge_treatment.route",{{"seed",seed}}).data;
    require(route.at("edges").size()==1&&route.at("edges")[0].at("key")==seed.at("key")&&route.at("endpoints").size()==2&&!route.at("closed").get<bool>(),"A square corner was included in a tangent route");
    const auto tail=run(host,"edge_treatment.edges",{{"offset",11},{"limit",2}}).data;
    require(tail.at("items").size()==1&&!tail.at("has_more").get<bool>()&&run(host,"edge_treatment.edges",{{"owner","missing"}}).data.at("total")==0,"Edge query ignored pagination or owner filter");
    for(const auto args:std::vector<Json>{{{"limit",0}},{{"offset",-1}},{{"limit",5001}},{{"container",box}}})
        require(!host.execute({{"command","edge_treatment.edges"},{"arguments",args}}).ok,"Invalid edge query accepted");
    for(const auto args:std::vector<Json>{{{"seed",seed},{"tolerance_degrees",91}},{{"seed",seed},{"tolerance_degrees",-1}},
        {{"seed",{{"owner",box},{"key","missing"}}}},{{"seed",{{"owner",box},{"key",seed.at("key")},{"instance_path","another"}}}},
        {{"seed",{{"owner",box},{"key",seed.at("key")},{"geometry_index",0}}}}})
        require(!host.execute({{"command","edge_treatment.route"},{"arguments",args}}).ok,"Invalid or ambiguous route request accepted");
    require(state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache,"Read-only edge queries changed the document or calculation cache");
    // Editing uses the input before the treatment, including its removed edge.
    auto next=state->session.document();auto fillet=document::PartDocument::create_fillet_container({{box,seed.at("key"),{}}});
    next.insert_history_entry(document::PartHistoryKind::Feature,fillet.id);next.history.push_back(fillet);
    auto calculated=kernel.evaluate_history(next.kernel_operations());state->session.commit(std::move(next),std::move(calculated));
    const auto input_edges=run(host,"edge_treatment.edges",{{"container",fillet.id}}).data;
    require(input_edges.at("total")==12&&std::ranges::all_of(input_edges.at("items"),[&](const auto& edge){return edge.at("owner")==box;}),"Treatment query read the final body instead of its rollback input");
    require(run(host,"edge_treatment.route",{{"container",fillet.id},{"seed",seed}}).data.at("edges").size()==1,"Treatment could not query its original removed edge");
    const auto result_edges=run(host,"edge_treatment.edges").data;
    require(std::ranges::any_of(result_edges.at("items"),[&](const auto& edge){return edge.at("owner")==fillet.id;}),"Insertion query omitted current treatment edges");
    const auto other=run(host,"body.create",{{"name","Other"}}).data.at("body");
    const auto other_box=zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","20"},{"width_mm","20"},{"height_mm","20"}}).data.at("container");
    const auto other_edges=run(host,"edge_treatment.edges").data;
    require(other_edges.at("body")==other&&std::ranges::all_of(other_edges.at("items"),[&](const auto& edge){return edge.at("owner")==other_box;}),"Edge query mixed separate bodies");
    require(run(host,"edge_treatment.edges",{{"container",fillet.id}}).data.at("body")==body,"Explicit read query changed the treatment's owning body");
    run(host,"body.activate",{{"body",body}});run(host,"body.cursor",{{"body",body},{"index",1}});
    require(run(host,"edge_treatment.edges").data.at("total")==12,"Insertion query ignored the active history cursor");
    run(host,"new",{{"type","part"},{"name","edge-circle"}});
    zima::test::circular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"radius_mm","5"},{"height_mm","20"}});
    const auto cylinder=run(host,"edge_treatment.edges").data;bool found_circle=false;
    for(const auto& item:cylinder.at("items")) {
        const auto& segment=item.at("segments")[0];const auto& ends=segment.at("endpoints");
        if(segment.at("point_count")>3&&ends.empty()) {
            const auto closed=run(host,"edge_treatment.route",{{"seed",{{"owner",item.at("owner")},{"key",item.at("key")}}}}).data;
            require(closed.at("edges").size()==1&&closed.at("closed").is_null()&&!closed.at("endpoints_complete").get<bool>()&&closed.at("endpoints").empty(),"Circular input route invented unpersisted endpoints");found_circle=true;
        }
    }
    require(found_circle,"Cylinder query lost its closed circular input edges");
}
}
int main(){try {
    kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());const auto directory=root/("zima-edge-queries-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create test directory");verify(kernel,directory);
    require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);
    std::cout<<"Treatment queries: exact source/endpoints, local frame, rollback, bodies, cursor, circle and read-only errors passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
