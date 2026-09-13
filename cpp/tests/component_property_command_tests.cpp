#include <zima/command_host/host.hpp>
#include <zima/workspace/component_properties.hpp>
#include <iostream>
#include <algorithm>
#include <array>
#include <cmath>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void near(double actual,double expected){if(std::abs(actual-expected)>1e-6)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;bool editing=false;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    options.interaction=[&]{command_host::Interaction value;value.editing=editing;return value;};
    command_host::Host host(live,kernel,dir,options);
    run(host,"new",{{"type","part"},{"name","source"}});const auto source=live.active_document_id();
    const auto box=run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}}).data.at("container").get<std::string>();run(host,"save");
    const auto source_revision=live.open_part(source)->session.revision();
    run(host,"new",{{"type","assembly"},{"name","properties"}});const auto id=live.active_document_id();
    const auto first=run(host,"component.insert",{{"source",source}}).data.at("occurrence").get<std::string>();
    const auto second=run(host,"component.insert",{{"source",source}}).data.at("occurrence").get<std::string>();
    const auto path=[](const std::string& occurrence){return assembly::InstancePath{}.child(occurrence).encoded();};
    const auto set=[&](const std::string& occurrence,Json value){value["instance_path"]=path(occurrence);return run(host,"component.set",std::move(value));};
    const auto get=[&](const std::string& occurrence){return run(host,"component.get",{{"instance_path",path(occurrence)}}).data;};
    const auto current=[&](const std::string& occurrence)->const assembly::PartOccurrence&{return *live.open_assembly(id)->session.document().find_occurrence(occurrence);};
    const auto original=current(second).calculated_source;
    const auto rejected=[&](Json args,const std::string& code=std::string{}) {
        if(!args.contains("instance_path"))args["instance_path"]=path(second);
        const auto revision=live.open_assembly(id)->session.revision(),generation=live.open_assembly(id)->session.data_generation();
        const auto before=workspace::component_properties(current(second));const auto packet=current(second).calculated_source;
        const auto result=host.execute({{"command","component.set"},{"arguments",args}});
        require(!result.ok,"Invalid component property command accepted");
        if(!code.empty()&&result.code!=code)throw std::runtime_error("Expected error "+code+", got "+result.code+": "+result.message);
        require(live.open_assembly(id)->session.revision()==revision&&live.open_assembly(id)->session.data_generation()==generation&&
            workspace::component_properties(current(second))==before&&current(second).calculated_source.shares_with(packet),("Rejected properties changed document/packet: "+args.dump()+" code="+result.code+" rev="+std::to_string(revision)+"->"+std::to_string(live.open_assembly(id)->session.revision())+" gen="+std::to_string(generation)+"->"+std::to_string(live.open_assembly(id)->session.data_generation())).c_str());
    };
    set(first,{{"grounded",false},{"placement",{{"x_mm",10},{"y_mm",11},{"z_mm",12}}}});set(first,{{"grounded",true}});
    set(second,{{"name","Druhý šroub"},{"visible",false},{"placement",{{"x_mm",40},{"y_mm",20},{"z_mm",30}}}});
    require(current(second).name=="Druhý šroub"&&!current(second).visible&&current(first).visible,"Occurrence properties leaked to another instance");
    require(current(second).calculated_source.shares_with(original),"Properties recalculated source geometry");
    run(host,"undo");require(current(second).visible,"Property patch did not undo together");run(host,"redo");
    const auto revision=live.open_assembly(id)->session.revision(),generation=live.open_assembly(id)->session.data_generation();
    require(!set(second,{{"name","Druhý šroub"}}).data.at("changed").get<bool>()&&live.open_assembly(id)->session.revision()==revision&&
        live.open_assembly(id)->session.data_generation()==generation,"No-op properties changed history");
    set(second,{{"visible",true}});
    const auto ref=[&](const std::string& occurrence,const std::string& owner,const std::string& key){return Json{{"instance_path",occurrence.empty()?"":path(occurrence)},{"owner",owner},{"key",key}};};
    const auto row=[&](const char* kind,const char* key,double offset=0){return Json{{"kind",kind},{"component",ref(second,source+":origin",key)},
        {"target",ref(first,source+":origin",key)},{"offset",offset}};};
    auto plane=row("plane_coincident","origin:plane:xy",7);
    set(second,{{"placement_references",Json::array({plane})}});near(current(second).placement.z,19);near(current(second).placement.x,40);
    require(live.open_assembly(id)->session.document().remaining_degrees_of_freedom(second)==3,"Plane mate did not constrain expected freedoms");
    rejected({{"placement",{{"z_mm",20}}}},"constrained_coordinate");
    run(host,"value_lock.set",{{"object",second},{"key","placement:x"},{"locked",true}});
    rejected({{"placement",{{"x_mm",41}}}},"value_locked");
    run(host,"value_lock.set",{{"object",second},{"key","placement:x"},{"locked",false}});
    set(second,{{"placement",{{"x_mm",41}}}});near(current(second).placement.z,19);
    plane["offset"]=9;plane["locked"]=true;plane["lower_limit"]=-5;plane["upper_limit"]=20;
    set(second,{{"placement_references",Json::array({plane})}});near(current(second).placement.z,21);
    plane["offset"]=14;rejected({{"placement_references",Json::array({plane})}},"value_locked");
    plane["locked"]=false;set(second,{{"placement_references",Json::array({plane})}});near(current(second).placement.z,26);
    plane["offset"]=25;rejected({{"placement_references",Json::array({plane})}},"invalid_arguments");
    plane["offset"]=14;plane["lower_limit"]=21;rejected({{"placement_references",Json::array({plane})}},"invalid_arguments");
    auto angle=row("plane_angle","origin:plane:xy",30);set(second,{{"placement_references",Json::array({angle})}});
    near(*live.open_assembly(id)->session.document().measure_placement_reference(current(second).placement_references.front()),30);
    angle["flip"]=true;set(second,{{"placement_references",Json::array({angle})}});
    near(*live.open_assembly(id)->session.document().measure_placement_reference(current(second).placement_references.front()),30);
    auto axis=row("axis_coincident","origin:axis:z");axis["lower_limit"]=-5;axis["upper_limit"]=5;set(second,{{"placement_references",Json::array({axis})}});
    near(current(second).placement.x,10);near(current(second).placement.y,11);
    require(current(second).placement_references.front().offset_locked,"New coincidence must retain the same zero lock as GUI");
    require(live.open_assembly(id)->session.document().remaining_degrees_of_freedom(second)==2,"Axis mate freedoms are wrong");
    set(second,{{"placement",{{"z_mm",40}}}});near(current(second).placement.z,40);
    auto point=row("point_coincident","origin:point");point["lower_limit"]=-2;point["upper_limit"]=0;point["flip"]=true;set(second,{{"placement_references",Json::array({point})}});
    near(current(second).placement.x,10);near(current(second).placement.y,11);near(current(second).placement.z,12);
    rejected({{"placement",{{"x_mm",11}}}},"constrained_coordinate");
    set(second,{{"placement_references",Json::array()},{"placement",{{"rotation_x_deg",0},{"rotation_y_deg",0},{"rotation_z_deg",0}}}});
    Json faces={{"kind","plane_coincident"},{"component",ref(second,box,"z_min")},{"target",ref(first,box,"z_max")},{"offset",3},{"flip",true}};
    set(second,{{"placement_references",Json::array({faces})}});near(current(second).placement.z,45);
    const auto rows=get(second).at("placement_references");set(second,{{"placement_references",rows}});
    require(current(second).placement_references.front().component_reference.owner_id==box,"Original geometry was replaced with result topology");
    auto broken=faces;broken["component"]["key"]="missing";rejected({{"placement_references",Json::array({broken})}},"missing_reference");
    broken=faces;broken["target"]["instance_path"]=path(second);rejected({{"placement_references",Json::array({broken})}},"invalid_reference");
    broken=faces;broken["component"]["instance_path"]=path(first);rejected({{"placement_references",Json::array({broken})}},"invalid_reference");
    broken=faces;broken["target"]["kind"]="axis";rejected({{"placement_references",Json::array({broken})}},"invalid_reference");
    broken=axis;broken["offset"]=1;rejected({{"placement_references",Json::array({broken})}},"invalid_arguments");
    broken=faces;broken["flip"]="true";rejected({{"placement_references",Json::array({broken})}},"invalid_arguments");
    rejected({{"placement_references",Json::array({faces,faces,faces,faces})}},"invalid_arguments");
    rejected({{"placement_references",Json::array({faces,row("plane_coincident","origin:plane:xy",0)})}});
    for(const auto& values:{Json{{"x_mm",1000001}},Json{{"rotation_x_deg",181}},Json{{"x_mm","1"}},Json{{"unknown",0}},Json::object()})rejected({{"placement",values}});
    rejected({{"name","   "}});rejected({{"visible","yes"}});rejected(Json::object());
    rejected({{"instance_path",path(first)+path(second)},{"visible",false}},"unsupported_context");
    editing=true;rejected({{"visible",false}},"editing_in_progress");editing=false;
    set(second,{{"grounded",true}});rejected({{"placement",{{"x_mm",1}}}},"constrained_coordinate");set(second,{{"grounded",false}});
    // Proposed target cycle must be rejected even if one component is grounded.
    auto cycle=faces;cycle["component"]=ref(first,box,"z_min");cycle["target"]=ref(second,box,"z_max");
    rejected({{"instance_path",path(first)},{"placement_references",Json::array({cycle})}});
    const auto edit=workspace::prepare_component_edit(live,id,second);set(second,{{"name","Updated"}});
    bool stale=false;try{static_cast<void>(workspace::commit_component_properties(live,edit,edit.initial));}catch(const workspace::ComponentOperationError& e){stale=std::string(e.code)=="document_changed";}require(stale,"Stale Properties accepted");
    run(host,"save");const auto native=assembly::AssemblyDocument::load(dir/"properties.asmz");
    require(workspace::component_properties(*native.find_occurrence(second))==workspace::component_properties(current(second)),"Native file lost component properties");
    near(native.find_occurrence(second)->calculated_source->volume,6000);
    require(live.open_part(source)->session.revision()==source_revision,"Component properties edited the source Part");
    const auto mirror=run(host,"mirror.create",{{"source",second},{"local_plane","yz"}}).data.at("object").get<std::string>();
    set(mirror,{{"name","Own mirror"},{"visible",false}});
    rejected({{"instance_path",path(mirror)},{"grounded",false}},"read_only_copy");
    rejected({{"instance_path",path(mirror)},{"placement",{{"x_mm",5}}}},"read_only_copy");
    // A physical-relation failure must not even advance the data generation.
    run(host,"document.relations.set",{{"relations",Json::array({{{"target","capacity"},{"expression","1 / (6000 - round(model.volume))"}}})}});
    rejected({{"suppressed",true}});
    run(host,"document.relations.set",{{"relations",Json::array()}});
    const auto prepared=workspace::prepare_component_edit(live,id,second);
    const auto old_mirror=current(mirror).calculated_source;
    run(host,"activate",{{"document",source}});run(host,"box.set",{{"container",box},{"length_mm","20"}});
    live.refresh_source_geometry();run(host,"activate",{{"document",id}});
    const auto fresh=current(second).calculated_source;near(fresh->volume,12000);
    auto renamed=prepared.initial;renamed.name="Current source retained";
    require(workspace::commit_component_properties(live,prepared,renamed),"Prepared name edit did not commit");
    require(current(second).calculated_source.shares_with(fresh)&&current(mirror).calculated_source.shares_with(old_mirror),
        "Prepared properties overwrote updated source geometry or regenerated a derived result");
    require(live.open_part(source)->session.is_dirty()&&document::PartDocument::load(dir/"source.prtz").find_container(box)->box.length==10,
        "Properties saved the authoritative source Part");
    // Followers precede their targets in component storage. Changing C must
    // settle B and then A without relying on tree/insertion order.
    run(host,"new",{{"type","assembly"},{"name","mate-chain"}});const auto chain_id=live.active_document_id();
    std::array<std::string,3> chain;
    for(auto& occurrence:chain)occurrence=run(host,"component.insert",{{"source",source}}).data.at("occurrence").get<std::string>();
    const auto chain_row=[&](std::size_t moving,std::size_t target,double distance){return Json{{"kind","plane_coincident"},
        {"component",ref(chain[moving],source+":origin","origin:plane:xy")},
        {"target",ref(chain[target],source+":origin","origin:plane:xy")},{"offset",distance}};};
    const auto chain_z=[&](std::size_t index){return live.open_assembly(chain_id)->session.document().find_occurrence(chain[index])->placement.z;};
    set(chain[0],{{"grounded",false},{"placement_references",Json::array({chain_row(0,1,2)})}});
    set(chain[1],{{"placement_references",Json::array({chain_row(1,2,3)})}});
    near(chain_z(1),3);near(chain_z(0),5);
    set(chain[2],{{"placement",{{"z_mm",10}}}});
    near(chain_z(2),10);near(chain_z(1),13);near(chain_z(0),15);
    run(host,"undo");near(chain_z(2),0);near(chain_z(1),3);near(chain_z(0),5);
    run(host,"redo");near(chain_z(0),15);
    run(host,"regenerate");near(chain_z(0),15);near(chain_z(1),13);near(chain_z(2),10);
    run(host,"save");
    const auto settled=assembly::AssemblyDocument::load(dir/"mate-chain.asmz");
    std::array<std::size_t,3> order{0,1,2};
    do {
        auto candidate=settled;
        for(std::size_t i=0;i<3;++i)candidate.components[i]=settled.components[order[i]];
        candidate.find_occurrence(chain[0])->placement.z=-40;
        candidate.find_occurrence(chain[1])->placement.z=70;
        // Several valid rows may depend on the same target occurrence.
        auto& rows=candidate.find_occurrence(chain[0])->placement_references;
        rows.push_back(rows.front());
        candidate.calculate_placement_references();
        near(candidate.find_occurrence(chain[0])->placement.z,15);
        near(candidate.find_occurrence(chain[1])->placement.z,13);
        near(candidate.find_occurrence(chain[2])->placement.z,10);
        for(std::size_t i=0;i<3;++i)require(candidate.components[i].occurrence_id==chain[order[i]]&&
            candidate.components[i].calculated_source.shares_with(settled.components[order[i]].calculated_source),
            "Mate solve reordered the tree or replaced source geometry");
        candidate.calculate_placement_references();near(candidate.find_occurrence(chain[0])->placement.z,15);
    }while(std::next_permutation(order.begin(),order.end()));
    auto cyclic=settled;
    auto back_edge=cyclic.find_occurrence(chain[0])->placement_references.front();
    back_edge.component_reference.instance_path=assembly::InstancePath{}.child(chain[2]);
    back_edge.target_reference.instance_path=assembly::InstancePath{}.child(chain[0]);
    back_edge.offset=0;
    cyclic.find_occurrence(chain[2])->placement_references={back_edge};
    const auto before_cycle=cyclic.components;
    bool cycle_failed=false;
    try{cyclic.calculate_placement_references();}catch(const std::runtime_error& e){
        cycle_failed=std::string(e.what()).find("cycle")!=std::string::npos;
    }
    require(cycle_failed,"Native mate evaluation accepted a cycle");
    for(std::size_t i=0;i<3;++i)require(cyclic.components[i].placement==before_cycle[i].placement&&
        cyclic.components[i].calculated_source.shares_with(before_cycle[i].calculated_source),
        "Rejected cyclic solve published a partial placement");
    // Native evaluation retains its existing grounded-component contract.
    cyclic.find_occurrence(chain[2])->grounded=true;
    cyclic.calculate_placement_references();near(cyclic.find_occurrence(chain[0])->placement.z,15);


}
}
int main(){try{const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-component-properties-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create test directory");kernel::OcctKernel kernel;verify(kernel,dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);
    std::cout<<"Component properties: original references, geometry, freedoms, locks, modes, native files, atomic failures and Undo passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
