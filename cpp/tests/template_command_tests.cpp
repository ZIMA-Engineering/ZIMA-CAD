#include <zima/command_host/host.hpp>
#include <zima/workspace/template_operations.hpp>
#include <zima/drawing/drawing_template.hpp>
#include <zima/sketcher/text_geometry.hpp>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Json request(const char* command,Json args=Json::object()){return {{"command",command},{"arguments",std::move(args)}};}
std::string path_text(const fs::path& p){const auto text=p.generic_u8string();return {text.begin(),text.end()};}
void verify(const kernel::OcctKernel& kernel,fs::path directory,bool title) {
    workspace::Workspace live;command_host::Interaction interaction;command_host::Options options;options.interaction=[&]{return interaction;};
    command_host::Host host(live,kernel,directory,options);
    const auto run=[&](const char* command,Json args=Json::object()){const auto result=host.execute(request(command,std::move(args)));if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.code+": "+result.message);return result.data;};
    const std::string kind=title?"title_block":"drawing_format",suffix=title?".tblz":".frmz",name=title?"Šablona razítka":"Šablona rámečku";
    const auto created=run("template.new",{{"kind",kind},{"name",name}});const std::string id=created.at("document"),sketch_id=created.at("sketch");
    const auto file=directory/fs::u8path(name+suffix);auto* state=live.open_part(id);
    const auto current=[&](){return workspace::drawing_template_sketch(live,id);};
    require(current().drawing_template->kind==kind&&state->session.calculated_boundaries().empty()&&!fs::exists(file),"Template creation calculated or saved a body");
    const auto revision=state->session.revision();require(run("template.get").at("sketch")==sketch_id&&state->session.revision()==revision&&!host.change(),"Template query changed model");
    const auto initial=current();
    const auto operations=Json::array({request("sketch.segment.create",{{"first",{0,0}},{"second",{-12,0}}}),
        request("sketch.circle.create",{{"center",{-6,5}},{"radius_mm",2}}),
        request("sketch.text.create",{{"value","Žluťoučký &bom.quantity"},{"position",{-4,3}},{"height_mm",2.123456789}})});
    const auto made=run("template.sketch.edit",{{"operations",operations}});const auto edited=current();
    require(made.at("changed")==true&&made.at("body_calculated")==false&&made.at("results").size()==3&&state->session.revision()==revision+1,"Template batch did not commit once");
    require(edited.points.size()==4&&edited.segments.size()==1&&edited.circles.size()==1&&edited.texts.size()==1,"Template batch omitted geometry");
    const auto& label=edited.texts.front();auto expected=label;sketcher::rebuild_text_contours(expected,true);
    require(!label.modeling_geometry&&label.flipped&&!label.contours.empty()&&label.contours==expected.contours,"Template text uses wrong coordinate system or modeling contours");
    run("undo");require(current().serialized()==initial.serialized(),"Template batch Undo was not atomic");run("redo");require(current().serialized()==edited.serialized(),"Template Redo changed IDs");
    const auto move=Json::array({request("sketch.point.move",{{"point",edited.points.front().id},{"position",{2,0}}})});
    const auto rejected=[&](const char* command,Json args,const char* code,int index=-1){const auto before=current().serialized();const auto rev=state->session.revision(),generation=state->session.data_generation();const auto count=live.size();
        const auto result=host.execute(request(command,std::move(args)));if(result.ok||result.code!=code)throw std::runtime_error(std::string(command)+" expected "+code+", got "+result.code+": "+result.message);
        if(index>=0)require(result.data.at("operation_index")==index,"Batch failure lost operation index");
        require(current().serialized()==before&&state->session.revision()==rev&&state->session.data_generation()==generation&&live.size()==count&&!host.change(),"Rejected template operation mutated workspace");};
    rejected("box.create",{{"length_mm","1"},{"width_mm","2"},{"height_mm","3"}},"unsupported_document");
    rejected("template.sketch.edit",{{"operations",Json::array()}},"invalid_arguments");
    auto invalid=move;invalid.push_back(request("save"));rejected("template.sketch.edit",{{"operations",invalid}},"unknown_command",1);
    invalid=move;invalid.push_back(request("sketch.point.move",{{"point","missing"},{"position",{0,0}}}));rejected("template.sketch.edit",{{"operations",invalid}},"point_not_found",1);
    invalid=move;invalid.push_back(request("sketch.reference.create",{{"kind","edge"},{"owner","part"},{"key","edge"}}));rejected("template.sketch.edit",{{"operations",invalid}},"invalid_reference",1);
    invalid=move;invalid.front()["arguments"]["sketch"]="other";rejected("template.sketch.edit",{{"operations",invalid}},"invalid_arguments",0);
    invalid=move;invalid.push_back({{"command",23}});rejected("template.sketch.edit",{{"operations",invalid}},"invalid_request",1);
    rejected("template.new",{{"kind",kind},{"name",name}},"path_in_use");
    rejected("template.new",{{"kind",kind},{"name","../bad"}},"invalid_name");
    rejected("template.save",{{"path","wrong.prtz"}},"unsupported_format");
    rejected("template.save",{{"path",path_text(file)},{"copy",true}},"invalid_path");
    rejected("template.open",{{"path","missing"+suffix}},"template_rejected");
    interaction.editing=true;interaction.template_document=true;interaction.active_sketch=sketch_id;
    rejected("template.sketch.edit",{{"operations",move}},"editing_in_progress");
    interaction.template_editor_ready=true;run("template.sketch.edit",{{"operations",move}});
    const auto moved=current();const auto moved_revision=state->session.revision(),generation=state->session.data_generation();
    require(run("template.sketch.edit",{{"operations",move}}).at("changed")==false&&state->session.revision()==moved_revision&&state->session.data_generation()==generation&&!host.change(),"No-op template batch added history");
    rejected("new",{{"type","part"},{"name","Blocked"}},"editing_in_progress");
    require(run("template.open",{{"path",path_text(file)}}).at("document")==id&&current().serialized()==moved.serialized(),"Open replaced unsaved template edits");
    run("template.save");require(fs::is_regular_file(file)&&!state->session.is_dirty(),"Template Save did not mark saved");
    const auto alias=directory/fs::u8path(name+" alias"+suffix);fs::create_hard_link(file,alias);
    rejected("template.save",{{"path",path_text(alias)},{"copy",true},{"overwrite",true}},"invalid_path");
    require(run("template.open",{{"path",path_text(alias)}}).at("document")==id&&live.size()==1,"A physical file alias opened a duplicate template");
    run("undo");require(state->session.is_dirty(),"Template Undo lost saved-state marker");run("redo");
    const auto copy=directory/fs::u8path(name+" kopie"+suffix);
    require(run("template.save",{{"path",path_text(copy)},{"copy",true}}).at("paths")==Json::array({path_text(copy)}),"Copy result lost saved paths");require(state->path==file,"Save Copy replaced source path");
    rejected("template.save",{{"path",path_text(copy)},{"copy",true}},"path_exists");
    run("template.save",{{"path",path_text(copy)},{"copy",true},{"overwrite",true}});
    run("close");interaction={};require(live.size()==0,"Template did not close after saving");
    const auto reopened=run("template.open",{{"path",path_text(file)}});const auto loaded=workspace::drawing_template_sketch(live,reopened.at("document").get<std::string>());
    require(loaded.id==sketch_id&&loaded.points==moved.points&&loaded.segments==moved.segments&&loaded.circles==moved.circles&&loaded.texts==moved.texts,"Template native reload changed geometry or text");
    require(workspace::drawing_template_sketch(live,run("template.open",{{"path",path_text(copy)}}).at("document").get<std::string>()).serialized()!=std::string{},"Template Copy did not reopen");
}
}
int main(){try{const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-template-command-"+document::PartDocument::create_default().document_id);require(fs::create_directory(dir),"Cannot create directory");kernel::OcctKernel kernel;verify(kernel,dir,false);verify(kernel,dir,true);require(dir.parent_path()==root,"Invalid cleanup root");fs::remove_all(dir);std::cout<<"Template lifecycle, geometry, atomic batches, text, guards, files and Undo/Redo passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
