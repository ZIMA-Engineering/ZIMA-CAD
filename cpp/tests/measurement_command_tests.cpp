#include "profile_solid_fixture.hpp"
#include "profile_command_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/document/measurement_record.hpp>
#include <zima/document/file_path.hpp>
#include <zima/workspace/metadata_operations.hpp>
#include <zima/assembly/physical_properties.hpp>
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
void verify_mixed_units(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);
    const auto execute=[&](const char* n,Json a){return run(host,n,std::move(a));};
    const auto settings=[&](const std::string& id,const char* length,const char* mass){
        auto value=workspace::file_settings(live,id);value.units["Length"]=length;value.units["Mass"]=mass;value.units["Angle"]="rad";
        require(!workspace::set_file_settings(live,kernel,id,value).calculated,"Unit metadata regenerated geometry");
    };
    run(host,"new",{{"type","part"},{"name","mixed-mm"}});const auto mm=live.active_document_id();
    test::rectangular_commands(execute,{{"length_mm",25.4},{"width_mm",50.8},{"height_mm",76.2}});
    run(host,"document.material.set",{{"properties",Json::array({{{"key","MASS_DENSITY"},{"value","2700"},{"unit","kg/m^3"}}})}});run(host,"save");
    run(host,"new",{{"type","part"},{"name","mixed-inch"}});const auto inch=live.active_document_id();settings(inch,"in","lb");
    test::rectangular_commands(execute,{{"length_mm",25.4},{"width_mm",25.4},{"height_mm",25.4}});
    run(host,"document.material.set",{{"properties",Json::array({{{"key","MASS_DENSITY"},{"value","0.1"},{"unit","lb/in^3"}}})}});run(host,"save");
    run(host,"new",{{"type","assembly"},{"name","mixed-sub"}});const auto sub=live.active_document_id();settings(sub,"cm","g");
    const auto a=run(host,"component.insert",{{"source",mm}}).data.at("occurrence").get<std::string>();
    const auto b=run(host,"component.insert",{{"source",inch}}).data.at("occurrence").get<std::string>();
    auto placed=live.open_assembly(sub)->session.document();placed.find_occurrence(b)->placement.x=101.6;
    placed.find_occurrence(a)->grounded=placed.find_occurrence(b)->grounded=true;live.open_assembly(sub)->session.commit(std::move(placed));run(host,"save");
    run(host,"new",{{"type","assembly"},{"name","mixed-top"}});const auto top=live.active_document_id();settings(top,"in","lb");
    const auto first=run(host,"component.insert",{{"source",sub}}).data.at("occurrence").get<std::string>();
    const auto second=run(host,"component.insert",{{"source",sub}}).data.at("occurrence").get<std::string>();
    placed=live.open_assembly(top)->session.document();placed.find_occurrence(second)->placement.x=254;placed.find_occurrence(second)->placement.rotation_z=90;
    placed.find_occurrence(first)->grounded=placed.find_occurrence(second)->grounded=true;live.open_assembly(top)->session.commit(std::move(placed));
    const auto first_a=assembly::InstancePath{}.child(first).child(a).encoded(),first_b=assembly::InstancePath{}.child(first).child(b).encoded();
    const auto second_a=assembly::InstancePath{}.child(second).child(a).encoded(),second_b=assembly::InstancePath{}.child(second).child(b).encoded();
    const auto pair=Json::array({ref("object","",{},first_a),ref("object","",{},first_b)});
    double va=25.4*50.8*76.2,ma=va*2700e-9,model_height=76.2;
    const double vb=std::pow(25.4,3),mb=.1*.45359237;
    double area_a=2*(25.4*50.8+25.4*76.2+50.8*76.2);const double area_b=6*25.4*25.4;
    const auto verify=[&](workspace::Workspace& state,command_host::Host& commands){
        const auto& document=state.open_assembly(top)->session.document();
        const auto physical=assembly::physical_values(document);
        const double l=document::length_unit_mm(document.document_units.at("Length")),m=document::mass_unit_kg(document.document_units.at("Mass"));
        near(physical.at("model.volume")*std::pow(l,3),2*(va+vb),"Mixed nested volume changed physical size");
        near(physical.at("model.area")*l*l,2*(area_a+area_b),"Mixed nested area scaled twice");
        require(physical.contains("model.mass"),"Updated nested Assembly lost current mass");
        near(physical.at("model.mass")*m,2*(ma+mb),"Mixed nested density or mass units are wrong");
        const auto scene=state.authoritative_viewer_mesh(top);require(!scene.vertices.empty(),"Mixed nested scene is empty");
        kernel::Vec3 low{1e100,1e100,1e100},high{-1e100,-1e100,-1e100};
        for(const auto& p:scene.vertices){low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);}
        near(low.x,-12.7,"Mixed Assembly X minimum");near(high.x,279.4,"Mixed Assembly X maximum");near(low.y,-25.4,"Mixed Assembly Y minimum");near(high.y,114.3,"Mixed Assembly Y maximum");near(high.z-low.z,model_height,"Mixed Assembly Z size");
        for(const auto& refs:{pair,Json::array({ref("object","",{},second_a),ref("object","",{},second_b)})}) {
            const auto result=run(commands,"measurement.evaluate",{{"document",top},{"references",refs}}).data;
            near(result["distance"]["value"]["value"],76.2,"Mixed nested occurrence gap changed");
            near(result["values"][0]["volume"]["value"],va,"Mixed mm Part volume changed");near(result["values"][1]["volume"]["value"],vb,"Mixed inch Part volume changed");
            if(!result["values"][0]["mass"].is_object()||!result["values"][1]["mass"].is_object())throw std::runtime_error("Mixed nested mass missing with "+std::to_string(state.documents().size())+" open documents: "+result.dump());
            near(result["values"][0]["mass"]["value"],ma,"Mixed mm Part mass changed");near(result["values"][1]["mass"]["value"],mb,"Mixed inch Part mass changed");
            require(result["units"]["length"]=="mm"&&result["units"]["mass"]=="kg"&&!result["body_calculated"].get<bool>(),"Measurement API lost canonical units or calculated bodies");
            require(!result["values"][0]["volume"]["approximate"].get<bool>(),"Mixed source lost exact volume");
        }
    };
    verify(live,host);
    const auto saved=run(host,"measurement.create",{{"name","Mixed gap"},{"references",pair}}).data;
    const auto record=saved.at("object").get<std::string>();
    const auto original_scene=live.authoritative_viewer_mesh(top);
    const auto source_shape=live.open_part(mm)->session.calculated_boundaries().back().kernel_shape;
    for(const auto* unit:{"in","m","cm","mm"}) {
        const auto before=live.open_part(mm)->session.document().serialized();
        const auto sub_revision=live.open_assembly(sub)->session.revision(),top_revision=live.open_assembly(top)->session.revision();
        settings(mm,unit,"t");const auto after=live.open_part(mm)->session.document().serialized();live.refresh_source_geometry();verify(live,host);
        require(live.open_part(mm)->session.calculated_boundaries().back().kernel_shape==source_shape,"Source unit change replaced calculated body");
        require(live.open_assembly(sub)->session.revision()==sub_revision&&live.open_assembly(top)->session.revision()==top_revision,"Source unit change created Assembly history");
        const auto scene=live.authoritative_viewer_mesh(top);require(scene.vertices==original_scene.vertices&&scene.triangles==original_scene.triangles,"Unit metadata changed scene coordinates");
        run(host,"activate",{{"document",mm}});run(host,"undo");require(live.open_part(mm)->session.document().serialized()==before,"Source unit Undo lost document");
        run(host,"redo");require(live.open_part(mm)->session.document().serialized()==after,"Source unit Redo lost document");run(host,"activate",{{"document",top}});live.refresh_source_geometry();verify(live,host);
    }
    for(const auto* unit:{"mm","cm","m","in"}) {
        settings(top,unit,"g");settings(sub,unit,"lb");live.refresh_source_geometry();verify(live,host);
        const auto stored=run(host,"measurement.get",{{"document",top},{"object",record}}).data;
        require(stored.at("values")==saved.at("values")&&stored.at("distance")==saved.at("distance")&&stored.at("references")==saved.at("references"),"Unit change rewrote saved measurement values or paths");
    }
    const auto original_va=va,original_ma=ma,original_area=area_a;
    const auto top_revision=live.open_assembly(top)->session.revision();
    const auto previous_parameters=live.open_assembly(top)->session.document().user_parameters;
    run(host,"activate",{{"document",mm}});
    const auto feature=live.open_part(mm)->session.document().history.back().id;
    test::resize_rectangular_commands(execute,{{"container",feature},{"height_mm",152.4}});
    va*=2;ma*=2;model_height=152.4;area_a=2*(25.4*50.8+25.4*152.4+50.8*152.4);
    run(host,"activate",{{"document",top}});live.refresh_source_geometry();verify(live,host);
    require(live.open_assembly(top)->session.revision()==top_revision,"Source geometry refresh created Assembly history");
    require(live.open_assembly(top)->session.document().user_parameters==previous_parameters,"Source geometry refresh evaluated Assembly relations");
    run(host,"activate",{{"document",mm}});run(host,"undo");va=original_va;ma=original_ma;area_a=original_area;model_height=76.2;
    run(host,"activate",{{"document",top}});live.refresh_source_geometry();verify(live,host);
    run(host,"activate",{{"document",mm}});
    run(host,"document.material.set",{{"properties",Json::array({{{"key","MASS_DENSITY"},{"value","5400"},{"unit","kg/m^3"}}})}});
    ma*=2;run(host,"activate",{{"document",top}});live.refresh_source_geometry();verify(live,host);
    run(host,"activate",{{"document",mm}});run(host,"undo");ma=original_ma;
    run(host,"activate",{{"document",top}});live.refresh_source_geometry();verify(live,host);
    run(host,"activate",{{"document",top}});run(host,"regenerate");verify(live,host);
    for(const auto& id:{mm,inch,sub,top}){run(host,"activate",{{"document",id}});run(host,"save");}
    // Reopen only the top document in a fresh Workspace: all sources must come
    // from native dependencies, not from the original open document cache.
    workspace::Workspace reopened;command_host::Host reopened_host(reopened,kernel,dir,options);
    run(reopened_host,"open",{{"path",document::path_to_utf8(dir/"mixed-top.asmz")}});verify(reopened,reopened_host);
    const auto restored=run(reopened_host,"measurement.get",{{"object",record}}).data;
    require(restored.at("values")==saved.at("values")&&restored.at("distance")==saved.at("distance")&&restored.at("references")==saved.at("references"),"Mixed Assembly native reopen lost measurement data");
    require(reopened.documents().size()==1,"Measuring a closed dependency opened a document");
    const auto before_missing=reopened.open_assembly(top)->session.document().serialized();
    fs::rename(dir/"mixed-sub.asmz",dir/"mixed-sub-unavailable.asmz");
    const auto missing=reopened_host.execute({{"command","measurement.evaluate"},{"arguments",{{"references",pair}}}});
    fs::rename(dir/"mixed-sub-unavailable.asmz",dir/"mixed-sub.asmz");
    require(!missing.ok&&missing.code=="missing_reference"&&reopened.open_assembly(top)->session.document().serialized()==before_missing,"Unavailable measurement source did not fail without mutation");
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
    require(fs::create_directory(dir),"Cannot create test directory");verify_mixed_units(kernel,dir);verify(kernel,dir);
    require(fs::canonical(dir).parent_path()==parent,"Unexpected cleanup path");fs::remove_all(dir);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
