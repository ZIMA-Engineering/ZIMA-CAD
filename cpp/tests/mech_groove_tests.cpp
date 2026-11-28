#include <zima/document/mech_groove.hpp>
#include <zima/document/groove_catalog.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/placement_orientation.hpp>
#include <zima/workspace/mech_groove_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/modeling_confirmation.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <GProp_GProps.hxx>
#include <nlohmann/json.hpp>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <numbers>
#include <set>

namespace {
using namespace zima;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b,const char* message){check(std::abs(a-b)<1e-6,message);}
double volume(const std::vector<kernel::BodyResult>& cache,const std::string& body) {
    check(!cache.empty()&&cache.back().calculation_errors.empty(),"Groove calculation failed");
    GProp_GProps p;BRepGProp::VolumeProperties(kernel::read_calculated_body_shape(cache.back().body_outputs.at(body)),p);
    return p.Mass();
}
double removed(const document::MechGrooveParameters& p) {
    const double r=p.support_diameter/2,sign=p.internal?1.:-1.;
    if(p.profile==document::GrooveProfile::Rectangular)
        return std::numbers::pi*std::abs((r+sign*p.depth)*(r+sign*p.depth)-r*r)*p.width;
    const double u=p.depth-p.radius,q=std::sqrt(p.radius*p.radius-u*u);
    const double area=u*q+p.radius*p.radius*(std::asin(u/p.radius)+std::numbers::pi/2);
    const double centre=r+sign*(p.depth-p.radius);
    return 2*std::numbers::pi*(centre*area+sign*2*q*q*q/3);
}
void material_probes(const kernel::BodyResult& calculated,const std::string& body,
        const document::HistoryContainer& groove) {
    // The feature and its exact section are authored in Body-local coordinates.
    const auto shape=kernel::read_calculated_body_shape(calculated.body_boundaries.at(body).back());
    check(BRepCheck_Analyzer(shape).IsValid(),"Groove result is not a valid BRep");
    const auto& p=groove.mech_groove;const auto section=sketcher::Sketch::from_serialized(p.section);
    const auto x=section.resolved_x_axis,z=section.resolved_y_axis;
    const kernel::Vec3 y{z.y*x.z-z.z*x.y,z.z*x.x-z.x*x.z,z.x*x.y-z.y*x.x};
    for(double angle:{0.,1.2,3.1})for(bool in_groove:{false,true}) {
        const double r=p.support_diameter/2+(p.internal?1.:-1.)*p.depth/2;
        const double station=in_groove?0:p.width/2+.25;
        const auto o=section.resolved_origin;
        const gp_Pnt point(o.x+z.x*station+r*(x.x*std::cos(angle)+y.x*std::sin(angle)),
            o.y+z.y*station+r*(x.y*std::cos(angle)+y.y*std::sin(angle)),
            o.z+z.z*station+r*(x.z*std::cos(angle)+y.z*std::sin(angle)));
        BRepClass3d_SolidClassifier classifier(shape,point,1e-7);
        check(classifier.State()==(in_groove?TopAbs_OUT:TopAbs_IN),"Groove material is at the wrong radial or axial position");
    }
}
void native_case(bool internal,document::GrooveProfile kind,bool rotated,bool plane_first,bool placed_body=false) {
    kernel::OcctKernel engine;
    auto part=document::PartDocument::create_default();
    const auto body=part.body_history.create_body("Groove specimen");
    if(placed_body) {
        auto moved=*part.body_history.find(body);auto& p=moved.scope.placement;
        p.x=31;p.y=-17;p.z=53;
        p.rotation_x=p.absolute_rotation_x=p.rotation_offset_x=31;
        p.rotation_y=p.absolute_rotation_y=p.rotation_offset_y=22;
        p.rotation_z=p.absolute_rotation_z=p.rotation_offset_z=-19;
        part.body_history.update_body(moved);
    }
    auto sketch=sketcher::Sketch::create_default();
    static_cast<void>(sketch.add_circle(0,0,internal?35:25));
    if(internal)static_cast<void>(sketch.add_circle(0,0,25));
    auto source=document::PartDocument::create_feature_container(sketch.id);
    source.feature.sides[0].length=40;sketch.owner_container_id=source.id;
    if(rotated) {
        auto& p=source.placement;p.x=100;p.y=-50;p.z=7;
        p.absolute_rotation_x=p.rotation_x=p.rotation_offset_x=30;
        p.absolute_rotation_y=p.rotation_y=p.rotation_offset_y=20;
        p.absolute_rotation_z=p.rotation_z=p.rotation_offset_z=10;
    }
    part.history.push_back(source);part.sketches.push_back(sketch);
    part.insert_history_entry(document::PartHistoryKind::Feature,source.id);
    auto cache=workspace::calculate_part_with_resolved_references(engine,part);
    const double initial=volume(cache,body);
    const auto* source_input=workspace::calculated_operation_input(part,cache,{});
    check(source_input,"Groove specimen input boundary is unavailable");
    const auto& refs=source_input->mesh.original_references;
    const auto cylinder=std::ranges::find_if(refs.triangle_references,[&](const auto& face){return face.surface&&
        face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder&&std::abs(face.surface->radius-25)<1e-7&&face.surface->reversed==internal;});
    check(cylinder!=refs.triangle_references.end(),"Groove specimen has no exact cylinder");
    const auto plane=std::ranges::find_if(refs.triangle_references,[](const auto& face){return face.surface&&
        face.surface->kind==kernel::SurfaceGeometry::Kind::Plane;});
    check(plane!=refs.triangle_references.end(),"Groove specimen has no plane");
    const auto origin=kernel::annotation_frame({source.placement.x,source.placement.y,source.placement.z},
        {source.placement.rotation_x,source.placement.rotation_y,source.placement.rotation_z}).world({0,0,20});
    auto groove=document::create_mech_groove();
    groove.mech_groove.profile=kind;groove.mech_groove.width=2.15;groove.mech_groove.depth=1.25;groove.mech_groove.radius=1.4;
    groove.placement.x=origin.x;groove.placement.y=origin.y;groove.placement.z=origin.z;
    document::ConstructionReference axis,face;
    axis.owner_id=cylinder->owner_id;axis.semantic_key=cylinder->semantic_key;axis.use_axis=true;
    axis.supports_offset=false;axis.orientation_role=document::placement_position_only_role;
    face.owner_id=plane->owner_id;face.semantic_key=plane->semantic_key;face.supports_offset=true;
    face.orientation_role=document::placement_position_only_role;
    const auto distance=document::measure_placement_reference_value(face,{axis,face},refs,origin);
    check(distance.has_value(),"Groove specimen plane distance is unavailable");face.offset=*distance;
    groove.placement.references=plane_first?std::vector{face,axis}:std::vector{axis,face};
    const auto before_preview=kernel::body_calculation_count.load();
    groove=document::resolve_mech_groove_draft(groove,refs);
    check(!document::mech_groove_preview_edges(groove).empty(),"Groove preview is empty");
    check(kernel::body_calculation_count.load()==before_preview,"Groove preview calculated material");
    workspace::Workspace live;const auto id=part.document_id;live.add_part(part,cache);
    check(workspace::commit_mech_groove(live,engine,id,groove),"Groove creation did not commit");
    auto& state=*live.open_part(id);
    const auto verify=[&] {
        const auto& current=state.session.document();const auto& feature=*current.find_container(groove.id);
        near(volume(state.session.calculated_boundaries(),body),initial-removed(feature.mech_groove),"Groove material disagrees with the analytic volume");
        check(feature.mech_groove.internal==internal,"Groove changed material side");
        check(feature.mech_groove.support==*cylinder,"Groove replaced its source identity");
        material_probes(state.session.calculated_boundaries().back(),body,feature);
    };
    verify();
    for(const double depth:{.9,1.5,1.25}) {
        auto value=*state.session.document().find_container(groove.id);value.mech_groove.depth=depth;
        const auto before=state.session.document().serialized();
        check(workspace::commit_mech_groove(live,engine,id,value),"Groove edit did not commit");verify();
        const auto after=state.session.document().serialized();
        check(state.session.undo()&&state.session.document().serialized()==before,"Groove Undo failed");
        check(state.session.redo()&&state.session.document().serialized()==after,"Groove Redo failed");
    }
    const auto count=kernel::body_calculation_count.load();
    check(!workspace::commit_mech_groove(live,engine,id,*state.session.document().find_container(groove.id)),"Unchanged groove committed");
    check(kernel::body_calculation_count.load()==count,"Unchanged groove calculated material");
    QTemporaryDir dir("build/groove-contract-XXXXXX");check(dir.isValid(),"Groove temporary directory failed");
    const std::filesystem::path file=std::filesystem::path(dir.path().toStdString())/"groove.prtz";
    state.session.document().save(file,state.session.calculated_boundaries());
    std::vector<kernel::BodyResult> loaded_cache;auto loaded=document::PartDocument::load(file,&loaded_cache);
    check(kernel::body_calculation_count.load()==count,"Groove native reopen calculated material");
    check(loaded.find_container(groove.id)->mech_groove==state.session.document().find_container(groove.id)->mech_groove,"Groove persistence lost its definition");
    near(volume(loaded_cache,body),volume(state.session.calculated_boundaries(),body),"Groove saved material changed");
    kernel::OcctKernel fresh;const auto cold=workspace::calculate_part_with_resolved_references(fresh,loaded);
    near(volume(cold,body),volume(loaded_cache,body),"Groove cold regeneration changed material");
    material_probes(cold.back(),body,*loaded.find_container(groove.id));
    {
        // A second cut uses the surviving exact source face. Editing it must
        // not replace the first groove's definition or its support identity.
        workspace::Workspace pair_live;pair_live.add_part(loaded,cold);
        auto second=document::create_mech_groove();
        second.placement=loaded.find_container(groove.id)->placement;
        second.mech_groove=loaded.find_container(groove.id)->mech_groove;
        const auto section=sketcher::Sketch::from_serialized(second.mech_groove.section);
        const auto o=section.resolved_origin,z=section.resolved_y_axis;
        const kernel::Vec3 station{o.x+z.x*10,o.y+z.y*10,o.z+z.z*10};
        second.placement.x=station.x;second.placement.y=station.y;second.placement.z=station.z;
        const auto& input=workspace::calculated_operation_input(loaded,cold,{})->mesh.original_references;
        for(auto& ref:second.placement.references)if(!ref.use_axis) {
            const auto offset=document::measure_placement_reference_value(ref,second.placement.references,input,station);
            check(offset.has_value(),"Second groove axial datum is unavailable");ref.offset=*offset;
        }
        const auto first=*loaded.find_container(groove.id);
        for(double depth:{1.1,1.3}) {
            second.mech_groove.depth=depth;
            check(workspace::commit_mech_groove(pair_live,engine,id,second),"Second groove did not commit");
            const auto& pair=*pair_live.open_part(id);
            const auto& committed=*pair.session.document().find_container(second.id);
            check(*pair.session.document().find_container(groove.id)==first,"Editing the second groove changed the first");
            near(volume(pair.session.calculated_boundaries(),body),initial-removed(first.mech_groove)-removed(committed.mech_groove),
                "Successive groove cuts disagree with the analytic volume");
            material_probes(pair.session.calculated_boundaries().back(),body,first);
            material_probes(pair.session.calculated_boundaries().back(),body,committed);
            second=committed;
        }
    }
    {
        workspace::Workspace origin_live;origin_live.add_part(loaded,cold);
        auto whole=*loaded.find_container(groove.id);whole.placement.references.clear();
        const auto datums=loaded.construction_reference_geometry_for(groove.id,{});
        for(const char* plane:{"xy","xz","yz"}) {
            document::ConstructionReference ref;ref.owner_id=body+":origin";
            ref.semantic_key=std::string("origin:plane:")+plane;ref.supports_offset=true;
            whole.placement.references.push_back(ref);
        }
        for(auto& ref:whole.placement.references) {
            const auto offset=document::measure_placement_reference_value(ref,whole.placement.references,datums,origin);
            check(offset.has_value(),"Whole-Origin groove datum offset is unavailable");ref.offset=*offset;
        }
        check(workspace::commit_mech_groove(origin_live,engine,id,whole),"Whole-Origin groove placement failed");
        const auto* placed=origin_live.open_part(id);
        near(volume(placed->session.calculated_boundaries(),body),initial-removed(whole.mech_groove),"Whole-Origin groove changed its material");
        material_probes(placed->session.calculated_boundaries().back(),body,*placed->session.document().find_container(groove.id));
        check(workspace::part_history_dependency_closure(placed->session.document(),source.id,true).contains(groove.id),
            "Whole-Origin groove lost its source suppression/deletion dependency");
    }
    if(!rotated&&!plane_first) {
        auto catalogue=*state.session.document().find_container(groove.id);
        catalogue.mech_groove.catalogue=kind==document::GrooveProfile::Circular?
            (internal?"din9926-seeger":"din9925-seeger"):(internal?"din472-seeger":"din471-seeger");
        for(double position:{0.,.5,1.}) {
            catalogue.mech_groove.width_position=position;
            check(workspace::commit_mech_groove(live,engine,id,catalogue),"Catalogue groove edit did not commit");verify();
            catalogue=*state.session.document().find_container(groove.id);
        }
        for(bool edit:{false,true}) {
            workspace::Workspace confirmation_live;
            confirmation_live.add_part(edit?loaded:part,edit?cold:cache);
            auto* tested=confirmation_live.open_part(id);
            auto pending=*loaded.find_container(groove.id);
            if(!edit)pending.mech_groove.section.clear();
            pending.mech_groove.catalogue="unavailable-test-catalogue";
            const auto before=tested->session.document().serialized();
            const auto revision=tested->session.revision();
            workspace::ModelingConfirmation confirmation;
            const auto submit=[&](bool failure) {
                workspace::ModelingConfirmationAttempt attempt(confirmation,confirmation_live,id);
                bool rejected=false;
                try {static_cast<void>(workspace::commit_mech_groove(confirmation_live,engine,id,pending));}
                catch(const std::exception&){rejected=true;}
                check(rejected==failure&&attempt.failed()==failure,"Wrong groove confirmation acknowledgement");
            };
            submit(true);
            check(tested->session.document().serialized()==before&&tested->session.revision()==revision,
                "First failed groove OK changed the live document");
            pending.mech_groove.width+=.1;submit(true);
            tested->session.update_calculated_boundaries(tested->session.calculated_boundaries());submit(true);
            confirmation.clear();submit(true);
            const auto calculations=kernel::body_calculation_count.load();submit(false);
            check(kernel::body_calculation_count.load()==calculations,"Second failed groove OK recalculated material");
            check(tested->session.revision()==revision+1&&tested->session.document().reference_errors.contains(groove.id),
                "Unresolved groove did not commit in one revision");
            auto unresolved=document::PartDocument::from_serialized(tested->session.document().serialized(tested->session.calculated_boundaries()));
            const auto replay=workspace::calculate_part_with_resolved_references(engine,unresolved);
            check(replay.back().calculation_errors.contains(groove.id),"Unresolved groove cold replay lost its error");
            check(tested->session.undo()&&tested->session.redo(),"Unresolved groove Undo/Redo failed");
            pending=*tested->session.document().find_container(groove.id);pending.mech_groove.catalogue.clear();
            confirmation.clear();submit(false);
            near(volume(tested->session.calculated_boundaries(),body),initial-removed(tested->session.document().find_container(groove.id)->mech_groove),
                "Repairing the unresolved groove changed its material definition");
        }
    }
    // Source changes must invalidate the derived section and retain reference identity.
    auto changed=loaded;auto source_cache=cold;
    for(double delta:{1.,-.5,.75}) {
        auto& source_sketch=*std::ranges::find(changed.sketches,sketch.id,&sketcher::Sketch::id);
        source_sketch.circles[internal?1:0].radius+=delta;
        const double r=source_sketch.circles[internal?1:0].radius;
        changed.find_container(source.id)->placement.x+=delta*3;
        source_cache=workspace::calculate_part_with_resolved_references(engine,changed,&source_cache);
        const auto& current=*changed.find_container(groove.id);
        near(current.mech_groove.support_diameter,2*r,"Source diameter did not update the groove");
        const double input_volume=std::numbers::pi*(internal?35*35-r*r:r*r)*40;
        near(volume(source_cache,body),input_volume-removed(current.mech_groove),"Source edit left a stale groove solid");
        material_probes(source_cache.back(),body,current);
        check(current.mech_groove.support==*cylinder,"Source edit replaced groove support identity");
    }
    // Reference loss retains the authored section and last valid frame; repair
    // must produce the same material, including after unresolved persistence.
    const auto valid=changed;const auto valid_cache=source_cache;
    auto& lost=*changed.find_container(groove.id);
    const auto retained=lost.mech_groove.section;
    for(auto& ref:lost.placement.references)if(ref.use_axis)ref.semantic_key="missing:groove-support";
    source_cache=workspace::calculate_part_with_resolved_references(engine,changed,&source_cache);
    check(changed.reference_errors.contains(groove.id),"Lost groove support was not reported");
    check(changed.find_container(groove.id)->mech_groove.section==retained,"Lost support discarded the last valid section frame");
    const auto& lost_frame=changed.find_container(groove.id)->placement;
    const auto& last_frame=valid.find_container(groove.id)->placement;
    near(lost_frame.x,last_frame.x,"Lost support moved the groove in X");
    near(lost_frame.y,last_frame.y,"Lost support moved the groove in Y");
    near(lost_frame.z,last_frame.z,"Lost support moved the groove in Z");
    changed=document::PartDocument::from_serialized(changed.serialized(source_cache));
    changed.find_container(groove.id)->placement.references=valid.find_container(groove.id)->placement.references;
    source_cache=workspace::calculate_part_with_resolved_references(engine,changed,&source_cache);
    near(volume(source_cache,body),volume(valid_cache,body),"Groove support repair changed the material");
    material_probes(source_cache.back(),body,*changed.find_container(groove.id));
    std::cout<<"Groove native case: internal="<<internal<<" circular="<<(kind==document::GrooveProfile::Circular)
        <<" rotated="<<rotated<<" plane_first="<<plane_first<<" placed_body="<<placed_body<<" passed\n";
}
}
int main() {
    try {
        for(const bool internal:{false,true})for(const auto profile:{document::GrooveProfile::Rectangular,document::GrooveProfile::Circular})
            for(const bool rotated:{false,true})for(const bool plane_first:{false,true})native_case(internal,profile,rotated,plane_first);
        for(const bool internal:{false,true})for(const auto profile:{document::GrooveProfile::Rectangular,document::GrooveProfile::Circular})
            native_case(internal,profile,false,false,true);
        return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
