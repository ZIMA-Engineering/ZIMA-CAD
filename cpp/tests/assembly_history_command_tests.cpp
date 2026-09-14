#include "assembly_profile_test_support.hpp"
#include <zima/workspace/assembly_history_operations.hpp>
#include <iostream>

using namespace assembly_profile_test;
namespace {
Json order(Fixture& f,const char* group) {return f.run("history.list").at("orders").at(group);}
void unchanged_geometry(Fixture& f,const assembly::AssemblyDocument& before) {
    for(const auto& component:before.components) {
        const auto* current=f.doc().find_occurrence(component.occurrence_id);
        require(current && current->calculated_source.shares_with(component.calculated_source) &&
            current->placement==component.placement && current->placement_references==component.placement_references,
            "Metadata ordering changed component geometry, placement or mates");
    }
}
void verify(const kernel::OcctKernel& kernel,const fs::path& directory) {
    Fixture f(kernel,directory,"assembly-history");
    const std::string third=f.run("component.insert",{{"source",f.source}}).at("occurrence");
    const auto original=f.doc();const auto revision=f.state().session.revision(),generation=f.state().session.data_generation();
    const auto source_revision=f.live.open_part(f.source)->session.revision();
    require(order(f,"components")==Json::array({f.first,f.second,third}),"Assembly history list lost component order");
    require(f.run("history.can_move",{{"object",third},{"before",f.first}}).at("would_change")==true,
        "Independent Assembly component move rejected");
    require(f.state().session.revision()==revision && f.state().session.data_generation()==generation && !f.host.change(),
        "Assembly ordering query mutated the session");
    require(f.run("history.move",{{"object",third}}).at("changed")==false,"No-op Assembly move committed");
    const auto moved=f.run("history.move",{{"object",third},{"before",f.first}});
    require(moved.at("changed")==true && moved.at("body_calculated")==false &&
        f.state().session.revision()==revision+1 && order(f,"components")==Json::array({third,f.first,f.second}),
        "Component move did not publish one metadata transaction");
    unchanged_geometry(f,original);near(f.volume(third),1000);
    f.run("undo");require(order(f,"components")==Json::array({f.first,f.second,third}),"Component order Undo failed");
    f.run("redo");require(order(f,"components")==Json::array({third,f.first,f.second}),"Component order Redo failed");
    require(f.run("history.move",{{"object",third},{"before",third}}).at("changed")==false,"Move before itself changed history");
    f.reject("history.move",{{"object","missing"}},"history_not_found");
    f.reject("history.can_move",{{"object",third},{"before","missing"}},"history_scope");
    auto mated=f.doc();
    mated.find_occurrence(f.second)->placement_references.push_back({
        assembly::MateKind::PointCoincident,
        {assembly::MateReferenceKind::Point,assembly::InstancePath{}.child(f.second),f.source+":origin","origin:point"},
        {assembly::MateReferenceKind::Point,assembly::InstancePath{}.child(f.first),f.source+":origin","origin:point"}});
    mated.calculate_placement_references();
    f.state().session.commit(std::move(mated));
    f.reject("history.can_move",{{"object",f.second},{"before",f.first}},"history_dependency");
    f.reject("history.move",{{"object",f.second},{"before",f.first}},"history_dependency");
    f.run("undo");

    const auto construction=[&](const char* kind,const char* name) {
        return f.run("construction.create",{{"kind",kind},{"name",name}});
    };
    const std::string point1=construction("point","Source point").at("construction");
    const std::string point2=construction("point","Dependent point").at("construction");
    const std::string point3=construction("point","Independent point").at("construction");
    const auto point_origin=f.doc().find_construction(point1)->container_origin.id;
    f.run("construction.reference.set",{{"construction",point2},{"index",0},
        {"reference",{{"owner",point_origin},{"key","point"}}}});
    f.reject("history.can_move",{{"object",point2},{"before",point1}},"history_dependency");
    f.reject("history.move",{{"object",point2},{"before",point1}},"history_dependency");
    const auto bound_point=*f.doc().find_construction(point2);
    require(f.run("history.move",{{"object",point3},{"before",point1}}).at("body_calculated")==false,
        "Construction reorder calculated bodies");
    require(order(f,"constructions")==Json::array({point3,point1,point2}) &&
        *f.doc().find_construction(point2)==bound_point,"Construction reorder changed its solved reference");
    f.reject("history.move",{{"object",point3},{"before",f.first}},"history_scope");

    const std::string sketch1=f.run("sketch.create",{{"name","Source Sketch"}}).at("sketch");
    const auto owner1=f.doc().sketches.back().owner_container_id;
    const auto origin1=f.doc().find_sketch_container(owner1)->container_origin.id;
    const auto bridge=construction("plane","Between Sketches");
    f.run("placement.reference.set",{{"object",bridge.at("construction")},{"index",0},
        {"reference",{{"owner",origin1},{"key","origin:plane:xy"}}}});
    const std::string sketch2=f.run("sketch.create",{{"name","Dependent Sketch"}}).at("sketch");
    const auto owner2=f.doc().sketches.back().owner_container_id;
    f.run("sketch.reference.set",{{"sketch",sketch2},{"index",0},
        {"reference",{{"owner",bridge.at("entity")},{"key","plane"}}}});
    const std::string sketch3=f.run("sketch.create",{{"name","Independent Sketch"}}).at("sketch");
    const auto owner3=f.doc().sketches.back().owner_container_id;
    const auto definitions=f.doc().sketches;const auto placement=*f.doc().find_sketch_container(owner2);
    f.reject("history.move",{{"object",sketch1}},"history_not_found");
    f.reject("history.move",{{"object",owner3},{"before",point1}},"history_scope");
    f.reject("history.can_move",{{"object",owner2},{"before",owner1}},"history_dependency");
    f.reject("history.move",{{"object",owner2},{"before",owner1}},"history_dependency");
    const auto before_sketch_order=order(f,"sketches");
    require(f.run("history.move",{{"object",owner3},{"before",owner1}}).at("body_calculated")==false &&
        order(f,"sketches")==Json::array({owner3,owner1,owner2}),"Standalone Sketch container order failed");
    require(*f.doc().find_sketch_container(owner2)==placement,"Sketch reorder changed its placement");
    for(std::size_t i=0;i<definitions.size();++i)
        require(f.doc().sketches[i].serialized()==definitions[i].serialized(),"Container order reordered or edited Sketch data");
    f.run("undo");require(order(f,"sketches")==before_sketch_order,"Sketch order Undo failed");f.run("redo");
    unchanged_geometry(f,original);
    require(f.live.open_part(f.source)->session.revision()==source_revision,"Ordering modified source Part");
    f.run("save");
    const auto loaded=assembly::AssemblyDocument::load(directory/"assembly-history.asmz");
    require(loaded.components.front().occurrence_id==third && loaded.sketch_containers.front().id==owner3 &&
        loaded.constructions.front().id==point3 && *loaded.find_sketch_container(owner2)==placement,
        "Native save lost independent history lists or reference definitions");
    f.interaction.editing=true;
    f.reject("history.move",{{"object",third}},"editing_in_progress");
    f.run("history.list");f.interaction.editing=false;

    // A parent cannot move a descendant's component by its source-local ID.
    f.run("new",{{"type","assembly"},{"name","assembly-history-parent"}});
    const auto parent=f.live.active_document_id();
    const std::string outer=f.run("component.insert",{{"source",f.owner}}).at("occurrence");
    const auto parent_revision=f.live.open_assembly(parent)->session.revision();
    const auto result=f.host.execute({{"command","history.move"},{"arguments",{{"object",f.first}}}});
    require(!result.ok && result.code=="history_not_found" &&
        f.live.open_assembly(parent)->session.revision()==parent_revision,"Parent moved an internal component");
    const auto active_path=assembly::InstancePath{}.child(outer).encoded();
    f.run("component.activate",{{"instance_path",active_path}});
    f.run("history.move",{{"object",f.first},{"before",third}});
    require(f.live.active_occurrence_path()==active_path && f.live.active_document_id()==f.owner &&
        f.live.displayed_document_id()==parent && f.live.open_assembly(parent)->session.revision()==parent_revision,
        "Nested reorder changed activation or the parent document");
    unchanged_geometry(f,original);

    // The general history command delegates cuts to their calculated transaction.
    const auto sketch_a=f.rectangle(-4,-4,1,1),sketch_b=f.rectangle(2,2,1,1);
    const std::string cut1=f.run("extrusion.create",{{"sketch",sketch_a},{"length_forward_mm",1},{"targets",{f.first}}}).at("container");
    const std::string cut2=f.run("extrusion.create",{{"sketch",sketch_b},{"length_forward_mm",2},{"targets",{f.first}}}).at("container");
    near(f.volume(f.first),997);
    const auto cut_revision=f.state().session.revision();
    require(f.run("history.can_move",{{"object",cut2},{"before",cut1}}).at("body_calculated")==false &&
        f.state().session.revision()==cut_revision,"Cut order query calculated or committed");
    require(f.run("history.move",{{"object",cut2},{"before",cut1}}).at("body_calculated")==true &&
        order(f,"cuts")==Json::array({cut2,cut1}) && f.state().session.revision()==cut_revision+1,
        "General history move did not delegate the cut transaction");
    near(f.volume(f.first),997);near(f.volume(f.second),1000);
    f.run("undo");require(order(f,"cuts")==Json::array({cut1,cut2}),"Cut order Undo failed");f.run("redo");
    near(f.volume(f.first),997);
}
}
int main(){try {
    const auto root=fs::canonical(fs::temp_directory_path());
    const auto directory=root/("zima-assembly-history-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create fixture directory");
    kernel::OcctKernel kernel;verify(kernel,directory);
    require(fs::canonical(directory).parent_path()==root,"Unsafe cleanup");fs::remove_all(directory);
    std::cout<<"Assembly history queries, ordering, dependencies, ownership, native save and cut calculations passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
