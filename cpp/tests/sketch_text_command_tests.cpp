#include <zima/command_host/host.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <iostream>
#include <stdexcept>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    command_host::Host host(live,kernel,directory,options);run(host,"new",{{"type","part"},{"name","native-text"}});const auto doc=live.active_document_id();
    const auto sketch=run(host,"sketch.create",{{"name","Text"}}).data.at("sketch").get<std::string>();auto* part=live.open_part(doc);
    const auto command=[&](const char* name,Json a=Json::object()){a["sketch"]=sketch;return run(host,name,std::move(a)).data;};
    const auto current=[&]{return workspace::document_sketch(live,doc,sketch);};
    const auto text=command("sketch.text.create",{{"value","0"},{"position",{10,20}},{"height_mm",3},{"color","white"},{"modeling_geometry",false}}).at("text").get<std::string>();
    const auto data=command("sketch.text.get",{{"text",text}});require(data.at("value")=="0" && data.at("font")=="osifont" && data.at("contour_count").get<int>()>=2 && current().texts.front().contours.size()>=2,"Native text command lost glyph holes");
    const auto original=current().texts.front();command("sketch.text.set",{{"text",text},{"value","Řez Ø10"},{"horizontal","center"},{"vertical","middle"},{"color","red"},{"flipped",true},{"angle_degrees",30},{"position",{20,30}},{"height_mm",4},{"modeling_geometry",true}});
    const auto updated=current().texts.front();require(updated.value=="Řez Ø10" && updated.height==4 && updated.flipped && updated.modeling_geometry && updated.horizontal==sketcher::TextHorizontalAlignment::Center && updated.color==sketcher::SketchTextColor::Red,"Text property patch lost a parameter");
    run(host,"undo");require(current().texts.front()==original,"Text property Undo changed original outlines");run(host,"redo");require(current().texts.front()==updated,"Text Redo changed outlines");
    const auto revision=part->session.revision();const auto* cached=part->session.calculated_boundaries().data();
    require(command("sketch.text.set",{{"text",text}}).at("changed")==false && part->session.revision()==revision && part->session.calculated_boundaries().data()==cached,"No-op text regenerated or committed");
    for(Json patch:std::vector<Json>{{{"height_mm",0}},{{"height_mm",-1}},{{"value",""}},{{"value",std::string(4097,'H')}},{{"horizontal","unknown"}},{{"vertical","unknown"}},{{"color","blue"}},{{"flipped","yes"}},{{"position",{1}}}}) {
        patch["sketch"]=sketch;patch["text"]=text;require(!host.execute({{"command","sketch.text.set"},{"arguments",patch}}).ok && current().texts.front()==updated && part->session.revision()==revision,"Rejected text partly changed the document");
    }
    command("sketch.geometry.delete",{{"geometry",text}});require(current().texts.empty(),"Native text deletion failed");run(host,"undo");require(current().texts.front()==updated,"Text deletion Undo lost its geometry");
    run(host,"save");const auto loaded=document::PartDocument::load(directory/"native-text.prtz");require(loaded.sketches.back().texts.front()==updated,"Part persistence changed text");
    run(host,"new",{{"type","assembly"},{"name","assembly-text"}});const auto assembly_sketch=run(host,"sketch.create",{{"name","Text"}}).data.at("sketch");
    run(host,"sketch.text.create",{{"sketch",assembly_sketch},{"value","Čert"},{"position",{2,-3}},{"height_mm",2}});run(host,"save");
    const auto assembly=assembly::AssemblyDocument::load(directory/"assembly-text.asmz");require(assembly.sketches.back().texts.front().value=="Čert" && !assembly.sketches.back().texts.front().contours.empty(),"Assembly lost native text");
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-text-command-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Text commands, Unicode, native glyphs, properties, atomicity, Undo and Part/Assembly persistence passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
