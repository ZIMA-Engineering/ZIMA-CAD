#include <zima/command_host/host.hpp>
#include <zima/workspace/opening_operations.hpp>
#include <cmath>
#include <numbers>
#include <iostream>
#include <algorithm>
#include <set>

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
    require(host.execute_text("opening.get missing").code=="unsupported_document","Opening query without Part did not fail cleanly");
    for(const auto* type:{"plain","metric","whitworth","pipe"}) {
        run(host,"new",{{"type","part"},{"name",std::string("opening-")+type}});
        run(host,"box.create",{{"length_mm","60"},{"width_mm","60"},{"height_mm","60"}});
        auto* state=live.open_part(live.active_document_id());
        Json args={{"type",type},{"bore_length_mm",20},{"thread_length_mm",10},{"chamfer_enabled",false},
            {"drill_point_enabled",false},{"placement",{{"z",-30}}}};
        const bool threaded=std::string(type)!="plain";
        if(threaded)args["designation"]=std::string(type)=="metric"?"M10":std::string(type)=="whitworth"?"W 1/2":"G 1/2";
        else args["nominal_diameter_mm"]=10;
        const auto created=run(host,"opening.create",args).data;const auto id=created.at("container").get<std::string>();
        const auto original=*state->session.document().find_container(id);
        run(host,"undo");require(state->session.document().history.size()==1,"Opening creation did not undo in one step");
        run(host,"redo");require(*state->session.document().find_container(id)==original,"Opening creation Redo replaced its source identities");
        require(original.feature_kind==document::FeatureKind::Thread && original.thread.enabled==threaded,"Wrong native opening kind or enabled state");
        const double r=(threaded?original.thread.profile_diameter:original.thread.nominal_diameter)/2;
        const double plain_volume=216000-std::numbers::pi*r*r*20;
        near(state->session.calculated_boundaries().back().volume,plain_volume);
        const auto rev=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
        const auto queried=run(host,"opening.get",{{"container",id}}).data;
        require(queried.at("feature")==original.feature_id&&state->session.revision()==rev&&state->session.calculated_boundaries().data()==cache,"Opening query recalculated or changed identity");
        require(!run(host,"opening.set",{{"container",id},{"bore_length_mm",20}}).data.at("changed").get<bool>() && state->session.calculated_boundaries().data()==cache,"Unchanged properties recalculated");
        for(const auto& patch:std::vector<Json>{{{"bore_length_mm",-1}},{{"chamfer_angle_degrees",180}},{{"runout_pitch_factor",-1}},
                {{"bore_diameter_mm",11}},{{"type","unknown"}},{{"bore_end","up_to"}}}) {
            auto input=patch;input["container"]=id;
            require(!host.execute({{"command","opening.set"},{"arguments",input}}).ok&&state->session.revision()==rev&&state->session.calculated_boundaries().data()==cache,"Invalid opening edit changed model");
        }
        if(threaded) {
            require(!host.execute({{"command","opening.set"},{"arguments",{{"container",id},{"bore_length_mm",10}}}}).ok,"Thread runout extended beyond blind bore");
            require(!host.execute({{"command","opening.set"},{"arguments",{{"container",id},{"designation","not-a-thread"}}}}).ok,"Unknown thread designation accepted");
            require(std::ranges::any_of(state->session.calculated_boundaries().back().mesh.triangle_references,[](const auto& ref){return ref.is_thread_surface();}),"Thread sheet is missing");
        }
        for(const double angle:{60.0,90.0,120.0}) {
            run(host,"opening.set",{{"container",id},{"chamfer_enabled",true},{"chamfer_depth_mm",2},{"chamfer_angle_degrees",angle},
                {"drill_point_enabled",true},{"drill_point_angle_degrees",118}});
            const double t=std::tan(angle*std::numbers::pi/360),tip=r/std::tan(118*std::numbers::pi/360);
            near(state->session.calculated_boundaries().back().volume,plain_volume-std::numbers::pi*(r*4*t+8*t*t/3+r*r*tip/3));
            run(host,"undo");require(*state->session.document().find_container(id)==original,"Undo did not restore complete opening parameters");
        }
        {
            auto placed=original;placed.placement={};
            document::ConstructionReference plane;plane.owner_id=state->session.document().document_id+":origin";
            plane.semantic_key="origin:plane:xy";plane.supports_offset=true;
            placed.placement.references={plane};
            static_cast<void>(workspace::commit_opening(live,kernel,live.active_document_id(),placed,workspace::OpeningEditMode::Replace));
            const auto& normalized=*state->session.document().find_container(id);
            require(normalized.placement.references.front().orientation_role=="front"&&normalized.placement.reference_valid,"Opening did not use the GUI FRONT normalization");
            near(state->session.calculated_boundaries().back().volume,plain_volume);
            run(host,"undo");require(*state->session.document().find_container(id)==original,"Referenced opening Undo changed original placement");
        }
        run(host,"opening.set",{{"container",id},{"direction","reverse"},{"placement",{{"z",30}}}});
        near(state->session.calculated_boundaries().back().volume,plain_volume);
        run(host,"undo");
        run(host,"opening.set",{{"container",id},{"bore_end","through_all"}});
        near(state->session.calculated_boundaries().back().volume,216000-std::numbers::pi*r*r*60);
        run(host,"undo");
        if(threaded) {
            run(host,"opening.set",{{"container",id},{"custom_bore_diameter",true},{"bore_diameter_mm",2*r+.1}});
            near(state->session.calculated_boundaries().back().volume,216000-std::numbers::pi*std::pow(r+.05,2)*20);
            run(host,"opening.set",{{"container",id},{"custom_bore_diameter",false}});
            near(state->session.calculated_boundaries().back().volume,plain_volume);
        }
        auto locked=*state->session.document().find_container(id);locked.value_locks.insert("bore_length");
        static_cast<void>(workspace::commit_opening(live,kernel,live.active_document_id(),locked,workspace::OpeningEditMode::Replace));
        require(host.execute({{"command","opening.set"},{"arguments",{{"container",id},{"bore_length_mm",21}}}}).code=="value_locked","Opening dimension lock bypassed");
        if(threaded) {
            run(host,"opening.set",{{"container",id},{"left_handed",true}});
            near(state->session.calculated_boundaries().back().volume,plain_volume);
            require(run(host,"opening.get",{{"container",id}}).data.at("left_handed").get<bool>(),"Opening handedness was not stored");
            run(host,"undo");
            auto diameter_locked=locked;diameter_locked.value_locks.insert("nominal_diameter");
            static_cast<void>(workspace::commit_opening(live,kernel,live.active_document_id(),diameter_locked,workspace::OpeningEditMode::Replace));
            const auto& catalog=document::thread_catalog(type);
            require(host.execute({{"command","opening.set"},{"arguments",{{"container",id},{"designation",catalog.front().designation}}}}).code=="value_locked","Catalog selection bypassed the nominal diameter lock");
            run(host,"undo");
        }
        run(host,"save");std::vector<kernel::BodyResult> saved;
        const auto loaded=document::PartDocument::load(directory/(std::string("opening-")+type+".prtz"),&saved);
        require(*loaded.find_container(id)==locked,"Native file lost opening properties or source identities");
        near(saved.back().volume,plain_volume);const auto cold=kernel.evaluate_history(loaded.kernel_operations());near(cold.back().volume,plain_volume);
        auto invalid=locked;invalid.hole.circle_id="replaced-source";
        bool rejected=false;try{static_cast<void>(workspace::commit_opening(live,kernel,live.active_document_id(),invalid,workspace::OpeningEditMode::Replace));}
        catch(const workspace::OpeningOperationError& error){rejected=std::string(error.code)=="identity_changed";}
        require(rejected,"Opening source circle identity was replaceable");
    }
}
}
int main(){try {
    kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());
    const auto directory=root/("zima-openings-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create test directory");verify(kernel,directory);
    require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);
    std::cout<<"Opening commands: catalog sizes, independent bore/chamfer/tip volumes, direction, through all, locks, atomic errors, identities and native round trip passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
