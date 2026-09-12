#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <zima/workspace/document_operations.hpp>
#include <zima/document/file_path.hpp>
#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()){
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path dir){
    workspace::Workspace live;command_host::Host host(live,kernel,dir);
    run(host,"new",{{"type","drawing"},{"name","Výkres"}});const auto id=live.active_document_id();auto* state=live.open_drawing(id);
    const auto first=run(host,"drawing.sheet.list").data.at("items")[0].at("sheet").get<std::string>();
    require(run(host,"drawing.sheet.get",{{"sheet",first}}).data.at("width_mm")==210,"A4 width is not 210 mm");
    const auto added=run(host,"drawing.sheet.create",{{"name","Řez druhý"},{"format","A3"},{"scale",2},{"projection","third_angle"}}).data;
    const auto second=added.at("sheet").get<std::string>();require(second!=first && state->document().sheets.size()==2,"Sheet creation lost identity");
    run(host,"save");const auto saved_revision=state->revision();
    require(!state->is_dirty(),"Drawing save remained dirty");
    const auto change=run(host,"drawing.sheet.set",{{"sheet",second},{"locale","de"},{"thin_line_mm",.35}}).data;
    require(change.at("changed")==true && state->revision()==saved_revision+1,"Sheet patch did not commit exactly once");
    const auto revision=state->revision();require(run(host,"drawing.sheet.set",{{"sheet",second},{"locale","de"}}).data.at("changed")==false&&state->revision()==revision,"No-op sheet edit created history");
    run(host,"undo");require(!state->is_dirty() && state->document().find_sheet(second)->title_block_locale=="cs","Undo failed to restore saved sheet state");run(host,"redo");
    const auto rejected=[&](const char* command,Json args){const auto rev=state->revision(),gen=state->data_generation();require(!host.execute({{"command",command},{"arguments",std::move(args)}}).ok,"Invalid drawing command accepted");require(state->revision()==rev&&state->data_generation()==gen,"Rejected drawing command committed data");};
    rejected("drawing.sheet.set",{{"sheet",second},{"scale",0}});rejected("drawing.sheet.set",{{"sheet",second},{"format","A5"}});rejected("drawing.sheet.create",{{"name",""}});rejected("drawing.sheet.set",{{"sheet",second},{"red_line_mm",3}});rejected("drawing.sheet.delete",{{"sheet","missing"}});
    const auto frame=fs::absolute("config/formats/ZE-A4.frmz"),title=fs::absolute("config/formats/ZE-TITLE-BLOCK.tblz");
    run(host,"drawing.frame.load",{{"sheet",first},{"path",document::path_to_utf8(frame)}});run(host,"drawing.title_block.load",{{"sheet",first},{"path",document::path_to_utf8(title)}});
    require(!state->document().find_sheet(first)->frame_lines.empty()&&!state->document().find_sheet(first)->title_block_texts.empty(),"Templates did not embed geometry");
    const auto bad=dir/"bad.frmz";{std::ofstream out(bad);out<<"[Format]\nSheetFormat=A4\n[FrameGeometry]\nLine1=invalid\n";}
    rejected("drawing.frame.load",{{"sheet",first},{"path",document::path_to_utf8(bad)}});
    rejected("drawing.frame.load",{{"sheet",second},{"path",document::path_to_utf8(frame)}});
    require(!state->document().find_sheet(first)->frame_lines.empty(),"Failed template load erased existing frame");
    auto next=state->document();auto* decorated=next.find_sheet(first);decorated->title_block_circles.push_back({{1,2},3});decorated->title_block_images.emplace_back();decorated->repeat_regions.emplace_back();state->commit(std::move(next));
    run(host,"drawing.title_block.clear",{{"sheet",first}});require(state->document().find_sheet(first)->title_block_images.empty()&&state->document().find_sheet(first)->repeat_regions.empty()&&state->document().find_sheet(first)->title_block_circles.empty(),"Clearing title block left images, circles or regions");run(host,"undo");
    run(host,"drawing.sheet.set",{{"sheet",first},{"format","A2"}});const auto* clean=state->document().find_sheet(first);require(clean->frame_lines.empty()&&clean->title_block_images.empty()&&clean->repeat_regions.empty(),"Format change kept incompatible template objects");
    run(host,"drawing.sheet.delete",{{"sheet",second}});require(state->document().sheets.size()==1,"Sheet deletion failed");rejected("drawing.sheet.delete",{{"sheet",first}});run(host,"undo");require(state->document().find_sheet(second),"Sheet deletion Undo lost its identity");
    run(host,"save");const auto path=state->path;const auto loaded=drawing::DrawingDocument::load(path);require(loaded.sheets.size()==2&&loaded.find_sheet(first)->format==drawing::SheetFormat::A2&&loaded.find_sheet(second)->default_scale==2,"Sheet settings did not persist");
    // Saving an earlier history state must not hide new identifier allocations.
    auto job=workspace::prepare_document_save(live,id,path);next=state->document();drawing::DrawingDimension dimension;dimension.id="retired-test";next.sheets.front().dimensions.push_back(dimension);state->commit(std::move(next));run(host,"undo");
    require(workspace::complete_document_save(live,job.write())&&state->is_dirty(),"Concurrent Undo and dimension allocation fooled save completion");
    const auto allocated=state->document().dimension_identifiers.identifier(id,"dimension:retired-test");require(!allocated.empty(),"Undo lost retired dimension allocation");
    next=state->document();dimension.id="new-test";next.sheets.front().dimensions.push_back(dimension);state->commit(std::move(next));require(!state->can_redo()&&state->document().dimension_identifiers.identifier(id,"dimension:new-test")!=allocated,"New history branch reused a dimension identifier");
}
void projections(const kernel::OcctKernel& kernel,fs::path dir){
    auto part=document::PartDocument::create_default();auto box=document::PartDocument::create_box_container();box.box={10,10,10};part.history={box};auto boundaries=kernel.evaluate_history(part.kernel_operations());const auto mesh=boundaries.back().mesh;
    workspace::Workspace live;live.add_part(part,boundaries,dir/"source.prtz");auto doc=drawing::DrawingDocument::create_default();const auto sheet=doc.sheets.front().id;
    auto section=document::create_section();static_cast<void>(section.sketch.add_segment(-20,0,20,0));
    auto view=drawing::DrawingDocument::create_view(part.document_id,dir/"source.prtz",mesh);view.section_id=section.id;view.section_snapshot=section;
    for(const auto& patch:document::calculate_section(mesh,section).patches)view.section_snapshot->components[patch.component]={0,{0,1.2,.1,0},true};
    drawing::refresh_view_geometry(view,mesh);const auto view_id=view.id;doc.sheets.front().views.push_back(view);
    auto custom=view;custom.id="custom-scale";custom.use_sheet_scale=false;custom.scale=.5;drawing::refresh_view_geometry(custom,mesh);doc.sheets.front().views.push_back(custom);
    live.add_drawing(doc,dir/"projection.drwz");live.activate(doc.document_id);live.display_top_level(doc.document_id);command_host::Host host(live,kernel,dir);
    const auto source_revision=live.open_part(part.document_id)->session.revision();run(host,"drawing.sheet.set",{{"sheet",sheet},{"scale",2}});
    const auto& result=*live.open_drawing(doc.document_id)->document().find_view(view_id);std::vector<double> levels;
    for(const auto& edge:result.projected_edges)if(edge.hatch){require(std::abs(edge.points.front().y-edge.points.back().y)*2<1e-6,"Hatch angle changed");levels.push_back(edge.points.front().y*2);}
    std::ranges::sort(levels);levels.erase(std::unique(levels.begin(),levels.end(),[](double a,double b){return std::abs(a-b)<1e-6;}),levels.end());require(levels.size()>3,"No section hatch lines after sheet scale edit");
    for(std::size_t i=1;i<levels.size();++i)require(std::abs(levels[i]-levels[i-1]-1.2)<1e-6,"Sheet scale changed the 1.2 mm paper hatch pitch");
    require(live.open_drawing(doc.document_id)->document().find_view(custom.id)->scale==.5&&live.open_part(part.document_id)->session.revision()==source_revision,"Sheet edit changed independent scale or source document");
    live.remove(part.document_id);const auto rev=live.open_drawing(doc.document_id)->revision();
    require(run(host,"drawing.sheet.get",{{"sheet",sheet}}).data.at("scale")==2,"Sheet query required a source file");
    require(!host.execute({{"command","drawing.sheet.set"},{"arguments",{{"sheet",sheet},{"scale",3}}}}).ok&&live.open_drawing(doc.document_id)->revision()==rev&&live.open_drawing(doc.document_id)->document().find_view(view_id)->scale==2,"Missing source partially changed sheet scale");
    run(host,"undo");require(live.open_drawing(doc.document_id)->document().find_view(view_id)->scale==1,"Undo reloaded a missing source");
    part.save(dir/"source.prtz",boundaries);require(workspace::read_drawing_source(&live,dir/"source.prtz",part.document_id).first==part.document_id,"Closed native source unavailable");
    auto other=part;other.document_id="wrong-source";other.save(dir/"source.prtz",boundaries);bool mismatch=false;try{workspace::read_drawing_source(&live,dir/"source.prtz",part.document_id);}catch(const workspace::DrawingOperationError& e){mismatch=e.code=="source_identity";}require(mismatch,"Wrong source identity accepted");
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-drawing-command-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);projections(kernel,dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Drawing sheets, templates, atomic errors, history, native persistence and paper hatch pitch passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
