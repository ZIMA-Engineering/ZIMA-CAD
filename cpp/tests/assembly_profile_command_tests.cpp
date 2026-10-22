#include "profile_command_fixture.hpp"
#include "assembly_profile_test_support.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <zima/document/feature_serialization.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
using namespace zima;
using commands::Json;
namespace fs = std::filesystem;
namespace {
using namespace assembly_profile_test;
void side_reversal(const kernel::OcctKernel& kernel,const fs::path& directory) {
    Fixture f(kernel,directory,"assembly-side-reversal");const auto source_before=f.live.open_part(f.source)->session.document().serialized();
    const auto sketch=f.rectangle(1,2,3,2);document::FeatureParameters p;
    p.sides[0].length=4;p.sides[1].operation=document::FeatureSideOperation::Extrusion;p.sides[1].length=1;
    const std::string id=f.create_feature(sketch,p,{{"targets",{f.first}}}).at("container");
    f.run("value_lock.set",{{"object",id},{"key","side0_length"},{"locked",true}});
    const auto before=f.doc().find_cut(id)->definition;
    const auto verify=[&](bool reversed) {
        near(f.volume(f.first),970);near(f.volume(f.second),1000);
        const auto& result=f.doc().find_occurrence(f.first)->calculated_source.get();
        require(result.volume_integrals.has_value(),"Assembly reversal has no current physical analysis");
        near(result.volume_integrals->centroid.z,(reversed?45.:-45.)/970.);
        const auto& current=f.doc().find_cut(id)->definition;
        require(current.feature.reversed_sides==reversed&&current.feature.sides==before.feature.sides&&current.value_locks==before.value_locks,
            "Assembly reversal moved definitions or locks between panels");
    };
    verify(false);f.set_feature(id,[](auto& parameters){parameters["reversed_sides"]=true;});verify(true);
    f.run("save");const auto reopened=assembly::AssemblyDocument::load(directory/"assembly-side-reversal.asmz");
    require(reopened.find_cut(id)->definition==f.doc().find_cut(id)->definition,"Assembly native persistence lost direction or side definitions");
    f.run("undo");verify(false);f.run("redo");verify(true);f.run("regenerate");verify(true);
    require(f.live.open_part(f.source)->session.document().serialized()==source_before,"Assembly reversal modified source Part");
}
void shared_feature(const kernel::OcctKernel& kernel,const fs::path& directory) {
    Fixture f(kernel,directory,"assembly-shared-feature");
    const auto sketch=f.rectangle(1,2,3,2);
    auto feature=workspace::profile_from_sketch(f.doc(),sketch,document::FeatureKind::Feature);
    feature.feature.sides[0].length=4;
    const auto id=feature.id;
    const auto created=f.run("feature.create",{{"sketch",sketch},{"parameters",document::serialize_feature_parameters(feature.feature)},{"targets",{f.first}}});
    require(created.at("container")==id&&created.at("parameters").at("type")=="modeling","Feature CLI did not use the shared definition");
    near(f.volume(f.first),976);near(f.volume(f.second),1000);
    feature=f.doc().find_cut(id)->definition;
    const auto revision=f.state().session.revision();
    require(!f.run("feature.set",{{"container",id},{"parameters",document::serialize_feature_parameters(feature.feature)}}).at("changed").get<bool>(),
        "Unchanged Feature CLI reported a change");
    require(f.state().session.revision()==revision,"Unchanged Assembly Feature committed or calculated");
    for(auto type:{document::FeatureType::Axis,document::FeatureType::Point,document::FeatureType::Plane,document::FeatureType::Sketch}) {
        feature=f.doc().find_cut(id)->definition;feature.feature.type=type;
        feature.feature.sides[0].length=7;feature.feature.sides[1].axis_enabled=false;
        f.run("feature.set",{{"container",id},{"parameters",document::serialize_feature_parameters(feature.feature)}});
        near(f.volume(f.first),1000);near(f.volume(f.second),1000);
        const auto stored=f.doc().find_cut(id)->definition;
        require(stored.feature_id==feature.feature_id&&stored.feature.sketch_id==sketch,"Assembly Feature type replaced native identity");
        require(f.doc().find_cut(id)->input_component_bodies.size()==f.doc().components.size(),"Datum Feature lost its Properties input boundary");
        require(!f.doc().owns_component_result(f.first),"Datum Feature pinned historical Part geometry");
        if(type==document::FeatureType::Axis) {
            const auto mesh=f.doc().construction_viewer_mesh();
            const auto axis=std::ranges::find_if(mesh.axes,[&](const auto& value){return value.reference.owner_id==id&&value.reference.semantic_key=="axis";});
            require(axis!=mesh.axes.end(),"Assembly did not render its shared Axis Feature");near(axis->display_length,7);
        }
        f.run("save");const auto restored=assembly::AssemblyDocument::load(directory/"assembly-shared-feature.asmz");
        require(restored.find_cut(id)->definition==stored,"Assembly Feature did not round-trip the shared Part definition");
    }
    f.run("undo");require(f.doc().find_cut(id)->definition.feature.type==document::FeatureType::Plane,"Feature type Undo lost ownership");
    f.run("redo");require(f.doc().find_cut(id)->definition.feature.type==document::FeatureType::Sketch,"Feature type Redo lost ownership");
    f.run("value_lock.set",{{"object",id},{"key","side0_length"},{"locked",true}});
    auto locked=f.doc().find_cut(id)->definition.feature;locked.sides[0].length+=1;
    f.reject("feature.set",{{"container",id},{"parameters",document::serialize_feature_parameters(locked)}},"value_locked");
    require(f.live.open_part(f.source)->session.document().history.size()==1,"Assembly Feature modified its source Part history");
}
void extrusion(const kernel::OcctKernel& kernel,const fs::path& directory) {
    Fixture f(kernel,directory,"assembly-extrusion");const auto sketch=f.rectangle(-1,-1.5,2,3);
    const auto source_revision=f.live.open_part(f.source)->session.revision();
    const auto made=f.run("feature.create",{{"sketch",sketch},{"parameters",zima::test::feature_profile_parameters(4)},{"targets",{f.first}}});
    const std::string cut=made.at("container");near(f.volume(f.first),976);near(f.volume(f.second),1000);
    near(f.doc().find_cut(cut)->input_component_bodies.at(f.first).volume,1000);
    const auto packet=f.doc().serialized();
    require(!packet.at("components")[0].at("operation_geometry").is_null() && packet.at("components")[1].at("operation_geometry").is_null(),
        "Assembly did not separate its cut result from the untouched source occurrence");
    require(!packet.at("operation_geometries").empty() && !packet.contains("source_geometries"),"Assembly cut result was not persisted in its own geometry table");
    require(f.doc().sketches.front().owner_container_id==cut,"Cut did not acquire its own Sketch");
    require(f.live.open_part(f.source)->session.revision()==source_revision,"Assembly cut changed source Part history");
    near(f.live.open_part(f.source)->session.calculated_boundaries().back().volume,1000);
    f.run("undo");require(f.doc().cuts.empty()&&f.doc().find_sketch_container(f.doc().sketches.front().owner_container_id)!=nullptr,"Undo lost standalone profile");near(f.volume(f.first),1000);
    f.run("redo");near(f.volume(f.first),976);
    const auto generation=f.state().session.data_generation();
    require(f.run("assembly.cut.list").at("items").front().at("targets")==Json::array({f.first}),"Cut query lost target identity");
    require(f.run("feature.get",{{"container",cut}}).at("sketch")==sketch&&f.state().session.data_generation()==generation,"Cut query calculated geometry");
    f.set_feature(cut,[](auto& p){p["sides"][0]["length"]=2;},{{"targets",{f.first,f.second}}});near(f.volume(f.first),988);near(f.volume(f.second),988);
    f.reject("feature.set",{{"container",cut},{"targets",{f.first,f.first}}},"duplicate_target");
    f.reject("feature.set",{{"container",cut},{"targets",{"missing"}}},"invalid_cut_target");
    f.reject("feature.set",{{"container",cut},{"targets",{27}}},"invalid_arguments");
    f.reject("feature.set",{{"container",cut},{"combine","add"}},"invalid_arguments");
    f.reject("feature.create",{{"sketch",sketch}},"profile_owned");
    f.interaction.editing=true;f.reject_feature(cut,[](auto& p){p["sides"][0]["length"]=3;},"editing_in_progress");f.interaction={};
    // Resolve a source cap through its exact occurrence path and persisted metadata.
    const auto geometry=f.doc().build_scene().original_references;
    const auto face=std::ranges::find_if(geometry.triangle_references,[&](const auto& ref){return ref.instance_path==assembly::InstancePath{}.child(f.first).encoded()&&ref.surface&&ref.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&ref.surface->axis.z>0.99&&!ref.surface->reversed;});
    require(face!=geometry.triangle_references.end(),"Missing original cap");
    f.set_feature(cut,[&](auto& p){p["sides"][0]["extrusion_extent"]="up_to";p["sides"][0]["targets"]=Json::array({{{"kind","face"},{"owner",face->owner_id},{"key",face->semantic_key},{"instance_path",face->instance_path},{"label",""},{"origin",{0,0,0}},{"normal",{0,0,1}},{"triangles",Json::array()}}});});
    near(f.volume(f.first),970);near(f.volume(f.second),970);
    require(f.doc().find_cut(cut)->definition.feature.sides[0].targets.front().reference.instance_path==face->instance_path,"End target lost occurrence identity");
    f.reject_feature(cut,[](auto& p){p["sides"][0]["targets"][0]["key"]="missing";},"missing_reference");
    f.set_feature(cut,[](auto& p){p["sides"][0]["extrusion_extent"]="length";p["sides"][0]["length"]=2;});
    auto draft=f.doc().sketches.front();for(auto& point:draft.points)point.x*=2;
    workspace::commit_assembly_profile(f.live,kernel,f.owner,f.doc().find_cut(cut)->definition,{f.first},workspace::ProfileEditMode::Replace,draft);
    near(f.volume(f.first),976);near(f.volume(f.second),1000);f.run("undo");near(f.volume(f.first),988);
    f.run("save");const auto loaded=assembly::AssemblyDocument::load(directory/"assembly-extrusion.asmz");
    require(loaded.cuts==f.doc().cuts&&loaded.sketches.front().serialized()==f.doc().sketches.front().serialized(),"Native cut lost definition or owned profile");near(loaded.find_occurrence(f.first)->calculated_source->volume,988);
    f.set_feature(cut,[](auto& p){p["sides"][0]["length"]=2;p["sides"][1]["operation"]="extrusion";p["sides"][1]["length"]=1;});near(f.volume(f.first),982);
    f.set_feature(cut,[](auto& p){p["symmetric"]=true;p["sides"][0]["length"]=2;});near(f.volume(f.first),976);
    f.set_feature(cut,[](auto& p){p["symmetric"]=false;p["sides"][1]["operation"]="none";p["sides"][0]["extrusion_extent"]="through_all";});near(f.volume(f.first),970);
    f.set_feature(cut,[](auto& p){p["sides"][1]["operation"]="extrusion";p["sides"][1]["extrusion_extent"]="through_all";});near(f.volume(f.first),940);
    f.set_feature(cut,[](auto& p){p["sides"][0]["extrusion_extent"]="length";p["sides"][0]["length"]=2;p["sides"][1]["operation"]="none";p["result_type"]="thin";p["thin_thickness"]=.25;p["thin_mode"]="symmetric";});near(f.volume(f.first),995);
    f.run("save");const auto thin=assembly::AssemblyDocument::load(directory/"assembly-extrusion.asmz");
    require(thin.find_cut(cut)->definition.feature==f.doc().find_cut(cut)->definition.feature,"Thin or extent parameters lost in Assembly format");
    f.set_feature(cut,[](auto& p){p["result_type"]="solid";});
    // The source may change without saving; failed edits must not expose a refresh.
    f.run("activate",{{"document",f.source}});zima::test::resize_rectangular_commands([&](const char* n,commands::Json a){return f.run(n,std::move(a));},{{"container",f.box},{"height_mm","20"}});f.run("activate",{{"document",f.owner}});
    f.reject_feature(cut,[](auto& p){p["sides"][0]["length"]=-1;},"invalid_arguments");
    f.set_feature(cut,[](auto& p){p["sides"][0]["length"]=3;});near(f.volume(f.first),1982);
    near(f.live.open_part(f.source)->session.calculated_boundaries().back().volume,2000);
    const std::string open=f.run("sketch.create",{{"name","Open profile"},{"plane","XY"}}).at("sketch");f.line(open,0,0,2,0);
    f.reject("feature.create",{{"sketch",open},{"targets",{f.first}}},"profile_rejected");
    f.run("new",{{"type","assembly"},{"name","nested-cut-target"}});const auto nested=f.live.active_document_id();
    const auto leaf=f.run("component.insert",{{"source",f.source}}).at("occurrence");f.run("save");f.run("activate",{{"document",f.owner}});
    const auto nested_occurrence=f.run("component.insert",{{"source",nested}}).at("occurrence");
    f.reject("feature.set",{{"container",cut},{"targets",{nested_occurrence}}},"invalid_cut_target");
    f.reject("feature.set",{{"container",cut},{"targets",{leaf}}},"invalid_cut_target");
    auto locked=f.doc().find_cut(cut)->definition;locked.value_locks.insert("side0_length");locked.placement.value_locks.insert("x");
    workspace::commit_assembly_profile(f.live,kernel,f.owner,locked,{f.first},workspace::ProfileEditMode::Replace);
    f.reject_feature(cut,[](auto& p){p["sides"][0]["length"]=4;},"value_locked");
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
    create["parameters"]=zima::test::feature_profile_parameters(4);
    if(!extrusion){create["parameters"]["sides"][0]["operation"]="revolution";create["parameters"]["sides"][0]["angle_degrees"]=360;}
    if(!extrusion) {
        const auto axis=f.line(sketch,0,0,0,4);
        f.run("sketch.segment.centerline",{{"sketch",sketch},{"segment",axis},{"centerline",true}});
    }
    const std::string cut=f.run("feature.create",create).at("container");
    const std::string command="placement.reference.set";
    const auto source_revision=f.live.open_part(f.source)->session.revision();
    const auto* source_cache=f.live.open_part(f.source)->session.calculated_boundaries().data();
    const auto initial=*f.doc().find_cut(cut);
    Json request={{"object",cut},{"index",0},{"reference",{{"owner",f.owner+":origin"},{"key","origin:plane:xy"}}},{"offset_mm",1}};
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
        f.run("feature.set",{{"container",cut},{"profile_plane","XY"}});
        near(f.volume(f.first),976);
        require(f.run("feature.get",{{"container",cut}}).at("profile_plane_auto")==false,
            "Assembly cut did not retain its manual work plane");
        f.run("feature.set",{{"container",cut},{"profile_plane","AUTO"}});
        near(f.volume(f.first),988);
    }
    require(f.doc().find_cut(cut)->definition.placement.references.front().instance_path==second_path,
        "Assembly profile reference merged repeated Part occurrences");
    auto bad=request;bad["reference"]["instance_path"]="missing";f.reject(command.c_str(),bad,"reference_not_found");
    bad=request;bad["reference"]={{"owner",sketch},{"key","sketch_origin"}};f.reject(command.c_str(),bad,"reference_not_available");
    bad=request;bad["index"]=1;f.reject(command.c_str(),bad,"duplicate_reference");
    bad=request;bad["index"]=4294967296LL;f.reject(command.c_str(),bad,"invalid_arguments");
    f.interaction.editing=true;f.reject(command.c_str(),request,"editing_in_progress");f.interaction={};
    // The same leaf ID beneath two instances of a subassembly is not one
    // reference. Both path levels must participate in lookup and persistence.
    f.run("new",{{"type","assembly"},{"name",name+"-nested"}});
    const auto nested=f.live.active_document_id();
    const std::string leaf=f.run("component.insert",{{"source",f.source}}).at("occurrence");f.run("save");
    f.run("activate",{{"document",f.owner}});
    // Insert reference sources before their consumer in the unified history.
    const auto history=f.doc().complete_history_order();
    const auto boundary=std::ranges::find(history,cut,&assembly::AssemblyDocument::HistoryEntry::id);
    require(boundary!=history.end(),"Reference consumer missing from history");
    f.run("history.cursor",{{"index",std::distance(history.begin(),boundary)}});
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
    near(f.doc().find_cut(cut)->definition.placement.z,3);
    const auto edit_geometry=workspace::placement_edit_geometry(f.live,f.owner,cut);
    const auto& placement=f.doc().find_cut(cut)->definition.placement;
    const auto measured=document::measure_placement_reference_offset(placement.references.front(),
        edit_geometry,{placement.x,placement.y,placement.z});
    require(measured.has_value(),"Locked nested plane distance is unavailable");
    // The leaf cap is at Z=5, its parent adds 2, and the cutter stays at 3.
    near(*measured,3-(5+2));
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
    document::FeatureParameters parameters;parameters.sides[0].operation=document::FeatureSideOperation::Revolution;parameters.sides[0].angle_degrees=360;
    auto initial=document::serialize_feature_parameters(parameters);initial.erase("sketch_id");
    f.reject("feature.create",{{"sketch",sketch},{"parameters",initial}},"profile_rejected");
    const auto axis=f.line(sketch,0,0,0,4);f.run("sketch.segment.centerline",{{"sketch",sketch},{"segment",axis},{"centerline",true}});
    const auto made=f.create_feature(sketch,parameters);const std::string cut=made.at("container");
    near(f.volume(f.first),1000-12*std::numbers::pi);near(f.volume(f.second),1000-12*std::numbers::pi);
    f.set_feature(cut,[](auto& p){p["sides"][0]["angle_degrees"]=180;},{{"targets",{f.second}}});near(f.volume(f.first),1000);near(f.volume(f.second),1000-6*std::numbers::pi);
    const auto revision=f.state().session.revision();
    require(!f.set_feature(cut,[](auto& p){p["axis_segment_id"]="missing";}).at("changed").get<bool>()&&f.state().session.revision()==revision,"Resolving the same construction axis committed a change");
    f.reject_feature(cut,[](auto& p){p["sides"][0]["angle_degrees"]=361;},"invalid_arguments");
    f.run("save");const auto loaded=assembly::AssemblyDocument::load(directory/"assembly-revolution.asmz");
    require(loaded.find_cut(cut)->definition.feature.axis_segment_id==axis,"Native revolution lost axis");near(loaded.find_occurrence(f.second)->calculated_source->volume,1000-6*std::numbers::pi);
}
}
int main(){try{
    const auto root=fs::canonical(fs::temp_directory_path());const auto directory=root/("zima-assembly-profile-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create fixture directory");kernel::OcctKernel kernel;extrusion(kernel,directory);revolution(kernel,directory);references(kernel,directory,true);references(kernel,directory,false);shared_feature(kernel,directory);side_reversal(kernel,directory);
    require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);
    std::cout<<"Assembly profiles: exact cuts, source ownership, shared draft edits, original end targets, native files and history passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
