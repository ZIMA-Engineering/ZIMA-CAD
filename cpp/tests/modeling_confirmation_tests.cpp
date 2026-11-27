#include "profile_solid_fixture.hpp"
#include <zima/workspace/modeling_confirmation.hpp>
#include <zima/workspace/edge_treatment_operations.hpp>
#include <zima/workspace/shell_operations.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <iostream>
#include <filesystem>
using namespace zima;
namespace {
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
auto authored(nlohmann::json value) {
    // Dimension numbers are deliberately never recycled by Undo. Retained
    // allocations change the derived definition fingerprint, not the model.
    value.erase("dimension_identifiers");value.erase("calculation_definition_fingerprint");
    return value;
}
void verify(bool edit,bool chamfer,bool stale_endpoint=false) {
    auto part=document::PartDocument::create_default();
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Confirmation"));part.set_body_history(graph);
    auto box=test::rectangular_feature(part,{40,30,20});
    part.insert_history_entry(document::PartHistoryKind::Feature,box.id);part.history.push_back(box);
    kernel::OcctKernel kernel;auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
    check(!cache.empty()&&cache.back().calculation_errors.empty(),"Confirmation input calculation failed");
    const auto edge=std::ranges::find_if(cache.back().mesh.edges,[](const auto& value){return value.reference.valid()&&value.edge_treatment_side_references.size()==2;});
    check(edge!=cache.back().mesh.edges.end(),"Confirmation input edge missing");
    auto feature=chamfer?document::PartDocument::create_chamfer_container({edge->reference}):
        document::PartDocument::create_fillet_container({edge->reference});
    feature.edge_treatment.primary_size=2.;
    feature.edge_treatment.calculation_method=document::EdgeTreatmentParameters::CalculationMethod::Zima;
    workspace::Workspace live;live.add_part(part,cache);live.activate(part.document_id);
    auto* state=live.open_part(part.document_id);
    if(edit)static_cast<void>(workspace::commit_edge_treatment(live,kernel,part.document_id,feature,workspace::EdgeTreatmentEditMode::Create));
    check(state->session.calculated_boundaries().back().calculation_errors.empty(),"Confirmation valid feature failed");
    const auto before=state->session.document().serialized();const auto revision=state->session.revision();
    const auto* calculated_data=state->session.calculated_boundaries().data();
    workspace::ModelingConfirmation confirmation;
    int calculations{};
    workspace::CalculationExecutionScope counted([&](auto action){++calculations;action();});
    feature.edge_treatment.primary_size=stale_endpoint?2.:1000.;
    if(stale_endpoint) {
        auto& ends=feature.edge_treatment.route_end_faces.front();
        ends.first_vertex=edge->edge_treatment_endpoint_references.front();
        ends.first_vertex.semantic_key+=":missing";
        ends.first_mode=document::EdgeTreatmentParameters::RouteEndMode::Cut;
        bool rejected{};
        try {static_cast<void>(workspace::commit_edge_treatment(live,kernel,part.document_id,feature,
            edit?workspace::EdgeTreatmentEditMode::Replace:workspace::EdgeTreatmentEditMode::Create));}
        catch(const workspace::EdgeTreatmentOperationError&){rejected=true;}
        check(rejected&&state->session.revision()==revision,"Native invalid endpoint preflight was weakened");
    }
    const auto submit=[&](bool expected_failure) {
        workspace::ModelingConfirmationAttempt attempt(confirmation,live,part.document_id);
        bool rejected{};
        try {static_cast<void>(workspace::commit_edge_treatment(live,kernel,part.document_id,feature,
            edit?workspace::EdgeTreatmentEditMode::Replace:workspace::EdgeTreatmentEditMode::Create));}
        catch(const std::exception&){rejected=true;}
        check(rejected==expected_failure,"Wrong first/second modeling confirmation result");
        check(attempt.failed()==expected_failure,"Calculation failure was not acknowledged by its attempt");
    };
    submit(true);
    check(!confirmation.failure().empty()&&state->session.revision()==revision&&
        state->session.document().serialized()==before&&state->session.calculated_boundaries().data()==calculated_data,
        "First failed OK changed the document, geometry or Undo revision");
    const auto after_first=calculations;
    feature.edge_treatment.primary_size=stale_endpoint?2.5:900.;submit(true);
    check(calculations>after_first,"Changed failed parameters reused the previous acknowledgement");
    const auto after_change=calculations;
    // A source recalculation invalidates the earlier acknowledgement even
    // when its authored document revision has not changed.
    state->session.update_calculated_boundaries(state->session.calculated_boundaries());
    submit(true);check(calculations>after_change,"Changed input generation reused a stale failure");
    const auto confirmed_count=calculations;submit(false);
    check(calculations==confirmed_count,"Second unchanged OK recalculated the failed geometry");
    check(state->session.revision()==revision+1&&state->session.document().find_container(feature.id)&&
        state->session.calculated_boundaries().back().calculation_errors.contains(feature.id),
        "Second OK did not retain one unresolved native definition");
    const auto file=std::filesystem::path("build")/(std::string("confirmation-")+(stale_endpoint?"endpoint-":"")+(edit?"edit-":"new-")+(chamfer?"chamfer.prtz":"fillet.prtz"));
    state->session.document().save(file,state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> reopened_cache;auto reopened=document::PartDocument::load(file,&reopened_cache);
    check(reopened.find_container(feature.id)->edge_treatment==feature.edge_treatment&&
        !reopened_cache.empty()&&reopened_cache.back().calculation_errors.contains(feature.id),
        "Save/reopen lost the unresolved definition or error");
    const auto regenerated=workspace::calculate_part_with_resolved_references(kernel,reopened);
    check(!regenerated.empty()&&regenerated.back().calculation_errors.contains(feature.id)&&
        reopened.find_container(feature.id)->edge_treatment==feature.edge_treatment,
        "Cold regeneration lost the unresolved authored definition");
    for(int repeat=0;repeat<2;++repeat) {
        check(state->session.undo(),"Undo is unavailable");
        const auto restored=state->session.document().serialized();
        check(authored(restored)==authored(before),"Undo failed to restore the previous model");
        for(const auto& allocation:before.at("dimension_identifiers").at("entries"))
            check(std::ranges::find(restored.at("dimension_identifiers").at("entries"),allocation)!=
                restored.at("dimension_identifiers").at("entries").end(),"Undo renumbered an existing dimension");
        check(state->session.redo()&&state->session.calculated_boundaries().back().calculation_errors.contains(feature.id),"Redo failed to retain unresolved definition");
    }
    check(state->session.undo(),"Cannot restore input for Cancel test");
    confirmation.clear();submit(true);confirmation.clear();submit(true);
    check(authored(state->session.document().serialized())==authored(before),"Cancelled failed draft changed the document");
}
void verify_shell(bool edit) {
    auto part=document::PartDocument::create_default();
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Shell confirmation"));part.set_body_history(graph);
    auto box=test::rectangular_feature(part,{10,10,10});
    part.insert_history_entry(document::PartHistoryKind::Feature,box.id);part.history.push_back(box);
    kernel::OcctKernel kernel;auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
    workspace::Workspace live;live.add_part(part,cache);live.activate(part.document_id);
    auto* state=live.open_part(part.document_id);
    auto feature=document::PartDocument::create_shell_container();feature.shell.thickness=1.;
    if(edit)static_cast<void>(workspace::commit_shell(live,kernel,part.document_id,feature,workspace::ShellEditMode::Create));
    const auto before=state->session.document().serialized();const auto revision=state->session.revision();
    feature.shell.thickness=6.;workspace::ModelingConfirmation confirmation;
    int calculations{};workspace::CalculationExecutionScope counted([&](auto action){++calculations;action();});
    for(bool first:{true,false}) {
        const auto count=calculations;bool rejected{};
        workspace::ModelingConfirmationAttempt attempt(confirmation,live,part.document_id);
        try{static_cast<void>(workspace::commit_shell(live,kernel,part.document_id,feature,
            edit?workspace::ShellEditMode::Replace:workspace::ShellEditMode::Create));}
        catch(const std::exception&){rejected=true;}
        check(rejected==first&&attempt.failed()==first,"Shell did not use the common two-OK confirmation");
        if(first)check(state->session.revision()==revision&&state->session.document().serialized()==before,
            "First failed Shell confirmation changed the document");
        else check(calculations==count&&state->session.revision()==revision+1&&
            state->session.calculated_boundaries().back().calculation_errors.contains(feature.id),
            "Second Shell confirmation recalculated or lost its unresolved definition");
    }
    for(int repeat=0;repeat<2;++repeat) {
        check(state->session.undo()&&authored(state->session.document().serialized())==authored(before),"Shell confirmation Undo failed");
        check(state->session.redo()&&state->session.calculated_boundaries().back().calculation_errors.contains(feature.id),"Shell confirmation Redo failed");
    }
}

void verify_assembly_profile(bool edit,bool shared_feature,bool revolve) {
    auto part=document::PartDocument::create_default();
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Source"));part.set_body_history(graph);
    auto box=test::rectangular_feature(part,{40,30,20});
    part.insert_history_entry(document::PartHistoryKind::Feature,box.id);part.history.push_back(box);
    kernel::OcctKernel kernel;auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
    const auto source_path=std::filesystem::absolute("build/confirmation-assembly-source.prtz");
    part.save(source_path,cache);
    auto owner=assembly::AssemblyDocument::create_default();
    for(int i=0;i<2;++i)owner.components.push_back(assembly::AssemblyDocument::create_part_occurrence(
        "Target",part.document_id,source_path,cache.back()));
    owner.synchronize_history();
    auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(2,2,4,5));
    auto feature=shared_feature?document::PartDocument::create_feature_container(sketch.id):
        revolve?document::PartDocument::create_revolution_container(sketch.id):
        document::PartDocument::create_extrusion_container(sketch.id);
    feature.combine_mode=document::CombineMode::Subtract;sketch.owner_container_id=feature.id;
    feature.feature.sides[0].length=5;feature.extrusion.length_forward=5;
    if(revolve) {
        const auto axis=sketch.add_segment(0,0,0,10);
        sketch.set_segment_centerline(axis,true);
        feature.feature.axis_segment_id=axis;
        feature.feature.sides[0].operation=document::FeatureSideOperation::Revolution;
        feature.feature.sides[0].angle_degrees=360;
        feature.revolution.axis_segment_id=axis;feature.revolution.angle_degrees=360;
    }
    std::vector<std::string> targets;for(const auto& component:owner.components)targets.push_back(component.occurrence_id);
    // Existing authored Assembly cuts remain editable; creating new material
    // operations in an Assembly is forbidden by the current ownership contract.
    if(edit){owner.cuts.push_back({feature,targets});owner.sketches.push_back(sketch);owner.synchronize_history();}
    workspace::Workspace live;live.add_part(part,cache,source_path);live.add_assembly(owner);live.activate(owner.document_id);
    auto* state=live.open_assembly(owner.document_id);
    if(edit)workspace::regenerate_assembly(live,kernel,owner.document_id);
    const auto before=state->session.document().serialized();const auto revision=state->session.revision();
    const auto source_before=live.open_part(part.document_id)->session.document().serialized();
    sketch.segments.erase(sketch.segments.begin()); // Valid authored open Sketch; invalid solid cutter.
    workspace::ModelingConfirmation confirmation;
    int calculations{};workspace::CalculationExecutionScope counted([&](auto task){++calculations;task();});
    if(!edit){
        for(int retry=0;retry<2;++retry){
            workspace::ModelingConfirmationAttempt attempt(confirmation,live,owner.document_id);
            bool rejected=false;
            try{workspace::commit_assembly_profile(live,kernel,owner.document_id,feature,targets,workspace::ProfileEditMode::Create,sketch);}
            catch(const workspace::ProfileOperationError&){rejected=true;}
            check(rejected&&!attempt.failed()&&calculations==0&&state->session.revision()==revision&&
                state->session.document().serialized()==before,"Second OK bypassed Assembly material ownership");
        }
        return;
    }
    const auto submit=[&](bool failure) {
        bool rejected{};workspace::ModelingConfirmationAttempt attempt(confirmation,live,owner.document_id);
        try {workspace::commit_assembly_profile(live,kernel,owner.document_id,feature,targets,
            edit?workspace::ProfileEditMode::Replace:workspace::ProfileEditMode::Create,sketch);}
        catch(const std::exception& error){rejected=true;if(!attempt.failed())std::cerr<<"Assembly preflight: "<<error.what()<<'\n';}
        check(rejected==failure&&attempt.failed()==failure,"Assembly first/second OK failed");
    };
    submit(true);
    check(state->session.revision()==revision&&state->session.document().serialized()==before,"Failed Assembly OK changed document");
    const auto first_count=calculations;
    sketch.points.front().x+=.2;submit(true);check(calculations>first_count,"Changed Assembly draft reused failure");
    const auto count=calculations;submit(false);
    check(calculations==count&&state->session.revision()==revision+1,"Second Assembly OK calculated or duplicated transaction");
    const auto verify=[&](const assembly::AssemblyDocument& document) {
        check(document.find_cut(feature.id),"Unresolved Assembly definition missing");
        for(const auto& component:document.components) {
            const auto& body=component.calculated_source.get();
            check(body.calculation_errors.contains(feature.id),"Assembly unresolved status missing");
            check(std::abs(body.volume-cache.back().volume)<1e-7,"Unresolved cutter changed its input material");
        }
    };
    verify(state->session.document());
    const auto path=std::filesystem::path("build")/(std::string("confirmation-assembly-")+(shared_feature?"feature-":"extrusion-")+(edit?"edit.asmz":"new.asmz"));
    state->session.document().save(path);auto reopened=assembly::AssemblyDocument::load(path);verify(reopened);
    for(int repeat=0;repeat<2;++repeat) {
        check(state->session.undo()&&authored(state->session.document().serialized())==authored(before),"Assembly unresolved Undo failed");
        check(state->session.redo(),"Assembly unresolved Redo failed");verify(state->session.document());
    }
    workspace::regenerate_assembly(live,kernel,owner.document_id);verify(state->session.document());
    check(live.open_part(part.document_id)->session.document().serialized()==source_before,"Assembly confirmation changed source Part");
}
}
int main(){try {
    for(bool edit:{false,true})for(bool chamfer:{false,true})for(bool stale_endpoint:{false,true})verify(edit,chamfer,stale_endpoint);
    for(bool edit:{false,true})verify_shell(edit);
    for(bool edit:{false,true})for(bool shared:{false,true})for(bool revolve:{false,true})verify_assembly_profile(edit,shared,revolve);
    std::cout<<"Modeling confirmation: new/edit, Fillet/Chamfer, unchanged retry, changed draft/source, Cancel, persistence and Undo/Redo passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
