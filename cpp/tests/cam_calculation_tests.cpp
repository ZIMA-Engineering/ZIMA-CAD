#include "profile_solid_fixture.hpp"
#include <zima/workspace/cam_calculation.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <cmath>
#include <iostream>

using namespace zima;
namespace {
unsigned assertions{};
void check(bool ok,const char* message){++assertions;if(!ok)throw std::runtime_error(message);}
document::BodyHistory setup(const workspace::BodyGraphEdit& edit,const std::string& stock,const std::string& target) {
    document::BodyHistory body;body.scope.id=edit.object_id;body.name="Face milling";body.cam.emplace();
    auto& d=*body.cam;d.stock_id=stock;d.target_id=target;d.machine.id="machine";d.machine.name="Three-axis mill";
    d.tools.push_back({"cutter","Flat cutter",{{-12,3},{0,3}},{{0,4},{20,4}},300,6000,true});
    d.operations.push_back(cam::Facing{edit.object_id+"-face","Top face","cutter",1,3,5,0});return body;
}
void volume(const document::CamCalculated& result,double remaining,double removed) {
    std::cout<<"Remaining="<<result.remaining->volume<<" expected="<<remaining<<" removed="<<result.removed_mm3
        <<" expected="<<removed<<" blocks="<<result.program.blocks.size()<<" collisions="<<result.collisions.size()<<'\n';
    std::cout<<"Frame "<<result.work_frame.rotation_x<<","<<result.work_frame.rotation_y<<","<<result.work_frame.rotation_z<<'\n';
    check(std::abs(result.remaining->volume-remaining)<1e-6,"Wrong independent remaining volume");
    check(std::abs(result.removed_mm3-removed)<1e-6,"Wrong independent removed volume");
    check(!document::cam_result_has_collisions(result),"Unexpected collision/gouge on face milling");
    check(result.remaining->solid_present&&!result.remaining->mesh.triangles.empty(),"Remaining stock lacks native solid/viewer geometry");
}
}
int main()try {
    auto part=document::PartDocument::create_default();auto stock_feature=test::rectangular_feature(part,{20,10,6});
    auto target_feature=test::rectangular_feature(part,{20,10,4});part.history={stock_feature,target_feature};
    document::BodyHistoryGraph graph;const auto stock=graph.create_body("Stock");graph.insert({document::PartHistoryKind::Feature,stock_feature.id});
    const auto target=graph.create_body("Design");graph.insert({document::PartHistoryKind::Feature,target_feature.id});graph.activate({});part.set_body_history(graph);
    kernel::OcctKernel kernel;auto boundaries=kernel.evaluate_history(part.kernel_operations());
    const auto original=document::serialize_body_result(boundaries.back().body_outputs.at(stock));
    workspace::Workspace live;live.add_part(part,std::move(boundaries));auto& session=live.open_part(part.document_id)->session;
    const auto calculations=kernel::body_calculation_count.load();
    auto edit=workspace::prepare_cam_edit(session.document());const auto first=edit.object_id;
    auto body=setup(edit,stock,target);body.cam->display_units=cam::Units::Inches;
    body.cam->work_frame.x=7;body.cam->work_frame.y=11;body.cam->work_frame.absolute_rotation_z=30;
    check(workspace::commit_cam(live,edit,body),"First setup creation failed");
    auto input=workspace::prepare_cam_calculation(live,part.document_id,first);
    const auto before=session.revision();std::size_t progress_count{};
    auto result=workspace::calculate_cam(input,[&](std::size_t done,std::size_t total){check(done<=total,"Invalid simulation progress");++progress_count;});
    volume(result,1000,200);
    const auto compiled=cam::compile(result.program,result.machine,result.tools,result.machine_work_origin);
    check(progress_count==compiled.motions.size()+1&&result.program.display_units==cam::Units::Inches,"Program/progress mapping lost");
    check(session.revision()==before,"Private calculation mutated the document");
    check(document::serialize_body_result(session.calculated_boundaries().back().body_outputs.at(stock))==original,"Calculation modified immutable source");
    check(workspace::publish_cam_calculation(live,input,result),"First result publication failed");
    edit=workspace::prepare_cam_edit(session.document());const auto second=edit.object_id;
    body=setup(edit,first,target);body.cam->work_frame.absolute_rotation_x=180;
    check(workspace::commit_cam(live,edit,body),"Second-side setup failed");
    input=workspace::prepare_cam_calculation(live,part.document_id,second);result=workspace::calculate_cam(input);volume(result,800,200);
    check(workspace::publish_cam_calculation(live,input,result),"Second-side result publication failed");
    check(workspace::cam_result_state(session,first)==workspace::CamResultState::Current&&
        workspace::cam_result_state(session,second)==workspace::CamResultState::Current,"Calculated setup chain not current");
    check(session.undo()&&!session.document().body_history.find(second)->cam->calculated,"Result Undo failed");
    check(session.redo()&&session.document().body_history.find(second)->cam->calculated->remaining->volume==result.remaining->volume,"Result Redo failed");
    const auto parent=std::filesystem::canonical(std::filesystem::temp_directory_path());
    const auto directory=parent/("zima-cam-calculation-"+part.document_id);std::filesystem::create_directory(directory);
    const auto file=directory/"two-setups.prtz";session.document().save(file,session.calculated_boundaries());
    std::vector<kernel::BodyResult> saved;const auto reopened=document::PartDocument::load(file,&saved);
    workspace::Workspace restored;restored.add_part(reopened,std::move(saved));const auto& restored_session=restored.open_part(part.document_id)->session;
    check(workspace::cam_result_state(restored_session,second)==workspace::CamResultState::Current,"Reopened remaining stock chain stale");
    check(document::serialize_body_result(restored_session.document().body_history.find(second)->cam->calculated->remaining)==
        document::serialize_body_result(result.remaining),"Native reopen lost exact remaining stock");
    // Exact retained material in document coordinates supports a fresh repeat in the flipped work frame.
    const auto repeat=workspace::calculate_cam(workspace::prepare_cam_calculation(restored,part.document_id,second));volume(repeat,800,200);
    input=workspace::prepare_cam_calculation(live,part.document_id,first);
    bool cancelled{};const auto revision=session.revision();
    try{static_cast<void>(workspace::calculate_cam(input,[](std::size_t done,std::size_t){if(done==1)throw std::runtime_error("cancel");}));}
    catch(const std::runtime_error&){cancelled=true;}
    check(cancelled&&session.revision()==revision,"Cancelled simulation committed partial material");
    edit=workspace::prepare_cam_edit(session.document(),first);body=*session.document().body_history.find(first);
    body.cam->clamping_sketch_ids={test::rectangular_sketch_id(stock_feature)};
    check(workspace::commit_cam(live,edit,body),"Clamping metadata setup failed");
    input=workspace::prepare_cam_calculation(live,part.document_id,first);
    const auto clamping_revision=session.revision();bool unsupported=false,progress_started=false;
    const std::string clamping_error="CAM clamping Sketch checks are not available yet. Use fixture Bodies for collision checking.";
    try{static_cast<void>(workspace::calculate_cam(input,[&](auto,auto){progress_started=true;}));}
    catch(const std::invalid_argument& error){unsupported=error.what()==clamping_error;}
    check(unsupported&&!progress_started&&session.revision()==clamping_revision,"Unsupported Sketch clamping was silently ignored or committed");
    auto unchecked=*session.document().body_history.find(first)->cam->calculated;
    unchecked.intent_key=input.intent_key;unchecked.sources=input.source_keys;unsupported=false;
    try{static_cast<void>(workspace::publish_cam_calculation(live,input,unchecked));}
    catch(const std::invalid_argument& error){unsupported=error.what()==clamping_error;}
    check(unsupported&&session.revision()==clamping_revision,"Unchecked Sketch clamping result was published");
    check(workspace::cam_result_state(session,first)==workspace::CamResultState::InvalidSource&&
        workspace::cam_result_state(session,second)==workspace::CamResultState::InvalidSource,"Unchecked clamping entered chained stock");
    edit=workspace::prepare_cam_edit(session.document(),first);body=*session.document().body_history.find(first);body.cam->fixture_ids={stock};
    body.cam->clamping_sketch_ids.clear();
    check(workspace::commit_cam(live,edit,body),"Fixture setup failed");
    input=workspace::prepare_cam_calculation(live,part.document_id,first);result=workspace::calculate_cam(input);
    check(!result.collisions.empty(),"Exact fixture collision was missed");
    check(workspace::publish_cam_calculation(live,input,result)&&workspace::cam_result_state(session,second)==workspace::CamResultState::Collision,
        "Collision result entered downstream stock");
    check(kernel::body_calculation_count.load()==calculations,"CAM regenerated authored design Bodies");
    check(std::filesystem::canonical(directory).parent_path()==parent,"Unexpected test directory");std::filesystem::remove_all(directory);
    std::cout<<"CAM calculation: "<<assertions<<" exact volume, frame, chaining, collision, persistence and publication assertions passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
