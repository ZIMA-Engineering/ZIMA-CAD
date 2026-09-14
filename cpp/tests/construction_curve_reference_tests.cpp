#include <zima/command_host/host.hpp>
#include <cmath>
#include <numbers>
#include "sweep_test_support.hpp"
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){if(!std::isfinite(actual)||std::abs(actual-expected)>1e-7)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
void verify(const kernel::OcctKernel& kernel,fs::path directory,bool assembly_mode) {
    workspace::Workspace live;std::string id;const auto file=directory/(assembly_mode?"curve-refs.asmz":"curve-refs.prtz");
    if(assembly_mode){auto doc=assembly::AssemblyDocument::create_default();id=doc.document_id;live.add_assembly(std::move(doc),file);}
    else{auto doc=document::PartDocument::create_default();id=doc.document_id;live.add_part(std::move(doc),{},file);}
    live.activate(id);command_host::Host host(live,kernel,directory);
    const auto run=[&](const char* command,Json args=Json::object()){const auto result=host.execute({{"command",command},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.code+": "+result.message);return result.data;};
    const auto all=[&]{return assembly_mode?live.open_assembly(id)->session.document().constructions:live.open_part(id)->session.document().constructions;};
    const auto object=[&](const std::string& key){return assembly_mode?*live.open_assembly(id)->session.document().find_construction(key):*live.open_part(id)->session.document().find_construction(key);};
    const auto revision=[&]{return assembly_mode?live.open_assembly(id)->session.revision():live.open_part(id)->session.revision();};
    const auto source=run("construction.create",{{"kind","point"},{"name","Source"},{"values",{{"x",12},{"y",7},{"z",3}}}}).at("construction").get<std::string>();
    const auto plane=run("construction.create",{{"kind","plane"},{"name","Plane"},{"base_plane","xy"},{"values",{{"z",5}}}}).at("construction").get<std::string>();
    const auto point=[](double x,double y){return Json{{"values",{{"x",x},{"y",y}}}};};
    const auto create_curve=[&](const char* name){return run("construction.create",{{"kind","curve3d"},{"name",name},{"values",{{"x",20},{"y",-4},{"z",2},{"rotation_z",90}}},
        {"points",Json::array({point(0,0),point(10,0),point(10,10),point(0,10)})}}).at("construction").get<std::string>();};
    const auto curve=create_curve("Local curve");const auto original=object(curve);const auto a=original.curve_points[0].id,b=original.curve_points[1].id,c=original.curve_points[2].id,d=original.curve_points[3].id;
    const auto request=[&](const std::string& target,int index,const std::string& owner,const std::string& key,double offset=0){return Json{{"construction",target},{"index",index},{"reference",{{"owner",owner},{"key",key}}},{"offset_mm",offset},{"derive_orientation",false}};};
    const auto set=[&](Json args){return run("construction.reference.set",std::move(args));};
    const auto reject=[&](Json args,const char* code){const auto before=all();const auto rev=revision();const auto result=host.execute({{"command","construction.reference.set"},{"arguments",std::move(args)}});
        if(result.ok||result.code!=code)throw std::runtime_error(std::string("Expected ")+code+", got "+result.code+": "+result.message);
        require(all()==before&&revision()==rev&&!host.change(),"Rejected child reference changed the document");};
    const auto req=request(b,0,object(source).container_origin.id,"point");const auto before=all();const auto assigned=set(req);
    require(assigned.at("body_calculated")==false&&assigned.at("coordinate_owner")==curve,"Child reference has wrong calculation/frame contract");
    near(object(b).origin.x,11);near(object(b).origin.y,8);near(object(b).origin.z,1);
    require(object(curve).origin==original.origin&&object(curve).rotation==original.rotation&&object(a)==original.curve_points[0]&&object(c)==original.curve_points[2],"Assigning one child moved parent or siblings");
    const auto after=all();run("undo");require(all()==before,"Child reference Undo was not atomic");run("redo");require(all()==after,"Child reference Redo changed identity");
    const auto rev=revision();require(set(req).at("changed")==false&&revision()==rev&&!host.change(),"Child reference no-op added history");
    set(request(a,0,original.container_origin.id,"origin:plane:yz",2));near(object(a).origin.x,2);near(object(a).origin.y,0);
    // The fourth point may use an earlier point. Its preceding neighbour remains distinct.
    set(request(d,0,object(a).container_origin.id,"point"));near(object(d).origin.x,object(a).origin.x);near(object(d).origin.y,object(a).origin.y);near(object(d).origin.z,object(a).origin.z);
    const auto before_chain=all();
    run("placement.set",{{"object",a},{"values",{{"reference_offset:0",4}}}});near(object(a).origin.x,4);near(object(d).origin.x,4);
    const auto after_chain=all();run("undo");require(all()==before_chain,"Undo lost the previous resolved reference chain");
    run("redo");require(all()==after_chain,"Redo lost the newly resolved reference chain");
    reject(request(b,0,object(b).container_origin.id,"point"),"reference_not_available");
    reject(request(a,0,object(c).container_origin.id,"point"),"reference_not_available");
    reject(request(a,0,original.entity_id,"curve:segment:"+a+":"+b),"reference_not_available");
    const auto later=run("construction.create",{{"kind","point"},{"name","Later"}}).at("construction").get<std::string>();
    reject(request(a,0,object(later).container_origin.id,"point"),"reference_not_available");
    // A valid source which collapses adjacent points must fail the final route check atomically.
    reject(request(c,0,object(b).container_origin.id,"point"),"construction_rejected");
    run("construction.set",{{"construction",source},{"values",{{"x",13}}}});near(object(b).origin.x,11);near(object(b).origin.y,7);
    run("construction.set",{{"construction",curve},{"values",{{"rotation_z",0}}}});near(object(b).origin.x,-7);near(object(b).origin.y,11);near(object(b).origin.z,1);
    near(object(a).origin.x,4);near(object(d).origin.x,object(a).origin.x);near(object(d).origin.y,object(a).origin.y);near(object(d).origin.z,object(a).origin.z);
    const auto planar=create_curve("Plane offsets");const auto middle=object(planar).curve_points[1].id;
    set(request(middle,0,object(plane).entity_id,"plane",2));near(object(middle).origin.z,5);near(object(middle).origin.x,10);
    set(request(middle,1,object(planar).container_origin.id,"origin:plane:yz",3));near(object(middle).origin.x,3);near(object(middle).origin.z,5);
    run("placement.set",{{"object",middle},{"values",{{"reference_offset:0",4}}}});near(object(middle).origin.z,7);
    const auto framed=create_curve("Earlier child frames");const auto frames=object(framed).curve_points;
    set(request(frames[0].id,0,object(framed).container_origin.id,"origin:plane:yz",2));
    set(request(frames[1].id,0,frames[0].container_origin.id,"origin:plane:yz",3));
    set(request(frames[2].id,0,frames[1].container_origin.id,"origin:plane:yz",4));
    set(request(frames[3].id,0,frames[0].container_origin.id,"origin:axis:y"));
    near(object(frames[1].id).origin.x,5);near(object(frames[2].id).origin.x,9);near(object(frames[3].id).origin.x,2);
    run("placement.set",{{"object",frames[0].id},{"values",{{"reference_offset:0",4}}}});
    near(object(frames[1].id).origin.x,7);near(object(frames[2].id).origin.x,11);near(object(frames[3].id).origin.x,4);
    auto entries=Json::array();for(const auto& child:object(framed).curve_points)entries.push_back({{"construction",child.id}});
    entries[1]["values"]={{"x",99}};const auto before_batch=all();const auto batch_revision=revision();
    const auto batch=host.execute({{"command","construction.set"},{"arguments",{{"construction",framed},{"points",entries}}}});
    require(!batch.ok&&batch.code=="parameter_not_editable"&&all()==before_batch&&revision()==batch_revision&&!host.change(),
        "Curve point-list edit ignored an earlier child's plane");
    reject(request(frames[0].id,0,frames[0].container_origin.id,"origin:plane:xy"),"reference_not_available");
    reject(request(frames[0].id,0,frames[2].container_origin.id,"origin:axis:y"),"reference_not_available");
    run("save");const auto loaded=assembly_mode?assembly::AssemblyDocument::load(file).constructions:document::PartDocument::load(file).constructions;
    for(const auto& expected:all()){const auto found=std::ranges::find(loaded,expected.id,&document::ConstructionObject::id);require(found!=loaded.end()&&*found==expected,"Native save lost child references, identities or local coordinates");}
}
document::ConstructionReference datum(const std::string& owner,const char* key,double offset=0) {
    document::ConstructionReference result;
    result.owner_id=owner;result.semantic_key=key;result.offset=offset;
    result.supports_offset=result.semantic_key.starts_with("origin:plane:");
    return result;
}
void verify_frames(bool body_mode) {
    std::cout << "Checking child datum frames, Body=" << body_mode << std::endl;
    auto part=document::PartDocument::create_default();
    auto curve=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);
    curve.curve_points.clear();
    for(int index=0;index<4;++index) {
        auto point=document::PartDocument::create_construction(document::ConstructionKind::Point);
        point.parent_construction_id=curve.id;point.origin={0.0,10.0*index,10.0*index};
        point.definition=document::ConstructionDefinition::PointReference;
        curve.curve_points.push_back(std::move(point));
    }
    curve.curve_points[0].references={datum(curve.container_origin.id,"origin:plane:yz",2)};
    curve.curve_points[1].references={datum(curve.curve_points[0].container_origin.id,"origin:plane:yz",3)};
    curve.curve_points[2].references={datum(curve.curve_points[1].container_origin.id,"origin:plane:yz",4)};
    curve.curve_points[3].references={datum(curve.curve_points[0].container_origin.id,"origin:axis:y")};
    const auto curve_id=curve.id;
    part.constructions.push_back(std::move(curve));
    if(body_mode) {
        document::BodyHistoryGraph graph;const auto id=graph.create_body("Rotated frame");
        auto body=*graph.find(id);body.scope.placement.x=100;
        body.scope.placement.rotation_z=body.scope.placement.absolute_rotation_z=90;
        graph.update_body(std::move(body));
        graph.insert({document::PartHistoryKind::Construction,curve_id});
        part.set_body_history(std::move(graph));
    }
    const auto root=[&]() -> document::ConstructionObject& {return *part.find_construction(curve_id);};
    const auto point=[&](std::size_t i) -> document::ConstructionObject& {return root().curve_points[i];};
    part.resolve_constructions();
    require(root().reference_valid,"A previous point's plane or axis was unavailable");
    near(point(0).origin.x,2);near(point(1).origin.x,5);near(point(2).origin.x,9);near(point(3).origin.x,2);
    near(point(3).origin.y,30);near(point(3).origin.z,0);
    point(0).references[0].offset=4;part.resolve_constructions();
    near(point(0).origin.x,4);near(point(1).origin.x,7);near(point(2).origin.x,11);near(point(3).origin.x,4);
    root().origin={20,-4,2};root().absolute_rotation={0,0,90};part.resolve_constructions();
    near(point(0).origin.x,4);near(point(1).origin.x,7);near(point(2).origin.x,11);near(point(3).origin.x,4);
    if(!body_mode) {
        const auto mesh=part.construction_viewer_mesh(curve_id);
        const auto hit=std::ranges::find_if(mesh.original_references.points,[&](const auto& item){
            return item.reference.owner_id==point(1).container_origin.id&&item.reference.semantic_key=="point";
        });
        require(hit!=mesh.original_references.points.end(),"Resolved child has no persisted point identity");
        near(hit->position.x,10);near(hit->position.y,3);near(hit->position.z,12);
    }
    point(0).absolute_rotation={0,0,90};part.resolve_constructions();
    require(root().reference_valid,"Rotating a previous child's datum frame broke a valid chain");
    near(point(1).origin.y,3);near(point(2).origin.x,point(1).origin.x+4);
    near(point(3).origin.y,point(0).origin.y);near(point(3).origin.z,point(0).origin.z);
    const auto last=point(0).origin;
    point(0).references={datum(point(2).container_origin.id,"point")};
    part.resolve_constructions();
    require(!root().reference_valid&&!point(0).reference_valid&&point(0).origin==last,
        "A forward reference consumed an old child frame or lost the last valid position");
    point(0).references={datum("missing-source","point")};part.resolve_constructions();
    require(!root().reference_valid&&!point(0).reference_valid&&point(0).origin==last,
        "A missing source lost the last valid child position");
}
void verify_sweep(const kernel::OcctKernel& kernel,const fs::path& directory) {
    std::cout << "Checking owned Sweep datum frames and volume" << std::endl;
    auto part=document::PartDocument::create_default();
    auto sweep=test_support::sweep_fixture(document::FeatureKind::Sweep3D);
    auto& path=sweep.sweep3d.path;
    while(path.curve_points.size()<4) {
        auto point=document::PartDocument::create_construction(document::ConstructionKind::Point);
        point.parent_construction_id=path.id;path.curve_points.push_back(std::move(point));
    }
    for(std::size_t index=0;index<path.curve_points.size();++index) {
        auto& point=path.curve_points[index];point.origin={0,0,10.0*index};
        point.definition=document::ConstructionDefinition::PointReference;
        point.references={datum(index?path.curve_points[index-1].container_origin.id:path.container_origin.id,
            "origin:plane:yz",index?1:2)};
    }
    const auto id=sweep.id;
    part.history.push_back(std::move(sweep));
    const auto curve=[&]() -> document::ConstructionObject& {return part.find_container(id)->sweep3d.path;};
    part.resolve_constructions();
    require(curve().reference_valid,"Owned Sweep point frames were unavailable");
    for(std::size_t i=0;i<4;++i)near(curve().curve_points[i].origin.x,2.0+i);
    curve().curve_points[0].references[0].offset=4;
    part.find_container(id)->placement.x=20;
    part.find_container(id)->placement.rotation_z=part.find_container(id)->placement.absolute_rotation_z=90;
    part.resolve_constructions();
    require(curve().reference_valid,"Owned Sweep references failed after placement changed");
    for(std::size_t i=0;i<4;++i)near(curve().curve_points[i].origin.x,4.0+i);
    const auto calculated=kernel.evaluate_history(part.kernel_operations());
    require(!calculated.empty()&&calculated.back().calculation_errors.empty()&&!calculated.back().kernel_shape.empty(),"Resolved Sweep did not produce a valid body");
    near(calculated.back().volume,4*std::numbers::pi*std::sqrt(909.0));
    const auto file=directory/"framed-sweep.prtz";
    part.save(file,calculated);std::vector<kernel::BodyResult> stored;
    auto reopened=document::PartDocument::load(file,&stored);
    require(reopened.find_container(id)->sweep3d.path.curve_points==curve().curve_points&&!stored.empty(),
        "Native Sweep reload lost its resolved references");
    reopened.resolve_constructions();
    require(reopened.find_container(id)->sweep3d.path.curve_points==curve().curve_points,
        "Regenerating the saved Sweep changed its reference chain");
}

}
int main(){try{const auto parent=fs::canonical(fs::temp_directory_path()),dir=parent/("zima-curve-references-"+document::PartDocument::create_default().document_id);require(fs::create_directory(dir),"Cannot create test directory");kernel::OcctKernel kernel;verify(kernel,dir,false);verify(kernel,dir,true);verify_frames(false);verify_frames(true);verify_sweep(kernel,dir);require(fs::canonical(dir).parent_path()==parent,"Unexpected cleanup path");fs::remove_all(dir);std::cout<<"Curve point references: local frames, ancestry, dependency guards and native Undo/Redo passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
