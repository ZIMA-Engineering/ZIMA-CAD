#include "profile_solid_fixture.hpp"
#include <zima/workspace/modeling_confirmation.hpp>
#include <zima/workspace/edge_treatment_operations.hpp>
#include <zima/workspace/shell_operations.hpp>
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
}
int main(){try {
    for(bool edit:{false,true})for(bool chamfer:{false,true})for(bool stale_endpoint:{false,true})verify(edit,chamfer,stale_endpoint);
    for(bool edit:{false,true})verify_shell(edit);
    std::cout<<"Modeling confirmation: new/edit, Fillet/Chamfer, unchanged retry, changed draft/source, Cancel, persistence and Undo/Redo passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
