#include "assembly_profile_test_support.hpp"
#include <zima/workspace/placement_edit.hpp>
#include <iostream>
using namespace assembly_profile_test;
namespace {
std::string path(const std::string& id){assembly::InstancePath result;result.occurrence_ids={id};return result.encoded();}
void verify(const kernel::OcctKernel& kernel,const fs::path& directory,bool nested,bool child_mode) {
    const std::string name=std::string(nested?"nested":"flat")+(child_mode?"-curve-refs":"-construction-refs");
    Fixture f(kernel,directory,name);
    std::string first=f.first,second=f.second,owner=f.owner;
    if(nested) {
        f.run("save");f.run("new",{{"type","assembly"},{"name",name+"-top"}});owner=f.live.active_document_id();
        first=f.run("component.insert",{{"source",f.owner}}).at("occurrence");second=f.run("component.insert",{{"source",f.owner}}).at("occurrence");
    }
    f.run("component.set",{{"instance_path",path(second)},{"placement",{{"x_mm",30}}}});
    const auto doc=[&]() -> const assembly::AssemblyDocument& {return f.live.open_assembly(owner)->session.document();};
    const auto original_part=f.live.open_part(f.source)->session.revision();
    const auto first_geometry=f.doc().find_occurrence(f.first)->calculated_source,second_geometry=f.doc().find_occurrence(f.second)->calculated_source;
    const auto created=child_mode?f.run("construction.create",{{"kind","curve3d"},{"name","Occurrence curve"},{"values",{{"x",20},{"rotation_z",90}}},
        {"points",Json::array({{{"values",{{"x",0}}}},{{"values",{{"x",10}}}},{{"values",{{"x",10},{"y",10}}}}})}})
        :f.run("construction.create",{{"kind","point"},{"name","Occurrence reference"}});
    const auto point=child_mode?created.at("children")[1].get<std::string>():created.at("construction").get<std::string>();
    const auto scene=doc().build_scene();const auto& geometry=scene.original_references;
    std::string source_path,other_path,key;double plane_x{},normal_x{};
    for(std::size_t i=0;i<geometry.triangle_references.size();++i) {
        const auto& ref=geometry.triangle_references[i];if(ref.owner_id!=f.box||ref.instance_path.empty())continue;
        const auto ids=assembly::InstancePath::decode(ref.instance_path).occurrence_ids;
        if(ids.size()!=(nested?2:1)||ids.front()!=second||(nested&&ids.back()!=f.first))continue;
        const auto a=geometry.vertices[geometry.triangles[i*3]],b=geometry.vertices[geometry.triangles[i*3+1]],c=geometry.vertices[geometry.triangles[i*3+2]];
        const double nx=(b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y);
        if(std::abs(nx)<1e-8||std::abs(a.x-b.x)>1e-8||std::abs(a.x-c.x)>1e-8)continue;
        source_path=ref.instance_path;key=ref.semantic_key;plane_x=a.x;normal_x=nx>0?1:-1;break;
    }
    require(!source_path.empty(),"Missing original geometry of the exact occurrence");
    for(const auto& ref:geometry.triangle_references) {
        if(ref.owner_id!=f.box||ref.semantic_key!=key||ref.instance_path.empty())continue;
        const auto ids=assembly::InstancePath::decode(ref.instance_path).occurrence_ids;
        if(ids.size()==(nested?2:1)&&ids.front()==first&&(!nested||ids.back()==f.first)){other_path=ref.instance_path;break;}
    }
    require(!other_path.empty()&&other_path!=source_path,"Repeated occurrences share one identity");
    const auto set=[&](const std::string& occurrence){return f.run("construction.reference.set",{{"construction",point},{"index",0},
        {"reference",{{"owner",f.box},{"key",key},{"instance_path",occurrence}}},{"offset_mm",2},{"derive_orientation",false}});};
    const auto get=[&]() -> const document::ConstructionObject& {return *doc().find_construction(point);};
    const auto world_x=[&](){return child_mode?20-get().origin.y:get().origin.x;};
    set(source_path);near(world_x(),plane_x+normal_x*2);require(get().references.front().instance_path==source_path,"The reference lost its selected occurrence");
    set(other_path);near(world_x(),plane_x-30+normal_x*2);require(get().references.front().instance_path==other_path,"Reference replacement used the first matching source ID");
    f.run("undo");near(world_x(),plane_x+normal_x*2);require(get().references.front().instance_path==source_path,"Undo lost occurrence identity");
    f.run("redo");near(world_x(),plane_x-30+normal_x*2);
    const auto before=doc().constructions;const auto revision=f.live.open_assembly(owner)->session.revision();
    const auto invalid=f.host.execute({{"command","construction.reference.set"},{"arguments",{{"construction",point},{"index",0},{"reference",{{"owner",f.box},{"key",key},{"instance_path",path("missing")}}}}}});
    require(!invalid.ok&&invalid.code=="reference_not_found"&&doc().constructions==before&&f.live.open_assembly(owner)->session.revision()==revision&&!f.host.change(),"Unknown occurrence mutated the construction");
    require(f.live.open_part(f.source)->session.revision()==original_part&&f.doc().find_occurrence(f.first)->calculated_source.shares_with(first_geometry)&&f.doc().find_occurrence(f.second)->calculated_source.shares_with(second_geometry),"Construction reference mutated or recalculated source Parts");
    f.run("save");const auto loaded=assembly::AssemblyDocument::load(f.live.open_assembly(owner)->path);
    require(loaded.find_construction(point)&&loaded.find_construction(point)->references.front().instance_path==other_path,"Native Assembly lost the full occurrence path");near(loaded.find_construction(point)->origin.x,get().origin.x);
}
}
int main(){try{const auto root=fs::canonical(fs::temp_directory_path()),directory=root/("zima-construction-occurrences-"+document::PartDocument::create_default().document_id);require(fs::create_directory(directory),"Cannot create fixture directory");kernel::OcctKernel kernel;for(bool nested:{false,true})for(bool child_mode:{false,true})verify(kernel,directory,nested,child_mode);require(directory.parent_path()==root,"Invalid cleanup root");fs::remove_all(directory);std::cout<<"Construction references preserve repeated and nested occurrences without source calculation\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
