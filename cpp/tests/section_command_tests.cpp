#include <zima/command_host/host.hpp>
#include <zima/document/section.hpp>
#include <zima/kernel/stable_id.hpp>
#include <cmath>
#include <iostream>
using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* command,Json args=Json::object()) {
    auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.code+": "+result.message);
    return result;
}
document::SectionDefinition section() {
    auto value=document::create_section();
    static_cast<void>(value.sketch.add_segment(-20,0,20,0));
    return value;
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);
    require(host.execute_text("section.list").code=="unsupported_document","Section query without a document failed unsafely");
    run(host,"new",{{"type","part"},{"name","section-source"}});
    run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}});
    const auto part_id=live.active_document_id();auto* part=live.open_part(part_id);
    const auto body=part->session.document().body_history.active_body_id();
    auto cut=section();cut.show_cut=true;cut.components[body]={2,{30,3,1,2},true};
    cut.components["missing-body"]={1,{},false};
    auto model=part->session.document();model.sections.push_back(cut);
    part->session.commit(std::move(model),part->session.calculated_boundaries());
    const auto revision=part->session.revision();const auto* cache=part->session.calculated_boundaries().data();
    const auto list=run(host,"section.list").data;
    require(list.at("total")==1&&list.at("items").at(0).at("object")==cut.id,"Section list lost the saved ID");
    const auto data=run(host,"section.get",{{"object",cut.id}}).data;
    require(data.at("sketch")==cut.sketch.id&&data.at("origin")==cut.container_origin.id&&data.at("valid")==true,"Section query lost owned identities or valid chain");
    require(data.at("path_mm")==Json::array({Json::array({-20,0}),Json::array({20,0})}),"Section query changed the local path");
    require(data.at("frames").size()==1&&data.at("frames").at(0).at("normal")==Json::array({0,-1,0}),"Section query returned the wrong cutting direction");
    const auto components=run(host,"section.components",{{"object",cut.id}}).data;
    require(components.at("total")==2,"Section options lost a missing component");
    bool found_body=false,found_missing=false;
    for(const auto& row:components.at("items")) {
        if(row.at("component")==body)found_body=row.at("mode")=="uncut"&&row.at("hatch").at("spacing_mm")==3&&row.at("hatch").at("angle_degrees")==30&&row.at("available")==true;
        if(row.at("component")=="missing-body")found_missing=row.at("mode")=="cut_only"&&row.at("available")==false;
    }
    require(found_body&&found_missing,"Section components lost hatch settings or availability");
    require(run(host,"section.components",{{"object",cut.id},{"offset",1},{"limit",1}}).data.at("items").size()==1,"Section pagination ignored the requested window");
    require(run(host,"section.list",{{"offset",1}}).data.at("items").empty(),"Section list pagination returned an extra item");
    for(const auto& args:std::vector<Json>{{{"offset",-1}},{{"limit",0}},{{"limit",10001}}})
        require(!host.execute({{"command","section.list"},{"arguments",args}}).ok,"Invalid section pagination accepted");
    require(host.execute({{"command","section.get"},{"arguments",{{"object","absent"}}}}).code=="section_not_found","Missing section did not return a precise error");
    require(part->session.revision()==revision&&part->session.calculated_boundaries().data()==cache,"Section queries changed history or calculated geometry");
    require(run(host,"section.activate").data.at("changed")==true,"Normal view did not deactivate the Section");
    const auto normal_revision=part->session.revision();
    require(run(host,"section.activate").data.at("changed")==false&&part->session.revision()==normal_revision,"Unchanged Section activation added history");
    run(host,"section.activate",{{"object",cut.id}});
    require(part->session.document().sections.front().show_cut,"Section activation did not persist");
    const auto before_rejection=part->session.revision();
    require(!host.execute({{"command","section.activate"},{"arguments",{{"object","missing"}}}}).ok&&
        !host.execute({{"command","section.delete"},{"arguments",{{"object","missing"}}}}).ok&&part->session.revision()==before_rejection,"Invalid Section action changed history");
    run(host,"section.delete",{{"object",cut.id}});require(part->session.document().sections.empty(),"Section removal failed");
    run(host,"undo");require(part->session.document().sections.front().id==cut.id&&part->session.document().sections.front().show_cut,"Section removal Undo lost its identity or activation");
    run(host,"redo");require(part->session.document().sections.empty(),"Section removal Redo failed");run(host,"undo");
    require(std::abs(part->session.calculated_boundaries().back().volume-6000)<1e-8,"Section actions changed solid volume");
    run(host,"save");
    auto invalid=section();invalid.sketch.segments.clear();
    auto broken=part->session.document();broken.sections.push_back(invalid);
    part->session.commit(std::move(broken),part->session.calculated_boundaries());
    const auto inspected=run(host,"section.get",{{"object",invalid.id}}).data;
    require(inspected.at("valid")==false&&!inspected.at("error").get<std::string>().empty()&&inspected.at("object")==invalid.id,"Invalid section disappeared instead of remaining inspectable");
    const auto invalid_revision=part->session.revision();
    require(!host.execute({{"command","section.activate"},{"arguments",{{"object",invalid.id}}}}).ok&&part->session.revision()==invalid_revision,"Invalid Section activation changed the model");
    run(host,"undo");
    run(host,"new",{{"type","assembly"},{"name","section-sub"}});const auto sub=live.active_document_id();
    const auto leaf=run(host,"component.insert",{{"source",part_id}}).data.at("occurrence").get<std::string>();run(host,"save");
    run(host,"new",{{"type","assembly"},{"name","section-parent"}});const auto parent=live.active_document_id();
    const auto first=run(host,"component.insert",{{"source",sub}}).data.at("occurrence").get<std::string>();
    const auto second=run(host,"component.insert",{{"source",sub}}).data.at("occurrence").get<std::string>();
    const auto first_path=assembly::InstancePath{}.child(first).child(leaf).encoded();
    const auto second_path=assembly::InstancePath{}.child(second).child(leaf).encoded();
    auto assembly_cut=section();assembly_cut.components[first_path]={2,{},false};
    auto* owner=live.open_assembly(parent);auto assembly_doc=owner->session.document();assembly_doc.sections.push_back(assembly_cut);owner->session.commit(std::move(assembly_doc));
    const auto owner_revision=owner->session.revision();const auto source_packet=owner->session.document().find_occurrence(first)->calculated_source;
    const auto nested=run(host,"section.components",{{"object",assembly_cut.id}}).data;
    require(nested.at("total")==2,"Repeated nested Parts were merged in the section query");
    bool first_ok=false,second_ok=false;
    for(const auto& row:nested.at("items")) {
        if(row.at("component")==first_path)first_ok=row.at("mode")=="uncut";
        if(row.at("component")==second_path)second_ok=row.at("mode")=="cut_hatch";
    }
    require(first_ok&&second_ok,"Section option leaked to another occurrence of the same Part");
    require(run(host,"section.get",{{"document",part_id},{"object",cut.id}}).data.at("object")==cut.id&&live.active_document_id()==parent,"Reading an inactive Part changed activation");
    require(owner->session.revision()==owner_revision&&owner->session.document().find_occurrence(first)->calculated_source.shares_with(source_packet),"Section query mutated Assembly cache");
    run(host,"save");run(host,"close",{{"document",parent}});run(host,"open",{{"path",(dir/"section-parent.asmz").generic_string()}});
    require(run(host,"section.components",{{"object",assembly_cut.id}}).data.at("items")==nested.at("items"),"Native Assembly round trip lost section options");
    const auto action_packet=live.open_assembly(parent)->session.document().find_occurrence(first)->calculated_source;
    run(host,"section.activate",{{"object",assembly_cut.id}});
    require(live.open_assembly(parent)->session.document().sections.front().show_cut,"Assembly Section was not activated");
    require(!host.execute({{"command","section.delete"},{"arguments",{{"document",part_id},{"object",cut.id}}}}).ok,"Section removal wrote to an inactive document");
    run(host,"section.delete",{{"object",assembly_cut.id}});run(host,"undo");
    require(run(host,"section.components",{{"object",assembly_cut.id}}).data.at("items")==nested.at("items"),"Assembly Section removal Undo lost component paths");
    require(live.open_assembly(parent)->session.document().find_occurrence(first)->calculated_source.shares_with(action_packet),"Assembly Section actions copied source geometry");
}
}
int main(){try {
    kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());
    const auto dir=parent/("zima-section-command-"+kernel::make_stable_id());
    require(fs::create_directory(dir),"Cannot create test directory");verify(kernel,dir);
    require(fs::canonical(dir).parent_path()==parent,"Unexpected test cleanup path");fs::remove_all(dir);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
