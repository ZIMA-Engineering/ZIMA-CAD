#include <zima/command_host/host.hpp>
#include <zima/workspace/section_reference_operations.hpp>
#include <zima/document/placement_json.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void near(double value,double expected){if(std::abs(value-expected)>1e-6)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(value));}
double area(const document::SectionResult& result) {
    double total=0;for(const auto& patch:result.patches)for(const auto& triangle:patch.triangles) {
        const auto a=triangle[0],b=triangle[1],c=triangle[2];
        const kernel::Vec3 u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z};
        const kernel::Vec3 cross{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
        total+=std::sqrt(cross.x*cross.x+cross.y*cross.y+cross.z*cross.z)/2;
    }return total;
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Interaction interaction;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};options.interaction=[&]{return interaction;};
    command_host::Host host(live,kernel,dir,options);
    const auto run=[&](const char* name,Json args=Json::object()){const auto result=host.execute({{"command",name},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result.data;};
    run("new",{{"type","part"},{"name","section-reference-source"}});const auto part_id=live.active_document_id();
    const auto box=run("box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}}).at("container").get<std::string>();
    const auto cache=live.open_part(part_id)->session.calculated_boundaries().back();
    const auto make=[&]{return run("section.create",{{"path_mm",{{-50,0},{50,0}}},{"plane","XY"},{"show_cut",true}}).at("object").get<std::string>();};
    const auto section=make();const auto get=[&]{return run("section.get",{{"object",section}});};
    const auto before=get();Json request={{"object",section},{"index",0},{"reference",{{"owner",part_id+":origin"},{"key","origin:plane:yz"}}},{"offset_mm",2}};
    const auto change=run("section.reference.set",request);require(change.at("changed")==true&&change.at("body_calculated")==false,"Section reference did not report its display-only transaction");
    const auto current=[&]()->const document::SectionDefinition&{return live.open_part(part_id)->session.document().sections.front();};
    near(current().placement.x,2);near(area(document::calculate_section(live.authoritative_viewer_mesh(part_id),current())),600);
    require(current().placement.references.size()==2&&current().placement.references.back().orientation_only,"Section reference lost its separate FRONT row");
    require(get().at("sketch")==before.at("sketch")&&get().at("origin")==before.at("origin")&&get().at("path_mm")==before.at("path_mm"),"Section reference replaced owned identities or path");
    const auto revision=live.open_part(part_id)->session.revision();
    require(run("section.reference.set",request).at("changed")==false&&live.open_part(part_id)->session.revision()==revision&&!host.change(),"Repeated Section reference created history");
    request["offset_mm"]=3;run("section.reference.set",request);near(current().placement.x,3);
    run("undo");near(current().placement.x,2);run("redo");near(current().placement.x,3);
    const auto reject=[&](Json args,const char* code){
        const auto state=document::serialize_sections(live.open_part(part_id)->session.document().sections);
        const auto rev=live.open_part(part_id)->session.revision();const auto* pointer=live.open_part(part_id)->session.calculated_boundaries().data();
        const auto result=host.execute({{"command","section.reference.set"},{"arguments",std::move(args)}});
        if(result.ok||result.code!=code)throw std::runtime_error(std::string("Expected ")+code+", got "+result.code+": "+result.message);
        require(!host.change()&&live.open_part(part_id)->session.revision()==rev&&live.open_part(part_id)->session.calculated_boundaries().data()==pointer&&document::serialize_sections(live.open_part(part_id)->session.document().sections)==state,"Rejected Section reference changed document or body");
    };
    auto bad=request;bad["object"]="";reject(bad,"missing_argument");bad=request;bad["index"]=1;reject(bad,"duplicate_reference");bad=request;bad["index"]=3;reject(bad,"parameter_not_editable");
    for(const auto index:{-1LL,5LL,4294967296LL}){bad=request;bad["index"]=index;reject(bad,"invalid_arguments");}
    bad=request;bad["offset_mm"]=std::numeric_limits<double>::infinity();reject(bad,"invalid_arguments");
    bad=request;bad["reference"]["owner"]="missing";reject(bad,"reference_not_found");
    bad=request;bad["reference"]["owner"]=current().container_origin.id;reject(bad,"invalid_reference");
    bad=request;bad["reference"]["instance_path"]="another occurrence";reject(bad,"invalid_reference");
    bad=request;bad["reference"]["unknown"]=true;reject(bad,"invalid_arguments");
    interaction.editing=true;reject(request,"editing_in_progress");interaction={};
    require(live.open_part(part_id)->session.calculated_boundaries().back().kernel_shape==cache.kernel_shape&&live.open_part(part_id)->session.calculated_boundaries().back().mesh.vertices==cache.mesh.vertices,"Section reference recalculated or changed the body");near(cache.volume,6000);
    auto locked=live.open_part(part_id)->session.document();locked.sections.front().placement.references.front().offset_locked=true;
    live.open_part(part_id)->session.commit(std::move(locked),live.open_part(part_id)->session.calculated_boundaries());
    request["offset_mm"]=99;run("section.reference.set",request);near(current().placement.x,3);require(current().placement.references.front().offset_locked,"Reference replacement unlocked the measured distance");
    const auto section_before_removal=document::serialize_sections(live.open_part(part_id)->session.document().sections);
    require(run("placement.reference.remove",{{"object",section},{"index",0}}).at("body_calculated")==false,"Section removal recalculated a body");
    require(current().placement.references.empty(),"Section removal retained its paired orientation");near(area(document::calculate_section(live.authoritative_viewer_mesh(part_id),current())),600);
    require(live.open_part(part_id)->session.calculated_boundaries().back().kernel_shape==cache.kernel_shape,"Section removal changed stored solid geometry");
    run("undo");require(document::serialize_sections(live.open_part(part_id)->session.document().sections)==section_before_removal,"Section removal Undo lost its sources");run("redo");
    run("save");std::vector<kernel::BodyResult> reopened_cache;const auto saved=document::PartDocument::load(dir/"section-reference-source.prtz",&reopened_cache);
    require(document::serialize_sections(saved.sections)==document::serialize_sections(live.open_part(part_id)->session.document().sections)&&!reopened_cache.empty(),"Part save lost Section references or body");near(reopened_cache.back().volume,6000);
    // Choose an existing original face by its analytic plane location, never by a kernel index.
    const auto faces=run("reference.list",{{"owner",box},{"kind","face"}});
    std::string face;for(const auto& row:faces.at("items")) {
        const auto value=run("reference.get",row);if(value.at("surface").at("kind")=="plane"&&std::abs(value.at("surface").at("axis")[0].get<double>())>.99&&value.at("surface").at("origin")[0].get<double>()>4.99){face=row.at("key");break;}
    }require(!face.empty(),"Fixture has no original positive-X face");
    run("new",{{"type","assembly"},{"name","section-reference-sub"}});const auto sub=live.active_document_id();const auto leaf=run("component.insert",{{"source",part_id}}).at("occurrence").get<std::string>();run("save");
    run("new",{{"type","assembly"},{"name","section-reference-top"}});const auto top=live.active_document_id();
    const auto first=run("component.insert",{{"source",sub}}).at("occurrence").get<std::string>(),second=run("component.insert",{{"source",sub}}).at("occurrence").get<std::string>();
    const auto first_path=assembly::InstancePath{}.child(first).child(leaf).encoded(),second_path=assembly::InstancePath{}.child(second).child(leaf).encoded();
    run("component.set",{{"instance_path",assembly::InstancePath{}.child(second).encoded()},{"placement",{{"x_mm",25}}}});
    const auto source_packet=live.open_assembly(top)->session.document().find_occurrence(first)->calculated_source;
    const auto assembly_section=make();Json nested={{"object",assembly_section},{"index",0},{"reference",{{"owner",box},{"key",face},{"instance_path",first_path}}},{"offset_mm",-2},{"derive_orientation",false}};
    run("section.reference.set",nested);const auto first_value=run("section.get",{{"object",assembly_section}});const auto& first_cut=live.open_assembly(top)->session.document().sections.front();near(first_cut.placement.x,3);near(area(document::calculate_section(live.authoritative_viewer_mesh(top),first_cut)),600);
    nested["reference"]["instance_path"]=second_path;run("section.reference.set",nested);
    const auto& second_cut=live.open_assembly(top)->session.document().sections.front();near(second_cut.placement.x,28);near(area(document::calculate_section(live.authoritative_viewer_mesh(top),second_cut)),600);
    require(second_cut.placement.references.front().instance_path==second_path&&live.open_assembly(top)->session.document().find_occurrence(first)->calculated_source.shares_with(source_packet),"Section reference lost exact occurrence or replaced source geometry");
    run("undo");near(live.open_assembly(top)->session.document().sections.front().placement.x,3);run("redo");run("save");
    const auto reopened=assembly::AssemblyDocument::load(dir/"section-reference-top.asmz");near(reopened.sections.front().placement.x,28);require(reopened.sections.front().placement.references.front().instance_path==second_path,"Assembly save merged repeated Section reference occurrences");
    const auto nested_bound=document::serialize_sections(live.open_assembly(top)->session.document().sections);
    require(run("placement.reference.remove",{{"object",assembly_section},{"index",0}}).at("body_calculated")==false,"Assembly Section removal regenerated bodies");
    require(live.open_assembly(top)->session.document().sections.front().placement.references.empty(),"Assembly Section retained removed occurrence");
    near(area(document::calculate_section(live.authoritative_viewer_mesh(top),live.open_assembly(top)->session.document().sections.front())),600);
    require(live.open_assembly(top)->session.document().find_occurrence(first)->calculated_source.shares_with(source_packet),"Assembly Section removal replaced source geometry");
    run("undo");require(document::serialize_sections(live.open_assembly(top)->session.document().sections)==nested_bound,"Assembly Section removal Undo lost exact occurrence");run("redo");run("save");
    require(assembly::AssemblyDocument::load(dir/"section-reference-top.asmz").sections.front().placement.references.empty(),"Native Assembly Section restored removed source");
    run("component.activate",{{"instance_path",first_path}});
    const auto denied=host.execute({{"command","section.reference.set"},{"arguments",request}});require(!denied.ok&&denied.code=="unsupported_context","A hidden source Part was edited through top-level Section controls");
}
}
int main(){try{const auto root=fs::canonical(fs::temp_directory_path()),dir=root/("zima-section-reference-"+document::PartDocument::create_default().document_id);require(fs::create_directory(dir),"Cannot create fixture directory");kernel::OcctKernel kernel;verify(kernel,dir);require(fs::canonical(dir).parent_path()==root,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Section references: original planes, cut area, exact nested occurrences, body preservation and native Undo passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
