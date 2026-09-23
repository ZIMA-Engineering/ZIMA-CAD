#include <zima/symbols/definition.hpp>
#include <zima/drawing/drawing_template.hpp>
#include <zima/workspace/native_documents.hpp>
#include <zima/workspace/template_operations.hpp>
#include <zima/sketcher/text_geometry.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <algorithm>
#include <cmath>
using namespace zima;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid symbol accepted");}
sketcher::SymbolInstance projection() {
    const auto definition=symbols::projection_method();sketcher::SymbolInstance i;
    i.id="test-projection";i.definition=definition.serialized();i.variant=definition.default_variant;i.use_cad_variant=true;return i;
}
void factory(const std::filesystem::path& root) {
    for(const auto* language:{"CS","EN","DE","FR","RU"}) {
        const auto path=root/"config/formats"/(std::string("ZE-TITLE-BLOCK-")+language+".tblz");
        auto sketch=drawing::load_template_sketch(path,[](auto& text){sketcher::rebuild_text_contours(text,true);});
        std::erase_if(sketch.points,[](const auto& v){return v.id.starts_with("projection:");});
        std::erase_if(sketch.segments,[](const auto& v){return v.id.starts_with("projection:");});
        std::erase_if(sketch.constraints,[](const auto& v){return v.id.starts_with("projection:");});
        std::erase_if(sketch.dimensions,[](const auto& v){return v.id.starts_with("projection:");});
        std::erase_if(sketch.circles,[](const auto& v){return v.id.starts_with("projection:");});
        std::erase_if(sketch.drawing_template->pens,[](const auto& v){return v.first.starts_with("projection:");});
        auto symbol=projection();symbol.id="ze:title-block:projection";symbol.x=45.8;symbol.y=42.5;symbol.scale=.5;
        std::erase_if(sketch.symbols,[&](const auto& v){return v.id==symbol.id;});sketch.symbols.push_back(symbol);
        for(auto& instance:sketch.symbols) {
            const auto definition=symbols::Definition::from_serialized(instance.definition);
            if(definition.id=="ze:general-edges:iso13715")instance.definition=symbols::Definition::load(root/"config/symbols/general/ZE-GENERAL-EDGES-ISO13715.symz").serialized();
            if(definition.id=="ze:general-surface-texture:iso21920")instance.definition=symbols::Definition::load(root/"config/symbols/surface-texture/ZE-GENERAL-SURFACE-TEXTURE-ISO21920.symz").serialized();
        }
        for(auto& text:sketch.texts)if(text.id=="field10:text"||text.id=="field11:text") {
            text.value=text.id=="field10:text"?"ISO 2768-m":"ISO 8015:2011";
            sketcher::rebuild_text_contours(text,true);
            auto& field=sketch.drawing_template->sections[text.id=="field10:text"?"Field.ACCURACY":"Field.TOLERANCING"];
            field["Text"]=text.value;field["Default"]=text.value;field["WriteBack"]="no";
            sketch.drawing_template->sections["TextAction."+text.id]={{"kind","none"}};
            if(text.id=="field10:text") {
                const std::vector<std::string> choices{"ISO 2768-f","ISO 2768-m","ISO 2768-c","ISO 2768-v"};
                sketch.drawing_template->sections["TextAction."+text.id]={{"kind","list"},{"choices",nlohmann::json(choices).dump()},{"allow_custom","yes"}};
            }
        }
        drawing::save_template_sketch(sketch,path);
    }
    for(const auto* filename:{"START_PART.prtz","START_SKELETON.prtz"}) {
        const auto path=root/"config/templates"/filename;auto part=document::PartDocument::load(path);
        for(const auto* key:{"general_tolerance","tolerancing"}){part.user_parameters.erase(key);part.user_parameter_values.erase(key);part.user_parameter_labels.erase(key);std::erase(part.user_parameter_order,key);}
        part.save(path);
    }
    const auto path=root/"config/templates/START_ASSEMBLY.asmz";auto assembly=assembly::AssemblyDocument::load(path);
    for(const auto* key:{"general_tolerance","tolerancing"}){assembly.user_parameters.erase(key);assembly.user_parameter_values.erase(key);assembly.user_parameter_labels.erase(key);std::erase(assembly.user_parameter_order,key);}
    assembly.save(path);
}
}
int main(int argc,char** argv) {
    try {
        const auto root=std::filesystem::current_path();
        if(argc==2&&std::string(argv[1])=="--update-factory"){factory(root);return 0;}
        for(const auto* language:{"CS","EN","DE","FR","RU"}) {
            drawing::DrawingSheet sheet;drawing::load_title_block_template(sheet,root/"config/formats"/(std::string("ZE-TITLE-BLOCK-")+language+".tblz"));
            for(const auto* id:{"ACCURACY","TOLERANCING"}) {
                const auto f=std::ranges::find(sheet.title_block_fields,id,&drawing::TitleBlockField::id);
                check(f!=sheet.title_block_fields.end()&&f->editable&&!f->write_back&&f->expression.empty(),"Tolerance field still writes a model parameter");
                check(f->value==(std::string(id)=="ACCURACY"?"ISO 2768-m":"ISO 8015:2011"),"Factory tolerance default differs");
                if(std::string(id)=="ACCURACY")check(nlohmann::json::parse(f->action_settings.at("choices")).size()==4&&f->action_settings.at("allow_custom")=="yes","Tolerance choices are incomplete");
            }
            const auto layout=drawing::title_block_layout(sheet,{});
            check(std::ranges::all_of(layout.lines,[](const auto& line){return line.field_id!="symbol:ze:title-block:projection"||line.pen==drawing::DrawingPen::Green;}),"Projection outline must be thin green");
            const auto i=std::ranges::find(sheet.title_block_symbols,"ze:title-block:projection",&sketcher::SymbolInstance::id);
            check(i!=sheet.title_block_symbols.end()&&i->x==45.8,"Projection did not move right in title-block coordinates");
        }
        const auto directory=std::filesystem::temp_directory_path()/("zima-symbols-"+document::PartDocument::create_default().document_id);
        std::filesystem::create_directory(directory);
        auto symbol=projection();symbol.x=20;symbol.y=10;
        auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(0,0,10,10));
        auto part=document::PartDocument::create_default();
        auto container=document::PartDocument::create_extrusion_container(sketch.id);sketch.owner_container_id=container.id;
        part.history.push_back(container);part.sketches.push_back(sketch);
        document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Symbol test"));graph.insert({document::PartHistoryKind::Feature,container.id});part.set_body_history(graph);part.resolve_constructions();
        kernel::OcctKernel kernel;const auto before=kernel.evaluate_history(part.kernel_operations());
        part.sketches.front().symbols.push_back(symbol);
        const auto after=kernel.evaluate_history(part.kernel_operations());
        check(!before.empty()&&before.size()==after.size(),"Annotation changed calculated bodies");
        check(std::abs(before.back().volume-after.back().volume)<1e-9,"Symbol changed Extrusion volume");
        check(before.back().mesh.triangles==after.back().mesh.triangles,"Symbol altered Extrusion tessellation");
        part.save(directory/"part.prtz",after);
        const auto reopened=document::PartDocument::load(directory/"part.prtz");
        check(reopened.sketches.front().symbols==part.sketches.front().symbols,"Part lost embedded symbol");
        auto definition=symbols::projection_method();
        auto text=sketcher::Sketch::create_text();text.value="Ra 3.2";text.modeling_geometry=false;sketcher::rebuild_text_contours(text,true);
        const auto text_id=text.id;definition.sketches.front().texts.push_back(text);
        definition.fields["roughness"]={definition.sketches.front().id,text_id,{"Ra 3.2","Ra 6.3"},false};
        definition.variants.at("first_angle").text_values["roughness"]="Ra 3.2";
        check(definition.evaluate("first_angle",{{"roughness","Ra 6.3"}}).front().texts.front().value=="Ra 6.3","Text override ignored");
        rejects([&]{static_cast<void>(definition.evaluate("first_angle",{{"roughness","bad"}}));});
        definition.variants.at("first_angle").hidden_texts={"roughness"};
        check(definition.evaluate("first_angle").front().texts.empty(),"Hidden field remained visible");
        check(definition.evaluate("third_angle").front().texts.empty(),"Text leaked from hidden Sketch");
        auto invalid=definition;invalid.sketches.front().plane_offset=2;rejects([&]{invalid.validate();});
        invalid=definition;invalid.sketches.front().symbols.push_back(symbol);rejects([&]{invalid.validate();});
        invalid=definition;invalid.pens[invalid.sketches.front().id]["missing-curve"]="yellow";rejects([&]{invalid.validate();});
        auto title=drawing::create_template_sketch(true,"Symbols");title.symbols.push_back(symbol);
        drawing::save_template_sketch(title,directory/"title.tblz");
        auto drawing=drawing::DrawingDocument::create_default();drawing::load_title_block_template(drawing.sheets.front(),directory/"title.tblz");
        check(drawing.sheets.front().title_block_lines.empty(),"Symbol was baked into fixed template geometry");
        const auto first=drawing::title_block_layout(drawing.sheets.front(),{});check(!first.lines.empty(),"Embedded symbol not rendered");
        check(std::abs(first.lines.front().first.x-symbol.x-7.2)<1e-9,"First-angle cone is mirrored by paper coordinates");
        for(const auto* frame:{"ZE-A0.frmz","ZE-A4.frmz"}) {
            drawing.sheets.front().format=std::string(frame)=="ZE-A0.frmz"?drawing::SheetFormat::A0:drawing::SheetFormat::A4;
            drawing::load_frame_template(drawing.sheets.front(),root/"config/formats"/frame);
            const auto resized=drawing::title_block_layout(drawing.sheets.front(),{});
            check(resized.lines.size()==first.lines.size(),"Frame replacement changed the title-block symbol");
            for(std::size_t i=0;i<first.lines.size();++i) {
                const auto& a=first.lines[i];const auto& b=resized.lines[i];
                check(a.first.x==b.first.x&&a.first.y==b.first.y&&a.second.x==b.second.x&&a.second.y==b.second.y,
                    "Frame replacement moved the right-anchored title block");
            }
        }
        drawing.sheets.front().projection_method=drawing::ProjectionMethod::ThirdAngle;
        const auto third=drawing::title_block_layout(drawing.sheets.front(),{});
        check(first.lines.front().first.x!=third.lines.front().first.x,"Projection did not follow owning sheet");
        drawing.save(directory/"drawing.drwz");std::filesystem::remove(directory/"title.tblz");
        const auto reopened_drawing=drawing::DrawingDocument::load(directory/"drawing.drwz");
        check(reopened_drawing.sheets.front().title_block_symbols==drawing.sheets.front().title_block_symbols,"Drawing needs source template to reopen");
        check(drawing::title_block_layout(reopened_drawing.sheets.front(),{}).lines.size()==third.lines.size(),"Reopened symbol did not render");
        auto manual=drawing.sheets.front();manual.title_block_symbols.front().use_cad_variant=false;
        check(drawing::title_block_layout(manual,{}).lines.front().first.x==first.lines.front().first.x,"Manual variant followed CAD settings");
        for(const auto* asset:{"surface-texture/ZE-SURFACE-TEXTURE-ISO21920.symz","surface-texture/ZE-GENERAL-SURFACE-TEXTURE-ISO21920.symz","general/ZE-GENERAL-EDGES-ISO13715.symz"}) {
            const auto definition=symbols::Definition::load(root/"config/symbols"/asset);
            auto instance=symbol;instance.use_cad_variant=false;instance.definition=definition.serialized();instance.variant=definition.default_variant;
            drawing::DrawingSheet sheet;sheet.title_block_symbols={instance};const auto layout=drawing::title_block_layout(sheet,{});
            std::size_t expected_texts=0;for(const auto& sketch:definition.evaluate(definition.default_variant))expected_texts+=sketch.texts.size();
            check(layout.texts.size()==expected_texts,"Symbol text not rendered as readable template text");
            check(layout.texts.front().flipped,"Title-block text is mirrored");
            std::size_t expected_lines=0;
            for(const auto& edge:symbols::instance_mesh(instance).edges)
                if(!edge.filled_text && !edge.points.empty())expected_lines+=edge.points.size()-1;
            check(layout.lines.size()==expected_lines,"Text outlines were stroked as template lines");
            if(definition.id=="ze:surface-texture:iso21920"||definition.id=="ze:general-surface-texture:iso21920") {
                const bool local=definition.id=="ze:surface-texture:iso21920";
                check(std::ranges::all_of(layout.lines,[](const auto& line){return line.pen==drawing::DrawingPen::Yellow;}),"Surface texture strokes must be yellow");
                check(std::ranges::all_of(layout.texts,[&](const auto& text){return text.pen==drawing::DrawingPen::Green;}),"Surface texture text pen differs");
                for(const auto& [variant,row]:definition.variants) {
                    auto candidate=instance;candidate.variant=variant;
                    for(const auto& edge:symbols::instance_mesh(candidate).edges)
                        check(edge.color==(edge.filled_text?"#4DD811":"#F5CD50"),"Surface texture mesh lost its text/stroke color");
                }
            }
            if(definition.id=="ze:general-edges:iso13715") {
                check(std::ranges::count_if(layout.lines,[](const auto& line){return line.pen==drawing::DrawingPen::Yellow;})==8,"Edge leaders/reference lines did not retain thin yellow pens");
                check(drawing::drawing_pen_width_mm(sheet,drawing::DrawingPen::Yellow)==sheet.thin_line_mm,"Yellow leader is not thin on paper");
                check(std::ranges::all_of(layout.texts,[](const auto& text){return text.pen==drawing::DrawingPen::Green;}),"Edge specification text must be green");
                check(std::ranges::none_of(layout.lines,[](const auto& line){return line.pen==drawing::DrawingPen::White;}),"Edge symbol retained a thick white stroke");
            }
            check(std::ranges::all_of(layout.lines,[&](const auto& line){return line.field_id=="symbol:"+instance.id;}),"Symbol strokes have no common editing identity");
            for(const auto& [key,row]:definition.variants)check(!definition.evaluate(key).empty(),"Catalog variant has no sketches");
        }
        workspace::Workspace live;auto carrier=workspace::template_part_from_sketch(title,"Symbols");const auto id=carrier.document_id;
        live.add_part(std::move(carrier),{},directory/"undo.tblz");auto* state=live.open_part(id);const auto original=state->session.document().sketches.front();
        auto edited=original;edited.symbols.front().x=99;
        check(workspace::commit_template_sketch(live,id,edited),"Symbol edit did not commit");
        check(state->session.undo(),"Symbol edit has no Undo");check(state->session.document().sketches.front().symbols==original.symbols,"Undo lost symbol");
        check(state->session.redo(),"Symbol edit has no Redo");check(state->session.document().sketches.front().symbols==edited.symbols,"Redo lost symbol");
        std::filesystem::remove_all(directory);
        std::cout<<"Symbol embedding, variants, text choices, CAD binding, profile isolation and Undo/Redo passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

