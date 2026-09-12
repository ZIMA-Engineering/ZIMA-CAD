#include <zima/command_host/host.hpp>
#include <zima/workspace/component_operations.hpp>
#include <zima/document/file_path.hpp>
#include <iostream>
#include <cmath>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
command_host::Options options() {command_host::Options options;options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};return options;}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Host host(live,kernel,dir,options());
    run(host,"new",{{"type","part"},{"name","component-part"}});const auto source=live.active_document_id();
    run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}});run(host,"save");
    run(host,"new",{{"type","assembly"},{"name","component-owner"}});const auto owner=live.active_document_id();
    const auto inserted=run(host,"component.insert",{{"source",source},{"name","První díl"}}).data;
    const auto second=run(host,"component.insert",{{"source",source},{"name","Druhý díl"}}).data;
    require(inserted.at("instance_path")!=second.at("instance_path") && live.open_assembly(owner)->session.document().components[0].calculated_source.shares_with(live.open_assembly(owner)->session.document().components[1].calculated_source),"Repeated instances lost identity or duplicated source snapshots");
    const auto query=run(host,"component.get",{{"instance_path",inserted.at("instance_path")}}).data;
    require(query.at("name")=="První díl" && std::abs(query.at("cached_volume_mm3").get<double>()-6000)<1e-8 && query.at("direct")==true && query.at("owning_document")==owner,"Component query lost snapshot data");
    run(host,"undo");require(run(host,"component.list").data.at("total")==1,"Insertion did not produce one Undo");run(host,"redo");
    require(run(host,"component.get",{{"instance_path",second.at("instance_path")}}).ok,"Insertion Redo changed the occurrence path");
    const auto rejected=[&](const char* command,Json args) {
        const auto id=live.active_document_id();const auto* state=live.open_assembly(id);
        const auto rev=state->session.revision(),gen=state->session.data_generation();const auto count=state->session.document().components.size();const auto docs=live.size();
        require(!host.execute({{"command",command},{"arguments",std::move(args)}}).ok,"Invalid component command accepted");
        require(live.open_assembly(id)->session.revision()==rev && live.open_assembly(id)->session.data_generation()==gen && live.open_assembly(id)->session.document().components.size()==count && live.size()==docs,"Rejected insertion changed documents, revision or generation");
    };
    rejected("component.insert",{{"source","missing"}});rejected("component.insert",{{"source",owner}});rejected("component.insert",{{"source",source},{"name",""}});
    rejected("component.list",{{"limit",4294967297LL}});rejected("component.get",{{"instance_path","invalid"}});
    run(host,"new",{{"type","assembly"},{"name","component-subassembly"}});const auto sub=live.active_document_id();
    const auto leaf=run(host,"component.insert",{{"source",source}}).data.at("occurrence").get<std::string>();run(host,"save");
    run(host,"activate",{{"document",owner}});const auto sub1=run(host,"component.insert",{{"source",sub}}).data.at("occurrence").get<std::string>();
    const auto sub2=run(host,"component.insert",{{"source",sub}}).data.at("occurrence").get<std::string>();
    const auto path1=assembly::InstancePath{}.child(sub1).child(leaf).encoded(),path2=assembly::InstancePath{}.child(sub2).child(leaf).encoded();
    require(run(host,"component.list",{{"recursive",true}}).data.at("total")==6 && run(host,"component.get",{{"instance_path",path1}}).data.at("source_document")==source && path1!=path2,"Nested query flattened repeated hierarchy");
    require(run(host,"component.list",{{"recursive",true},{"limit",2}}).data.at("items").size()==2,"Query output limit ignored");
    const auto owner_revision=live.open_assembly(owner)->session.revision();
    run(host,"new",{{"type","assembly"},{"name","component-parent"}});const auto parent=live.active_document_id();run(host,"component.insert",{{"source",owner}});const auto parent_revision=live.open_assembly(parent)->session.revision();
    run(host,"activate",{{"document",owner}});run(host,"component.insert",{{"source",source}});
    require(live.open_assembly(parent)->session.revision()==parent_revision && live.open_assembly(owner)->session.revision()==owner_revision+1,"Insertion refreshed a parent or committed twice");
    auto hidden=live.open_assembly(owner)->session.document();hidden.find_occurrence(sub1)->visible=false;live.open_assembly(owner)->session.commit(std::move(hidden));
    require(run(host,"component.get",{{"instance_path",path1}}).data.at("effective_visible")==false && run(host,"component.get",{{"instance_path",path2}}).data.at("effective_visible")==true,"Ancestor visibility leaked between repeated occurrences");
    run(host,"save");run(host,"close",{{"document",sub}});run(host,"close",{{"document",source}});
    const auto before=live.open_assembly(owner)->session.revision(),generation=live.open_assembly(owner)->session.data_generation();
    fs::remove(dir/"component-part.prtz");fs::remove(dir/"component-subassembly.asmz");
    require(run(host,"component.get",{{"instance_path",path1}}).data.at("name")=="component-part" && run(host,"component.list",{{"recursive",true}}).data.at("total")==7,"Snapshot queries loaded closed source files");
    require(live.open_assembly(owner)->session.revision()==before && live.open_assembly(owner)->session.data_generation()==generation,"Queries changed revision or generation");
    const auto loaded=assembly::AssemblyDocument::load(dir/"component-owner.asmz");require(loaded.find_occurrence(sub1)->nested_snapshot.front().occurrence_id==leaf,"Native save lost the nested occurrence identity");
}
void cycles(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Host host(live,kernel,dir,options());
    const auto make=[&](const char* name){run(host,"new",{{"type","assembly"},{"name",name}});return live.active_document_id();};
    const auto owner=make("cycle-owner");run(host,"save");const auto mid=make("cycle-mid");run(host,"component.insert",{{"source",owner}});run(host,"save");
    const auto top=make("cycle-top");run(host,"component.insert",{{"source",mid}});run(host,"save");run(host,"close",{{"document",mid}});run(host,"activate",{{"document",owner}});
    const auto rev=live.open_assembly(owner)->session.revision(),gen=live.open_assembly(owner)->session.data_generation();const auto count=live.size();
    auto cycle=host.execute({{"command","component.insert"},{"arguments",{{"source",top}}}});
    require(!cycle.ok && cycle.code=="dependency_cycle" && live.open_assembly(owner)->session.revision()==rev && live.open_assembly(owner)->session.data_generation()==gen && live.size()==count,"Cycle through a closed subassembly was accepted or partially opened documents");
    run(host,"new",{{"type","part"},{"name","cycle-reference-part"}});const auto part=live.active_document_id();run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}});
    auto next=live.open_part(part)->session.document();auto sketch=sketcher::Sketch::create_default();sketcher::SketchExternalReference ref;ref.id="reference";ref.source_document_id=owner;sketch.external_references.push_back(ref);next.sketches.push_back(std::move(sketch));live.open_part(part)->session.commit(std::move(next),live.open_part(part)->session.calculated_boundaries());
    run(host,"activate",{{"document",owner}});cycle=host.execute({{"command","component.insert"},{"arguments",{{"source",part}}}});require(!cycle.ok && cycle.code=="dependency_cycle","Cycle through a Part external reference accepted");
    next=live.open_part(part)->session.document();next.sketches.clear();live.open_part(part)->session.commit(std::move(next),live.open_part(part)->session.calculated_boundaries());
    run(host,"component.insert",{{"source",part}});
    run(host,"document.relations.set",{{"relations",Json::array({{{"target","capacity"},{"expression","1 / (12000 - round(model.volume))"}}})}});
    const auto revision=live.open_assembly(owner)->session.revision(),generation=live.open_assembly(owner)->session.data_generation();
    const auto failed=host.execute({{"command","component.insert"},{"arguments",{{"source",part}}}});
    require(!failed.ok && live.open_assembly(owner)->session.revision()==revision && live.open_assembly(owner)->session.data_generation()==generation && live.open_assembly(owner)->session.document().components.size()==1,"A physical relation error partially committed insertion");
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-component-command-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);cycles(kernel,dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Component snapshots, exact paths, native insertion, Undo, parent isolation and dependency cycles passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
