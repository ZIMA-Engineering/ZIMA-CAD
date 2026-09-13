#include <zima/command_host/host.hpp>
#include <cmath>
#include <numbers>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){if(std::abs(actual-expected)>1e-5)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
Json run(command_host::Host& host,const char* name,Json args=Json::object()) {
    const auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result.data;
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);
    run(host,"new",{{"type","part"},{"name","hole-target"}});
    const auto box=run(host,"box.create",{{"length_mm","40"},{"width_mm","40"},{"height_mm","40"}}).at("container");
    const auto plane=run(host,"construction.create",{{"kind","plane"},{"name","Hole end"},{"base_plane","xy"},{"offset_mm",0}});
    const auto target=Json::array({Json{{"owner",plane.at("entity")},{"key","plane"}}});
    const auto created=run(host,"hole.create",{{"diameter_mm",8},{"bore_length_mm",20},{"bore_end","up_to"},{"bore_targets",target},{"placement",{{"z",-20}}}});
    const auto doc=live.active_document_id(),id=created.at("container").get<std::string>();auto* part=live.open_part(doc);
    const auto original=*part->session.document().find_container(id);const auto area=std::numbers::pi*16;
    const auto volume=[&](double block,double depth){near(part->session.calculated_boundaries().back().volume,block-area*depth);};
    volume(64000,20);
    require(original.feature_kind==document::FeatureKind::Hole&&created.at("bore_targets")[0].at("owner")==plane.at("entity"),"Native Hole target identity changed");
    const auto revision=part->session.revision();
    require(!run(host,"hole.set",{{"container",id},{"bore_targets",created.at("bore_targets")}}).at("changed").get<bool>()&&part->session.revision()==revision,
        "Identical Hole target assignment recalculated the model");
    auto stale=part->session.document();stale.find_container(id)->hole.bore_end_targets.front().fallback_origin.z=500;
    near(kernel.evaluate_history(stale.kernel_operations()).back().volume,64000-area*20);
    auto missing_datum=stale;
    std::erase_if(missing_datum.constructions,[&](const auto& value){return value.entity_id==plane.at("entity").get<std::string>();});
    bool datum_failed=false;try{static_cast<void>(kernel.evaluate_history(missing_datum.kernel_operations()));}catch(const std::exception&){datum_failed=true;}
    require(datum_failed,"Native Hole accepted a cached plane after its original datum disappeared");
    run(host,"construction.set",{{"construction",plane.at("construction")},{"offset_mm",5}});run(host,"regenerate");volume(64000,25);
    const auto reject=[&](Json values) {
        const auto before=*part->session.document().find_container(id);const auto revision=part->session.revision();
        const auto* cache=part->session.calculated_boundaries().data();
        const auto result=host.execute({{"command","hole.set"},{"arguments",{{"container",id},{"bore_targets",std::move(values)}}}});
        require(!result.ok&&part->session.revision()==revision&&part->session.calculated_boundaries().data()==cache&&
            *part->session.document().find_container(id)==before,"Invalid Hole target partially changed the model");
    };
    reject(Json::array());reject(Json::array({Json{{"owner","missing"},{"key","plane"}}}));
    reject(Json::array({Json{{"owner",id},{"key","end"}}}));
    reject(Json::array({Json{{"owner",box},{"key","z_max"},{"kind","point"}}}));
    reject(Json::array({Json{{"owner",box},{"key","z_max"},{"fallback_origin",Json::array({0,0,5})}}}));
    reject(Json::array({target[0],target[0]}));
    auto external=target;external[0]["instance_path"]="outside";reject(external);
    const auto face=Json::array({Json{{"owner",box},{"key","z_max"}}});
    run(host,"hole.set",{{"container",id},{"bore_targets",face}});volume(64000,40);
    run(host,"undo");volume(64000,25);run(host,"redo");volume(64000,40);
    run(host,"box.set",{{"container",box},{"height_mm","80"}});volume(128000,60);
    run(host,"save");auto loaded=document::PartDocument::load(dir/"hole-target.prtz");
    require(loaded.find_container(id)->hole.sketch_id==original.hole.sketch_id&&loaded.find_container(id)->hole.circle_id==original.hole.circle_id&&
        loaded.find_container(id)->hole.tip_sketch_id==original.hole.tip_sketch_id&&loaded.find_container(id)->hole.chamfer_sketch_id==original.hole.chamfer_sketch_id,
        "Hole target edits replaced owned profile identities");
    loaded.find_container(box.get<std::string>())->box.height=100;
    near(kernel.evaluate_history(loaded.kernel_operations()).back().volume,160000-area*70);
    loaded.find_container(id)->hole.bore_end_targets.front().reference.semantic_key="removed-original-face";
    bool missing_failed=false;try{static_cast<void>(kernel.evaluate_history(loaded.kernel_operations()));}catch(const std::exception&){missing_failed=true;}
    require(missing_failed,"Native Hole calculation accepted cached geometry for a missing original face");
    loaded.find_container(id)->hole.bore_end_condition=document::EndCondition::Length;
    near(kernel.evaluate_history(loaded.kernel_operations()).back().volume,160000-area*20);
    auto future=document::PartDocument::create_box_container();
    loaded.insert_history_entry(document::PartHistoryKind::Feature,future.id);loaded.history.push_back(future);
    loaded.find_container(id)->hole.bore_end_condition=document::EndCondition::UpTo;
    loaded.find_container(id)->hole.bore_end_targets.front().reference={future.id,"z_max",{}};
    bool future_failed=false;try{static_cast<void>(kernel.evaluate_history(loaded.kernel_operations()));}catch(const std::exception&){future_failed=true;}
    require(future_failed,"Native Hole referenced geometry from a later history operation");

    run(host,"new",{{"type","part"},{"name","hole-body-target"}});
    const auto stock=run(host,"box.create",{{"length_mm","20"},{"width_mm","20"},{"height_mm","20"}}).at("container");
    part=live.open_part(live.active_document_id());const auto source_body=part->session.document().body_history.active_body_id();
    run(host,"placement.set",{{"object",source_body},{"values",{{"reference_offset:0",10}}}});
    const auto carrier=run(host,"body.create",{{"name","Hole carrier"}}).at("body");
    run(host,"placement.set",{{"object",carrier},{"values",{{"reference_offset:0",5}}}});
    run(host,"box.create",{{"length_mm","40"},{"width_mm","40"},{"height_mm","80"}});
    const auto body_target=Json::array({Json{{"owner",stock},{"key","z_max"}}});
    const auto body_hole=run(host,"hole.create",{{"diameter_mm",8},{"bore_length_mm",60},{"bore_end","up_to"},
        {"bore_targets",body_target},{"placement",{{"z",-40}}}}).at("container").get<std::string>();
    volume(136000,55);near(part->session.document().find_container(body_hole)->hole.bore_end_targets.front().fallback_origin.z,15);
    run(host,"body.activate",{{"body",source_body}});run(host,"box.set",{{"container",stock},{"height_mm","22"}});volume(136800,56);
    run(host,"undo");volume(136000,55);run(host,"redo");volume(136800,56);run(host,"save");
    std::vector<kernel::BodyResult> body_cache;auto body_saved=document::PartDocument::load(dir/"hole-body-target.prtz",&body_cache);
    body_saved.find_container(stock.get<std::string>())->box.height=24;
    kernel::OcctKernel cold_kernel;const auto cold=cold_kernel.evaluate_history_incremental(body_saved.kernel_operations(),body_cache);
    near(cold.back().volume,137600-area*57);

}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());
    const auto dir=parent/("zima-hole-target-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create test directory");verify(kernel,dir);
    require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);
    std::cout<<"Native Hole original end targets passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
