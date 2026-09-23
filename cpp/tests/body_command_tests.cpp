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
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
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
    {
        const auto geometry=state->session.calculated_boundaries().back();
        const auto visible=[&](const std::string& feature){
            const auto mesh=state->session.body_context_mesh();
            return std::ranges::any_of(mesh.triangle_references,[&](const auto& face){return face.owner_id==feature;});
        };
        for(const auto& active:{first,tool}) {
            run(host,"body.activate",{{"body",active}});
            require(visible(first_box)&&visible(tool_box),"Activation hid another visible Body");
        }
        run(host,"body.set",{{"body",first},{"visible",false}});
        for(const auto& active:{first,tool}) {
            run(host,"body.activate",{{"body",active}});
            require(!visible(first_box)&&visible(tool_box),"Activation revealed a hidden Body or hid its sibling");
            require(!state->session.document().body_history.find(first)->visible,"Activation changed stored visibility");
        }
        require(state->session.calculated_boundaries().back().kernel_shape==geometry.kernel_shape&&
            state->session.calculated_boundaries().back().source_fingerprint==geometry.source_fingerprint,
            "Activation or visibility regenerated body geometry");
        run(host,"body.set",{{"body",first},{"visible",true}});
    }
    {
        const auto original=state->session.document().body_history;
        const auto calculated=state->session.calculated_boundaries();
        const auto unchanged_geometry=[&]{
            const auto& current=state->session.calculated_boundaries();
            require(current.size()==calculated.size(),"Body display metadata changed history boundaries");
            for(std::size_t i=0;i<current.size();++i)
                require(current[i].kernel_shape==calculated[i].kernel_shape &&
                    current[i].source_fingerprint==calculated[i].source_fingerprint && current[i].volume==calculated[i].volume,
                    "Body visibility or cursor changed calculated geometry");
        };
        const auto revision=state->session.revision();
        const auto hidden=run(host,"body.set",{{"body",first},{"visible",false}}).data;
        auto expected=original;auto hidden_body=*expected.find(first);hidden_body.visible=false;expected.update_body(hidden_body);
        require(hidden.at("changed")==true && hidden.at("body_calculated")==false &&
            state->session.revision()==revision+1 && state->session.document().body_history==expected,
            "Visibility did not change only the requested Body in one metadata transaction");
        unchanged_geometry();
        const auto same_revision=state->session.revision(),same_generation=state->session.data_generation();
        require(run(host,"body.set",{{"body",first},{"visible",false}}).data.at("changed")==false &&
            state->session.revision()==same_revision && state->session.data_generation()==same_generation && !host.change(),
            "No-op visibility created an Undo entry");
        require(!host.execute({{"command","body.set"},{"arguments",{{"body","missing"},{"visible",false}}}}).ok &&
            state->session.revision()==same_revision,"Invalid visibility target changed state");
        run(host,"save");std::vector<kernel::BodyResult> reopened;
        const auto saved=document::PartDocument::load(directory/"bodies.prtz",&reopened);
        require(saved.body_history==expected && !reopened.empty() && reopened.back().volume==calculated.back().volume,
            "Native save lost visibility, placement or calculated geometry");
        run(host,"undo");require(state->session.document().body_history==original,"Visibility Undo changed more than visibility");
        run(host,"redo");require(state->session.document().body_history==expected,"Visibility Redo failed");
        run(host,"body.set",{{"body",first},{"visible",true}});
        const auto cursor=original.insertion_cursor();
        const auto cursor_revision=state->session.revision();
        const auto global=run(host,"body.cursor",{{"index",cursor}}).data;
        auto global_expected=original;global_expected.activate({});
        require(global.at("changed")==true && global.at("body_calculated")==false &&
            state->session.revision()==cursor_revision+1 && state->session.document().body_history==global_expected,
            "A global cursor at the same index did not atomically end Body activation");
        unchanged_geometry();
        run(host,"undo");require(state->session.document().body_history==original,"Global cursor Undo lost the active Body");
        run(host,"redo");
        const auto no_op_revision=state->session.revision();
        require(run(host,"body.cursor",{{"index",cursor}}).data.at("changed")==false &&
            state->session.revision()==no_op_revision && !host.change(),"Unchanged global cursor created an Undo entry");
        run(host,"body.activate",{{"body",tool}});
        const auto failed_revision=state->session.revision();const auto failed_graph=state->session.document().body_history;
        require(!host.execute({{"command","body.cursor"},{"arguments",{{"index",original.order().size()+1}}}}).ok &&
            state->session.revision()==failed_revision && state->session.document().body_history==failed_graph,
            "An invalid global cursor deactivated the Body before validation");
        unchanged_geometry();
    }
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
    const auto owner=live.active_document_id();
    const std::string occurrence=run(host,"component.insert",{{"source",id}}).data.at("occurrence");
    const auto owner_revision=live.open_assembly(owner)->session.revision();
    const auto path=assembly::InstancePath{}.child(occurrence).encoded();
    run(host,"component.activate",{{"instance_path",path}});interaction.active_occurrence=path;
    run(host,"body.set",{{"body",first},{"visible",false}});
    require(live.active_document_id()==id && live.displayed_document_id()==owner &&
        live.active_occurrence_path()==path && live.open_assembly(owner)->session.revision()==owner_revision,
        "Body visibility changed the parent Assembly or exact active occurrence");

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
