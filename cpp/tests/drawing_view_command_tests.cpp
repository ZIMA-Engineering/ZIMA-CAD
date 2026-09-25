#include "profile_solid_fixture.hpp"
#include <zima/drawing/annotation_guides.hpp>
#include <zima/document/native_read_capture.hpp>
#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_view_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/workspace/drawing_projection.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/drawing/detail_view.hpp>
#include <zima/drawing/view_breaks.hpp>
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
void verify_projection_reuse(const kernel::OcctKernel& kernel,const fs::path& dir) {
    auto part=document::PartDocument::create_default();auto box=zima::test::rectangular_feature(part,{20,10,6});part.history={box};
    static_cast<void>(test::family_length_binding(part,box));
    auto calculated=kernel.evaluate_history(part.kernel_operations());
    const auto path=dir/"reuse.prtz";
    workspace::Workspace live;live.add_part(part,calculated,path);
    drawing::DrawingView original;original.id="reuse-view";original.source_document_id=part.document_id;original.source_path=path;
    workspace::DrawingProjection preview(&live,dir/"reuse.drwz");preview.project(original,{});
    require(preview.calculated_camera_count()==1,"Initial projection not calculated");
    workspace::DrawingProjection interactive(&live,dir/"interactive.drwz");auto display=original;interactive.project(display,{.interactive=true});
    require(interactive.calculated_camera_count()==0&&display.output_source,"Interactive view calculated exact output or lost its source snapshot");
    require(!display.projected_edges.empty()&&std::ranges::all_of(display.projected_edges,[](const auto& e){return e.vertex_depths.size()==e.points.size();}),"Interactive geometry lacks per-vertex depth");
    workspace::DrawingProjection display_commit(&live,dir/"interactive.drwz",&interactive);
    auto accepted_display=display;display_commit.project(accepted_display,{.interactive=true});
    require(display_commit.source_load_count()==0&&display_commit.calculated_interactive_camera_count()==0,
        "Unchanged preview source or interactive camera calculated twice on OK");
    require(accepted_display.output_source==display.output_source&&accepted_display.measurement_geometry==display.measurement_geometry,
        "Preview commit lost its original geometry snapshot");
    workspace::Workspace unsaved;unsaved.documents()=live.documents();
    auto edited=part;zima::test::profile_dimension(edited,edited.history.front(),0)=25;
    unsaved.open_part(part.document_id)->session.commit(edited,kernel.evaluate_history(edited.kernel_operations()));
    workspace::DrawingProjection unsaved_commit(&unsaved,dir/"interactive.drwz",&interactive);
    auto unsaved_display=display;unsaved_commit.project(unsaved_display,{.interactive=true});
    require(unsaved_commit.source_load_count()==1&&unsaved_commit.calculated_interactive_camera_count()==1,
        "Unsaved edit with the same runtime identity reused a stale interactive view");near(width(unsaved_display),25);
    auto saved_display=drawing::DrawingDocument::create_default();saved_display.sheets.front().views={display};saved_display.save(dir/"interactive.drwz");
    auto reopened=drawing::DrawingDocument::load(dir/"interactive.drwz").sheets.front().views.front();
    require(reopened.output_source&&reopened.projected_edges.front().vertex_depths==display.projected_edges.front().vertex_depths,"Reopened drawing lost deferred output geometry");
    drawing::prepare_output_view(reopened);
    require(!reopened.output_source&&reopened.projected_edges.size()==original.projected_edges.size(),"Deferred output changed edge count");
    for(std::size_t i=0;i<original.projected_edges.size();++i)require(reopened.projected_edges[i].points==original.projected_edges[i].points&&reopened.projected_edges[i].hidden==original.projected_edges[i].hidden&&reopened.projected_edges[i].source==original.projected_edges[i].source,"Deferred output changed exact geometry, visibility or identity");
    auto broken=display;broken.breaks={{"depth-break",false,2,3,1,drawing::BreakMark::Zigzag}};
    const auto clipped=drawing::broken_edges(broken);require(!clipped.empty(),"Interactive break removed every edge");
    for(const auto& edge:clipped)require(edge.points.size()==edge.vertex_depths.size(),"Interactive break lost interpolated depth");
    auto detail=display;detail.id="depth-detail";detail.detail_view=true;detail.parent_view_id=display.id;
    detail.crop=drawing::ViewCrop{drawing::ViewCropShape::Circle,{0,0},{{5,5}}};
    drawing::refresh_detail_view(detail,broken);
    require(detail.output_source==broken.output_source&&detail.breaks.size()==1&&detail.measurement_geometry==broken.measurement_geometry,"Detail lost source, break or measurement snapshot");
    drawing::prepare_output_view(detail);require(!detail.output_source&&detail.crop&&detail.breaks.size()==1,"Output preparation discarded detail or break settings");
    workspace::DrawingProjection commit(&live,dir/"reuse.drwz",&preview);auto reused=original;commit.project(reused,{});
    require(commit.calculated_camera_count()==0,"Unchanged source repeated camera calculation");
    require(reused.projected_edges.size()==original.projected_edges.size()&&reused.projected_triangles.size()==original.projected_triangles.size(),"Reuse changed geometry counts");
    for(std::size_t i=0;i<original.projected_edges.size();++i) {
        const auto& a=original.projected_edges[i];const auto& b=reused.projected_edges[i];
        require(a.points==b.points&&a.source==b.source&&a.hidden==b.hidden&&a.tangent==b.tangent&&a.silhouette==b.silhouette,"Reuse changed edge geometry or visibility");
    }
    // Identical IDs do not imply identical geometry. Open unsaved source edits
    // and a new camera must both invalidate the corresponding projection.
    zima::test::profile_dimension(part,part.history.front(),0)=35;
    workspace::Workspace changed;changed.add_part(part,kernel.evaluate_history(part.kernel_operations()),path);
    workspace::DrawingProjection after_edit(&changed,dir/"reuse.drwz",&preview);auto updated=original;after_edit.project(updated,{});
    require(after_edit.calculated_camera_count()==1,"Changed source reused stale geometry");near(width(updated),35);
    updated.camera=drawing::standard_camera(drawing::ViewOrientation::Right);after_edit.project(updated,{});
    require(after_edit.calculated_camera_count()==2,"Changed camera reused stale geometry");
    // Preserve signed coordinates in cache validity; never merge side states.
    auto positive_zero=calculated;require(!positive_zero.back().mesh.vertices.empty(),"Missing box vertices");
    positive_zero.back().mesh.vertices.front().z=0.;
    workspace::Workspace zero_before;zero_before.add_part(live.open_part(part.document_id)->session.document(),positive_zero,path);
    workspace::DrawingProjection zero_preview(&zero_before,dir/"reuse.drwz");auto zero_view=original;zero_preview.project(zero_view,{});
    auto negative_zero=positive_zero;negative_zero.back().mesh.vertices.front().z=-0.;
    workspace::Workspace zero;zero.add_part(live.open_part(part.document_id)->session.document(),negative_zero,path);
    workspace::DrawingProjection changed_zero(&zero,dir/"reuse.drwz",&zero_preview);changed_zero.project(zero_view,{});
    require(changed_zero.calculated_camera_count()==1,"Changed signed source coordinate reused stale projection");
    const auto& unchanged=live.open_part(part.document_id)->session.document();unchanged.save(path,calculated);
    workspace::DrawingProjection disk_preview(nullptr,dir/"reuse.drwz");auto disk_view=original;disk_preview.project(disk_view,{});
    workspace::DrawingProjection disk_commit(nullptr,dir/"reuse.drwz",&disk_preview);disk_commit.project(disk_view,{});
    require(disk_commit.calculated_camera_count()==0,"Unchanged native source repeated projection");
    part.save(path,kernel.evaluate_history(part.kernel_operations()));
    workspace::DrawingProjection disk_changed(nullptr,dir/"reuse.drwz",&disk_preview);disk_changed.project(disk_view,{});
    require(disk_changed.calculated_camera_count()==1,"Modified native source reused stale projection");near(width(disk_view),35);
    // A nested dependency can change without changing the root Assembly or its
    // timestamp. Validate native bytes across the complete loaded hierarchy.
    unchanged.save(path,calculated);
    auto nested=assembly::AssemblyDocument::create_default();
    nested.components.push_back(assembly::AssemblyDocument::create_part_occurrence("Part",part.document_id,path,calculated.back()));
    const auto nested_path=dir/"nested.asmz";nested.save(nested_path);
    auto group=assembly::AssemblyDocument::create_default();
    group.components.push_back(assembly::AssemblyDocument::create_assembly_occurrence("Nested",nested.document_id,nested_path,nested));
    const auto group_path=dir/"group.asmz";group.save(group_path);
    auto assembly_view=original;assembly_view.source_document_id=group.document_id;assembly_view.source_path=group_path;
    workspace::DrawingProjection nested_preview(nullptr,dir/"reuse.drwz");nested_preview.project(assembly_view,{.interactive=true});
    workspace::DrawingProjection nested_commit(nullptr,dir/"reuse.drwz",&nested_preview);nested_commit.project(assembly_view,{.interactive=true});
    require(nested_commit.source_load_count()==0&&nested_commit.calculated_interactive_camera_count()==0,"Unchanged nested Assembly was prepared twice");
    const auto timestamp=fs::last_write_time(path);
    part.save(path,kernel.evaluate_history(part.kernel_operations()));fs::last_write_time(path,timestamp);
    workspace::DrawingProjection nested_changed(nullptr,dir/"reuse.drwz",&nested_preview);nested_changed.project(assembly_view,{.interactive=true});
    require(nested_changed.source_load_count()==1&&nested_changed.calculated_interactive_camera_count()==1,
        "Changed nested Part with unchanged timestamp reused stale preview");near(width(assembly_view),35);
    // A source becoming available must also invalidate an earlier missing state.
    const auto absent=dir/"missing.prtz";
    document::NativeReadCapture capture;
    {document::NativeReadCapture::Scope scope(capture);document::NativeReadCapture::observe(absent);}
    require(capture.unchanged(),"Stable missing dependency changed");unchanged.save(absent,calculated);
    require(!capture.unchanged(),"Newly available dependency did not invalidate the capture");
}
void verify_editing(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;auto part=document::PartDocument::create_default();auto box=zima::test::rectangular_feature(part,{20,10,6});part.history={box};
    static_cast<void>(test::family_length_binding(part,box));
    auto boundaries=kernel.evaluate_history(part.kernel_operations());
    auto section=document::create_section();static_cast<void>(section.sketch.add_segment(-30,0,30,0));part.sections.push_back(section);
    live.add_part(part,boundaries,dir/"editing.prtz");
    auto doc=drawing::DrawingDocument::create_default();const auto sheet=doc.sheets.front().id,id=doc.document_id;doc.sheets.front().default_scale=2;
    live.add_drawing(doc,dir/"editing.drwz");live.activate(id);live.display_top_level(id);command_host::Host host(live,kernel,dir);
    const auto source_revision=live.open_part(part.document_id)->session.revision();
    const auto base=run(host,"drawing.view.create",{{"sheet",sheet},{"source",part.document_id},{"name","Hlavní pohled"},{"x_mm",80},{"y_mm",50}}).data;
    const auto parent=base.at("view").get<std::string>();near(base.at("scale").get<double>(),2);near(width(*live.open_drawing(id)->document().find_view(parent)),20);
    const auto child=run(host,"drawing.view.create",{{"sheet",sheet},{"parent_view",parent},{"projection_direction","right"},{"distance_mm",30}}).data.at("view").get<std::string>();
    require(live.open_drawing(id)->document().find_view(child)->name=="Pohled 2","CLI projected view has no numbered name");
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
    run(host,"drawing.view.set",{{"view",parent},{"guide_count",7},{"guide_spacing_mm",1.234}});
    const auto breaks=Json::array({{{"id","one"},{"vertical",false},{"start",-15},{"length",5},{"gap",1},{"mark",2}}});
    run(host,"drawing.view.set",{{"view",parent},{"breaks",breaks}});run(host,"undo");require(live.open_drawing(id)->document().find_view(parent)->breaks.empty(),"Undo retained break");run(host,"redo");
    run(host,"save");const auto saved=drawing::DrawingDocument::load(dir/"editing.drwz");require(saved.find_view(grand)&&saved.find_view(parent)->name=="Hlavní pohled","Native Drawing lost edited views");
    require(saved.find_view(parent)->breaks.size()==1&&saved.find_view(child)->breaks.empty(),"View break leaked to child or failed to persist");
    require(saved.find_view(parent)->dimension_guide_count==7&&saved.find_view(parent)->dimension_guide_spacing==1.234,"Guide count or spacing did not persist");
    run(host,"drawing.view.delete",{{"view",parent}});run(host,"undo");require(live.open_drawing(id)->document().find_view(grand),"Undo lost view identities");
    auto empty=document::PartDocument::create_default();live.add_part(empty,{},{});
    require(!host.execute({{"command","drawing.view.create"},{"arguments",{{"sheet",sheet},{"source",empty.document_id}}}}).ok,"Uncalculated empty source accepted");
    // Separate sources in one projection session must never reuse another mesh.
    auto second=part;second.document_id=document::PartDocument::create_default().document_id;zima::test::resize_rectangular_feature(second,second.history.front(),{7,7,7});auto small=kernel.evaluate_history(second.kernel_operations());live.add_part(second,small,{});
    const auto unsaved_view=run(host,"drawing.view.create",{{"sheet",sheet},{"source",second.document_id}}).data.at("view").get<std::string>();
    near(width(*live.open_drawing(id)->document().find_view(unsaved_view)),7);
    require(live.open_drawing(id)->document().sheets.front().bom_rows.front().source_document_id==part.document_id&&
        live.open_drawing(id)->document().sheets.front().bom_source_document_id==part.document_id,
        "Independent view source changed the sheet title-block and BOM variant");
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
    auto part=document::PartDocument::create_default();auto box=zima::test::rectangular_feature(part,{20,10,6});part.history={box};
    static_cast<void>(test::family_length_binding(part,box));
    auto boundaries=kernel.evaluate_history(part.kernel_operations());part.save(source_path,boundaries);
    workspace::Workspace live;live.add_part(part,boundaries,source_path);
    auto doc=drawing::DrawingDocument::create_default();doc.source_document_id=part.document_id;doc.source_path="../source.prtz";doc.sheets.front().bom_source_document_id=part.document_id;
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
    auto changed=part;zima::test::profile_dimension(changed,changed.history.front(),0)=40;auto larger=kernel.evaluate_history(changed.kernel_operations());live.open_part(part.document_id)->session.commit(changed,larger);
    near(width(*state->document().find_view(parent.id)),20);const auto source_revision=live.open_part(part.document_id)->session.revision();
    run(host,"regenerate");near(width(*state->document().find_view(parent.id)),40);
    near(drawing::evaluate_drawing_dimension(*state->document().find_view(parent.id),state->document().sheets.front().dimensions.front()).presentations[0].value,40);
    const auto expected=drawing::projected_camera(parent.camera,child.projection_direction,doc.sheets.front().projection_method);
    const auto& actual=state->document().find_view(child.id)->camera;near(actual.horizontal.x,expected.horizontal.x);near(actual.horizontal.y,expected.horizontal.y);near(actual.vertical.z,expected.vertical.z);
    require(live.open_part(part.document_id)->session.revision()==source_revision&&state->document().sheets.front().bom_rows.size()==1&&!state->document().find_view(parent.id)->model_annotations.empty(),"Regeneration changed source or omitted BOM and annotations");
    run(host,"undo");near(width(*state->document().find_view(parent.id)),20);run(host,"redo");near(width(*state->document().find_view(parent.id)),40);
    const auto before_rotation=state->document();
    auto dimensioned=before_rotation;
    auto child_dimension=drawing::make_drawing_dimension(child.id);child_dimension.attachments=dim.attachments;
    auto grand_dimension=drawing::make_drawing_dimension(grand.id);grand_dimension.attachments=dim.attachments;
    dimensioned.sheets.front().dimensions.push_back(child_dimension);dimensioned.sheets.front().dimensions.push_back(grand_dimension);
    state->commit(dimensioned);
    const auto root_camera=dimensioned.find_view(parent.id)->camera;
    const auto camera=drawing::rotated_camera(root_camera,0,0,27);
    const auto vector=[](kernel::Vec3 p){return Json::array({p.x,p.y,p.z});};
    run(host,"drawing.view.set",{{"view",parent.id},{"camera",{{"horizontal",vector(camera.horizontal)},{"vertical",vector(camera.vertical)},{"depth",vector(camera.depth)}}}});
    require(state->document().sheets.front().dimensions.empty(),"Camera roll retained measurements in changed projection planes");
    for(const auto& annotation:dimensioned.find_view(parent.id)->model_annotations) {
        const auto& current=state->document().find_view(parent.id)->model_annotations;
        const auto found=std::ranges::find(current,annotation.source,&drawing::ModelAnnotation::source);
        require(found!=current.end()&&found->value==annotation.value&&found->model_dimension==annotation.model_dimension,"Rotating Drawing changed model dimensions");
    }
    run(host,"undo");require(state->document().sheets.front().dimensions==dimensioned.sheets.front().dimensions,"Rotation Undo lost Drawing dimensions");
    run(host,"redo");require(state->document().sheets.front().dimensions.empty(),"Rotation Redo retained Drawing dimensions");
    run(host,"undo");run(host,"undo");
    require(state->document().sheets.front().dimensions==before_rotation.sheets.front().dimensions,"Rotation checks did not restore the initial dimensions");
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
void verify_breaks(const fs::path& dir) {
    drawing::DrawingView v;v.id="break-view";v.source_document_id="rod";v.scale=.5;v.camera={{1,0,0},{0,1,0},{0,0,-1}};
    v.breaks={{"first",false,100,700,8,drawing::BreakMark::Zigzag},{"second",false,850,50,5,drawing::BreakMark::Straight}};
    drawing::validate_view_breaks(v);
    near(drawing::break_map(v,{1000,0}).x,276);near(drawing::break_map(v,{800,0}).x,116);
    for(double x:{-100.,0.,100.,300.,800.,825.,850.,875.,900.,1000.})near(drawing::break_map(v,drawing::break_map(v,{x,5}),true).x,x);
    for(double scale:{.1,.5,2.}) {
        v.scale=scale;
        near((drawing::break_map(v,{800,0}).x-drawing::break_map(v,{100,0}).x)*scale,8);
    }
    v.scale=.5;
    auto fragments=drawing::break_fragments(v,{{0,0},{1000,0}});require(fragments.size()==3,"Multiple breaks lost retained fragments");near(fragments[1].front().x,800);near(fragments[1].back().x,850);
    drawing::ProjectedEdge e;e.points={{0,0},{1000,0}};e.source={"rod","edge",""};v.projected_edges={e};
    auto edges=drawing::broken_edges(v);require(edges.size()==3&&edges[2].source==e.source,"Break changed original edge identity");
    auto geometry=drawing::MeasurementGeometry{};geometry.points={{{"rod","a",""},{0,0,0}},{{"rod","b",""},{1000,0,0}},{{"rod","hidden",""},{500,0,0}}};drawing::MeasurementCurve curve;curve.source=e.source;curve.points={{0,0,0},{1000,0,0}};curve.line=true;geometry.curves={curve};v.measurement_geometry=drawing::share_measurement_geometry(geometry);
    require(drawing::measurement_candidates(v,{500,0},2,{}).empty(),"Hidden source offered a measurement candidate");
    auto d=drawing::make_drawing_dimension(v.id);drawing::DimensionAttachment a,b;a.kind=b.kind=drawing::DimensionAttachmentKind::Point;a.reference={"rod","a",""};b.reference={"rod","b",""};d.attachments={a,b};drawing::resize_dimension_segments(d);
    auto result=drawing::evaluate_drawing_dimension(v,d);require(result.state==drawing::MeasurementState::Resolved,"Spanning length became hidden");near(result.presentations[0].value,1000);
    d.attachments[1].reference={"rod","hidden",""};require(drawing::evaluate_drawing_dimension(v,d).state==drawing::MeasurementState::Hidden,"Hidden anchor was fabricated at break boundary");
    drawing::ProjectedTriangle triangle;triangle.points={drawing::Point2{0,0},drawing::Point2{1000,0},drawing::Point2{0,20}};v.projected_triangles={triangle};
    for(double scale:{.5,2.})for(bool vertical:{false,true}) {
        auto marks_view=v;marks_view.scale=scale;marks_view.breaks.resize(1);marks_view.breaks[0].vertical=vertical;
        if(vertical)for(auto& edge:marks_view.projected_edges)for(auto& p:edge.points)std::swap(p.x,p.y);
        if(vertical)for(auto& face:marks_view.projected_triangles)for(auto& p:face.points)std::swap(p.x,p.y);
        const auto marks=drawing::break_marks(marks_view);
        require(!marks.empty()&&marks[0].size()==6,"Zigzag mark missing its two arms");
        const auto a=marks[0][1],b=marks[0][2],c=marks[0][3];
        const double ux=a.x-b.x,uy=a.y-b.y,vx=c.x-b.x,vy=c.y-b.y;
        near(std::acos((ux*vx+uy*vy)/std::hypot(ux,uy)/std::hypot(vx,vy))*180/std::acos(-1.),20);
    }
    double area=0;for(const auto& t:drawing::broken_triangles(v)){const auto a=t.points[0],b=t.points[1],c=t.points[2];area+=std::abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))/2;}near(area,2175);
    auto doc=drawing::DrawingDocument::create_default();doc.sheets.front().views={v};doc.save(dir/"breaks.drwz");auto loaded=drawing::DrawingDocument::load(dir/"breaks.drwz");require(loaded.sheets.front().views.front().breaks==v.breaks,"Break native round trip changed settings");
    for(auto& cut:v.breaks)cut.vertical=true;near(drawing::break_map(v,{0,1000}).y,276);
    const auto valid=v.breaks;
    for(int fault=0;fault<6;++fault) {
        v.breaks=valid;
        if(fault==0)v.breaks[1].id=v.breaks[0].id;
        if(fault==1)v.breaks[1].vertical=false;
        if(fault==2)v.breaks[0].length=0;
        if(fault==3)v.breaks[0].gap=0;
        if(fault==4)v.breaks[0].mark=static_cast<drawing::BreakMark>(3);
        if(fault==5)v.breaks[1].start=v.breaks[0].start+v.breaks[0].length;
        bool rejected=false;try{drawing::validate_view_breaks(v);}catch(...){rejected=true;}
        require(rejected,"Invalid break configuration accepted");
    }
    v.breaks=valid;
    v.breaks[1].start=200;bool rejected=false;try{drawing::validate_view_breaks(v);}catch(...){rejected=true;}require(rejected,"Overlapping breaks accepted");
}
void verify_annotation_guides() {
    using namespace zima;
    drawing::DrawingView view;view.scale=2;view.dimension_guide_offset=5;view.dimension_guide_spacing=3;
    drawing::ProjectedEdge edge;edge.points={{0,0},{10,10}};view.projected_edges={edge};
    const auto snapped=drawing::snap_annotation(view,{10,25.2},.5);
    require(snapped.guide.has_value()&&std::abs(snapped.point.y-25)<1e-9,"Guide did not snap in paper millimetres");
    auto other=view;other.dimension_guide_offset=15;
    require(!drawing::snap_annotation(other,{10,25.2},.5).guide,"Another view supplied snap geometry");
    view.x=500;view.y=200;
    require(drawing::snap_annotation(view,{10,25.2},.5).point==snapped.point,"Sheet placement changed view-local snapping");
    view.dimension_guide_count=1;require(drawing::annotation_guides(view).size()==4&&!drawing::snap_annotation(view,{10,28.2},.5).guide,"Removed guide remains available for snapping");
    drawing::ModelAnnotation annotation;annotation.visible=true;
    annotation.model_envelope.include({-100,-100,-100});annotation.model_envelope.include({100,100,100});
    view.model_annotations={annotation};view.camera=drawing::standard_camera(drawing::ViewOrientation::Isometric);
    const auto rectangle=drawing::annotation_guides(view);
    require(rectangle.size()==4,"Model envelope replaced the 2D guide rectangle");
    near(rectangle[0].first.x,-5);near(rectangle[0].first.y,-5);near(rectangle[1].second.x,25);near(rectangle[1].second.y,25);
    view.breaks={{"middle",false,2,6,2,drawing::BreakMark::None}};
    const auto shortened=drawing::annotation_guides(view);require(shortened.size()==4,"Break split the helper rectangle");
    near(drawing::break_paper(view,shortened[1].first).x,15);
    require(drawing::snap_annotation(view,drawing::break_paper(view,{15.1,10},true),.5).guide.has_value(),"Broken-view guide did not snap in displayed coordinates");
    view.breaks.clear();
    view.dimension_guide_count=0;require(drawing::annotation_guides(view).empty(),"Zero count still generates guides");
    auto names=drawing::DrawingDocument::create_default();names.sheets.front().views={view,view};
    names.sheets.front().views[0].name="Custom";names.sheets.front().views[1].name="Pohled 3";
    require(workspace::next_drawing_view_name(names)=="Pohled 4","Default view name collides with an existing name");
    const auto camera=drawing::standard_camera(drawing::ViewOrientation::Isometric);
    const auto rotated=drawing::rotated_camera(camera,17,-23,31);
    const auto dot=[](auto a,auto b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    require(std::abs(dot(rotated.depth,rotated.depth)-1)<1e-12&&std::abs(dot(rotated.horizontal,rotated.vertical))<1e-12,"Degree rotation damaged camera basis");
    const auto rolled=drawing::rotated_camera(camera,0,0,90);
    near(dot(rolled.depth,camera.depth),1);near(dot(rolled.horizontal,camera.vertical),-1);near(dot(rolled.vertical,camera.horizontal),1);
    const auto quarter=drawing::rotated_camera(camera,90,0);
    const auto expected=drawing::projected_camera(camera,drawing::ProjectionDirection::Right,drawing::ProjectionMethod::ThirdAngle);
    require(std::abs(dot(quarter.depth,expected.depth)-1)<1e-12,"Degree rotation differs from existing quarter turn");
}
void verify_projected_lengths() {
    drawing::DrawingView view;view.id="projected-length";
    drawing::MeasurementGeometry geometry;
    geometry.points={{{"part","a","one"},{0,0,0}},{{"part","b","one"},{10,20,30}}};
    view.measurement_geometry=drawing::share_measurement_geometry(geometry);
    auto d=drawing::make_drawing_dimension(view.id);
    d.attachments={{drawing::DimensionAttachmentKind::Point,{"part","a","one"}},
                   {drawing::DimensionAttachmentKind::Point,{"part","b","one"}}};
    drawing::resize_dimension_segments(d);
    for(auto orientation:{drawing::ViewOrientation::Front,drawing::ViewOrientation::Top,drawing::ViewOrientation::Isometric})
        for(double roll:{0.,37.,90.})for(double scale:{.5,2.}) {
            view.camera=drawing::rotated_camera(drawing::standard_camera(orientation),17,-23,roll);view.scale=scale;
            const auto h=view.camera.horizontal,v=view.camera.vertical;
            const double x=10*h.x+20*h.y+30*h.z,y=10*v.x+20*v.y+30*v.z;
            for(auto direction:{drawing::DimensionDirection::Horizontal,drawing::DimensionDirection::Vertical,drawing::DimensionDirection::Automatic}) {
                d.direction=direction;auto result=drawing::evaluate_drawing_dimension(view,d);
                require(result.state==drawing::MeasurementState::Resolved,"Oblique projected length unresolved");
                near(result.presentations[0].value,direction==drawing::DimensionDirection::Horizontal?std::abs(x):direction==drawing::DimensionDirection::Vertical?std::abs(y):std::hypot(x,y));
                view.x+=20;view.y-=15;near(drawing::evaluate_drawing_dimension(view,d).presentations[0].value,result.presentations[0].value);
            }
        }
}
int main(){try{
    verify_projected_lengths();verify_annotation_guides();kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-drawing-view-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify_projection_reuse(kernel,dir);verify_breaks(dir);verify(kernel,dir);verify_editing(kernel,dir);require(dir.parent_path()==root,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Drawing view snapshots, original references, parent-first regeneration, dimensions, native sources and deletion passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
