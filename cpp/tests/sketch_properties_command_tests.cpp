#include <zima/command_host/host.hpp>
#include <zima/workspace/sketch_properties.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <iostream>
#include <numbers>
#include <cmath>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){if(!std::isfinite(actual)||std::abs(actual-expected)>1e-6)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
Json run(command_host::Host& host,const char* name,Json args=Json::object()){
    const auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);
    return result.data;
}
void verify(kernel::OcctKernel& kernel,fs::path directory,bool rotated){
    workspace::Workspace live;auto doc=document::PartDocument::create_default();const auto id=doc.document_id;
    if(rotated){document::BodyHistoryGraph graph;const auto body_id=graph.create_body("Rotated Body");auto body=*graph.find(body_id);
        body.scope.placement.x=100;body.scope.placement.rotation_z=body.scope.placement.absolute_rotation_z=90;
        graph.update_body(std::move(body));doc.set_body_history(std::move(graph));}
    const auto file=directory/(rotated?"rotated.prtz":"plain.prtz");live.add_part(std::move(doc),{},file);live.activate(id);
    command_host::Interaction interaction;command_host::Options options;options.interaction=[&]{return interaction;};
    command_host::Host host(live,kernel,directory,options);
    const auto result=run(host,"sketch.create",{{"name","Original"}});const std::string sketch_id=result.at("sketch"),owner=result.at("owner");
    auto* state=live.open_part(id);
    const auto current=[&]{return workspace::document_sketch(live,id,sketch_id);};
    const auto feature=[&]{return *state->session.document().find_container(owner);};
    const auto circle=run(host,"sketch.circle.create",{{"sketch",sketch_id},{"center",{0,0}},{"radius_mm",3}}).at("geometry").get<std::string>();
    const auto original=current().serialized();const auto initial=feature();
    auto args=Json{{"sketch",sketch_id},{"name","Renamed"},{"plane","YZ"},{"plane_offset_mm",4},{"placement",{{"x",10},{"y",20},{"z",30}}}};
    require(run(host,"sketch.set",args).at("changed")==true,"Properties edit did not commit");
    require(current().name=="Renamed"&&current().circles.front().id==circle&&current().owner_container_id==owner,"Properties changed geometry identity");
    near(current().plane_offset,4);near(feature().placement.x,10);
    const auto saved=current().serialized();const auto saved_feature=feature();
    run(host,"undo");require(current().serialized()==original&&feature()==initial,"Properties Undo was not atomic");
    run(host,"redo");require(current().serialized()==saved&&feature()==saved_feature,"Properties Redo differs");
    const auto revision=state->session.revision();
    require(run(host,"sketch.set",args).at("changed")==false&&state->session.revision()==revision,"No-op Properties added history");
    const auto reject=[&](const char* command,Json bad,const char* code){
        const auto s=current().serialized();const auto f=feature();const auto rev=state->session.revision();
        const auto rejected=host.execute({{"command",command},{"arguments",std::move(bad)}});
        if(rejected.ok||rejected.code!=code)throw std::runtime_error(std::string("Expected ")+code+", got "+rejected.code+": "+rejected.message);
        require(current().serialized()==s&&feature()==f&&state->session.revision()==rev&&!host.change(),"Rejected edit changed document");
    };
    reject("sketch.set",{{"sketch",sketch_id},{"plane","XX"}},"invalid_arguments");
    reject("sketch.set",{{"sketch",sketch_id},{"plane_offset_mm",1000001}},"invalid_arguments");
    run(host,"value_lock.set",{{"object",owner},{"key","profile_offset"},{"locked",true}});
    reject("sketch.set",{{"sketch",sketch_id},{"plane_offset_mm",5}},"parameter_not_editable");
    run(host,"value_lock.set",{{"object",owner},{"key","profile_offset"},{"locked",false}});
    interaction.editing=true;reject("sketch.set",{{"sketch",sketch_id},{"name","Blocked"}},"editing_in_progress");interaction.editing=false;
    // Check an independent solid measure after converting the positioned Sketch.
    auto profile=workspace::profile_from_sketch(state->session.document(),sketch_id,document::FeatureKind::Extrusion);
    profile.extrusion.length_forward=5;
    workspace::commit_profile(live,kernel,id,std::move(profile),workspace::ProfileEditMode::TransformSketch);
    near(state->session.calculated_boundaries().back().volume,45*std::numbers::pi);
    const auto extent=[&](double low,double high){
        const auto& vertices=state->session.calculated_boundaries().back().mesh.vertices;
        require(!vertices.empty(),"Extrusion has no visible vertices");
        double minimum=1e9,maximum=-1e9;
        for(const auto& point:vertices){const auto coordinate=rotated?point.y:point.x;minimum=std::min(minimum,coordinate);maximum=std::max(maximum,coordinate);}
        near(minimum,low);near(maximum,high);
    };
    extent(14,19);
    run(host,"sketch.set",{{"sketch",sketch_id},{"plane_offset_mm",8}});
    near(current().plane_offset,8);near(feature().extrusion.profile_plane_offset,8);
    near(state->session.calculated_boundaries().back().volume,45*std::numbers::pi);
    extent(18,23);
    run(host,"undo");run(host,"undo");require(feature().feature_kind==document::FeatureKind::Sketch,"Conversion Undo lost Sketch container");
    const auto* body=state->session.document().body_owner_for_object(owner);
    const auto ref_owner=body?body->origin().id:id+":origin";
    const auto reference=[&](int index,const std::string& source,const char* key){return Json{{"sketch",sketch_id},{"index",index},{"reference",{{"owner",source},{"key",key}}}};};
    run(host,"sketch.reference.set",reference(0,ref_owner,"origin:plane:xy"));
    require(current().plane==sketcher::SketchPlane::XZ&&feature().placement.references.front().orientation_role=="front","First plane did not define Sketch frame");
    run(host,"placement.reference.set",{{"object",owner},{"index",1},{"reference",{{"owner",ref_owner},{"key","origin:plane:yz"}}}});
    run(host,"sketch.reference.set",reference(2,ref_owner,"origin:plane:xz"));
    near(feature().placement.x,0);near(feature().placement.y,0);near(feature().placement.z,0);
    reject("sketch.set",{{"sketch",sketch_id},{"placement",{{"x",2}}}},"parameter_not_editable");
    reject("sketch.reference.set",reference(0,feature().container_origin.id,"origin:plane:xy"),"reference_not_available");
    const auto later=run(host,"construction.create",{{"kind","point"},{"name","Later"}}).at("construction").get<std::string>();
    reject("sketch.reference.set",reference(0,state->session.document().find_construction(later)->container_origin.id,"point"),"reference_not_available");
    run(host,"placement.reference.remove",{{"object",owner},{"index",0}});
    require(feature().placement.reference_valid,"Removing work plane invalidated remaining references");
    run(host,"save");std::vector<kernel::BodyResult> cache;const auto loaded=document::PartDocument::load(file,&cache);
    require(*loaded.find_container(owner)==feature()&&std::ranges::find(loaded.sketches,sketch_id,&sketcher::Sketch::id)->serialized()==current().serialized(),"Native Sketch Properties differ");
}
}
int main(){try{
    const auto root=fs::canonical(fs::temp_directory_path()),directory=root/("zima-sketch-properties-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create test directory");kernel::OcctKernel kernel;
    verify(kernel,directory,false);verify(kernel,directory,true);
    require(fs::canonical(directory).parent_path()==root,"Unsafe cleanup");fs::remove_all(directory);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
