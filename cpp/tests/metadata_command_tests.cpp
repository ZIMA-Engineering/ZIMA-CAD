#include <zima/command_host/host.hpp>
#include <zima/workspace/metadata_operations.hpp>
#include <zima/document/physical_properties.hpp>
#include <cmath>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);run(host,"new",{{"type","part"},{"name","metadata-part"}});
    const auto id=live.active_document_id();run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}});
    auto* part=live.open_part(id);const auto original=workspace::user_parameters(live,id);const auto cached=part->session.calculated_boundaries().back().kernel_shape;
    const auto revision=part->session.revision(),generation=part->session.data_generation();
    const auto initial=run(host,"document.parameters.get").data.at("parameters");run(host,"document.settings.get");
    require(part->session.revision()==revision && part->session.data_generation()==generation,"Metadata queries changed the document");
    require(!run(host,"document.parameters.set",{{"parameters",initial}}).data.at("changed").get<bool>(),"Unchanged parameter table created an Undo entry");
    Json entries=Json::array({{{"key","NUMBER"},{"values",{{"","ZE-100"}}},{"labels",{{"cs","Číslo"},{"en","Number"}}}},
        {{"key","NAME"},{"values",{{"cs","Držák"},{"en","Bracket"}}},{"labels",{{"cs","Název"}}}}});
    run(host,"document.parameters.set",{{"parameters",entries}});
    require(part->session.document().user_parameters.at("NUMBER")=="ZE-100" && !part->session.document().user_parameters.contains("NAME") &&
        part->session.document().user_parameter_order==std::vector<std::string>({"NUMBER","NAME","mass"}),"Shared/localized parameter storage or order changed");
    require(!run(host,"document.parameters.set",{{"parameters",entries}}).data.at("changed").get<bool>(),"Derived parameter normalization created a spurious Undo entry");
    const auto edited=workspace::user_parameters(live,id);run(host,"undo");require(workspace::user_parameters(live,id)==original,"Parameter Undo did not restore data");run(host,"redo");require(workspace::user_parameters(live,id)==edited,"Parameter Redo lost localized values");
    const auto reject=[&](const char* name,Json args) {
        const auto revision=part->session.revision(),generation=part->session.data_generation();const auto values=workspace::user_parameters(live,id);const auto settings=workspace::file_settings(live,id);
        require(!host.execute({{"command",name},{"arguments",std::move(args)}}).ok,"Invalid metadata accepted");
        require(part->session.revision()==revision && part->session.data_generation()==generation && workspace::user_parameters(live,id)==values && workspace::file_settings(live,id)==settings,"Failed metadata edit changed state");
    };
    auto duplicated=entries;duplicated.push_back(entries[0]);reject("document.parameters.set",{{"parameters",duplicated}});
    auto unsupported=entries;unsupported[0]["valeu"]="mistyped";reject("document.parameters.set",{{"parameters",unsupported}});
    auto wrongtype=entries;wrongtype[0]["values"][""]=42;reject("document.parameters.set",{{"parameters",wrongtype}});
    reject("document.settings.set",{{"units",{{"Length","yard"}}}});reject("document.settings.set",{{"units",{{"Lenght","cm"}}}});
    reject("document.settings.set",{{"precision",{{"mesh_deflection",0}}}});reject("document.settings.set",{{"precision",{{"decimal_places",2.5}}}});
    reject("document.settings.set",{{"precision",{{"linear_tolerance",-1}}}});reject("document.settings.set",{{"precision",{{"decimal_places",13}}}});
    const auto settings=workspace::file_settings(live,id);run(host,"document.settings.set",{{"units",{{"Length","cm"}}},{"precision",{{"decimal_places",6}}}});
    require(part->session.document().document_units.at("Length")=="cm" && part->session.document().document_precision.at("decimal_places")=="6" &&
        part->session.calculated_boundaries().back().kernel_shape==cached && std::abs(part->session.calculated_boundaries().back().volume-6000)<1e-7,
        "Changing display units or precision rebuilt or rescaled geometry");
    require(std::abs(document::physical_values(part->session.document(),part->session.calculated_boundaries()).at("model.volume")-6)<1e-8,"mm to cm display conversion is incorrect");
    run(host,"undo");require(workspace::file_settings(live,id)==settings,"Settings Undo failed");run(host,"redo");
    const auto recalculated=run(host,"document.settings.set",{{"precision",{{"mesh_deflection",2}}}}).data;
    require(recalculated.at("calculated")==true && std::abs(part->session.calculated_boundaries().back().volume-6000)<1e-7,"Geometric precision did not explicitly calculate a consistent result");
    require(!run(host,"document.settings.set",{{"units",{{"Length","cm"}}},{"precision",{{"mesh_deflection",2},{"decimal_places",6}}}}).data.at("changed").get<bool>(),"Unchanged settings created Undo");
    // A relation error must fail before even the generation counter changes.
    auto related=part->session.document();related.user_parameters["DIVISOR"]="1";related.user_parameter_values["DIVISOR"][""]="1";related.user_parameter_order.push_back("DIVISOR");
    related.relations.push_back({"VOLUME","model.volume / DIVISOR"});part->session.commit(std::move(related),part->session.calculated_boundaries());
    auto invalid_relation=run(host,"document.parameters.get").data.at("parameters");for(auto& entry:invalid_relation)if(entry.at("key")=="DIVISOR")entry["values"][""]="0";
    reject("document.parameters.set",{{"parameters",invalid_relation}});
    run(host,"save");const auto loaded=document::PartDocument::load(dir/"metadata-part.prtz");
    require(loaded.user_parameter_values==part->session.document().user_parameter_values && loaded.document_precision==part->session.document().document_precision,"Native save lost metadata");
    run(host,"new",{{"type","assembly"},{"name","metadata-assembly"}});const auto owner=live.active_document_id();static_cast<void>(live.insert_open_part(owner,id,"Part"));
    auto parent=assembly::AssemblyDocument::create_default();const auto parent_id=parent.document_id;live.add_assembly(std::move(parent),dir/"metadata-parent.asmz");static_cast<void>(live.insert_open_assembly(parent_id,owner,"Assembly"));
    const auto parent_revision=live.open_assembly(parent_id)->session.revision();const auto snapshot=live.open_assembly(owner)->session.document().components.front().calculated_source;
    run(host,"document.parameters.set",{{"parameters",entries}});run(host,"document.settings.set",{{"units",{{"Length","m"}}},{"precision",{{"decimal_places",9}}}});
    require(live.open_assembly(owner)->session.document().components.front().calculated_source.shares_with(snapshot) && live.open_assembly(parent_id)->session.revision()==parent_revision,"Metadata edit refreshed an Assembly or parent snapshot");
    run(host,"save");const auto assembly=assembly::AssemblyDocument::load(dir/"metadata-assembly.asmz");require(assembly.user_parameter_values.at("NAME").at("cs")=="Držák" && assembly.document_units.at("Length")=="m","Assembly metadata did not persist");
    run(host,"document.parameters.set",{{"parameters",Json::array()}});require(workspace::user_parameters(live,owner).order==std::vector<std::string>{"mass"},"Empty table did not remove user parameters or preserve the physical relation target");run(host,"undo");require(workspace::user_parameters(live,owner).order.size()==3,"Table removal Undo failed");
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-metadata-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Parameters, localized values, units, geometry preservation, relation errors, Undo and native metadata persistence passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
