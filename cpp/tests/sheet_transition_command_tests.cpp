#include <zima/workspace/sheet_transition_operations.hpp>
#include <zima/workspace/sheet_state_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/sheet_exchange_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/workspace/sketch_properties.hpp>
#include <zima/document/sheet_transition.hpp>
#include <chrono>
#include <iostream>
using namespace zima;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
}
int main()try {
    auto part=document::PartDocument::create_default();
    static_cast<void>(part.body_history.create_body("Transition test"));
    kernel::OcctKernel kernel;workspace::Workspace live;const auto id=part.document_id;
    live.add_part(part,workspace::calculate_part_with_resolved_references(kernel,part));live.activate(id);
    auto feature=document::create_sheet_transition();feature.name="Transition";
    const auto start=std::chrono::steady_clock::now();
    check(workspace::commit_sheet_transition(live,kernel,id,feature),"Creation did not commit");
    auto* state=live.open_part(id);const auto created=state->session.document().serialized();
    const auto volume=state->session.calculated_boundaries().back().volume;check(volume>1000,"Missing transition solid");
    check(!workspace::commit_sheet_transition(live,kernel,id,feature),"Unchanged OK created an Undo transaction");
    check(workspace::step_part_document_history(live,id,false),"Undo failed");
    check(!state->session.document().find_container(feature.id),"Undo retained feature");
    check(workspace::step_part_document_history(live,id,true),"Redo failed");
    check(state->session.document().serialized()==created,"Redo changed feature");
    auto invalid=feature;invalid.sheet_transition.inside_radius=10000;
    bool rejected=false;try{static_cast<void>(workspace::commit_sheet_transition(live,kernel,id,invalid));}catch(const std::exception&){rejected=true;}
    check(rejected&&state->session.document().serialized()==created,"Invalid edit mutated document");
    auto edited=feature;edited.sheet_transition.facets={6,3};edited.sheet_transition.thickness=1.2;
    edited.sheet_transition.inside_radius=1.4;edited.sheet_transition.k_factor=.4;
    check(workspace::commit_sheet_transition(live,kernel,id,edited),"Parameter edit did not commit");
    check(state->session.document().find_container(feature.id)->sheet_transition==edited.sheet_transition,"Parameter edit lost values");
    check(state->session.calculated_boundaries().back().volume>volume,"Thickness edit did not change solid");
    check(workspace::step_part_document_history(live,id,false)&&state->session.document().serialized()==created,"Edit Undo failed");
    auto moved=feature;moved.sheet_transition.end_position.z=200;
    check(workspace::commit_sheet_transition(live,kernel,id,moved),"Second Origin movement did not commit");
    check(state->session.calculated_boundaries().back().calculation_errors.empty()&&state->session.calculated_boundaries().back().volume>volume,"Second Origin movement did not regenerate transition");
    check(workspace::step_part_document_history(live,id,false)&&state->session.document().serialized()==created,"Origin edit Undo failed");
    auto placed=feature;placed.placement.x=23;placed.placement.y=-11;placed.placement.z=8;
    placed.placement.rotation_x=17;placed.placement.rotation_y=-13;placed.placement.rotation_z=27;
    placed.placement.absolute_rotation_x=17;placed.placement.absolute_rotation_y=-13;placed.placement.absolute_rotation_z=27;
    check(workspace::commit_sheet_transition(live,kernel,id,placed),"Rigid placement did not commit");
    check(std::abs(state->session.calculated_boundaries().back().volume-volume)<1e-4,"Rigid placement changed volume");
    check(workspace::step_part_document_history(live,id,false)&&state->session.document().serialized()==created,"Rigid placement Undo failed");
    auto tilted=feature;tilted.sheet_transition.end_rotation.y=15;
    check(workspace::commit_sheet_transition(live,kernel,id,tilted),"Relative Origin tilt did not commit");
    check(state->session.calculated_boundaries().back().calculation_errors.empty(),"Compatible Origin tilt failed");
    check(workspace::step_part_document_history(live,id,false)&&state->session.document().serialized()==created,"Relative tilt Undo failed");
    const auto directory=std::filesystem::absolute("build/transition-model");std::filesystem::create_directories(directory);
    const auto path=directory/"native-transition.prtz";
    state->session.document().save(path,state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> cache;auto loaded=document::PartDocument::load(path,&cache);
    check(loaded.find_container(feature.id)->sheet_transition==feature.sheet_transition,"Native file lost parameters");
    kernel::OcctKernel cold;const auto recalculated=workspace::calculate_part_with_resolved_references(cold,loaded);
    check(!recalculated.empty()&&recalculated.back().calculation_errors.empty(),"Cold regeneration failed");
    check(std::abs(recalculated.back().volume-volume)<1e-5,"Cold regeneration changed volume");
    const auto dxf=workspace::prepare_sheet_dxf(state->session.document(),state->session.calculated_boundaries(),kernel);
    check(!dxf.contour.segments.empty()||!dxf.contour.bsplines.empty(),"DXF has no flat contour");
    for(bool unfold:{true,false}) {
        auto operation=document::PartDocument::create_sketch_container();
        operation.feature_kind=unfold?document::FeatureKind::Unbend:document::FeatureKind::BendBack;
        operation.sheet_state.all=false;operation.sheet_state.owners={feature.id};
        check(workspace::commit_sheet_state(live,kernel,id,operation),"State command did not commit");
        check(std::abs(state->session.calculated_boundaries().back().volume-volume)<volume*1e-4,"State command changed volume");
        std::size_t axes=0;
        for(const auto& source:workspace::drawing_annotation_sources(&live,id,{}))for(const auto& axis:source.axes)
            if(kernel::sheet_material::is_bend_line(axis.reference))++axes;
        check(unfold?axes>=6:axes==0,"Drawing offers wrong bend axes for material state");
    }
    std::cout<<"Native transition create/edit/save/reopen/Undo/Redo/state: "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
