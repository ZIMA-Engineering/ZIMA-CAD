#include "assembly_profile_test_support.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
using namespace zima;
using commands::Json;
namespace fs = std::filesystem;
namespace {
using namespace assembly_profile_test;
void extrusion(const kernel::OcctKernel& kernel,const fs::path& directory) {
    Fixture f(kernel,directory,"assembly-extrusion");const auto sketch=f.rectangle(-1,-1.5,2,3);
    const auto source_revision=f.live.open_part(f.source)->session.revision();
    const auto made=f.run("extrusion.create",{{"sketch",sketch},{"length_forward_mm",4},{"targets",{f.first}}});
    const std::string cut=made.at("container");near(f.volume(f.first),976);near(f.volume(f.second),1000);
    near(f.doc().find_cut(cut)->input_component_bodies.at(f.first).volume,1000);
    require(f.doc().sketches.front().owner_container_id==cut,"Cut did not acquire its own Sketch");
    require(f.live.open_part(f.source)->session.revision()==source_revision,"Assembly cut changed source Part history");
    near(f.live.open_part(f.source)->session.calculated_boundaries().back().volume,1000);
    f.run("undo");require(f.doc().cuts.empty()&&f.doc().find_sketch_container(f.doc().sketches.front().owner_container_id)!=nullptr,"Undo lost standalone profile");near(f.volume(f.first),1000);
    f.run("redo");near(f.volume(f.first),976);
    const auto generation=f.state().session.data_generation();
    require(f.run("assembly.cut.list").at("items").front().at("targets")==Json::array({f.first}),"Cut query lost target identity");
    require(f.run("extrusion.get",{{"container",cut}}).at("sketch")==sketch&&f.state().session.data_generation()==generation,"Cut query calculated geometry");
    f.run("extrusion.set",{{"container",cut},{"length_forward_mm",2},{"targets",{f.first,f.second}}});near(f.volume(f.first),988);near(f.volume(f.second),988);
    f.reject("extrusion.set",{{"container",cut},{"targets",{f.first,f.first}}},"duplicate_target");
    f.reject("extrusion.set",{{"container",cut},{"targets",{"missing"}}},"invalid_cut_target");
    f.reject("extrusion.set",{{"container",cut},{"targets",{27}}},"invalid_arguments");
    f.reject("extrusion.set",{{"container",cut},{"combine","add"}},"invalid_arguments");
    f.reject("extrusion.create",{{"sketch",sketch}},"profile_owned");
    f.reject("revolution.get",{{"container",cut}},"wrong_feature");
    f.interaction.editing=true;f.reject("extrusion.set",{{"container",cut},{"length_forward_mm",3}},"editing_in_progress");f.interaction={};
    // Resolve a source cap through its exact occurrence path and persisted metadata.
    const auto geometry=f.doc().build_scene().original_references;
    const auto face=std::ranges::find_if(geometry.triangle_references,[&](const auto& ref){return ref.instance_path==assembly::InstancePath{}.child(f.first).encoded()&&ref.surface&&ref.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&ref.surface->axis.z>0.99&&!ref.surface->reversed;});
    require(face!=geometry.triangle_references.end(),"Missing original cap");
    f.run("extrusion.set",{{"container",cut},{"end_forward","up_to"},{"targets_forward",Json::array({{{"owner",face->owner_id},{"key",face->semantic_key},{"instance_path",face->instance_path}}})}});
    near(f.volume(f.first),970);near(f.volume(f.second),970);
    require(f.doc().find_cut(cut)->definition.extrusion.end_targets_forward.front().reference.instance_path==face->instance_path,"End target lost occurrence identity");
    f.reject("extrusion.set",{{"container",cut},{"targets_forward",Json::array({{{"owner",face->owner_id},{"key","missing"},{"instance_path",face->instance_path}}})}},"missing_reference");
    f.run("extrusion.set",{{"container",cut},{"end_forward","length"},{"length_forward_mm",2}});
    auto draft=f.doc().sketches.front();for(auto& point:draft.points)point.x*=2;
    workspace::commit_assembly_profile(f.live,kernel,f.owner,f.doc().find_cut(cut)->definition,{f.first},workspace::ProfileEditMode::Replace,draft);
    near(f.volume(f.first),976);near(f.volume(f.second),1000);f.run("undo");near(f.volume(f.first),988);
    f.run("save");const auto loaded=assembly::AssemblyDocument::load(directory/"assembly-extrusion.asmz");
    require(loaded.cuts==f.doc().cuts&&loaded.sketches.front().serialized()==f.doc().sketches.front().serialized(),"Native cut lost definition or owned profile");near(loaded.find_occurrence(f.first)->calculated_source->volume,988);
    f.run("extrusion.set",{{"container",cut},{"extent","two_sides"},{"length_forward_mm",2},{"length_reverse_mm",1}});near(f.volume(f.first),982);
    f.run("extrusion.set",{{"container",cut},{"extent","symmetric"},{"length_forward_mm",2}});near(f.volume(f.first),976);
    f.run("extrusion.set",{{"container",cut},{"extent","one_side"},{"end_forward","through_all"}});near(f.volume(f.first),970);
    f.run("extrusion.set",{{"container",cut},{"extent","two_sides"},{"end_reverse","through_all"}});near(f.volume(f.first),940);
    f.run("extrusion.set",{{"container",cut},{"end_forward","length"},{"extent","one_side"},{"result_type","thin"},{"thin_thickness_mm",.25},{"thin_mode","symmetric"},{"length_forward_mm",2}});near(f.volume(f.first),995);
    f.run("save");const auto thin=assembly::AssemblyDocument::load(directory/"assembly-extrusion.asmz");
    require(thin.find_cut(cut)->definition.extrusion==f.doc().find_cut(cut)->definition.extrusion,"Thin or extent parameters lost in Assembly format");
    f.run("extrusion.set",{{"container",cut},{"result_type","solid"}});
    // The source may change without saving; failed edits must not expose a refresh.
    f.run("activate",{{"document",f.source}});f.run("box.set",{{"container",f.box},{"height_mm","20"}});f.run("activate",{{"document",f.owner}});
    f.reject("extrusion.set",{{"container",cut},{"length_forward_mm",-1}},"invalid_arguments");
    f.run("extrusion.set",{{"container",cut},{"length_forward_mm",3}});near(f.volume(f.first),1982);
    near(f.live.open_part(f.source)->session.calculated_boundaries().back().volume,2000);
    const std::string open=f.run("sketch.create",{{"name","Open profile"},{"plane","XY"}}).at("sketch");f.line(open,0,0,2,0);
    f.reject("extrusion.create",{{"sketch",open},{"targets",{f.first}}},"profile_rejected");
    f.run("new",{{"type","assembly"},{"name","nested-cut-target"}});const auto nested=f.live.active_document_id();
    const auto leaf=f.run("component.insert",{{"source",f.source}}).at("occurrence");f.run("save");f.run("activate",{{"document",f.owner}});
    const auto nested_occurrence=f.run("component.insert",{{"source",nested}}).at("occurrence");
    f.reject("extrusion.set",{{"container",cut},{"targets",{nested_occurrence}}},"invalid_cut_target");
    f.reject("extrusion.set",{{"container",cut},{"targets",{leaf}}},"invalid_cut_target");
    auto locked=f.doc().find_cut(cut)->definition;locked.value_locks.insert("length_forward");locked.placement.value_locks.insert("x");
    workspace::commit_assembly_profile(f.live,kernel,f.owner,locked,{f.first},workspace::ProfileEditMode::Replace);
    f.reject("extrusion.set",{{"container",cut},{"length_forward_mm",4}},"value_locked");
    f.run("save");const auto locked_saved=assembly::AssemblyDocument::load(directory/"assembly-extrusion.asmz");
    require(locked_saved.find_cut(cut)->definition.value_locks==locked.value_locks&&locked_saved.find_cut(cut)->definition.placement.value_locks==locked.placement.value_locks,"Assembly format lost profile or placement locks");

}
void references(const kernel::OcctKernel& kernel,const fs::path& directory,bool extrusion) {
    const std::string prefix=extrusion?"extrusion":"revolution",name="assembly-"+prefix+"-references";
    Fixture f(kernel,directory,name);
    const auto second_path=assembly::InstancePath{}.child(f.second).encoded();
    const auto first_path=assembly::InstancePath{}.child(f.first).encoded();
    f.run("component.set",{{"instance_path",second_path},{"placement",{{"z_mm",2}}}});
    const auto sketch=extrusion?f.rectangle(-1,-1.5,2,3):f.rectangle(1,0,1,2);
    // This scenario follows reference planes; explicit XY in the generic
    // rectangle fixture now deliberately means a persistent manual plane.
    Json create={{"sketch",sketch},{"targets",{f.first}},{"profile_plane","AUTO"}};
    if(extrusion)create["length_forward_mm"]=4;
    else {
        const auto axis=f.line(sketch,0,0,0,4);
        f.run("sketch.segment.centerline",{{"sketch",sketch},{"segment",axis},{"centerline",true}});
    }
    const std::string cut=f.run((prefix+".create").c_str(),create).at("container");
    const auto command=prefix+".reference.set";
    const auto source_revision=f.live.open_part(f.source)->session.revision();
    const auto* source_cache=f.live.open_part(f.source)->session.calculated_boundaries().data();
    const auto initial=*f.doc().find_cut(cut);
    Json request={{"container",cut},{"index",0},{"reference",{{"owner",f.owner+":origin"},{"key","origin:plane:xy"}}},{"offset_mm",1}};
    require(f.run(command.c_str(),request).at("changed")==true,"Assembly profile reference did not report its commit");
    near(f.doc().find_cut(cut)->definition.placement.z,1);
    near(f.volume(f.first),1000-(extrusion?24:6*std::numbers::pi));near(f.volume(f.second),1000);
    const auto assigned=*f.doc().find_cut(cut);
    const auto revision=f.state().session.revision(),generation=f.state().session.data_generation();
    require(f.run(command.c_str(),request).at("changed")==false&&f.state().session.revision()==revision&&
        f.state().session.data_generation()==generation&&!f.host.change(),"Repeated Assembly profile reference calculated or added history");
    f.run("undo");require(f.doc().find_cut(cut)->definition==initial.definition,"Assembly reference Undo changed its input");
    f.run("redo");require(f.doc().find_cut(cut)->definition==assigned.definition,"Assembly reference Redo changed native identities");
    const auto geometry=f.doc().build_scene().original_references;
    const auto face=std::ranges::find_if(geometry.triangle_references,[&](const auto& ref){return ref.instance_path==first_path&&ref.surface&&
        ref.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&ref.surface->origin.z>4.99&&std::abs(ref.surface->axis.z)>.99;});
    require(face!=geometry.triangle_references.end(),"Assembly reference fixture has no original top face");
    request["reference"]={{"owner",face->owner_id},{"key",face->semantic_key},{"instance_path",first_path}};request["offset_mm"]=-4;
    // Replacing a positional source replaces its automatic orientation twin.
    // The old FRONT must not survive and conflict with the new source.
    f.run(command.c_str(),request);near(f.doc().find_cut(cut)->definition.placement.z,1);
    const auto replaced=*f.doc().find_cut(cut);
    require(std::ranges::all_of(replaced.definition.placement.references,[&](const auto& ref) {
        return ref.owner_id==face->owner_id&&ref.semantic_key==face->semantic_key&&ref.instance_path==first_path;
    }),"Replacing a source retained its obsolete orientation twin");
    f.run("undo");require(f.doc().find_cut(cut)->definition==assigned.definition,"Reference replacement Undo lost the previous source");
    f.run("redo");require(f.doc().find_cut(cut)->definition==replaced.definition,"Reference replacement Redo changed occurrence identity");
    f.run("undo");
    f.run("undo"); // Return to the original cutter with empty reference rows.
    request["derive_orientation"]=false;
    f.run(command.c_str(),request);near(f.doc().find_cut(cut)->definition.placement.z,1);
    request["reference"]["instance_path"]=second_path;
    f.run(command.c_str(),request);near(f.doc().find_cut(cut)->definition.placement.z,3);
    near(f.volume(f.first),1000-(extrusion?12:6*std::numbers::pi));near(f.volume(f.second),1000);
    if (extrusion) {
        f.run("extrusion.set",{{"container",cut},{"profile_plane","XY"}});
        near(f.volume(f.first),976);
        require(f.run("extrusion.get",{{"container",cut}}).at("profile_plane_auto")==false,
            "Assembly cut did not retain its manual work plane");
        f.run("extrusion.set",{{"container",cut},{"profile_plane","AUTO"}});
        near(f.volume(f.first),988);
    }
    require(f.doc().find_cut(cut)->definition.placement.references.front().instance_path==second_path,
        "Assembly profile reference merged repeated Part occurrences");
    auto bad=request;bad["reference"]["instance_path"]="missing";f.reject(command.c_str(),bad,"reference_not_found");
    bad=request;bad["reference"]={{"owner",sketch},{"key","sketch_origin"}};f.reject(command.c_str(),bad,"reference_not_available");
    bad=request;bad["index"]=1;f.reject(command.c_str(),bad,"duplicate_reference");
    bad=request;bad["index"]=4294967296LL;f.reject(command.c_str(),bad,"invalid_arguments");
    f.reject(extrusion?"revolution.reference.set":"extrusion.reference.set",request,"wrong_feature");
    f.interaction.editing=true;f.reject(command.c_str(),request,"editing_in_progress");f.interaction={};
    // The same leaf ID beneath two instances of a subassembly is not one
    // reference. Both path levels must participate in lookup and persistence.
    f.run("new",{{"type","assembly"},{"name",name+"-nested"}});
    const auto nested=f.live.active_document_id();
    const std::string leaf=f.run("component.insert",{{"source",f.source}}).at("occurrence");f.run("save");
    f.run("activate",{{"document",f.owner}});
    const std::string parent1=f.run("component.insert",{{"source",nested}}).at("occurrence");
    const std::string parent2=f.run("component.insert",{{"source",nested}}).at("occurrence");
    f.run("component.set",{{"instance_path",assembly::InstancePath{}.child(parent2).encoded()},{"placement",{{"z_mm",2}}}});
    request["reference"]["instance_path"]=assembly::InstancePath{}.child(parent1).child(leaf).encoded();
    f.run(command.c_str(),request);near(f.doc().find_cut(cut)->definition.placement.z,1);
    near(f.volume(f.first),1000-(extrusion?24:6*std::numbers::pi));
    const auto nested_path=assembly::InstancePath{}.child(parent2).child(leaf).encoded();
    request["reference"]["instance_path"]=nested_path;
    f.run(command.c_str(),request);near(f.doc().find_cut(cut)->definition.placement.z,3);
    near(f.volume(f.first),1000-(extrusion?12:6*std::numbers::pi));near(f.volume(f.second),1000);
    require(f.doc().find_cut(cut)->definition.placement.references.front().instance_path==nested_path,
        "Assembly reference lost a parent occurrence from its path");
    bad=request;bad["reference"]["instance_path"]=assembly::InstancePath{}.child(leaf).encoded();
    f.reject(command.c_str(),bad,"reference_not_found");
    auto locked=f.doc().find_cut(cut)->definition;locked.placement.references.front().offset_locked=true;
    workspace::commit_assembly_profile(f.live,kernel,f.owner,locked,{f.first},workspace::ProfileEditMode::Replace);
    request["offset_mm"]=99;f.run(command.c_str(),request);near(f.doc().find_cut(cut)->definition.placement.z,3);
    require(f.doc().find_cut(cut)->definition.placement.references.front().offset_locked,"Reference input removed the locked offset");
    require(f.live.open_part(f.source)->session.revision()==source_revision&&
        f.live.open_part(f.source)->session.calculated_boundaries().data()==source_cache,
        "Assembly reference recalculated or edited its source Part");
    const auto bound=f.doc().find_cut(cut)->definition;const auto cut_volume=f.volume(f.first);
    require(f.run("placement.reference.remove",{{"object",cut},{"index",0}}).at("body_calculated")==true,"Assembly profile removal did not calculate its cutter");
    require(f.doc().find_cut(cut)->definition.placement.references.empty(),"Assembly profile retained removed source");near(f.volume(f.first),cut_volume);near(f.volume(f.second),1000);
    const auto freed=f.doc().find_cut(cut)->definition;f.run("undo");require(f.doc().find_cut(cut)->definition==bound,"Assembly removal Undo lost the exact original reference");f.run("redo");require(f.doc().find_cut(cut)->definition==freed,"Assembly removal Redo lost owned profile");
    require(f.live.open_part(f.source)->session.revision()==source_revision&&f.live.open_part(f.source)->session.calculated_boundaries().data()==source_cache,"Assembly removal modified its source Part");
    f.run("save");const auto saved=assembly::AssemblyDocument::load(directory/(name+".asmz"));
    require(saved.find_cut(cut)->definition==f.doc().find_cut(cut)->definition&&
        saved.find_cut(cut)->target_occurrence_ids==std::vector<std::string>{f.first}&&
        saved.sketches.front().id==sketch&&saved.sketches.front().owner_container_id==cut,
        "Native Assembly lost its reference, target or owned profile identity");
}
void revolution(const kernel::OcctKernel& kernel,const fs::path& directory) {
    Fixture f(kernel,directory,"assembly-revolution");const auto sketch=f.rectangle(1,0,1,4);
    f.reject("revolution.create",{{"sketch",sketch}},"profile_rejected");
    const auto axis=f.line(sketch,0,0,0,4);f.run("sketch.segment.centerline",{{"sketch",sketch},{"segment",axis},{"centerline",true}});
    const auto made=f.run("revolution.create",{{"sketch",sketch},{"axis",axis},{"angle_degrees",360}});const std::string cut=made.at("container");
    near(f.volume(f.first),1000-12*std::numbers::pi);near(f.volume(f.second),1000-12*std::numbers::pi);
    f.run("revolution.set",{{"container",cut},{"angle_degrees",180},{"targets",{f.second}}});near(f.volume(f.first),1000);near(f.volume(f.second),1000-6*std::numbers::pi);
    f.reject("revolution.set",{{"container",cut},{"axis","missing"}},"invalid_reference");
    f.reject("revolution.set",{{"container",cut},{"angle_degrees",361}},"invalid_arguments");
    f.run("save");const auto loaded=assembly::AssemblyDocument::load(directory/"assembly-revolution.asmz");
    require(loaded.find_cut(cut)->definition.revolution.axis_segment_id==axis,"Native revolution lost axis");near(loaded.find_occurrence(f.second)->calculated_source->volume,1000-6*std::numbers::pi);
}
}
int main(){try{
    const auto root=fs::canonical(fs::temp_directory_path());const auto directory=root/("zima-assembly-profile-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create fixture directory");kernel::OcctKernel kernel;extrusion(kernel,directory);revolution(kernel,directory);references(kernel,directory,true);references(kernel,directory,false);
    require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);
    std::cout<<"Assembly profiles: exact cuts, source ownership, shared draft edits, original end targets, native files and history passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
