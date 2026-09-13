#include <zima/command_host/host.hpp>
#include <zima/workspace/section_operations.hpp>
#include <cmath>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b){if(std::abs(a-b)>1e-6)throw std::runtime_error("Unexpected Section geometry");}
commands::Result run(command_host::Host& host,const char* command,Json args=Json::object()) {
    const auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);
    run(host,"new",{{"type","part"},{"name","section-properties"}});
    run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}});
    const auto doc=live.active_document_id();auto* part=live.open_part(doc);
    const auto before=part->session.calculated_boundaries().back().source_fingerprint;
    const auto created=run(host,"section.create",{{"path_mm",Json::array({Json::array({-20,0}),Json::array({20,0})})},{"show_cut",true}}).data;
    const auto id=created.at("object").get<std::string>();
    require(part->session.document().sections.size()==1&&part->session.document().sections.front().id==id,"Section creation did not store one definition");
    const auto original=part->session.document().sections.front();
    const auto cut=document::calculate_section(live.authoritative_viewer_mesh(doc),original);double area=0;
    for(const auto& patch:cut.patches)for(const auto& t:patch.triangles) {
        const auto a=t[0],b=t[1],c=t[2];
        const kernel::Vec3 u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z};
        area+=std::hypot(u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x)/2;
    }
    near(area,300);near(part->session.calculated_boundaries().back().volume,6000);
    require(part->session.calculated_boundaries().back().source_fingerprint==before,"Section creation recalculated the body");
    run(host,"section.set",{{"object",id},{"name","B–B"},{"reversed",true},{"show_plane",true},{"placement",{{"y",2}}}});
    const auto& changed=part->session.document().sections.front();
    require(changed.name=="B–B"&&changed.reversed&&changed.show_plane&&changed.sketch.id==original.sketch.id&&
        changed.container_origin==original.container_origin,"Section properties lost state or owned identities");
    near(changed.placement.y,2);
    const auto revision=part->session.revision();
    require(!run(host,"section.set",{{"object",id},{"name","B–B"}}).data.at("changed").get<bool>()&&part->session.revision()==revision,"Unchanged Section properties added history");
    run(host,"undo");require(document::serialize_sections(part->session.document().sections)==document::serialize_sections({original}),"Section properties did not undo together");
    run(host,"redo");run(host,"save");
    const auto loaded=document::PartDocument::load(dir/"section-properties.prtz");
    require(document::serialize_sections(loaded.sections)==document::serialize_sections(part->session.document().sections),"Native Part lost Section properties");
    const auto body=part->session.document().body_history.active_body_id();
    const auto patch=Json::array({{{"component",body},{"hatch",{{"angle_degrees",12.3456789},{"spacing_mm",2.3456789},{"offset_mm",1.23456789},{"pattern","cross"}}}}});
    run(host,"section.set",{{"object",id},{"components",patch}});
    const auto precise=part->session.document().sections.front().components.at(body).hatch;
    run(host,"section.set",{{"object",id},{"components",Json::array({{{"component",body},{"mode","uncut"}}})}});
    require(part->session.document().sections.front().components.at(body).hatch==precise,"Mode change rounded hatch parameters");
    run(host,"section.set",{{"object",id},{"components",Json::array({{{"component",body},{"hatch",{{"spacing_mm",4.5}}}}})}});
    const auto spacing=part->session.document().sections.front().components.at(body).hatch;
    require(spacing.spacing_mm==4.5&&spacing.angle==precise.angle&&spacing.offset_mm==precise.offset_mm&&spacing.pattern==precise.pattern,"Sparse hatch patch changed unrelated values");
    const auto reject=[&](const char* command,Json args,const char* code=nullptr) {
        const auto revision=part->session.revision(),generation=part->session.data_generation();
        const auto model=document::serialize_sections(part->session.document().sections);
        const auto* cache=part->session.calculated_boundaries().data();
        const auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
        require(!result.ok,"Invalid Section edit accepted");
        if(code&&result.code!=code)throw std::runtime_error("Expected "+std::string(code)+", got "+result.code);
        require(part->session.revision()==revision&&part->session.data_generation()==generation&&
            document::serialize_sections(part->session.document().sections)==model&&part->session.calculated_boundaries().data()==cache,
            "Rejected Section edit changed the model or cache");
    };
    for(const auto& path:std::vector<Json>{Json::array(),Json::array({Json::array({0,0}),Json::array({0,0})}),
        Json::array({Json::array({0,0}),Json::array({2,0}),Json::array({2,2}),Json::array({0,0})}),
        Json::array({Json::array({0,0}),Json::array({1,"bad"})})})reject("section.create",{{"path_mm",path}});
    reject("section.set",{{"object",id},{"name","  "}},"invalid_name");
    reject("section.set",{{"object",id},{"plane","wrong"}},"invalid_arguments");
    reject("section.set",{{"object",id},{"placement",{{"unknown",2}}}},"parameter_not_editable");
    for(const auto& component:std::vector<Json>{Json{{"component","missing"},{"mode","uncut"}},Json{{"component",body},{"mode","bad"}},
        Json{{"component",body},{"custom_hatch",false},{"hatch",{{"spacing_mm",2}}}},Json{{"component",body},{"hatch",{{"spacing_mm",0}}}},
        Json{{"component",body},{"hatch",{{"unknown",2}}}},Json{{"component",body},{"custom_hatch","true"}}})
        reject("section.set",{{"object",id},{"components",Json::array({component})}});
    reject("section.set",{{"object",id},{"components",Json::array({{{"component",body}},{{"component",body}}})}},"invalid_arguments");
    const auto second=run(host,"section.create",{{"path_mm",Json::array({Json::array({-20,0}),Json::array({20,0})})},{"name","C–C"},{"show_cut",true}}).data.at("object").get<std::string>();
    require(!part->session.document().sections.front().show_cut&&part->session.document().sections.back().show_cut,"Two Sections became active together");
    reject("section.set",{{"object",second},{"name","B–B"}},"duplicate_name");
    const auto stale=workspace::prepare_section_edit(live,doc,id);
    run(host,"section.set",{{"object",id},{"show_plane",false}});
    bool stale_failed=false;try{static_cast<void>(workspace::commit_section(live,stale,stale.initial));}
    catch(const workspace::SectionOperationError& error){stale_failed=std::string(error.code)=="stale_edit";}
    require(stale_failed,"Stale Section properties overwrote a later change");
    const auto edit=workspace::prepare_section_edit(live,doc,id);auto wrong=edit.initial;wrong.sketch.id="replacement";
    bool identity_failed=false;try{static_cast<void>(workspace::commit_section(live,edit,std::move(wrong)));}
    catch(const workspace::SectionOperationError& error){identity_failed=std::string(error.code)=="identity_changed";}
    require(identity_failed,"Section editing replaced its owned Sketch identity");
    auto stored=part->session.document();stored.sections.front().components["missing-body"]={2,{31.123456789,2,0,1},true};
    part->session.commit(std::move(stored),part->session.calculated_boundaries());
    run(host,"section.set",{{"object",id},{"name","D–D"}});
    require(part->session.document().sections.front().components.at("missing-body").hatch.angle==31.123456789,"Properties discarded an unavailable component's settings");
    run(host,"save");

    run(host,"new",{{"type","assembly"},{"name","section-subassembly"}});const auto sub=live.active_document_id();
    const auto leaf=run(host,"component.insert",{{"source",doc}}).data.at("occurrence").get<std::string>();run(host,"save");
    run(host,"new",{{"type","assembly"},{"name","section-assembly-properties"}});const auto assembly_id=live.active_document_id();
    const auto one=run(host,"component.insert",{{"source",sub}}).data.at("occurrence").get<std::string>();
    const auto two=run(host,"component.insert",{{"source",sub}}).data.at("occurrence").get<std::string>();
    const auto one_path=assembly::InstancePath{}.child(one).child(leaf).encoded(),two_path=assembly::InstancePath{}.child(two).child(leaf).encoded();
    auto* owner=live.open_assembly(assembly_id);const auto packet=owner->session.document().components.front().calculated_source;
    // Adding documents can relocate Workspace document wrappers. Reacquire by identity.
    part=live.open_part(doc);
    const auto source_revision=part->session.revision();
    const auto assembly_section=run(host,"section.create",{{"path_mm",Json::array({Json::array({-50,0}),Json::array({50,0})})},
        {"components",Json::array({{{"component",one_path},{"mode","uncut"}}})}}).data.at("object").get<std::string>();
    require(owner->session.document().sections.front().components.at(one_path).mode==2&&owner->session.document().sections.front().components.at(two_path).mode==0,
        "Section settings leaked between repeated nested occurrences");
    run(host,"section.set",{{"object",assembly_section},{"show_cut",true},{"reversed",true}});
    require(owner->session.document().components.front().calculated_source.shares_with(packet)&&part->session.revision()==source_revision,
        "Section edits changed source geometry or its Part");
    run(host,"undo");require(!owner->session.document().sections.front().show_cut,"Assembly Section did not undo atomically");
    run(host,"redo");run(host,"save");
    const auto assembly_loaded=assembly::AssemblyDocument::load(dir/"section-assembly-properties.asmz");
    require(document::serialize_sections(assembly_loaded.sections)==document::serialize_sections(owner->session.document().sections),"Native Assembly lost Section properties");
    require(!host.execute({{"command","section.set"},{"arguments",{{"document",doc},{"object",id},{"name","inactive"}}}}).ok,
        "Section properties edited an inactive source document");

}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());
    const auto dir=parent/("zima-section-properties-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create test directory");verify(kernel,dir);
    require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);
    std::cout<<"Section creation and properties passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
