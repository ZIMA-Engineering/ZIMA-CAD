#include <zima/document/part_document.hpp>
#include <zima/document/document_session.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/shaft_thread_geometry.hpp>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace zima;
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
int main() {
    try {
        kernel::OcctKernel kernel;
        // A circular extrusion can have an indirect surface frame and a
        // reversed OCCT face while still being an external shaft.
        for (const bool reverse : {false,true}) {
            auto extruded=document::PartDocument::create_default();
            auto sketch=sketcher::Sketch::create_default();
            static_cast<void>(sketch.add_circle(0,0,5));
            auto cylinder=document::PartDocument::create_extrusion_container(sketch.id);
            cylinder.extrusion.length_forward=30;cylinder.extrusion.length_reverse=30;
            if (reverse) cylinder.extrusion.direction=document::ExtrusionDirection::Reverse;
            extruded.sketches.push_back(sketch);extruded.history.push_back(cylinder);
            const auto bodies=kernel.evaluate_history(extruded.kernel_operations());
            bool found=false;
            for (const auto& ref : bodies.back().mesh.original_references.triangle_references)
                if (ref.owner_id==cylinder.id && ref.surface &&
                    ref.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder) {
                    require(!ref.surface->reversed,"External circular extrusion was classified as an internal wall");
                    found=true;
                }
            require(found,"Circular extrusion did not persist its cylinder");
            // The same geometric cylinder used as a subtraction must stay
            // internal, independently of the extrusion direction.
            auto block=document::PartDocument::create_box_container();
            block.box={30,30,80};
            cylinder.combine_mode=document::CombineMode::Subtract;
            extruded.history={block,cylinder};
            const auto cut=kernel.evaluate_history(extruded.kernel_operations());
            bool inside=false;
            for (const auto& ref : cut.back().mesh.original_references.triangle_references)
                if (ref.owner_id==cylinder.id && ref.surface &&
                    ref.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder) {
                    require(ref.surface->reversed,"Circular cut was classified as an external shaft");
                    inside=true;
                }
            require(inside,"Circular cut did not persist its cylinder");

        }
        for (const bool threaded : {false,true}) {
            double forward_volume=0;
            for (const bool flipped : {false,true}) {
                auto opening_doc=document::PartDocument::create_default();
                auto base=document::PartDocument::create_box_container();base.box={40,40,40};
                auto opening=document::PartDocument::create_thread_container();
                opening.thread.enabled=threaded;
                opening.thread.bore_length=20;opening.thread.length_forward=15;
                opening.thread.chamfer_enabled=true;opening.hole.drill_point_enabled=true;
                opening.placement.z=flipped ? 20 : -20;
                opening.placement.rotation_x=flipped ? 180 : 0;
                opening_doc.history={base,opening};
                const auto bodies=kernel.evaluate_history(opening_doc.kernel_operations());
                const auto& body=bodies.back();
                require(body.volume>0 && body.volume<bodies.front().volume,
                    "Opening with drill point did not remove material");
                if (!flipped) forward_volume=body.volume;
                else require(std::abs(body.volume-forward_volume)<1e-5,
                    "Reversing the opening changed its bore/tip/chamfer volume");
                std::set<std::string> cones;
                bool bore=false;
                for (const auto& ref : body.mesh.original_references.triangle_references) {
                    if (ref.owner_id!=opening.id || !ref.surface) continue;
                    if (ref.surface->kind==kernel::SurfaceGeometry::Kind::Cone) cones.insert(ref.semantic_key);
                    if (ref.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder &&
                        !ref.semantic_key.starts_with("thread:")) {
                        require(ref.surface->reversed,"Opening bore was offered as an external shaft");
                        bore=true;
                    }
                }
                require(bore && cones.size()>=2,"Opening lost its bore, drill point or entrance chamfer");
            }
        }
        auto doc=document::PartDocument::create_default();
        auto shaft=document::PartDocument::create_cylinder_container();
        shaft.cylinder.radius=5;shaft.cylinder.height=30;
        doc.history.push_back(shaft);doc.insert_history_entry(document::PartHistoryKind::Feature,shaft.id);
        const auto source=kernel.evaluate_history(doc.kernel_operations());
        const auto& references=source.back().mesh.original_references;
        const auto face=[&](const char* key) {
            for (const auto& ref : references.triangle_references)
                if (ref.owner_id==shaft.id && ref.semantic_key==key && ref.surface) return ref;
            throw std::runtime_error("Cylinder did not persist its analytic face geometry");
        };
        auto feature=document::PartDocument::create_shaft_thread_container();
        feature.shaft_thread.cylinder=face("side");feature.shaft_thread.start=face("z_min");
        feature.shaft_thread.root_diameter=8.160;feature.shaft_thread.length=15;
        auto request=document::PartDocument::shaft_thread_request(feature,&references);
        const auto resolved=kernel::resolve_shaft_thread(request);
        require(std::abs(resolved.length-18)<1e-8 && resolved.runout_end==3 &&
            resolved.axis_direction.z>0.99,"Shaft extent/runout did not start at the selected plane");
        doc.history.push_back(feature);doc.insert_history_entry(document::PartHistoryKind::Feature,feature.id);
        auto result=kernel.evaluate_history(doc.kernel_operations());
        require(result.size()==2 && std::abs(result.back().volume-source.back().volume)<1e-6,
            "Cosmetic shaft thread changed solid material");
        const auto count=[&](const auto& mesh,const char* key) {
            return std::ranges::count_if(mesh.triangle_references,[&](const auto& ref) {
                return ref.owner_id==feature.id && ref.semantic_key==key;
            });
        };
        require(count(result.back().mesh,"thread:surface:root")>0 &&
            count(result.back().mesh,"thread:surface:runout:end")>0 &&
            count(result.back().mesh,"thread:surface:nominal")==0,
            "Shaft thread did not create the root sheet and runout or duplicated the outer cylinder");
        for (const auto& ref : result.back().mesh.triangle_references)
            if (ref.owner_id==feature.id)
                require(ref.is_thread_surface()==(ref.semantic_key=="thread:surface:root"),
                    "Runout was classified as the threaded cylinder");
        // Numeric edits are independent of the catalog designation.
        doc.history.back().shaft_thread.root_diameter=8.1;
        result=kernel.evaluate_history_incremental(doc.kernel_operations(),result);
        require(doc.history.back().shaft_thread.designation=="M10","Numeric diameter changed the catalog selection");
        const auto path=std::filesystem::temp_directory_path()/"zima-shaft-thread-contract.prtz";
        doc.save(path,result);
        std::vector<kernel::BodyResult> loaded_boundaries;
        auto loaded=document::PartDocument::load(path,&loaded_boundaries);
        std::filesystem::remove(path);
        require(loaded.history.back().shaft_thread==doc.history.back().shaft_thread &&
            !loaded_boundaries.empty(),"Shaft thread parameters did not round-trip");
        const auto loaded_request=document::PartDocument::shaft_thread_request(loaded.history.back(),
            &loaded_boundaries.back().mesh.original_references);
        require(kernel::resolve_shaft_thread(loaded_request).root_radius==4.05,
            "Reloaded preview requires missing analytic source data");
        loaded.history.back().shaft_thread.end_condition=document::EndCondition::ThroughAll;
        auto through=kernel.evaluate_history(loaded.kernel_operations());
        require(count(through.back().mesh,"thread:surface:runout:end")==0 &&
            count(through.back().mesh,"thread:surface:root")>0,"Through-all thread retained its exit runout");
        loaded.history.back().shaft_thread.end_condition=document::EndCondition::UpTo;
        loaded.history.back().shaft_thread.end=face("z_max");
        auto up_to=kernel.evaluate_history(loaded.kernel_operations());
        require(count(up_to.back().mesh,"thread:surface:runout:end")==0,"Up-to thread retained its exit runout");
        loaded.history.back().shaft_thread.end=face("side");
        const auto to_cylinder=kernel.evaluate_history(loaded.kernel_operations());
        require(count(to_cylinder.back().mesh,"thread:surface:runout:end")==0 &&
            count(to_cylinder.back().mesh,"thread:surface:root")>0,
            "Up-to cylinder did not reach its end without runout");
        loaded.history.back().shaft_thread.end_condition=document::EndCondition::Length;
        loaded.history.back().shaft_thread.start=face("z_max");
        const auto reverse=kernel::resolve_shaft_thread(document::PartDocument::shaft_thread_request(
            loaded.history.back(),&references));
        require(reverse.axis_direction.z<-0.99 && std::abs(reverse.origin.z-30)<1e-7,
            "Thread starting at the far shaft face did not reverse inward");
        {
            auto beveled=document::PartDocument::create_default();
            beveled.history.push_back(shaft);
            auto chamfer=document::PartDocument::create_chamfer_container({{shaft.id,"circle:z_min",{}}});
            chamfer.edge_treatment.primary_size=1;
            beveled.history.push_back(chamfer);
            const auto input=kernel.evaluate_history(beveled.kernel_operations());
            const auto& geometry=input.back().mesh.original_references;
            std::optional<kernel::FaceReference> cone;
            for (const auto& ref : geometry.triangle_references)
                if (ref.owner_id==chamfer.id && ref.surface && ref.surface->kind==kernel::SurfaceGeometry::Kind::Cone)
                    cone=ref;
            require(cone.has_value(),"Chamfer did not persist its analytic cone reference");
            auto cut_thread=feature;cut_thread.shaft_thread.chamfer=cone;
            const auto analytic=kernel::resolve_shaft_thread(document::PartDocument::shaft_thread_request(cut_thread,&geometry));
            require(analytic.start_offset>0.07 && analytic.start_offset<0.09 &&
                std::abs(analytic.start_offset+analytic.length-analytic.runout_end-15)<1e-7,
                "Chamfer shifted the length datum or left the root surface in air");
            beveled.history.push_back(cut_thread);
            const auto output=kernel.evaluate_history(beveled.kernel_operations());
            require(std::abs(output.back().volume-input.back().volume)<1e-6 &&
                count(output.back().mesh,"thread:surface:root")>0,"Chamfer-clipped thread changed the material or disappeared");
            document::DocumentSession remembered(beveled,output);
            auto missing_chamfer=remembered.document();
            missing_chamfer.history.erase(missing_chamfer.history.begin()+1);
            const auto retained=kernel.evaluate_history(missing_chamfer.kernel_operations());
            require(count(retained.back().mesh,"thread:surface:root")>0 &&
                missing_chamfer.history.back().shaft_thread.chamfer->surface,
                "Missing entrance chamfer erased the thread instead of retaining its reference data");
        }
        for (const bool reversed : {false,true}) {
            auto beveled=document::PartDocument::create_default();
            beveled.history.push_back(shaft);
            auto chamfer=document::PartDocument::create_chamfer_container(
                {{shaft.id,reversed ? "circle:z_min" : "circle:z_max",{}}});
            chamfer.edge_treatment.primary_size=1;
            beveled.history.push_back(chamfer);
            const auto input=kernel.evaluate_history(beveled.kernel_operations());
            auto ending=feature;
            ending.shaft_thread.start=face(reversed ? "z_max" : "z_min");
            ending.shaft_thread.end_condition=document::EndCondition::UpTo;
            for (const auto& ref : input.back().mesh.original_references.triangle_references)
                if (ref.owner_id==chamfer.id && ref.surface && ref.surface->kind==kernel::SurfaceGeometry::Kind::Cone)
                    ending.shaft_thread.end=ref;
            const auto resolved_end=kernel::resolve_shaft_thread(document::PartDocument::shaft_thread_request(
                ending,&input.back().mesh.original_references));
            require(std::abs(resolved_end.start_offset+resolved_end.length-29.92)<1e-6 &&
                resolved_end.runout_end==0,"Up-to end chamfer did not intersect the root cylinder");
            beveled.history.push_back(ending);
            const auto output=kernel.evaluate_history(beveled.kernel_operations());
            require(count(output.back().mesh,"thread:surface:runout:end")==0 &&
                count(output.back().mesh,"thread:surface:root")>0 &&
                std::abs(output.back().volume-input.back().volume)<1e-6,
                "Up-to chamfer changed material, retained runout or lost the thread");
            beveled.history.back().shaft_thread.root_diameter=7;
            const auto shallow=kernel::resolve_shaft_thread(document::PartDocument::shaft_thread_request(
                beveled.history.back(),&input.back().mesh.original_references));
            require(std::abs(shallow.length-30)<1e-6 && shallow.runout_end==0,
                "Shallow end chamfer did not retain the root inside material up to the end face");
            const auto shallow_output=kernel.evaluate_history(beveled.kernel_operations());
            require(count(shallow_output.back().mesh,"thread:surface:root")>0 &&
                std::abs(shallow_output.back().volume-input.back().volume)<1e-6,
                "Shallow chamfer lost the thread or changed material");
            beveled.history.front().cylinder.radius=3;
            beveled.history.back().shaft_thread.root_diameter=8.16;
            const auto outside=kernel.evaluate_history(beveled.kernel_operations());
            require(count(outside.back().mesh,"thread:surface:root")>0 &&
                count(outside.back().mesh,"thread:surface:runout:end")==0 &&
                std::abs(outside.back().volume-outside[outside.size()-2].volume)<1e-6,
                "Shrinking shaft erased an Up-to chamfer thread outside material");
            document::DocumentSession remembered(beveled,outside);
            auto missing_end=remembered.document();
            missing_end.history.erase(missing_end.history.begin()+1);
            const auto retained_end=kernel.evaluate_history(missing_end.kernel_operations());
            require(count(retained_end.back().mesh,"thread:surface:root")>0 &&
                count(retained_end.back().mesh,"thread:surface:runout:end")==0,
                "Missing Up-to target erased the thread or restored its runout");
        }
        for (const double radius : {3.0,doc.history.back().shaft_thread.root_diameter*0.5}) {
            auto smaller=doc;
            smaller.history.front().cylinder.radius=radius;
            const auto output=kernel.evaluate_history(smaller.kernel_operations());
            require(count(output.back().mesh,"thread:surface:root")>0 &&
                count(output.back().mesh,"thread:surface:runout:end")>0 &&
                std::abs(output.back().volume-output.front().volume)<1e-6 &&
                smaller.history.back().shaft_thread.root_diameter==doc.history.back().shaft_thread.root_diameter,
                "Shrinking shaft erased, resized or invalidated the independent thread");
        }
        {
            auto moved=doc;
            moved.history.front().placement.rotation_y=35;
            moved.history.front().placement.x=12;
            moved.history.front().cylinder.height=45;
            moved.history.back().shaft_thread.end_condition=document::EndCondition::ThroughAll;
            const auto rebuilt=kernel.evaluate_history(moved.kernel_operations());
            const auto resolved_moved=kernel::resolve_shaft_thread(document::PartDocument::shaft_thread_request(
                moved.history.back(),&rebuilt.back().mesh.original_references));
            require(std::abs(resolved_moved.length-45)<1e-7 &&
                resolved_moved.axis_direction.x>0.5 && std::abs(resolved_moved.origin.x-12)<1e-7,
                "Regenerated shaft thread did not follow its source placement and length");
        }
        {
            document::DocumentSession session(doc,kernel.evaluate_history(doc.kernel_operations()));
            auto resized=session.document();
            resized.history.front().cylinder.radius=4.5;
            resized.history.front().placement.x=12;
            session.commit(resized,kernel.evaluate_history(resized.kernel_operations()));
            auto detached=session.document();
            require(detached.history.back().shaft_thread.cylinder.surface &&
                std::abs(detached.history.back().shaft_thread.cylinder.surface->radius-4.5)<1e-8 &&
                std::abs(detached.history.back().shaft_thread.cylinder.surface->origin.x-12)<1e-8,
                "Successful regeneration did not retain the latest reference geometry");
            detached.history.erase(detached.history.begin());
            std::erase_if(detached.history_order,[&](const auto& entry) { return entry.id==shaft.id; });
            detached.history_cursor=detached.history_order.size();
            auto orphan=kernel.evaluate_history(detached.kernel_operations());
            require(count(orphan.back().mesh,"thread:surface:root")>0 && orphan.back().volume==0,
                "Deleting the last source solid erased its retained thread");
            const auto retained_path=std::filesystem::temp_directory_path()/"zima-shaft-fallback.prtz";
            detached.save(retained_path,orphan);
            auto reopened=document::PartDocument::load(retained_path);
            std::filesystem::remove(retained_path);
            const auto restored=kernel.evaluate_history(reopened.kernel_operations());
            require(count(restored.back().mesh,"thread:surface:root")>0 &&
                reopened.history.back().shaft_thread.cylinder.surface &&
                std::abs(reopened.history.back().shaft_thread.cylinder.surface->radius-4.5)<1e-8,
                "Missing-reference fallback did not survive save, reopen and regeneration");
        }
        loaded.history.back().shaft_thread.length=40;
        bool rejected=false;
        try { static_cast<void>(kernel.evaluate_history(loaded.kernel_operations())); }
        catch (const std::exception&) { rejected=true; }
        require(rejected,"Overlong shaft thread was accepted");
        std::cout<<"Shaft thread contracts passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
