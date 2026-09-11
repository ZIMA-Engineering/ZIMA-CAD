#include <zima/workspace/model_calculation.hpp>
#include <algorithm>
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
    auto first=document::PartDocument::create_box_container();first.box={10,10,10};
    auto second=document::PartDocument::create_box_container();second.box={2,2,2};
    auto third=document::PartDocument::create_box_container();third.box={2,2,2};
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
    part.history[0].box.length=20;
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
void verify_recovery_policy(const kernel::OcctKernel& kernel){
    auto part=document::PartDocument::create_default();
    auto box=document::PartDocument::create_box_container();box.box={10,10,10};
    auto broken=document::PartDocument::create_extrusion_container("missing-profile");
    broken.extrusion.sketch_id="missing-profile";part.history={box,broken};
    const auto recovered=workspace::calculate_part(kernel,part);
    near(recovered.back().volume,1000,"Recovery erased valid input geometry");
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
    near(live.open_part(part.document_id)->session.calculated_boundaries().back().volume,1000,"Explicit recovery lost valid geometry");
    fails([&]{static_cast<void>(workspace::regenerate_part(live,kernel,"missing"));},"Missing Part was accepted");
}
void verify_assembly(const kernel::OcctKernel& kernel){
    auto part=document::PartDocument::create_default();
    auto box=document::PartDocument::create_box_container();box.box={10,10,10};part.history={box};
    workspace::Workspace live;live.add_part(part,workspace::calculate_part(kernel,part));
    auto assembly=assembly::AssemblyDocument::create_default();const auto id=assembly.document_id;
    live.add_assembly(assembly);
    const auto occurrence=live.insert_open_part(id,part.document_id,"source");
    live.activate(part.document_id);live.display_top_level(id);
    auto changed=part;changed.history.front().box.length=20;
    live.open_part(part.document_id)->session.commit(changed,workspace::calculate_part(kernel,changed));
    near(live.open_assembly(id)->session.document().find_occurrence(occurrence)->calculated_source->volume,1000,"Source edit implicitly regenerated parent");
    workspace::regenerate_assembly(live,kernel,id);
    const auto* parent=live.open_assembly(id);
    near(parent->session.document().find_occurrence(occurrence)->calculated_source->volume,2000,"Explicit Assembly regeneration ignored unsaved Part");
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
    near(cut_model.find_occurrence(occurrence)->calculated_source->volume,1800,"Assembly cutter volume is incorrect");
    near(live.open_part(part.document_id)->session.calculated_boundaries().back().volume,2000,"Assembly cutter modified its source Part");
    require(cut_model.cuts.front().input_component_bodies.contains(occurrence),"Cut lost the real rollback input");
    require(cut_model.cuts.front().definition.id==cutter.id,"Cut calculation changed feature identity");
}
}
int main(){try{
    kernel::OcctKernel kernel;
    verify_reference_chain(kernel);verify_recovery_policy(kernel);verify_assembly(kernel);
    std::cout<<"Model calculation: reference convergence, sections, source identity, Undo, error policy, explicit Assembly refresh and cuts passed without Qt.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
