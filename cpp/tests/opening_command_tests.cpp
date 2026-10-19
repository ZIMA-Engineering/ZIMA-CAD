#include "profile_command_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/opening_operations.hpp>
#include <zima/workspace/opening_copy.hpp>
#include <zima/workspace/relation_operations.hpp>
#include <zima/document/placement_orientation.hpp>
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
    options.settings=[] {command_host::Settings settings;settings.templates={fs::absolute("config/templates"),"START_PART_mm.prtz","START_ASSEMBLY_mm.asmz","Body"};return settings;};
    command_host::Host host(live,kernel,directory,options);
    require(host.execute_text("opening.get missing").code=="unsupported_document","Opening query without Part did not fail cleanly");
    for(const auto* type:{"plain","metric","whitworth","pipe"}) {
        run(host,"new",{{"type","part"},{"name",std::string("opening-")+type}});
        zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","60"},{"width_mm","60"},{"height_mm","60"}});
        auto* state=live.open_part(live.active_document_id());
        Json args={{"type",type},{"bore_length_mm",20},{"thread_length_mm",10},{"chamfer_enabled",false},
            {"drill_point_enabled",false},{"placement",{{"z",-30}}}};
        const bool threaded=std::string(type)!="plain";
        if(threaded)args["designation"]=std::string(type)=="metric"?"M10":std::string(type)=="whitworth"?"W 1/2":"G 1/2";
        else args["nominal_diameter_mm"]=10;
        const auto created=run(host,"opening.create",args).data;const auto id=created.at("container").get<std::string>();
        const auto original=*state->session.document().find_container(id);
        if(threaded) {
            const auto& doc=state->session.document();
            const auto nominal=doc.dimension_identifiers.identifier(id,"parameter:thread_designation");
            const auto pitch=doc.dimension_identifiers.identifier(id,"parameter:pitch");
            const auto inputs=workspace::relation_dimensions(doc);
            require(inputs.contains(nominal)&&inputs.contains(pitch),"Catalog dimensions are missing from Relations");
            near(std::get<double>(inputs.at(nominal).input.value.data),original.thread.nominal_diameter);
            near(std::get<double>(inputs.at(pitch).input.value.data),original.thread.pitch);
            require(!inputs.at(nominal).input.writable&&!inputs.at(pitch).input.writable,"Catalog dimensions became writable scalars");
            auto inches=doc;inches.document_units["Length"]="in";
            const auto converted=workspace::relation_dimensions(inches);
            near(std::get<double>(converted.at(nominal).input.value.data),original.thread.nominal_diameter/25.4);
            near(std::get<double>(converted.at(pitch).input.value.data),original.thread.pitch/25.4);
            const auto references=workspace::family_references(live,live.active_document_id());
            require(std::ranges::none_of(references,[&](const auto& r){return r.name==nominal||r.name==pitch;}),"Read-only catalog values became editable Family fields");
            const auto revision=state->session.revision();
            const auto initial_history=doc.history;const auto initial_relations=doc.relations;
            for(const auto& symbol:{nominal,pitch}) {
                const auto rejected=host.execute({{"command","document.relations.set"},{"arguments",{{"relations",symbol+" = 12\n"}}}});
                require(!rejected.ok&&state->session.revision()==revision&&state->session.document().history==initial_history&&
                    state->session.document().relations==initial_relations,"Writing a catalog scalar changed the document");
            }
            auto relation_inputs=workspace::relation_inputs(doc,{});
            const auto expression="stock = \"M\" & "+nominal+" & \"x\" & "+pitch+"\n";
            const auto values=document::RelationProgram(expression).evaluate(relation_inputs,6);
            require(!std::get<std::string>(values.at("stock").data).empty(),"Thread stock expression could not read catalog dimensions");
        }
        run(host,"undo");require(state->session.document().history.size()==1,"Opening creation did not undo in one step");
        run(host,"redo");require(*state->session.document().find_container(id)==original,"Opening creation Redo replaced its source identities");
        require(original.feature_kind==document::FeatureKind::Thread && original.thread.enabled==threaded,"Wrong native opening kind or enabled state");
        {
            auto source=original;source.placement.x=123.;source.value_locks.insert("bore_length");
            source.thread.end_targets_forward.emplace_back();source.thread.length_end_targets.emplace_back();
            const auto before=source;
            const auto first=workspace::copy_opening_settings(source),second=workspace::copy_opening_settings(source);
            require(source==before,"Copying Opening settings modified its source");
            require(first.id!=source.id&&first.id!=second.id&&first.feature_id!=source.feature_id&&
                first.container_origin.id!=source.container_origin.id&&first.hole.sketch_id!=source.hole.sketch_id&&
                first.hole.sketch_id!=second.hole.sketch_id,"Pasted Opening reused source or previous-paste identity");
            auto parameters=source.thread;parameters.end_targets_forward.clear();parameters.length_end_targets.clear();
            require(first.thread==parameters&&first.value_locks==source.value_locks&&
                first.hole.drill_point_enabled==source.hole.drill_point_enabled&&
                first.hole.drill_point_angle_degrees==source.hole.drill_point_angle_degrees,"Opening copy lost settings");
            require(first.placement==document::Placement{},"Opening copy retained source placement");
        }
        const double r=(threaded?original.thread.profile_diameter:original.thread.nominal_diameter)/2;
        const double plain_volume=216000-std::numbers::pi*r*r*20;
        near(state->session.calculated_boundaries().back().volume,plain_volume);
        {
            auto pasted=workspace::copy_opening_settings(original);
            pasted.placement=original.placement;pasted.placement.x+=20;
            const auto revision=state->session.revision();
            require(workspace::commit_opening(live,kernel,live.active_document_id(),pasted,workspace::OpeningEditMode::Create),"Copied Opening did not commit");
            require(state->session.revision()==revision+1&&state->session.document().find_container(pasted.id),"Opening paste must be one transaction");
            near(state->session.calculated_boundaries().back().volume,216000-2*std::numbers::pi*r*r*20);
            require(*state->session.document().find_container(id)==original,"Pasting changed the original Opening");
            run(host,"undo");require(!state->session.document().find_container(pasted.id),"Opening paste Undo left the clone");
            near(state->session.calculated_boundaries().back().volume,plain_volume);
            run(host,"redo");require(state->session.document().find_container(pasted.id),"Opening paste Redo lost its identity");
            near(state->session.calculated_boundaries().back().volume,216000-2*std::numbers::pi*r*r*20);
            run(host,"undo");
        }
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
        // Whole-Origin entry is a frame attachment, not the single FRONT
        // plane work plane. Test the ordinary Opening commit independently
        // of the clipboard and compare its physical cut with its wire axis.
        for(const bool reverse:{false,true})for(const int shape:{0,1,2,3}) {
            auto placed=original;placed.placement={};
            placed.thread.direction=reverse?document::ExtrusionDirection::Reverse:document::ExtrusionDirection::Forward;
            placed.thread.end_condition_forward=shape==2?document::EndCondition::ThroughAll:shape==3?document::EndCondition::UpTo:document::EndCondition::Length;
            placed.thread.chamfer_enabled=shape==1;placed.thread.chamfer_depth=.6;placed.thread.chamfer_angle_degrees=90;
            placed.hole.drill_point_enabled=shape==1;placed.hole.drill_point_angle_degrees=118;
            for(const auto* plane:{"xz","xy","yz"}) {
                document::ConstructionReference reference;reference.owner_id=state->session.document().body_history.active_body_id()+":origin";
                reference.semantic_key=std::string("origin:plane:")+plane;reference.supports_offset=true;
                reference.offset=std::string(plane)=="xy"?(reverse?30.:-30.):std::string(plane)=="yz"?15.:0.;
                placed.placement.references.push_back(reference);
            }
            for(const auto* plane:{"xz","xy"}) {
                document::ConstructionReference reference;reference.owner_id=state->session.document().body_history.active_body_id()+":origin";
                reference.semantic_key=std::string("origin:plane:")+plane;reference.orientation_only=true;reference.orientation_drives_rotation=true;
                reference.orientation_role=std::string(plane)=="xz"?"front":"top";placed.placement.references.push_back(reference);
            }
            if(shape==3) {
                document::ExtrusionParameters::EndTarget target;target.kind=document::EndTargetKind::Plane;
                target.reference={state->session.document().body_history.active_body_id()+":origin","origin:plane:xy",{}};
                placed.thread.end_targets_forward={target};
            }
            const auto revision=state->session.revision();
            require(workspace::commit_opening(live,kernel,live.active_document_id(),placed,workspace::OpeningEditMode::Replace),"Whole-Origin Opening did not commit");
            const auto committed=*state->session.document().find_container(id);
            require(committed.placement.reference_valid&&document::placement_references_use_whole_origin(committed.placement.references),"Whole-Origin frame identity lost");
            near(committed.placement.x,15);near(committed.placement.y,0);near(committed.placement.z,reverse?30:-30);
            near(committed.placement.rotation_x,0);near(committed.placement.rotation_y,0);near(committed.placement.rotation_z,0);
            const double tip=r/std::tan(118*std::numbers::pi/360),c=.6;
            const double cut=std::numbers::pi*(r*r*(shape==2?60:shape==3?30:20)+(shape==1?r*c*c+c*c*c/3+r*r*tip/3:0));
            const double expected=216000-cut;
            near(state->session.calculated_boundaries().back().volume,expected);
            std::optional<kernel::ViewerAxis> preview_axis;
            const auto wire=state->session.document().thread_edges(committed,nullptr,&preview_axis);
            require(!wire.empty()&&preview_axis.has_value(),"Whole-Origin Opening preview missing");
            near(preview_axis->direction.x,0);near(preview_axis->direction.y,0);near(preview_axis->direction.z,reverse?-1:1);
            auto up_to_thread=committed;up_to_thread.thread.length_end_condition=document::EndCondition::UpTo;
            document::ExtrusionParameters::EndTarget thread_end;thread_end.kind=document::EndTargetKind::Plane;
            thread_end.fallback_origin={15,0,reverse?20.:-20.};up_to_thread.thread.length_end_targets={thread_end};
            near(document::PartDocument::thread_length(up_to_thread),10);
            const auto committed_revision=state->session.revision();
            require(committed_revision!=revision,"Whole-Origin Opening failed to publish a new revision");
            const bool changed_noop=workspace::commit_opening(live,kernel,live.active_document_id(),committed,workspace::OpeningEditMode::Replace);
            require(!changed_noop&&state->session.revision()==committed_revision,
                "Unchanged whole-Origin Opening added a transaction");
            if(shape==1) {
                const auto path=directory/(std::string("whole-origin-")+type+(reverse?"-reverse.prtz":"-forward.prtz"));
                state->session.document().save(path,state->session.calculated_boundaries());std::vector<kernel::BodyResult> bodies;
                const auto reopened=document::PartDocument::load(path,&bodies);
                require(*reopened.find_container(id)==committed,"Whole-Origin Opening save/reopen changed placement or identities");
                require(!bodies.empty(),"Whole-Origin Opening lost calculated native geometry");
                near(bodies.back().volume,expected);near(kernel.evaluate_history(reopened.kernel_operations()).back().volume,expected);
            }
            run(host,"undo");require(*state->session.document().find_container(id)==original,"Whole-Origin Opening Undo changed original");
            near(state->session.calculated_boundaries().back().volume,plain_volume);
            run(host,"redo");require(*state->session.document().find_container(id)==committed,"Whole-Origin Opening Redo changed references");
            near(state->session.calculated_boundaries().back().volume,expected);run(host,"undo");
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
    // A derived bore must follow its thread driver in the same transaction,
    // before validating the new thread/runout against the previous bore.
    run(host,"new",{{"type","part"},{"name","opening-derived-bore"}});
    zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","60"},{"width_mm","60"},{"height_mm","60"}});
    auto* state=live.open_part(live.active_document_id());
    const auto id=run(host,"opening.create",{{"type","metric"},{"designation","M10"},{"bore_length_mm",14.5},
        {"thread_length_mm",13},{"runout_pitch_factor",.5},{"chamfer_enabled",false},{"drill_point_enabled",false},
        {"placement",{{"z",-30}}}}).data.at("container").get<std::string>();
    const auto& identifiers=state->session.document().dimension_identifiers;
    const auto expression=identifiers.identifier(id,"parameter:bore_length")+" = min(16, "+
        identifiers.identifier(id,"parameter:thread_length")+" + "+identifiers.identifier(id,"parameter:pitch")+")\n";
    run(host,"document.relations.set",{{"relations",expression}});
    const auto source=*state->session.document().find_container(id);
    const double radius=source.thread.profile_diameter/2;
    for(const double length:{11.7,13.,14.5,13.}) {
        const auto before=*state->session.document().find_container(id);
        run(host,"opening.set",{{"container",id},{"thread_length_mm",length}});
        const auto current=*state->session.document().find_container(id);
        near(current.thread.length_forward,length);near(current.thread.bore_length,std::min(16.,length+1.5));
        near(state->session.calculated_boundaries().back().volume,216000-std::numbers::pi*radius*radius*current.thread.bore_length);
        require(current.hole==source.hole&&current.placement==source.placement,"Derived opening edit replaced profile or placement identity");
        run(host,"undo");require(*state->session.document().find_container(id)==before,"Derived opening Undo lost dependent bore");
        run(host,"redo");require(*state->session.document().find_container(id)==current,"Derived opening Redo lost dependent bore");
    }
    const auto revision=state->session.revision();const auto before=*state->session.document().find_container(id);
    const auto* cache=state->session.calculated_boundaries().data();
    require(!host.execute({{"command","opening.set"},{"arguments",{{"container",id},{"thread_length_mm",16}}}}).ok&&
        state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache&&
        *state->session.document().find_container(id)==before,"Impossible derived opening edit was not atomic");
    require(!run(host,"opening.set",{{"container",id},{"thread_length_mm",13}}).data.at("changed").get<bool>()&&
        state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache,"Unchanged derived opening recalculated");
    run(host,"save");std::vector<kernel::BodyResult> saved;
    const auto restored=document::PartDocument::load(directory/"opening-derived-bore.prtz",&saved);
    require(restored.relations==expression&&*restored.find_container(id)==before,"Derived opening native round trip lost definition");
    near(saved.back().volume,216000-std::numbers::pi*radius*radius*before.thread.bore_length);
}
}
int main(){try {
    kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());
    const auto directory=root/("zima-openings-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create test directory");verify(kernel,directory);
    require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);
    std::cout<<"Opening commands: catalog sizes, independent bore/chamfer/tip volumes, direction, through all, locks, atomic errors, identities and native round trip passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
