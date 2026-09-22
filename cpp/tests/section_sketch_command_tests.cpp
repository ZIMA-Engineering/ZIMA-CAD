#include <zima/command_host/host.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <cmath>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Json request(const char* name,Json args=Json::object()){return {{"command",name},{"arguments",std::move(args)}};}
Json run(command_host::Host& host,const char* name,Json args=Json::object()) {
    const auto result=host.execute(request(name,std::move(args)));
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result.data;
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);
    run(host,"new",{{"type","part"},{"name","section-sketch"}});
    run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}});
    const auto doc=live.active_document_id();auto* part=live.open_part(doc);
    const auto id=run(host,"section.create",{{"path_mm",Json::array({Json::array({-20,0}),Json::array({20,0})})}}).at("object").get<std::string>();
    const auto before=part->session.document().sections.front();
    const auto first=before.sketch.points.front().id,second=before.sketch.points.back().id;
    const auto edits=Json::array({request("sketch.point.move",{{"point",first},{"position",Json::array({-20,2})}}),
        request("sketch.point.move",{{"point",second},{"position",Json::array({20,2})}})});
    const auto revision=part->session.revision();const auto fingerprint=part->session.calculated_boundaries().back().source_fingerprint;
    const auto moved=run(host,"section.sketch.edit",{{"object",id},{"operations",edits}});
    require(moved.at("changed")==true&&moved.at("results").size()==2&&part->session.revision()==revision+1,"Section batch did not commit exactly once");
    const auto after=part->session.document().sections.front();
    require(after.sketch.id==before.sketch.id&&after.sketch.segments.front().id==before.sketch.segments.front().id&&
        after.sketch.find_point(first)->y==2&&after.sketch.find_point(second)->y==2,"Section point edits lost geometry or stable identities");
    require(part->session.calculated_boundaries().back().source_fingerprint==fingerprint&&
        std::abs(part->session.calculated_boundaries().back().volume-6000)<1e-6,"Section sketch edit changed calculated body geometry");
    require(!run(host,"section.sketch.edit",{{"object",id},{"operations",edits}}).at("changed").get<bool>(),"Unchanged batch added history");
    run(host,"undo");require(document::serialize_sections(part->session.document().sections)==document::serialize_sections({before}),"Undo did not restore whole Section batch");
    run(host,"redo");
    const auto reject=[&](Json operations,const char* code=nullptr,int failed=-1) {
        const auto saved=document::serialize_sections(part->session.document().sections);
        const auto rev=part->session.revision(),generation=part->session.data_generation();
        const auto result=host.execute(request("section.sketch.edit",{{"object",id},{"operations",std::move(operations)}}));
        require(!result.ok,"Invalid Section batch accepted");
        if(code&&result.code!=code)throw std::runtime_error("Expected "+std::string(code)+", got "+result.code+": "+result.message);
        if(failed>=0)require(result.data.at("operation_index")==failed,"Batch error lost failing operation index");
        require(part->session.revision()==rev&&part->session.data_generation()==generation&&
            document::serialize_sections(part->session.document().sections)==saved,"Rejected batch partially changed Section");
    };
    reject(Json::array(),"invalid_arguments");
    auto pending=edits;pending[0]["arguments"]["position"]=Json::array({-20,3});pending[1]["arguments"]["position"]=Json::array({20,3});
    auto invalid=pending;invalid.push_back(request("save"));reject(invalid,"unknown_command",2);
    invalid=pending;invalid.push_back(request("sketch.point.move",{{"point",first},{"position",Json::array({0,0})},{"sketch","another"}}));reject(invalid,"invalid_arguments",2);
    invalid=pending;invalid.push_back(request("sketch.point.move",{{"point","missing"},{"position",Json::array({0,0})}}));reject(invalid,"point_not_found",2);
    invalid=pending;invalid.push_back(request("sketch.circle.create",{{"center",Json::array({0,0})},{"radius_mm",1}}));reject(invalid);
    const auto line=after.sketch.segments.front().id;
    reject(Json::array({request("sketch.geometry.delete",{{"geometry",line}})}));
    require(!host.execute(request("sketch.point.move",{{"sketch",before.sketch.id},{"point",first},{"position",Json::array({-20,1})}})).ok,
        "Ordinary Sketch command bypassed Section transaction");
    const auto rebuilt=Json::array({request("sketch.geometry.delete",{{"geometry",line}}),
        request("sketch.segment.create",{{"first",Json::array({-20,2})},{"second",Json::array({0,2})}}),
        request("sketch.segment.create",{{"first",Json::array({0,2})},{"second",Json::array({20,2})}})});
    run(host,"section.sketch.edit",{{"object",id},{"operations",rebuilt}});
    require(part->session.document().sections.front().sketch.segments.size()==2&&document::section_path(part->session.document().sections.front()).size()==2,
        "Atomic rebuild rejected valid final chain or changed its simplified path");
    const auto source=part->session.calculated_boundaries().back().mesh.original_references;
    const auto edge=std::ranges::find_if(source.edges,[](const auto& e){return e.points.size()>=2&&
        std::hypot(e.points.front().x-e.points.back().x,e.points.front().y-e.points.back().y)>1;});
    require(edge!=source.edges.end(),"Box has no original edge suitable for projection");
    const auto reference_args=Json{{"kind","edge"},{"owner",edge->reference.owner_id},{"key",edge->reference.semantic_key}};
    const auto referenced=run(host,"section.sketch.edit",{{"object",id},{"operations",Json::array({request("sketch.reference.create",reference_args)})}});
    const auto ref=referenced.at("results")[0].at("reference").get<std::string>();
    const auto& projected=part->session.document().sections.front().sketch.external_references.front();
    require(projected.id==ref&&projected.source_document_id==doc&&projected.source_owner_id==edge->reference.owner_id&&
        projected.source_semantic_key==edge->reference.semantic_key&&!projected.broken,"Section lost its original edge reference");
    reject(Json::array({request("sketch.reference.create",reference_args)}));
    const auto unlinked=part->session.document().sections.front().sketch.segments;
    run(host,"section.sketch.edit",{{"object",id},{"operations",Json::array({request("sketch.reference.refresh"),request("sketch.reference.delete",{{"reference",ref}})})}});
    require(part->session.document().sections.front().sketch.external_references.empty()&&part->session.document().sections.front().sketch.segments==unlinked,
        "Detaching a Section reference changed its native trace");
    run(host,"undo");require(part->session.document().sections.front().sketch.external_references.size()==1,"Undo did not restore Section reference identity");
    run(host,"redo");
    run(host,"save");const auto saved=document::PartDocument::load(dir/"section-sketch.prtz");
    require(document::serialize_sections(saved.sections)==document::serialize_sections(part->session.document().sections),"Native Part lost rebuilt Section Sketch");
    const auto source_revision=part->session.revision();
    run(host,"new",{{"type","assembly"},{"name","section-sketch-assembly"}});const auto aid=live.active_document_id();
    const auto one=run(host,"component.insert",{{"source",doc}}).at("occurrence").get<std::string>();
    const auto two=run(host,"component.insert",{{"source",doc}}).at("occurrence").get<std::string>();
    const auto assembly_section=run(host,"section.create",{{"path_mm",Json::array({Json::array({-20,0}),Json::array({20,0})})}}).at("object").get<std::string>();
    const auto* owner=live.open_assembly(aid);const auto packet=owner->session.document().components.front().calculated_source;
    const auto reference=[&](const std::string& occurrence){auto args=reference_args;args["instance_path"]=assembly::InstancePath{}.child(occurrence).encoded();return request("sketch.reference.create",args);};
    const auto assembly_before=owner->session.revision();
    run(host,"section.sketch.edit",{{"object",assembly_section},{"operations",Json::array({reference(one),reference(two)})}});
    const auto& refs=owner->session.document().sections.front().sketch.external_references;
    require(refs.size()==2&&refs[0].source_instance_path!=refs[1].source_instance_path&&refs[0].source_document_id==doc&&refs[1].source_document_id==doc,
        "Section merged original edges from repeated Assembly occurrences");
    require(owner->session.revision()==assembly_before+1&&owner->session.document().components.front().calculated_source.shares_with(packet)&&
        live.open_part(doc)->session.revision()==source_revision,"Section batch changed source data or committed more than once");
    run(host,"undo");require(owner->session.document().sections.front().sketch.external_references.empty(),"Assembly batch Undo lost its original Sketch");
    run(host,"redo");run(host,"save");
    const auto assembly_saved=assembly::AssemblyDocument::load(dir/"section-sketch-assembly.asmz");
    require(document::serialize_sections(assembly_saved.sections)==document::serialize_sections(owner->session.document().sections),"Native Assembly lost batched references");

}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());
    const auto dir=parent/("zima-section-sketch-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create test directory");verify(kernel,dir);
    require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);
    std::cout<<"Section Sketch command batches passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
