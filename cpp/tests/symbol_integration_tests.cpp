#include <zima/symbols/definition.hpp>
#include <zima/symbols/placement.hpp>
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
            if(definition.id=="ze:general-surface-texture:iso21920")instance.definition=symbols::Definition::load(root/"config/symbols/general/ZE-GENERAL-SURFACE-TEXTURE-ISO21920.symz").serialized();
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
        const auto historical_path=root/"config/symbols/surface-texture/ZE-SURFACE-TEXTURE-ISO1302-1978.symz";
        if(argc==1) {
            const auto historical=symbols::Definition::load(historical_path);
            check(historical.variants.size()==2,"Historical roughness must have two process variants");
            for(const auto* variant:{"any_process","material_removal"}) {
                const auto sketches=historical.evaluate(variant);std::size_t lines=0;for(const auto& sketch:sketches)lines+=sketch.segments.size();
                check(lines==(std::string(variant)=="any_process"?2:3),"Historical roughness is not ordinary editable segments");
                check(sketches.front().texts.front().drawing_keep_readable,"Historical roughness text is not marked readable");
                check(sketches.front().texts.front().value=="3,2","Historical default must omit Ra");
                for(const auto& value:historical.fields.at("Specification").choices) {
                    const auto sample=historical.evaluate(variant,{{"Specification",value}});
                    for(const auto& contour:sample.front().texts.front().contours)for(const auto& p:contour)
                        check(p[1]>3.031&&p[0]<p[1]*3.5/6.062-.2,"Historical text intersects the bar or long arm");
                }
            }
        }

        if(argc==2&&std::string(argv[1])=="--update-factory"){factory(root);return 0;}
        {
            // An arbitrary user-authored definition exercises the same policy;
            // no catalog ID or roughness-specific rendering is involved.
            symbols::Definition d;d.id="user:readable";d.name="User text";d.default_variant="default";
            auto sketch=sketcher::Sketch::create_default();sketch.id="readable-sketch";
            auto text=sketcher::Sketch::create_text();text.id="readable-text";text.value="Ra 3.2";text.modeling_geometry=false;
            text.anchor_x=4;text.anchor_y=9;text.angle_degrees=15;text.horizontal=sketcher::TextHorizontalAlignment::Center;
            text.vertical=sketcher::TextVerticalAlignment::Middle;text.drawing_keep_readable=true;
            sketcher::rebuild_text_contours(text,true);sketch.texts={text};
            sketch.points={{"p",0,0,true},{"q",5,3,true}};sketch.segments={{"segment","p","q"}};
            d.sketches={sketch};d.variants["default"].sketches={sketch.id};
            d=symbols::Definition::from_serialized(d.serialized());check(d.sketches.front().texts.front().drawing_keep_readable,"Text readability flag lost on save/reopen");
            sketcher::SymbolInstance instance;instance.id="instance";instance.definition=d.serialized();instance.variant="default";instance.scale=1.7;
            for(double total:{-450.,-270.,-180.,-90.001,-90.,-89.999,0.,89.999,90.,90.001,135.,180.,270.,450.}) {
                const double frame_angle=25;instance.angle_degrees=total-text.angle_degrees-frame_angle;
                const auto spatial=symbols::instance_mesh(instance),paper=symbols::instance_mesh(instance,{},frame_angle);
                check(spatial.edges.size()==paper.edges.size(),"Readability changed symbol topology");
                const double normalized=std::remainder(total,360.);const bool flip=normalized>90.||normalized<=-90.;
                double xmin=1e100,xmax=-1e100,ymin=1e100,ymax=-1e100;
                // The pivot is authored in symbol coordinates, then transformed with the instance.
                // A rotated axis-aligned bounding box has a different center for asymmetric glyphs.
                for(const auto& contour:text.contours)for(auto p:contour){xmin=std::min(xmin,p[0]);xmax=std::max(xmax,p[0]);ymin=std::min(ymin,p[1]);ymax=std::max(ymax,p[1]);}
                const double a=instance.angle_degrees*std::acos(-1.)/180.;
                const double cx=(xmin+xmax)*.5*instance.scale,cy=(ymin+ymax)*.5*instance.scale;
                const double pivot_x=instance.x+std::cos(a)*cx-std::sin(a)*cy,pivot_y=instance.y+std::sin(a)*cx+std::cos(a)*cy;
                for(std::size_t i=0;i<spatial.edges.size();++i)for(std::size_t j=0;j<spatial.edges[i].points.size();++j) {
                    const auto before=spatial.edges[i].points[j],after=paper.edges[i].points[j];
                    const bool turn=flip&&spatial.edges[i].filled_text;
                    check(std::abs(after.x-(turn?2*pivot_x-before.x:before.x))<1e-7&&std::abs(after.y-(turn?2*pivot_y-before.y:before.y))<1e-7,"Paper readability moved text center or symbol geometry");
                }
            }
            d.sketches.front().texts.front().drawing_keep_readable=false;instance.definition=d.serialized();
            const auto ordinary=symbols::instance_mesh(instance),unchanged=symbols::instance_mesh(instance,{},180);
            check(ordinary.edges.size()==unchanged.edges.size(),"Disabled readability changed geometry");
            for(std::size_t i=0;i<ordinary.edges.size();++i)check(ordinary.edges[i].points==unchanged.edges[i].points,"Disabled readability changed text");
        }
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
        for(const auto* asset:{"surface-texture/ZE-SURFACE-TEXTURE-ISO21920.symz","general/ZE-GENERAL-SURFACE-TEXTURE-ISO21920.symz","general/ZE-GENERAL-EDGES-ISO13715.symz"}) {
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
        const auto tolerance_root=root/"config/symbols/geometric-tolerances";int tolerance_count=0;
        for(const auto& entry:std::filesystem::directory_iterator(tolerance_root))if(entry.path().extension()==".symz") {
            const auto d=symbols::Definition::load(entry.path());check(d.frame_layout.has_value(),"Tolerance frame has fixed borders");++tolerance_count;
            sketcher::SymbolInstance i;i.id="frame-test";i.definition=d.serialized();i.variant=d.default_variant;
            const auto width=[&](const auto& instance){double a=1e100,b=-1e100;for(const auto& edge:symbols::instance_mesh(instance).edges)for(auto p:edge.points){a=std::min(a,p.x);b=std::max(b,p.x);}return b-a;};
            const auto initial=width(i);i.text_values["Tolerance"]="0.000000123456789";check(width(i)>initial+5,"Tolerance frame did not grow with text");
            if(d.fields.contains("Secondary datum")) {i.text_values["Secondary datum"]="B";const auto two=width(i);i.text_values["Tertiary datum"]="C";check(width(i)>=two+6.9,"Datum cells overlap or fail to grow");}
            const auto roundtrip=symbols::Definition::from_serialized(d.serialized());check(roundtrip.serialized()==d.serialized(),"Dynamic frame layout lost on save");
        }
        check(tolerance_count==14,"Geometric tolerance library incomplete");
        const auto bilateral=[&](const symbols::Definition& d,const std::string& variant) {
            symbols::Placement a;a.symbol.id="bilateral";a.symbol.definition=d.serialized();a.symbol.variant=variant;
            a.leader=true;a.symbol.x=40;a.symbol.y=15;
            auto b=a;b.symbol.id="bilateral-other";b.symbol.x=-40;
            const auto left=a.viewer_mesh(0.),right=b.viewer_mesh(0.);
            check(left.edges.size()==right.edges.size(),"Changing leader end changed symbol topology");
            for(std::size_t e=0;e<left.edges.size();++e) {
                check(left.edges[e].filled_text==right.edges[e].filled_text,"Leader end changed text semantics");
                if(e+2<left.edges.size()) {
                    check(left.edges[e].points.size()==right.edges[e].points.size(),"Leader end changed glyph size");
                    for(std::size_t i=0;i<left.edges[e].points.size();++i) {
                        const auto p=left.edges[e].points[i],q=right.edges[e].points[i];
                        check(std::abs(p.x-q.x-80)<1e-8&&std::abs(p.y-q.y)<1e-8,"Leader end mirrored glyphs or changed weld side");
                    }
                }
            }
            const auto& l=left.edges[left.edges.size()-2].points;
            const auto& r=right.edges[right.edges.size()-2].points;
            check(l.front()==a.frame.origin&&r.front()==b.frame.origin,"Arrow contact moved");
            check(l.back().x<40&&r.back().x>-40,"Leader did not join the nearest side");
            check(std::abs(l.back().y-15)<1e-8&&std::abs(r.back().y-15)<1e-8,"Leader did not meet frame centre/reference line");
            const auto saved=symbols::placements_json({a,b});check(symbols::placements_from_json(saved)==std::vector<symbols::Placement>{a,b},"Bilateral symbol did not round trip");
        };
        for(const auto& entry:std::filesystem::directory_iterator(tolerance_root))if(entry.path().extension()==".symz") {
            const auto d=symbols::Definition::load(entry.path());bilateral(d,d.default_variant);
        }
        int weld_count=0;
        for(const auto& entry:std::filesystem::directory_iterator(root/"config/symbols/welding"))if(entry.path().extension()==".symz") {
            const auto d=symbols::Definition::load(entry.path());check(d.reference_line_layout.has_value(),"Weld lacks reference-line layout");++weld_count;
            for(const auto& [variant,row]:d.variants) {
                bilateral(d,variant);
                const auto prefix=variant=="other_side"?"Other":"Arrow";
                const auto measure=[&](const auto& overrides){double left=1e100,right=-1e100;for(const auto& s:d.evaluate(variant,overrides))for(const auto& edge:s.viewer_mesh().edges) {
                    if(edge.reference.semantic_key.starts_with("sketch_axis:"))continue;
                    for(auto p:edge.points){left=std::min(left,p.x);right=std::max(right,p.x);}
                }return right-left;};
                const auto original=measure(std::map<std::string,std::string>{});
                check(measure(std::map<std::string,std::string>{{std::string(prefix)+" length","12 x 123456789 (123456789)"}})>original+10,"Weld reference line did not grow with text");
                auto invalid=d;invalid.reference_line_layout->columns.front().push_back("missing");rejects([&]{invalid.validate();});
            }
            check(symbols::Definition::from_serialized(d.serialized()).serialized()==d.serialized(),"Weld layout persistence changed");
        }
        check(weld_count==4,"Weld catalog incomplete");

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

