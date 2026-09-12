#include <zima/command_host/host.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/workspace/metadata_operations.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/assembly/physical_properties.hpp>
#include <cmath>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);run(host,"new",{{"type","part"},{"name","engineering-part"}});
    const auto id=live.active_document_id();run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}});
    run(host,"document.settings.set",{{"precision",{{"decimal_places",9}}}});
    auto* part=live.open_part(id);const auto geometry=part->session.calculated_boundaries().back().kernel_shape;
    const auto initial_material=workspace::material_data(live,id);
    const auto revision=part->session.revision(),generation=part->session.data_generation();
    for(const auto* command:{"document.relations.get","document.material.get","document.family.get"})run(host,command);
    require(part->session.revision()==revision && part->session.data_generation()==generation,"Engineering queries mutated state");
    const auto initial_rows=run(host,"document.material.get").data.at("properties");
    require(!run(host,"document.material.set",{{"properties",initial_rows}}).data.at("changed").get<bool>(),"Material read/write created a redundant transaction");
    Json material=Json::array({{{"key","MATERIAL_NAME"},{"value","Hliník"},{"descriptions",{{"cs","Název"},{"en","Name"}}}},
        {{"key","MASS_DENSITY"},{"value","2700"},{"unit","kg/m^3"}}});
    run(host,"document.material.set",{{"properties",material}});
    require(std::abs(document::physical_values(part->session.document(),part->session.calculated_boundaries()).at("model.mass")-.0162)<1e-10 && std::abs(std::stod(part->session.document().user_parameters.at("mass"))-.0162)<1e-10,"6000 mm3 of aluminium must have mass 16.2 g");
    require(part->session.calculated_boundaries().back().kernel_shape==geometry,"Material edit changed B-Rep");
    run(host,"undo");require(workspace::material_data(live,id)==initial_material,"Material Undo failed");run(host,"redo");
    run(host,"document.parameters.set",{{"parameters",Json::array({{{"key","x"},{"values",{{"","2"}}}}})}});
    const auto relation_rows=Json::array({{{"target","result"},{"expression","x * 3"}},{{"target","square"},{"expression","result ^ 2"}},
        {{"target","weight"},{"expression","model.mass * 1000"}},{{"target","final"},{"expression","sqrt(square) + weight"}}});
    const auto evaluated=run(host,"document.relations.set",{{"relations",relation_rows}}).data;
    require(evaluated.at("parameters").at("result")=="6.000000000" && evaluated.at("parameters").at("square")=="36.000000000" && evaluated.at("parameters").at("final")=="22.200000000","Sequential relations lost exact intermediate results or physical values");
    require(!run(host,"document.relations.set",{{"relations",relation_rows}}).data.at("changed").get<bool>(),"Identical relations created an Undo entry");
    const auto rejected=[&](const char* command,Json args) {
        const auto before_revision=part->session.revision(),before_generation=part->session.data_generation();
        const auto parameters=workspace::user_parameters(live,id);const auto material_before=workspace::material_data(live,id);
        const auto relations=part->session.document().relations;const auto family=part->session.document().family_table;
        require(!host.execute({{"command",command},{"arguments",std::move(args)}}).ok,"Invalid engineering metadata accepted");
        require(part->session.revision()==before_revision && part->session.data_generation()==before_generation && workspace::user_parameters(live,id)==parameters && workspace::material_data(live,id)==material_before && part->session.document().relations==relations && part->session.document().family_table==family,"Rejected metadata changed state or history");
    };
    for(const auto* expression:{"1 / 0","unknown_name * 3","sqrt(-1)","unavailable(2)"})rejected("document.relations.set",{{"relations",Json::array({{{"target","bad"},{"expression",expression}}})}});
    auto duplicate=relation_rows;duplicate.push_back(relation_rows[0]);rejected("document.relations.set",{{"relations",duplicate}});
    rejected("document.relations.set",{{"relations",Json::array({{{"target","bad.name"},{"expression","2"}}})}});
    for(const auto& deep:std::vector<std::string>{std::string(300,'(')+"1"+std::string(300,')'),std::string(300,'-')+"1",[] {std::string s="1";for(int i=0;i<300;++i)s+="^1";return s;}()})
        rejected("document.relations.set",{{"relations",Json::array({{{"target","bad"},{"expression",deep}}})}});
    auto invalid_material=material;invalid_material[1]["value"]="-5";rejected("document.material.set",{{"properties",invalid_material}});
    invalid_material=material;invalid_material[1]["unit"]="MPa";rejected("document.material.set",{{"properties",invalid_material}});
    invalid_material=material;invalid_material.push_back(material[0]);rejected("document.material.set",{{"properties",invalid_material}});
    invalid_material=material;invalid_material[1]["value"]=2700;rejected("document.material.set",{{"properties",invalid_material}});
    invalid_material=material;invalid_material[0]["value"]="line1\nline2";rejected("document.material.set",{{"properties",invalid_material}});
    invalid_material=material;invalid_material[0]["key"]="NAME=VALUE";rejected("document.material.set",{{"properties",invalid_material}});
    Json family={{"columns",{"NUMBER","LENGTH"}},{"instances",Json::array({{{"name","Držák 10"},{"values",{{"NUMBER","ZE-10"},{"LENGTH","10"}}}},{{"name","Držák 20"},{"values",{{"LENGTH","20"}}}}})}};
    const auto empty_family=part->session.document().family_table;const auto changed_family=run(host,"document.family.set",{{"table",family}}).data.at("table");
    require(changed_family.at("instances")[1].at("values").at("NUMBER")=="","Missing family cells were not normalized");
    require(!run(host,"document.family.set",{{"table",family}}).data.at("changed").get<bool>(),"Family normalization created redundant Undo");
    run(host,"undo");require(part->session.document().family_table==empty_family,"Family Undo failed");run(host,"redo");
    auto invalid_family=family;invalid_family["columns"].push_back("NUMBER");rejected("document.family.set",{{"table",invalid_family}});
    invalid_family=family;invalid_family["instances"][1]["name"]="Držák 10";rejected("document.family.set",{{"table",invalid_family}});
    invalid_family=family;invalid_family["instances"][0]["name"]="engineering-part";rejected("document.family.set",{{"table",invalid_family}});
    invalid_family=family;invalid_family["instances"][0]["values"]["unknown"]="5";rejected("document.family.set",{{"table",invalid_family}});
    invalid_family=family;invalid_family["instances"][0]["values"]["LENGTH"]=5;rejected("document.family.set",{{"table",invalid_family}});
    run(host,"document.relations.set",{{"relations",Json::array()}});
    require(part->session.document().relations.empty() && part->session.document().user_parameters.at("final")=="22.200000000","Removing relations lost the last calculated parameter value");run(host,"undo");
    run(host,"save");const auto saved=document::PartDocument::load(dir/"engineering-part.prtz");
    require(saved.family_table==part->session.document().family_table && saved.relations==part->session.document().relations && saved.material_parameter_descriptions.at("MATERIAL_NAME").at("cs")=="Název","Native save lost engineering metadata");
    require(part->session.calculated_boundaries().back().kernel_shape==geometry,"Engineering metadata rebuilt geometry");
    run(host,"new",{{"type","assembly"},{"name","engineering-assembly"}});const auto owner=live.active_document_id();static_cast<void>(live.insert_open_part(owner,id,"Part"));
    const auto snapshot=live.open_assembly(owner)->session.document().components.front().calculated_source;
    const auto owner_revision=live.open_assembly(owner)->session.revision();
    run(host,"activate",{{"document",id}});material[1]["value"]="7800";run(host,"document.material.set",{{"properties",material}});
    require(live.open_assembly(owner)->session.revision()==owner_revision && std::abs(assembly::physical_values(live.open_assembly(owner)->session.document()).at("model.mass")-.0162)<1e-10,"Source material silently refreshed a parent Assembly");
    run(host,"activate",{{"document",owner}});run(host,"document.material.set",{{"properties",material}});run(host,"document.family.set",{{"table",family}});
    run(host,"document.relations.set",{{"relations",Json::array({{{"target","grams"},{"expression","model.mass * 1000"}}})}});
    require(std::abs(std::stod(live.open_assembly(owner)->session.document().user_parameters.at("grams"))-16.2)<1e-8 && live.open_assembly(owner)->session.document().components.front().calculated_source.shares_with(snapshot),"Assembly material replaced component mass or recalculated the component snapshot");
    run(host,"save");const auto loaded=assembly::AssemblyDocument::load(dir/"engineering-assembly.asmz");require(loaded.relations.size()==1 && document::parse_family_table(loaded.family_table).instances.size()==2,"Assembly engineering metadata did not persist");
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-engineering-metadata-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Relations, material units and mass, family tables, atomic errors, Undo, persistence and parent isolation passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
