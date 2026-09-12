#include <zima/command_host/host.hpp>
#include <zima/workspace/component_source_operations.hpp>
#include <zima/workspace/native_documents.hpp>
#include <iostream>
#include <cmath>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);run(host,"new",{{"type","part"},{"name","source-part"}});const auto part_id=live.active_document_id();
    const auto box=run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}}).data.at("container").get<std::string>();run(host,"save");
    run(host,"new",{{"type","assembly"},{"name","source-middle"}});const auto middle_id=live.active_document_id();const auto leaf=run(host,"component.insert",{{"source",part_id}}).data.at("occurrence").get<std::string>();run(host,"save");
    run(host,"new",{{"type","assembly"},{"name","source-top"}});const auto top=live.active_document_id();const auto root=run(host,"component.insert",{{"source",middle_id}}).data.at("occurrence").get<std::string>();run(host,"save");
    const auto path=assembly::InstancePath{}.child(root).child(leaf);
    run(host,"close",{{"document",part_id}});run(host,"close",{{"document",middle_id}});
    const auto revision=live.open_assembly(top)->session.revision();
    auto opened=workspace::open_component_source(live,top,path);
    require(opened.opened && opened.document_id==part_id && opened.source_instance_path==path && live.active_document_id()==top && live.displayed_document_id()==top && !live.open_assembly(middle_id),"Nested source open changed context or opened intermediate assemblies");
    require(live.open_assembly(top)->session.revision()==revision && std::abs(live.open_part(part_id)->session.calculated_boundaries().back().volume-6000)<1e-8,"Source open regenerated geometry or parent");
    run(host,"activate",{{"document",part_id}});run(host,"box.set",{{"container",box},{"height_mm","40"}});run(host,"activate",{{"document",top}});
    bool io=false;opened=workspace::open_component_source(live,top,path,[&](auto){io=true;});
    require(!opened.opened && !io && std::abs(live.open_part(part_id)->session.calculated_boundaries().back().volume-8000)<1e-8,"Opening an already edited source reloaded its file");
    run(host,"close",{{"document",part_id},{"discard",true}});
    const auto fails=[&](const auto& runner,const char* code) {
        bool failed=false;try{static_cast<void>(workspace::open_component_source(live,top,path,runner));}catch(const workspace::ComponentOperationError& e){failed=e.code==std::string(code);}
        require(failed && !live.open_part(part_id),"Delayed source open was not rejected atomically");
    };
    fails([](auto){},"read_incomplete");
    fails([&](auto read){read();auto doc=live.open_assembly(top)->session.document();doc.name="changed";live.open_assembly(top)->session.commit(std::move(doc));},"document_changed");
    fails([&](auto read){read();auto doc=live.open_assembly(top)->session.document();static_cast<void>(live.remove(top));live.add_assembly(std::move(doc),dir/"source-top.asmz");},"document_changed");
    auto other=document::PartDocument::create_default();const auto other_id=other.document_id;live.add_part(std::move(other),{},dir/"other.prtz");
    fails([&](auto read){read();live.activate(other_id);live.display_top_level(other_id);},"document_changed");live.activate(top);live.display_top_level(top);
    const auto source_file=dir/"source-part.prtz",backup=dir/"source-backup.prtz";fs::copy_file(source_file,backup);
    document::PartDocument::create_default().save(source_file,{});
    fails([](auto read){read();},"dependency_identity");fs::copy_file(backup,source_file,fs::copy_options::overwrite_existing);
    opened=workspace::open_component_source(live,top,path,[&](auto read){read();static_cast<void>(workspace::insert_native_document(live,workspace::read_native_document(source_file)));auto doc=live.open_part(part_id)->session.document();doc.name="already edited";live.open_part(part_id)->session.commit(std::move(doc),live.open_part(part_id)->session.calculated_boundaries());});
    require(!opened.opened && live.open_part(part_id)->session.document().name=="already edited","Source opened during reading was overwritten");
    // A copied subassembly redirects opening to its editable original occurrence.
    auto doc=live.open_assembly(top)->session.document();auto copy=doc.components.front();copy.occurrence_id="derived-occurrence";copy.name="Mirror";copy.derived_copy=document::DerivedCopyParameters{};copy.derived_copy->source_id=root;doc.components.push_back(std::move(copy));live.open_assembly(top)->session.commit(std::move(doc));
    const auto copied=assembly::InstancePath{}.child("derived-occurrence").child(leaf);
    opened=workspace::open_component_source(live,top,copied);
    require(opened.document_id==part_id && opened.source_instance_path==path,"Derived occurrence did not open its editable original source");
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-component-source-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Source opening, nested paths, identity checks, delayed reads and authoritative open documents passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
