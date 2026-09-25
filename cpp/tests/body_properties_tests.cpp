#include "profile_command_fixture.hpp"
#include "profile_solid_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/body_properties_edits.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/stable_id.hpp>
#include <iostream>
using namespace zima;
using commands::Json;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void near(double value,double expected,const char* message){if(std::abs(value-expected)>1e-7*std::max(1.0,std::abs(expected)))throw std::runtime_error(std::string(message)+": "+std::to_string(value)+" != "+std::to_string(expected));}
Json run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",args}});if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.message);return result.data;
}
void geometry(const kernel::OcctKernel& kernel) {
    zima::test::ProfilePrism box{10,8,6};const auto bodies=kernel.evaluate_history({{"block",box}});const auto& b=bodies.back();
    check(b.volume_integrals.has_value(),"Kernel omitted volume integrals");const auto& p=*b.volume_integrals;
    near(b.volume,480,"block volume");near(p.centroid.x,5,"centroid x");near(p.centroid.y,4,"centroid y");near(p.centroid.z,3,"centroid z");
    near(p.inertia[0],4000,"Ixx");near(p.inertia[4],5440,"Iyy");near(p.inertia[8],6560,"Izz");near(p.inertia[1],0,"Ixy");
    const auto rotated=kernel::inertia_rotate(p.inertia,kernel::inertia_frame({0,0,90}),true);
    near(rotated[0],5440,"rotated Ixx");near(rotated[4],4000,"rotated Iyy");
    const auto packet=document::load_body_result(document::serialize_body_result(b));check(packet.volume_integrals==b.volume_integrals,"Native packet lost exact integrals");
    const auto mirror=kernel.mirror_body(b,{{0,0,0},{1,0,0}},"mirror");near(mirror.volume_integrals->centroid.x,-5,"mirror centroid");near(mirror.volume_integrals->inertia[4],5440,"mirror inertia");
    auto angled=box;angled.rotation_degrees={0,0,45};angled.translation={10,20,30};
    const auto rotated_body=kernel.evaluate_history({{"rotated",angled}}).back();
    near(rotated_body.volume_integrals->inertia[1],-720,"physical off-diagonal inertia");
    const auto reflected=kernel.mirror_body(rotated_body,{{0,0,0},{1,0,0}},"reflected");
    near(reflected.volume_integrals->inertia[1],720,"reflection changed tensor sign incorrectly");
    kernel::PatternRequest pattern;pattern.linear[0].count=3;pattern.linear[0].spacing=20;
    const auto copies=kernel.pattern_body(b,pattern,"copies");near(copies.volume,960,"pattern volume");
    near(copies.volume_integrals->centroid.x,35,"pattern centroid");near(copies.volume_integrals->inertia[4],2*5440+960*100,"pattern parallel axes");
    zima::test::ProfilePrism cutter{2,8,6};cutter.translation={8,0,0};
    const auto cut=kernel.evaluate_history({{"block",box},{"cut",cutter,kernel::BooleanOperation::Subtract}}).back();
    near(cut.volume,384,"cut volume");near(cut.volume_integrals->centroid.x,4,"cut centroid");near(cut.volume_integrals->inertia[8],384*(64+64)/12.,"cut inertia");
}
void workflow(const kernel::OcctKernel& kernel,std::filesystem::path dir) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[]{return command_host::Settings{{std::filesystem::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);
    run(host,"new",{{"type","part"},{"name","mass-model"}});const auto id=live.active_document_id();
    check(!host.execute({{"command","body_properties.create"},{"arguments",Json::object()}}).ok,"Empty model accepted body properties");
    const auto box=zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","10"},{"width_mm","8"},{"height_mm","6"}}).at("container").get<std::string>();
    auto* part=live.open_part(id);const auto body=part->session.document().body_history.active_body_id();
    auto doc=part->session.document();doc.physical_parameters["MASS_DENSITY"]="7850";doc.physical_parameter_units["MASS_DENSITY"]="kg/m^3";
    part->session.commit(doc,part->session.calculated_boundaries());
    const auto shape=part->session.calculated_boundaries().back().kernel_shape;
    auto row=run(host,"body_properties.create",{{"name","Before cut"}});const auto object=row.at("object").get<std::string>();
    check(row["body_id"]==body&&row["after_object_id"]==box,"Creation lost history position");near(row["volume_mm3"],480,"saved volume");
    const auto model_tree=command_host::model_tree(live,id,2000).at("items");
    check(std::ranges::any_of(model_tree,[&](const auto& item){return item.at("id")==object&&item.at("type")=="body-properties";}),"Model tree omitted the analysis feature");
    near(row["mass_kg"],.003768,"saved mass");near(row["inertia_kg_mm2"][0],.0314,"saved mass inertia");
    check(!row["body_calculated"].get<bool>()&&part->session.calculated_boundaries().back().kernel_shape==shape,"Properties recalculated a solid");
    const auto revision=part->session.revision();check(!run(host,"body_properties.set",{{"object",object}})["changed"].get<bool>(),"No-op created undo state");
    check(part->session.revision()==revision,"No-op changed revision");
    const auto rejected=host.execute({{"command","body_properties.set"},{"arguments",{{"object",object},{"rotation_degrees",{1,2}}}}});
    check(!rejected.ok&&part->session.revision()==revision,"Malformed edit changed history");
    run(host,"body_properties.set",{{"object",object},{"rotation_degrees",{0,0,90}}});
    row=run(host,"body_properties.get",{{"object",object}});near(row["inertia_kg_mm2"][0],.042704,"rotated saved tensor");
    run(host,"undo");row=run(host,"body_properties.get",{{"object",object}});near(row["rotation_degrees"][2],0,"undo rotation");run(host,"redo");
    const auto stale=workspace::prepare_body_properties_edit(live,id,object);
    const auto downstream=zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","2"},{"width_mm","2"},{"height_mm","2"}}).at("container").get<std::string>();
    row=run(host,"body_properties.get",{{"object",object}});near(row["volume_mm3"],480,"Downstream feature changed earlier measurement");
    try{static_cast<void>(workspace::commit_body_properties(live,stale,stale.initial));throw std::runtime_error("stale accepted");}
    catch(const workspace::MeasurementOperationError& e){check(std::string(e.code)=="stale_edit","Wrong stale error");}
    zima::test::resize_rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"container",box},{"length_mm","12"}});row=run(host,"body_properties.get",{{"object",object}});near(row["volume_mm3"],576,"Upstream change did not recalculate measurement");
    run(host,"body.cursor",{{"body",body},{"index",1}});
    auto middle=run(host,"body_properties.create",{{"name","At cursor"}});check(middle["after_object_id"]==box,"Creation used final history instead of cursor");
    const auto origins=document::body_properties_origins(part->session.document());check(origins.points.size()==2,"Historical COG origins missing");
    run(host,"body.activate");run(host,"body.cursor",{{"index",1}});
    const auto whole=run(host,"body_properties.create",{{"name","Whole part"}});check(whole["body_id"]==""&&whole["after_object_id"]==body,"Whole Part anchor incorrect");near(whole["volume_mm3"],576,"Whole Part wrong volume");
    run(host,"body.create",{{"name","Later body"}});zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","10"},{"width_mm","8"},{"height_mm","6"}});
    near(run(host,"body_properties.get",{{"object",whole.at("object")}})["volume_mm3"],576,"Later Body changed earlier whole-Part measurement");
    run(host,"body.activate");run(host,"body.cursor",{{"index",2}});
    const auto aggregate=run(host,"body_properties.create",{{"name","Both bodies"}});near(aggregate["volume_mm3"],1056,"Whole-Part aggregate ignored a Body");
    auto no_density=part->session.document();no_density.physical_parameters.erase("MASS_DENSITY");part->session.commit(no_density,part->session.calculated_boundaries());
    row=run(host,"body_properties.get",{{"object",object}});check(row["mass_kg"].is_null()&&row["inertia_kg_mm2"].is_null()&&!row["integrals"].is_null(),"Unknown density fabricated mass or removed geometry");
    run(host,"save");std::vector<kernel::BodyResult> restored;const auto loaded=document::PartDocument::load(dir/"mass-model.prtz",&restored);
    check(loaded.body_properties==part->session.document().body_properties,"Native document lost records");
    check(restored.back().volume_integrals.has_value(),"Native document lost kernel integrals");
    run(host,"body_properties.delete",{{"object",object}});run(host,"undo");check(run(host,"body_properties.get",{{"object",object}})["name"]=="Before cut","Delete Undo lost record");
    auto missing=part->session.document();missing.body_properties.front().after_object_id="missing";
    part->session.commit(missing,part->session.calculated_boundaries());row=run(host,"body_properties.get",{{"object",object}});
    check(!row["error"].get<std::string>().empty()&&row["integrals"].is_null(),"Missing anchor silently moved measurement to final body");
}
void placed_bodies(const kernel::OcctKernel& kernel) {
    auto doc=document::PartDocument::create_default();auto first=zima::test::rectangular_feature(doc,{10,8,6});
    auto second=zima::test::rectangular_feature(doc,{10,8,6});doc.history={first,second};
    document::BodyHistoryGraph graph;const auto a=graph.create_body("A");graph.insert({document::PartHistoryKind::Feature,first.id});
    auto def=*graph.find(a);def.scope.placement.x=100;def.scope.placement.y=200;def.scope.placement.z=300;def.scope.placement.rotation_z=90;graph.update_body(def);
    const auto b=graph.create_body("B");graph.insert({document::PartHistoryKind::Feature,second.id});
    def=*graph.find(b);def.scope.placement.x=120;def.scope.placement.y=200;def.scope.placement.z=300;def.scope.placement.rotation_z=90;graph.update_body(def);
    doc.set_body_history(graph);const auto calculated=kernel.evaluate_history(doc.kernel_operations());
    document::BodyProperties row;row.id="measurement";row.name="Placed";row.body_id=a;row.after_object_id=first.id;
    row=document::evaluate_body_properties(doc,calculated,row);check(row.integrals.has_value(),"Placed Body measurement unavailable");
    near(row.integrals->centroid.x,100,"placed centroid X");near(row.integrals->centroid.y,200,"placed centroid Y");near(row.integrals->centroid.z,300,"placed centroid Z");
    near(row.integrals->inertia[0],5440,"placed Body tensor X");near(row.integrals->inertia[4],4000,"placed Body tensor Y");
    row.body_id.clear();row.after_object_id=b;row=document::evaluate_body_properties(doc,calculated,row);
    near(row.volume,960,"placed aggregate volume");near(row.integrals->centroid.x,110,"placed aggregate centroid");
    near(row.integrals->inertia[4],104000,"aggregate parallel-axis inertia");
    near(calculated.back().volume_integrals->centroid.x,110,"kernel aggregate centroid");
    near(calculated.back().volume_integrals->inertia[4],104000,"kernel aggregate tensor");
}
void surfaces(const kernel::OcctKernel& kernel) {
    auto doc=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
    static_cast<void>(sketch.add_segment(0,0,10,0));
    static_cast<void>(sketch.add_segment(10,0,10,20));
    auto feature=document::PartDocument::create_extrusion_container(sketch.id);sketch.owner_container_id=feature.id;
    feature.extrusion.result_type=document::ProfileResultType::Surface;
    feature.extrusion.height=feature.extrusion.length_forward=10;
    doc.history={feature};doc.sketches={sketch};
    document::BodyHistoryGraph graph;const auto body=graph.create_body("Open surfaces");
    graph.insert({document::PartHistoryKind::Feature,feature.id});
    auto definition=*graph.find(body);definition.scope.placement.x=100;definition.scope.placement.rotation_z=90;
    graph.update_body(definition);doc.set_body_history(graph);
    doc.physical_parameters["MASS_DENSITY"]="0.00000785";
    const auto calculated=kernel.evaluate_history(doc.kernel_operations());
    document::BodyProperties row;row.id="area-center";row.name="Surface center";row.body_id=body;row.after_object_id=feature.id;
    row=document::evaluate_body_properties(doc,calculated,row);
    check(row.error.empty()&&row.surface_centroid&&!row.integrals,"Open surfaces did not offer an area centroid");
    near(row.volume,0,"Surface fabricated volume");near(row.area,300,"Surface area");
    near(row.surface_centroid->x,100-20./3,"Area-weighted rotated X");
    near(row.surface_centroid->y,25./3,"Area-weighted rotated Y");near(row.surface_centroid->z,5,"Surface Z");
    check(!document::body_properties_origin(row).points.empty(),"Surface centroid has no displayed Origin");
    check(document::parse_body_properties(document::serialize_body_properties({row})).front()==row,"Surface measurement persistence");
    const auto restored=document::load_body_result(document::serialize_body_result(calculated.back()));
    check(restored.surface_centroid==calculated.back().surface_centroid,"Surface centroid lost in calculation cache");
    const auto mirror=kernel.mirror_body(restored,{{0,0,0},{1,0,0}},"surface-mirror");
    near(mirror.surface_centroid->x,-restored.surface_centroid->x,"Mirrored surface centroid");
    kernel::PatternRequest pattern;pattern.linear[0].count=3;pattern.linear[0].spacing=20;
    const auto copies=kernel.pattern_body(restored,pattern,"surface-copies");
    near(copies.surface_centroid->x,restored.surface_centroid->x+30,"Pattern surface centroid");
}
}
int main(){try {
    kernel::OcctKernel kernel;geometry(kernel);placed_bodies(kernel);surfaces(kernel);
    const auto parent=std::filesystem::canonical(std::filesystem::temp_directory_path());const auto dir=parent/("zima-body-properties-"+kernel::make_stable_id());
    std::filesystem::create_directory(dir);workflow(kernel,dir);
    check(std::filesystem::canonical(dir).parent_path()==parent,"Invalid test cleanup path");std::filesystem::remove_all(dir);
    std::cout<<"Body properties analytical, history, transaction and persistence checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
