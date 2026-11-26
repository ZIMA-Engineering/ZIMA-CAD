#include "profile_solid_fixture.hpp"
#include <zima/document/document_session.hpp>
#include <zima/document/cam_library.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <limits>

using namespace zima;
namespace {
unsigned assertions{};
void check(bool ok,const char* message){++assertions;if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F action) {
    bool rejected{};try{action();}catch(const std::exception&){rejected=true;}
    check(rejected,"Invalid CAM document input was accepted");
}
document::BodyHistory setup(const std::string& stock,const std::string& target,const std::string& prefix="one") {
    document::BodyHistory b;b.name="CAM setup";b.cam.emplace();auto& d=*b.cam;
    d.stock_id=stock;d.target_id=target;d.machine.id="machine";d.machine.name="Three-axis mill";
    d.tools.push_back({"tool","Cutter",{{-20,3},{0,3}},{{0,4},{40,4}},300,6000,true});
    document::CamManualOperation operation;operation.id=prefix+"-manual";operation.name="Authored path";
    for(const auto kind:{cam::BlockKind::Tool,cam::BlockKind::Spindle,cam::BlockKind::Feed}) {
        cam::Block block;block.operation_id=operation.id;block.id=prefix+"-block-"+std::to_string(operation.program.blocks.size());
        block.kind=kind;block.tool_id="tool";block.value=kind==cam::BlockKind::Spindle?6000:300;
        operation.program.blocks.push_back(block);
    }
    d.operations.push_back(operation);return b;
}
void verify_definition() {
    auto d=*setup("stock","target").cam;
    d.fixture_ids={"vise"};d.clampings={{"clamping","Clamping","contact","upper",false,true}};d.display_units=cam::Units::Inches;
    cam::Facing face;face.id="face";face.name="Face milling";face.tool_id="tool";
    face.step_down_mm=1;face.step_over_mm=2;face.clearance_mm=5;face.allowance_mm=.25;
    d.operations.push_back(face);
    cam::ProfileMilling pocket;pocket.id="pocket";pocket.name="Pocket";pocket.tool_id="tool";pocket.sketch_id="profile";
    pocket.top_mm=3.123456789;pocket.bottom_mm=-2.123456789;pocket.radial_allowance_mm=.125;
    d.operations.push_back(pocket);auto contour=pocket;contour.id="contour";contour.kind=cam::ProfileMillingKind::Contour;
    contour.side=cam::ContourSide::Outside;contour.clockwise=true;d.operations.push_back(contour);
    check(document::cam_input_sketch_ids(d)==std::vector<std::string>{"contact","upper","profile"},"Profile input IDs are not deduplicated");
    const nlohmann::json encoded=d;
    check(encoded.at("schema")==3,"CAM schema was not advanced");
    check(encoded.get<document::CamDefinition>()==d,"CAM definition did not round trip");
    auto old=encoded;old.erase("schema");rejects([&]{static_cast<void>(old.get<document::CamDefinition>());});
    old=encoded;old["schema"]=1;rejects([&]{static_cast<void>(old.get<document::CamDefinition>());});
    old=encoded;old["schema"]=2;rejects([&]{static_cast<void>(old.get<document::CamDefinition>());});
    old=encoded;old["clampings"][0].erase("outward_positive");rejects([&]{static_cast<void>(old.get<document::CamDefinition>());});
    const auto key=document::cam_intent_key(d);
    check(document::cam_intent_key(encoded.get<document::CamDefinition>())==key,"CAM fingerprint changed on reopen");
    auto changed=d;changed.tools[0].cutting[0].radius_mm=2;
    check(document::cam_intent_key(changed)!=key,"Changed tool did not invalidate CAM intent");
    changed=d;std::swap(changed.operations[0],changed.operations[1]);
    check(document::cam_intent_key(changed)!=key,"Operation order did not invalidate CAM intent");
    changed=d;changed.work_frame.x=7;
    check(document::cam_intent_key(changed)!=key,"Work frame did not invalidate CAM intent");
    changed=d;std::get<cam::ProfileMilling>(changed.operations[2]).sketch_id="replacement";
    check(document::cam_intent_key(changed)!=key,"Profile replacement did not invalidate CAM intent");
    changed=d;std::get<cam::ProfileMilling>(changed.operations[3]).side=cam::ContourSide::Inside;
    check(document::cam_intent_key(changed)!=key,"Contour side did not invalidate CAM intent");
    changed=d;changed.tolerance_mm=std::numeric_limits<double>::infinity();
    rejects([&]{document::validate_cam_definition(changed);});
    changed=d;changed.tools.push_back(changed.tools.front());rejects([&]{document::validate_cam_definition(changed);});
    changed=d;changed.operations.push_back(changed.operations.front());rejects([&]{document::validate_cam_definition(changed);});
    auto broken=encoded;broken["operations"][0]["program"]["blocks"][0]["kind"]="unknown";
    changed=d;rejects([&]{document::from_json(broken,changed);});check(changed==d,"Invalid load partially changed CAM");
    broken=encoded;broken["machine"]["axes"]=3.5;rejects([&]{static_cast<void>(broken.get<document::CamDefinition>());});
    broken=encoded;broken["tools"][0]["cutting"][0]=nlohmann::json::array({-20});
    rejects([&]{static_cast<void>(broken.get<document::CamDefinition>());});
    // Deleting a tool leaves a repairable authored program, not lost operations.
    changed=d;changed.tools.clear();document::validate_cam_definition(changed);
    check(nlohmann::json(changed).get<document::CamDefinition>()==changed,"Missing tool discarded authored paths");
}
void verify_graph() {
    document::BodyHistoryGraph graph;
    const auto stock=graph.create_body("Stock");graph.insert({document::PartHistoryKind::Feature,"stock-feature"});
    const auto target=graph.create_body("Target");graph.insert({document::PartHistoryKind::Feature,"target-feature"});
    const auto fixture=graph.create_body("Fixture");
    const auto before=graph;auto value=setup(stock,target);value.cam->fixture_ids={fixture};
    const auto first=graph.create_cam(value);
    check(graph.find(first)->kind()==document::BodyKind::Cam&&graph.active_body_id()==first,"CAM kind/activation lost");
    check(graph.visible_context()==std::vector<std::string>{stock,target,fixture},"CAM lost source context");
    check(graph.available_before(graph.order().size())==before.available_before(before.order().size()),"CAM entered material operands");
    const auto ordinary=nlohmann::json::parse(before.serialized()),extended=nlohmann::json::parse(graph.serialized());
    for(std::size_t i=0;i<ordinary["bodies"].size();++i)
        check(ordinary["bodies"][i]==extended["bodies"][i],"CAM changed ordinary Body storage");
    const auto second=graph.create_cam(setup(first,target,"two"));
    check(graph.cam_sources(second)==std::vector<std::string>{first,target},"Chained CAM stock was rebound");
    check(document::BodyHistoryGraph::from_serialized(graph.serialized())==graph,"CAM graph failed native round trip");
    const auto unchanged=graph;
    value=*graph.find(first);value.cam->stock_id=second;rejects([&]{graph.update_body(value);});
    rejects([&]{graph.move_step(second,0);});
    rejects([&]{graph.insert({document::PartHistoryKind::Feature,"unexpected"});});
    rejects([&]{static_cast<void>(graph.create_boolean("Invalid",kernel::BodyCombination::Add,stock,first));});
    rejects([&]{static_cast<void>(graph.create_cam(setup(stock,first,"three")));});
    value=*graph.find(second);value.cam->stock_id="one-manual";rejects([&]{graph.update_body(value);});
    check(graph==unchanged,"Rejected CAM dependency edit changed the graph");
    graph.activate({});auto suppressed=*graph.find(first);suppressed.suppressed=true;graph.update_body(suppressed);
    check(graph.cam_source_state(second)==document::CamSourceState::Suppressed,"Suppressed CAM stock remained available");
    graph.erase_step(first);
    check(graph.cam_source_state(second)==document::CamSourceState::Missing&&graph.find(second)->cam->stock_id==first,
        "Missing CAM stock was discarded or silently rebound");
    check(document::BodyHistoryGraph::from_serialized(graph.serialized())==graph,"Broken CAM chain did not persist");
    value=*graph.find(second);value.cam->stock_id=stock;graph.update_body(value);
    check(graph.cam_source_state(second)==document::CamSourceState::Available,"CAM stock replacement failed");
}
void verify_native() {
    auto part=document::PartDocument::create_default();
    const auto feature=test::rectangular_feature(part,{20,10,5});part.history={feature};
    document::BodyHistoryGraph graph;const auto stock=graph.create_body("Stock");
    graph.insert({document::PartHistoryKind::Feature,feature.id});graph.activate({});part.set_body_history(graph);
    kernel::OcctKernel kernel;auto calculated=kernel.evaluate_history(part.kernel_operations());
    const auto snapshot=calculated.back().body_outputs.at(stock);
    document::DocumentSession session(part,std::move(calculated));const auto* boundaries=&session.calculated_boundaries();
    const auto count=kernel::body_calculation_count.load();
    auto body=setup(stock,stock);auto& d=*body.cam;auto result=std::make_shared<document::CamCalculated>();
    result->id="result";result->intent_key=document::cam_intent_key(d);result->sources={{stock,snapshot->source_fingerprint}};
    result->program=std::get<document::CamManualOperation>(d.operations.front()).program;
    result->machine=d.machine;result->tools=d.tools;result->remaining=snapshot;d.calculated=result;
    document::validate_cam_calculated(*result);
    check(document::cam_result_matches_definition(d)&&!document::cam_result_has_collisions(*result),"Valid retained CAM result rejected");
    const auto id=graph.create_cam(body);auto next=part;next.set_body_history(graph);session.commit_metadata(next);
    check(&session.calculated_boundaries()==boundaries&&session.document().body_history.find(id)->cam->calculated==result,
        "CAM metadata copied calculated geometry");
    check(session.undo()&&session.document().body_history==part.body_history,"CAM Undo failed");
    check(session.redo()&&session.document().body_history==graph,"CAM Redo failed");
    const auto parent=std::filesystem::canonical(std::filesystem::temp_directory_path());
    const auto directory=parent/("zima-cam-document-"+part.document_id);std::filesystem::create_directory(directory);
    const auto file=directory/"setup.prtz";session.document().save(file,session.calculated_boundaries());
    std::vector<kernel::BodyResult> restored;auto reopened=document::PartDocument::load(file,&restored);
    check(reopened.body_history==graph&&!restored.empty(),"Native CAM Part failed to reopen");
    const auto& saved=*reopened.body_history.find(id)->cam;
    check(document::serialize_body_result(saved.calculated->remaining)==document::serialize_body_result(snapshot),
        "Native CAM result lost exact geometry or references");
    check(restored.back().body_outputs.size()==1&&!restored.back().body_outputs.contains(id),"CAM duplicated design material");
    body=*graph.find(id);body.cam->work_frame.x=12;body.cam->work_frame.rotation_z=30;
    body.cam->work_frame.references.push_back({{},"missing-origin","origin:point"});
    graph.update_body(body);reopened.set_body_history(graph);reopened.resolve_constructions();
    const auto& missing=*reopened.body_history.find(id)->cam;
    check(!missing.work_frame.reference_valid&&missing.work_frame.x==12&&missing.work_frame.rotation_z==30,
        "Missing CAM placement reference moved the last valid frame");
    check(missing.calculated&&!document::cam_result_matches_definition(missing),"Stale CAM result was discarded or remained current");
    reopened.save(file,restored);const auto broken=document::PartDocument::load(file);
    check(broken.body_history==reopened.body_history,"Broken CAM frame/result did not persist");
    check(kernel::body_calculation_count.load()==count,"CAM metadata, persistence or Undo/Redo regenerated CAD geometry");
    check(std::filesystem::canonical(directory).parent_path()==parent,"Unexpected CAM test directory");
    std::filesystem::remove_all(directory);
}
void verify_library(){
    const auto root=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/lib/CAM";
    const auto count=kernel::body_calculation_count.load();
    auto source=document::PartDocument::load(root/"Tools/Milling/End_mill_10.toolz");
    auto tool=document::cam_library_tool(source);const auto again=document::cam_library_tool(source);
    check(tool.id!=again.id&&tool.cutting==std::vector<cam::ToolStation>{{-22,5},{0,5}}&&tool.non_cutting==std::vector<cam::ToolStation>{{0,5},{50,5}},"Library tool identity or explicit profiles differ");
    check(tool.feed_mm_min==2663&&tool.spindle_rpm==7767,"Library cutting preset was lost");
    source.user_parameters["cam_diameter_mm"]="2 * 6";source.name="Authored variant";
    tool=document::cam_library_tool(source);check(tool.name==source.name&&tool.cutting.front().radius_mm==6,"Explicit CAM arithmetic or authored name was lost");
    source.user_parameters.erase("cam_feed_mm_min");rejects([&]{static_cast<void>(document::cam_library_tool(source));});
    source=document::PartDocument::load(root/"Machines/Milling/Three_axis_mill.prtz");
    auto machine=document::cam_library_machine(source);
    check(machine.axes==3&&machine.maximum_feed_mm_min==10000&&machine.maximum_spindle_rpm==12000&&machine.rapid_mm_min==20000&&machine.postprocessor_id=="generic", "Machine library capabilities differ");
    source.user_parameters["cam_machine_axes"]="3.5";rejects([&]{static_cast<void>(document::cam_library_machine(source));});
    check(kernel::body_calculation_count.load()==count,"Library definition import regenerated geometry");
}
}
int main()try {
    verify_definition();verify_graph();verify_native();verify_library();
    std::cout<<"CAM document: "<<assertions<<" serialization, dependency, result, last-frame and Undo/Redo assertions passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
