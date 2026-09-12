#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_view_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/workspace/drawing_projection.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/document/file_path.hpp>
#include <algorithm>
#include <iostream>
#include <cmath>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()){
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
double width(const drawing::DrawingView& view){double low=1e100,high=-1e100;for(const auto& t:view.projected_triangles)for(auto p:t.points){low=std::min(low,p.x);high=std::max(high,p.x);}return high-low;}
void near(double a,double b){require(std::abs(a-b)<1e-6,"Projected size or dimension differs from the analytical box size");}
void verify_editing(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;auto part=document::PartDocument::create_default();auto box=document::PartDocument::create_box_container();box.box={20,10,6};part.history={box};
    auto boundaries=kernel.evaluate_history(part.kernel_operations());
    auto section=document::create_section();static_cast<void>(section.sketch.add_segment(-30,0,30,0));part.sections.push_back(section);
    live.add_part(part,boundaries,dir/"editing.prtz");
    auto doc=drawing::DrawingDocument::create_default();const auto sheet=doc.sheets.front().id,id=doc.document_id;doc.sheets.front().default_scale=2;
    live.add_drawing(doc,dir/"editing.drwz");live.activate(id);live.display_top_level(id);command_host::Host host(live,kernel,dir);
    const auto source_revision=live.open_part(part.document_id)->session.revision();
    const auto base=run(host,"drawing.view.create",{{"sheet",sheet},{"source",part.document_id},{"name","Hlavní pohled"},{"x_mm",80},{"y_mm",50}}).data;
    const auto parent=base.at("view").get<std::string>();near(base.at("scale").get<double>(),2);near(width(*live.open_drawing(id)->document().find_view(parent)),20);
    const auto child=run(host,"drawing.view.create",{{"sheet",sheet},{"parent_view",parent},{"projection_direction","right"},{"distance_mm",30}}).data.at("view").get<std::string>();
    near(width(*live.open_drawing(id)->document().find_view(child)),10);near(live.open_drawing(id)->document().find_view(child)->x,50);
    const auto grand=run(host,"drawing.view.create",{{"sheet",sheet},{"parent_view",child},{"projection_direction","top"},{"distance_mm",25}}).data.at("view").get<std::string>();
    run(host,"drawing.view.set",{{"view",parent},{"x_mm",100},{"y_mm",60},{"scale",.5},{"orientation","back"},{"display_style","hidden_edges"},{"value_locks",Json::array({"scale"})}});
    const auto& current=live.open_drawing(id)->document();near(current.find_view(child)->x,70);near(current.find_view(grand)->x,70);near(current.find_view(grand)->y,85);
    near(current.find_view(parent)->scale,.5);near(current.find_view(child)->scale,2);require(current.find_view(parent)->value_locks.contains("scale"),"View value locks lost");
    run(host,"undo");near(live.open_drawing(id)->document().find_view(grand)->y,75);run(host,"redo");near(live.open_drawing(id)->document().find_view(grand)->y,85);
    run(host,"drawing.view.set",{{"view",child},{"distance_mm",40}});near(live.open_drawing(id)->document().find_view(child)->x,60);near(live.open_drawing(id)->document().find_view(grand)->x,60);
    const auto revision=live.open_drawing(id)->revision(),generation=live.open_drawing(id)->data_generation();
    for(const auto& args:std::vector<Json>{
        {{"view",parent},{"scale",0}},{{"view",parent},{"scale",1},{"use_sheet_scale",true}},{{"view",parent},{"name","   "}},
        {{"view",child},{"source",part.document_id}},{{"view",child},{"x_mm",0}},{{"view",parent},{"value_locks",Json::array({"other"})}},
        {{"view",parent},{"camera",{{"horizontal",{1,0,0}},{"vertical",{0,1,0}},{"depth",{0,0,1}}}}},
        {{"view",parent},{"camera",{{"horizontal",{2,0,0}},{"vertical",{0,1,0}},{"depth",{0,0,-1}}}}},
        {{"view",parent},{"section","missing"}}}) {
        require(!host.execute({{"command","drawing.view.set"},{"arguments",args}}).ok,"Invalid view edit accepted");
        require(live.open_drawing(id)->revision()==revision&&live.open_drawing(id)->data_generation()==generation,"Failed edit changed Drawing history");
    }
    run(host,"drawing.view.set",{{"view",parent},{"camera",{{"horizontal",{0,1,0}},{"vertical",{0,0,1}},{"depth",{-1,0,0}}}}});near(width(*live.open_drawing(id)->document().find_view(parent)),10);
    run(host,"undo");
    // A late descendant error must not publish the already-projected parent.
    auto bad=live.open_drawing(id)->document();bad.find_view(grand)->section_id="missing";live.open_drawing(id)->commit(bad);const auto bad_revision=live.open_drawing(id)->revision();
    require(!host.execute({{"command","drawing.view.set"},{"arguments",{{"view",parent},{"x_mm",150}}}}).ok&&live.open_drawing(id)->revision()==bad_revision,"Descendant failure partly committed parent");
    near(live.open_drawing(id)->document().find_view(parent)->x,100);run(host,"undo");
    require(live.open_part(part.document_id)->session.revision()==source_revision,"Drawing edit regenerated its source");
    run(host,"save");const auto saved=drawing::DrawingDocument::load(dir/"editing.drwz");require(saved.find_view(grand)&&saved.find_view(parent)->name=="Hlavní pohled","Native Drawing lost edited views");
    run(host,"drawing.view.delete",{{"view",parent}});run(host,"undo");require(live.open_drawing(id)->document().find_view(grand),"Undo lost view identities");
    auto empty=document::PartDocument::create_default();live.add_part(empty,{},{});
    require(!host.execute({{"command","drawing.view.create"},{"arguments",{{"sheet",sheet},{"source",empty.document_id}}}}).ok,"Uncalculated empty source accepted");
    // Separate sources in one projection session must never reuse another mesh.
    auto second=part;second.document_id=document::PartDocument::create_default().document_id;second.history.front().box={7,7,7};auto small=kernel.evaluate_history(second.kernel_operations());live.add_part(second,small,{});
    const auto unsaved_view=run(host,"drawing.view.create",{{"sheet",sheet},{"source",second.document_id}}).data.at("view").get<std::string>();
    near(width(*live.open_drawing(id)->document().find_view(unsaved_view)),7);
    require(live.open_drawing(id)->document().sheets.front().bom_rows.front().name==second.name,"Unsaved source lost its BOM name");
    workspace::DrawingProjection projection(&live,{});auto a=drawing::DrawingDocument::create_view(part.document_id,{},{}),b=drawing::DrawingDocument::create_view(second.document_id,{},{});
    projection.project(a,{});projection.project(b,{});near(width(a),20);near(width(b),7);a.camera=drawing::standard_camera(drawing::ViewOrientation::Right);projection.project(a,{});near(width(a),10);
    const auto section_view=run(host,"drawing.view.create",{{"sheet",sheet},{"source",part.document_id},{"section",section.id},{"scale",2}}).data.at("view").get<std::string>();
    const auto* cut=live.open_drawing(id)->document().find_view(section_view);
    require(cut->section_snapshot&&cut->section_snapshot->id==section.id&&!cut->section_parent_id.empty()&&std::ranges::any_of(cut->projected_edges,[](const auto& edge){return edge.hatch;}),"Command Section view lost its cut, hatch or trace parent");
    run(host,"drawing.view.set",{{"view",parent},{"section_markers",Json::array({section.id})}});
    require(run(host,"drawing.view.get",{{"view",parent}}).data.at("section_markers")==Json::array({section.id}),"Section trace selection did not persist");
    run(host,"drawing.view.set",{{"view",section_view},{"section",""}});require(!live.open_drawing(id)->document().find_view(section_view)->section_snapshot,"Clearing Section kept the old cut");
    require(live.open_part(part.document_id)->session.revision()==source_revision,"Section view edit changed its source model");
}
void verify(const kernel::OcctKernel& kernel,fs::path dir){
    fs::create_directory(dir/"drawings");const auto source_path=dir/"source.prtz";
    auto part=document::PartDocument::create_default();auto box=document::PartDocument::create_box_container();box.box={20,10,6};part.history={box};
    auto boundaries=kernel.evaluate_history(part.kernel_operations());part.save(source_path,boundaries);
    workspace::Workspace live;live.add_part(part,boundaries,source_path);
    auto doc=drawing::DrawingDocument::create_default();doc.source_document_id=part.document_id;doc.source_path="../source.prtz";
    auto parent=drawing::DrawingDocument::create_view(part.document_id,"../source.prtz",boundaries.back().mesh);
    auto child=drawing::DrawingDocument::create_view(part.document_id,"../source.prtz",boundaries.back().mesh);child.parent_view_id=parent.id;child.projection_direction=drawing::ProjectionDirection::Right;
    auto grand=drawing::DrawingDocument::create_view(part.document_id,"../source.prtz",boundaries.back().mesh);grand.parent_view_id=child.id;grand.projection_direction=drawing::ProjectionDirection::Top;
    doc.sheets.front().views={grand,child,parent};const auto sheet=doc.sheets.front().id;
    auto dim=drawing::make_drawing_dimension(parent.id);const auto curve=std::ranges::find_if(parent.measurement_geometry->curves,[](const auto& c){return c.points.size()>1&&std::abs(c.points.front().x-c.points.back().x)>19;});require(curve!=parent.measurement_geometry->curves.end(),"Box has no original measuring curve");
    dim.attachments={{drawing::DimensionAttachmentKind::CurvePoint,curve->source,{},0},{drawing::DimensionAttachmentKind::CurvePoint,curve->source,{},1}};
    drawing::refresh_drawing_dimension(parent,dim);near(drawing::evaluate_drawing_dimension(parent,dim).presentations[0].value,20);doc.sheets.front().dimensions={dim};
    live.add_drawing(doc,dir/"drawings/view.drwz");const auto id=doc.document_id;live.activate(id);live.display_top_level(id);command_host::Host host(live,kernel,dir);auto* state=live.open_drawing(id);
    const auto limited=run(host,"drawing.view.list",{{"limit",1}}).data;require(limited.at("total")==3&&limited.at("items").size()==1,"Drawing view query limit lost total");
    const auto references=run(host,"drawing.view.references",{{"view",parent.id}}).data;
    require(references.at("total").get<int>()>0&&references.at("source_document")==part.document_id,"Drawing view lost persisted measuring references");
    const auto query_revision=state->revision();run(host,"drawing.view.get",{{"view",parent.id}});require(state->revision()==query_revision,"Reading a view changed history");
    auto changed=part;changed.history.front().box.length=40;auto larger=kernel.evaluate_history(changed.kernel_operations());live.open_part(part.document_id)->session.commit(changed,larger);
    near(width(*state->document().find_view(parent.id)),20);const auto source_revision=live.open_part(part.document_id)->session.revision();
    run(host,"regenerate");near(width(*state->document().find_view(parent.id)),40);
    near(drawing::evaluate_drawing_dimension(*state->document().find_view(parent.id),state->document().sheets.front().dimensions.front()).presentations[0].value,40);
    const auto expected=drawing::projected_camera(parent.camera,child.projection_direction,doc.sheets.front().projection_method);
    const auto& actual=state->document().find_view(child.id)->camera;near(actual.horizontal.x,expected.horizontal.x);near(actual.horizontal.y,expected.horizontal.y);near(actual.vertical.z,expected.vertical.z);
    require(live.open_part(part.document_id)->session.revision()==source_revision&&state->document().sheets.front().bom_rows.size()==1&&!state->document().find_view(parent.id)->model_annotations.empty(),"Regeneration changed source or omitted BOM and annotations");
    run(host,"undo");near(width(*state->document().find_view(parent.id)),20);run(host,"redo");near(width(*state->document().find_view(parent.id)),40);
    const auto snapshot_references=run(host,"drawing.view.references",{{"view",parent.id}}).data;
    changed.save(source_path,larger);
    const auto open_mesh=workspace::read_drawing_source(&live,source_path,part.document_id).second;
    const auto closed_mesh=workspace::read_drawing_source(nullptr,source_path,part.document_id).second;
    require(open_mesh.original_references.points.size()==closed_mesh.original_references.points.size()&&open_mesh.original_references.edges.size()==closed_mesh.original_references.edges.size()&&open_mesh.edges.size()==closed_mesh.edges.size(),"Open and closed Parts expose different reference packets");
    live.remove(part.document_id);state=live.open_drawing(id);fs::remove(source_path);const auto revision=state->revision(),generation=state->data_generation();
    require(run(host,"drawing.view.references",{{"view",parent.id}}).data==snapshot_references,"View reference query loaded a missing source");
    require(!host.execute({{"command","regenerate"}}).ok&&state->revision()==revision&&state->data_generation()==generation,"Missing source partially committed drawing regeneration");
    changed.save(source_path,larger);run(host,"regenerate");require(live.size()==1,"Drawing regeneration opened source tabs");near(width(*state->document().find_view(parent.id)),40);
    run(host,"save");const auto persisted=drawing::DrawingDocument::load(state->path);require(persisted.sheets.front().views.size()==3&&persisted.find_view(parent.id)->measurement_geometry->curves.size()==state->document().find_view(parent.id)->measurement_geometry->curves.size(),"Regenerated view data did not persist");
    auto cyclic=state->document();cyclic.find_view(parent.id)->parent_view_id=child.id;state->commit(cyclic);const auto cycle_revision=state->revision();
    auto failed=host.execute({{"command","regenerate"}});require(!failed.ok&&failed.code=="dependency_cycle"&&state->revision()==cycle_revision,"Projection cycle was accepted or partly committed");run(host,"undo");
    auto wrong=changed;wrong.document_id="other-part";wrong.save(source_path,larger);const auto before=state->revision();failed=host.execute({{"command","regenerate"}});require(!failed.ok&&failed.code=="source_identity"&&state->revision()==before,"Wrong native source identity was accepted");changed.save(source_path,larger);
    auto next=state->document();auto independent=drawing::DrawingDocument::create_view(part.document_id,"../source.prtz",larger.back().mesh);independent.section_parent_id=parent.id;next.sheets.front().views.push_back(independent);state->commit(next);
    const auto removed=run(host,"drawing.view.delete",{{"view",parent.id}}).data.at("removed");require(removed.size()==3&&state->document().sheets.front().views.size()==1&&state->document().sheets.front().dimensions.empty()&&state->document().find_view(independent.id)->section_parent_id.empty(),"View deletion left descendants, dimensions or a dangling section-parent link");
    run(host,"undo");require(state->document().find_view(grand.id)&&state->document().sheets.front().dimensions.front().id==dim.id,"View deletion Undo lost stable identities");
    failed=host.execute({{"command","drawing.view.list"},{"arguments",{{"limit",4294967297LL}}}});require(!failed.ok,"Overflowed view query limit accepted");
    auto wire=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_segment(0,0,8,6));wire.sketches.push_back(sketch);
    const auto wire_path=dir/fs::path(u8"Skica česká.PRTZ");wire.save(wire_path);
    const auto wire_mesh=workspace::read_drawing_source(nullptr,wire_path,wire.document_id).second;
    require(std::ranges::any_of(wire_mesh.edges,[](const auto& edge){return edge.points.size()==2&&std::abs(std::hypot(edge.points[1].x-edge.points[0].x,edge.points[1].y-edge.points[0].y)-10)<1e-6;}),"Closed Sketch-only Part lost its 10 mm curve");
    require(workspace::build_title_block_context_for_source(wire.document_id,wire_path,nullptr).file_stem=="Skica česká"&&!workspace::drawing_annotation_sources(nullptr,wire.document_id,wire_path).empty(),"Uppercase native extension or Unicode metadata failed");

}
}
int main(){try{kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-drawing-view-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);verify_editing(kernel,dir);require(dir.parent_path()==root,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Drawing view snapshots, original references, parent-first regeneration, dimensions, native sources and deletion passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
