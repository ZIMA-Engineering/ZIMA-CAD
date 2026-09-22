#include "drill_point_test_support.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/drill_point_operations.hpp>
#include <zima/kernel/drill_point_identity.hpp>
#include <iostream>
#include <set>
using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b){if(std::abs(a-b)>1e-5)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings value;value.templates={fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"};return value;};
    command_host::Host host(live,kernel,directory,options);
    require(host.execute_text("drill_point.get missing").code=="unsupported_document","Drill query without Part failed incorrectly");
    run(host,"new",{{"type","part"},{"name","drill-command"}});
    auto* state=live.open_part(live.active_document_id());auto fixture=test::drill_point_fixture(state->session.document());
    const auto large=fixture.history[1].id,small=fixture.history[2].id,block=fixture.history[0].id;
    const auto face=[](const std::string& owner,const char* key="z_min"){return Json{{"owner",owner},{"key",key}};};
    const Json a=face(large),b=face(small);auto calculated=kernel.evaluate_history(fixture.kernel_operations());
    state->session.commit(std::move(fixture),std::move(calculated));
    const auto created=run(host,"drill_point.create",{{"faces",Json::array({a,b})}}).data;
    const auto id=created.at("container").get<std::string>();const auto original=*state->session.document().find_container(id);
    const auto geometry=[&](double angle,bool big,bool little) {
        const auto& result=state->session.calculated_boundaries().back();near(result.volume,test::drilled_block_volume(angle,big,little));
        std::set<std::string> sources;
        for(std::size_t i=0;i<result.mesh.triangle_references.size();++i) {
            const auto& ref=result.mesh.triangle_references[i];if(ref.owner_id!=id)continue;
            const auto parent=kernel::drill_point_source(ref.semantic_key);require(parent.has_value(),"Drill cone has no recoverable original parent");
            if(!ref.semantic_key.starts_with("drill-point:side:from:"))continue;
            const bool is_large=parent->owner_id==large;require(is_large||parent->owner_id==small,"Drill cone changed its source owner");
            require(parent->semantic_key=="z_min","Drill cone changed its parent face");sources.insert(parent->owner_id);
            for(std::size_t j=0;j<3;++j) {
                const auto p=result.mesh.vertices.at(result.mesh.triangles.at(i*3+j));const double radius=is_large?5:3;
                const double radial=std::hypot(p.x-(is_large?-8:8),p.y);
                near(radial,radius+p.z*std::tan(angle*std::numbers::pi/360));
            }
        }
        require(sources.contains(large)==big&&sources.contains(small)==little,"Drill cone references moved to a different selected bottom");
    };
    geometry(118,true,true);run(host,"undo");require(!state->session.document().find_container(id),"Drill creation did not undo in one step");run(host,"redo");
    require(*state->session.document().find_container(id)==original,"Drill Redo changed original identities");
    const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    run(host,"drill_point.get",{{"container",id}});
    require(!run(host,"drill_point.set",{{"container",id},{"angle_degrees",118}}).data.at("changed").get<bool>()&&state->session.revision()==revision&&cache==state->session.calculated_boundaries().data(),"Drill query/no-op recalculated");
    for(const auto& patch:std::vector<Json>{{{"angle_degrees",0}},{{"angle_degrees",180}},{{"faces",Json::array({a,a})}},
        {{"faces",Json::array({a,face(block)})}},{{"faces",Json::array({a,face(small,"side")})}},{{"faces",Json::array({face("missing")})}},
        {{"faces",Json::array({Json{{"owner",large},{"key","z_min"},{"instance_path","other"}}})}}}) {
        auto args=patch;args["container"]=id;
        require(!host.execute({{"command","drill_point.set"},{"arguments",args}}).ok&&state->session.revision()==revision&&cache==state->session.calculated_boundaries().data(),"Invalid drill edit partly committed");
    }
    run(host,"drill_point.set",{{"container",id},{"angle_degrees",120}});geometry(120,true,true);
    run(host,"drill_point.set",{{"container",id},{"faces",Json::array({b,a})}});geometry(120,true,true);
    run(host,"drill_point.set",{{"container",id},{"faces",Json::array({b})}});geometry(120,false,true);
    run(host,"undo");geometry(120,true,true);run(host,"redo");geometry(120,false,true);
    run(host,"drill_point.set",{{"container",id},{"faces",Json::array()}});geometry(120,false,false);
    run(host,"undo");geometry(120,false,true);
    auto locked=*state->session.document().find_container(id);locked.value_locks.insert("angle");
    static_cast<void>(workspace::commit_drill_point(live,kernel,live.active_document_id(),locked,workspace::DrillPointEditMode::Replace));
    require(host.execute({{"command","drill_point.set"},{"arguments",{{"container",id},{"angle_degrees",118}}}}).code=="value_locked","Drill angle bypassed its Properties lock");
    run(host,"save");std::vector<kernel::BodyResult> saved;
    const auto loaded=document::PartDocument::load(directory/"drill-command.prtz",&saved);
    require(*loaded.find_container(id)==locked,"Drill native save lost its parameters/references");near(saved.back().volume,test::drilled_block_volume(120,false,true));
    const auto cold=kernel.evaluate_history(loaded.kernel_operations());near(cold.back().volume,saved.back().volume);
    const auto expected=kernel::drill_point_key("side",{small,"z_min",{}});
    require(std::ranges::any_of(cold.back().mesh.triangle_references,[&](const auto& ref){return ref.owner_id==id&&ref.semantic_key==expected;}),"Cold calculation lost remaining bottom identity");
    for(const auto* malformed:{"drill-point:side:from:0::x","drill-point:side:from:999:x:y","drill-point:unknown:from:1:a:b","drill-point:side:from:x:a:b"})
        require(!kernel::drill_point_source(malformed),"Malformed drill ancestry was accepted");
    // The current Otvor command generates its bore inside a Thread operation.
    // Its original bottom must work just like a directly subtracted cylinder.
    run(host,"new",{{"type","part"},{"name","opening-bottom"}});
    run(host,"box.create",{{"length_mm","40"},{"width_mm","40"},{"height_mm","40"}});
    const auto opening=run(host,"opening.create",{{"type","plain"},{"nominal_diameter_mm",10},{"bore_length_mm",15},
        {"drill_point_enabled",false},{"chamfer_enabled",false},{"placement",{{"z",-20}}}}).data.at("container").get<std::string>();
    state=live.open_part(live.active_document_id());std::optional<kernel::FaceReference> opening_bottom;
    for(const auto& ref:state->session.calculated_boundaries().back().mesh.original_references.triangle_references)
        if(ref.owner_id==opening&&ref.surface&&ref.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&std::abs(ref.surface->origin.z+5)<1e-7)opening_bottom=ref;
    require(opening_bottom.has_value(),"Current Otvor did not expose its original blind bottom");
    const auto point=run(host,"drill_point.create",{{"faces",Json::array({face(opening,opening_bottom->semantic_key.c_str())})},{"angle_degrees",120}}).data.at("container").get<std::string>();
    near(state->session.calculated_boundaries().back().volume,64000-375*std::numbers::pi-125*std::numbers::pi/(3*std::tan(std::numbers::pi/3)));
    require(std::ranges::any_of(state->session.calculated_boundaries().back().mesh.triangle_references,
        [&](const auto& ref){return ref.owner_id==point&&kernel::drill_point_source(ref.semantic_key)==opening_bottom;}),
        "Drill point on the current Otvor lost its original bottom parent");
}
}
int main(){try {
    kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());const auto directory=root/("zima-drill-commands-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create test directory");verify(kernel,directory);
    require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);
    std::cout<<"Drill commands: independent volumes/cones, stable bottom ancestry, reference edits, locks, Undo and native round trip passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
