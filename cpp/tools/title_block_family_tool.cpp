#include <zima/workspace/template_operations.hpp>
#include <zima/workspace/template_object_operations.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/drawing/balloon.hpp>
#include <zima/sketcher/text_geometry.hpp>
#include <zima/drawing/drawing_template.hpp>
#include <zima/document/engineering_metadata.hpp>
#include <zima/kernel/stable_id.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <tuple>

using namespace zima;
namespace fs=std::filesystem;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
const std::array<std::string,5> languages{"CS","EN","DE","FR","RU"};
bool repeats(const sketcher::Sketch& s,const sketcher::SketchSegment& line) {
    const auto* a=s.find_point(line.first_point_id);const auto* b=s.find_point(line.second_point_id);
    for(const auto& r:s.drawing_template->repeat_regions) {
        const auto in=[&](const auto* p){return p->x>=r.x-1e-6&&p->x<=r.x+r.width+1e-6&&p->y>=r.y-1e-6&&p->y<=r.y+r.height+1e-6;};
        if(in(a)&&in(b))return true;
    }
    return false;
}
document::PartDocument make_family() {
    std::vector<sketcher::Sketch> sources;
    for(const auto& language:languages)sources.push_back(drawing::load_template_sketch(fs::path("tests/fixtures/drawing-library")/("ZE-TITLE-BLOCK-"+language+".tblz"),[](auto&){}));
    std::set<std::string> common;
    for(const auto& line:sources.front().segments) {
        if(repeats(sources.front(),line))continue;
        const auto* a=sources.front().find_point(line.first_point_id);const auto* b=sources.front().find_point(line.second_point_id);
        const bool shared=std::ranges::all_of(sources,[&](const auto& s){
            const auto found=std::ranges::find(s.segments,line.id,&sketcher::SketchSegment::id);
            if(found==s.segments.end()||repeats(s,*found))return false;
            const auto* x=s.find_point(found->first_point_id);const auto* y=s.find_point(found->second_point_id);
            return a->x==x->x&&a->y==x->y&&b->x==y->x&&b->y==y->y&&line.construction==found->construction&&line.centerline==found->centerline;
        });
        if(shared)common.insert(line.id);
    }
    check(!common.empty(),"No common title-block frame found");
    auto part=document::PartDocument::create_default();part.document_id="ze-title-block-family";part.name="ZE-TITLE-BLOCK";
    document::BodyHistoryGraph bodies;static_cast<void>(bodies.create_body("Title Block"));
    const auto add=[&](sketcher::Sketch sketch,const std::string& name,bool suppressed) {
        auto container=document::PartDocument::create_sketch_container();container.name=name;container.suppressed=suppressed;
        sketch.id="ze-title-block-sketch-"+name;sketch.name=name;sketch.owner_container_id=container.id;
        for(auto& region:sketch.drawing_template->repeat_regions)region.id=sketch.id+":"+region.id;
        for(auto& image:sketch.drawing_template->images)image.id=sketch.id+":"+image.id;
        sketch.drawing_template->sections["TitleBlock"]["Name"]=name;
        bodies.insert({document::PartHistoryKind::Feature,container.id});part.history.push_back(container);part.sketches.push_back(std::move(sketch));
    };
    auto base=sources.front();base.texts.clear();base.symbols.clear();base.drawing_template->field_ids.clear();base.drawing_template->images.clear();base.drawing_template->repeat_regions.clear();
    base.drawing_template->sections["TitleBlock"].erase("Locale");
    std::vector<std::string> remove;for(const auto& line:base.segments)if(!common.contains(line.id))remove.push_back(line.id);
    for(const auto& id:remove)base.remove_geometry(id);
    check(base.circles.empty()&&base.arcs.empty()&&base.bsplines.empty()&&base.ellipses.empty(),"Factory frame contains unsupported shared curves");
    std::set<std::string> used;for(const auto& line:base.segments){used.insert(line.first_point_id);used.insert(line.second_point_id);}
    std::erase_if(base.points,[&](const auto& p){return !used.contains(p.id);});
    base.constraints.clear();base.dimensions.clear();base.validate();add(std::move(base),"Frame",false);
    for(std::size_t i=0;i<languages.size();++i) {
        auto sketch=std::move(sources[i]);for(const auto& id:common)sketch.remove_geometry(id);
        add(std::move(sketch),languages[i],true);
    }
    part.set_body_history(bodies);
    document::FamilyTable table;
    for(const auto& container:part.history){table.columns.push_back(container.name);table.bindings[container.name]={"feature",container.id,{}};}
    for(const auto& language:languages) {
        document::FamilyInstance row;row.name=language;row.id="ze-title-block-variant-"+language;
        for(const auto& column:table.columns)row.values[column]=column=="Frame"||column==language?"yes":"no";
        table.instances.push_back(std::move(row));
    }
    document::validate_family_table(table,part.name);part.family_table=document::serialize_family_table(table);
    part.resolve_constructions();part.validate_body_ownership();return part;
}
document::PartDocument make_frames() {
    auto part=document::PartDocument::create_default();part.document_id="ze-drawing-frame-family";part.name="ZE-DRAWING-FRAME";
    document::BodyHistoryGraph bodies;static_cast<void>(bodies.create_body("Frame"));document::FamilyTable table;
    for(const auto* format:{"A4","A3","A2","A1","A0"}) {
        auto sketch=drawing::load_template_sketch(fs::path("tests/fixtures/drawing-library")/(std::string("ZE-")+format+".frmz"),[](auto& text){sketcher::rebuild_text_contours(text,true);});
        auto container=document::PartDocument::create_sketch_container();container.name=format;container.suppressed=std::string(format)!="A4";
        sketch.id=std::string("ze-frame-sketch-")+format;sketch.name=format;sketch.owner_container_id=container.id;
        bodies.insert({document::PartHistoryKind::Feature,container.id});part.history.push_back(container);part.sketches.push_back(std::move(sketch));
        table.columns.push_back(format);table.bindings[format]={"feature",container.id,{}};
    }
    part.set_body_history(bodies);
    for(const auto& column:table.columns) {
        document::FamilyInstance row;row.name=column;row.id="ze-frame-variant-"+column;
        for(const auto& name:table.columns)row.values[name]=name==column?"yes":"no";
        table.instances.push_back(std::move(row));
    }
    document::validate_family_table(table,part.name);part.family_table=document::serialize_family_table(table);
    part.resolve_constructions();part.validate_body_ownership();return part;
}
void verify_frames_and_bom(const fs::path& directory) {
    const auto frames=document::PartDocument::load("config/formats/ZE-DRAWING-FRAME.frmz");const auto frame_path=directory/"ZE-DRAWING-FRAME.frmz";frames.save(frame_path);
    workspace::Workspace live;auto pin=document::PartDocument::create_default(),plate=document::PartDocument::create_default();
    live.add_part(pin);live.add_part(plate);auto assembly=assembly::AssemblyDocument::create_default();kernel::BodyResult empty;
    auto a=assembly::AssemblyDocument::create_part_occurrence("Pin",pin.document_id,{},empty);a.occurrence_id="a";
    auto b=a;b.occurrence_id="b";auto c=assembly::AssemblyDocument::create_part_occurrence("Plate",plate.document_id,{},empty);c.occurrence_id="c";
    assembly.components={a,b,c};live.add_assembly(assembly);
    auto doc=drawing::DrawingDocument::create_default();doc.source_document_id=assembly.document_id;auto& sheet=doc.sheets.front();
    workspace::load_drawing_template(doc,sheet.id,directory/"ZE-TITLE-BLOCK.tblz",true,&live,{},"ze-title-block-variant-CS");
    check(sheet.bom_rows.size()==2&&sheet.bom_rows.front().quantity==2,"Assembly Drawing lost grouped BOM quantities");
    kernel::ViewerMesh mesh;mesh.edges={{{{0,0,0},{20,0,0}},{"pin-solid","edge","1:a"}},{{{0,10,0},{20,10,0}},{"plate-solid","edge","1:c"}}};
    auto view=drawing::DrawingDocument::create_view(assembly.document_id,{},mesh,drawing::ViewOrientation::Top);
    view.camera={{1,0,0},{0,1,0},{0,0,1}};drawing::refresh_view_geometry(view,mesh);sheet.views.push_back(view);
    drawing::show_all_balloons(sheet,view.id);check(sheet.balloons.size()==2,"Automatic positions missing from Assembly Drawing");
    const auto balloons=sheet.balloons;
    for(const auto& row:document::parse_family_table(frames.family_table).instances) {
        workspace::load_drawing_template(doc,sheet.id,frame_path,false,nullptr,{},row.id);
        drawing::DrawingSheet legacy;legacy.format=sheet.format;drawing::load_frame_template(legacy,fs::path("tests/fixtures/drawing-library")/("ZE-"+row.name+".frmz"));
        check(sheet.frame_lines.size()==legacy.frame_lines.size()&&sheet.frame_texts.size()==legacy.frame_texts.size(),"Frame family geometry differs from original format");
        check(sheet.title_block_variant=="ze-title-block-variant-CS"&&sheet.balloons==balloons,"Frame replacement changed title block or positions");
    }
    for(const auto& language:languages) {
        workspace::set_title_block_variant(doc,sheet.id,"ze-title-block-variant-"+language);
        const auto layout=drawing::title_block_layout(sheet,{});
        const auto number=std::ranges::find_if(sheet.title_block_texts,[](const auto& text){return text.text=="&bom.item_number";});
        check(number!=sheet.title_block_texts.end(),"Title block lacks the automatic item-number expression");
        const auto binding=sheet.title_block_repeat_bindings.at("texts").at(number-sheet.title_block_texts.begin());
        const auto region=std::ranges::find(sheet.repeat_regions,binding,&sketcher::SketchRepeatRegion::id);
        check(region!=sheet.repeat_regions.end(),"Item-number expression has no repeat region");
        for(const auto& balloon:sheet.balloons) {
            const auto row=std::ranges::find(sheet.bom_rows,balloon.item_number,&drawing::BomRow::item_number);
            check(row!=sheet.bom_rows.end(),"Balloon references a missing BOM row");
            auto position=number->position;const double offset=(row-sheet.bom_rows.begin())*region->step;
            if(region->direction=="up")position.y+=offset;else if(region->direction=="down")position.y-=offset;
            else if(region->direction=="left")position.x+=offset;else position.x-=offset;
            const bool matched=!balloon.unresolved&&std::ranges::any_of(layout.texts,[&](const auto& text){
                return text.text==std::to_string(balloon.item_number)&&text.position==position;});
            check(matched,"Automatic position does not match the title-block BOM");
        }
        check(sheet.balloons==balloons,"Language variant changed automatic positions");
    }
    doc.save(directory/"assembly-drawing.drwz");auto reopened=drawing::DrawingDocument::load(directory/"assembly-drawing.drwz");
    check(reopened.sheets.front().balloons==balloons&&reopened.sheets.front().frame_definition==sheet.frame_definition,"Drawing persistence lost frame or positions");
    workspace::set_frame_variant(reopened,sheet.id,"ze-frame-variant-A4");
    check(reopened.sheets.front().format==drawing::SheetFormat::A4&&reopened.sheets.front().balloons==balloons,"Embedded frame replacement failed");
}
void verify(const document::PartDocument& part,const fs::path& directory) {
    const auto path=directory/"ZE-TITLE-BLOCK.tblz";part.save(path);
    auto reopened=document::PartDocument::load(path);check(reopened.sketches.size()==6,"Native title block lost Sketches");
    check(reopened.body_history.bodies().size()==1&&!reopened.body_history.active_body_id().empty(),"Title block must open with an active Body");
    for(const auto& sketch:reopened.sketches)check(reopened.body_owner_for_object(sketch.owner_container_id)!=nullptr,"Title-block Sketch is outside a Body");
    workspace::Workspace live;const auto id=workspace::open_drawing_template(live,path);
    check(live.open_part(id)->native_drawing_template&&workspace::family_references(live,id).size()>=6,"Native editor or Sketch presence is unavailable");
    auto text=live.open_part(id)->session.document().sketches.at(1);text.texts.front().value="Edited";
    check(workspace::commit_template_sketch(live,id,text),"Language Sketch edit failed");
    check(live.open_part(id)->session.undo(),"Language Sketch Undo failed");
    const auto table=document::parse_family_table(part.family_table);
    for(std::size_t i=0;i<languages.size();++i) {
        auto doc=drawing::DrawingDocument::create_default();const auto sheet_id=doc.sheets.front().id;
        workspace::load_drawing_template(doc,sheet_id,path,true,nullptr,{},table.instances[i].id);
        auto& sheet=doc.sheets.front();sheet.bom_rows.resize(3);
        for(std::size_t n=0;n<sheet.bom_rows.size();++n){sheet.bom_rows[n].item_number=int(n+1);sheet.bom_rows[n].quantity=2;}
        const auto result=drawing::title_block_layout(sheet,{});
        drawing::DrawingSheet legacy;legacy.bom_rows=sheet.bom_rows;
        drawing::load_title_block_template(legacy,fs::path("tests/fixtures/drawing-library")/("ZE-TITLE-BLOCK-"+languages[i]+".tblz"));
        const auto expected=drawing::title_block_layout(legacy,{});
        check(result.lines.size()==expected.lines.size(),"Family variant changed frame/repeated line count");
        check(result.texts.size()==expected.texts.size(),"Family variant changed text/BOM count");
        const auto names=[](const auto& texts){std::multiset<std::string> values;for(const auto& t:texts)values.insert(t.text);return values;};
        check(names(result.texts)==names(expected.texts),"Family variant changed localized text or parameter values");
        const auto geometry=[](const auto& lines){
            std::multiset<std::tuple<double,double,double,double,drawing::DrawingPen,bool>> values;
            for(const auto& line:lines)values.emplace(line.first.x,line.first.y,line.second.x,line.second.y,line.pen,line.centerline);
            return values;
        };
        check(geometry(result.lines)==geometry(expected.lines),"Family variant changed line coordinates or styles");
        const auto typography=[](const auto& texts){
            std::multiset<std::tuple<std::string,double,double,double,double,bool,std::string,std::string,std::string,drawing::DrawingPen>> values;
            for(const auto& t:texts)values.emplace(t.text,t.position.x,t.position.y,t.height,t.angle,t.flipped,t.font,t.alignment,t.vertical_alignment,t.pen);
            return values;
        };
        check(typography(result.texts)==typography(expected.texts),"Family variant changed text placement or orientation");
        doc.save(directory/"inserted.drwz");auto saved=drawing::DrawingDocument::load(directory/"inserted.drwz");
        check(saved.sheets.front().title_block_definition==sheet.title_block_definition&&saved.sheets.front().title_block_variant==table.instances[i].id,
            "Drawing lost embedded definition or selected variant");
        workspace::set_title_block_variant(saved,sheet_id,table.instances[(i+1)%5].id);
        check(saved.sheets.front().title_block_variant==table.instances[(i+1)%5].id,"Embedded variant switch failed");
    }
    // A region in a language Sketch must not repeat the separate base Sketch.
    auto scoped=drawing::DrawingDocument::create_default();const auto sheet_id=scoped.sheets.front().id;
    workspace::load_drawing_template(scoped,sheet_id,path,true,nullptr,{},table.instances.front().id);
    check(scoped.sheets.front().title_block_repeat_bindings.at("lines").front().empty(),"Base frame inherited a language repeat region");
    verify_frames_and_bom(directory);
}
}
int main(int argc,char** argv){try{
    const auto part=argc==2&&fs::path(argv[1]).extension()==".frmz"?make_frames():make_family();
    if(argc==2&&std::string(argv[1])=="--verify") {
        const auto directory=fs::temp_directory_path()/("zima-title-family-"+kernel::make_stable_id());fs::create_directory(directory);
        verify(document::PartDocument::load("config/formats/ZE-TITLE-BLOCK.tblz"),directory);std::cout<<"Native six-Sketch title block and five Family variants verified at "<<directory<<'\n';
    } else {
        check(argc==2,"Specify the output tblz path or --verify");part.save(fs::path(argv[1]));
        std::cout<<"Wrote native Part title block with five shared-name variants\n";
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
