#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_balloon_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/workspace/drawing_view_operations.hpp>
#include <zima/kernel/stable_id.hpp>
#include <iostream>
using namespace zima;
using commands::Json;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
Json run(command_host::Host& host,const char* command,Json args=Json::object()){
    auto r=host.execute({{"command",command},{"arguments",args}});if(!r.ok)throw std::runtime_error(std::string(command)+": "+r.message);return r.data;
}
void verify(std::filesystem::path dir) {
    kernel::OcctKernel kernel;workspace::Workspace live;
    auto part=document::PartDocument::create_default();part.name="Pin";live.add_part(part);
    auto sub=assembly::AssemblyDocument::create_default();sub.name="Subassembly";live.add_assembly(sub);
    auto assembly=assembly::AssemblyDocument::create_default();assembly.name="Top";
    kernel::BodyResult empty;
    auto a=assembly::AssemblyDocument::create_part_occurrence("Pin A",part.document_id,{},empty);a.occurrence_id="a";
    auto b=a;b.occurrence_id="ab";b.name="Pin B";
    auto nested=assembly::AssemblyDocument::create_part_occurrence("Subassembly",sub.document_id,{},empty);nested.occurrence_id="s";
    auto skeleton=a;skeleton.occurrence_id="reference";skeleton.source_path="reference_skeleton.prtz";
    assembly.components={a,b,nested,skeleton};live.add_assembly(assembly);
    auto bom=workspace::build_bom_rows_for_source(assembly.document_id,{},&live);
    check(bom.size()==2&&bom[0].quantity==2&&bom[0].occurrence_paths==std::vector<std::string>{"1:a","2:ab"},"Grouped BOM lost exact occurrences");
    const kernel::EdgeReference pin{"pin-solid","edge","1:a"},pin2{"pin-solid","edge","2:ab"},leaf{"leaf-solid","edge","1:s4:leaf"};
    kernel::ViewerMesh mesh;mesh.edges={{{{0,0,0},{20,0,0}},pin},{{{0,10,0},{20,10,0}},pin2},{{{0,20,0},{20,20,0}},leaf}};
    auto doc=drawing::DrawingDocument::create_default();auto& sheet=doc.sheets.front();
    auto view=drawing::DrawingDocument::create_view(assembly.document_id,{},mesh,drawing::ViewOrientation::Top);
    view.camera={{1,0,0},{0,1,0},{0,0,1}};drawing::refresh_view_geometry(view,mesh);
    sheet.views={view};sheet.bom_rows=bom;sheet.bom_source_document_id=assembly.document_id;
    check(drawing::balloon_bom_row(sheet,view,leaf)==&sheet.bom_rows[1],"Nested leaf must label its immediate subassembly");
    check(!drawing::balloon_bom_row(sheet,view,{"leaf-solid","edge","1:x4:leaf"}),"Unknown occurrence resolved to another item");
    auto unrelated=view;unrelated.source_document_id="other";
    check(!drawing::balloon_bom_row(sheet,unrelated,pin),"Unrelated view reused the sheet BOM");
    auto family_variant=view;family_variant.source_document_id=assembly.document_id+":family:row";
    check(drawing::balloon_bom_row(sheet,family_variant,pin)==&sheet.bom_rows[0],
        "Assembly variant view did not resolve the sheet BOM occurrence");
    auto component_view=view;component_view.source_document_id=part.document_id;
    check(drawing::balloon_bom_row(sheet,component_view,{"pin-solid","edge",{}})==&sheet.bom_rows[0],
        "Independent component view did not resolve its sheet BOM row");
    auto absent=sheet;absent.bom_rows.erase(absent.bom_rows.begin());
    check(!drawing::balloon_bom_row(absent,component_view,{"pin-solid","edge",{}}),
        "Component absent from the selected sheet variant received a position");
    const auto document_id=doc.document_id,view_id=view.id,sheet_id=sheet.id;
    live.add_drawing(doc);live.display_top_level(doc.document_id);live.activate(doc.document_id);command_host::Host host(live,kernel,dir);
    auto args=Json{{"view",view_id},{"reference",{{"owner",pin.owner_id},{"key",pin.semantic_key},{"instance_path",pin.instance_path}}},{"parameter",.25},{"position",{30,25}}};
    const auto made=run(host,"drawing.balloon.create",args);const auto id=made.at("balloon").get<std::string>();
    check(made["item_number"]==1&&!made["unresolved"].get<bool>()&&made["text_height"]==5,"Manual balloon lost BOM or default text height");
    run(host,"drawing.balloon.show_all",{{"view",view_id}});
    auto rows=run(host,"drawing.balloon.list")["items"];
    check(rows.size()==2&&rows[1]["item_number"]==2,"Show all must generate one balloon per first-level BOM row");
    {
        auto pending=live.open_drawing(document_id)->document().sheets[0];
        pending.balloons[0].attachment.reference=leaf;drawing::refresh_balloons(pending);
        drawing::show_all_balloons(pending,view_id);check(pending.balloons.size()==3,"Show all did not add the newly unlabelled row");
        for(std::size_t i=0;i+1<pending.balloons.size();++i)check(std::hypot(pending.balloons[i].position.x-pending.balloons.back().position.x,pending.balloons[i].position.y-pending.balloons.back().position.y)>=20,"Automatic balloon overlaps a retained circle");
    }
    const auto revision=live.open_drawing(document_id)->revision();run(host,"drawing.balloon.show_all",{{"view",view_id}});
    check(live.open_drawing(document_id)->revision()==revision,"Repeated Show all added duplicate undo state");
    run(host,"drawing.balloon.erase_all",{{"view",view_id}});
    check(!run(host,"drawing.balloon.get",{{"balloon",id}})["visible"].get<bool>(),"Erase all deleted or failed to hide a balloon");
    run(host,"undo");check(run(host,"drawing.balloon.get",{{"balloon",id}})["visible"].get<bool>(),"Undo failed to restore visibility");
    args["reference"]["instance_path"]="1:x";
    const auto rejected=host.execute({{"command","drawing.balloon.create"},{"arguments",args}});
    check(!rejected.ok&&rejected.code=="unresolved_reference","Invalid BOM reference accepted");
    doc=live.open_drawing(document_id)->document();auto& s=doc.sheets.front();s.bom_rows[0].item_number=7;drawing::refresh_balloons(s);
    check(s.balloons[0].item_number==7&&s.balloons[0].last_anchor==drawing::Point2{5,0},"BOM renumber or curve attachment drifted");
    auto moved=mesh;for(auto& edge:moved.edges)for(auto& p:edge.points)p.y+=5;
    drawing::refresh_view_geometry(s.views[0],moved);drawing::refresh_balloons(s);
    check(s.balloons[0].last_anchor==drawing::Point2{5,5},"Regenerated attachment did not follow its original curve");
    s.views[0].measurement_geometry=drawing::share_measurement_geometry({});drawing::refresh_balloons(s);
    check(s.balloons[0].unresolved&&s.balloons[0].last_anchor==drawing::Point2{5,5},"Broken balloon did not retain repair presentation");
    const auto file=dir/"balloons.drwz";doc.save(file);auto loaded=drawing::DrawingDocument::load(file);
    check(loaded.sheets[0].balloons==s.balloons&&loaded.sheets[0].bom_source_document_id==assembly.document_id&&loaded.sheets[0].bom_rows[0].occurrence_paths==bom[0].occurrence_paths,"Native balloon round trip lost references");
    drawing::refresh_view_geometry(loaded.sheets[0].views[0],mesh);auto repaired=loaded.sheets[0].balloons[0];repaired.attachment.reference=leaf;
    workspace::edit_drawing_balloon(loaded,sheet_id,repaired,false);
    check(!loaded.sheets[0].balloons[0].unresolved&&loaded.sheets[0].balloons[0].item_number==2,"Balloon reattachment did not follow its new subassembly item");
    auto child=view;child.id="child";child.parent_view_id=view_id;loaded.sheets[0].views.push_back(child);
    auto child_balloon=loaded.sheets[0].balloons[0];child_balloon.id="child-balloon";child_balloon.view_id=child.id;loaded.sheets[0].balloons.push_back(child_balloon);
    workspace::delete_drawing_view(loaded,view_id);check(loaded.sheets[0].balloons.empty(),"View deletion retained orphaned balloons");
    auto part_bom=workspace::build_bom_rows_for_source(part.document_id,{},&live);check(part_bom.size()==1&&part_bom[0].occurrence_paths==std::vector<std::string>{""},"Part BOM missing root binding");
}
}
int main(){try{
    const auto root=std::filesystem::canonical(std::filesystem::temp_directory_path());const auto dir=root/("zima-balloons-"+kernel::make_stable_id());std::filesystem::create_directory(dir);
    verify(dir);check(std::filesystem::canonical(dir).parent_path()==root,"Invalid test cleanup path");std::filesystem::remove_all(dir);
    std::cout<<"Drawing balloon BOM, history, repair and persistence checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
