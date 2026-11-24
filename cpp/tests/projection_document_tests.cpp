#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/history_operations.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/workspace/document_dependencies.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <STEPControl_Writer.hxx>
#include <set>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace zima;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
#include "projection_external_contract.inc"
#include "projection_relief_contract.inc"
int main(){try {
    verify_projection_relief();
    for(const auto mode:{document::ProjectionOutput::Curves,document::ProjectionOutput::Split,document::ProjectionOutput::Surface}) {
        kernel::OcctKernel engine;workspace::Workspace live;
        auto part=document::PartDocument::create_default();const auto id=part.document_id;
        live.add_part(part);live.activate(id);
        auto base_sketch=sketcher::Sketch::create_default();base_sketch.plane_auto=false;
        static_cast<void>(base_sketch.add_rectangle(-10,-10,10,10));
        auto base=document::PartDocument::create_feature_container(base_sketch.id);
        base.feature.sides[0].length=11.;
        workspace::commit_profile(live,engine,id,base,workspace::ProfileEditMode::Create,base_sketch);
        auto* state=live.open_part(id);
        kernel::FaceReference target;
        for(const auto& face:state->session.calculated_boundaries().back().mesh.triangle_references)
            if(face.owner_id==base.id&&face.semantic_key.find("end:from:")!=std::string::npos)target=face;
        require(target.valid(),"No current bounded target face");
        auto source=sketcher::Sketch::create_default();source.plane_auto=false;
        const auto circle=source.add_circle(0,0,4);
        auto feature=document::PartDocument::create_feature_container(source.id);
        feature.feature.type=document::FeatureType::Projection;feature.placement.z=20;
        feature.feature.projection.output=mode;
        document::ExtrusionParameters::EndTarget selected;selected.reference=target;
        feature.feature.projection.targets={selected};
        if(mode==document::ProjectionOutput::Surface)feature.feature.projection.regions={{target,{0,0,11},{}}};
        workspace::commit_profile(live,engine,id,feature,workspace::ProfileEditMode::Create,source);
        require(state->session.calculated_boundaries().back().calculation_errors.empty(),"Projection transaction failed");
        feature=*state->session.document().find_container(feature.id);
        source=*std::ranges::find(state->session.document().sketches,source.id,&sketcher::Sketch::id);
        if(mode==document::ProjectionOutput::Surface)
            require(!feature.feature.projection.regions.front().boundary_key.empty(),"Region guard was not captured");
        const auto before=state->session.document().serialized();
        const auto generation=state->session.data_generation();
        const auto calculations=kernel::body_calculation_count.load();
        const auto* input=workspace::calculated_operation_input(state->session,feature.id);
        require(input,"Projection preview lost its input packet");
        const auto preview=state->session.document().projection_preview_mesh(feature,input->mesh,&state->session.document());
        bool source_wire=false,result_wire=false,source_point=false,result_point=false;
        for(const auto& edge:preview.edges) {
            require(!edge.preview_terminal_dashed&&edge.color=="#FFFFFF","Valid Projection preview added helper lines or a different curve style");
            if(edge.reference.semantic_key.starts_with("projection:preview:source:"))source_wire=true;
            else {
                require(edge.reference.semantic_key=="projection:preview:result","Unexpected Projection preview geometry");
                for(const auto& point:edge.points)require(std::abs(point.z-11)<1e-7,"Preview misses the target plane");
                result_wire=true;
            }
        }
        for(const auto& point:preview.points) {
            if(point.reference.semantic_key.starts_with("projection:preview:source:")) {
                require(std::abs(point.position.z-20)<1e-7,"Preview source marker moved");source_point=true;
            }else {
                require(std::abs(point.position.z-11)<1e-7,"Preview result marker misses the target");result_point=true;
            }
        }
        require(source_wire&&result_wire&&source_point&&result_point,"Projection preview omitted source/result geometry or markers");
        if(mode==document::ProjectionOutput::Curves) {
            const auto& calculated=state->session.calculated_boundaries().back().mesh.original_references.points;
            for(const auto& marker:preview.points)if(marker.reference.semantic_key.starts_with("projection:preview:result:"))
                require(std::ranges::any_of(calculated,[&](const auto& point){
                    return point.reference.owner_id==feature.id&&point.always_visible&&
                        std::abs(point.position.x-marker.position.x)<1e-7&&
                        std::abs(point.position.y-marker.position.y)<1e-7&&
                        std::abs(point.position.z-marker.position.z)<1e-7;
                }),"Confirmed Projection lost an authored Sketch marker shown by preview");
        }
        auto missing_preview=feature;missing_preview.feature.projection.targets.front().reference.semantic_key="missing-face";
        bool incomplete=false;
        const auto incomplete_preview=state->session.document().projection_preview_mesh(missing_preview,input->mesh,&state->session.document(),&incomplete);
        require(incomplete&&!incomplete_preview.edges.empty(),"Missing target preview did not retain its source or diagnostic");
        for(const auto& edge:incomplete_preview.edges)
            require(edge.color=="#FFFFFF"&&!edge.preview_terminal_dashed&&edge.reference.semantic_key.starts_with("projection:preview:source:"),
                "Incomplete Projection painted a failure over the source Sketch");
        require(kernel::body_calculation_count.load()==calculations&&state->session.document().serialized()==before,
            "Read-only Projection preview calculated or mutated the model");
        require(!workspace::materialize_part_history_input_boundary(live,engine,id,feature.id),"Projection discarded its calculated rollback input");
        workspace::commit_profile(live,engine,id,feature,workspace::ProfileEditMode::Replace,source);
        require(state->session.data_generation()==generation&&state->session.document().serialized()==before&&kernel::body_calculation_count.load()==calculations,"Unchanged OK calculated or created a transaction");
        require(workspace::part_history_dependencies(state->session.document()).contains({base.id,feature.id}),"Target is missing from history dependencies");
        auto invalid=feature;invalid.feature.projection.targets.clear();invalid.feature.projection.regions.clear();
        bool rejected=false;
        try{workspace::commit_profile(live,engine,id,invalid,workspace::ProfileEditMode::Replace,source);}catch(const std::exception&){rejected=true;}
        require(rejected&&state->session.data_generation()==generation&&state->session.document().serialized()==before,"Failed projection changed the document");
        for(bool external:{false,true}) {
            auto invalid_native=state->session.document();auto* invalid_owner=invalid_native.find_container(feature.id);
            if(external)invalid_owner->feature.profile_source=document::ProfileSource::External;
            else invalid_owner->combine_mode=document::CombineMode::Subtract;
            rejected=false;try {static_cast<void>(invalid_native.kernel_operations());}catch(const std::exception&){rejected=true;}
            require(rejected,"Native Projection request bypassed the owned Sketch/material contract");
        }
        for(double radius:{3.,5.,4.}) {
            source.circles.front().radius=radius;
            workspace::commit_profile(live,engine,id,feature,workspace::ProfileEditMode::Replace,source);
            require(state->session.calculated_boundaries().back().calculation_errors.empty(),"Repeated source edit broke projection");
            if(mode==document::ProjectionOutput::Surface)
                require(state->session.document().find_container(feature.id)->feature.projection.regions.front().boundary_key==feature.feature.projection.regions.front().boundary_key,"Radius edit reassigned region identity");
        }
        require(workspace::step_part_document_history(live,id,false)&&workspace::step_part_document_history(live,id,true),"Projection Undo/Redo failed");
        for(bool diameter:{false,true})for(bool reference:{false,true})for(bool locked:{false,true})for(bool fixed_center:{false,true}) {
            auto editable=source;editable.dimensions.clear();
            auto dimension=diameter?editable.create_circle_diameter_dimension(circle):editable.create_circle_radius_dimension(circle);
            dimension.driving=!reference;dimension.locked=locked&&!reference;editable.apply_dimension(dimension);
            const auto center_id=editable.circles.front().center_point_id;editable.set_point_fixed(center_id,fixed_center);
            for(double value:{3.,5.,4.}) {
                const auto radius_before=editable.circles.front().radius;
                require(editable.set_dimension_value(dimension.id,diameter?2*value:value),"Projection source dimension cannot be edited repeatedly");
                const auto radius=editable.circles.front().radius;
                require(std::abs(radius-(reference?radius_before:value))<1e-7,"Source radius/diameter equation failed");
                const auto before=*editable.find_point(center_id);
                static_cast<void>(editable.move_point(center_id,before.x+.2,before.y-.15));
                const auto center=*editable.find_point(center_id);
                const double moved=std::hypot(center.x-before.x,center.y-before.y);
                require(fixed_center?moved<1e-8:moved>1e-6,"Projection source center drag violated its freedom");
                require(std::abs(editable.circles.front().radius-radius)<1e-7,"Center drag broke the radius dimension");
                workspace::commit_profile(live,engine,id,feature,workspace::ProfileEditMode::Replace,editable);
                const auto& result=state->session.calculated_boundaries().back();
                require(result.calculation_errors.empty(),"Dimension/drag broke dependent Projection");
                if(mode==document::ProjectionOutput::Curves) {
                    bool measured=false;
                    for(const auto& edge:result.mesh.original_references.edges)if(edge.reference.owner_id==feature.id)
                        for(const auto& point:edge.points) {
                            require(std::abs(point.z-11)<1e-6&&std::abs(std::hypot(point.x-center.x,point.y-center.y)-radius)<1e-6,"Projected curve disagrees with edited source equation");measured=true;
                        }
                    require(measured,"Edited Projection lost its spatial curves");
                }
                editable=sketcher::Sketch::from_serialized(editable.serialized());
            }
        }
        std::cout<<"PASS source radius/diameter, driving/reference, locks, free/fixed center and successive edits/drags for output "<<static_cast<int>(mode)<<'\n';
        const auto path=std::filesystem::temp_directory_path()/("zima-projection-"+id+".prtz");
        state->session.document().save(path,state->session.calculated_boundaries());
        std::vector<kernel::BodyResult> cache;
        const auto reopened=document::PartDocument::load(path,&cache);
        workspace::Workspace restored;restored.add_part(reopened,cache,path);restored.activate(id);
        kernel::OcctKernel untouched;
        const auto reopen_calls=kernel::body_calculation_count.load();
        require(!workspace::materialize_part_history_input_boundary(restored,untouched,id,feature.id),"Reopened Projection lost its calculated rollback input");
        workspace::commit_profile(restored,untouched,id,*reopened.find_container(feature.id),workspace::ProfileEditMode::Replace);
        require(kernel::body_calculation_count.load()==reopen_calls,"Reopened unchanged Projection calculated geometry");
        kernel::OcctKernel cold;
        const auto regenerated=workspace::calculate_part(cold,reopened);
        require(!regenerated.empty()&&regenerated.back().calculation_errors.empty(),"Cold native reopen failed");
        require(reopened.find_container(feature.id)->feature.projection==state->session.document().find_container(feature.id)->feature.projection,"Native file lost Projection intent");
        std::filesystem::remove(path);
        if(mode==document::ProjectionOutput::Curves) {
            // Exercise the document-to-native adapters with disconnected curves,
            // rather than only sending preconstructed native edges to the kernel.
            auto mixed=source;mixed.circles.clear();mixed.points.clear();
            static_cast<void>(mixed.add_segment(-8,-8,-3,-8));
            static_cast<void>(mixed.add_segment(3,8,8,8));
            static_cast<void>(mixed.add_arc(0,0,4,0,0,4));
            static_cast<void>(mixed.add_ellipse(0,0,6,0,0,3));
            static_cast<void>(mixed.add_elliptical_arc(0,0,5,0,0,2,5,0,0,2));
            static_cast<void>(mixed.add_bspline({{-8,0},{-4,6},{4,-6},{8,0}},3));
            static_cast<void>(mixed.add_bspline({{-3,-3},{3,-3},{3,3},{-3,3}},3,true));
            workspace::commit_profile(live,engine,id,feature,workspace::ProfileEditMode::Replace,mixed);
            require(state->session.calculated_boundaries().back().calculation_errors.empty(),"Mixed disconnected Sketch curves did not project");
            const auto& edges=state->session.calculated_boundaries().back().mesh.original_references.edges;
            for(const auto& edge:edges)if(edge.reference.owner_id==feature.id)
                for(const auto& point:edge.points)require(std::abs(point.z-11)<1e-6,"Document adapter projected outside the bounded target plane");
        }
        std::cout<<"PASS document output "<<static_cast<int>(mode)<<'\n';
    }
    verify_external_projection();
    std::cout<<"Projection document contracts passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
