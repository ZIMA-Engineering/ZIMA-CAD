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
    f.run("undo");require(f.doc().cuts.empty()&&f.doc().sketches.front().owner_container_id.empty(),"Undo lost standalone profile");near(f.volume(f.first),1000);
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
    require(fs::create_directory(directory),"Cannot create fixture directory");kernel::OcctKernel kernel;extrusion(kernel,directory);revolution(kernel,directory);
    require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);
    std::cout<<"Assembly profiles: exact cuts, source ownership, shared draft edits, original end targets, native files and history passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
