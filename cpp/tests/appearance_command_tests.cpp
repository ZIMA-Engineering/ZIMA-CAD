#include <zima/workspace/appearance_operations.hpp>
#include <zima/command_host/host.hpp>
#include <iostream>
#include <cmath>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);
    require(host.execute_text("appearance.get").code=="unsupported_document","Appearance query without document failed unsafely");
    require(run(host,"appearance.palette").data.at("items").size()>=26,"Built-in palette missing");
    run(host,"new",{{"type","part"},{"name","paint-source"}});const auto source=live.active_document_id();
    run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}});
    auto* part=live.open_part(source);const auto body=part->session.document().body_history.active_body_id();
    const auto revision=part->session.revision();const auto* allocation=part->session.calculated_boundaries().data();
    const auto faces=run(host,"appearance.faces").data;
    require(faces.at("total")==6,"Box appearance query did not offer six result faces");
    const auto original=run(host,"appearance.get").data;
    require(part->session.revision()==revision&&part->session.calculated_boundaries().data()==allocation,"Appearance queries calculated geometry");
    const auto blue=Json{{"color","#225FC2"},{"roughness",.123456789},{"metallic",.75}};
    const auto stale=workspace::prepare_appearance_edit(live,source);
    run(host,"appearance.set",{{"style",blue}});
    require(part->session.document().appearance.bodies.at(body).color=="#225FC2","Part active Body appearance did not change");
    require(std::abs(part->session.calculated_boundaries().back().volume-6000)<1e-8,"Appearance changed volume");
    bool stale_rejected=false;
    try {static_cast<void>(workspace::commit_appearance(live,stale,stale.initial));}catch(const workspace::AppearanceError& e){stale_rejected=std::string(e.code)=="stale_edit";}
    require(stale_rejected,"Stale appearance edit overwrote newer changes");
    const auto changed_revision=part->session.revision();const auto* changed_cache=part->session.calculated_boundaries().data();
    require(!run(host,"appearance.set",{{"style",blue}}).data.at("changed").get<bool>()&&part->session.revision()==changed_revision&&part->session.calculated_boundaries().data()==changed_cache,"Identical appearance committed or copied geometry");
    const auto face=faces.at("items").front();
    const auto group=Json{{"id","polished"},{"name","Leštěná plocha"},{"style",{{"color","#CCBB99"},{"roughness",.04},{"metallic",1}}},
        {"faces",Json::array({{{"owner",face.at("owner")},{"key",face.at("key")}}})}};
    run(host,"appearance.set",{{"groups",Json::array({group})}});
    const auto painted=part->session.document().appearance;const auto before_revision=part->session.revision();
    for(const auto& args:std::vector<Json>{{{"style",{{"color","red"}}}},{{"style",{{"roughness",0}}}},{{"style",{{"metallic",2}}}},
        {{"style",{{"surprise",1}}}},{{"groups",Json::array({group,group})}},{{"groups",Json::array({{{"id","bad"},{"name","Bad"},{"faces",Json::array({{{"owner","missing"},{"key","face"}}})}}})}}}) {
        require(!host.execute({{"command","appearance.set"},{"arguments",args}}).ok,"Invalid appearance accepted");
        require(part->session.revision()==before_revision&&part->session.document().appearance==painted,"Rejected appearance partially committed");
    }
    run(host,"appearance.reset");require(part->session.document().appearance.groups.empty()&&!part->session.document().appearance.bodies.contains(body),"Part reset did not remove body overrides");
    run(host,"undo");require(part->session.document().appearance==painted,"Appearance Undo lost groups");run(host,"redo");run(host,"undo");
    run(host,"body.create",{{"name","Other"}});
    require(host.execute({{"command","appearance.set"},{"arguments",{{"body",body},{"style",blue}}}}).code=="inactive_body","Inactive Body appearance was editable");
    run(host,"undo");run(host,"body.activate",{{"body",body}});
    run(host,"save");const auto native=document::PartDocument::load(dir/"paint-source.prtz");require(native.appearance==painted,"Native Part lost appearance");
    run(host,"new",{{"type","assembly"},{"name","paint-assembly"}});const auto assembly_id=live.active_document_id();
    const auto first=run(host,"component.insert",{{"source",source}}).data.at("occurrence").get<std::string>();
    const auto second=run(host,"component.insert",{{"source",source}}).data.at("occurrence").get<std::string>();
    const auto path=assembly::InstancePath{}.child(first).encoded();
    const auto source_revision=live.open_part(source)->session.revision();
    auto* owner=live.open_assembly(assembly_id);const auto snapshot=owner->session.document().find_occurrence(first)->calculated_source;
    run(host,"appearance.set",{{"instance_path",path},{"body",body},{"style",{{"color","#AA2200"}}}});
    const auto& first_value=*owner->session.document().find_occurrence(first);
    require(first_value.appearance_override&&first_value.appearance_override->bodies.at(body).color=="#AA2200","Occurrence body override missing");
    require(!owner->session.document().find_occurrence(second)->appearance_override&&live.open_part(source)->session.revision()==source_revision,"Occurrence style leaked into sibling or source");
    require(first_value.calculated_source.shares_with(snapshot),"Appearance copied or recalculated component geometry");
    run(host,"save");const auto saved=assembly::AssemblyDocument::load(dir/"paint-assembly.asmz");
    require(saved.find_occurrence(first)->appearance_override==first_value.appearance_override,"Native Assembly lost override");
    require(!host.execute({{"command","appearance.set"},{"arguments",{{"instance_path",assembly::InstancePath{}.child(first).child(second).encoded()},{"style",blue}}}}).ok,"Nested occurrence bypassed its owning Assembly");
    run(host,"appearance.reset",{{"instance_path",path},{"body",body}});
    require(!owner->session.document().find_occurrence(first)->appearance_override->bodies.contains(body),"Occurrence body reset failed");
    run(host,"undo");require(owner->session.document().find_occurrence(first)->appearance_override->bodies.at(body).color=="#AA2200","Occurrence appearance Undo failed");
    run(host,"appearance.reset",{{"instance_path",path},{"inherit",true}});
    require(!owner->session.document().find_occurrence(first)->appearance_override,"Source inheritance kept an occurrence override");
    run(host,"activate",{{"document",source}});
    run(host,"appearance.set",{{"style",{{"color","#336699"}}}});
    run(host,"activate",{{"document",assembly_id}});
    require(run(host,"appearance.get",{{"instance_path",path},{"body",body}}).data.at("style").at("color")=="#336699","Open source appearance required saving or regeneration");
    run(host,"new",{{"type","part"},{"name","paint-copies"}});
    run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}});
    const auto copy_part=live.active_document_id();const auto source_body=live.open_part(copy_part)->session.document().body_history.active_body_id();
    run(host,"body.activate"); // Whole-Body sources are offered at the Part boundary.
    const auto mirrored=run(host,"mirror.create",{{"source",source_body},{"local_plane","yz"},{"placement",{{"x",30}}}}).data.at("object").get<std::string>();
    const auto copied_faces=run(host,"appearance.faces",{{"body",mirrored}}).data;
    require(copied_faces.at("total")==6,"Mirrored Body appearance lost its result-face owners");
    const auto mirrored_packet=live.open_part(copy_part)->session.calculated_boundaries().back().body_outputs.at(mirrored);
    const auto active_before_color=live.open_part(copy_part)->session.document().body_history.active_body_id();
    run(host,"appearance.set",{{"body",mirrored},{"style",blue}});
    require(live.open_part(copy_part)->session.calculated_boundaries().back().body_outputs.at(mirrored).shares_with(mirrored_packet),"Derived appearance copied or recalculated its geometry");
    require(live.open_part(copy_part)->session.document().body_history.active_body_id()==active_before_color,"Derived appearance changed the active Body");
    require(live.open_part(copy_part)->session.document().appearance.bodies.at(mirrored).color=="#225FC2","Derived Body appearance was not editable independently");
    run(host,"save");run(host,"new",{{"type","assembly"},{"name","paint-copied-source"}});
    const auto copied_owner=live.active_document_id();
    const auto copied_occurrence=run(host,"component.insert",{{"source",copy_part}}).data.at("occurrence").get<std::string>();
    const auto copied_path=assembly::InstancePath{}.child(copied_occurrence).encoded();
    run(host,"close",{{"document",copy_part}});
    require(live.active_document_id()==copied_owner,"Closing inactive source changed active Assembly");
    require(run(host,"appearance.faces",{{"instance_path",copied_path},{"body",mirrored}}).data.at("total")==6,"Closed source lost mirrored face ownership");
    require(run(host,"appearance.get",{{"instance_path",copied_path},{"body",mirrored}}).data.at("style").at("color")=="#225FC2","Closed source lost mirrored body style");



}
}
int main(){try {kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());
    const auto dir=parent/("zima-appearance-command-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create test directory");verify(kernel,dir);
    require(dir.parent_path()==parent,"Unexpected cleanup path");fs::remove_all(dir);
    std::cout<<"Appearance commands, ownership, geometry preservation and native persistence passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
