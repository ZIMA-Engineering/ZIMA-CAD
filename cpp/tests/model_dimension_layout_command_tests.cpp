#include <zima/command_host/host.hpp>
#include <zima/workspace/model_dimension_layout_operations.hpp>
#include <zima/document/dimension_layout_json.hpp>
#include <iostream>
#include <limits>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Interaction interaction;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};options.interaction=[&]{return interaction;};
    command_host::Host host(live,kernel,dir,options);
    const auto run=[&](const std::string& name,Json args=Json::object()){const auto r=host.execute({{"command",name},{"arguments",std::move(args)}});if(!r.ok)throw std::runtime_error(name+": "+r.code+": "+r.message);return r.data;};
    run("new",{{"type","part"},{"name","dimension-layout"}});const auto part_id=live.active_document_id();
    const auto box=run("box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}}).at("container").get<std::string>();
    const auto state=[&]{return live.open_part(part_id);};const auto body=state()->session.document().body_history.active_body_id();
    const auto original=*state()->session.document().find_container(box);const auto cache=state()->session.calculated_boundaries().back();
    const Json ref={{"owner",box},{"key","parameter:length"}};
    const auto get=[&]{return run("dimension.layout.get",{{"reference",ref}});};
    const auto initial=get();require(!initial.at("has_override").get<bool>()&&initial.at("layout").at("text_style").is_null(),"Initial layout did not inherit the dimension style");
    const auto list=run("dimension.layout.list",{{"owner",box}});require(list.at("total")>=3&&list.at("items")[0].at("reference").at("owner")==box,"Dimension list lost native parameter identities");
    require(run("dimension.layout.list",{{"owner",body}}).at("total")>=6,"Body placement dimension identities are missing");
    auto layout=initial.at("layout");const auto rev=state()->session.revision();
    require(!run("dimension.layout.set",{{"reference",ref},{"layout",layout}}).at("changed").get<bool>()&&state()->session.revision()==rev,"Unchanged default layout pinned an override");
    layout["text_along"]=4;layout["text_outward"]=-2;layout["line_offset"]=3;layout["arrows_reversed"]=true;
    kernel::DimensionTextStyle style;style.prefix="CHECK ";style.suffix=" mm";style.decimals=4;style.tolerance_mode="deviations";style.upper_tolerance="0.1";style.lower_tolerance="0.2";
    layout["text_style"]=document::dimension_text_style_json(style);
    const auto changed=run("dimension.layout.set",{{"reference",ref},{"layout",layout}});
    require(changed.at("changed")==true&&changed.at("has_override")==true&&changed.at("body_calculated")==false&&state()->session.revision()==rev+1,"Layout did not commit exactly one metadata transaction");
    require(*state()->session.document().find_container(box)==original&&state()->session.calculated_boundaries().back().kernel_shape==cache.kernel_shape&&state()->session.calculated_boundaries().back().volume==6000&&state()->session.calculated_boundaries().back().mesh.vertices==cache.mesh.vertices,"Layout changed feature values or calculated body geometry");
    const auto current=state()->session.revision();const auto* unchanged_cache=state()->session.calculated_boundaries().data();
    require(run("dimension.layout.set",{{"reference",ref},{"layout",layout}}).at("changed")==false&&state()->session.revision()==current&&state()->session.calculated_boundaries().data()==unchanged_cache&&!host.change(),"Repeated layout changed history or cache");
    run("undo");require(get().at("has_override")==false,"Layout Undo failed");run("redo");require(get().at("layout")==layout,"Layout Redo lost complete text properties");
    const auto fail=[&](Json args,const char* code) {
        const auto before=get();const auto revision=state()->session.revision();const auto* stored_cache=state()->session.calculated_boundaries().data();
        const auto result=host.execute({{"command","dimension.layout.set"},{"arguments",std::move(args)}});
        if(result.ok||result.code!=code)throw std::runtime_error(std::string("Expected ")+code+", got "+result.code+": "+result.message);
        require(!host.change(),"Rejected layout published a workspace change");
        require(get()==before&&state()->session.revision()==revision&&state()->session.calculated_boundaries().data()==stored_cache&&!host.change(),"Rejected layout modified document or body");
    };
    for(auto bad:std::vector<Json>{{{"plane_quarter_turns",1.5}},{{"plane_quarter_turns",4}},{{"plane_quarter_turns",4294967296ULL}},{{"envelope_offset",-1}},{{"text_along",true}},{{"text_outward",std::numeric_limits<double>::infinity()}},{{"unknown",1}},{{"arrows_reversed",1}}}) {
        auto patched=layout;patched.update(bad);fail({{"reference",ref},{"layout",patched}},"invalid_arguments");
    }
    for(const auto& patch:std::vector<Json>{{{"decimals",13}},{{"decimals",4294967296ULL}},{{"tolerance_mode","unknown"}},{{"prefix",std::string(2049,'x')}},{{"extra",true}}}) {
        auto bad=layout;bad["text_style"].update(patch);fail({{"reference",ref},{"layout",bad}},"invalid_arguments");
    }
    fail({{"reference",ref}},"invalid_arguments");fail({{"reference",ref},{"layout",layout},{"reset",true}},"invalid_arguments");fail({{"reference",ref},{"reset",false}},"invalid_arguments");
    auto wrong=ref;wrong["key"]="face:invented";fail({{"reference",wrong},{"layout",layout}},"dimension_not_found");
    wrong=ref;wrong["instance_path"]="another occurrence";fail({{"reference",wrong},{"layout",layout}},"wrong_occurrence");
    interaction.editing=true;fail({{"reference",ref},{"layout",layout}},"editing_in_progress");interaction={};
    run("dimension.layout.set",{{"reference",ref},{"reset",true}});require(get().at("has_override")==false,"Reset retained a stale override");run("undo");require(get().at("layout")==layout,"Reset Undo lost the style");
    const auto point=run("construction.create",{{"kind","point"},{"name","Point dimensions"},{"values",{{"x",7},{"y",3},{"z",4}}}}).at("construction").get<std::string>();
    for(const auto* key:{"parameter:x","parameter:y","parameter:z","parameter:placement:x","parameter:placement:y","parameter:placement:z"}) {
        const Json point_ref={{"owner",point},{"key",key}};
        require(run("dimension.layout.get",{{"reference",point_ref}}).at("has_override")==false,"An existing Point display identity is inaccessible");
        run("dimension.layout.set",{{"reference",point_ref},{"layout",layout}});
        require(run("dimension.layout.get",{{"reference",point_ref}}).at("layout")==layout,"Point appearance did not retain its exact View identity");
    }
    require(state()->session.document().find_construction(point)->origin==kernel::Vec3{7,3,4},"Point appearance moved the construction");
    const auto other=run("body.create",{{"name","Other"}}).at("body").get<std::string>();fail({{"reference",ref},{"layout",layout}},"inactive_body");run("body.activate",{{"body",body}});
    const Json body_ref={{"owner",other},{"key","parameter:placement:x"}};
    run("dimension.layout.set",{{"reference",body_ref},{"layout",layout}});require(run("dimension.layout.get",{{"reference",body_ref}}).at("has_override")==true,"Body-level metadata became inaccessible outside its active Body");
    run("save");std::vector<kernel::BodyResult> saved_cache;const auto saved=document::PartDocument::load(dir/"dimension-layout.prtz",&saved_cache);
    const auto* stored=kernel::find_dimension_layout(saved.dimension_layouts,{box,"parameter:length",{}});
    require(stored&&document::dimension_layout_json(*stored)==layout&&!saved_cache.empty()&&saved.find_container(box)->box.length==10,"Native file lost layout or changed the dimension value");
    run("new",{{"type","assembly"},{"name","dimension-context"}});const auto top=live.active_document_id();
    const auto first=run("component.insert",{{"source",part_id}}).at("occurrence").get<std::string>();
    const auto second=run("component.insert",{{"source",part_id}}).at("occurrence").get<std::string>();
    const auto first_path=assembly::InstancePath{}.child(first).encoded(),second_path=assembly::InstancePath{}.child(second).encoded();
    const auto assembly_revision=live.open_assembly(top)->session.revision();
    const Json component_ref={{"owner",first},{"key","parameter:placement:x"}};
    run("dimension.layout.set",{{"reference",component_ref},{"layout",layout}});
    require(live.open_assembly(top)->session.revision()==assembly_revision+1&&live.open_assembly(top)->session.document().find_occurrence(first)->placement.x==0,"Assembly layout changed component placement");
    const auto assembly_after=live.open_assembly(top)->session.document().dimension_layouts;
    run("save");const auto saved_assembly=assembly::AssemblyDocument::load(dir/"dimension-context.asmz");
    require(saved_assembly.dimension_layouts==assembly_after&&saved_assembly.find_occurrence(first)->placement.x==0,"Native Assembly lost dimension appearance or moved its component");
    run("component.activate",{{"instance_path",first_path}});auto nested=ref;nested["instance_path"]=second_path;
    fail({{"reference",nested},{"layout",layout}},"wrong_occurrence");
    auto nested_layout=layout;nested_layout["text_along"]=9;run("dimension.layout.set",{{"reference",ref},{"layout",nested_layout}});
    require(get().at("reference").at("instance_path")==first_path&&live.displayed_document_id()==top&&live.open_assembly(top)->session.document().dimension_layouts==assembly_after,"Active Part layout changed the parent Assembly or lost occurrence context");
    run("undo");require(get().at("layout")==layout,"Nested Part layout Undo failed");run("redo");run("save");
    require(state()->session.document().find_container(box)->box.length==10,"Nested layout modified the modeled length");
}
}
int main(){try{const auto root=fs::canonical(fs::temp_directory_path()),dir=root/("zima-model-layout-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);kernel::OcctKernel kernel;verify(kernel,dir);require(fs::canonical(dir).parent_path()==root,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Model dimension layouts: geometry preservation, native identity, Body ownership, occurrence scope, reset and Undo passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
