#include "profile_solid_fixture.hpp"
#include <zima/workspace/cam_operations.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <iostream>

using namespace zima;
namespace {
unsigned assertions{};
void check(bool value,const char* message){++assertions;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F f){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid CAM edit accepted");}
document::BodyHistory setup(const workspace::BodyGraphEdit& edit,const std::string& stock,const std::string& target) {
    document::BodyHistory b;b.scope.id=edit.object_id;b.name="Setup";b.cam.emplace();
    auto& d=*b.cam;d.stock_id=stock;d.target_id=target;d.machine.id="mill";d.machine.name="Mill";
    d.tools.push_back({"tool","Flat cutter",{{-20,3},{0,3}},{{0,4},{30,4}},300,6000,true});
    document::CamManualOperation manual;manual.id=b.scope.id+"-manual";manual.name="Manual";
    cam::Block block;block.id=manual.id+"-tool";block.operation_id=manual.id;block.kind=cam::BlockKind::Tool;block.tool_id="tool";
    manual.program.blocks.push_back(block);d.operations.push_back(manual);return b;
}
document::CamCalculated result(const workspace::CamCalculationInput& input,const std::string& id) {
    document::CamCalculated r;r.id=id;r.intent_key=input.intent_key;r.sources=input.source_keys;
    r.machine=input.definition.machine;r.machine_work_origin=input.definition.machine_work_origin;r.tools=input.definition.tools;
    r.work_frame=input.definition.work_frame;r.tolerance_mm=input.definition.tolerance_mm;
    r.program=std::get<document::CamManualOperation>(input.definition.operations.front()).program;
    r.remaining=input.material.front().second;return r;
}
}
int main()try {
    auto part=document::PartDocument::create_default();auto feature=test::rectangular_feature(part,{20,10,5});part.history={feature};
    document::BodyHistoryGraph graph;const auto stock=graph.create_body("Stock");
    graph.insert({document::PartHistoryKind::Feature,feature.id});graph.activate({});part.set_body_history(graph);
    kernel::OcctKernel kernel;auto calculated=kernel.evaluate_history(part.kernel_operations());
    workspace::Workspace live;live.add_part(part,std::move(calculated));auto& session=live.open_part(part.document_id)->session;
    const auto count=kernel::body_calculation_count.load();const auto* boundaries=&session.calculated_boundaries();
    auto edit=workspace::prepare_cam_edit(session.document());const auto first=edit.object_id;
    check(workspace::cam_source_candidates(session,edit,true)==std::vector<std::string>{stock},"Invalid initial CAM candidates");
    check(!session.can_undo(),"Draft changed Undo");auto body=setup(edit,stock,stock);
    check(workspace::commit_cam(live,edit,body),"CAM creation failed");
    check(workspace::cam_result_state(session,first)==workspace::CamResultState::NotCalculated,"New CAM falsely calculated");
    edit=workspace::prepare_cam_edit(session.document(),first);const auto revision=session.revision();
    check(!workspace::commit_cam(live,edit,*session.document().body_history.find(first))&&session.revision()==revision,"Unchanged OK added Undo");
    auto input=workspace::prepare_cam_calculation(live,part.document_id,first);auto r=result(input,"first-result");
    check(input.material.size()==1&&input.material.front().second.shares_with(session.calculated_boundaries().back().body_outputs.at(stock)),
        "Input preparation duplicated material or failed stock/target deduplication");
    check(workspace::publish_cam_calculation(live,input,r),"CAM result publication failed");
    check(workspace::cam_result_state(session,first)==workspace::CamResultState::Current,"Published result not current");
    check(!workspace::publish_cam_calculation(live,input,r),"Old publication was accepted twice");
    auto current=workspace::prepare_cam_calculation(live,part.document_id,first);const auto published_revision=session.revision();
    check(!workspace::publish_cam_calculation(live,current,r)&&session.revision()==published_revision,"Identical result added Undo");
    auto bad=r;bad.machine.maximum_feed_mm_min+=10;rejects([&]{static_cast<void>(workspace::publish_cam_calculation(live,current,bad));});
    check(session.revision()==published_revision,"Invalid result changed document");
    current.runtime_identity=std::make_shared<const int>(0);
    check(!workspace::publish_cam_calculation(live,current,r),"Another runtime published into the document");
    edit=workspace::prepare_cam_edit(session.document());const auto second=edit.object_id;
    check(workspace::cam_source_candidates(session,edit,true)==std::vector<std::string>{stock,first},"Current CAM stock not offered");
    check(workspace::cam_source_candidates(session,edit,false)==std::vector<std::string>{stock},"CAM offered as target/fixture");
    check(workspace::commit_cam(live,edit,setup(edit,first,stock)),"Chained setup failed");
    auto second_input=workspace::prepare_cam_calculation(live,part.document_id,second);
    check(second_input.material.front().first==first,"Chained setup did not use preceding remaining stock");
    check(workspace::publish_cam_calculation(live,second_input,result(second_input,"second-result")),"Chained publication failed");
    check(workspace::cam_result_state(session,second)==workspace::CamResultState::Current,"Chained result not current");
    const auto retained=session.document().body_history.find(first)->cam->calculated;
    edit=workspace::prepare_cam_edit(session.document(),first);body=*session.document().body_history.find(first);body.cam->tools[0].cutting[0].radius_mm=2;
    check(workspace::commit_cam(live,edit,body),"Tool edit failed");
    check(session.document().body_history.find(first)->cam->calculated==retained,"Tool edit discarded immutable inspection result");
    check(workspace::cam_result_state(session,first)==workspace::CamResultState::Stale&&
        workspace::cam_result_state(session,second)==workspace::CamResultState::Stale,"Tool edit did not invalidate downstream stock");
    rejects([&]{static_cast<void>(workspace::prepare_cam_calculation(live,part.document_id,second));});
    check(session.undo()&&workspace::cam_result_state(session,second)==workspace::CamResultState::Current,"Undo did not restore chain");
    check(session.redo()&&workspace::cam_result_state(session,second)==workspace::CamResultState::Stale,"Redo reused stale stock");
    check(session.undo(),"Restore tool failed");
    input=workspace::prepare_cam_calculation(live,part.document_id,first);r=result(input,"collision");r.missing_target_mm3=1;
    check(workspace::publish_cam_calculation(live,input,r),"Collision result unavailable for inspection");
    check(workspace::cam_result_state(session,first)==workspace::CamResultState::Collision&&
        workspace::cam_result_state(session,second)==workspace::CamResultState::Collision,"Collision stock accepted downstream");
    rejects([&]{static_cast<void>(workspace::prepare_cam_calculation(live,part.document_id,second));});
    check(session.undo(),"Undo collision failed");
    // A lost work-frame source retains the previous coordinates, not the draft's fallback.
    edit=workspace::prepare_cam_edit(session.document(),first);body=*session.document().body_history.find(first);
    body.cam->work_frame.x=17;body.cam->work_frame.references.push_back({{},"deleted-origin","origin:point"});
    check(workspace::commit_cam(live,edit,body),"Broken placement could not be retained");
    const auto& broken=*session.document().body_history.find(first)->cam;
    check(broken.work_frame.x==0&&!broken.work_frame.reference_valid&&broken.calculated==retained,"Reference loss moved frame or discarded result");
    check(workspace::cam_result_state(session,second)==workspace::CamResultState::UnresolvedFrame,"Broken frame allowed chained stock");
    check(session.undo(),"Undo broken frame failed");
    // Clamping references are real preceding Sketches; missing references remain repairable.
    edit=workspace::prepare_cam_edit(session.document(),first);body=*session.document().body_history.find(first);
    body.cam->clamping_sketch_ids={test::rectangular_sketch_id(feature)};
    check(workspace::commit_cam(live,edit,body),"Clamping Sketch selection failed");
    input=workspace::prepare_cam_calculation(live,part.document_id,first);
    check(input.clamping_sketches.size()==1&&input.source_keys.size()==2,"Clamping input was omitted");
    check(workspace::publish_cam_calculation(live,input,result(input,"clamped")),"Clamping result publication failed");
    auto changed=session.document();changed.sketches.front().suppressed=true;session.commit_metadata(changed);
    check(workspace::cam_result_state(session,first)==workspace::CamResultState::SuppressedSource,"Suppressed clamping Sketch accepted");
    check(session.undo(),"Undo suppressed Sketch failed");
    // A metadata change alone invalidates an in-flight publication, including Undo/Redo generations.
    input=workspace::prepare_cam_calculation(live,part.document_id,first);r=result(input,"late");
    changed=session.document();auto altered=changed.body_history;auto renamed=*altered.find(first);renamed.name="Renamed";
    altered.update_body(renamed);changed.set_body_history(altered);session.commit_metadata(changed);
    check(!workspace::publish_cam_calculation(live,input,r),"Late calculation replaced a newer revision");
    check(session.undo()&&!workspace::publish_cam_calculation(live,input,r),"Undo made an old publication current again");
    check(kernel::body_calculation_count.load()==count&&&session.calculated_boundaries()==boundaries,
        "CAM editing, inspection or Undo recalculated/copied design geometry");
    // Change actual native source geometry, then verify retained outputs cannot be reused.
    changed=session.document();test::resize_rectangular_feature(changed,changed.history.front(),{20,10,6});
    auto replacement=kernel.evaluate_history(changed.kernel_operations());
    session.commit(changed,std::move(replacement));
    check(workspace::cam_result_state(session,first)==workspace::CamResultState::Stale&&
        workspace::cam_result_state(session,second)!=workspace::CamResultState::Current,"Changed native source did not invalidate chain");
    const auto after=kernel::body_calculation_count.load();
    check(session.undo()&&workspace::cam_result_state(session,first)==workspace::CamResultState::Current,"Source Undo lost current CAM result");
    check(session.redo()&&workspace::cam_result_state(session,first)==workspace::CamResultState::Stale,"Source Redo reused stale result");
    check(kernel::body_calculation_count.load()==after,"Result inspection regenerated source geometry");
    std::cout<<"CAM operations: "<<assertions<<" lifecycle, source, chaining and publication assertions passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
