#include "profile_solid_fixture.hpp"
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/body_operations.hpp>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <tuple>

using namespace zima;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(double value,double expected,const char* message){if(std::abs(value-expected)>=1e-7)throw std::runtime_error(std::string(message)+": expected "+std::to_string(expected)+", got "+std::to_string(value));}
template<class F> void fails(F&& f,const char* message){bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed,message);}
double measured_volume(const kernel::OcctKernel& kernel,kernel::BodyResult body){
    kernel.ensure_body_properties(body);return body.volume;
}
auto face_ids(const kernel::ViewerMesh& mesh){
    std::set<std::tuple<std::string,std::string,std::string>> ids;
    for(const auto& face:mesh.original_references.triangle_references)
        ids.emplace(face.owner_id,face.semantic_key,face.instance_path);
    return ids;
}
// Select a known geometric corner; never derive its persisted identity from enumeration.
kernel::ViewerPoint upper_corner(const kernel::ViewerReferenceGeometry& geometry,const std::string& owner){
    const auto point=std::find_if(geometry.points.begin(),geometry.points.end(),[&](const auto& p){
        return p.reference.owner_id==owner && p.position.x>0 && p.position.y>0 && p.position.z>0;
    });
    require(point!=geometry.points.end(),"Fixture has no positive corner");return *point;
}
document::ConstructionReference reference_to(const kernel::ViewerPoint& point){
    return {point.reference.instance_path,point.reference.owner_id,point.reference.semantic_key};
}
void verify_reference_chain(const kernel::OcctKernel& kernel){
    auto part=document::PartDocument::create_default();
    auto first=zima::test::rectangular_feature(part,{10,10,10});
    auto second=zima::test::rectangular_feature(part,{2,2,2});
    auto third=zima::test::rectangular_feature(part,{2,2,2});
    part.history={first,second,third};
    // Obtain the persisted original point IDs from calculated source objects.
    const auto initial=workspace::calculate_part(kernel,part);
    const auto geometry=initial.back().mesh.original_references;
    part.history[1].placement.references={reference_to(upper_corner(geometry,first.id))};
    part.history[2].placement.references={reference_to(upper_corner(geometry,second.id))};
    auto section=document::create_section();
    section.placement.references={reference_to(upper_corner(geometry,third.id))};
    part.sections={section};
    auto calculated=workspace::calculate_part_with_resolved_references(kernel,part);
    near(part.history[1].placement.x,5,"First reference link did not converge");
    near(part.history[2].placement.x,6,"Second reference link did not converge");
    near(part.sections.front().plane_origin.x,7,"Section did not follow the final calculated link");
    require(calculated.back().calculation_errors.empty(),"Valid placement chain failed calculation");
    const auto original_ids=face_ids(calculated.back().mesh);
    zima::test::resize_rectangular_feature(part,part.history[0],{20,10,10});
    workspace::Workspace live;live.add_part(part,calculated);const auto id=part.document_id;
    const auto revision=live.open_part(id)->session.revision();
    static_cast<void>(workspace::regenerate_part(live,kernel,id));
    auto* state=live.open_part(id);
    near(state->session.document().history[1].placement.x,10,"Source edit did not move first link");
    near(state->session.document().history[2].placement.x,11,"Source edit did not move second link");
    near(state->session.document().sections.front().plane_origin.x,12,"Section retained an old source point");
    require(state->session.revision()!=revision&&state->session.can_undo(),"Changed placement did not commit one history transaction");
    require(face_ids(state->session.calculated_boundaries().back().mesh)==original_ids,"Regeneration replaced persisted source face IDs");
    const auto stable_revision=state->session.revision();
    const auto stable_history=state->session.document().history;
    static_cast<void>(workspace::regenerate_part(live,kernel,id));
    require(state->session.revision()==stable_revision&&state->session.document().history==stable_history,"Unchanged regeneration created another Undo step");
    require(state->session.undo(),"Placement regeneration cannot be undone");
    near(state->session.document().history[1].placement.x,5,"Undo lost the previous placement");
    require(state->session.redo(),"Placement regeneration cannot be redone");
    near(state->session.document().history[2].placement.x,11,"Redo lost the recalculated placement");
}
// A manually positioned owner may already be unchanged while its owned
// Sketch still carries the preceding resolved frame (including loaded data).
void verify_profile_frame_convergence(const kernel::OcctKernel& kernel){
    for(bool multi_body:{false,true})for(double x:{-2.0,2.0})for(double angle:{0.0,90.0}) {
        auto part=document::PartDocument::create_default();document::BodyHistoryGraph graph;
        if(multi_body)static_cast<void>(graph.create_body("First"));
        auto first=test::rectangular_feature(part,{8,6,4});
        if(multi_body)graph.insert({document::PartHistoryKind::Feature,first.id});
        auto cut=test::rectangular_feature(part,{2,1,4});
        cut.combine_mode=document::CombineMode::Subtract;cut.placement.x=x;
        cut.placement.rotation_z=angle;cut.placement.absolute_rotation_z=angle;
        if(multi_body)graph.insert({document::PartHistoryKind::Feature,cut.id});
        part.history={first,cut};
        if(multi_body) {
            static_cast<void>(graph.create_body("Later"));
            auto later=test::rectangular_feature(part,{8,6,4});later.placement.x=80;
            graph.insert({document::PartHistoryKind::Feature,later.id});part.history.push_back(later);
            part.set_body_history(graph);
        }
        const auto initial=workspace::calculate_part(kernel,part);
        workspace::Workspace live;live.add_part(part,initial);
        static_cast<void>(workspace::regenerate_part(live,kernel,part.document_id));
        auto& session=live.open_part(part.document_id)->session;
        const auto& resolved=session.document();const auto& result=session.calculated_boundaries();
        const auto fingerprints=kernel::history_fingerprints(resolved.kernel_operations(false,true));
        require(result.size()+1==fingerprints.size(),"Frame convergence lost a history boundary");
        for(std::size_t i=0;i<result.size();++i)
            require(result[i].source_fingerprint==fingerprints[i+1],"Profile frame and calculated history disagree");
        near(measured_volume(kernel,result.back()),multi_body?376:184,"Resolved subtraction has incorrect volume");
        double min_x=1e100,max_x=-1e100,min_y=1e100,max_y=-1e100;
        for(const auto& point:result.back().mesh.original_references.points)if(point.reference.owner_id==cut.id) {
            min_x=std::min(min_x,point.position.x);max_x=std::max(max_x,point.position.x);
            min_y=std::min(min_y,point.position.y);max_y=std::max(max_y,point.position.y);
        }
        const double half_x=angle==0?1:0.5,half_y=angle==0?0.5:1;
        near(min_x,x-half_x,"Cut references retained the old X frame");near(max_x,x+half_x,"Cut X extent is wrong");
        near(min_y,-half_y,"Cut references retained the old rotation");near(max_y,half_y,"Cut Y extent is wrong");
        std::vector<kernel::BodyResult> restored;
        const auto reopened=document::PartDocument::from_serialized(resolved.serialized(result),&restored);
        require(face_ids(restored.back().mesh)==face_ids(result.back().mesh),"Save/reopen changed source identities");
        near(measured_volume(kernel,restored.back()),measured_volume(kernel,result.back()),"Save/reopen changed calculated geometry");
        require(reopened.find_container(cut.id)->placement.x==x,"Save/reopen lost authored placement");
        const auto fingerprint=result.back().source_fingerprint;
        const auto stable_revision=session.revision();
        static_cast<void>(workspace::regenerate_part(live,kernel,part.document_id));
        require(session.revision()==stable_revision,"Stable frame regeneration created an Undo step");
        require(session.calculated_boundaries().back().source_fingerprint==fingerprint,"Stable frame regenerated different inputs");
        require(session.undo(),"Frame-only regeneration was not undoable");
        require(session.calculated_boundaries().back().source_fingerprint==initial.back().source_fingerprint,"Undo lost original calculation");
        require(session.redo(),"Frame-only regeneration was not redoable");
        require(session.calculated_boundaries().back().source_fingerprint==fingerprint,"Redo lost resolved calculation");
    }
}
void verify_recovery_policy(const kernel::OcctKernel& kernel){
    auto part=document::PartDocument::create_default();
    auto box=zima::test::rectangular_feature(part,{10,10,10});
    auto broken=document::PartDocument::create_extrusion_container("missing-profile");
    broken.extrusion.sketch_id="missing-profile";part.history={box,broken};
    const auto recovered=workspace::calculate_part(kernel,part);
    near(measured_volume(kernel,recovered.back()),1000,"Recovery erased valid input geometry");
    require(recovered.back().calculation_errors.contains(broken.id),"Recovery lost the failed feature owner");
    workspace::PartCalculationPolicy policy{true,part.document_id,0};
    static_cast<void>(workspace::calculate_part(kernel,part,nullptr,policy));
    policy.edited_history_limit=1;
    fails([&]{static_cast<void>(workspace::calculate_part(kernel,part,nullptr,policy));},"Editing a failed feature did not reject calculation");
    policy.edited_history_limit.reset();
    fails([&]{static_cast<void>(workspace::calculate_part(kernel,part,nullptr,policy));},"Creation accepted a failed calculation");
    policy.edited_history_limit=0;policy.edited_document_id="another-part";
    fails([&]{static_cast<void>(workspace::calculate_part(kernel,part,nullptr,policy));},"Another document inherited rollback error filtering");
    workspace::Workspace live;live.add_part(part,recovered);
    const auto generation=live.open_part(part.document_id)->session.data_generation();
    fails([&]{static_cast<void>(workspace::regenerate_part(live,kernel,part.document_id,policy));},"Strict regeneration unexpectedly committed");
    require(live.open_part(part.document_id)->session.data_generation()==generation,"Rejected regeneration changed the session");
    static_cast<void>(workspace::regenerate_part(live,kernel,part.document_id));
    near(measured_volume(kernel,live.open_part(part.document_id)->session.calculated_boundaries().back()),1000,"Explicit recovery lost valid geometry");
    fails([&]{static_cast<void>(workspace::regenerate_part(live,kernel,"missing"));},"Missing Part was accepted");
}
void verify_deferred_history_boundary(const kernel::OcctKernel& kernel){
    auto part=document::PartDocument::create_default();
    auto first=zima::test::rectangular_feature(part,{10,10,10});
    auto second=zima::test::rectangular_feature(part,{6,6,6});second.placement.x=4;
    auto third=zima::test::rectangular_feature(part,{4,4,4});third.placement.x=-4;
    part.history={first,second,third};
    document::BodyHistoryGraph graph;
    static_cast<void>(graph.create_body("Deferred history"));
    for(const auto& feature:part.history)
        graph.insert({document::PartHistoryKind::Feature,feature.id});
    part.set_body_history(std::move(graph));
    auto complete=workspace::calculate_part(kernel,part);
    while(const auto deferred=workspace::deferred_part_history_boundary(
            part,complete))
        require(workspace::complete_part_history_boundary(
            kernel,part,complete,*deferred),
            "History fixture could not complete its baseline boundary");
    require(complete.size()==3&&std::ranges::all_of(complete,[](const auto& boundary) {
        return !boundary.mesh.triangles.empty();
    }),"History fixture did not start with complete boundaries");

    auto edited=part;
    zima::test::resize_rectangular_feature(edited,edited.history.front(),{12,10,10});
    auto policy=workspace::feature_definition_calculation_policy(
        part,complete,first.id);
    auto deferred=workspace::calculate_part(kernel,edited,&complete,policy);
    require(deferred.size()==complete.size()&&
        deferred[1].mesh.triangles.empty()&&
        !deferred.back().mesh.triangles.empty(),
        "Existing feature edit did not prioritize the final boundary");
    require(workspace::deferred_part_history_boundary(edited,deferred)==1,
        "Idle completion did not identify the first deferred boundary");
    const auto final_volume=measured_volume(kernel,deferred.back());
    const auto final_fingerprint=deferred.back().source_fingerprint;
    const auto final_ids=face_ids(deferred.back().mesh);

    auto cursor_calculation_document=edited;
    cursor_calculation_document.set_history_cursor(2);
    const auto cursor_calculated=workspace::calculate_part(
        kernel,cursor_calculation_document,&complete,policy);
    const auto active_body=cursor_calculation_document.body_history.
        active_body_id();
    const auto retained=cursor_calculated.back().body_boundaries.find(
        active_body);
    require(retained!=cursor_calculated.back().body_boundaries.end()&&
            retained->second.size()==3&&
            !retained->second[1].mesh.triangles.empty()&&
            !cursor_calculated.back().mesh.triangles.empty(),
        "Calculation at a history cursor did not publish its visible Body");
    std::vector<kernel::BodyResult> reopened_cursor_boundaries;
    const auto reopened_cursor=document::PartDocument::from_serialized(
        cursor_calculation_document.serialized(cursor_calculated),
        &reopened_cursor_boundaries);
    document::DocumentSession reopened_cursor_session(
        reopened_cursor,std::move(reopened_cursor_boundaries));
    require(!reopened_cursor_session.body_context_mesh().triangles.empty(),
        "Save/reopen lost the calculated Body visible at the history cursor");

    {
        workspace::Workspace cursor_live;
        cursor_live.add_part(edited,deferred);
        auto* cursor_state=cursor_live.open_part(edited.document_id);
        const auto body_id=
            cursor_state->session.document().body_history.active_body_id();
        const auto revision=cursor_state->session.revision();
        const auto change=workspace::set_body_history_cursor(
            cursor_live,kernel,edited.document_id,2,body_id);
        require(change.changed&&change.boundary_materialized&&
            cursor_state->session.revision()==revision+1&&
            !cursor_state->session.body_context_mesh().triangles.empty(),
            "History cursor did not materialize its exact visible Body");
        require(measured_volume(kernel,
                cursor_state->session.calculated_boundaries().back())==
                    final_volume&&
            cursor_state->session.calculated_boundaries().back().
                source_fingerprint==final_fingerprint&&
            face_ids(cursor_state->session.calculated_boundaries().back().mesh)==
                final_ids,
            "History cursor materialization changed the final result");
        const auto generation=cursor_state->session.data_generation();
        const auto* allocation=
            cursor_state->session.calculated_boundaries().data();
        const auto same=workspace::set_body_history_cursor(
            cursor_live,kernel,edited.document_id,2,body_id);
        require(!same.changed&&!same.boundary_materialized&&
            cursor_state->session.data_generation()==generation&&
            cursor_state->session.calculated_boundaries().data()==allocation,
            "Unchanged history cursor recalculated or created a transaction");
        require(cursor_state->session.undo()&&
                cursor_state->session.document().body_history.find(body_id)->
                    cursor==3&&
                cursor_state->session.redo()&&
                cursor_state->session.document().body_history.find(body_id)->
                    cursor==2&&
                !cursor_state->session.body_context_mesh().triangles.empty(),
            "History cursor Undo/Redo lost the materialized Body");
    }

    workspace::Workspace live;live.add_part(edited,std::move(deferred));
    auto* state=live.open_part(edited.document_id);
    const auto revision=state->session.revision();
    const auto generation=state->session.data_generation();
    const auto authored=state->session.document().serialized();
    const auto* document_address=&state->session.document();
    const auto* container_address=state->session.document().find_container(first.id);
    const bool undo_before=state->session.can_undo();
    require(workspace::materialize_part_history_input_boundary(
        live,kernel,edited.document_id,third.id),
        "Deferred input boundary was not materialized for Properties");
    require(!state->session.calculated_boundaries()[1].mesh.triangles.empty(),
        "Materialized rollback boundary has no viewer geometry");
    require(state->session.revision()==revision&&
        state->session.can_undo()==undo_before&&
        state->session.document().serialized()==authored,
        "Boundary materialization changed authored history or Undo");
    require(&state->session.document()==document_address&&
        state->session.document().find_container(first.id)==container_address,
        "Cache-only boundary materialization invalidated editor document pointers");
    require(state->session.data_generation()==generation+1,
        "Published rollback cache did not advance its data generation");
    require(measured_volume(kernel,state->session.calculated_boundaries().back())==final_volume&&
        state->session.calculated_boundaries().back().source_fingerprint==final_fingerprint&&
        face_ids(state->session.calculated_boundaries().back().mesh)==final_ids,
        "Boundary materialization changed the final calculated result");
    const auto completed_generation=state->session.data_generation();
    require(!workspace::materialize_part_history_input_boundary(
            live,kernel,edited.document_id,third.id)&&
        state->session.data_generation()==completed_generation,
        "Complete rollback boundary was calculated or published twice");
    auto background_candidate=state->session.calculated_boundaries();
    require(workspace::complete_part_history_boundary(
            kernel,state->session.document(),background_candidate,1)&&
        !workspace::deferred_part_history_boundary(
            state->session.document(),background_candidate)&&
        measured_volume(kernel,background_candidate.back())==final_volume&&
        face_ids(background_candidate.back().mesh)==final_ids,
        "Private idle completion changed the final result or left a deferred row");
}
void verify_retained_edge_treatment_boundary(const kernel::OcctKernel& kernel){
    auto part=document::PartDocument::create_default();
    auto stock=test::rectangular_feature(part,{10,10,10});
    auto fillet=document::PartDocument::create_fillet_container({{
        stock.id,test::profile_key(part,stock,
            "edge:x_max:y_min:z_max--x_max:y_min:z_min"),{}}});
    fillet.edge_treatment.primary_size=1;
    auto cut=test::rectangular_feature(part,{2,2,2});
    cut.combine_mode=document::CombineMode::Subtract;
    part.history={stock,fillet,cut};
    auto complete=workspace::calculate_part(kernel,part);
    while(const auto deferred=workspace::deferred_part_history_boundary(
            part,complete))
        require(workspace::complete_part_history_boundary(
            kernel,part,complete,*deferred),
            "Edge-treatment fixture could not complete its baseline boundary");
    require(complete.size()==3&&std::ranges::all_of(complete,[](const auto& boundary) {
        return !boundary.mesh.triangles.empty();
    }),"Edge-treatment history fixture did not start with complete boundaries");

    auto edited=part;
    test::resize_rectangular_feature(edited,edited.history.front(),{12,10,10});
    const auto policy=workspace::feature_definition_calculation_policy(
        part,complete,stock.id);
    auto deferred=workspace::calculate_part(kernel,edited,&complete,policy);
    const auto reference=workspace::calculate_part(kernel,edited);
    require(deferred.size()==3&&
        deferred[0].mesh.triangles.empty()&&!deferred[0].mesh.edges.empty()&&
        !deferred[1].mesh.triangles.empty()&&!deferred[1].mesh.edges.empty()&&
        !deferred.back().mesh.triangles.empty(),
        "Final-first edit lost the identity-bearing Fillet output");
    require(std::abs(measured_volume(kernel,deferred.back())-
            measured_volume(kernel,reference.back()))<1e-8,
        "Deferring an intermediate Fillet packet changed the final volume");
    require(face_ids(deferred.back().mesh)==face_ids(reference.back().mesh),
        "Deferring an intermediate Fillet packet changed final face identities");

    require(face_ids(deferred[1].mesh)==face_ids(reference[1].mesh),
        "Retained Fillet output changed its own face identities");
}
void verify_assembly(const kernel::OcctKernel& kernel){
    auto part=document::PartDocument::create_default();
    auto box=zima::test::rectangular_feature(part,{10,10,10});part.history={box};
    workspace::Workspace live;live.add_part(part,workspace::calculate_part(kernel,part));
    auto assembly=assembly::AssemblyDocument::create_default();const auto id=assembly.document_id;
    live.add_assembly(assembly);
    const auto occurrence=live.insert_open_part(id,part.document_id,"source");
    live.activate(part.document_id);live.display_top_level(id);
    auto changed=part;zima::test::resize_rectangular_feature(changed,changed.history.front(),{20,10,10});
    live.open_part(part.document_id)->session.commit(changed,workspace::calculate_part(kernel,changed));
    near(measured_volume(kernel,live.open_assembly(id)->session.document().find_occurrence(occurrence)->calculated_source.get()),1000,"Source edit implicitly regenerated parent");
    workspace::regenerate_assembly(live,kernel,id);
    const auto* parent=live.open_assembly(id);
    near(measured_volume(kernel,parent->session.document().find_occurrence(occurrence)->calculated_source.get()),2000,"Explicit Assembly regeneration ignored unsaved Part");
    require(live.open_part(part.document_id)->session.is_dirty(),"Assembly regeneration saved a source document");
    require(live.active_document_id()==part.document_id&&live.displayed_document_id()==id,"Regeneration changed editing/display ownership");
    require(parent->session.document().components.size()==1&&parent->session.document().components.front().occurrence_id==occurrence,"Regeneration replaced occurrence identity");
    // A centered 2x20x40 cutter removes a 2x10x10 slice: 2000 - 200 = 1800.
    auto cut_model=parent->session.document();
    auto sketch=sketcher::Sketch::create_default();
    static_cast<void>(sketch.add_rectangle(-1,-10,1,10));
    auto cutter=document::PartDocument::create_extrusion_container(sketch.id);
    cutter.extrusion.extent_mode=document::ProfileExtentMode::Symmetric;
    cutter.extrusion.length_forward=20;cutter.extrusion.height=20;
    cutter.combine_mode=document::CombineMode::Subtract;
    cutter.placement.x=30;
    cutter.placement.references={{{},id+":origin","origin:point"}};
    sketch.owner_container_id=cutter.id;cut_model.sketches.push_back(sketch);
    assembly::AssemblyCut cut;cut.definition=cutter;cut.target_occurrence_ids={occurrence};cut_model.cuts.push_back(cut);
    workspace::calculate_resolved_assembly_cuts(kernel,cut_model);
    near(cut_model.cuts.front().definition.placement.x,0,"Assembly cutter reference was not resolved");
    near(measured_volume(kernel,cut_model.find_occurrence(occurrence)->calculated_source.get()),1800,"Assembly cutter volume is incorrect");
    near(measured_volume(kernel,live.open_part(part.document_id)->session.calculated_boundaries().back()),2000,"Assembly cutter modified its source Part");
    require(cut_model.cuts.front().input_component_bodies.contains(occurrence),"Cut lost the real rollback input");
    require(cut_model.cuts.front().definition.id==cutter.id,"Cut calculation changed feature identity");
}
}
int main(){try{
    kernel::OcctKernel kernel;
    verify_reference_chain(kernel);verify_profile_frame_convergence(kernel);verify_recovery_policy(kernel);
    verify_deferred_history_boundary(kernel);verify_retained_edge_treatment_boundary(kernel);
    verify_assembly(kernel);
    std::cout<<"Model calculation: reference convergence, deferred rollback materialization, source identity, Undo, error policy, explicit Assembly refresh and cuts passed without Qt.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
