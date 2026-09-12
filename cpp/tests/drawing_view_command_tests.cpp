#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_view_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
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
int main(){try{kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-drawing-view-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);require(dir.parent_path()==root,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Drawing view snapshots, original references, parent-first regeneration, dimensions, native sources and deletion passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
