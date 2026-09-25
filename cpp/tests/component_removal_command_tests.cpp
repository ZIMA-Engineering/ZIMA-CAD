#include "profile_command_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/component_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
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
    workspace::Workspace live;command_host::Options options;bool editing=false;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
    options.interaction=[&]{command_host::Interaction result;result.editing=editing;return result;};
    command_host::Host host(live,kernel,dir,options);
    run(host,"new",{{"type","part"},{"name","remove-source"}});const auto source=live.active_document_id();
    const auto box=zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","10"},{"width_mm","10"},{"height_mm","10"}}).data.at("container").get<std::string>();run(host,"save");
    const auto make=[&](const char* name){run(host,"new",{{"type","assembly"},{"name",name}});return live.active_document_id();};
    const auto insert=[&]{return run(host,"component.insert",{{"source",source}}).data.at("occurrence").get<std::string>();};
    const auto remove=[&](const std::string& occurrence){return run(host,"component.remove",{{"instance_path",path(occurrence)}});};
    const auto owner=make("remove-owner");const auto first=insert(),second=insert();
    const auto rejected=[&](const std::string& occurrence,const std::string& code=std::string{}) {
        const auto id=live.active_document_id();const auto* state=live.open_assembly(id);const auto before=state->session.document();
        const auto revision=state->session.revision(),generation=state->session.data_generation(),documents=live.size();
        const auto result=host.execute({{"command","component.remove"},{"arguments",{{"instance_path",path(occurrence)}}}});
        require(!result.ok,"Invalid component removal succeeded");
        if(!code.empty()&&result.code!=code)throw std::runtime_error("Expected "+code+", got "+result.code);
        state=live.open_assembly(id);require(state->session.revision()==revision&&state->session.data_generation()==generation&&live.size()==documents&&
            state->session.document().components.size()==before.components.size()&&state->session.document().cuts==before.cuts,
            "Rejected removal partially changed the live Assembly");
        for(const auto& item:before.components) {
            const auto* actual=state->session.document().find_occurrence(item.occurrence_id);
            require(actual&&actual->placement==item.placement&&actual->calculated_source.shares_with(item.calculated_source),
                "Rejected removal published fresh or partly cut source geometry");
        }
    };
    editing=true;rejected(second,"editing_in_progress");editing=false;rejected("missing","occurrence_not_found");
    require(!host.execute({{"command","component.remove"},{"arguments",{{"instance_path",path(first)+path(second)}}}}).ok,"Parent removed a nested component directly");
    run(host,"activate",{{"document",source}});zima::test::resize_rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"container",box},{"length_mm","20"}});const auto source_revision=live.open_part(source)->session.revision();
    run(host,"activate",{{"document",owner}});const auto removed=remove(second).data;
    require(removed.at("removed")==true&&live.open_assembly(owner)->session.document().components.size()==1,"Removal result or component count is wrong");
    near(live.open_assembly(owner)->session.document().find_occurrence(first)->calculated_source->volume,2000);
    require(live.open_part(source)->session.revision()==source_revision&&live.open_part(source)->session.is_dirty()&&fs::exists(dir/"remove-source.prtz"),"Removal changed or deleted the source document");
    run(host,"undo");require(live.open_assembly(owner)->session.document().find_occurrence(second),"Removal did not undo in one step");run(host,"redo");
    require(!live.open_assembly(owner)->session.document().find_occurrence(second),"Redo changed removal identity");run(host,"undo");live.refresh_source_geometry();
    // A fresh-source preparation succeeds, but the final physical relation fails.
    // The old GUI path published that preparation before attempting removal.
    run(host,"document.relations.set",{{"relations",Json::array({{{"target","capacity"},{"expression","1 / (3000 - round(model.volume))"}}})}});
    run(host,"activate",{{"document",source}});zima::test::resize_rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"container",box},{"length_mm","30"}});run(host,"activate",{{"document",owner}});
    rejected(second);near(live.open_assembly(owner)->session.document().find_occurrence(first)->calculated_source->volume,2000);
    run(host,"document.relations.set",{{"relations",Json::array()}});
    auto original=live.open_assembly(owner)->session.document();auto next=original;
    next.find_occurrence(second)->placement_references.push_back({assembly::MateKind::PointCoincident,
        {assembly::MateReferenceKind::Point,assembly::InstancePath{}.child(second),source+":origin","origin:point"},
        {assembly::MateReferenceKind::Point,{},owner+":origin","origin:point"}});
    live.open_assembly(owner)->session.commit(next);rejected(second,"component_in_use");run(host,"undo");
    next=original;next.add_dependency(assembly::AssemblyDocument::create_dependency(first,second,assembly::ComponentDependencyKind::ExternalSketchReference));
    live.open_assembly(owner)->session.commit(next);rejected(second,"component_in_use");run(host,"undo");
    next=original;auto sketch=sketcher::Sketch::create_default();sketcher::SketchExternalReference ref;
    ref.id="external";ref.kind=sketcher::ExternalReferenceKind::Point;
    ref.source_document_id=source;ref.source_owner_id=source+":origin";
    ref.source_semantic_key="origin:point";ref.cached_points={{{0,0}}};
    ref.source_instance_path=path(second);sketch.external_references={ref};next.insert_sketch(sketch);
    live.open_assembly(owner)->session.commit(next);rejected(second,"component_in_use");run(host,"undo");
    next=original;next.find_occurrence(second)->source_document_id="unavailable";next.find_occurrence(second)->source_path=dir/"unavailable.prtz";
    live.open_assembly(owner)->session.commit(next);rejected(second);run(host,"undo");
    const auto mirror=run(host,"mirror.create",{{"source",second},{"local_plane","yz"}}).data.at("object").get<std::string>();
    rejected(second,"component_in_use");remove(mirror);remove(second);
    require(live.open_assembly(owner)->session.document().components.size()==1,"Removing a copy and its source left an occurrence");
    const auto cut_owner=make("remove-cut");const auto a=insert(),b=insert();
    auto cut_model=live.open_assembly(cut_owner)->session.document();
    auto profile=sketcher::Sketch::create_default();static_cast<void>(profile.add_rectangle(-1,-10,1,10));
    auto cutter=document::PartDocument::create_extrusion_container(profile.id);cutter.combine_mode=document::CombineMode::Subtract;
    cutter.extrusion.extent_mode=document::ProfileExtentMode::Symmetric;cutter.extrusion.length_forward=20;cutter.extrusion.height=20;
    profile.owner_container_id=cutter.id;cut_model.sketches.push_back(profile);cut_model.cuts.push_back({cutter,{a,b},{}});
    workspace::calculate_resolved_assembly_cuts(kernel,cut_model);near(cut_model.find_occurrence(a)->calculated_source->volume,2800);
    live.open_assembly(cut_owner)->session.commit(std::move(cut_model));
    remove(b);const auto& after=live.open_assembly(cut_owner)->session.document();
    require(after.cuts.front().target_occurrence_ids==std::vector<std::string>{a}&&after.cuts.front().input_component_bodies.size()==1,
        "Removal retained deleted cut targets or rollback input packets");
    near(after.cuts.front().input_component_bodies.at(a).volume,3000);near(after.find_occurrence(a)->calculated_source->volume,2800);
    run(host,"undo");near(live.open_assembly(cut_owner)->session.document().find_occurrence(b)->calculated_source->volume,2800);run(host,"redo");
    run(host,"save");const auto native=assembly::AssemblyDocument::load(dir/"remove-cut.asmz");
    require(native.components.size()==1&&native.cuts.front().target_occurrence_ids==std::vector<std::string>{a},"Native Assembly lost removal or cut targets");
    near(native.find_occurrence(a)->calculated_source->volume,2800);
    remove(a);require(live.open_assembly(cut_owner)->session.document().components.empty()&&live.open_assembly(cut_owner)->session.document().cuts.front().target_occurrence_ids.empty(),
        "Removing the last cut target retained a component");
    require(live.open_part(source)&&fs::exists(dir/"remove-source.prtz"),"Removing last occurrence deleted source Part");
}
}
int main(){try{const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-component-removal-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create test directory");kernel::OcctKernel kernel;verify(kernel,dir);require(dir.parent_path()==root,"Unsafe cleanup");fs::remove_all(dir);
    std::cout<<"Component removal: current sources, dependency blockers, cuts, atomic failures, native files and Undo passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
