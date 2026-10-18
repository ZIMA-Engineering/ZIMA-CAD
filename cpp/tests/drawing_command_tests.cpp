#include "profile_solid_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <zima/workspace/document_operations.hpp>
#include <zima/workspace/family_operations.hpp>
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
    const auto frame=fs::absolute("tests/fixtures/drawing-library/ZE-A4.frmz"),title=fs::absolute("tests/fixtures/drawing-library/ZE-TITLE-BLOCK-EN.tblz");
    run(host,"drawing.frame.load",{{"sheet",first},{"path",document::path_to_utf8(frame)}});run(host,"drawing.title_block.load",{{"sheet",first},{"path",document::path_to_utf8(title)}});
    require(!state->document().find_sheet(first)->frame_lines.empty()&&!state->document().find_sheet(first)->title_block_texts.empty(),"Templates did not embed geometry");
    const auto bad=dir/"bad.frmz";{std::ofstream out(bad);out<<"[Format]\nSheetFormat=A4\n[FrameGeometry]\nLine1=invalid\n";}
    rejected("drawing.frame.load",{{"sheet",first},{"path",document::path_to_utf8(bad)}});
    rejected("drawing.frame.load",{{"sheet",second},{"path",document::path_to_utf8(frame)}});
    require(!state->document().find_sheet(first)->frame_lines.empty(),"Failed template load erased existing frame");
    auto next=state->document();auto* decorated=next.find_sheet(first);decorated->title_block_circles.push_back({{1,2},3});auto& image=decorated->title_block_images.emplace_back();image.id="retained-image";image.format="svg";image.pixel_width=image.pixel_height=1;image.data_base64="PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxIiBoZWlnaHQ9IjEiPjwvc3ZnPg==";decorated->repeat_regions.emplace_back().id="retained-region";state->commit(std::move(next));
    run(host,"drawing.title_block.clear",{{"sheet",first}});require(state->document().find_sheet(first)->title_block_images.empty()&&state->document().find_sheet(first)->repeat_regions.empty()&&state->document().find_sheet(first)->title_block_circles.empty(),"Clearing title block left images, circles or regions");run(host,"undo");
    rejected("drawing.sheet.set",{{"sheet",first},{"format","A2"}});
    run(host,"drawing.frame.clear",{{"sheet",first}});
    run(host,"drawing.sheet.set",{{"sheet",first},{"format","A2"}});const auto* clean=state->document().find_sheet(first);require(clean->frame_lines.empty()&&!clean->title_block_images.empty()&&!clean->repeat_regions.empty(),"Format change erased the retained title block");
    run(host,"drawing.sheet.delete",{{"sheet",second}});require(state->document().sheets.size()==1,"Sheet deletion failed");rejected("drawing.sheet.delete",{{"sheet",first}});run(host,"undo");require(state->document().find_sheet(second),"Sheet deletion Undo lost its identity");
    run(host,"save");const auto path=state->path;const auto loaded=drawing::DrawingDocument::load(path);require(loaded.sheets.size()==2&&loaded.find_sheet(first)->format==drawing::SheetFormat::A2&&loaded.find_sheet(second)->default_scale==2,"Sheet settings did not persist");
    // Saving an earlier history state must not hide new identifier allocations.
    auto job=workspace::prepare_document_save(live,id,path);next=state->document();drawing::DrawingDimension dimension;dimension.id="retired-test";next.sheets.front().dimensions.push_back(dimension);state->commit(std::move(next));run(host,"undo");
    require(workspace::complete_document_save(live,job.write())&&state->is_dirty(),"Concurrent Undo and dimension allocation fooled save completion");
    const auto allocated=state->document().dimension_identifiers.identifier(id,"dimension:retired-test");require(!allocated.empty(),"Undo lost retired dimension allocation");
    next=state->document();dimension.id="new-test";next.sheets.front().dimensions.push_back(dimension);state->commit(std::move(next));require(!state->can_redo()&&state->document().dimension_identifiers.identifier(id,"dimension:new-test")!=allocated,"New history branch reused a dimension identifier");
}
void embedded_formats(const kernel::OcctKernel& kernel,fs::path dir) {
    auto doc=drawing::DrawingDocument::create_default();const auto sid=doc.sheets.front().id;
    auto source=document::PartDocument::create_default();source.history={zima::test::rectangular_feature(source,{10,10,10})};
    const auto boundary=kernel.evaluate_history(source.kernel_operations()).back();
    auto view=drawing::DrawingDocument::create_view(source.document_id,dir/"unavailable-format-source.prtz",boundary.mesh);
    view.x=83;view.y=73;view.scale=.6;view.use_sheet_scale=false;
    const auto curve=std::ranges::find_if(view.projected_edges,[](const auto& edge){return edge.points.size()>1&&
        std::hypot(edge.points.back().x-edge.points.front().x,edge.points.back().y-edge.points.front().y)>5;});
    require(curve!=view.projected_edges.end(),"Format fixture has no real measuring curve");
    const auto reference=Json{{"owner",curve->source.owner_id},{"key",curve->source.semantic_key},{"instance_path",curve->source.instance_path}};
    doc.sheets.front().views.push_back(view);
    workspace::load_drawing_template(doc,sid,fs::absolute("config/formats/ZE-DRAWING-FRAME.frmz"),false,nullptr,{},"ze-frame-variant-A4");
    workspace::load_drawing_template(doc,sid,fs::absolute("config/formats/ZE-TITLE-BLOCK.tblz"),true,nullptr,{},"ze-title-block-variant-CS");
    doc.sheets.front().local_parameters["custom-note"]="Retain authored title data";
    workspace::Workspace live;live.add_drawing(doc,dir/"embedded-formats.drwz");live.activate(doc.document_id);live.display_top_level(doc.document_id);
    command_host::Host host(live,kernel,dir);auto* state=live.open_drawing(doc.document_id);
    run(host,"drawing.dimension.create",{{"view",view.id},{"kind","linear"},{"attachments",Json::array({
        Json{{"kind","curve_point"},{"reference",reference},{"parameter",0}},
        Json{{"kind","curve_point"},{"reference",reference},{"parameter",1}}})},{"placements",Json::array({Json::array({0,-10})})}});
    const auto bytes=[](const fs::path& path){std::ifstream input(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});};
    for(const auto* from:{"A4","A3","A2","A1","A0"})for(const auto* to:{"A4","A3","A2","A1","A0"}) {
        run(host,"drawing.sheet.set",{{"sheet",sid},{"format",from}});
        const auto before=state->document();const auto revision=state->revision();
        run(host,"drawing.sheet.set",{{"sheet",sid},{"format",to}});
        const auto after=state->document();const auto& sheet=after.sheets.front();
        require(!sheet.frame_lines.empty()&&sheet.frame_variant==std::string("ze-frame-variant-")+to,"Sheet format did not select its embedded Family frame");
        auto normalized=after;auto& comparable=normalized.sheets.front();const auto& original=before.sheets.front();
        comparable.format=original.format;comparable.frame_lines=original.frame_lines;comparable.frame_texts=original.frame_texts;
        comparable.frame_circles=original.frame_circles;comparable.frame_trimming_marks=original.frame_trimming_marks;comparable.frame_variant=original.frame_variant;
        before.save(dir/"before-format.drwz");normalized.save(dir/"after-format.drwz");
        require(bytes(dir/"before-format.drwz")==bytes(dir/"after-format.drwz"),"Format change altered title-block geometry, fields, data, anchor or other sheet contents");
        if(std::string(from)!=to) {
            require(state->revision()==revision+1,"Format change did not create exactly one transaction");
            run(host,"undo");require(state->document().sheets.front().frame_variant==std::string("ze-frame-variant-")+from,"Format Undo lost the old frame");
            run(host,"redo");require(state->document().sheets.front().frame_variant==sheet.frame_variant,"Format Redo lost the new frame");
        } else require(state->revision()==revision,"Unchanged format created a transaction");
        run(host,"save");const auto loaded=drawing::DrawingDocument::load(state->path);
        require(loaded.sheets.front().frame_variant==sheet.frame_variant&&loaded.sheets.front().title_block_definition==doc.sheets.front().title_block_definition,
            "Save/reopen lost the selected frame or embedded title block");
    }
    for(bool duplicate:{false,true}) {
        auto rejected=state->document();auto& value=rejected.sheets.front();
        auto definition=document::PartDocument::from_serialized(Json::parse(value.frame_definition));
        auto table=document::parse_family_table(definition.family_table);
        if(duplicate) {
            auto row=table.instances.front();row.id="duplicate-a4";row.name="Duplicate A4";table.instances.push_back(row);
        } else std::erase_if(table.instances,[](const auto& row){return row.id=="ze-frame-variant-A4";});
        definition.family_table=document::serialize_family_table(table);value.frame_definition=definition.serialized().dump();
        auto settings=workspace::sheet_settings(value);settings.format=drawing::SheetFormat::A4;
        const auto frame=value.frame_variant,title=value.title_block_definition,embedded=value.frame_definition;
        bool failed=false;try{workspace::set_drawing_sheet(rejected,sid,settings);}catch(const workspace::DrawingOperationError& error){failed=error.code=="frame_variant_unavailable";}
        require(failed&&value.format==drawing::SheetFormat::A0&&value.frame_variant==frame&&
            value.title_block_definition==title&&value.frame_definition==embedded,"Missing/ambiguous embedded Family variant partially changed the sheet");
    }
    std::cout<<"25 embedded format pairs: Family selection, unchanged title block/data/anchor, Undo/Redo and persistence passed\n";
}
void projections(const kernel::OcctKernel& kernel,fs::path dir){
    auto part=document::PartDocument::create_default();auto box=zima::test::rectangular_feature(part,{10,10,10});part.history={box};auto boundaries=kernel.evaluate_history(part.kernel_operations());const auto mesh=boundaries.back().mesh;
    {
        auto skeleton=part;skeleton.document_id=document::PartDocument::create_default().document_id;
        part.save(dir/"real.prtz",boundaries);skeleton.save(dir/"layout_SKELETON.PRTZ",boundaries);
        auto inner=assembly::AssemblyDocument::create_default();
        inner.components={assembly::AssemblyDocument::create_part_occurrence("real",part.document_id,dir/"real.prtz",boundaries.back()),
            assembly::AssemblyDocument::create_part_occurrence("layout",skeleton.document_id,dir/"layout_SKELETON.PRTZ",boundaries.back())};
        inner.save(dir/"inner.asmz");
        auto outer=assembly::AssemblyDocument::create_default();
        outer.components.push_back(assembly::AssemblyDocument::create_assembly_occurrence("inner",inner.document_id,dir/"inner.asmz",inner));
        outer.save(dir/"outer.asmz");
        workspace::Workspace sources;sources.add_assembly(inner,dir/"inner.asmz");sources.add_assembly(outer,dir/"outer.asmz");
        for(const auto* source:{&inner,&outer})for(bool open:{false,true}) {
            const auto path=dir/(source==&inner?"inner.asmz":"outer.asmz");
            const auto drawing_mesh=workspace::read_drawing_source(open?&sources:nullptr,path,source->document_id).second;
            require(drawing_mesh.triangles.size()==mesh.triangles.size(),"Skeleton geometry entered an Assembly drawing");
            require(source->build_scene().triangles.size()==2*mesh.triangles.size(),"Drawing filtering changed the Assembly scene");
            for(const auto& edge:drawing_mesh.edges)require(edge.reference.instance_path.find(inner.components[1].occurrence_id)==std::string::npos,"Skeleton edge remained pickable in a Drawing");
        }
    }
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
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-drawing-command-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);projections(kernel,dir);embedded_formats(kernel,dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Drawing sheets, templates, atomic errors, history, native persistence and paper hatch pitch passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
