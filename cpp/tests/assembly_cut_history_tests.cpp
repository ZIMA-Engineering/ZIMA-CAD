#include "assembly_profile_test_support.hpp"
#include <iostream>
using namespace assembly_profile_test;
namespace {
void verify(const kernel::OcctKernel& kernel,const fs::path& directory) {
    Fixture f(kernel,directory,"cut-history");const auto sketch1=f.rectangle(-1,-1.5,2,3);
    const std::string first=f.run("extrusion.create",{{"sketch",sketch1},{"length_forward_mm",4},{"targets",{f.first}}}).at("container");
    const auto sketch2=f.rectangle(2,0,1,2);
    const std::string second=f.run("extrusion.create",{{"sketch",sketch2},{"length_forward_mm",3},{"targets",{f.first}}}).at("container");
    near(f.volume(f.first),970);near(f.volume(f.second),1000);
    near(f.doc().find_cut(second)->input_component_bodies.at(f.first).volume,976);
    const auto unchanged=[&](const char* command,Json args) {
        const auto revision=f.state().session.revision(),generation=f.state().session.data_generation();
        const auto result=f.run(command,std::move(args));
        require(!result.at("changed").get<bool>()&&f.state().session.revision()==revision&&f.state().session.data_generation()==generation&&!f.host.change(),"No-op cut operation changed history or geometry");
    };
    unchanged("assembly.cut.suppress",{{"container",first},{"suppressed",false}});
    unchanged("assembly.cut.move",{{"container",first},{"before",first}});
    const auto revision=f.state().session.revision(),generation=f.state().session.data_generation();
    const auto allowed=f.run("assembly.cut.can_move",{{"container",second},{"before",first}});
    require(allowed.at("allowed")==true&&allowed.at("would_change")==true&&f.state().session.revision()==revision&&f.state().session.data_generation()==generation&&!f.host.change(),"Move preflight calculated or mutated the document");
    f.run("assembly.cut.move",{{"container",second},{"before",first}});
    require(f.doc().cuts.front().definition.id==second,"Move retained the old order");near(f.volume(f.first),970);
    near(f.doc().find_cut(first)->input_component_bodies.at(f.first).volume,994);
    f.run("undo");require(f.doc().cuts.front().definition.id==first,"Move Undo lost original order");near(f.doc().find_cut(second)->input_component_bodies.at(f.first).volume,976);
    f.run("redo");near(f.doc().find_cut(first)->input_component_bodies.at(f.first).volume,994);
    f.run("assembly.cut.suppress",{{"container",first},{"suppressed",true}});near(f.volume(f.first),994);
    unchanged("assembly.cut.suppress",{{"container",first},{"suppressed",true}});
    f.run("undo");near(f.volume(f.first),970);f.run("redo");near(f.volume(f.first),994);
    f.run("assembly.cut.suppress",{{"container",first},{"suppressed",false}});near(f.volume(f.first),970);
    f.reject("assembly.cut.move",{{"container",first},{"before","missing"}},"container_not_found");
    f.reject("assembly.cut.can_move",{{"container","missing"}},"container_not_found");
    f.reject("assembly.cut.remove",{{"container","missing"}},"container_not_found");
    f.interaction.editing=true;
    f.reject("assembly.cut.remove",{{"container",first}},"editing_in_progress");
    f.reject("assembly.cut.suppress",{{"container",first},{"suppressed",true}},"editing_in_progress");
    f.reject("assembly.cut.move",{{"container",first}},"editing_in_progress");f.interaction={};
    // A source-order dependency blocks both preflight and commit. No OCCT IDs.
    auto dependent=f.doc();auto* consumer=dependent.find_cut(first);
    consumer->definition.placement.references.push_back({"",dependent.find_cut(second)->definition.container_origin.id,"origin:plane:xy",0,true});
    f.state().session.commit(std::move(dependent));
    f.reject("assembly.cut.can_move",{{"container",first},{"before",second}},"history_dependency");
    f.reject("assembly.cut.move",{{"container",first},{"before",second}},"history_dependency");
    f.run("undo");
    f.run("assembly.cut.remove",{{"container",first}});near(f.volume(f.first),994);
    require(!f.doc().find_cut(first)&&std::ranges::none_of(f.doc().sketches,[&](const auto& s){return s.id==sketch1||s.owner_container_id==first;})&&f.doc().sketches.front().id==sketch2,"Cut removal lost the wrong Sketch or left an orphan");
    f.run("undo");require(f.doc().find_cut(first)&&std::ranges::any_of(f.doc().sketches,[&](const auto& s){return s.id==sketch1&&s.owner_container_id==first;}),"Removal Undo did not restore owned Sketch");near(f.volume(f.first),970);
    f.run("redo");near(f.volume(f.first),994);
    f.run("save");const auto loaded=assembly::AssemblyDocument::load(directory/"cut-history.asmz");
    require(loaded.cuts==f.doc().cuts&&loaded.sketches.size()==1,"Native history lost order or retained the deleted profile");
    near(loaded.find_occurrence(f.first)->calculated_source->volume,994);
    f.run("assembly.cut.remove",{{"container",second}});near(f.volume(f.first),1000);
    require(f.doc().cuts.empty()&&f.doc().sketches.empty(),"Final cut removal retained hidden profile data");
    near(f.live.open_part(f.source)->session.calculated_boundaries().back().volume,1000);
}
void lost_reference(const kernel::OcctKernel& kernel,const fs::path& directory) {
    Fixture f(kernel,directory,"cut-lost-reference");const auto profile=f.rectangle(-1,-1.5,2,3);
    const std::string cut=f.run("extrusion.create",{{"sketch",profile},{"length_forward_mm",4},{"targets",{f.first}}}).at("container");
    const std::string survivor=f.run("sketch.create",{{"name","Dependent curve"},{"plane","XY"}}).at("sketch");
    auto next=f.doc();auto& sketch=*std::ranges::find(next.sketches,survivor,&sketcher::Sketch::id);
    auto reference=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
    reference.source_document_id=f.owner;reference.source_owner_id=profile;reference.source_semantic_key=next.sketches.front().segments.front().id;
    reference.cached_points={{0,0},{2,0}};sketch.add_external_reference(reference);
    const auto geometry=sketch.add_external_profile_geometry(reference.id);require(!geometry.empty(),"Projected curve was not created");
    f.state().session.commit(std::move(next));
    f.run("assembly.cut.remove",{{"container",cut}});
    const auto& retained=*std::ranges::find(f.doc().sketches,survivor,&sketcher::Sketch::id);
    require(retained.external_references.front().broken&&retained.external_references.front().cached_points==reference.cached_points&&retained.segments.size()==1,"Removal lost a projected curve or hid its missing source");
    f.run("save");const auto loaded=assembly::AssemblyDocument::load(directory/"cut-lost-reference.asmz");
    require(loaded.sketches.front().external_references.front().broken&&loaded.sketches.front().segments.size()==1,"Broken reference or retained curve was not persisted");
    f.run("undo");require(!std::ranges::find(f.doc().sketches,survivor,&sketcher::Sketch::id)->external_references.front().broken&&f.doc().find_cut(cut),"Undo did not restore the projected source");
}
}
int main(){try{
    const auto root=fs::canonical(fs::temp_directory_path());const auto directory=root/("zima-cut-history-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create fixture directory");kernel::OcctKernel kernel;verify(kernel,directory);lost_reference(kernel,directory);
    require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);
    std::cout<<"Assembly cut history: calculated order, input bodies, suppression, deletion, owned profiles, atomic errors and Undo/Redo passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
