#include <zima/command_host/host.hpp>
#include <zima/workspace/sweep_operations.hpp>
#include "sweep_test_support.hpp"
#include <cmath>
#include <numbers>
#include <iostream>
using namespace zima; using commands::Json; namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){if(!std::isfinite(actual)||std::abs(actual-expected)>1e-6)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
Json run(command_host::Host& host,const char* name,Json args=Json::object()){
    const auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);
    return result.data;
}
void verify(const kernel::OcctKernel& kernel,fs::path directory,bool body_mode){
    workspace::Workspace live;auto doc=document::PartDocument::create_default();const auto id=doc.document_id;
    if(body_mode){
        document::BodyHistoryGraph graph;const auto key=graph.create_body("Rotated Body");
        auto body=*graph.find(key);body.scope.placement.x=100;body.scope.placement.rotation_z=body.scope.placement.absolute_rotation_z=90;
        graph.update_body(std::move(body));doc.set_body_history(std::move(graph));
    }
    const auto file=directory/(body_mode?"body.prtz":"part.prtz");
    live.add_part(std::move(doc),{},file);live.activate(id);
    command_host::Interaction interaction;command_host::Options options;options.interaction=[&]{return interaction;};command_host::Host host(live,kernel,directory,options);
    const auto source=run(host,"construction.create",{{"kind","point"},{"name","Earlier source"},{"values",{{"x",12},{"y",7},{"z",42}}}}).at("construction").get<std::string>();
    auto feature=test_support::sweep_fixture(document::FeatureKind::Sweep3D);const auto owner=feature.id;
    feature.placement.x=20;feature.placement.y=-4;feature.placement.z=2;
    feature.placement.rotation_z=feature.placement.absolute_rotation_z=90;
    workspace::commit_sweep(live,kernel,id,std::move(feature),workspace::SweepEditMode::Create);
    const auto state=[&]() -> workspace::PartState& {return *live.open_part(id);};
    const auto current=[&]{return *state().session.document().find_container(owner);};
    const auto initial=current();const auto a=initial.sweep3d.path.curve_points[0].id,b=initial.sweep3d.path.curve_points[1].id;
    const auto point=[&](int i){return current().sweep3d.path.curve_points[i];};
    const auto request=[&](const std::string& target,int index,const std::string& ref,const std::string& key,double offset=0){
        return Json{{"construction",target},{"index",index},{"reference",{{"owner",ref},{"key",key}}},{"offset_mm",offset},{"derive_orientation",false}};
    };
    const auto set=[&](Json args){return run(host,"construction.reference.set",std::move(args));};
    const auto reject=[&](Json args,const char* code){
        const auto before=current();const auto revision=state().session.revision();const auto cache=state().session.calculated_boundaries().back().kernel_shape;
        const auto result=host.execute({{"command","construction.reference.set"},{"arguments",std::move(args)}});
        if(result.ok||(!std::string(code).empty()&&result.code!=code))throw std::runtime_error(std::string("Expected rejection ")+code+", got "+result.code+": "+result.message);
        require(current()==before&&state().session.revision()==revision&&!host.change()&&state().session.calculated_boundaries().back().kernel_shape==cache,"Rejected point changed the Sweep, history or body");
    };
    const auto query=run(host,"construction.get",{{"construction",b}});
    require(query.at("owning_feature")==owner&&query.at("coordinate_owner")==initial.sweep3d.path.id,"Owned point query has wrong ownership");
    const auto first=request(b,0,initial.sweep3d.path.container_origin.id,"origin:plane:yz",5);
    const auto result=set(first);
    require(result.at("body_calculated")==true&&result.at("changed")==true,"Owned point did not calculate through the Sweep transaction");
    near(point(1).origin.x,5);near(point(1).origin.z,20);
    near(state().session.calculated_boundaries().back().volume,4*std::numbers::pi*std::sqrt(425.0));
    const auto bound=current();run(host,"undo");require(current()==initial,"Sweep point Undo changed identity");run(host,"redo");require(current()==bound,"Sweep point Redo differs");
    const auto revision=state().session.revision();require(set(first).at("changed")==false&&state().session.revision()==revision&&!host.change(),"Owned point no-op added history");
    set(request(a,0,initial.sweep3d.path.container_origin.id,"origin:plane:yz",2));
    set(request(b,0,point(0).container_origin.id,"origin:plane:yz",3));
    near(point(0).origin.x,2);near(point(1).origin.x,5);
    auto entries=Json::array({Json{{"construction",a},{"values",{{"reference_offset:0",4}}}},Json{{"construction",b}}});
    run(host,"sweep3d.set",{{"container",owner},{"path",{{"points",entries}}}});
    near(point(0).origin.x,4);near(point(1).origin.x,7);
    near(state().session.calculated_boundaries().back().volume,4*std::numbers::pi*std::sqrt(409.0));
    const auto before_constrained=current();const auto constrained_revision=state().session.revision();
    const auto constrained=host.execute({{"command","sweep3d.set"},{"arguments",{{"container",owner},{"path",{{"points",Json::array({
        Json{{"construction",a}},Json{{"construction",b},{"values",{{"x",99}}}}})}}}}}});
    require(!constrained.ok&&constrained.code=="parameter_not_editable"&&current()==before_constrained&&
        state().session.revision()==constrained_revision&&!host.change(),"Owned point coordinate bypassed its earlier Point plane constraint");
    reject(request(a,0,point(0).container_origin.id,"point"),"reference_not_available");
    reject(request(a,0,point(1).container_origin.id,"origin:axis:y"),"reference_not_available");
    reject(request(a,0,initial.sweep3d.path.entity_id,"curve:segment:"+a+":"+b),"reference_not_available");
    reject(request(a,0,owner,"face:invalid"),"reference_not_available");
    reject(request(b,0,point(0).container_origin.id,"point"),"");
    const auto later=run(host,"construction.create",{{"kind","point"},{"name","Later source"}}).at("construction").get<std::string>();
    reject(request(a,0,state().session.document().find_construction(later)->container_origin.id,"point"),"reference_not_available");
    reject(request(a,5,id+":origin","origin:plane:xy"),"invalid_arguments");
    auto occurrence=request(a,0,id+":origin","origin:plane:xy");occurrence["reference"]["instance_path"]="not-local";
    reject(occurrence,"invalid_arguments");
    interaction.editing=true;reject(first,"editing_in_progress");interaction.editing=false;
    const auto external=state().session.document().find_construction(source)->container_origin.id;
    set(request(b,0,external,"point"));near(point(1).origin.x,11);near(point(1).origin.y,8);near(point(1).origin.z,40);
    near(state().session.calculated_boundaries().back().volume,4*std::numbers::pi*std::sqrt(1713.0));
    require(current().placement==initial.placement&&current().sweep3d.path.id==initial.sweep3d.path.id&&
        current().sweep3d.profiles[0].id==initial.sweep3d.profiles[0].id,"Point edit changed parent placement or profile identity");
    // Explicit FRONT/TOP references use the same independent orientation rows.
    const auto before_orientation=current();
    set(request(b,3,initial.sweep3d.path.container_origin.id,"origin:plane:xz"));
    require(std::ranges::any_of(point(1).references,[](const auto& ref){return ref.orientation_only&&ref.orientation_role=="front";}),
        "Owned Point lost the explicit FRONT row");
    run(host,"undo");require(current()==before_orientation,"Orientation Undo did not restore the full Sweep");
    // Preserve a locked distance when replacing its source using the shared input contract.
    const auto before_lock_cache=state().session.calculated_boundaries().back().kernel_shape;
    run(host,"value_lock.set",{{"object",a},{"key","reference_offset:0"},{"locked",true}});
    require(state().session.calculated_boundaries().back().kernel_shape==before_lock_cache,"Locking a Point recalculated the body");
    set(request(a,0,initial.sweep3d.path.container_origin.id,"origin:plane:yz",99));
    near(point(0).references[0].offset,4);near(point(0).origin.x,4);
    require(point(0).references[0].offset_locked,"Reference replacement dropped the distance lock");
    const auto before_lock=current();const auto lock_revision=state().session.revision();
    const auto lock_edit=host.execute({{"command","sweep3d.set"},{"arguments",{{"container",owner},{"path",{{"points",Json::array({
        Json{{"construction",a},{"values",{{"reference_offset:0",6}}}},Json{{"construction",b}}})}}}}}});
    require(!lock_edit.ok&&lock_edit.code=="parameter_not_editable"&&current()==before_lock&&state().session.revision()==lock_revision,
        "Sweep point list bypassed a reference distance lock");
    if(body_mode){
        const auto saved_doc=state().session.document();auto inactive=saved_doc;auto graph=inactive.body_history;
        static_cast<void>(graph.create_body("Other Body"));inactive.set_body_history(std::move(graph));
        state().session.commit(std::move(inactive),state().session.calculated_boundaries());
        reject(first,"inactive_body");run(host,"undo");
        require(state().session.document().body_history==saved_doc.body_history,"Inactive Body test Undo changed ownership");
    }
    run(host,"save");std::vector<kernel::BodyResult> cache;const auto loaded=document::PartDocument::load(file,&cache);
    require(*loaded.find_container(owner)==current()&&!cache.empty(),"Native file lost owned point references or cached body");
    near(cache.back().volume,state().session.calculated_boundaries().back().volume);
    std::cout<<"Owned Sweep Point: Body="<<body_mode<<" passed\n";
}
}
int main(){try{
    const auto root=fs::canonical(fs::temp_directory_path()),directory=root/("zima-sweep-point-refs-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create test directory");kernel::OcctKernel kernel;
    verify(kernel,directory,false);verify(kernel,directory,true);
    require(fs::canonical(directory).parent_path()==root,"Unsafe cleanup");fs::remove_all(directory);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
