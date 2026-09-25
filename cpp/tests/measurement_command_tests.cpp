#include "profile_solid_fixture.hpp"
#include "profile_command_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/document/measurement_record.hpp>
#include <zima/document/file_path.hpp>
#include <zima/kernel/stable_id.hpp>
#include <cmath>
#include <numbers>
#include <iostream>
using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void near(double actual,double expected,const char* text){require(std::abs(actual-expected)<1e-7,text);}
commands::Result run(command_host::Host& host,const char* command,Json args=Json::object()) {
    auto value=host.execute({{"command",command},{"arguments",std::move(args)}});
    if(!value.ok)throw std::runtime_error(std::string(command)+": "+value.code+": "+value.message);return value;
}
Json ref(const char* kind,const std::string& owner,const std::string& key={},const std::string& path={}) {
    return {{"kind",kind},{"owner",owner},{"key",key},{"instance_path",path}};
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;bool editing=false;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
    options.interaction=[&]{command_host::Interaction value;value.editing=editing;return value;};
    command_host::Host host(live,kernel,dir,options);
    require(host.execute_text("measurement.list").code=="unsupported_document","Measurement query accepted no document");
    run(host,"new",{{"type","part"},{"name","measurement-source"}});
    const auto box=zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}}).data.at("container").get<std::string>();
    const auto id=live.active_document_id();
    Json material=Json::array({{{"key","MASS_DENSITY"},{"value","2700"},{"unit","kg/m^3"}}});
    run(host,"document.material.set",{{"properties",material}});
    const auto measure=[&](Json refs,const std::string& source=std::string{}) {
        Json args={{"references",std::move(refs)}};if(!source.empty())args["document"]=source;
        return run(host,"measurement.evaluate",std::move(args)).data;
    };
    auto object=ref("object",box);const auto initial=measure(Json::array({object}));
    near(initial["values"][0]["volume"]["value"],6000,"Box volume");
    near(initial["values"][0]["area"]["value"],2200,"Box surface area");
    near(initial["values"][0]["mass"]["value"],.0162,"Mass must use kg and mm3");
    require(initial["values"][0]["volume"]["approximate"]==false&&initial["body_calculated"]==false,"Exact cached volume was not reported");
    const auto plane=ref("plane",id+":origin","origin:plane:xy");
    const auto top=ref("face",box,test::profile_key(live.open_part(id)->session.document(),box,"z_max"));auto result=measure(Json::array({plane,top}));
    near(result["distance"]["value"]["value"],15,"Root plane to finite top face");
    near(result["values"][1]["area"]["value"],200,"Exact original face area");
    const auto x_axis=ref("axis",id+":origin","origin:axis:x");
    near(measure(Json::array({x_axis,top}))["distance"]["value"]["value"],15,"Root axis to finite face");
    const auto& points=live.open_part(id)->session.calculated_boundaries().back().mesh.original_references.points;
    const auto low=std::ranges::find_if(points,[](const auto& p){return p.position.x==-5&&p.position.y==-10&&p.position.z==-15;});
    const auto high=std::ranges::find_if(points,[](const auto& p){return p.position.x==5&&p.position.y==10&&p.position.z==15;});
    require(low!=points.end()&&high!=points.end(),"Original box vertices unavailable");
    const auto low_ref=ref("point",low->reference.owner_id,low->reference.semantic_key);
    const auto high_ref=ref("point",high->reference.owner_id,high->reference.semantic_key);
    result=measure(Json::array({low_ref,high_ref}));near(result["distance"]["value"]["value"],std::sqrt(1400.),"Box diagonal");
    const auto revision=live.open_part(id)->session.revision(),generation=live.open_part(id)->session.data_generation();
    const auto shape=live.open_part(id)->session.calculated_boundaries().back().kernel_shape;
    editing=true;run(host,"measurement.list");measure(Json::array({object}));editing=false;
    const auto rejected=[&](Json args,const char* code) {
        const auto response=host.execute({{"command","measurement.evaluate"},{"arguments",std::move(args)}});
        require(!response.ok&&response.code==code,"Invalid measurement input was accepted or misreported");
        require(!host.change(),"Rejected query reported a mutation");
    };
    for(const auto& invalid:std::vector<Json>{Json::array(),Json::array({object,object,object}),Json::array({1}),
        Json::array({{{"kind","unknown"},{"owner",box},{"key",test::profile_key(live.open_part(live.active_document_id())->session.document(),box,"z_max")}}}),
        Json::array({{{"kind","point"},{"owner",box},{"key",test::profile_key(live.open_part(live.active_document_id())->session.document(),box,"z_max")},{"position",{0,0,0}}}})})
        rejected({{"references",invalid}},"invalid_arguments");
    rejected({{"references",Json::array({ref("object",box,"ignored-key")})}},"invalid_reference");
    rejected({{"references",Json::array({ref("point",box,"",{})})}},"invalid_reference");
    rejected({{"references",Json::array({ref("face",box,"container:display")})}},"invalid_reference");
    rejected({{"references",Json::array({ref("object","",{},"broken-path")})}},"invalid_reference");
    rejected({{"references",Json::array({object,ref("face",box,"missing")})}},"missing_reference");
    const Json missing_args={{"references",Json::array({object,ref("face",box,"missing")})}};
    const auto missing=host.execute({{"command","measurement.evaluate"},{"arguments",missing_args}});
    require(missing.data.at("reference_index")==1,"Missing reference index was lost");
    require(host.execute({{"command","measurement.list"},{"arguments",{{"limit",0}}}}).code=="invalid_arguments","Invalid page accepted");
    require(host.execute({{"command","measurement.get"},{"arguments",{{"object","missing"}}}}).code=="measurement_not_found","Missing record accepted");
    auto* part=live.open_part(id);
    require(part->session.revision()==revision&&part->session.data_generation()==generation&&part->session.calculated_boundaries().back().kernel_shape==shape,"Queries changed model state");
    auto saved_json=initial;
    saved_json["id"]="saved-measurement";saved_json["name"]="Control volume";
    saved_json["body_id"]=part->session.document().body_history.active_body_id();saved_json["after_object_id"]=box;
    const auto saved=document::parse_measurements(Json::array({saved_json}).dump()).front();
    auto next=part->session.document();next.measurements.push_back(saved);part->session.commit(std::move(next),part->session.calculated_boundaries());
    require(run(host,"measurement.list",{{"offset",0},{"limit",1}}).data.at("total")==1,"Stored measurements missing from list");
    require(run(host,"measurement.list",{{"offset",1}}).data.at("items").empty(),"Measurement pagination wrong");
    const auto get=run(host,"measurement.get",{{"object",saved.id}}).data;
    require(get["saved_values"]==true&&get["references"]==initial["references"]&&get["values"]==initial["values"]&&get["units"]["mass"]=="kg","Stored record changed values or units");
    zima::test::resize_rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"container",box},{"height_mm","60"}});
    near(measure(Json::array({object}))["values"][0]["volume"]["value"],12000,"Measurement ignored source change");
    require(run(host,"measurement.get",{{"object",saved.id}}).data.at("values")==initial.at("values"),"Read query overwrote last saved measurement");
    run(host,"save");run(host,"close",{{"document",id}});
    run(host,"open",{{"path",document::path_to_utf8(dir/"measurement-source.prtz")}});
    require(run(host,"measurement.get",{{"object",saved.id}}).data.at("references")==initial.at("references"),"Native Part lost saved measurement identity");
    run(host,"new",{{"type","assembly"},{"name","measurement-sub"}});const auto sub=live.active_document_id();
    const auto leaf=run(host,"component.insert",{{"source",id}}).data.at("occurrence").get<std::string>();run(host,"save");
    run(host,"new",{{"type","assembly"},{"name","measurement-top"}});const auto parent=live.active_document_id();
    const auto first=run(host,"component.insert",{{"source",sub}}).data.at("occurrence").get<std::string>();
    const auto second=run(host,"component.insert",{{"source",sub}}).data.at("occurrence").get<std::string>();
    auto* owner=live.open_assembly(parent);auto placed=owner->session.document();placed.find_occurrence(second)->placement.x=50;owner->session.commit(std::move(placed));
    const auto first_path=assembly::InstancePath{}.child(first).child(leaf).encoded();
    const auto second_path=assembly::InstancePath{}.child(second).child(leaf).encoded();
    auto first_object=ref("object","",{},first_path),second_object=ref("object","",{},second_path);
    const auto packet=owner->session.document().find_occurrence(first)->calculated_source;const auto asm_revision=owner->session.revision();
    auto pair=measure(Json::array({first_object,second_object}));
    near(pair["distance"]["value"]["value"],40,"Repeated nested objects measured wrong occurrence");
    near(pair["values"][0]["volume"]["value"],12000,"Nested source volume");
    near(pair["values"][0]["mass"]["value"],.0324,"Nested source mass");
    auto first_face=top,second_face=top;first_face["instance_path"]=first_path;second_face["instance_path"]=second_path;
    near(measure(Json::array({first_face,second_face}))["distance"]["value"]["value"],40,"Exact nested face paths merged");
    near(measure(Json::array({object}),id)["values"][0]["volume"]["value"],12000,"Inactive source query failed");
    require(live.active_document_id()==parent&&owner->session.revision()==asm_revision&&owner->session.document().find_occurrence(first)->calculated_source.shares_with(packet),"Query activated or recalculated a document");
    auto asm_saved=saved;asm_saved.id="saved-pair";asm_saved.references={{kernel::MeasurementKind::Object,"","",first_path},{kernel::MeasurementKind::Object,"","",second_path}};
    auto row=pair;row["id"]=asm_saved.id;row["name"]="Gap";row["body_id"]="";row["after_object_id"]="";
    auto assembly_doc=owner->session.document();assembly_doc.measurements=document::parse_measurements(Json::array({row}).dump());owner->session.commit(std::move(assembly_doc));
    run(host,"save");run(host,"close",{{"document",parent}});run(host,"open",{{"path",document::path_to_utf8(dir/"measurement-top.asmz")}});
    const auto restored=run(host,"measurement.get",{{"object",asm_saved.id}}).data;
    require(restored.at("references")==pair.at("references")&&restored.at("distance")==pair.at("distance"),"Native Assembly lost measurement paths or witness points");
    run(host,"new",{{"type","part"},{"name","measurement-curves"}});
    const auto cylinder=zima::test::circular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"radius_mm","5"},{"height_mm","10"}}).data.at("container").get<std::string>();
    const auto& edges=live.open_part(live.active_document_id())->session.calculated_boundaries().back().mesh.original_references.edges;
    const auto circle=std::ranges::find_if(edges,[&](const auto& edge){return edge.reference.owner_id==cylinder&&edge.measured_length&&std::abs(*edge.measured_length-10*std::numbers::pi)<1e-7;});
    require(circle!=edges.end(),"Original circular edge missing");
    const auto circle_measure=measure(Json::array({ref("curve",cylinder,circle->reference.semantic_key)}));
    near(circle_measure["values"][0]["length"]["value"],10*std::numbers::pi,"Circular edge lost exact length");
    require(circle_measure["values"][0]["length"]["approximate"]==false,"Stored exact circle length marked approximate");
    const auto circular_distance=measure(Json::array({ref("curve",cylinder,circle->reference.semantic_key),
        ref("plane",live.active_document_id()+":origin","origin:plane:yz")}));
    near(circular_distance["distance"]["value"]["value"],0,"Circular boundary must intersect the axial datum plane");
    require(circular_distance["distance"]["value"]["approximate"]==true&&circular_distance["values"][0]["length"]["approximate"]==false,
        "Distance approximation leaked into the exact stored curve length");
    rejected({{"references",Json::array({ref("axis",cylinder,circle->reference.semantic_key)})}},"missing_reference");
    const auto& faces=live.open_part(live.active_document_id())->session.calculated_boundaries().back().mesh.original_references.triangle_references;
    const auto curved=std::ranges::find_if(faces,[&](const auto& face){return face.owner_id==cylinder&&face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder;});
    require(curved!=faces.end(),"Original cylinder surface missing");
    rejected({{"references",Json::array({ref("plane",cylinder,curved->semantic_key)})}},"missing_reference");
    run(host,"new",{{"type","drawing"},{"name","measurement-drawing"}});
    require(host.execute_text("measurement.list").code=="unsupported_document","Drawing treated as a Part measurement document");
}
}
int main(){try {
    kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());
    const auto dir=parent/("zima-measurement-command-"+kernel::make_stable_id());
    require(fs::create_directory(dir),"Cannot create test directory");verify(kernel,dir);
    require(fs::canonical(dir).parent_path()==parent,"Unexpected cleanup path");fs::remove_all(dir);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
