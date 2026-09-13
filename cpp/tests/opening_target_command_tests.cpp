#include <zima/command_host/host.hpp>
#include <zima/workspace/opening_operations.hpp>
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
    options.settings=[] {command_host::Settings settings;settings.templates={fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return settings;};
    command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","opening-limits"}});
    run(host,"box.create",{{"length_mm","60"},{"width_mm","60"},{"height_mm","60"}});
    const auto bore=run(host,"construction.create",{{"kind","plane"},{"name","Bore end"},{"base_plane","xy"},{"offset_mm",-10}}).data;
    const auto thread=run(host,"construction.create",{{"kind","plane"},{"name","Thread end"},{"base_plane","xy"},{"offset_mm",-15}}).data;
    auto* state=live.open_part(live.active_document_id());
    const auto target=[](const Json& plane){return Json::array({Json{{"owner",plane.at("entity")},{"key","plane"}}});};
    const auto created=run(host,"opening.create",{{"type","metric"},{"designation","M10"},{"bore_end","up_to"},
        {"bore_targets",target(bore)},{"thread_end","up_to"},{"thread_targets",target(thread)},
        {"chamfer_enabled",false},{"drill_point_enabled",false},{"placement",{{"z",-30}}}}).data;
    const auto id=created.at("container").get<std::string>();const double r=8.376/2;
    const auto volume=[&](double depth){near(state->session.calculated_boundaries().back().volume,216000-std::numbers::pi*r*r*depth);};
    const auto sheet_end=[&](double end) {
        const auto& mesh=state->session.calculated_boundaries().back().mesh;double maximum=-1e20;
        for(std::size_t i=0;i<mesh.triangle_references.size();++i) {
            const auto& ref=mesh.triangle_references[i];if(ref.owner_id!=id||!ref.is_thread_surface())continue;
            for(std::size_t j=0;j<3;++j)maximum=std::max(maximum,mesh.vertices.at(mesh.triangles.at(i*3+j)).z);
        }
        near(maximum,end);
    };
    volume(20);sheet_end(-15);
    const auto unchanged_revision=state->session.revision();
    require(!run(host,"opening.set",{{"container",id},{"bore_targets",created.at("bore_targets")},
        {"thread_targets",created.at("thread_targets")}}).data.at("changed").get<bool>() &&
        state->session.revision()==unchanged_revision,"Reassigning identical end references added history");

    // Native calculation must resolve datums even when persisted display data is stale.
    auto stale=state->session.document();
    stale.find_container(id)->thread.end_targets_forward.front().fallback_origin.z=200;
    stale.find_container(id)->thread.length_end_targets.front().fallback_origin.z=300;
    const auto native_datum=kernel.evaluate_history(stale.kernel_operations());
    near(native_datum.back().volume,216000-std::numbers::pi*r*r*20);

    require(created.at("bore_targets")[0].at("owner")==bore.at("entity")&&created.at("thread_targets")[0].at("owner")==thread.at("entity"),"Opening target identities changed");
    run(host,"construction.set",{{"construction",bore.at("construction")},{"offset_mm",-5}});
    run(host,"regenerate");volume(25);sheet_end(-15);
    run(host,"construction.set",{{"construction",thread.at("construction")},{"offset_mm",-12}});
    run(host,"regenerate");volume(25);sheet_end(-12);
    const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    for(const auto& targets:std::vector<Json>{Json::array(),Json::array({Json{{"owner","missing"},{"key","plane"}}}),
            Json::array({Json{{"owner",id},{"key","self"}}}),Json::array({Json{{"owner",bore.at("entity")},{"key","plane"},{"instance_path","other"}}}),
            Json::array({Json{{"owner",bore.at("entity")},{"key","plane"},{"fallback_origin",{0,0,0}}}})}) {
        require(!host.execute({{"command","opening.set"},{"arguments",{{"container",id},{"bore_targets",targets}}}}).ok&&
            state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache,"Invalid opening target partly committed");
    }
    run(host,"save");std::vector<kernel::BodyResult> stored;
    const auto loaded=document::PartDocument::load(directory/"opening-limits.prtz",&stored);
    near(stored.back().volume,216000-std::numbers::pi*r*r*25);
    const auto cold=kernel.evaluate_history(loaded.kernel_operations());near(cold.back().volume,stored.back().volume);
    require(host.execute({{"command","history.suppress"},{"arguments",{{"object",bore.at("construction")},{"suppressed",true}}}}).code=="calculation_errors","Suppressing an opening target did not report dependent errors");
    require(state->session.calculated_boundaries().back().calculation_errors.contains(id),"Suppressed opening target left a silently valid stale bore");
    run(host,"history.suppress",{{"object",bore.at("construction")},{"suppressed",false}});
    require(!state->session.calculated_boundaries().back().calculation_errors.contains(id),"Restored opening target did not repair the feature");volume(25);
    require(host.execute({{"command","history.suppress"},{"arguments",{{"object",thread.at("construction")},{"suppressed",true}}}}).code=="calculation_errors","Suppressing an opening target did not report dependent errors");
    require(state->session.calculated_boundaries().back().calculation_errors.contains(id),"Missing thread end remained valid");
    run(host,"undo");volume(25);sheet_end(-12);
    run(host,"redo");require(state->session.calculated_boundaries().back().calculation_errors.contains(id),"Redo lost missing thread target error");
    run(host,"undo");

    run(host,"new",{{"type","part"},{"name","opening-faces"}});
    const auto box=run(host,"box.create",{{"length_mm","60"},{"width_mm","60"},{"height_mm","60"}}).data.at("container");
    const auto face=Json::array({Json{{"owner",box},{"key","z_max"}}});
    const auto face_opening=run(host,"opening.create",{{"type","metric"},{"designation","M10"},{"bore_end","up_to"},
        {"bore_targets",face},{"thread_end","up_to"},{"thread_targets",face},
        {"chamfer_enabled",false},{"drill_point_enabled",false},{"placement",{{"z",-30}}}}).data.at("container").get<std::string>();
    state=live.open_part(live.active_document_id());
    const auto face_sheet_end=[&](const auto& result,double z) {
        double maximum=-1e20;const auto& mesh=result.mesh;
        for(std::size_t i=0;i<mesh.triangle_references.size();++i) {
            const auto& ref=mesh.triangle_references[i];if(ref.owner_id!=face_opening||!ref.is_thread_surface())continue;
            for(std::size_t j=0;j<3;++j)maximum=std::max(maximum,mesh.vertices.at(mesh.triangles.at(i*3+j)).z);
        }
        near(maximum,z);
    };
    near(state->session.calculated_boundaries().back().volume,216000-std::numbers::pi*r*r*60);
    face_sheet_end(state->session.calculated_boundaries().back(),30);
    run(host,"box.set",{{"container",box},{"height_mm","80"}});
    near(state->session.calculated_boundaries().back().volume,288000-std::numbers::pi*r*r*70);
    face_sheet_end(state->session.calculated_boundaries().back(),40);
    auto cold_face=state->session.document();cold_face.find_container(box.get<std::string>())->box.height=100;
    const auto face_result=kernel.evaluate_history(cold_face.kernel_operations());
    near(face_result.back().volume,360000-std::numbers::pi*r*r*80);face_sheet_end(face_result.back(),50);
    // Keep the bore reference intact: a missing thread-only face must also fail.
    auto missing=state->session.document();
    missing.find_container(face_opening)->thread.length_end_targets.front().reference.semantic_key="missing-original-face";
    const auto missing_result=kernel.evaluate_history_recovering(missing.kernel_operations(false,true));
    require(missing_result.back().calculation_errors.contains(face_opening),"Missing original thread face silently reused its cached plane");
    auto missing_bore=state->session.document();
    missing_bore.find_container(face_opening)->thread.end_targets_forward.front().reference.semantic_key="missing-original-face";
    const auto missing_bore_result=kernel.evaluate_history_recovering(missing_bore.kernel_operations(false,true));
    require(missing_bore_result.back().calculation_errors.contains(face_opening),"Missing original bore face silently reused its cached plane");
    run(host,"save");std::vector<kernel::BodyResult> face_stored;
    const auto saved_faces=document::PartDocument::load(directory/"opening-faces.prtz",&face_stored);
    const auto reloaded_faces=kernel.evaluate_history(saved_faces.kernel_operations());
    near(reloaded_faces.back().volume,face_stored.back().volume);face_sheet_end(reloaded_faces.back(),40);
    run(host,"new",{{"type","part"},{"name","opening-body-target"}});
    const auto stock=run(host,"box.create",{{"length_mm","20"},{"width_mm","20"},{"height_mm","20"}}).data.at("container");
    state=live.open_part(live.active_document_id());
    const auto source_body=state->session.document().body_history.active_body_id();
    run(host,"placement.set",{{"object",source_body},{"values",{{"reference_offset:0",10}}}});
    const auto carrier=run(host,"body.create",{{"name","Opening carrier"}}).data.at("body");
    run(host,"placement.set",{{"object",carrier},{"values",{{"reference_offset:0",5}}}});
    run(host,"box.create",{{"length_mm","40"},{"width_mm","40"},{"height_mm","80"}});
    const auto other_face=Json::array({Json{{"owner",stock},{"key","z_max"}}});
    const auto other_opening=run(host,"opening.create",{{"type","metric"},{"designation","M10"},
        {"bore_end","up_to"},{"bore_targets",other_face},{"thread_end","up_to"},{"thread_targets",other_face},
        {"chamfer_enabled",false},{"drill_point_enabled",false},{"placement",{{"z",-40}}}}).data.at("container").get<std::string>();
    near(state->session.calculated_boundaries().back().volume,136000-std::numbers::pi*r*r*55);
    near(state->session.document().find_container(other_opening)->thread.end_targets_forward.front().fallback_origin.z,15);
    run(host,"body.activate",{{"body",source_body}});run(host,"box.set",{{"container",stock},{"height_mm","22"}});
    near(state->session.calculated_boundaries().back().volume,136800-std::numbers::pi*r*r*56);
    near(state->session.document().find_container(other_opening)->thread.length_end_targets.front().fallback_origin.z,16);
    run(host,"undo");near(state->session.calculated_boundaries().back().volume,136000-std::numbers::pi*r*r*55);
    run(host,"redo");near(state->session.calculated_boundaries().back().volume,136800-std::numbers::pi*r*r*56);
    run(host,"save");std::vector<kernel::BodyResult> body_cache;
    auto body_loaded=document::PartDocument::load(directory/"opening-body-target.prtz",&body_cache);
    kernel::OcctKernel cold_kernel;
    auto* saved_opening=body_loaded.find_container(other_opening);
    saved_opening->thread.profile_diameter+=0.1;
    const auto incremental=cold_kernel.evaluate_history_incremental(body_loaded.kernel_operations(),body_cache);
    near(incremental.back().volume,136800-std::numbers::pi*std::pow(r+0.05,2)*56);


}
}
int main(){try {
    kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());
    const auto directory=root/("zima-opening-targets-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create test directory");verify(kernel,directory);
    require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);
    std::cout<<"Opening targets: independent limits, live original planes, atomic errors, persistence and missing-reference recovery passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
