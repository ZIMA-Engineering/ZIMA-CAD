#include <zima/command_host/host.hpp>
#include <zima/workspace/component_source_operations.hpp>
#include <iostream>
#include <cmath>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){if(std::abs(actual-expected)>1e-6)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto value=host.execute({{"command",name},{"arguments",std::move(args)}});if(!value.ok)throw std::runtime_error(std::string(name)+": "+value.code+": "+value.message);return value;
}
std::string path(const std::string& id){return assembly::InstancePath{}.child(id).encoded();}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;bool editing=false;std::string supplied_path;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    options.interaction=[&]{command_host::Interaction value;value.editing=editing;value.active_occurrence=supplied_path;return value;};
    command_host::Host host(live,kernel,dir,options);
    run(host,"new",{{"type","part"},{"name","activation-part"}});const auto source=live.active_document_id();
    const auto box=run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}}).data.at("container").get<std::string>();run(host,"save");
    run(host,"new",{{"type","assembly"},{"name","activation-middle"}});const auto middle=live.active_document_id();
    const auto leaf=run(host,"component.insert",{{"source",source}}).data.at("occurrence").get<std::string>();run(host,"save");
    run(host,"new",{{"type","assembly"},{"name","activation-top"}});const auto top=live.active_document_id();
    const auto first=run(host,"component.insert",{{"source",middle}}).data.at("occurrence").get<std::string>();
    const auto second=run(host,"component.insert",{{"source",middle}}).data.at("occurrence").get<std::string>();
    run(host,"component.set",{{"instance_path",path(second)},{"placement",{{"x_mm",100}}}});run(host,"save");
    const auto first_leaf=assembly::InstancePath{{first,leaf}}.encoded(),second_leaf=assembly::InstancePath{{second,leaf}}.encoded();
    const auto top_revision=live.open_assembly(top)->session.revision();
    const auto first_placement=live.open_assembly(top)->session.document().find_occurrence(first)->placement;
    const auto second_placement=live.open_assembly(top)->session.document().find_occurrence(second)->placement;
    run(host,"close",{{"document",source}});run(host,"close",{{"document",middle}});
    const auto activate=[&](const std::string& selected){return run(host,"component.activate",{{"instance_path",selected}});};
    const auto context=[&](const std::string& active,const std::string& selected) {
        const auto value=run(host,"context").data;
        require(value.at("active_document")==active&&value.at("displayed_document")==top&&value.at("active_occurrence")==selected,"Activation lost the exact source or top-level scene");
    };
    auto result=activate(first_leaf);
    require(result.data.at("opened")==true&&result.data.at("changed")==true&&!live.open_assembly(middle),"Deep activation did not open only the requested source");
    context(source,first_leaf);near(live.open_part(source)->session.calculated_boundaries().back().volume,6000);
    result=activate(first_leaf);require(result.data.at("changed")==false&&!host.change(),"Repeated activation emitted a change");
    run(host,"box.set",{{"container",box},{"height_mm","40"}});context(source,first_leaf);
    near(live.open_part(source)->session.calculated_boundaries().back().volume,8000);
    run(host,"undo");near(live.open_part(source)->session.calculated_boundaries().back().volume,6000);
    run(host,"redo");near(live.open_part(source)->session.calculated_boundaries().back().volume,8000);
    live.refresh_source_geometry();
    for(const auto& component:live.open_assembly(top)->session.document().components)near(component.calculated_source->volume,8000);
    require(live.open_assembly(top)->session.revision()==top_revision&&
        live.open_assembly(top)->session.document().find_occurrence(first)->placement==first_placement&&
        live.open_assembly(top)->session.document().find_occurrence(second)->placement==second_placement,"Source edit regenerated or repositioned parent components");
    std::vector<kernel::BodyResult> saved;static_cast<void>(document::PartDocument::load(dir/"activation-part.prtz",&saved));near(saved.back().volume,6000);
    result=activate(second_leaf);require(result.data.at("changed")==true&&result.data.at("opened")==false,"Repeated source occurrences shared their activation identity");context(source,second_leaf);
    const auto rejected=[&](const char* command,Json args,const std::string& code) {
        const auto before=live.active_document_id(),displayed=live.displayed_document_id(),selected=live.active_occurrence_path();const auto count=live.size();
        const auto revision=live.open_part(source)->session.revision();
        const auto failure=host.execute({{"command",command},{"arguments",std::move(args)}});
        if(failure.ok||failure.code!=code)throw std::runtime_error(std::string(command)+" expected "+code+", got "+failure.code);
        require(live.active_document_id()==before&&live.displayed_document_id()==displayed&&live.active_occurrence_path()==selected&&live.size()==count&&live.open_part(source)->session.revision()==revision,"Rejected activation command partially changed context or source");
    };
    rejected("component.activate",{{"instance_path",path("missing")}},"invalid_arguments");
    rejected("component.activate",{{"instance_path",""}},"missing_argument");
    rejected("component.activate",{{"instance_path",first_leaf},{"document",source}},"unsupported_document");
    rejected("box.set",{{"container",box},{"height_mm","50"},{"document",top}},"document_changed");
    editing=true;rejected("component.activate",{{"instance_path",first_leaf}},"editing_in_progress");rejected("component.deactivate",Json::object(),"editing_in_progress");editing=false;
    supplied_path=first_leaf;rejected("box.set",{{"container",box},{"height_mm","50"}},"active_occurrence");supplied_path.clear();
    run(host,"save");context(source,second_leaf);saved.clear();static_cast<void>(document::PartDocument::load(dir/"activation-part.prtz",&saved));near(saved.back().volume,8000);
    result=activate(path(first));require(result.data.at("opened")==true,"Nested Assembly source was not opened");context(middle,path(first));
    const auto inserted=run(host,"component.insert",{{"source",source},{"name","Inserted locally"}}).data.at("occurrence").get<std::string>();
    require(live.open_assembly(middle)->session.document().components.size()==2&&live.open_assembly(top)->session.document().components.size()==2,"Nested insertion used the parent Assembly");
    run(host,"component.set",{{"instance_path",path(inserted)},{"name","Local edit"},{"placement",{{"z_mm",15}}}});
    live.refresh_source_geometry();
    const auto nested_inserted=assembly::InstancePath{{first,inserted}}.encoded();
    require(live.resolve_occurrence(top,assembly::InstancePath::decode(nested_inserted)).has_value(),"Inserted component is absent from the displayed hierarchy");
    require(live.open_assembly(top)->session.revision()==top_revision&&live.open_assembly(middle)->session.document().find_occurrence(inserted)->placement.z==15,"Local component edit changed its owning Assembly");
    rejected("component.set",{{"instance_path",nested_inserted},{"name","Wrong owner"}},"unsupported_context");
    rejected("component.insert",{{"source",top}},"dependency_cycle");
    run(host,"component.remove",{{"instance_path",path(inserted)}});run(host,"undo");require(live.open_assembly(middle)->session.document().find_occurrence(inserted),"Nested removal Undo used another document");
    run(host,"redo");require(!live.open_assembly(middle)->session.document().find_occurrence(inserted),"Nested removal Redo failed");context(middle,path(first));
    run(host,"save");
    // Closing an unrelated source tab keeps the exact active Assembly context.
    run(host,"close",{{"document",source}});context(middle,path(first));
    activate(second_leaf);context(source,second_leaf);
    // Closing the active source restores the displayed Assembly for editing.
    run(host,"close");context(top,"");
    activate(first_leaf);run(host,"activate",{{"document",source}});
    require(live.active_document_id()==source&&live.displayed_document_id()==source&&live.active_occurrence_path().empty(),"Opening the active source tab retained a stale occurrence");
    run(host,"component.activate",{{"instance_path",second_leaf},{"document",top}});context(source,second_leaf);
    run(host,"close",{{"document",top}});
    require(live.active_document_id()==source&&live.displayed_document_id()==source&&live.active_occurrence_path().empty(),"Closing the displayed parent did not retain its open editable source");
    run(host,"open",{{"path",(dir/"activation-top.asmz").string()}});activate(first_leaf);
    result=run(host,"component.deactivate");require(result.data.at("changed")==true,"Deactivation was not reported");context(top,"");
    result=run(host,"component.deactivate");require(result.data.at("changed")==false&&!host.change(),"Repeated deactivation changed the document");
    activate(first_leaf);run(host,"new",{{"type","part"},{"name","activation-unrelated"}});
    require(live.active_document_id()==live.displayed_document_id()&&live.active_occurrence_path().empty(),"New document retained component activation");
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-component-activation-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Exact nested activation, authoritative sources, ownership, Undo, native save and document lifecycle passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
