#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_hatch_operations.hpp>
#include <zima/kernel/stable_id.hpp>
#include <iostream>
#include <cmath>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
Json patch(const std::string& view,Json component){return {{"view",view},{"components",Json::array({std::move(component)})}};}
void expect_stale(const std::function<bool()>& commit) {
    try{commit();throw std::runtime_error("Obsolete hatch edit was accepted");}
    catch(const workspace::DrawingOperationError& e){require(e.code=="stale_edit","Unexpected stale edit error");}
}
std::size_t hatches(const drawing::DrawingView& view){return std::ranges::count_if(view.projected_edges,[](const auto& edge){return edge.hatch&&!edge.hidden;});}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;auto part=document::PartDocument::create_default();auto box=document::PartDocument::create_box_container();box.box={20,10,6};part.history={box};document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Body"));graph.insert({document::PartHistoryKind::Feature,box.id});part.set_body_history(graph);
    auto cache=kernel.evaluate_history(part.kernel_operations());auto section=document::create_section();static_cast<void>(section.sketch.add_segment(-30,0,30,0));part.sections={section};
    const auto body=part.body_history.bodies().front().scope.id,source_id=part.document_id;const auto source_path=dir/"source.PRTZ";
    part.save(source_path,cache);live.add_part(part,cache,source_path);
    auto doc=drawing::DrawingDocument::create_default();const auto id=doc.document_id,sheet=doc.sheets.front().id;
    live.add_drawing(doc,dir/"hatches.drwz");live.activate(id);live.display_top_level(id);command_host::Host host(live,kernel,dir);
    const auto cut=run(host,"drawing.view.create",{{"sheet",sheet},{"source",source_id},{"section",section.id}}).data.at("view").get<std::string>();
    const auto other=run(host,"drawing.view.create",{{"sheet",sheet},{"source",source_id},{"section",section.id}}).data.at("view").get<std::string>();
    const auto view=[&](const std::string& key)->const drawing::DrawingView&{return *live.open_drawing(id)->document().find_view(key);};
    const auto state=[&]{return live.open_part(source_id);};const auto source_revision=state()->session.revision();const auto shape=state()->session.calculated_boundaries().back().kernel_shape;
    const auto drawing_revision=live.open_drawing(id)->revision();
    auto queried=run(host,"drawing.view.hatch.get",{{"view",cut}}).data;
    require(queried.at("total")==1&&queried["items"][0]["component"]==body&&queried["items"][0]["hatch"]["spacing_mm"]==2,"Current source component query is wrong");
    require(hatches(view(cut))>0&&state()->session.revision()==source_revision&&live.open_drawing(id)->revision()==drawing_revision,"Query changed history or fixture has no cut hatch");
    const auto hidden=run(host,"drawing.view.hatch.set",patch(cut,{{"component",body},{"mode","cut_only"}})).data;
    require(hidden["source_changed"]==false&&hidden["changed"]==true&&state()->session.revision()==source_revision,"Local hiding changed source");
    require(hatches(view(cut))==0&&hatches(view(other))>0,"Local hatch visibility leaked to another view");
    run(host,"undo");require(hatches(view(cut))>0,"Drawing Undo lost hatching");run(host,"redo");require(hatches(view(cut))==0,"Drawing Redo lost hiding");
    const auto patch_style=patch(cut,{{"component",body},{"hatch",{{"pattern","cross"},{"angle_degrees",12.3456789},{"spacing_mm",2.3456789},{"offset_mm",.3456789}}}});
    const auto edited=run(host,"drawing.view.hatch.set",patch_style).data;
    require(edited["source_changed"]==true&&edited["items"][0]["hidden_in_view"]==true,"Style edit lost local hiding");
    const auto style=state()->session.document().sections.front().components.at(body);
    require(style.custom_hatch&&style.mode==0&&style.hatch.pattern==1&&style.hatch.angle==12.3456789&&style.hatch.spacing_mm==2.3456789&&style.hatch.offset_mm==.3456789,"Source hatch lost numeric precision");
    require(view(other).section_snapshot->components.at(body)==style&&hatches(view(other))>0&&hatches(view(cut))==0,"Shared style did not update other view independently of hiding");
    require(state()->session.calculated_boundaries().back().kernel_shape==shape&&state()->session.document().sections.front().sketch.serialized()==section.sketch.serialized(),"Hatch edit recalculated body or changed cutting Sketch");
    const auto before_noop=live.open_drawing(id)->revision(),source_noop=state()->session.revision();
    require(run(host,"drawing.view.hatch.set",patch_style).data["changed"]==false&&live.open_drawing(id)->revision()==before_noop&&state()->session.revision()==source_noop,"No-op changed either history");
    for(const auto& bad:std::vector<Json>{
        patch(cut,{{"component","absent"},{"mode","uncut"}}),patch(cut,{{"component",body},{"hatch",{{"spacing_mm",0}}}}),
        patch(cut,{{"component",body},{"hatch",{{"pattern","other"}}}}),patch(cut,{{"component",body},{"hatch",{{"angle_degrees","4"}}}}),
        patch(cut,{{"component",body},{"mode",1}}),patch(cut,{{"component",body},{"custom_hatch",false},{"hatch",{{"offset_mm",2}}}}),
        {{"view",cut},{"components",Json::array()}},{{"view",cut},{"components",Json::array({{{"component",body}},{{"component",body}}})}},
        {{"view",cut},{"components",Json::array({{{"component",body},{"hatch",{{"spacing_mm",4}}}},{{"component","missing"}}})}}}) {
        require(!host.execute({{"command","drawing.view.hatch.set"},{"arguments",bad}}).ok,"Invalid component patch accepted");
        require(live.open_drawing(id)->revision()==before_noop&&state()->session.revision()==source_noop&&state()->session.document().sections.front().components.at(body)==style,"Rejected edit partially changed documents");
    }
    // 3D source visibility is not Drawing hatch visibility.
    auto source_model=state()->session.document();source_model.sections.front().components.at(body).mode=1;state()->session.commit(source_model,state()->session.calculated_boundaries());
    run(host,"drawing.view.hatch.set",patch(cut,{{"component",body},{"mode","cut_hatch"}}));
    require(state()->session.document().sections.front().components.at(body).mode==1&&hatches(view(cut))>0,"Drawing visibility replaced source 3D visibility");
    run(host,"drawing.view.hatch.set",patch(cut,{{"component",body},{"mode","uncut"}}));
    require(state()->session.document().sections.front().components.at(body).mode==2&&hatches(view(cut))==0&&hatches(view(other))==0,"Uncut mode did not belong to source");
    run(host,"drawing.view.hatch.set",patch(cut,{{"component",body},{"mode","cut_hatch"},{"custom_hatch",false}}));
    require(hatches(view(cut))>0&&!state()->session.document().sections.front().components.at(body).custom_hatch,"Inherited hatch could not be restored");
    auto expected=workspace::sections_for_part(state()->session.document()).front();auto changed=expected;changed.components[body].custom_hatch=true;changed.components[body].hatch.offset_mm=1;
    auto delayed=workspace::prepare_section_component_commit(&live,source_id,source_path,changed,&expected);
    auto intervening=state()->session.document();intervening.name="New source name";state()->session.commit(intervening,state()->session.calculated_boundaries());require(state()->session.undo(),"Source Undo unavailable");
    expect_stale(delayed);require(state()->session.document().sections.front().components==expected.components,"Stale editor overwrote source");
    delayed=workspace::prepare_section_component_commit(&live,source_id,source_path,changed,&expected);
    state()->session.document().save(source_path,state()->session.calculated_boundaries());require(live.remove(source_id),"Source removal failed");live.add_part(document::PartDocument::load(source_path),cache,source_path);expect_stale(delayed);
    // A late descendant projection failure commits neither document.
    auto broken=live.open_drawing(id)->document();auto child=view(cut);child.id=kernel::make_stable_id();child.parent_view_id=cut;child.projection_direction=drawing::ProjectionDirection::Right;child.section_id="missing";broken.sheets.front().views.push_back(child);
    live.open_drawing(id)->commit(broken);const auto before_failure=live.open_drawing(id)->revision(),source_failure=state()->session.revision();
    require(!host.execute({{"command","drawing.view.hatch.set"},{"arguments",patch_style}}).ok&&live.open_drawing(id)->revision()==before_failure&&state()->session.revision()==source_failure,"Late projection failure partly committed hatch");
    run(host,"undo");
    // Closed source query/local visibility do not open it. Style commit opens once, keeps focus and is dirty/undoable.
    require(live.remove(source_id),"Source removal failed");const auto count=live.size();run(host,"drawing.view.hatch.get",{{"view",cut}});require(live.size()==count,"Query opened source");
    run(host,"drawing.view.hatch.set",patch(cut,{{"component",body},{"mode","cut_only"}}));require(live.size()==count,"Local visibility opened source");
    run(host,"drawing.view.hatch.set",patch_style);
    require(live.size()==count+1&&live.active_document_id()==id&&live.displayed_document_id()==id&&state()->session.is_dirty(),"Closed source commit lost focus, opening or dirty state");
    require(state()->session.document().sections.front().components.at(body).hatch.offset_mm==.3456789,"Closed source style not committed");
    require(state()->session.undo(),"Source Undo unavailable");require(state()->session.document().sections.front().components.at(body).custom_hatch==false,"Source undo lost pre-edit file state");require(state()->session.redo(),"Source Redo unavailable");
    run(host,"save");state()->session.document().save(source_path,state()->session.calculated_boundaries());
    require(drawing::DrawingDocument::load(dir/"hatches.drwz").find_view(cut)->hidden_hatch_components.contains(body)&&document::PartDocument::load(source_path).sections.front().components.at(body).hatch.offset_mm==.3456789,"Native files lost local/source hatch settings");
    expected=workspace::sections_for_part(state()->session.document()).front();changed=expected;changed.components[body].hatch.offset_mm=2;
    require(live.remove(source_id),"Source removal failed");delayed=workspace::prepare_section_component_commit(&live,source_id,source_path,changed,&expected);require(live.size()==count,"Preparation opened source");
    auto wrong=part;wrong.document_id=kernel::make_stable_id();wrong.save(source_path,cache);expect_stale(delayed);
    const auto identity_failure=host.execute({{"command","drawing.view.hatch.get"},{"arguments",{{"view",cut}}}});
    require(!identity_failure.ok&&identity_failure.code=="source_identity","Wrong closed source identity accepted");
    live.add_part(wrong,cache,source_path);
    require(host.execute({{"command","drawing.view.hatch.get"},{"arguments",{{"view",cut}}}}).code=="source_identity","Open path redirected hatch query to wrong document");
}
void assembly_cases(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;auto part=document::PartDocument::create_default();auto box=document::PartDocument::create_box_container();box.box={10,10,10};part.history={box};document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Body"));graph.insert({document::PartHistoryKind::Feature,box.id});part.set_body_history(graph);auto cache=kernel.evaluate_history(part.kernel_operations());
    live.add_part(part,cache,{});auto child=assembly::AssemblyDocument::create_default();const auto child_id=child.document_id;live.add_assembly(child,{});
    const auto leaf=live.insert_open_part(child_id,part.document_id,"Bolt");auto top=assembly::AssemblyDocument::create_default();const auto source=top.document_id;live.add_assembly(top,dir/"nested.asmz");
    const auto a=live.insert_open_assembly(source,child_id,"First"),b=live.insert_open_assembly(source,child_id,"Second");
    auto* owner=live.open_assembly(source);auto model=owner->session.document();model.find_occurrence(b)->placement.x=30;auto section=document::create_section();static_cast<void>(section.sketch.add_segment(-50,0,50,0));model.sections={section};owner->session.commit(model);
    auto doc=drawing::DrawingDocument::create_default();const auto id=doc.document_id;live.add_drawing(doc,dir/"nested.drwz");live.activate(id);live.display_top_level(id);command_host::Host host(live,kernel,dir);
    const auto view=run(host,"drawing.view.create",{{"sheet",doc.sheets.front().id},{"source",source},{"section",section.id}}).data.at("view").get<std::string>();
    const auto first=assembly::InstancePath{}.child(a).child(leaf).encoded(),second=assembly::InstancePath{}.child(b).child(leaf).encoded();
    const auto query=run(host,"drawing.view.hatch.get",{{"view",view}}).data;require(query["total"]==2&&std::abs(query["items"][0]["hatch"]["angle_degrees"].get<double>()-query["items"][1]["hatch"]["angle_degrees"].get<double>())==90,"Nested occurrences merged or inherited alternation lost");
    const auto packet=live.open_assembly(source)->session.document().find_occurrence(a)->calculated_source;
    const auto child_revision=live.open_assembly(child_id)->session.revision(),part_revision=live.open_part(part.document_id)->session.revision();
    run(host,"drawing.view.hatch.set",patch(view,{{"component",first},{"hatch",{{"spacing_mm",3.125}}}}));
    const auto& components=live.open_assembly(source)->session.document().sections.front().components;
    require(components.size()==1&&components.at(first).hatch.spacing_mm==3.125&&!components.contains(second),"Nested source edit affected sibling occurrence");
    require(live.open_assembly(source)->session.document().find_occurrence(a)->calculated_source.shares_with(packet)&&live.open_assembly(child_id)->session.revision()==child_revision&&live.open_part(part.document_id)->session.revision()==part_revision,"Hatch edit changed child owners or copied source geometry");
    run(host,"save");live.open_assembly(source)->session.document().save(dir/"nested.asmz");require(live.remove(source),"Assembly removal failed");
    run(host,"drawing.view.hatch.set",patch(view,{{"component",second},{"hatch",{{"offset_mm",.375}}}}));
    require(live.open_assembly(source)&&live.open_assembly(source)->session.document().sections.front().components.at(second).hatch.offset_mm==.375,"Closed Assembly did not accept exact hatch edit");
    require(run(host,"drawing.view.hatch.get",{{"view",view},{"limit",1}}).data["items"].size()==1,"Hatch query limit ignored");
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-hatch-"+kernel::make_stable_id());fs::create_directory(dir);verify(kernel,dir);assembly_cases(kernel,dir);require(fs::canonical(dir).parent_path()==parent,"Unexpected test cleanup path");fs::remove_all(dir);std::cout<<"Drawing hatch query/edit, source identity, exact occurrences, native files and history passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
