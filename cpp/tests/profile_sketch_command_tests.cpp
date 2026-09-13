#include "assembly_profile_test_support.hpp"
#include <zima/workspace/sketch_operations.hpp>
#include <numbers>
#include <iostream>
using namespace assembly_profile_test;
namespace {
Json request(const char* command,Json args=Json::object()){return {{"command",command},{"arguments",std::move(args)}};}
void verify(const kernel::OcctKernel& kernel,const fs::path& directory,bool assembly,bool extrusion) {
    const std::string stem=std::string("batch-")+(assembly?"assembly-":"part-")+(extrusion?"extrusion":"revolution");
    Fixture f(kernel,directory,stem+"-fixture");
    if(!assembly)f.run("new",{{"type","part"},{"name",stem}});
    const auto document=f.live.active_document_id();
    const auto sketch=extrusion?f.rectangle(-1,-1.5,2,3):f.rectangle(1,0,1,4);
    Json args={{"sketch",sketch}};
    if(extrusion)args["length_forward_mm"]=4;
    else {const auto axis=f.line(sketch,0,0,0,4);f.run("sketch.segment.centerline",{{"sketch",sketch},{"segment",axis},{"centerline",true}});args["axis"]=axis;}
    if(assembly)args["targets"]={f.first};
    const std::string prefix=extrusion?"extrusion":"revolution",command=prefix+".sketch.edit";
    const auto created=f.run((prefix+".create").c_str(),args);const std::string container=created.at("container");
    const auto current=[&](){return workspace::document_sketch(f.live,document,sketch);};
    const auto revision=[&](){return assembly?f.state().session.revision():f.live.open_part(document)->session.revision();};
    const auto generation=[&](){return assembly?f.state().session.data_generation():f.live.open_part(document)->session.data_generation();};
    const auto volume=[&](){return assembly?f.volume(f.first):f.live.open_part(document)->session.calculated_boundaries().back().volume;};
    const auto expected=[&](double body){return assembly?1000-body:body;};
    const auto original=current();const auto original_revision=revision();
    const double old_volume=extrusion?24:12*std::numbers::pi,new_volume=extrusion?36:32*std::numbers::pi;
    near(volume(),expected(old_volume));
    auto operations=Json::array();
    for(const auto& point:original.points)if(std::abs(point.x-(extrusion?1:2))<1e-9)
        operations.push_back(request("sketch.point.move",{{"point",point.id},{"position",{point.x+1,point.y}}}));
    require(operations.size()==2,"Missing two profile points");
    const auto edited=f.run(command.c_str(),{{"container",container},{"operations",operations}});
    require(edited.at("changed")==true&&edited.at("body_calculated")==true&&edited.at("results").size()==2&&revision()==original_revision+1,"Profile batch did not calculate in one transaction");
    near(volume(),expected(new_volume));
    const auto stable=current().serialized();const auto stamp=revision(),gen=generation();
    const auto same=f.run(command.c_str(),{{"container",container},{"operations",operations}});
    require(same.at("changed")==false&&same.at("body_calculated")==false&&revision()==stamp&&generation()==gen&&!f.host.change(),"No-op batch recalculated or changed history");
    f.run("undo");require(current().serialized()==original.serialized(),"Batch Undo did not restore the complete Sketch");near(volume(),expected(old_volume));
    f.run("redo");require(current().serialized()==stable,"Batch Redo changed curve identities");near(volume(),expected(new_volume));
    const auto rejected=[&](Json input,const char* code,int failed=-1) {
        const auto before=current().serialized();const auto rev=revision(),generation_before=generation();const auto old=volume();
        const auto result=f.host.execute(request(command.c_str(),{{"container",container},{"operations",std::move(input)}}));
        if(result.ok||result.code!=code)throw std::runtime_error(command+" expected "+code+", got "+result.code+": "+result.message);
        if(failed>=0)require(result.data.at("operation_index")==failed,"Batch failure lost operation index");
        require(current().serialized()==before&&revision()==rev&&generation()==generation_before&&!f.host.change(),"Rejected profile batch changed model or history");near(volume(),old);
    };
    rejected(Json::array(),"invalid_arguments");
    auto oversized=Json::array();for(int i=0;i<1001;++i)oversized.push_back(operations.front());rejected(oversized,"invalid_arguments");
    auto pending=operations;for(auto& operation:pending)operation["arguments"]["position"][0]=extrusion?3:4;
    auto invalid=pending;invalid.push_back(request("save"));rejected(invalid,"unknown_command",2);
    invalid=pending;invalid.push_back(request("sketch.point.move",{{"point","missing"},{"position",{0,0}}}));rejected(invalid,"point_not_found",2);
    invalid=pending;invalid.front()["arguments"]["sketch"]="other";rejected(invalid,"invalid_arguments",0);
    rejected(Json::array({request("sketch.geometry.delete",{{"geometry",original.segments.front().id}})}),"profile_rejected");
    f.interaction.editing=true;rejected(operations,"editing_in_progress");f.interaction={};
    if(assembly) {
        const auto references=f.live.open_part(f.source)->session.calculated_boundaries().back().mesh.original_references;
        const auto edge=std::ranges::find_if(references.edges,[](const auto& item){return item.points.size()>1&&std::hypot(item.points.front().x-item.points.back().x,item.points.front().y-item.points.back().y)>1;});
        require(edge!=references.edges.end(),"Missing original edge for profile reference");
        const auto source_revision=f.live.open_part(f.source)->session.revision();
        const auto linked=f.run(command.c_str(),{{"container",container},{"operations",Json::array({request("sketch.reference.create",{{"kind","edge"},{"owner",edge->reference.owner_id},{"key",edge->reference.semantic_key},{"instance_path",assembly::InstancePath{}.child(f.first).encoded()}})})}});
        const auto reference=current().external_references.front();
        require(!reference.broken&&reference.id==linked.at("results")[0].at("reference").get<std::string>()&&reference.source_owner_id==edge->reference.owner_id&&reference.source_instance_path==assembly::InstancePath{}.child(f.first).encoded(),"Batch lost original reference identity or occurrence");
        near(volume(),expected(new_volume));require(f.live.open_part(f.source)->session.revision()==source_revision,"Profile batch changed source Part");
        f.run("undo");require(current().serialized()==stable,"Undo did not remove only the batched reference");
    }
    f.run("save");
    if(assembly) {
        const auto saved=assembly::AssemblyDocument::load(directory/(stem+"-fixture.asmz"));
        require(saved.find_cut(container)->definition.feature_id==created.at("feature").get<std::string>()&&std::ranges::find(saved.sketches,sketch,&sketcher::Sketch::id)->serialized()==stable,"Native Assembly lost batched profile identity");
        near(saved.find_occurrence(f.first)->calculated_source->volume,expected(new_volume));near(saved.find_occurrence(f.second)->calculated_source->volume,1000);
    } else {
        std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(directory/(stem+".prtz"),&cache);
        require(saved.find_container(container)->feature_id==created.at("feature").get<std::string>()&&saved.sketches.front().serialized()==stable,"Native Part lost batched profile identity");near(cache.back().volume,new_volume);
    }
}
}
int main(){try{
    const auto root=fs::canonical(fs::temp_directory_path());const auto directory=root/("zima-profile-batch-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create fixture directory");kernel::OcctKernel kernel;
    for(bool assembly:{false,true})for(bool extrusion:{false,true})verify(kernel,directory,assembly,extrusion);
    require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);
    std::cout<<"Profile batches: Part/Assembly extrusion and revolution, exact volumes, atomic errors, native files and single Undo passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
