#include "profile_solid_fixture.hpp"
#include <zima/workspace/cam_calculation.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <cmath>
#include <iostream>
#include <numbers>

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
void profile_lifecycle(){
    auto part=document::PartDocument::create_default();auto stock_feature=test::rectangular_feature(part,{20,10,6});
    auto target_feature=test::rectangular_feature(part,{20,10,4});part.history={stock_feature,target_feature};
    auto profile=sketcher::Sketch::create_default();static_cast<void>(profile.add_circle(0,0,3));
    auto owner=document::PartDocument::create_sketch_container();profile.owner_container_id=owner.id;part.sketches.push_back(profile);part.history.push_back(owner);
    document::BodyHistoryGraph graph;const auto stock=graph.create_body("Stock");graph.insert({document::PartHistoryKind::Feature,stock_feature.id});
    const auto target=graph.create_body("Target");graph.insert({document::PartHistoryKind::Feature,target_feature.id});
    const auto profiles=graph.create_body("Profiles");graph.insert({document::PartHistoryKind::Feature,owner.id});
    auto profile_body=*graph.find(profiles);profile_body.scope.placement.x=3;profile_body.scope.placement.y=1;
    profile_body.scope.placement.rotation_z=profile_body.scope.placement.absolute_rotation_z=30;graph.update_body(profile_body);graph.activate({});part.set_body_history(graph);
    kernel::OcctKernel kernel;auto boundaries=kernel.evaluate_history(part.kernel_operations());workspace::Workspace live;live.add_part(part,std::move(boundaries));
    auto& session=live.open_part(part.document_id)->session;const auto count=kernel::body_calculation_count.load();
    auto edit=workspace::prepare_cam_edit(session.document());const auto id=edit.object_id;auto body=setup(edit,stock,target);
    body.cam->work_frame.x=3;body.cam->work_frame.y=1;body.cam->work_frame.absolute_rotation_z=30;
    body.cam->tools.front().cutting={{-10,1},{0,1}};body.cam->tools.front().non_cutting={{0,1},{20,1}};
    cam::ProfileMilling op;op.id="pocket";op.name="Circular pocket";op.tool_id="cutter";op.sketch_id=profile.id;op.top_mm=3;op.bottom_mm=2;
    body.cam->operations={op};check(workspace::commit_cam(live,edit,body),"Profile setup failed");
    auto input=workspace::prepare_cam_calculation(live,part.document_id,id);const auto revision=session.revision();
    bool cancelled=false;try{static_cast<void>(workspace::calculate_cam(input,[](auto,auto total){if(total==0)throw std::runtime_error("cancel path");}));}catch(const std::runtime_error&){cancelled=true;}
    check(cancelled&&session.revision()==revision,"Path cancellation published partial data");
    auto result=workspace::calculate_cam(input);volume(result,1200-9*std::numbers::pi,9*std::numbers::pi);
    check(workspace::publish_cam_calculation(live,input,result),"Profile result publication failed");
    check(workspace::cam_result_state(session,id)==workspace::CamResultState::Current,"Profile result not current");
    check(session.undo()&&!session.document().body_history.find(id)->cam->calculated,"Profile result Undo failed");
    check(session.redo()&&workspace::cam_result_state(session,id)==workspace::CamResultState::Current,"Profile result Redo failed");
    const auto parent=std::filesystem::canonical(std::filesystem::temp_directory_path());const auto directory=parent/("zima-cam-profile-"+part.document_id);
    std::filesystem::create_directory(directory);const auto path=directory/"profile.prtz";session.document().save(path,session.calculated_boundaries());
    std::vector<kernel::BodyResult> saved;auto reopened=document::PartDocument::load(path,&saved);workspace::Workspace restored;restored.add_part(reopened,std::move(saved));
    const auto& restored_session=restored.open_part(part.document_id)->session;
    check(workspace::cam_result_state(restored_session,id)==workspace::CamResultState::Current,"Profile result stale after native reopen");
    volume(workspace::calculate_cam(workspace::prepare_cam_calculation(restored,part.document_id,id)),1200-9*std::numbers::pi,9*std::numbers::pi);
    auto chain_edit=workspace::prepare_cam_edit(restored_session.document());const auto chained=chain_edit.object_id;
    check(workspace::commit_cam(restored,chain_edit,setup(chain_edit,id,target)),"Profile remaining stock could not feed a later setup");
    const auto chain_input=workspace::prepare_cam_calculation(restored,part.document_id,chained);auto chain_result=workspace::calculate_cam(chain_input);
    volume(chain_result,1000,200-9*std::numbers::pi);check(workspace::publish_cam_calculation(restored,chain_input,chain_result),"Profile stock chain publication failed");
    auto& chain_session=restored.open_part(part.document_id)->session;auto chain_changed=chain_session.document();
    std::ranges::find(chain_changed.sketches,profile.id,&sketcher::Sketch::id)->circles.front().radius=2.5;chain_session.commit_metadata(chain_changed);
    check(workspace::cam_result_state(chain_session,chained)==workspace::CamResultState::Stale,"Profile source edit did not invalidate downstream stock");
    check(chain_session.undo()&&workspace::cam_result_state(chain_session,chained)==workspace::CamResultState::Current,"Profile source Undo did not restore downstream stock");
    edit=workspace::prepare_cam_edit(session.document(),id);const auto before=session.revision();
    check(!workspace::commit_cam(live,edit,*session.document().body_history.find(id))&&session.revision()==before,"Unchanged profile OK added Undo");
    input=workspace::prepare_cam_calculation(live,part.document_id,id);
    auto changed=session.document();auto found=std::ranges::find(changed.sketches,profile.id,&sketcher::Sketch::id);found->circles.front().radius=2.5;session.commit_metadata(changed);
    check(workspace::cam_result_state(session,id)==workspace::CamResultState::Stale,"Changed exact profile reused old paths");
    check(!workspace::publish_cam_calculation(live,input,result),"Changed profile accepted an old calculation");
    volume(workspace::calculate_cam(workspace::prepare_cam_calculation(live,part.document_id,id)),1200-6.25*std::numbers::pi,6.25*std::numbers::pi);
    check(session.undo()&&workspace::cam_result_state(session,id)==workspace::CamResultState::Current,"Profile source Undo lost result");
    changed=session.document();std::ranges::find(changed.sketches,profile.id,&sketcher::Sketch::id)->circles.front().radius=1;session.commit_metadata(changed);
    volume(workspace::calculate_cam(workspace::prepare_cam_calculation(live,part.document_id,id)),1200-std::numbers::pi,std::numbers::pi);
    check(session.undo(),"Exactly fitting pocket Undo failed");
    changed=session.document();std::ranges::find(changed.sketches,profile.id,&sketcher::Sketch::id)->suppressed=true;session.commit_metadata(changed);
    check(workspace::cam_result_state(session,id)==workspace::CamResultState::SuppressedSource,"Suppressed profile remained available");
    check(session.undo(),"Profile suppression Undo failed");
    changed=session.document();auto moved=changed.body_history;auto moved_body=*moved.find(profiles);moved_body.scope.placement.x=2.5;moved.update_body(moved_body);changed.set_body_history(moved);session.commit_metadata(changed);
    check(workspace::cam_result_state(session,id)==workspace::CamResultState::Stale,"Profile Body frame change reused old paths");
    check(session.undo()&&workspace::cam_result_state(session,id)==workspace::CamResultState::Current,"Profile Body frame Undo lost result");
    changed=session.document();std::erase_if(changed.sketches,[&](const auto& s){return s.id==profile.id;});session.commit_metadata(changed);
    check(workspace::cam_result_state(session,id)==workspace::CamResultState::MissingSource,"Missing profile remained available");
    check(std::get<cam::ProfileMilling>(session.document().body_history.find(id)->cam->operations.front()).sketch_id==profile.id,"Missing profile identity was discarded");
    check(session.undo()&&workspace::cam_result_state(session,id)==workspace::CamResultState::Current,"Missing profile Undo lost result");
    edit=workspace::prepare_cam_edit(session.document(),id);body=*session.document().body_history.find(id);std::get<cam::ProfileMilling>(body.cam->operations.front()).bottom_mm=1;
    check(workspace::commit_cam(live,edit,body),"Profile depth edit failed");
    const auto gouge=workspace::calculate_cam(workspace::prepare_cam_calculation(live,part.document_id,id));
    check(std::abs(gouge.removed_mm3-18*std::numbers::pi)<1e-6&&std::ranges::any_of(gouge.collisions,[](const auto& c){return c.kind==cam::CollisionKind::TargetGouge;}),"Profile target gouge missed");
    check(session.undo(),"Profile depth Undo failed");
    edit=workspace::prepare_cam_edit(session.document(),id);body=*session.document().body_history.find(id);body.cam->fixture_ids={stock};
    check(workspace::commit_cam(live,edit,body),"Profile fixture edit failed");
    check(!workspace::calculate_cam(workspace::prepare_cam_calculation(live,part.document_id,id)).collisions.empty(),"Profile fixture collision missed");
    check(kernel::body_calculation_count.load()==count,"CAM profile lifecycle regenerated design geometry");
    check(std::filesystem::canonical(directory).parent_path()==parent,"Unexpected profile test directory");std::filesystem::remove_all(directory);
}
}
int main()try {
    profile_lifecycle();
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
    const auto clamp_sketch=test::rectangular_sketch_id(stock_feature);
    body.cam->clampings={{"clamping","Clamping",clamp_sketch,clamp_sketch,false,true}};
    check(workspace::commit_cam(live,edit,body),"Clamping metadata setup failed");
    input=workspace::prepare_cam_calculation(live,part.document_id,first);
    const auto clamping_revision=session.revision();bool rejected=false;
    const auto clamped=workspace::calculate_cam(input);volume(clamped,1000,200);
    check(clamped.contacts.size()==1&&std::abs(clamped.contacts.front().remaining_mm2-200)<1e-6&&clamped.warnings.empty(),"Clamping contact area/frame is incorrect");
    check(session.revision()==clamping_revision,"Clamping calculation committed a private draft");
    auto unchecked=*session.document().body_history.find(first)->cam->calculated;
    unchecked.intent_key=input.intent_key;unchecked.sources=input.source_keys;
    try{static_cast<void>(workspace::publish_cam_calculation(live,input,unchecked));}
    catch(const std::exception&){rejected=true;}
    check(rejected&&session.revision()==clamping_revision,"Unchecked Sketch clamping result was published");
    check(workspace::cam_result_state(session,first)==workspace::CamResultState::Stale&&
        workspace::cam_result_state(session,second)==workspace::CamResultState::Stale,"Unchecked clamping entered chained stock");
    check(workspace::publish_cam_calculation(live,input,clamped),"Checked clamping failed publication");
    session.document().save(file,session.calculated_boundaries());
    const auto clamped_reopen=document::PartDocument::load(file);
    check(*clamped_reopen.body_history.find(first)->cam==*session.document().body_history.find(first)->cam,"Clamping sides/report lost on reopen");
    edit=workspace::prepare_cam_edit(session.document(),first);body=*session.document().body_history.find(first);body.cam->clampings.front().outward_positive=true;
    check(workspace::commit_cam(live,edit,body),"Clamping side edit failed");
    auto flipped=workspace::calculate_cam(workspace::prepare_cam_calculation(live,part.document_id,first));
    check(std::ranges::any_of(flipped.collisions,[](const auto& c){return c.kind==cam::CollisionKind::CuttingClamping;}),"Reversed clamping side did not change collision result");
    check(session.undo()&&!session.document().body_history.find(first)->cam->clampings.front().outward_positive,"Clamping side Undo failed");
    check(session.redo()&&session.document().body_history.find(first)->cam->clampings.front().outward_positive,"Clamping side Redo failed");
    edit=workspace::prepare_cam_edit(session.document(),first);body=*session.document().body_history.find(first);body.cam->fixture_ids={stock};
    body.cam->clampings.clear();
    check(workspace::commit_cam(live,edit,body),"Fixture setup failed");
    input=workspace::prepare_cam_calculation(live,part.document_id,first);result=workspace::calculate_cam(input);
    check(!result.collisions.empty(),"Exact fixture collision was missed");
    check(workspace::publish_cam_calculation(live,input,result)&&workspace::cam_result_state(session,second)==workspace::CamResultState::Collision,
        "Collision result entered downstream stock");
    check(kernel::body_calculation_count.load()==calculations,"CAM regenerated authored design Bodies");
    check(std::filesystem::canonical(directory).parent_path()==parent,"Unexpected test directory");std::filesystem::remove_all(directory);
    std::cout<<"CAM calculation: "<<assertions<<" exact volume, frame, chaining, collision, persistence and publication assertions passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
