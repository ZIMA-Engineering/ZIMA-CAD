#include "profile_command_fixture.hpp"
#include "profile_solid_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/shaft_thread_operations.hpp>
#include <zima/kernel/shaft_thread_geometry.hpp>
#include <cmath>
#include <numbers>
#include <iostream>
#include <algorithm>

using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){if(std::abs(actual-expected)>1e-5)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
commands::Result run(command_host::Host& host,const std::string& command,Json args=Json::object()) {
    auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(command+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings settings;settings.templates={fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"};return settings;};
    command_host::Host host(live,kernel,directory,options);
    require(host.execute_text("shaft_thread.get missing").code=="unsupported_document","Shaft query without Part did not fail cleanly");
    run(host,"new",{{"type","part"},{"name","shaft-command"}});
    const auto cylinder=zima::test::circular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"radius_mm","5"},{"height_mm","30"}}).data.at("container").get<std::string>();
    const auto face=[&](const char* key){return Json{{"owner",cylinder},{"key",test::profile_key(live.open_part(live.active_document_id())->session.document(),cylinder,key)}};};
    auto* state=live.open_part(live.active_document_id());const double volume=750*std::numbers::pi;
    const auto created=run(host,"shaft_thread.create",{{"cylinder",face("side")},{"start",face("z_min")},{"designation","M10"},{"length_mm",15}}).data;
    const auto id=created.at("container").get<std::string>();const auto original=*state->session.document().find_container(id);
    const auto geometry=[&](double start,double end,double radius,double runout_end) {
        const auto& mesh=state->session.calculated_boundaries().back().mesh;near(state->session.calculated_boundaries().back().volume,volume);
        double low=1e20,high=-1e20,runout_high=-1e20;std::size_t count=0;
        for(std::size_t i=0;i<mesh.triangle_references.size();++i) {
            const auto& ref=mesh.triangle_references[i];if(ref.owner_id!=id)continue;
            for(std::size_t j=0;j<3;++j) {
                const auto p=mesh.vertices.at(mesh.triangles.at(i*3+j));
                if(ref.semantic_key=="thread:surface:root") {near(std::hypot(p.x,p.y),radius);low=std::min(low,p.z);high=std::max(high,p.z);++count;}
                if(ref.semantic_key=="thread:surface:runout:end")runout_high=std::max(runout_high,p.z);
            }
        }
        require(count>0,"Missing shaft thread sheet");near(low,start);near(high,end);
        if(runout_end<0)require(runout_high==-1e20,"Disabled runout remains visible");else near(runout_high,runout_end);
    };
    geometry(0,15,4.08,18);
    run(host,"undo");require(state->session.document().history.size()==1,"Shaft creation did not undo atomically");run(host,"redo");
    require(*state->session.document().find_container(id)==original,"Shaft Redo changed reference identities");
    const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    const auto queried=run(host,"shaft_thread.get",{{"container",id}}).data;
    require(queried.at("start").at("owner")==cylinder&&state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache,"Shaft query changed the model");
    require(!run(host,"shaft_thread.set",{{"container",id},{"length_mm",15}}).data.at("changed").get<bool>()&&state->session.calculated_boundaries().data()==cache,"Unchanged shaft thread recalculated");
    for(const auto& patch:std::vector<Json>{{{"root_diameter_mm",-1}},{{"length_mm",31}},{{"cylinder",face("z_min")}},{{"start",face("side")}},
            {{"chamfer",face("z_max")}},{{"end_condition","up_to"}},{{"end_condition","unknown"}},{{"designation","missing"}},
            {{"cylinder",Json{{"owner",cylinder},{"key","side"},{"instance_path","other"}}}}}) {
        auto args=patch;args["container"]=id;
        require(!host.execute({{"command","shaft_thread.set"},{"arguments",args}}).ok&&state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache,"Invalid shaft edit partly committed");
    }
    run(host,"shaft_thread.set",{{"container",id},{"length_mm",20},{"root_diameter_mm",8.05},{"runout_pitch_factor",1}});geometry(0,20,4.025,21.5);
    run(host,"shaft_thread.set",{{"container",id},{"runout_enabled",false}});geometry(0,20,4.025,-1);
    run(host,"shaft_thread.set",{{"container",id},{"start",face("z_max")}});geometry(10,30,4.025,-1);
    run(host,"shaft_thread.set",{{"container",id},{"end_condition","up_to"},{"end",face("z_min")}});geometry(0,30,4.025,-1);
    run(host,"shaft_thread.set",{{"container",id},{"end",face("side")}});geometry(0,30,4.025,-1);
    run(host,"shaft_thread.set",{{"container",id},{"end_condition","through_all"},{"end",Json::object()}});geometry(0,30,4.025,-1);
    const auto valid_revision=state->session.revision();
    require(!host.execute({{"command","shaft_thread.set"},{"arguments",{{"container",id},{"runout_enabled",true}}}}).ok&&state->session.revision()==valid_revision,"Through-all shaft accepted a runout");
    run(host,"shaft_thread.set",{{"container",id},{"standard","whitworth"},{"designation","W 1/2"}});geometry(0,30,9.988/2,-1);
    run(host,"shaft_thread.set",{{"container",id},{"standard","pipe"},{"designation","G 1/2"}});geometry(0,30,18.631/2,-1);
    run(host,"shaft_thread.set",{{"container",id},{"standard","metric"},{"designation","M12"}});geometry(0,30,9.853/2,-1);
    auto locked=*state->session.document().find_container(id);locked.value_locks.insert("root_diameter");
    static_cast<void>(workspace::commit_shaft_thread(live,kernel,live.active_document_id(),locked,workspace::ShaftThreadEditMode::Replace));
    require(host.execute({{"command","shaft_thread.set"},{"arguments",{{"container",id},{"designation","M10"}}}}).code=="value_locked","Shaft catalog selection bypassed root diameter lock");
    auto locked_edit=locked;locked_edit.shaft_thread.root_diameter=8;
    bool rejected=false;
    try {static_cast<void>(workspace::commit_shaft_thread(live,kernel,live.active_document_id(),locked_edit,workspace::ShaftThreadEditMode::Replace));}
    catch(const workspace::ShaftThreadOperationError& error){rejected=error.code=="value_locked";}
    require(rejected&&*state->session.document().find_container(id)==locked,"Shared Properties commit bypassed the shaft diameter lock");
    const auto later=zima::test::circular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"radius_mm","2"},{"height_mm","5"}}).data.at("container");
    const auto later_revision=state->session.revision();
    require(host.execute({{"command","shaft_thread.set"},{"arguments",{{"container",id},{"start",Json{{"owner",later},{"key","z_max"}}}}}}).code=="missing_reference"&&state->session.revision()==later_revision,
        "Shaft edit accepted a face after its history boundary");
    run(host,"undo");
    run(host,"save");std::vector<kernel::BodyResult> saved;
    const auto loaded=document::PartDocument::load(directory/"shaft-command.prtz",&saved);
    require(*loaded.find_container(id)==locked,"Shaft thread native file lost parameters or reference identities");near(saved.back().volume,volume);
    near(kernel.evaluate_history(loaded.kernel_operations()).back().volume,volume);
}
void verify_chamfer(const kernel::OcctKernel& kernel,fs::path directory) {
    auto doc=document::PartDocument::create_default();auto shaft=zima::test::circular_feature(doc,5,30);

    auto chamfer=document::PartDocument::create_chamfer_container({{shaft.id,test::profile_key(doc,shaft,"circle:z_min"),{}},{shaft.id,test::profile_key(doc,shaft,"circle:z_max"),{}}});
    chamfer.edge_treatment.primary_size=1;
    for(const auto& feature:{shaft,chamfer}){doc.history.push_back(feature);doc.insert_history_entry(document::PartHistoryKind::Feature,feature.id);}
    const auto calculated=kernel.evaluate_history(doc.kernel_operations());
    Json beginning,ending;
    for(const auto& ref:calculated.back().mesh.original_references.triangle_references) {
        if(ref.owner_id!=chamfer.id||!ref.surface||ref.surface->kind!=kernel::SurfaceGeometry::Kind::Cone)continue;
        auto& target=ref.surface->origin.z<15?beginning:ending;target={{"owner",ref.owner_id},{"key",ref.semantic_key}};
    }
    require(!beginning.is_null()&&!ending.is_null(),"Chamfer fixture did not expose original cones");
    const auto file=directory/"chamfered-shaft.prtz";doc.save(file,calculated);
    workspace::Workspace live;command_host::Host host(live,kernel,directory);
    run(host,"open",{{"path",file.string()}});
    const auto face=[&](const char* key){return Json{{"owner",shaft.id},{"key",test::profile_key(doc,shaft,key)}};};
    const auto id=run(host,"shaft_thread.create",{{"cylinder",face("side")},{"start",face("z_min")},
        {"chamfer",beginning},{"end_condition","up_to"},{"end",ending},{"designation","M10"}}).data.at("container").get<std::string>();
    const auto* state=live.open_part(live.active_document_id());const auto& mesh=state->session.calculated_boundaries().back().mesh;
    near(state->session.calculated_boundaries().back().volume,calculated.back().volume);
    double low=1e20,high=-1e20;
    for(std::size_t i=0;i<mesh.triangle_references.size();++i) {
        const auto& ref=mesh.triangle_references[i];if(ref.owner_id!=id||ref.semantic_key!="thread:surface:root")continue;
        for(std::size_t j=0;j<3;++j){const auto p=mesh.vertices.at(mesh.triangles.at(i*3+j));near(std::hypot(p.x,p.y),4.08);low=std::min(low,p.z);high=std::max(high,p.z);}
    }
    near(low,.08);near(high,29.92);
    const auto revision=state->session.revision();
    require(!host.execute({{"command","shaft_thread.set"},{"arguments",{{"container",id},{"end",beginning}}}}).ok&&state->session.revision()==revision,
        "Shaft accepted the entrance chamfer as its end");
    run(host,"shaft_thread.set",{{"container",id},{"chamfer",Json::object()}});
    require(!state->session.document().find_container(id)->shaft_thread.chamfer,"Clearing the shaft entrance chamfer failed");
}
}
int main(){try {
    kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());const auto directory=root/("zima-shaft-commands-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create test directory");verify(kernel,directory);verify_chamfer(kernel,directory);
    require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);
    std::cout<<"Shaft thread commands: unchanged solid, exact sheet radii/extents, references, catalog, errors, locks, Undo and native round trip passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
