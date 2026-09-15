#include <zima/document/placement_json.hpp>
#include <zima/command_host/host.hpp>
#include <zima/workspace/construction_reference_operations.hpp>
#include <cmath>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool v,const char* text){if(!v)throw std::runtime_error(text);}
void near(double a,double b){if(std::abs(a-b)>1e-7)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
void verify(const kernel::OcctKernel& kernel,fs::path directory,bool assembly_mode) {
    workspace::Workspace live;command_host::Interaction interaction;command_host::Options options;options.interaction=[&]{return interaction;};
    std::string id,box_id;const auto file=directory/(assembly_mode?"construction-refs.asmz":"construction-refs.prtz");
    if(assembly_mode){auto doc=assembly::AssemblyDocument::create_default();id=doc.document_id;live.add_assembly(std::move(doc),file);}
    else {auto doc=document::PartDocument::create_default();id=doc.document_id;auto box=document::PartDocument::create_box_container();box.box={10,10,10};box_id=box.id;
        doc.insert_history_entry(document::PartHistoryKind::Feature,box.id);doc.history.push_back(box);doc.resolve_constructions();auto calculated=workspace::calculate_part_with_resolved_references(kernel,doc);live.add_part(std::move(doc),std::move(calculated),file);}
    live.activate(id);command_host::Host host(live,kernel,directory,options);
    const auto verify_cache=[&](const std::string& step){if(assembly_mode)return;const auto* state=live.open_part(id);const auto operations=state->session.document().kernel_operations(false,true);const auto& cache=state->session.calculated_boundaries();
        for(std::size_t i=0;i<cache.size();++i)if(cache[i].source_fingerprint!=kernel::history_fingerprint(operations,i+1)) {
            auto normalized=operations;auto& box=std::get<kernel::BoxRequest>(normalized.front().primitive);
            for(auto* v:{&box.translation.x,&box.translation.y,&box.translation.z,&box.rotation_degrees.x,&box.rotation_degrees.y,&box.rotation_degrees.z})if(*v==0)*v=0;
            throw std::runtime_error("Cache no longer matches after "+step+"; positive-zero normalization matches="+(cache[i].source_fingerprint==kernel::history_fingerprint(normalized,i+1)?"yes":"no"));
        }};
    verify_cache("fixture calculation");

    const auto run=[&](const char* cmd,Json args=Json::object()){const auto input=args.dump();const auto result=host.execute({{"command",cmd},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(cmd)+" "+input+": "+result.code+": "+result.message);verify_cache(std::string(cmd)+" "+input);return result.data;};
    const auto all=[&](){return assembly_mode?live.open_assembly(id)->session.document().constructions:live.open_part(id)->session.document().constructions;};
    const auto object=[&](const std::string& key){const auto values=all();const auto it=std::ranges::find(values,key,&document::ConstructionObject::id);require(it!=values.end(),"Missing construction");return *it;};
    const auto revision=[&](){return assembly_mode?live.open_assembly(id)->session.revision():live.open_part(id)->session.revision();};
    const auto create=[&](const char* kind,const char* name,Json values=Json::object(),Json extra=Json::object()){
        extra["kind"]=kind;extra["name"]=name;if(!values.empty())extra["values"]=std::move(values);return run("construction.create",extra).at("construction").get<std::string>();};
    const auto a=create("point","A",{{"x",12},{"y",7},{"z",3}}),b=create("point","B",{{"x",12},{"y",7},{"z",8}});
    const auto plane=create("plane","Source plane",{{"z",5}},{{"base_plane","xy"}});
    const auto point=create("point","Target point"),axis=create("axis","Target axis"),target_plane=create("plane","Target plane");
    const auto set=[&](const std::string& target,int index,const std::string& owner,const std::string& key,double offset=0){return run("construction.reference.set",{{"construction",target},{"index",index},{"reference",{{"owner",owner},{"key",key}}},{"offset_mm",offset}});};
    const auto args=[&](const std::string& target,int index,const std::string& owner,const std::string& key){return Json{{"construction",target},{"index",index},{"reference",{{"owner",owner},{"key",key}}}};};
    const auto reject=[&](Json request,const char* code){const auto before=all();const auto rev=revision();const auto result=host.execute({{"command","construction.reference.set"},{"arguments",std::move(request)}});
        if(result.ok||result.code!=code)throw std::runtime_error(std::string("Expected ")+code+", got "+result.code+": "+result.message);
        require(all()==before&&revision()==rev&&!host.change(),"Rejected reference mutated document");};
    const auto curve=create("curve3d","Referenced curve",Json::object(),{{"points",Json::array({{{"values",{{"x",0}}}},{{"values",{{"x",2}}}}})}});
    set(curve,0,object(a).container_origin.id,"point");near(object(curve).origin.x,12);near(object(curve).origin.z,3);near(object(curve).curve_points.front().origin.x,0);near(object(curve).curve_points.back().origin.x,2);
    const auto initial=all();const auto result=set(point,0,object(a).container_origin.id,"point");require(result.at("changed")==true&&result.at("body_calculated")==false,"Reference did not commit without body calculation");
    near(object(point).origin.x,12);near(object(point).origin.y,7);near(object(point).origin.z,3);const auto assigned=all();
    run("undo");require(all()==initial,"Construction reference Undo changed source or target");run("redo");require(all()==assigned,"Construction reference Redo changed identities");
    const auto rev=revision();require(set(point,0,object(a).container_origin.id,"point").at("changed")==false&&revision()==rev&&!host.change(),"No-op reference added history");
    set(axis,0,object(a).container_origin.id,"point");set(axis,1,object(b).container_origin.id,"point");near(object(axis).origin.z,3);near(std::abs(object(axis).direction.z),1);
    set(target_plane,0,object(plane).entity_id,"plane",2);const auto placed=object(target_plane),source=object(plane);
    require(placed.base_plane==document::LocalDatumPlane::XZ&&placed.definition==document::ConstructionDefinition::PointReference,"Plane did not follow first reference as GUI does");
    const auto dot=[](auto a,auto b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    near(std::abs(dot(placed.direction,source.direction)),1);near(dot(kernel::Vec3{placed.origin.x-source.entity_origin.x,placed.origin.y-source.entity_origin.y,placed.origin.z-source.entity_origin.z},source.direction),2);
    run("construction.set",{{"construction",target_plane},{"base_plane","xy"},{"offset_mm",3}});
    require(!object(target_plane).base_plane_auto&&object(target_plane).base_plane==document::LocalDatumPlane::XY,"Plane manual override missing");
    near(dot(object(target_plane).direction,source.direction),0);
    set(target_plane,0,object(plane).entity_id,"plane",2);
    require(!object(target_plane).base_plane_auto,"Reference replaced manual Plane choice");
    run("construction.set",{{"construction",target_plane},{"base_plane","auto"}});
    require(object(target_plane).base_plane_auto,"Plane AUTO was not restored");near(std::abs(dot(object(target_plane).direction,source.direction)),1);
    require(placed.references.size()==2&&placed.references[1].orientation_only&&placed.references[1].orientation_role=="front","Planar assignment lost separate FRONT reference");
    // The position row was locked by a point. Replacing it captures the current
    // measured distance from the plane, rather than accepting a requested jump.
    set(point,0,source.entity_id,"plane",99);near(object(point).origin.z,3);near(object(point).references.front().offset,-2);require(object(point).references.front().offset_locked,"Replacing locked reference unlocked distance");
    reject(args(point,1,source.entity_id,"plane"),"duplicate_reference");
    set(point,4,id+":origin","origin:plane:yz");require(std::ranges::any_of(object(point).references,[](const auto& r){return r.orientation_only&&r.orientation_role=="top";}),"Explicit TOP reference was omitted");
    reject(args(point,0,object(point).entity_id,"point"),"reference_not_available");
    const auto later=create("point","Later");reject(args(point,0,object(later).entity_id,"point"),"reference_not_available");
    reject(args(point,8,source.entity_id,"plane"),"invalid_arguments");reject(args(point,0,"missing","plane"),"reference_not_found");
    auto bad=args(point,0,source.entity_id,"plane");bad["reference"]["unexpected"]=true;reject(bad,"invalid_arguments");
    bad=args(axis,1,object(b).container_origin.id,"point");bad["offset_mm"]=3;reject(bad,"parameter_not_editable");
    if(!assembly_mode) {
        bad=args(point,0,source.entity_id,"plane");bad["reference"]["instance_path"]="occurrence";reject(bad,"invalid_reference");
        const auto face_point=create("point","Face target");const auto& geometry=live.open_part(id)->session.calculated_boundaries().back().mesh.original_references;
        bool checked=false;for(std::size_t i=0;i<geometry.triangle_references.size();++i){const auto& ref=geometry.triangle_references[i];if(ref.owner_id!=box_id)continue;
            const auto p=geometry.vertices[geometry.triangles[i*3]],q=geometry.vertices[geometry.triangles[i*3+1]],r=geometry.vertices[geometry.triangles[i*3+2]];
            const double nx=(q.y-p.y)*(r.z-p.z)-(q.z-p.z)*(r.y-p.y);
            if(std::abs(nx)<1e-8||std::abs(p.x-q.x)>1e-8||std::abs(p.x-r.x)>1e-8)continue;
            const auto key=ref.semantic_key;const double expected=p.x+(nx>0?2:-2);set(face_point,0,box_id,key,2);near(object(face_point).origin.x,expected);checked=true;break;}
        require(checked,"No original Box face was tested");near(live.open_part(id)->session.calculated_boundaries().back().volume,1000);
    }
    interaction.editing=true;reject(args(point,0,source.entity_id,"plane"),"editing_in_progress");interaction={};
    run("save");const auto current=all();const auto loaded=assembly_mode?assembly::AssemblyDocument::load(file).constructions:document::PartDocument::load(file).constructions;
    require(loaded.size()==current.size(),"Native save lost a construction");
    for(const auto& expected:current){const auto found=std::ranges::find(loaded,expected.id,&document::ConstructionObject::id);require(found!=loaded.end(),"Native save lost construction identity");
        if(found->entity_id!=expected.entity_id||found->references!=expected.references)throw std::runtime_error(std::string(assembly_mode?"Assembly ":"Part ")+expected.name+" native reference mismatch; entity="+expected.entity_id+" -> "+found->entity_id+"; before="+Json(expected.references).dump()+"; after="+Json(found->references).dump());
        near(found->origin.x,expected.origin.x);near(found->origin.z,expected.origin.z);}
}
}
int main(){try{const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-construction-refs-"+document::PartDocument::create_default().document_id);require(fs::create_directory(dir),"Cannot create fixture directory");kernel::OcctKernel kernel;verify(kernel,dir,false);verify(kernel,dir,true);require(dir.parent_path()==root,"Invalid cleanup root");fs::remove_all(dir);std::cout<<"Construction references: original geometry, orientation, locked distances, errors and native Undo/Redo passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
