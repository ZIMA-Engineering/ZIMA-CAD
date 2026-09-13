#include <zima/workspace/hole_operations.hpp>
#include <zima/command_host/host.hpp>
#include <cmath>
#include <iostream>
#include <numbers>
using namespace zima;
namespace fs=std::filesystem;
using commands::Json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){if(std::abs(actual-expected)>1e-5)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
commands::Result run(command_host::Host& host,const std::string& command,Json args=Json::object()) {
    auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(command+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);
    require(host.execute_text("hole.get missing").code=="unsupported_document","Query without Part did not fail cleanly");
    run(host,"new",{{"type","part"},{"name","native-hole"}});
    run(host,"box.create",{{"length_mm","40"},{"width_mm","40"},{"height_mm","40"}});
    const auto document_id=live.active_document_id();
    auto* state=live.open_part(document_id);
    auto created=run(host,"hole.create",{{"diameter_mm",10},{"bore_length_mm",10},{"placement",{{"z",-20}}}}).data;
    const auto id=created.at("container").get<std::string>();
    const auto original=*state->session.document().find_container(id);
    require(original.feature_kind==document::FeatureKind::Hole,"Wrong native Hole kind");
    const double cylinder=std::numbers::pi*25*10;
    near(state->session.calculated_boundaries().back().volume,64000-cylinder);
    run(host,"undo");require(state->session.document().history.size()==1,"Creation did not undo in one step");
    run(host,"redo");require(*state->session.document().find_container(id)==original,"Redo lost Hole profiles");
    const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    const auto queried=run(host,"hole.get",{{"container",id}}).data;
    require(queried.at("feature")==original.feature_id&&state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache,"Query changed geometry");
    require(!run(host,"hole.set",{{"container",id},{"diameter_mm",10}}).data.at("changed").get<bool>(),"Unchanged diameter recalculated");
    require(state->session.calculated_boundaries().data()==cache,"No-op replaced cached geometry");
    for(const auto& patch:std::vector<Json>{{{"diameter_mm",0}},{{"bore_length_mm",-1}},{{"entrance_chamfer_mm",-1}},
        {{"drill_point_angle_degrees",180}},{{"type","unknown"}},{{"bore_end","up_to"}},{{"thread_end","up_to"}},
        {{"thread_enabled",true},{"thread_length_mm",11}},{{"bore_end","through_all"},{"drill_point_enabled",true}},{{"placement",{{"unknown",2}}}}}) {
        auto args=patch;args["container"]=id;
        require(!host.execute({{"command","hole.set"},{"arguments",args}}).ok,"Invalid Hole accepted");
        require(state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache&&
            *state->session.document().find_container(id)==original,"Rejected Hole partially changed model");
    }
    run(host,"hole.set",{{"container",id},{"diameter_mm",8},{"bore_length_mm",12}});
    const auto modified=*state->session.document().find_container(id);
    near(state->session.calculated_boundaries().back().volume,64000-std::numbers::pi*16*12);
    const auto profile=sketcher::Sketch::from_serialized(modified.hole.sketch_serialized);
    require(profile.id==original.hole.sketch_id&&profile.owner_container_id==id&&profile.circles.front().id==original.hole.circle_id,"Diameter edit replaced profile identities");
    near(profile.circles.front().radius,4);
    run(host,"undo");require(*state->session.document().find_container(id)==original,"Undo lost source Hole");
    for(const double angle:{60.0,118.0,150.0}) {
        run(host,"hole.set",{{"container",id},{"entrance_chamfer_mm",1},{"drill_point_enabled",true},{"drill_point_angle_degrees",angle}});
        const double depth=5/std::tan(angle*std::numbers::pi/360);
        near(state->session.calculated_boundaries().back().volume,64000-cylinder-std::numbers::pi*(5+1.0/3+25*depth/3));
        const auto& h=state->session.document().find_container(id)->hole;
        require(h.chamfer_point_ids==original.hole.chamfer_point_ids&&h.tip_point_ids==original.hole.tip_point_ids&&h.tip_axis_segment_id==original.hole.tip_axis_segment_id,"Tip edit replaced persisted identities");
        run(host,"undo");
    }
    run(host,"hole.set",{{"container",id},{"exit_chamfer_enabled",true},{"exit_chamfer_mm",1}});
    near(state->session.calculated_boundaries().back().volume,64000-cylinder-std::numbers::pi*(5+1.0/3));
    run(host,"undo");
    run(host,"hole.set",{{"container",id},{"bore_end","through_all"}});
    near(state->session.calculated_boundaries().back().volume,64000-std::numbers::pi*25*40);
    run(host,"undo");
    for(const auto* type:{"metric","pipe","whitworth"}) {
        run(host,"hole.set",{{"container",id},{"type",type},{"thread_length_mm",8},{"thread_diameter_mm",12},{"thread_pitch_mm",1.75},{"left_handed",true}});
        near(state->session.calculated_boundaries().back().volume,64000-cylinder);
        const auto& feature=*state->session.document().find_container(id);
        require(feature.hole.thread_enabled&&feature.hole.left_hand_thread&&state->session.document().hole_thread_edges(feature).size()==4,"Cosmetic Hole thread is missing");
        run(host,"hole.set",{{"container",id},{"thread_end","through_all"}});
        near(state->session.document().find_container(id)->hole.thread_length,10);
        run(host,"undo");run(host,"undo");
    }
    const auto body=state->session.document().body_history.active_body_id();
    require(host.execute({{"command","hole.get"},{"arguments",{{"container",state->session.document().history.front().id}}}}).code=="wrong_feature","Wrong feature accepted by query");
    for(const auto& entry:std::vector<std::pair<std::string,std::string>>{{"diameter","diameter_mm"},{"bore_length","bore_length_mm"},
        {"entrance_chamfer","entrance_chamfer_mm"},{"exit_chamfer","exit_chamfer_mm"},{"drill_point_angle","drill_point_angle_degrees"},
        {"thread_diameter","thread_diameter_mm"},{"pitch","thread_pitch_mm"},{"thread_length","thread_length_mm"}}) {
        run(host,"value_lock.set",{{"object",id},{"key",entry.first},{"locked",true}});
        const auto old=run(host,"hole.get",{{"container",id}}).data;
        const auto rev=state->session.revision();
        require(host.execute({{"command","hole.set"},{"arguments",{{"container",id},{entry.second,old.at(entry.second).get<double>()+.01}}}}).code=="value_locked","A Hole dimension lock was bypassed");
        require(state->session.revision()==rev,"Locked edit committed");run(host,"undo");
    }
    {
        auto placed=original;placed.placement={};
        document::ConstructionReference plane;plane.owner_id=document_id+":origin";plane.semantic_key="origin:plane:xy";plane.supports_offset=true;
        placed.placement.references={plane};placed.hole.entrance_chamfer=1;placed.hole.drill_point_enabled=true;
        static_cast<void>(workspace::commit_hole(live,kernel,document_id,placed,workspace::HoleEditMode::Replace));
        require(state->session.document().find_container(id)->placement.references.front().orientation_role=="front","Hole did not consume the common FRONT contract");
        near(state->session.calculated_boundaries().back().volume,64000-cylinder-std::numbers::pi*(5+1.0/3+125/std::tan(118*std::numbers::pi/360)/3));
        run(host,"undo");
        auto invalid=original;invalid.hole.circle_id="replaced-source";
        bool rejected=false;try{static_cast<void>(workspace::commit_hole(live,kernel,document_id,invalid,workspace::HoleEditMode::Replace));}catch(const workspace::HoleOperationError&){rejected=true;}
        require(rejected&&*state->session.document().find_container(id)==original,"Invalid owned profile committed");
    }
    run(host,"body.create",{{"name","Other"}});
    require(host.execute({{"command","hole.set"},{"arguments",{{"container",id},{"diameter_mm",9}}}}).code=="inactive_body","Inactive Body was editable");
    run(host,"undo");run(host,"body.activate",{{"body",body}});
    run(host,"save");std::vector<kernel::BodyResult> saved;
    const auto loaded=document::PartDocument::load(directory/"native-hole.prtz",&saved);
    require(*loaded.find_container(id)==original,"Native round trip lost profiles or parameters");
    near(saved.back().volume,64000-cylinder);
    near(kernel.evaluate_history(loaded.kernel_operations()).back().volume,64000-cylinder);
}
}
int main(){try {
    kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());
    const auto directory=root/("zima-native-hole-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create test directory");verify(kernel,directory);
    require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);
    std::cout<<"Native Hole CLI geometry, profiles, history and persistence passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
