#include <zima/command_host/host.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <zima/document/file_path.hpp>
#include <cmath>
#include <iostream>
#include <limits>
using namespace zima;
using commands::Json;
namespace fs = std::filesystem;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(double a, double b) { if (std::abs(a-b)>1e-7) throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a)); }
commands::Result run(command_host::Host& host, const char* command, Json args=Json::object()) {
    auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
    if(!result.ok) throw std::runtime_error(std::string(command)+": "+result.code+": "+result.message);
    return result;
}
void helpers() {
    kernel::ViewerReferenceGeometry geometry;
    geometry.axes.push_back({{}, {1,0,0}, 10, {"front", "axis", {}}});
    document::Placement value;
    value.references={{{},"front","axis",0,false,"front",true,true}};
    require(workspace::assign_placement_dimension(value,geometry,"rotation_x",12.5),"RX rejected");
    require(workspace::assign_placement_dimension(value,geometry,"rotation_y",37),"RY rejected");
    require(workspace::assign_placement_dimension(value,geometry,"rotation_z",23),"RZ rejected");
    near(value.rotation_offset_x,12.5);near(value.rotation_offset_z,23);
    near(value.absolute_rotation_y,37);near(value.rotation_y,37);near(value.rotation_offset_y,0);
    value.value_locks.insert("rotation_x");const auto locked=value;
    require(!workspace::assign_placement_dimension(value,geometry,"rotation_x",40)&&value==locked,"Locked RX changed");
    value.value_locks={"rotation_offset_x","rotation_offset_y"};const auto correction_locked=value;
    require(!workspace::assign_placement_dimension(value,geometry,"rotation_x",40)&&value==correction_locked,"Correction field lock was bypassed");
    require(workspace::assign_placement_dimension(value,geometry,"rotation_y",42),"Unused correction lock blocked free absolute angle");
    document::ConstructionObject point=document::PartDocument::create_construction(document::ConstructionKind::Point);
    point.references=value.references;
    require(workspace::assign_placement_dimension(point,geometry,"rotation_y",37),"Point absolute RY rejected");
    near(point.absolute_rotation.y,37);near(point.rotation_offset_y,0);
    point.value_locks.insert("placement:y"); const auto point_before=point;
    require(!workspace::assign_placement_dimension(point,geometry,"y",10)&&point==point_before,"Point placement lock lost");
    document::Placement offsets;
    offsets.references.push_back({});offsets.references.push_back(value.references[0]);
    offsets.references.push_back({{},"plane","plane",1,true});
    require(workspace::assign_placement_dimension(offsets,{},"reference_offset:0",-4),"Reference offset skipped wrong rows");
    near(offsets.references[2].offset,-4);
    offsets.references[2].offset_locked=true;const auto before=offsets;
    for(const auto* key:{"reference_offset:0","reference_offset:","reference_offset:-1","reference_offset:999999999999999999999999999999999999","bogus"})
        require(!workspace::assign_placement_dimension(offsets,{},key,2)&&offsets==before,"Invalid or locked offset partly changed");
    try { static_cast<void>(workspace::assign_placement_dimension(offsets,{},"x",std::numeric_limits<double>::infinity()));throw std::logic_error("Infinity accepted"); }
    catch(const std::invalid_argument&) { require(offsets==before,"Invalid number mutated placement"); }
}
std::array<double,6> bounds(const kernel::ViewerMesh& mesh) {
    std::array<double,6> b{1e100,1e100,1e100,-1e100,-1e100,-1e100};
    for(const auto& p:mesh.vertices) { const std::array<double,3> v{p.x,p.y,p.z};
        for(int i=0;i<3;++i){b[i]=std::min(b[i],v[i]);b[i+3]=std::max(b[i+3],v[i]);} }
    return b;
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;command_host::Interaction interaction;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    options.interaction=[&]{return interaction;};command_host::Host host(live,kernel,directory,options);
    require(host.execute_text("placement.get absent").code=="unsupported_document","Empty placement query accepted");
    run(host,"new",{{"type","part"},{"name","placement"}});const auto id=live.active_document_id();
    const auto feature=run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}}).data.at("container").get<std::string>();
    auto* state=live.open_part(id);const auto body=state->session.document().body_history.active_body_id();
    const auto initial_bounds=bounds(state->session.calculated_boundaries().back().mesh);
    const auto get=[&](const std::string& object){return run(host,"placement.get",{{"object",object}}).data;};
    const auto set=[&](const std::string& object,Json values){return run(host,"placement.set",{{"object",object},{"values",std::move(values)}}).data;};
    const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    require(get(feature).at("coordinate_system")=="body"&&get(body).at("coordinate_owner")==id,"Placement frame ownership lost");
    require(state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache&&!host.change(),"Placement get changed model");
    set(feature,{{"x",5},{"y",7},{"z",11},{"rotation_z",90}});
    const auto feature_bounds=bounds(state->session.calculated_boundaries().back().mesh);
    near(feature_bounds[0],5-initial_bounds[4]);near(feature_bounds[3],5-initial_bounds[1]);
    near(feature_bounds[1],7+initial_bounds[0]);near(feature_bounds[4],7+initial_bounds[3]);
    near(feature_bounds[2],11+initial_bounds[2]);near(feature_bounds[5],11+initial_bounds[5]);
    near(state->session.calculated_boundaries().back().volume,6000);
    run(host,"undo");require(bounds(state->session.calculated_boundaries().back().mesh)==initial_bounds,"Undo did not restore exact body cache");run(host,"redo");
    const auto unchanged_revision=state->session.revision();const auto* unchanged_cache=state->session.calculated_boundaries().data();
    require(set(feature,{{"x",5}}).at("changed")==false&&state->session.revision()==unchanged_revision&&state->session.calculated_boundaries().data()==unchanged_cache,"No-op placement recalculated");
    for(const auto& values:std::vector<Json>{{{"x",9},{"invalid",7}},{{"x",true}},{{"x","4"}},Json::object()}) {
        const auto result=host.execute({{"command","placement.set"},{"arguments",{{"object",feature},{"values",values}}}});
        require(!result.ok&&state->session.revision()==unchanged_revision&&state->session.calculated_boundaries().data()==unchanged_cache,"Invalid patch partly committed");
    }
    require(host.execute({{"command","placement.set"},{"arguments",{{"object",body},{"values",{{"x",10}}}}}}).code=="parameter_not_editable","Body reference constraint bypassed");
    set(body,{{"reference_offset:2",100},{"rotation_z",90}});
    const auto body_bounds=bounds(state->session.calculated_boundaries().back().mesh);
    near(body_bounds[0],100-feature_bounds[4]);near(body_bounds[3],100-feature_bounds[1]);
    near(body_bounds[1],feature_bounds[0]);near(body_bounds[4],feature_bounds[3]);
    near(state->session.calculated_boundaries().back().volume,6000);
    // Point/Plane commits reuse calculated bodies and keep an atomic undo entry.
    auto point=document::PartDocument::create_construction(document::ConstructionKind::Point);point.origin={1,2,3};
    static_cast<void>(workspace::commit_construction(live,id,point,workspace::ConstructionEditMode::Create));
    const auto shape=state->session.calculated_boundaries().back().kernel_shape;
    set(point.id,{{"x",8},{"z",9}});
    near(state->session.document().find_construction(point.id)->origin.x,8);
    require(state->session.calculated_boundaries().back().kernel_shape==shape,"Construction edit calculated a body");
    run(host,"undo");near(state->session.document().find_construction(point.id)->origin.x,1);run(host,"redo");
    auto referenced_curve=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);
    referenced_curve.origin={50,60,70};referenced_curve.rotation={0,0,90};referenced_curve.absolute_rotation=referenced_curve.rotation;
    for(int i=0;i<2;++i){
        auto child=document::PartDocument::create_construction(document::ConstructionKind::Point);
        child.parent_construction_id=referenced_curve.id;child.origin={double(i*10),0,0};
        if(i==1)child.references.push_back({{},body+":origin","origin:plane:xz",0,true});
        referenced_curve.curve_points.push_back(child);
    }
    static_cast<void>(workspace::commit_construction(live,id,referenced_curve,workspace::ConstructionEditMode::Create));
    const auto local_child=referenced_curve.curve_points[1].id;
    // In Body coordinates Y=60+local X; the Body XZ plane therefore fixes X=-60.
    near(state->session.document().find_construction(local_child)->origin.x,-60);
    require(host.execute({{"command","placement.set"},{"arguments",{{"object",local_child},{"values",{{"x",2}}}}}}).code=="parameter_not_editable",
        "A Body plane constrained the wrong axis in the rotated Curve frame");
    set(local_child,{{"y",5}});near(state->session.document().find_construction(local_child)->origin.y,5);
    near(state->session.document().find_construction(local_child)->origin.x,-60);
    const auto second_body=run(host,"body.create",{{"name","Other"}}).data.at("body").get<std::string>();
    require(host.execute({{"command","placement.set"},{"arguments",{{"object",feature},{"values",{{"x",2}}}}}}).code=="inactive_body",
        "Placement edit bypassed active Body ownership");
    run(host,"body.activate",{{"body",body}});
    auto locked=state->session.document();locked.find_container(feature)->placement.value_locks.insert("y");
    auto locked_body=*locked.body_history.find(body);locked_body.scope.placement.value_locks.insert("rotation_offset_z");locked.body_history.update_body(std::move(locked_body));
    state->session.commit(locked,state->session.calculated_boundaries());const auto locked_revision=state->session.revision();
    require(host.execute({{"command","placement.set"},{"arguments",{{"object",feature},{"values",{{"x",20},{"y",99}}}}}}).code=="parameter_not_editable"&&state->session.revision()==locked_revision,"Multi-value patch overrode lock or committed its first field");
    require(host.execute({{"command","placement.set"},{"arguments",{{"object",body},{"values",{{"rotation_z",20}}}}}}).code=="parameter_not_editable"&&state->session.revision()==locked_revision,
        "CLI bypassed the Properties correction-angle lock");
    auto bad=*state->session.document().find_construction(point.id);bad.references.push_back({{},"missing","point"});
    try {static_cast<void>(workspace::commit_construction(live,id,bad,workspace::ConstructionEditMode::Replace));throw std::logic_error("Missing reference accepted");}
    catch(const std::runtime_error&) {require(state->session.revision()==locked_revision,"Failed construction solve committed");}
    interaction.editing=true;require(host.execute({{"command","placement.set"},{"arguments",{{"object",point.id},{"values",{{"x",10}}}}}}).code=="editing_in_progress","Pending edit overwritten");interaction={};
    run(host,"save");std::vector<kernel::BodyResult> saved;const auto native=document::PartDocument::load(directory/"placement.prtz",&saved);
    near(native.find_container(feature)->placement.x,5);near(native.find_construction(point.id)->origin.z,9);near(saved.back().volume,6000);
    run(host,"new",{{"type","assembly"},{"name","placement-assembly"}});const auto assembly_id=live.active_document_id();
    auto curve=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);curve.origin={50,60,70};curve.rotation={0,0,90};curve.absolute_rotation=curve.rotation;
    for(int i=0;i<2;++i){auto child=document::PartDocument::create_construction(document::ConstructionKind::Point);child.parent_construction_id=curve.id;child.origin={double(i*10),0,0};curve.curve_points.push_back(child);}
    static_cast<void>(workspace::commit_construction(live,assembly_id,curve,workspace::ConstructionEditMode::Create));
    const auto child=curve.curve_points[1].id;set(child,{{"y",5}});
    require(get(child).at("coordinate_owner")==curve.id,"Assembly Curve Point lost its local frame");
    const auto& assembly=live.open_assembly(assembly_id)->session.document();
    near(assembly.find_construction(child)->origin.y,5);near(assembly.find_construction(curve.id)->origin.x,50);
    run(host,"undo");near(live.open_assembly(assembly_id)->session.document().find_construction(child)->origin.y,0);run(host,"redo");
    run(host,"save");const auto restored=assembly::AssemblyDocument::load(directory/"placement-assembly.asmz");near(restored.find_construction(child)->origin.y,5);
    require(run(host,"placement.get",{{"object",point.id},{"document",id}}).data.at("body")==body&&live.active_document_id()==assembly_id,"Explicit source query changed activation");
}
}
int main(){try{
    helpers();const auto root=fs::canonical(fs::temp_directory_path());
    const auto directory=root/("zima-placement-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create fixture directory");kernel::OcctKernel kernel;verify(kernel,directory);
    require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);
    std::cout<<"Placement: mixed reference angles, atomic values, locks, independent transformed bounds, native persistence, Part/Assembly and history passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
