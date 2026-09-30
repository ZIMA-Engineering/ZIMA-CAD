#include "profile_command_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/workspace/metadata_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/relation_operations.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/document/file_path.hpp>
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
    workspace::Workspace live;command_host::Options options;options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);run(host,"new",{{"type","part"},{"name","engineering-part"}});
    const auto id=live.active_document_id();zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}});
    run(host,"document.settings.set",{{"precision",{{"decimal_places",9}}}});
    auto* part=live.open_part(id);auto geometry=part->session.calculated_boundaries().back().kernel_shape;
    const auto initial_material=workspace::material_data(live,id);
    const auto revision=part->session.revision(),generation=part->session.data_generation();
    for(const auto* command:{"document.relations.get","document.material.get","document.family.get"})run(host,command);
    require(part->session.revision()==revision && part->session.data_generation()==generation,"Engineering queries mutated state");
    const auto initial_rows=run(host,"document.material.get").data.at("properties");
    require(!run(host,"document.material.set",{{"properties",initial_rows}}).data.at("changed").get<bool>(),"Material read/write created a redundant transaction");
    Json material=Json::array({{{"key","MATERIAL_NAME"},{"value","Hliník"},{"descriptions",{{"cs","Název"},{"en","Name"}}}},
        {{"key","MASS_DENSITY"},{"value","2700"},{"unit","kg/m^3"}}});
    const auto previous_mass=part->session.document().user_parameters.at("mass");
    run(host,"document.material.set",{{"properties",material}});
    require(std::abs(document::physical_values(part->session.document(),part->session.calculated_boundaries()).at("model.mass")-.0162)<1e-10 && part->session.document().user_parameters.at("mass")==previous_mass,"Material must update physical inputs without evaluating relations");
    require(part->session.calculated_boundaries().back().kernel_shape==geometry,"Material edit changed B-Rep");
    run(host,"undo");require(workspace::material_data(live,id)==initial_material,"Material Undo failed");run(host,"redo");
    run(host,"document.parameters.set",{{"parameters",Json::array({{{"key","x"},{"values",{{"","2"}}}}})}});
    const std::string relation_rows="result = x * 3\nsquare = result ^ 2\nweight = model.mass * 1000\nfinal = weight + 6\n";
    const auto evaluated=run(host,"document.relations.set",{{"relations",relation_rows}}).data;
    require(!evaluated.at("parameters").contains("result"),"Saving relation source evaluated it prematurely");
    run(host,"regenerate");
    geometry=part->session.calculated_boundaries().back().kernel_shape;
    const auto& computed=part->session.document().user_parameters;
    require(computed.at("result")=="6"&&computed.at("square")=="36"&&computed.at("final")=="22.2","Regeneration lost relation results");
    require(!run(host,"document.relations.set",{{"relations",relation_rows}}).data.at("changed").get<bool>(),"Identical relations created an Undo entry");
    const auto rejected=[&](const char* command,Json args) {
        const auto before_revision=part->session.revision(),before_generation=part->session.data_generation();
        const auto parameters=workspace::user_parameters(live,id);const auto material_before=workspace::material_data(live,id);
        const auto relations=part->session.document().relations;const auto family=part->session.document().family_table;
        require(!host.execute({{"command",command},{"arguments",std::move(args)}}).ok,"Invalid engineering metadata accepted");
        require(part->session.revision()==before_revision && part->session.data_generation()==before_generation && workspace::user_parameters(live,id)==parameters && workspace::material_data(live,id)==material_before && part->session.document().relations==relations && part->session.document().family_table==family,"Rejected metadata changed state or history");
    };
    for(const auto* expression:{"1 / 0","unknown_name * 3","sqrt(-1)","unavailable(2)"})rejected("document.relations.set",{{"relations",std::string("bad = ")+expression}});
    rejected("document.relations.set",{{"relations",relation_rows+"result = 3\n"}});
    rejected("document.relations.set",{{"relations","bad.name = 2"}});
    for(const auto& deep:std::vector<std::string>{std::string(300,'(')+"1"+std::string(300,')'),std::string(300,'-')+"1",[] {std::string s="1";for(int i=0;i<300;++i)s+="^1";return s;}()})
        rejected("document.relations.set",{{"relations","bad = "+deep}});
    auto invalid_material=material;invalid_material[1]["value"]="-5";rejected("document.material.set",{{"properties",invalid_material}});
    invalid_material=material;invalid_material[1]["unit"]="MPa";rejected("document.material.set",{{"properties",invalid_material}});
    invalid_material=material;invalid_material.push_back(material[0]);rejected("document.material.set",{{"properties",invalid_material}});
    invalid_material=material;invalid_material[1]["value"]=2700;rejected("document.material.set",{{"properties",invalid_material}});
    invalid_material=material;invalid_material[0]["value"]="line1\nline2";rejected("document.material.set",{{"properties",invalid_material}});
    invalid_material=material;invalid_material[0]["key"]="NAME=VALUE";rejected("document.material.set",{{"properties",invalid_material}});
    const auto box_id=part->session.document().history.front().id;
    Json family={{"columns",{"NUMBER","LENGTH"}},{"bindings",{{"NUMBER",{{"kind","feature"},{"owner",box_id},{"key",""}}},{"LENGTH",{{"kind","dimension"},{"owner",box_id},{"key","parameter:length_forward"}}}}},{"instances",Json::array({{{"id",""},{"shared_name",true},{"labels",nlohmann::json::object()},{"name","Držák 10"},{"values",{{"NUMBER","yes"},{"LENGTH","10"}}}},{{"id",""},{"shared_name",true},{"labels",nlohmann::json::object()},{"name","Držák 20"},{"values",{{"LENGTH","20"}}}}})}};
    family["instances"][0]["shared_name"]=false;family["instances"][0]["labels"]={{"en","Bracket 10"},{"cs","Drzak 10"}};
    const auto empty_family=part->session.document().family_table;const auto changed_family=run(host,"document.family.set",{{"table",family}}).data.at("table");
    require(changed_family.at("instances")[0].at("labels")==family.at("instances")[0].at("labels")&&!changed_family.at("instances")[0].at("shared_name").get<bool>(),"Family command lost localization");
    require(changed_family.at("instances")[1].at("values").at("NUMBER")=="","Missing family cells were not normalized");
    require(!run(host,"document.family.set",{{"table",family}}).data.at("changed").get<bool>(),"Family normalization created redundant Undo");
    run(host,"undo");require(part->session.document().family_table==empty_family,"Family Undo failed");run(host,"redo");
    auto invalid_family=family;invalid_family["columns"].push_back("NUMBER");rejected("document.family.set",{{"table",invalid_family}});
    invalid_family=family;invalid_family["instances"][1]["name"]="Držák 10";rejected("document.family.set",{{"table",invalid_family}});
    invalid_family=family;invalid_family["instances"][0]["name"]="engineering-part";rejected("document.family.set",{{"table",invalid_family}});
    invalid_family=family;invalid_family["instances"][0]["values"]["unknown"]="5";rejected("document.family.set",{{"table",invalid_family}});
    invalid_family=family;invalid_family["instances"][0]["values"]["LENGTH"]=5;rejected("document.family.set",{{"table",invalid_family}});
    invalid_family=family;invalid_family["instances"][0]["labels"]["xx"]="Unsupported";rejected("document.family.set",{{"table",invalid_family}});
    invalid_family=family;invalid_family["instances"][0]["labels"]["en"]=5;rejected("document.family.set",{{"table",invalid_family}});
    invalid_family=family;invalid_family["instances"][0]["shared_name"]="yes";rejected("document.family.set",{{"table",invalid_family}});
    run(host,"document.relations.set",{{"relations",""}});
    require(part->session.document().relations.empty() && part->session.document().user_parameters.at("final")=="22.2","Removing relations lost the last calculated parameter value");run(host,"undo");
    run(host,"save");const auto saved=document::PartDocument::load(dir/"engineering-part.prtz");
    require(saved.family_table==part->session.document().family_table && saved.relations==part->session.document().relations && saved.material_parameter_descriptions.at("MATERIAL_NAME").at("cs")=="Název","Native save lost engineering metadata");
    require(part->session.calculated_boundaries().back().kernel_shape==geometry,"Engineering metadata rebuilt geometry");
    const auto library=dir/fs::path(u8"Ocel česká.matz");fs::copy_file("config/materials/01_steels/structural/S235JR.matz",library);
    const auto aluminum_before=workspace::material_data(live,id);
    run(host,"document.material.load",{{"path",document::path_to_utf8(library.filename())}});
    require(std::abs(document::physical_values(part->session.document(),part->session.calculated_boundaries()).at("model.mass")-.0471)<1e-10 && part->session.calculated_boundaries().back().kernel_shape==geometry,"Library assignment did not use cached volume and steel density");
    require(!run(host,"document.material.load",{{"path",document::path_to_utf8(library.filename())}}).data.at("changed").get<bool>(),"Reloading identical material created Undo");
    rejected("document.material.load",{{"path","missing.matz"}});rejected("document.material.load",{{"path","engineering-part.prtz"}});
    run(host,"undo");require(workspace::material_data(live,id)==aluminum_before,"Material library Undo failed");
    run(host,"new",{{"type","assembly"},{"name","engineering-assembly"}});const auto owner=live.active_document_id();static_cast<void>(live.insert_open_part(owner,id,"Part"));
    const auto snapshot=live.open_assembly(owner)->session.document().components.front().calculated_source;
    const auto owner_revision=live.open_assembly(owner)->session.revision();
    run(host,"activate",{{"document",id}});material[1]["value"]="7800";run(host,"document.material.set",{{"properties",material}});
    require(live.open_assembly(owner)->session.revision()==owner_revision && std::abs(assembly::physical_values(live.open_assembly(owner)->session.document()).at("model.mass")-.0162)<1e-10,"Source material silently refreshed a parent Assembly");
    run(host,"activate",{{"document",owner}});
    for(const auto& request:std::vector<Json>{
        {{"command","document.material.get"}},
        {{"command","document.material.set"},{"arguments",{{"properties",material}}}},
        {{"command","document.material.load"},{"arguments",{{"path",document::path_to_utf8(library)}}}}}) {
        const auto result=host.execute(request);
        require(!result.ok && live.open_assembly(owner)->session.revision()==owner_revision,
            "Assembly accepted material or changed its history after rejection");
    }
    const auto packet=live.open_assembly(owner)->session.document().serialized();
    require(!packet.contains("physical_parameters")&&!packet.contains("physical_parameter_units")&&
        !packet.contains("material_parameter_descriptions"),"Assembly still serializes its own material");
    family["columns"]={"Part"};family["bindings"]={{"Part",{{"kind","component"},{"owner",live.open_assembly(owner)->session.document().components.front().occurrence_id},{"key",""}}}};
    for(auto& row:family["instances"])row["values"]={{"Part","yes"}};
    run(host,"document.family.set",{{"table",family}});
    run(host,"document.relations.set",{{"relations","grams = model.mass * 1000\n"}});
    require(!live.open_assembly(owner)->session.document().user_parameters.contains("grams")&&live.open_assembly(owner)->session.document().components.front().calculated_source.shares_with(snapshot),"Saving Assembly source evaluated or recalculated it");
    run(host,"regenerate");part=live.open_part(id);
    require(std::abs(std::stod(live.open_assembly(owner)->session.document().user_parameters.at("grams"))-46.8)<1e-8,"Assembly regeneration did not evaluate current source mass");
    run(host,"undo");require(!live.open_assembly(owner)->session.document().user_parameters.contains("grams"),"Assembly relation regeneration Undo was not atomic");run(host,"redo");
    run(host,"save");const auto loaded=assembly::AssemblyDocument::load(dir/"engineering-assembly.asmz");require(loaded.relations=="grams = model.mass * 1000\n" && document::parse_family_table(loaded.family_table).instances.size()==2,"Assembly engineering metadata did not persist");
}
void verify_driving(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);
    run(host,"new",{{"type","part"},{"name","relation-driving"}});
    const auto id=live.active_document_id();
    test::rectangular_commands([&](const char* n,Json a){return run(host,n,std::move(a));},{{"length_mm",10},{"width_mm",20},{"height_mm",30}});
    auto* part=live.open_part(id);auto initial=part->session.document();
    const auto feature=std::ranges::find_if(initial.history,[](const auto& f){return f.feature_kind==document::FeatureKind::Extrusion;});
    require(feature!=initial.history.end(),"Relation geometry fixture missing");
    const auto dimension=initial.dimension_identifiers.identifier(feature->id,"parameter:length_forward");
    require(!dimension.empty(),"Driving dimension identifier missing");
    initial.user_parameters["povrch"]="Zn";
    initial.user_parameters["divisor"]="2";
    initial.appearance.bodies[initial.body_history.active_body_id()].color="#123456";
    part->session.commit(initial,part->session.calculated_boundaries());
    const auto original_color=part->session.document().body_color;
    const auto source=dimension+" = 60 / divisor\nstock = \"⌀\" & "+dimension+" & \"x20\"\nnote = \"  řádek 1\\n[Document]\\nname=нержавейка  \"\nif povrch == \"Zn\"\n color = \"#A0A0A0\"\nelseif povrch == \"nerez\"\n color = \"#FFFFFF\"\nelse\n color = \"#000000\"\nendif\n";
    run(host,"document.relations.set",{{"relations",source}});
    require(part->session.document().body_color==original_color&&std::abs(part->session.calculated_boundaries().back().volume-6000)<1e-7,"Saving source changed geometry or color");
    run(host,"regenerate");
    require(std::abs(part->session.calculated_boundaries().back().volume-12000)<1e-7,"Relation did not drive extrusion volume");
    require(part->session.document().body_color=="#A0A0A0"&&part->session.document().user_parameters.at("stock")=="⌀30x20","Color or Unicode text was not evaluated");
    for(const auto& [body,style]:part->session.document().appearance.bodies)require(style.color=="#A0A0A0","Whole-Part color missed Body appearance");
    require(!part->session.document().user_parameters.contains("color"),"Color became an ordinary parameter");
    const auto evaluated_parameters=run(host,"document.parameters.get").data.at("parameters");
    require(!run(host,"document.parameters.set",{{"parameters",evaluated_parameters}}).data.at("changed").get<bool>(),"Reading or confirming multiline relation results changed metadata");
    run(host,"undo");require(part->session.document().body_color==original_color&&std::abs(part->session.calculated_boundaries().back().volume-6000)<1e-7&&part->session.document().relations==source,"Regeneration Undo did not restore geometry and color together");
    run(host,"redo");
    auto bad=part->session.document();bad.user_parameters["divisor"]="0";part->session.commit(bad,part->session.calculated_boundaries());
    const auto revision=part->session.revision(),generation=part->session.data_generation();
    const auto geometry=part->session.calculated_boundaries().back().kernel_shape;
    require(!host.execute({{"command","regenerate"}}).ok,"Division by zero regenerated successfully");
    require(part->session.revision()==revision&&part->session.data_generation()==generation&&part->session.calculated_boundaries().back().kernel_shape==geometry&&part->session.document().body_color=="#A0A0A0","Failed regeneration partially published results");
    require(!host.execute({{"command","document.relations.set"},{"arguments",{{"relations","color = 12"}}}}).ok,"Numeric color accepted");
    require(!host.execute({{"command","document.relations.set"},{"arguments",{{"relations","color = \"#GG0000\""}}}}).ok,"Invalid RGB color accepted");
    bad=part->session.document();bad.user_parameters["divisor"]="2";bad.user_parameters["povrch"]="nerez";part->session.commit(bad,part->session.calculated_boundaries());
    run(host,"regenerate");require(part->session.document().body_color=="#FFFFFF","Text-driven white color failed");
    run(host,"save");const auto loaded=document::PartDocument::load(dir/"relation-driving.prtz");
    require(loaded.relations==source&&loaded.body_color=="#FFFFFF","Relation source or color did not survive save/reopen");
    require(loaded.user_parameters.at("note")=="  řádek 1\n[Document]\nname=нержавейка  "&&loaded.name=="relation-driving","Multiline relation text changed native sections or lost whitespace");
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-engineering-metadata-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);verify_driving(kernel,dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Relations, dimensions, conditional whole-Part colors, atomic regeneration, Undo and persistence passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
