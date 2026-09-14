#include "assembly_profile_test_support.hpp"
#include <zima/workspace/sketch_properties.hpp>
#include <zima/workspace/assembly_scene.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <iostream>
#include <numbers>
using namespace assembly_profile_test;
namespace {
void standalone(const kernel::OcctKernel& kernel,const fs::path& directory) {
    Fixture f(kernel,directory,"assembly-sketch-properties");
    const std::string sketch=f.run("sketch.create",{{"name","Plane profile"}}).at("sketch");
    const auto current=[&]() {return workspace::document_sketch(f.live,f.owner,sketch);};
    const auto owner=current().owner_container_id;
    const auto feature=[&]() {return *f.doc().find_sketch_container(owner);};
    const auto original=feature();
    const auto components=f.doc().components;
    const auto source_revision=f.live.open_part(f.source)->session.revision();
    const auto check_components=[&]() {
        require(f.doc().components.size()==components.size()&&f.doc().cuts.empty(),"Sketch changed Assembly structure");
        for(const auto& before:components) {
            const auto* after=f.doc().find_occurrence(before.occurrence_id);
            require(after->placement==before.placement&&after->calculated_source.shares_with(before.calculated_source),
                "Sketch Properties recalculated or moved a component");
        }
        require(f.live.open_part(f.source)->session.revision()==source_revision,"Sketch Properties modified the source Part");
    };
    const auto result=f.run("sketch.set",{{"sketch",sketch},{"name","Placed profile"},{"plane","YZ"},
        {"plane_offset_mm",4},{"placement",{{"x",10},{"y",20},{"z",30},{"rotation_z",90}}}});
    require(result.at("body_calculated")==false,"Standalone Sketch reported a body calculation");
    near(current().resolved_origin.x,10);near(current().resolved_origin.y,24);near(current().resolved_origin.z,30);
    require(feature().container_origin==original.container_origin&&feature().feature_id==original.feature_id,"Sketch edit replaced identity");
    check_components();
    const auto placed=feature();const auto placed_sketch=current().serialized();
    f.run("undo");require(feature()==original,"Sketch Properties Undo lost container");
    f.run("redo");require(feature()==placed&&current().serialized()==placed_sketch,"Sketch Properties Redo lost frame");
    require(f.run("sketch.set",{{"sketch",sketch},{"name","Placed profile"}}).at("changed")==false,"No-op added a revision");
    f.run("placement.set",{{"object",owner},{"values",{{"x",12}}}});near(current().resolved_origin.x,12);
    f.run("undo");check_components();
    f.reject("sketch.set",{{"sketch",sketch},{"plane_offset_mm",1000001}},"invalid_arguments");
    f.reject("sketch.reference.set",{{"sketch",sketch},{"index",0},{"reference",{{"owner",original.container_origin.id},{"key","origin:plane:xy"}}}},"reference_not_available");
    f.run("value_lock.set",{{"object",owner},{"key","profile_offset"},{"locked",true}});
    f.reject("sketch.set",{{"sketch",sketch},{"plane_offset_mm",9}},"parameter_not_editable");
    f.run("undo");
    f.run("value_lock.set",{{"object",owner},{"key","placement:x"},{"locked",true}});
    f.reject("placement.set",{{"object",owner},{"values",{{"x",9}}}},"parameter_not_editable");
    f.run("undo");
    auto invalid=f.doc();invalid.sketch_containers.clear();
    bool rejected=false;try{invalid.save(directory/"invalid-owner.asmz");}catch(const std::exception&){rejected=true;}
    require(rejected,"Native save accepted a Sketch without an owner");
    f.run("save");
    const auto reopened=assembly::AssemblyDocument::load(directory/"assembly-sketch-properties.asmz");
    require(reopened.sketch_containers==f.doc().sketch_containers&&reopened.sketches.front().serialized()==current().serialized(),
        "Native Assembly lost Sketch frame, owner, or placement");
    const auto tree=f.run("tree");
    require(std::ranges::any_of(tree.at("items"),[&](const auto& item){return item.at("id")==owner;}),"CLI tree lost Sketch container");
    f.run("sketch.delete",{{"sketch",sketch}});
    require(f.doc().sketches.empty()&&f.doc().sketch_containers.empty(),"Deleting Sketch left an orphan");
    check_components();f.run("undo");
    require(feature()==placed&&current().serialized()==placed_sketch,"Delete Undo lost native Sketch");
    f.run("redo");require(f.doc().sketch_containers.empty(),"Delete Redo failed");
}
void occurrence_reference(const kernel::OcctKernel& kernel,const fs::path& directory) {
    Fixture f(kernel,directory,"assembly-sketch-references");
    const auto first_path=assembly::InstancePath{}.child(f.first).encoded();
    const auto second_path=assembly::InstancePath{}.child(f.second).encoded();
    f.run("component.set",{{"instance_path",second_path},{"placement",{{"z_mm",2}}}});
    const std::string sketch=f.run("sketch.create",{{"name","Face sketch"}}).at("sketch");
    const auto current=[&]() {return workspace::document_sketch(f.live,f.owner,sketch);};
    const auto owner=current().owner_container_id;
    const auto feature=[&]() {return *f.doc().find_sketch_container(owner);};
    const auto geometry=f.doc().build_scene().original_references;
    const auto face=std::ranges::find_if(geometry.triangle_references,[&](const auto& ref){
        return ref.instance_path==first_path&&ref.surface&&ref.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
            ref.surface->origin.z>4.99&&std::abs(ref.surface->axis.z)>.99;
    });
    require(face!=geometry.triangle_references.end(),"Missing original cap");
    const auto components=f.doc().components;
    Json request={{"sketch",sketch},{"index",0},{"reference",{{"owner",face->owner_id},{"key",face->semantic_key},{"instance_path",first_path}}}};
    f.run("sketch.reference.set",request);near(current().resolved_origin.z,5);
    require(feature().placement.reference_valid,"First occurrence reference failed");
    request["reference"]["instance_path"]=second_path;
    f.run("sketch.reference.set",request);near(current().resolved_origin.z,7);
    require(feature().placement.references.front().instance_path==second_path,"Reference lost exact occurrence");
    require(f.run("component.dependencies",{{"instance_path",second_path}}).at("blocked")==true,"Sketch placement was absent from component dependencies");
    f.reject("component.remove",{{"instance_path",second_path}},"component_in_use");
    const auto second=feature();const auto frame=current().serialized();
    f.run("undo");near(current().resolved_origin.z,5);f.run("redo");near(current().resolved_origin.z,7);
    require(feature()==second&&current().serialized()==frame,"Reference Undo/Redo changed definition");
    require(f.run("sketch.reference.set",request).at("changed")==false,"Repeated reference added a revision");
    f.run("placement.reference.remove",{{"object",owner},{"index",0}});
    require(feature().placement.reference_valid,"Removal made remaining orientation invalid");
    f.run("undo");
    request.erase("sketch");request["object"]=owner;request["offset_mm"]=1;
    f.run("placement.reference.set",request);near(current().resolved_origin.z,8);
    for(const auto& before:components)require(f.doc().find_occurrence(before.occurrence_id)->calculated_source.shares_with(before.calculated_source),
        "Standalone reference recalculated component bodies");
    f.run("save");
    const auto reopened=assembly::AssemblyDocument::load(directory/"assembly-sketch-references.asmz");
    require(reopened.sketch_containers==f.doc().sketch_containers&&reopened.sketches.front().serialized()==current().serialized(),"Native reference or frame changed");
}
void dependencies(const kernel::OcctKernel& kernel,const fs::path& directory) {
    Fixture f(kernel,directory,"assembly-sketch-dependencies");
    const std::string sketch=f.run("sketch.create",{{"name","Dependent"}}).at("sketch");
    const auto owner=workspace::document_sketch(f.live,f.owner,sketch).owner_container_id;
    const auto origin=f.doc().find_sketch_container(owner)->container_origin.id;
    const auto datum=f.run("construction.create",{{"kind","plane"},{"name","On sketch"}});
    f.run("placement.reference.set",{{"object",datum.at("construction")},{"index",0},{"reference",{{"owner",origin},{"key","origin:plane:xy"}}}});
    f.reject("sketch.reference.set",{{"sketch",sketch},{"index",0},{"reference",{{"owner",datum.at("entity")},{"key","plane"}}}},"reference_cycle");
    f.run("placement.reference.remove",{{"object",datum.at("construction")},{"index",0}});
    f.run("sketch.reference.set",{{"sketch",sketch},{"index",0},{"reference",{{"owner",datum.at("entity")},{"key","plane"}}}});
    f.reject("construction.delete",{{"construction",datum.at("construction")}},"construction_in_use");
}
void nested_display(const kernel::OcctKernel& kernel,const fs::path& directory) {
    Fixture f(kernel,directory,"assembly-sketch-display");
    const std::string sketch=f.run("sketch.create",{{"name","Display sketch"}}).at("sketch");
    f.run("save");
    f.run("new",{{"type","assembly"},{"name","assembly-sketch-display-parent"}});
    const auto parent=f.live.active_document_id();
    const std::string first=f.run("component.insert",{{"source",f.owner}}).at("occurrence");
    const std::string second=f.run("component.insert",{{"source",f.owner}}).at("occurrence");
    const auto first_path=assembly::InstancePath{}.child(first),second_path=assembly::InstancePath{}.child(second);
    f.run("component.set",{{"instance_path",second_path.encoded()},{"placement",{{"x_mm",20}}}});
    const auto before=f.live.open_assembly(parent)->session.document();
    const auto revision=f.live.open_assembly(parent)->session.revision();
    auto display=f.doc().build_scene();
    display.points.push_back({{11,12,13},{sketch,"point:preview",{}},"Preview"});
    const auto scene=workspace::build_scene_with_assembly_override(f.live,parent,second_path,f.doc(),&display);
    const auto marker=std::ranges::find_if(scene.points,[&](const auto& point){
        return point.reference.owner_id==sketch&&point.reference.semantic_key=="point:preview";
    });
    require(marker!=scene.points.end()&&marker->reference.instance_path==second_path.encoded(),
        "Nested Sketch preview is missing or belongs to the wrong occurrence");
    near(marker->position.x,31);near(marker->position.y,12);near(marker->position.z,13);
    require(std::ranges::count_if(scene.points,[&](const auto& point){return point.reference.owner_id==sketch;})==1,
        "Nested Sketch preview leaked into another occurrence");
    require(std::ranges::any_of(scene.original_references.triangle_references,[&](const auto& ref){
        const auto path=assembly::InstancePath::decode(ref.instance_path);
        return !path.occurrence_ids.empty()&&path.occurrence_ids.front()==first;
    }),"Nested preview hid passive sibling geometry");
    const auto& after=f.live.open_assembly(parent)->session.document();
    require(f.live.open_assembly(parent)->session.revision()==revision&&after.components.size()==before.components.size(),
        "Viewing nested Sketch changed parent history");
    for(const auto& component:before.components)
        require(after.find_occurrence(component.occurrence_id)->calculated_source.shares_with(component.calculated_source),
            "Viewing nested Sketch published transient geometry into the parent");
}
void conversion(const kernel::OcctKernel& kernel,const fs::path& directory,bool extrusion) {
    Fixture f(kernel,directory,extrusion?"assembly-sketch-extrusion":"assembly-sketch-revolution");
    std::string sketch;
    if(extrusion) {
        sketch=f.run("sketch.create",{{"name","Circle"}}).at("sketch");
        f.run("sketch.circle.create",{{"sketch",sketch},{"center",{0,0}},{"radius_mm",1}});
    } else {
        sketch=f.rectangle(1,0,1,2);
        const auto axis=f.line(sketch,0,0,0,4);
        f.run("sketch.segment.centerline",{{"sketch",sketch},{"segment",axis},{"centerline",true}});
    }
    const auto definition=workspace::document_sketch(f.live,f.owner,sketch);
    f.run("value_lock.set",{{"object",definition.owner_container_id},{"key","profile_offset"},{"locked",true}});
    const auto owner=*f.doc().find_sketch_container(definition.owner_container_id);
    auto proposed=workspace::profile_from_sketch(f.doc(),sketch,extrusion?document::FeatureKind::Extrusion:document::FeatureKind::Revolution);
    require(proposed.combine_mode==document::CombineMode::Subtract,"Assembly profile factory offered addition");
    proposed.combine_mode=document::CombineMode::Add;
    const auto revision=f.state().session.revision();
    bool rejected=false;
    try{workspace::commit_assembly_profile(f.live,kernel,f.owner,proposed,{f.first},workspace::ProfileEditMode::TransformSketch);}
    catch(const workspace::ProfileOperationError&){rejected=true;}
    require(rejected&&f.state().session.revision()==revision&&f.doc().find_sketch_container(owner.id),"Domain accepted addition or changed rejected Sketch");
    Json args={{"sketch",sketch},{"targets",{f.first}}};if(extrusion)args["length_forward_mm"]=2;
    const std::string command=extrusion?"extrusion.create":"revolution.create";
    require(f.run(command.c_str(),args).at("container")==owner.id,"Conversion replaced owning container");
    const auto* cut=f.doc().find_cut(owner.id);
    require(cut&&cut->definition.container_origin==owner.container_origin&&cut->definition.combine_mode==document::CombineMode::Subtract&&
        f.doc().sketch_containers.empty(),"Conversion lost Origin, duplicated owner, or added material");
    near(f.volume(f.first),1000-(extrusion?2:6)*std::numbers::pi);near(f.volume(f.second),1000);
    require(cut->definition.value_locks.contains("profile_offset")&&!cut->definition.placement.value_locks.contains("profile_offset"),
        "Conversion lost or duplicated the offset lock");
    f.reject(extrusion?"extrusion.set":"revolution.set",{{"container",owner.id},{"profile_offset_mm",1}},"value_locked");
    f.reject("sketch.set",{{"sketch",sketch},{"plane_offset_mm",1}},"parameter_not_editable");
    require(f.run("sketch.set",{{"sketch",sketch},{"name",definition.name}}).at("changed")==false,"Owned locked Sketch no-op calculated");

    f.reject("sketch.delete",{{"sketch",sketch}},"profile_owned");
    f.run("undo");require(f.doc().cuts.empty()&&*f.doc().find_sketch_container(owner.id)==owner,"Conversion Undo lost standalone container");
    near(f.volume(f.first),1000);f.run("redo");near(f.volume(f.first),1000-(extrusion?2:6)*std::numbers::pi);
}
}
int main(){try {
    const auto root=fs::canonical(fs::temp_directory_path());
    const auto directory=root/("zima-assembly-sketch-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create fixture directory");
    kernel::OcctKernel kernel;standalone(kernel,directory);occurrence_reference(kernel,directory);
    conversion(kernel,directory,true);conversion(kernel,directory,false);dependencies(kernel,directory);nested_display(kernel,directory);
    require(fs::canonical(directory).parent_path()==root,"Unsafe cleanup");fs::remove_all(directory);
    std::cout<<"Assembly Sketch placement, occurrence references, native ownership, deletion and subtract-only conversion passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
