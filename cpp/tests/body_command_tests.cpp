#include <zima/command_host/host.hpp>
#include <zima/workspace/body_operations.hpp>
#include <zima/workspace/document_operations.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const std::string& command,Json args=Json::object()) {
    auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(command+": "+result.code+": "+result.message);return result;
}
void volume(const workspace::PartState& state,double expected) {
    const auto actual=state.session.calculated_boundaries().back().volume;
    if(std::abs(actual-expected)>1e-6)throw std::runtime_error("Expected volume "+std::to_string(expected)+", got "+std::to_string(actual));
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    command_host::Interaction interaction;options.interaction=[&]{return interaction;};
    command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","bodies"}});const auto id=live.active_document_id();
    auto* state=live.open_part(id);
    const auto first=state->session.document().body_history.active_body_id();
    const auto first_box=run(host,"box.create",{{"length_mm","10"},{"width_mm","10"},{"height_mm","10"}}).data.at("container").get<std::string>();
    volume(*state,1000);
    const auto created=run(host,"body.create",{{"name","Tool"}}).data;
    const auto tool=created.at("body").get<std::string>();
    require(created.at("placement").at("x")==0 && created.at("placement").at("references").size()==5,"New Body is not fully attached to the Part origin");
    for(const auto& ref:created.at("placement").at("references"))require(ref.at("owner_id")==id+":origin","New Body references another object");
    const auto tool_box=run(host,"box.create",{{"length_mm","4"},{"width_mm","4"},{"height_mm","4"}}).data.at("container").get<std::string>();
    require(state->session.document().body_history.owner(first_box)->scope.id==first && state->session.document().body_history.owner(tool_box)->scope.id==tool,"Features crossed Body ownership");
    const auto before=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    const auto info=run(host,"body.list").data;run(host,"body.get",{{"body",tool}});
    require(info.at("items").size()==2 && state->session.revision()==before && state->session.calculated_boundaries().data()==cache,"Body query changed calculated data");
    require(run(host,"body.set",{{"body",tool},{"name","Tool"}}).data.at("changed")==false && state->session.calculated_boundaries().data()==cache,"No-op Body properties recalculated");
    require(!host.execute({{"command","body.set"},{"arguments",{{"body",tool},{"name","   "}}}}).ok && state->session.revision()==before,"Empty name partly committed");
    require(!host.execute({{"command","body.set"},{"arguments",{{"body",tool},{"visible","false"}}}}).ok && state->session.revision()==before,"String bypassed typed visibility");
    run(host,"body.set",{{"body",tool},{"name","Tool renamed"},{"visible",false}});
    require(!state->session.document().body_history.find(tool)->visible,"Visibility not persisted");
    run(host,"undo");require(state->session.document().body_history.find(tool)->name=="Tool","Body edit did not participate in shared Undo");
    run(host,"redo");run(host,"body.set",{{"body",tool},{"visible",true}});
    const auto original_graph=state->session.document().body_history;
    const auto cut=run(host,"body.boolean.create",{{"operation","subtract"},{"target",first},{"tool",tool}}).data.at("boolean").get<std::string>();
    volume(*state,936);require(state->session.document().body_history.active_body_id().empty(),"Boolean did not finish Body editing");
    require(state->session.document().body_history.find(first)->entries==original_graph.find(first)->entries &&
        state->session.document().body_history.find(tool)->entries==original_graph.find(tool)->entries,"Boolean replaced source history");
    const auto revision=state->session.revision();const auto* boolean_cache=state->session.calculated_boundaries().data();
    run(host,"body.boolean.get",{{"boolean",cut}});
    require(run(host,"body.boolean.set",{{"boolean",cut},{"operation","subtract"}}).data.at("changed")==false && state->session.calculated_boundaries().data()==boolean_cache,"Boolean no-op recalculated");
    require(!host.execute({{"command","body.boolean.set"},{"arguments",{{"boolean",cut},{"tool",first}}}}).ok && state->session.revision()==revision,"Invalid Boolean inputs partly committed");
    require(!host.execute({{"command","body.boolean.create"},{"arguments",{{"operation","add"},{"target",first},{"tool",tool}}}}).ok && state->session.revision()==revision,"Consumed results reused by another Boolean");
    run(host,"body.boolean.set",{{"boolean",cut},{"operation","intersect"}});volume(*state,64);
    run(host,"undo");volume(*state,936);run(host,"redo");volume(*state,64);
    run(host,"body.boolean.set",{{"boolean",cut},{"operation","add"}});volume(*state,1000);
    require(state->session.document().find_container(first_box)->id==first_box && state->session.document().find_container(tool_box)->id==tool_box,"Boolean changed original feature identities");
    // Exercise the exact same body placement transaction as the GUI. The
    // persisted origin-plane offset drives the resulting position.
    auto edit=workspace::prepare_body_edit(state->session.document(),tool);
    auto moved=*edit.pending.find(tool);moved.scope.placement.references.at(2).offset=20;
    require(workspace::commit_body_edit(live,kernel,edit,moved,false),"Body placement did not commit");volume(*state,1064);
    require(state->session.document().body_history.find(tool)->scope.placement.x==20 &&
        state->session.document().body_history.find(tool)->scope.placement.references.at(2).owner_id==id+":origin","Body lost its original placement reference");
    run(host,"undo");volume(*state,1000);run(host,"redo");volume(*state,1064);
    // A stale properties draft cannot overwrite a later graph edit.
    edit=workspace::prepare_body_edit(state->session.document(),tool);moved=*edit.pending.find(tool);moved.name="stale edit";
    run(host,"body.activate",{{"body",first}});
    bool stale=false;try{static_cast<void>(workspace::commit_body_edit(live,kernel,edit,moved,false));}
    catch(const workspace::BodyOperationError& error){stale=std::string(error.code)=="body_history_changed";}
    require(stale && state->session.document().body_history.find(tool)->name=="Tool renamed","Stale draft overwrote newer graph state");
    const auto preserved_volume=state->session.calculated_boundaries().back().volume;
    run(host,"body.cursor",{{"index",0},{"body",first}});
    require(state->session.document().body_history.find(first)->cursor==0,"Body cursor not changed");volume(*state,preserved_volume);
    const auto cursor_revision=state->session.revision();const auto* cursor_cache=state->session.calculated_boundaries().data();
    run(host,"body.cursor",{{"index",0},{"body",first}});
    require(state->session.revision()==cursor_revision && state->session.calculated_boundaries().data()==cursor_cache,"No-op cursor rebuilt history");
    require(!host.execute({{"command","body.cursor"},{"arguments",{{"index",-1}}}}).ok && state->session.revision()==cursor_revision,"Negative cursor partly committed");
    require(host.execute({{"command","body.cursor"},{"arguments",{{"index",0},{"body",tool}}}}).code=="inactive_body","Inactive Body cursor changed");
    const auto inserted=run(host,"box.create",{{"length_mm","2"},{"width_mm","2"},{"height_mm","2"}}).data.at("container").get<std::string>();
    require(state->session.document().body_history.find(first)->entries.front().id==inserted &&
        state->session.document().body_history.find(tool)->entries.front().id==tool_box,"Cursor insertion crossed Body boundary");
    run(host,"body.cursor",{{"index",0}});
    const auto at_front=run(host,"body.create",{{"name","First in tree"},{"active",false}}).data.at("body").get<std::string>();
    require(state->session.document().body_history.order().front()==at_front,"Document cursor did not place new Body");
    interaction.editing=true;
    require(host.execute_text("body.create forbidden").code=="editing_in_progress","Body command bypassed GUI edit guard");run(host,"body.list");interaction.editing=false;
    run(host,"save");std::vector<kernel::BodyResult> reloaded;
    const auto loaded=document::PartDocument::load(directory/"bodies.prtz",&reloaded);
    require(loaded.body_history==state->session.document().body_history && !reloaded.empty() &&
        loaded.body_history.find_boolean(cut)->tool_id==tool,"Save/load lost body graph or Boolean identity");
    run(host,"new",{{"type","assembly"},{"name","other"}});
    require(run(host,"body.list",{{"document",id}}).data.at("document")==id && live.active_document_id()!=id,"Read inactive Body graph switched documents");
    require(host.execute_text("body.create forbidden").code=="unsupported_document","Body created in Assembly");
}
}
int main() {
    try {
        kernel::OcctKernel kernel;
        const auto directory=fs::canonical(fs::temp_directory_path())/("zima-body-commands-"+document::PartDocument::create_default().document_id);
        fs::create_directory(directory);verify(kernel,directory);
        require(directory.parent_path()==fs::canonical(fs::temp_directory_path()),"Unsafe cleanup directory");fs::remove_all(directory);
        std::cout<<"Bodies, origin references, ownership, Boolean volumes, cursors, no-op, failures and history passed without Qt\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
