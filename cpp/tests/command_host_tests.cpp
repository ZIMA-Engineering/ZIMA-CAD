#include <zima/command_host/host.hpp>
#include <zima/workspace/document_operations.hpp>
#include <algorithm>
#include <cmath>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace zima;
namespace fs=std::filesystem;
namespace {
using command_host::Host;
using commands::Json;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
commands::Result run(Host& host,std::string_view text){
    auto result=host.execute_text(text);
    if(!result.ok)throw std::runtime_error(std::string(text)+": "+result.code+": "+result.message);
    return result;
}
commands::Result request(Host& host,const char* name,Json args){
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);
    return result;
}
const Json& row(const Json& tree,const std::string& id,const std::string& path={}){
    for(const auto& item:tree.at("items"))if(item.at("id")==id&&item.at("instance_path")==path)return item;
    throw std::runtime_error("Missing model row: "+id+" at "+path);
}
std::size_t count(const Json& tree,const std::string& id){
    return std::count_if(tree.at("items").begin(),tree.at("items").end(),[&](const auto& item){return item.at("id")==id;});
}
void verify_commands(const kernel::OcctKernel& kernel,const fs::path& root){
    workspace::Workspace live;auto directory=root;
    command_host::Interaction interaction;
    command_host::Settings settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Těleso 1"},{{"Length","cm"}}};
    command_host::Options options;
    options.settings=[&]{return settings;};options.interaction=[&]{return interaction;};
    bool reject_io=false;int io_count=0;Host* current=nullptr;
    const auto owner=std::this_thread::get_id();
    options.run_io=[&](std::function<void()> task){
        ++io_count;
        require(current->execute_text("documents").code=="busy","Reentrant command was accepted during I/O");
        if(reject_io)throw std::runtime_error("simulated writer failure");
        std::async(std::launch::async,[&,task=std::move(task)]{
            require(std::this_thread::get_id()!=owner,"I/O did not run on a worker");task();
        }).get();
    };
    Host host(live,kernel,directory,options);current=&host;
    require(run(host,"help").data.size()==105,"Command catalog changed");
    require(run(host,"documents").data.empty()&&run(host,"tree").data.at("items").empty(),"Empty workspace query failed");
    require(host.execute_text("save").code=="no_document","Empty save accepted");
    run(host,"new part \"díl s mezerou\"");const auto id=live.active_document_id();
    require(host.change()&&host.change()->kind==command_host::ChangeKind::New,"New presentation change missing");
    auto* state=live.open_part(id);const auto path=root/fs::path(u8"díl s mezerou.prtz");
    if(!state || state->path!=path || fs::exists(path)){
        std::cerr<<run(host,"documents").data.dump()<<" expected="<<path.generic_string()<<'\n';
        throw std::runtime_error("New did not respect the UTF-8 working path");
    }
    require(state->session.document().document_units.at("Length")=="cm"&&state->session.document().body_history.bodies().front().name=="Těleso 1","Template/settings ignored");
    require(run(host,"documents").json()==host.execute({{"command","documents"}}).json(),"Text and JSON document queries differ");
    const auto context=run(host,"context").data;
    require(context.at("selection").is_null()&&context.at("hover").is_null()&&context.at("camera").is_null()&&context.at("pointer").at("inside_view")==false,"Host fabricated View context");
    require(!host.change(),"Read command retained a previous mutation event");
    require(host.execute_text("fit").code=="view_unavailable","Host without a View accepted fit");
    require(host.execute_text("save wrong-id").code=="document_changed","Wrong target accepted");
    interaction.editing=true;
    require(host.execute_text("regenerate").code=="editing_in_progress","Pending edit overwritten");run(host,"tree");
    interaction.editing=false;interaction.active_occurrence="root/leaf";
    require(host.execute_text("undo").code=="active_occurrence","Active occurrence guard missing");interaction.active_occurrence.clear();
    require(host.execute_text("new part ../escape").code=="invalid_name","Path accepted as document name");
    require(host.execute_text("open model.step").code=="unsupported_format","Console silently imported foreign geometry");
    require(!host.execute_text("new part \"díl s mezerou\"").ok&&live.size()==1,"Duplicate open path accepted");

    // Commit through the normal model API, then operate on that same history via commands.
    auto edited=state->session.document();auto box=document::PartDocument::create_box_container();box.box={10,20,30};
    edited.history.push_back(box);edited.insert_history_entry(document::PartHistoryKind::Feature,box.id);
    auto calculated=workspace::calculate_part(kernel,edited);state->session.commit(edited,calculated);
    require(std::abs(calculated.back().volume-6000)<1e-6,"Independent box volume fixture failed");
    const auto generation=state->session.data_generation();const auto revision=state->session.revision();
    const auto* body_data=state->session.calculated_boundaries().data();
    const auto tree=run(host,"tree").data;require(row(tree,box.id).at("parent_id")==edited.body_history.active_body_id(),"History feature lost its Body parent");
    run(host,"context");run(host,"documents");
    require(state->session.data_generation()==generation&&state->session.revision()==revision&&state->session.calculated_boundaries().data()==body_data,"Read command recalculated or edited model");
    run(host,"save");require(!state->session.is_dirty(),"Save did not mark current revision saved");
    std::vector<kernel::BodyResult> saved;
    auto loaded=document::PartDocument::load(path,&saved);
    require(loaded.history==edited.history&&std::abs(saved.back().volume-6000)<1e-6,"Command save lost geometry or history");
    auto unsaved=state->session.document();unsaved.name="unsaved name";state->session.commit(unsaved,calculated);
    const auto reads=io_count;request(host,"open",{{"path","díl s mezerou.prtz"}});
    state=live.open_part(id);
    require(io_count==reads&&state->session.document().name=="unsaved name"&&state->session.is_dirty(),"Open reloaded an already open dirty document");
    reject_io=true;require(!host.execute_text("save").ok&&state->session.is_dirty()&&!host.change(),"Failed write cleared dirty state or published success");reject_io=false;
    run(host,"undo");require(state->session.document().name==edited.name,"Undo did not use existing model history");
    run(host,"undo");require(state->session.document().history.empty(),"Undo did not remove the feature");
    run(host,"redo");run(host,"regenerate");
    require(std::abs(state->session.calculated_boundaries().back().volume-6000)<1e-6,"Regeneration produced different geometry");
    run(host,"save");
    require(!host.execute_text("open missing.prtz").ok&&live.active_document_id()==id&&live.size()==1&&!host.change(),"Failed open switched context");
    require(live.remove(id),"Fixture close failed");request(host,"open",{{"path","díl s mezerou.prtz"}});
    state=live.open_part(id);require(state&&!state->session.is_dirty()&&state->session.calculated_boundaries().back().volume==saved.back().volume,"Read/insert changed persisted state");

    run(host,"new assembly group");const auto group=live.active_document_id();
    require(request(host,"tree",{{"document",id}}).data.at("document")==id&&live.active_document_id()==group,"Read of another document activated it");
    live.activate(id);require(host.execute_text("save").code=="active_occurrence","Active/displayed mismatch accepted");live.activate(group);
    run(host,"regenerate");run(host,"save");
    require(assembly::AssemblyDocument::load(root/"group.asmz").document_id==group,"Assembly command save failed");
    run(host,"new drawing sheet");const auto drawing_id=live.active_document_id();run(host,"save");
    require(drawing::DrawingDocument::load(root/"sheet.drwz").document_id==drawing_id,"Drawing command save failed");
    require(host.execute_text("regenerate").code=="unsupported_document","Drawing accepted model regeneration");
    require(live.remove(group)&&live.remove(drawing_id),"Fixture close failed");
    run(host,"open group.asmz");require(live.active_document_id()==group,"Assembly command open failed");
    run(host,"open sheet.drwz");require(live.active_document_id()==drawing_id,"Drawing command open failed");
}

void verify_document_lifecycle(const kernel::OcctKernel& kernel,const fs::path& root) {
    workspace::Workspace live;auto directory=root;
    command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    Host host(live,kernel,directory,options);
    const auto folder=root/fs::path(u8"soubory žluťoučké");fs::create_directory(folder);
    const auto utf8=folder.generic_u8string();const std::string folder_text(utf8.begin(),utf8.end());
    request(host,"cd",{{"path",folder_text}});
    require(directory==folder && run(host,"pwd").data.at("path")==folder_text,"Working directory did not change");
    require(host.execute_text("cd missing").code=="invalid_directory" && directory==folder,"Failed cd changed directory");
    run(host,"new part original");const auto id=live.active_document_id();
    require(host.execute_text("close").code=="unsaved_changes" && live.size()==1,"Unwritten new document was discarded");
    auto created=run(host,"box.create 10 20 30");const auto box=created.data.at("container");run(host,"save");
    require(!run(host,"documents").data.front().at("needs_save").get<bool>(),"Saved document still needs saving");
    run(host,"new assembly owner");const auto owner=live.active_document_id();
    auto* part=live.open_part(id);const auto revision=part->session.revision();
    const auto* cache=part->session.calculated_boundaries().data();
    request(host,"activate",{{"document",id}});
    require(live.active_document_id()==id && live.displayed_document_id()==id &&
        part->session.revision()==revision && part->session.calculated_boundaries().data()==cache,"Activation calculated or edited model");
    require(host.execute_text("activate missing").code=="document_not_found" && live.active_document_id()==id,"Unknown activation changed context");
    request(host,"box.set",{{"container",box},{"length_mm","15"}});
    auto drawing=drawing::DrawingDocument::create_default();drawing.source_document_id=id;
    drawing.source_path=folder/"original.prtz";const auto drawing_id=drawing.document_id;
    drawing::DrawingView view;view.id="source-view";view.name="Front";view.source_document_id=id;view.source_path=drawing.source_path;
    drawing.sheets.front().views.push_back(view);
    drawing::BomRow bom;bom.name="Original";bom.source_document_id=id;bom.source_path=drawing.source_path;
    drawing.sheets.front().bom_rows.push_back(bom);
    live.add_drawing(drawing,folder/"original.drwz");
    const auto original_revision=live.open_part(id)->session.revision();
    const auto copied=run(host,"save_as copy.prtz");
    require(copied.data.at("paths").size()==2,"Save As lost companion drawing");
    std::vector<kernel::BodyResult> copied_cache;
    const auto copy=document::PartDocument::load(folder/"copy.prtz",&copied_cache);
    const auto companion=drawing::DrawingDocument::load(folder/"copy.drwz");
    require(copy.document_id!=id && companion.document_id!=drawing_id && companion.source_document_id==copy.document_id &&
        std::abs(copied_cache.back().volume-9000)<1e-6,"Independent copy identity, geometry or drawing link incorrect");
    require(companion.source_path==folder/"copy.prtz" && companion.sheets.front().views.front().source_path==folder/"copy.prtz" &&
        companion.sheets.front().bom_rows.front().source_path==folder/"copy.prtz" &&
        companion.sheets.front().bom_rows.front().source_document_id==copy.document_id,"Drawing Unicode source paths did not roundtrip");
    auto group=live.open_assembly(owner)->session.document();
    group.components.push_back(assembly::AssemblyDocument::create_part_occurrence("Original",id,folder/"original.prtz",live.open_part(id)->session.calculated_boundaries().back()));
    live.open_assembly(owner)->session.commit(group);
    request(host,"activate",{{"document",owner}});run(host,"save");
    const auto group_saved=assembly::AssemblyDocument::load(folder/"owner.asmz");
    require(group_saved.components.front().source_path==folder/"original.prtz","Assembly Unicode source path did not roundtrip");
    run(host,"save_as owner-copy.asmz");
    const auto group_copy=assembly::AssemblyDocument::load(folder/"owner-copy.asmz");
    require(group_copy.document_id!=owner && group_copy.components.front().source_document_id==id &&
        group_copy.components.front().source_path==folder/"original.prtz","Assembly copy changed component source identity/path");
    request(host,"activate",{{"document",id}});
    require(live.active_document_id()==id && live.open_part(id)->session.revision()==original_revision &&
        live.open_part(id)->session.is_dirty() && live.open_part(id)->path==folder/"original.prtz","Save As altered original document");
    require(!host.execute_text("save_as copy.prtz").ok && document::PartDocument::load(folder/"copy.prtz").document_id==copy.document_id,"Save As overwrote an existing file");
    require(!host.execute_text("save_as wrong.asmz").ok && !fs::exists(folder/"wrong.asmz"),"Save As accepted wrong native type");
    require(host.execute_text("close").code=="unsaved_changes" && live.size()==3,"Dirty model was discarded");
    require(host.execute({{"command","close"},{"arguments",{{"discard",1}}}}).code=="invalid_arguments" && live.size()==3,"Non-boolean discard was accepted");
    request(host,"close",{{"document",owner},{"discard",true}});
    require(live.active_document_id()==id && live.displayed_document_id()==id,"Closing another tab switched documents");
    run(host,"save");run(host,"close");
    require(live.active_document_id()==drawing_id && live.displayed_document_id()==drawing_id,"Closing current tab did not select survivor");
    require(host.execute_text("close").code=="unsaved_changes","Unwritten drawing closed without saving");
    run(host,"save");auto edited=live.open_drawing(drawing_id)->document();edited.name="Changed sheet";
    live.open_drawing(drawing_id)->commit(edited);
    const auto info=run(host,"documents").data.front();
    require(info.at("dirty")==true && info.at("needs_save")==true && info.at("revision")==1,"Drawing edits not reported");
    require(host.execute_text("close").code=="unsaved_changes","Drawing edit was silently discarded");
    run(host,"save");run(host,"close");
    require(live.size()==0 && live.active_document_id().empty() && live.displayed_document_id().empty(),"Last document did not close");
    require(host.execute_text("close").code=="no_document","Close on empty workspace succeeded");
    request(host,"open",{{"path","original.drwz"}});
    require(live.open_drawing(drawing_id)->document().name=="Changed sheet" && !live.open_drawing(drawing_id)->is_dirty(),"Drawing save/reopen lost edit or clean state");
}

void verify_model_tree(const kernel::OcctKernel& kernel,const fs::path& root){
    workspace::Workspace live;auto directory=root;
    auto part=document::PartDocument::create_default();
    auto body=part.body_history.create_body("branch");
    auto sketch=sketcher::Sketch::create_default();sketch.points.push_back(sketcher::Sketch::create_point(3,4));
    auto feature=document::PartDocument::create_extrusion_container(sketch.id);feature.suppressed=true;
    sketch.owner_container_id=feature.id;part.sketches={sketch};part.history={feature};
    part.insert_history_entry(document::PartHistoryKind::Feature,feature.id);
    live.add_part(part);const auto id=part.document_id;
    Host host(live,kernel,directory);
    auto tree=run(host,"tree").data;
    require(tree.at("projection")=="model"&&row(tree,body).at("parent_id")==id,"Model hierarchy root/body missing");
    require(row(tree,feature.id).at("suppressed")==true&&row(tree,sketch.id).at("parent_id")==feature.id&&row(tree,sketch.points.front().id).at("parent_id")==sketch.id,"Owned sketch geometry hierarchy incorrect");
    require(count(tree,sketch.id)==1&&live.open_part(id)->session.calculated_boundaries().empty(),"Tree duplicated owned sketch or calculated geometry");
    const auto full=tree.at("items").size();
    require(command_host::model_tree(live,id,full).at("truncated")==false,"Exact limit reported false truncation");
    const auto bounded=command_host::model_tree(live,id,3);
    require(bounded.at("items").size()==3&&bounded.at("truncated")==true,"Tree row budget not enforced");
    require(command_host::model_tree(live,id,0).at("items").empty(),"Zero tree budget ignored");

    // Same nested source and leaf IDs in two instances must remain distinguishable.
    auto nested=assembly::AssemblyDocument::create_default();
    auto leaf=assembly::AssemblyDocument::create_part_occurrence("bolt",id,root/"unavailable.prtz",{});
    nested.components={leaf};
    auto top=assembly::AssemblyDocument::create_default();
    auto first=assembly::AssemblyDocument::create_assembly_occurrence("group",nested.document_id,root/"unavailable.asmz",nested);
    auto second=assembly::AssemblyDocument::create_assembly_occurrence("group",nested.document_id,root/"unavailable.asmz",nested);
    first.visible=false;first.suppressed=true;top.components={first,second};live.add_assembly(top);
    const auto before=live.open_assembly(top.document_id)->session.document().occurrence_snapshot();
    tree=request(host,"tree",{{"document",top.document_id}}).data;
    const auto first_path=assembly::InstancePath{}.child(first.occurrence_id);
    const auto second_path=assembly::InstancePath{}.child(second.occurrence_id);
    const auto& a=row(tree,leaf.occurrence_id,first_path.child(leaf.occurrence_id).encoded());
    const auto& b=row(tree,leaf.occurrence_id,second_path.child(leaf.occurrence_id).encoded());
    require(a.at("parent_instance_path")==first_path.encoded()&&b.at("parent_instance_path")==second_path.encoded()&&a.at("document_id")==nested.document_id&&a.at("source_document_id")==id,"Repeated leaf lost owning assembly or instance identity");
    require(a.at("visible")==false&&a.at("suppressed")==true&&b.at("visible")==true&&b.at("suppressed")==false,"Inherited instance states mixed");
    require(live.size()==2&&live.active_document_id()==id&&live.open_assembly(top.document_id)->session.document().occurrence_snapshot()==before,"Tree resolved files, activated or changed dependencies");
    auto pattern=assembly::AssemblyDocument::create_default();
    auto group=first;group.source_kind=assembly::ComponentSourceKind::Pattern;
    group.nested_snapshot=nested.occurrence_snapshot();pattern.components={group};live.add_assembly(pattern);
    tree=request(host,"tree",{{"document",pattern.document_id}}).data;
    const auto group_path=assembly::InstancePath{}.child(group.occurrence_id);
    require(row(tree,group.occurrence_id,group_path.encoded()).at("type")=="pattern-occurrence" &&
        row(tree,leaf.occurrence_id,group_path.child(leaf.occurrence_id).encoded()).at("document_id")==pattern.document_id,
        "Pattern transferred child ownership to its source document");
    auto large=part;large.document_id+="-large";large.sketches.front().points.clear();
    for(int i=0;i<2100;++i)large.sketches.front().points.push_back(sketcher::Sketch::create_point(i,0));
    live.add_part(large);
    const auto limited=command_host::model_tree(live,large.document_id,9999);
    require(limited.at("items").size()==2000 && limited.at("truncated")==true,"Large tree exceeded the public row cap");
    auto drawing=drawing::DrawingDocument::create_default();drawing.sheets.front().views.push_back({});
    auto& view=drawing.sheets.front().views.back();view.id="persisted-view";view.name="front";view.source_document_id=id;
    live.add_drawing(drawing);tree=request(host,"tree",{{"document",drawing.document_id}}).data;
    require(row(tree,view.id).at("parent_id")==drawing.sheets.front().id&&row(tree,view.id).at("source_document_id")==id,"Drawing view lost source/parent identity");
    require(!host.execute_text("tree nonexistent-document").ok&&!host.change(),"Unknown tree document accepted");
}
}
int main(){
    try{
        kernel::OcctKernel kernel;
        const auto directory=fs::canonical(fs::temp_directory_path())/("zima-command-host-"+document::PartDocument::create_default().document_id);
        fs::create_directory(directory);
        verify_commands(kernel,directory);verify_model_tree(kernel,directory);verify_document_lifecycle(kernel,directory);
        // Only the uniquely created test directory can be removed.
        require(directory.parent_path()==fs::canonical(fs::temp_directory_path()),"Unexpected fixture directory");
        fs::remove_all(directory);
        std::cout<<"Command host without Qt: native I/O, shared model operations, guards and model tree passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
