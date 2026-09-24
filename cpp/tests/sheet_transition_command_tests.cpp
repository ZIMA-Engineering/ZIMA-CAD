#include <zima/workspace/sheet_transition_operations.hpp>
#include <zima/workspace/sheet_state_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/sheet_exchange_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/workspace/sketch_properties.hpp>
#include <zima/document/sheet_transition.hpp>
#include <zima/document/bend.hpp>
#include <zima/workspace/bend_operations.hpp>
#include <transition_sketches.hpp>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopExp_Explorer.hxx>
#include <sstream>
#include <chrono>
#include <iostream>
using namespace zima;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void check_solid(const kernel::BodyResult& body) {
    TopoDS_Shape shape;BRep_Builder builder;std::istringstream stream(body.kernel_shape);BRepTools::Read(shape,stream,builder);
    check(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Transition has invalid B-Rep");
    unsigned solids=0;for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())++solids;
    if(solids!=1)throw std::runtime_error("Transition solid count: "+std::to_string(solids));
}
}
int main()try {
    auto part=document::PartDocument::create_default();
    static_cast<void>(part.body_history.create_body("Transition test"));
    kernel::OcctKernel kernel;workspace::Workspace live;const auto id=part.document_id;
    live.add_part(part,workspace::calculate_part_with_resolved_references(kernel,part));live.activate(id);
    auto feature=document::create_sheet_transition();feature.name="Transition";
    {
        auto rectangle=sketcher::Sketch::from_serialized(feature.sheet_transition.sketches[1]);
        const auto height=std::ranges::find_if(rectangle.dimensions,[](const auto& d){return d.kind==sketcher::DimensionKind::DistanceY;});
        check(height!=rectangle.dimensions.end()&&height->value==80,"Rectangle height uses trimmed leg instead of overall envelope");
        const auto preview=rectangle.viewer_mesh();
        check(std::ranges::any_of(preview.dimensions,[&](const auto& d){return d.reference.semantic_key=="dimension:"+height->id&&std::abs(d.value-80)<1e-8;}),"Overall height annotation disappeared from trimmed profile");
        check(rectangle.set_dimension_value(height->id,90),"Overall rectangle height is not editable");
        const auto radius=std::ranges::find_if(rectangle.corner_radii,[](const auto& c){return c.dimension_visible;});
        check(radius!=rectangle.corner_radii.end(),"Corner radius annotation is missing");
        static_cast<void>(rectangle.add_corner_fillet(radius->first_segment_id,radius->second_segment_id,18));
        check(std::ranges::all_of(rectangle.corner_radii,[](const auto& c){return std::abs(c.radius-18)<1e-8;}),"Corner radii did not stay equal");
        const auto parsed=research::transition::read_sketches(sketcher::Sketch::from_serialized(feature.sheet_transition.sketches[0]),rectangle);
        check(std::abs(parsed.model.depth-180)<1e-8&&std::abs(parsed.model.corner_radius-18)<1e-8,"Envelope edit did not reach transition input");
    }
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
    for(const auto rotation:std::array<kernel::Vec3,2>{{{0,-15,0},{0,15,0}}}) {
        auto rotated=feature;rotated.sheet_transition.end_rotation=rotation;
        std::cout<<"Manufacturing relative tilt "<<rotation.x<<','<<rotation.y<<','<<rotation.z<<std::endl;
        check(workspace::commit_sheet_transition(live,kernel,id,rotated),"Relative rotated solid failed");
        check_solid(state->session.calculated_boundaries().back());
        const auto rotated_volume=state->session.calculated_boundaries().back().volume;
        for(bool unfold:{true,false}) {
            auto change=document::PartDocument::create_sketch_container();change.feature_kind=unfold?document::FeatureKind::Unbend:document::FeatureKind::BendBack;
            change.sheet_state.all=true;
            check(workspace::commit_sheet_state(live,kernel,id,change),"Rotated transition state change failed");
            const auto& result=state->session.calculated_boundaries().back();check_solid(result);
            // Tilted finite-radius lofts and flat reconstruction are approximate.
            // Bound the flat volume error to 0.02%; Bend Back restores the
            // original material and must agree to numerical roundoff.
            check(std::abs(result.volume-rotated_volume)<(unfold?rotated_volume*2e-4:1e-6),"Rotated transition state changed volume");
        }
        check(workspace::step_part_document_history(live,id,false)&&workspace::step_part_document_history(live,id,false),"Rotated state Undo failed");
        check(state->session.calculated_boundaries().back().calculation_errors.empty(),"Rotated solid has errors");
        check(workspace::step_part_document_history(live,id,false),"Rotated solid Undo failed");
    }
    const auto directory=std::filesystem::absolute("build/transition-model");std::filesystem::create_directories(directory);
    const auto path=directory/"native-transition.prtz";
    state->session.document().save(path,state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> cache;auto loaded=document::PartDocument::load(path,&cache);
    check(loaded.find_container(feature.id)->sheet_transition==feature.sheet_transition,"Native file lost parameters");
    kernel::OcctKernel cold;const auto recalculated=workspace::calculate_part_with_resolved_references(cold,loaded);
    check(!recalculated.empty()&&recalculated.back().calculation_errors.empty(),"Cold regeneration failed");
    check(std::abs(recalculated.back().volume-volume)<1e-5,"Cold regeneration changed volume");
    for(const kernel::BodyResult* result:{static_cast<const kernel::BodyResult*>(&cache.back()),&recalculated.back()}) {
        const auto& axes=result->mesh.original_references.axes;
        const auto axis=std::ranges::find_if(axes,[&](const auto& a){return a.reference.owner_id==feature.id&&a.reference.semantic_key=="axis:primary";});
        check(axis!=axes.end(),"Transition centre axis was not persisted or regenerated");
        check(std::abs(axis->point.z-75)<1e-8&&std::abs(axis->direction.z-1)<1e-8,
            "Transition axis does not connect the two profile centres");
        check(std::ranges::any_of(result->mesh.axes,[&](const auto& a){return a.reference==axis->reference;}),
            "Transition centre axis is missing from ordinary display");
    }
    {
        auto shifted=placed;shifted.sheet_transition.end_position={0,0,180};
        shifted.sheet_transition.end_rotation={0,15,0};
        const auto operation=document::sheet_transition_operation(loaded,shifted);
        const auto& axes=std::get<kernel::FeatureGroupRequest>(operation.primitive).axes;
        const auto axis=std::ranges::find_if(axes,[](const auto& a){return a.reference.semantic_key=="axis:primary";});
        check(axis!=axes.end()&&std::abs(axis->display_length-182)<1e-8,
            "Offset/rotated transition centre axis has the wrong span");
    }
    {
        const auto origins=loaded.history_origin_reference_geometry_before({});
        for(const auto& owner:{feature.container_origin.id,feature.sheet_transition.end_origin_id}) {
            check(std::ranges::count_if(origins.axes,[&](const auto& a){return a.reference.owner_id==owner;})==3,"Transition Origin is missing its three reference axes");
            document::Placement attachment;
            attachment.references={{{},owner,"origin:point"},{{},owner,"origin:plane:xy",0,false,"front",true},{{},owner,"origin:plane:yz",0,false,"top",true}};
            check(document::resolve_placement(attachment,origins),"Whole transition Origin cannot resolve a downstream placement");
            check(std::abs(attachment.z-(owner==feature.container_origin.id?0.:150.))<1e-8,"Transition Origin resolved to the wrong profile");
        }
    }
    // A real downstream Bend consumes the same original edge/face/point packet
    // as the interactive sheet commands; display-only edges are insufficient.
    {
        const auto& geometry=recalculated.back().mesh.original_references;
        const auto edge=std::ranges::find_if(geometry.edges,[&](const auto& candidate){
            if(candidate.reference.owner_id!=feature.id||candidate.reference.semantic_key.find("rectangle-rim")==std::string::npos)return false;
            try {return document::bend_sheet_references(candidate).size()==3;}catch(const std::exception&){return false;}
        });
        check(edge!=geometry.edges.end(),"Transition has no attachable original rectangle rim");
        auto bend=document::PartDocument::create_sketch_container();bend.feature_kind=document::FeatureKind::Bend;
        bend.bend.sheet_attachment=true;bend.bend.radius=3;bend.bend.angle_degrees=30;
        bend.placement.references=document::bend_sheet_references(*edge);
        auto outline=sketcher::Sketch::create_default();outline.owner_container_id=bend.id;bend.bend.sketch_id=outline.id;
        document::initialize_bend_start_profile(outline,*edge->measured_length);
        check(workspace::commit_bend(live,kernel,id,bend,outline),"Bend attachment to transition did not commit");
        check(state->session.calculated_boundaries().back().calculation_errors.empty(),"Attached Bend failed calculation");
        auto changed=feature;changed.sheet_transition.end_position.z=175;
        check(workspace::commit_sheet_transition(live,kernel,id,changed),"Transition edit with attached Bend failed");
        check(state->session.document().find_container(bend.id)->placement.reference_valid,"Transition edit invalidated Bend references");
        const auto attached_path=directory/"native-transition-attached.prtz";
        state->session.document().save(attached_path,state->session.calculated_boundaries());
        auto attached=document::PartDocument::load(attached_path);
        kernel::OcctKernel attachment_kernel;
        const auto rebuilt=workspace::calculate_part_with_resolved_references(attachment_kernel,attached);
        check(rebuilt.back().calculation_errors.empty()&&attached.find_container(bend.id)->placement.reference_valid,"Attached Bend failed native save/reopen/regeneration");
        check(attached.find_container(bend.id)->placement.references==bend.placement.references,"Native file changed original attachment identity");
        const auto attached_volume=state->session.calculated_boundaries().back().volume;
        for(bool unfold:{true,false}) {
            auto change=document::PartDocument::create_sketch_container();change.feature_kind=unfold?document::FeatureKind::Unbend:document::FeatureKind::BendBack;change.sheet_state.all=true;
            check(workspace::commit_sheet_state(live,kernel,id,change),"Attached transition state change failed");
            const auto& result=state->session.calculated_boundaries().back();check_solid(result);
            check(std::abs(result.volume-attached_volume)<(unfold?attached_volume*2e-4:1e-6),"Attached transition state lost material");
        }
        check(workspace::step_part_document_history(live,id,false)&&workspace::step_part_document_history(live,id,false),"Attached state Undo failed");
        std::cout<<"Original transition edge/face/point attachment: create, edit, save/reopen and cold regeneration passed\n";
        check(workspace::step_part_document_history(live,id,false),"Attached source edit Undo failed");
        check(workspace::step_part_document_history(live,id,false)&&!state->session.document().find_container(bend.id),"Bend attachment Undo failed");
    }
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
