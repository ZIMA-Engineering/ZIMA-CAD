#include <zima/workspace/solid_state_operations.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/sweep_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/kernel/solid_state_ancestry.hpp>
#include <zima/kernel/solid_straightening.hpp>
#include "sweep_test_support.hpp"

#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <chrono>
using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){if(!std::isfinite(actual)||std::abs(actual-expected)>=1e-5)throw std::runtime_error("Transferred contour coordinate differs: actual="+std::to_string(actual)+", expected="+std::to_string(expected));}
void helical_frame() {
    auto feature=test_support::sweep_fixture(document::FeatureKind::HelicalSweep);
    feature.helical.pitch=5;feature.sweep_precision.custom_tolerance=1e-4;
    auto sketch=sketcher::Sketch::from_serialized(feature.helical.sketches[2]);sketch.circles.clear();sketch.points.clear();
    static_cast<void>(sketch.add_rectangle(-.5,-.3,.5,.3));feature.helical.sketches[2]=sketch.serialized();
    const auto source=document::PartDocument::helical_sweep_request(feature);
    kernel::OcctKernel kernel;const auto plan=kernel.prepare_straightening(source,.9);
    const auto calculated=kernel.evaluate_history({{"spring",source}}).back();
    const auto& points=calculated.mesh.original_references.points;
    const auto& f=plan.end_transform;gp_Trsf end;
    end.SetValues(f[1].x,f[2].x,f[3].x,f[0].x,f[1].y,f[2].y,f[3].y,f[0].y,f[1].z,f[2].z,f[3].z,f[0].z);
    for(const auto& id:source.sections.front().profile.outer_vertex_source_ids) {
        const auto a=std::ranges::find_if(points,[&](const auto& p){return p.reference.owner_id=="spring"&&p.reference.semantic_key=="start:generated:"+id;});
        const auto b=std::ranges::find_if(points,[&](const auto& p){return p.reference.owner_id=="spring"&&p.reference.semantic_key=="end:generated:"+id;});
        check(a!=points.end()&&b!=points.end(),"Spring has no semantic section vertices");
        const auto p=gp_Pnt(a->position.x,a->position.y,a->position.z).Transformed(end);
        near(p.X(),b->position.x);near(p.Y(),b->position.y);near(p.Z(),b->position.z);
    }
    kernel::ExtrusionRequest child;const auto& section=source.sections.front().profile;
    child.outer_profile=section.outer_profile;child.outer_edge_source_ids=section.outer_edge_source_ids;
    child.outer_vertex_source_ids=section.outer_vertex_source_ids;child.profile_region_id="tail";child.outer_boundary_id="tail-loop";
    const auto a=plan.source_start_centroid,b=plan.straight_end_centroid;
    for(auto& p:std::get<kernel::ExtrusionRequest::PolygonProfile>(child.outer_profile).vertices){p.x+=b.x-a.x;p.y+=b.y-a.y;p.z+=b.z-a.z;}
    child.direction={plan.start_tangent.x*2,plan.start_tangent.y*2,plan.start_tangent.z*2};
    kernel::FeatureGroupRequest group;group.children={child};
    group.axes.push_back({plan.straight_end_centroid,plan.start_tangent,2,{"tail","section-axis"}});
    group.reference_points.push_back({plan.straight_end_centroid,{"tail","section-center"}});
    kernel::FeatureGroupRequest::ReferencePlane plane;plane.reference={"tail","section-plane"};
    const auto& corners=std::get<kernel::ExtrusionRequest::PolygonProfile>(child.outer_profile).vertices;
    std::copy_n(corners.begin(),4,plane.corners.begin());
    auto surface=std::make_shared<kernel::SurfaceGeometry>();surface->origin=plan.straight_end_centroid;surface->axis=plan.start_tangent;
    plane.reference.surface=surface;group.reference_planes.push_back(plane);
    std::vector<kernel::HistoryOperation> history{{"spring",source},{"straight",kernel::SolidStateRequest{false,true,.9,{}}},
        {"tail",group},{"restore",kernel::SolidStateRequest{true,true,1,{}}}};
    history.back().solid_state_face_transfers={{"tail","spring"}};
    const auto restored=kernel.evaluate_history(history).back();
    check(restored.calculation_errors.empty(),"Spring continuation could not be restored");
    const auto& transferred=restored.solid_state_reference_views.at("restore")->points;
    const auto& packet=*restored.solid_state_reference_views.at("restore");
    const auto axis=std::ranges::find_if(packet.axes,[](const auto& a){return a.reference.owner_id=="tail"&&a.reference.semantic_key=="section-axis";});
    const auto center=std::ranges::find_if(transferred,[](const auto& p){return p.reference.owner_id=="tail"&&p.reference.semantic_key=="section-center";});
    const auto moved_plane=std::ranges::find_if(packet.triangle_references,[](const auto& f){return f.owner_id=="tail"&&f.semantic_key=="section-plane";});
    check(axis!=packet.axes.end()&&center!=transferred.end()&&moved_plane!=packet.triangle_references.end()&&moved_plane->surface,
        "Transferred Feature lost its auxiliary references");
    for(const auto p:{axis->point,center->position,moved_plane->surface->origin}) {
        near(p.x,plan.source_end_centroid.x);near(p.y,plan.source_end_centroid.y);near(p.z,plan.source_end_centroid.z);
    }
    for(const auto& id:section.outer_vertex_source_ids) {
        const auto a=std::ranges::find_if(points,[&](const auto& p){return p.reference.owner_id=="spring"&&p.reference.semantic_key=="end:generated:"+id;});
        const auto b=std::ranges::find_if(transferred,[&](const auto& p){return p.reference.owner_id=="tail"&&p.reference.semantic_key=="start:"+id;});
        check(a!=points.end()&&b!=transferred.end(),"Spring continuation lost its join vertices");
        near(a->position.x,b->position.x);near(a->position.y,b->position.y);near(a->position.z,b->position.z);
    }
    auto tapered=history;
    std::get<kernel::ExtrusionRequest>(std::get<kernel::FeatureGroupRequest>(tapered[2].primitive).children.front()).draft_angle_degrees=1;
    auto tip=child;
    for(auto& p:std::get<kernel::ExtrusionRequest::PolygonProfile>(tip.outer_profile).vertices) {
        p.x+=child.direction.x;p.y+=child.direction.y;p.z+=child.direction.z;
    }
    tapered.insert(tapered.end()-1,{"tip",tip});
    tapered.back().solid_state_face_transfers.push_back({"tip","tail"});
    bool rejected=false;
    try{static_cast<void>(kernel.evaluate_history(tapered));}
    catch(const std::invalid_argument& error){rejected=std::string(error.what()).find("Solid state replay cannot yet preserve this operation.")!=std::string::npos;}
    check(rejected,"Drafted end-section centroid was guessed for a descendant");
    std::cout<<"Spring section frame and tangential continuation agree with independently calculated source vertices\n";
}
void native_curved_chain(bool initially_straight,int sweep_kind=0) {
    kernel::OcctKernel kernel;workspace::Workspace live;auto doc=document::PartDocument::create_default();
    if(doc.body_history.bodies().empty())static_cast<void>(doc.body_history.create_body("Body"));
    const auto id=doc.document_id;live.add_part(doc,{});live.activate(id);
    auto section=sketcher::Sketch::create_default();static_cast<void>(section.add_rectangle(9,-1,11,1));
    const auto axis=section.add_segment(0,-20,0,20,true);section.set_segment_centerline(axis,true);
    auto root=document::PartDocument::create_revolution_container(section.id);
    root.revolution.axis_segment_id=axis;root.revolution.angle_degrees=90;
    workspace::commit_profile(live,kernel,id,root,workspace::ProfileEditMode::Create,section);
    const auto root_plan=kernel.prepare_straightening(std::get<kernel::RevolutionRequest>(live.open_part(id)->session.document().kernel_operations().front().primitive),.9);
    std::optional<std::string> initial_state;
    if(initially_straight) {
        auto first=document::PartDocument::create_solid_state_container();first.solid_state.coefficient=.9;
        check(workspace::commit_solid_state(live,kernel,id,first),"Cannot prepare curved-chain straight input");initial_state=first.id;
    }
    const auto end_tangent=[](const kernel::SolidStraighteningPlan& plan) {
        const auto& f=plan.end_transform;const auto t=plan.start_tangent;
        return kernel::Vec3{f[1].x*t.x+f[2].x*t.y+f[3].x*t.z,
            f[1].y*t.x+f[2].y*t.y+f[3].y*t.z,f[1].z*t.x+f[2].z*t.y+f[3].z*t.z};
    };
    const auto attach=[&](document::HistoryContainer& child,const std::string& owner,kernel::Vec3 center) {
        const auto& geometry=live.open_part(id)->session.calculated_boundaries().back().mesh.original_references;
        const auto face=std::ranges::find_if(geometry.triangle_references,[&](const auto& face) {
            if(face.owner_id!=owner||!face.surface||face.surface->kind!=kernel::SurfaceGeometry::Kind::Plane)return false;
            const auto key=initial_state&&owner==*initial_state?kernel::solid_state_parent(face.semantic_key)->second:face.semantic_key;
            if(!key.starts_with("start:from:")&&!key.starts_with("end:from:")&&
                !key.starts_with("sweep:cap:start:from:")&&!key.starts_with("sweep:cap:end:from:"))return false;
            const auto p=face.surface->origin,n=face.surface->axis;
            return std::abs((center.x-p.x)*n.x+(center.y-p.y)*n.y+(center.z-p.z)*n.z)<1e-7;
        });
        check(face!=geometry.triangle_references.end(),"Curved chain lacks its physical end cap");
        document::ConstructionReference position;position.owner_id=owner;position.semantic_key=face->semantic_key;
        position.supports_offset=true;position.offset=-0.;
        auto top=position;top.orientation_only=true;top.orientation_drives_rotation=true;top.orientation_role="top";top.supports_offset=false;
        child.placement.references={position,top};child.placement.x=center.x;child.placement.y=center.y;child.placement.z=center.z;
    };
    auto contour=sketcher::Sketch::create_default();static_cast<void>(contour.add_rectangle(-.3,-.2,.3,.2));
    const auto child_axis=contour.add_segment(-4,-10,-4,10,true);contour.set_segment_centerline(child_axis,true);
    auto child=document::PartDocument::create_revolution_container(contour.id);
    child.revolution.axis_segment_id=child_axis;child.revolution.angle_degrees=child.revolution.angle_reverse=90;
    if(sweep_kind)child=test_support::continuation_sweep_fixture(sweep_kind==1?document::FeatureKind::Sweep2D:
        sweep_kind==2?document::FeatureKind::Sweep3D:document::FeatureKind::HelicalSweep);
    const auto center=initially_straight?root_plan.straight_end_centroid:root_plan.source_end_centroid;
    const auto tangent=initially_straight?root_plan.start_tangent:end_tangent(root_plan);
    attach(child,initial_state.value_or(root.id),center);
    if(sweep_kind)workspace::commit_sweep(live,kernel,id,child,workspace::SweepEditMode::Create);
    else workspace::commit_profile(live,kernel,id,child,workspace::ProfileEditMode::Create,contour);
    const auto child_plan=[&]() {
        const auto operations=live.open_part(id)->session.document().kernel_operations();
        const auto found=std::ranges::find(operations,child.id,&kernel::HistoryOperation::owner_id);
        return sweep_kind?kernel.prepare_straightening(std::get<kernel::Sweep3DRequest>(found->primitive),1.):
            kernel.prepare_straightening(std::get<kernel::RevolutionRequest>(found->primitive),1.);
    };
    auto plan=child_plan();
    if(sweep_kind==3) {
        // A spring's initial tangent is tilted to its rotation axis. Use the
        // ordinary numerical correction and free in-plane position to put its
        // first section on the predecessor; do not special-case the solver.
        child=*live.open_part(id)->session.document().find_container(child.id);
        const kernel::Vec3 rotation{child.placement.rotation_x,child.placement.rotation_y,child.placement.rotation_z};
        const auto x=document::construction_direction_from_local_axis("x",rotation);
        const auto y=document::construction_direction_from_local_axis("y",rotation);
        const auto z=document::construction_direction_from_local_axis("z",rotation);
        const auto dot=[](kernel::Vec3 a,kernel::Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
        near(dot(plan.start_tangent,x),0);near(std::abs(dot(tangent,z)),1);
        child.placement.rotation_offset_x=(std::atan2(dot(plan.start_tangent,y),dot(plan.start_tangent,z))+
            (dot(tangent,z)<0?std::numbers::pi:0))*180/std::numbers::pi;
        child.placement.x+=center.x-plan.source_start_centroid.x;
        child.placement.y+=center.y-plan.source_start_centroid.y;
        child.placement.z+=center.z-plan.source_start_centroid.z;
        workspace::commit_sweep(live,kernel,id,child,workspace::SweepEditMode::Replace);plan=child_plan();
    }
    if(plan.start_tangent.x*tangent.x+plan.start_tangent.y*tangent.y+plan.start_tangent.z*tangent.z<0) {
        check(!sweep_kind,"Sweep fixture starts opposite the joining tangent");
        child=*live.open_part(id)->session.document().find_container(child.id);child.revolution.direction=document::ExtrusionDirection::Reverse;
        workspace::commit_profile(live,kernel,id,child,workspace::ProfileEditMode::Replace,contour);plan=child_plan();
    }
    near(plan.source_start_centroid.x,center.x);near(plan.source_start_centroid.y,center.y);near(plan.source_start_centroid.z,center.z);
    near(plan.start_tangent.x,tangent.x);near(plan.start_tangent.y,tangent.y);near(plan.start_tangent.z,tangent.z);
    auto tip_section=sketcher::Sketch::create_default();static_cast<void>(tip_section.add_rectangle(-.1,-.1,.1,.1));
    auto tip=document::PartDocument::create_extrusion_container(tip_section.id);tip.extrusion.length_forward=tip.extrusion.length_reverse=2;
    attach(tip,child.id,plan.source_end_centroid);
    workspace::commit_profile(live,kernel,id,tip,workspace::ProfileEditMode::Create,tip_section);
    const auto operations=live.open_part(id)->session.document().kernel_operations();
    const auto direction=std::get<kernel::ExtrusionRequest>(operations.back().primitive).direction,t=end_tangent(plan);
    if(direction.x*t.x+direction.y*t.y+direction.z*t.z<0) {
        tip=*live.open_part(id)->session.document().find_container(tip.id);tip.extrusion.direction=document::ExtrusionDirection::Reverse;
        workspace::commit_profile(live,kernel,id,tip,workspace::ProfileEditMode::Replace,tip_section);
    }
    auto* state=live.open_part(id);const auto authored_child=*state->session.document().find_container(child.id);
    const auto authored_tip=*state->session.document().find_container(tip.id);
    const auto authored_axes=state->session.calculated_boundaries().back().mesh.original_references.axes;
    auto straight=document::PartDocument::create_solid_state_container();straight.solid_state.coefficient=.9;
    const auto verify=[&](const document::PartDocument& document,const kernel::BodyResult& actual,const std::string& boundary) {
        auto expected_operations=document.kernel_operations();
        for(auto& operation:expected_operations)if(operation.owner_id==straight.id||operation.owner_id==boundary)
            operation.solid_state_face_transfers={{child.id,root.id},{tip.id,child.id}};
        kernel::OcctKernel independent;const auto expected=independent.evaluate_history(expected_operations).back();
        check(actual.calculation_errors.empty()&&expected.calculation_errors.empty(),"Native curved chain calculation failed");
        near(actual.volume,expected.volume);
        const auto& points=actual.solid_state_reference_views.at(boundary)->points;
        std::size_t checked=0;
        for(const auto& p:expected.solid_state_reference_views.at(boundary)->points)if(p.reference.owner_id==child.id||p.reference.owner_id==tip.id) {
            const auto q=std::ranges::find_if(points,[&](const auto& q){return q.reference==p.reference;});
            check(q!=points.end(),"Native curved chain lost its semantic vertex");
            near(q->position.x,p.position.x);near(q->position.y,p.position.y);near(q->position.z,p.position.z);++checked;
        }
        check(checked>=(sweep_kind?10:16),"Native curved chain lacks vertex evidence");
        check(*document.find_container(child.id)==authored_child&&*document.find_container(tip.id)==authored_tip,"Curved chain rewrote authored placement");
    };
    const auto check_state=[&](document::HistoryContainer change) {
        check(workspace::commit_solid_state(live,kernel,id,change),"Native curved-chain state did not commit");
        verify(state->session.document(),state->session.calculated_boundaries().back(),change.id);
        const auto generation=state->session.data_generation(),revision=state->session.revision();
        check(!workspace::commit_solid_state(live,kernel,id,change)&&state->session.data_generation()==generation&&state->session.revision()==revision,"Curved-chain unchanged OK recalculated");
        check(workspace::step_part_document_history(live,id,false)&&workspace::step_part_document_history(live,id,true),"Curved-chain Undo/Redo failed");
        verify(state->session.document(),state->session.calculated_boundaries().back(),change.id);
        const auto path=std::filesystem::temp_directory_path()/(id+"-curved-chain.prtz");
        state->session.document().save(path,state->session.calculated_boundaries());std::vector<kernel::BodyResult> cache;
        auto loaded=document::PartDocument::load(path,&cache);verify(loaded,cache.back(),change.id);
        kernel::OcctKernel cold;verify(loaded,workspace::calculate_part_with_resolved_references(cold,loaded).back(),change.id);
        std::filesystem::remove(path);
    };
    check_state(straight);
    straight.solid_state.coefficient=1.1;check_state(straight);
    const auto restored=document::PartDocument::create_solid_state_container(true);check_state(restored);
    if(sweep_kind==3) {
        gp_Trsf movement;
        if(initially_straight) {
            const auto& f=root_plan.end_transform;
            movement.SetValues(f[1].x,f[2].x,f[3].x,0,f[1].y,f[2].y,f[3].y,0,f[1].z,f[2].z,f[3].z,0);
            const auto a=root_plan.straight_end_centroid,b=root_plan.source_end_centroid;
            movement.SetTranslationPart(gp_Vec(gp_Pnt(a.x,a.y,a.z).Transformed(movement),gp_Pnt(b.x,b.y,b.z)));
        }
        const auto& axes=state->session.calculated_boundaries().back().solid_state_reference_views.at(restored.id)->axes;
        std::size_t checked=0;
        for(const auto& original:authored_axes)if(original.reference.owner_id==child.id) {
            const auto axis=std::ranges::find_if(axes,[&](const auto& a){return a.reference==original.reference;});
            check(axis!=axes.end(),"Restored spring lost its authored rotation axis");
            const auto p=gp_Pnt(original.point.x,original.point.y,original.point.z).Transformed(movement);
            const auto d=gp_Vec(original.direction.x,original.direction.y,original.direction.z).Transformed(movement);
            near(axis->point.x,p.X());near(axis->point.y,p.Y());near(axis->point.z,p.Z());
            near(axis->direction.x,d.X());near(axis->direction.y,d.Y());near(axis->direction.z,d.Z());
            near(axis->display_length,original.display_length);++checked;
        }
        check(checked>0,"Spring continuation has no axis evidence");
    }
    if(!initially_straight) {
        workspace::Workspace family;family.add_part(state->session.document(),state->session.calculated_boundaries());family.activate(id);
        document::FamilyTable table;table.columns={"Restore"};table.bindings["Restore"]={"feature",restored.id,{}};
        table.instances={{"Formed",{{"Restore","yes"}}},{"Straight",{{"Restore","no"}}}};
        static_cast<void>(workspace::set_family_table(family,id,table));
        for(const auto* name:{"Formed","Straight"}) {
            const auto member=workspace::open_family_instance(family,kernel,id,name,false);
            const auto* variant=family.open_part(member);
            verify(variant->session.document(),variant->session.calculated_boundaries().back(),std::string(name)=="Formed"?restored.id:straight.id);
        }
    }
    std::cout<<"Native curved continuation kind="<<sweep_kind<<", initially straight="<<initially_straight<<": state, coefficient, Restore, cold native reopen and Undo/Redo passed\n";
}
void verify(bool circle,bool flip,int attachment=0,int invalid=0,int sweep_kind=0,bool formed_first=false,bool chain=false) {
    const bool helical=sweep_kind==1,sweep=sweep_kind!=0;
    std::cerr<<"case circle="<<circle<<" flip="<<flip<<" attachment="<<attachment<<" invalid="<<invalid<<'\n';
    kernel::OcctKernel kernel;workspace::Workspace live;auto doc=document::PartDocument::create_default();
    if(doc.body_history.bodies().empty())static_cast<void>(doc.body_history.create_body("Body"));
    const auto id=doc.document_id;live.add_part(doc,{});live.activate(id);
    auto sketch=sketcher::Sketch::create_default();
    if(circle)static_cast<void>(sketch.add_circle(110,10,8));
    else static_cast<void>(sketch.add_rectangle(100,0,120,20));
    const auto axis=sketch.add_segment(0,-100,0,100,true);sketch.set_segment_centerline(axis,true);
    auto source=document::PartDocument::create_revolution_container(sketch.id);
    source.revolution.axis_segment_id=axis;source.revolution.angle_degrees=90;
    if(sweep) {
        source=test_support::sweep_fixture(helical?document::FeatureKind::HelicalSweep:
            sweep_kind==2?document::FeatureKind::Sweep2D:document::FeatureKind::Sweep3D);
        source.sweep_precision.custom_tolerance=1e-4;
        if(helical)source.helical.pitch=5;
        else if(sweep_kind==2) {
            auto path=sketcher::Sketch::from_serialized(source.sweep2d.path_sketch);path.segments.clear();path.points.clear();
            static_cast<void>(path.add_bspline({{0,0},{0,100./3},{20./3,200./3},{20,100}}));
            source.sweep2d.path_sketch=path.serialized();source.sweep2d.profiles.clear();
            document::PartDocument::reframe_sweep2d_sketches(source);
            const auto station=document::PartDocument::sweep2d_route(source).stations.front();
            const auto index=document::PartDocument::ensure_sweep2d_profile(source,station.point_id,station.incoming);
            auto section=sketcher::Sketch::from_serialized(source.sweep2d.profiles[index].sketch_serialized);
            static_cast<void>(section.add_circle(0,0,2));source.sweep2d.profiles[index].sketch_serialized=section.serialized();
        } else {
            source.sweep3d.path.curve_points.clear();source.sweep3d.path.curve_type=document::Curve3DType::InterpolatingSpline;
            for(const auto origin:std::vector<kernel::Vec3>{{0,0,0},{5,1,25},{15,5,50},{30,15,75}}) {
                auto point=document::PartDocument::create_construction(document::ConstructionKind::Point);
                point.parent_construction_id=source.sweep3d.path.id;point.origin=origin;source.sweep3d.path.curve_points.push_back(point);
            }
            source.sweep3d.profiles.front().point_id=source.sweep3d.path.curve_points.front().id;
        }
        workspace::commit_sweep(live,kernel,id,source,workspace::SweepEditMode::Create);
    } else workspace::commit_profile(live,kernel,id,source,workspace::ProfileEditMode::Create,sketch);
    const auto request=live.open_part(id)->session.document().kernel_operations().front().primitive;
    const auto plan=sweep?kernel.prepare_straightening(std::get<kernel::Sweep3DRequest>(request),.9):
        kernel.prepare_straightening(std::get<kernel::RevolutionRequest>(request),.9);
    auto straight=document::PartDocument::create_solid_state_container();straight.solid_state.coefficient=.9;
    if(!formed_first)check(workspace::commit_solid_state(live,kernel,id,straight),"Straighten failed");
    const auto join_center=formed_first?plan.source_end_centroid:plan.straight_end_centroid;
    auto join_tangent=plan.start_tangent;
    if(formed_first) {
        const auto& f=plan.end_transform;const auto t=join_tangent;
        join_tangent={f[1].x*t.x+f[2].x*t.y+f[3].x*t.z,
            f[1].y*t.x+f[2].y*t.y+f[3].y*t.z,f[1].z*t.x+f[2].z*t.y+f[3].z*t.z};
    }
    const auto join_owner=formed_first?source.id:straight.id;
    auto* state=live.open_part(id);const auto geometry=state->session.calculated_boundaries().back().mesh.original_references;
    const auto face=std::ranges::find_if(geometry.triangle_references,[&](const auto& ref) {
        const auto parent=formed_first?std::optional{std::pair{ref.owner_id,ref.semantic_key}}:kernel::solid_state_parent(ref.semantic_key);
        return ref.owner_id==join_owner&&parent&&parent->first==source.id&&
            (parent->second.starts_with("end:from:")||parent->second.starts_with("sweep:cap:end:from:"));
    });
    check(face!=geometry.triangle_references.end(),"No state-owned end cap");
    auto contour=sketcher::Sketch::create_default();
    static_cast<void>(contour.add_rectangle(-1,-1.5,1,1.5));
    auto child=document::PartDocument::create_extrusion_container(contour.id);child.extrusion.length_forward=4;
    child.placement.x=join_center.x;child.placement.y=join_center.y;child.placement.z=join_center.z;
    document::ConstructionReference ref;ref.owner_id=face->owner_id;ref.semantic_key=face->semantic_key;ref.supports_offset=true;ref.offset=invalid==5?2.5:-0.;ref.flip=flip;
    auto top=ref;top.orientation_only=true;top.orientation_drives_rotation=true;top.orientation_role="top";top.supports_offset=false;
    const auto triangle=static_cast<std::size_t>(face-geometry.triangle_references.begin())*3;
    const auto pa=geometry.vertices.at(geometry.triangles.at(triangle)),pb=geometry.vertices.at(geometry.triangles.at(triangle+1)),pc=geometry.vertices.at(geometry.triangles.at(triangle+2));
    const auto normal=gp_Vec(pb.x-pa.x,pb.y-pa.y,pb.z-pa.z).Crossed(gp_Vec(pc.x-pa.x,pc.y-pa.y,pc.z-pa.z)).Normalized();
    const kernel::Vec3 n{normal.X(),normal.Y(),normal.Z()};
    const bool reverse=(n.x*join_tangent.x+n.y*join_tangent.y+n.z*join_tangent.z)*(flip?-1:1)<0;
    child.extrusion.direction=(reverse!=(invalid==2))?document::ExtrusionDirection::Reverse:document::ExtrusionDirection::Forward;
    child.extrusion.length_reverse=4;
    if(invalid==1||invalid==3)child.placement.x+=1;
    child.placement.references={ref,top};child.placement.absolute_rotation_z=17;
    // The automatic owned profile is XZ: local Y rotates within its plane.
    // A local Z correction instead tilts the profile and breaks tangency.
    child.placement.rotation_offset_y=invalid==6?0:11;
    child.placement.rotation_offset_z=invalid==6?11:0;
    if(attachment==1||attachment==2||attachment==4) {
        const auto center=gp_Pnt(join_center.x,join_center.y,join_center.z);
        const auto on_end=[&](kernel::Vec3 p) {return std::abs((p.x-center.X())*n.x+(p.y-center.Y())*n.y+(p.z-center.Z())*n.z)<1e-7;};
        std::vector<kernel::ViewerPoint> points;
        for(const auto& p:geometry.points)if(p.reference.owner_id==join_owner&&on_end(p.position)&&
            std::ranges::none_of(points,[&](const auto& prior){return prior.reference==p.reference;}))points.push_back(p);
        check(points.size()>=2,"Source end has too few semantic points");
        auto anchor=ref;anchor.semantic_key=points[0].reference.semantic_key;anchor.supports_offset=false;anchor.offset=0;anchor.flip=false;
        auto direction=anchor;direction.orientation_only=true;direction.orientation_drives_rotation=true;direction.orientation_role="direction";
        if(attachment==2) {
            const auto edge=std::ranges::find_if(geometry.edges,[&](const auto& e){return e.reference.owner_id==join_owner&&e.points.size()==2&&on_end(e.points.front())&&on_end(e.points.back());});
            check(edge!=geometry.edges.end(),"Source end has no straight semantic edge");
            direction.semantic_key=edge->reference.semantic_key;
        } else direction.semantic_key=points[1].reference.semantic_key;
        child.placement.references=attachment==4?std::vector{ref,anchor,top}:std::vector{ref,anchor,top,direction};
        check(document::resolve_placement(child.placement,geometry),"Plane/point/direction fixture cannot be resolved");
    }
    workspace::commit_profile(live,kernel,id,child,workspace::ProfileEditMode::Create,contour);
    if(attachment==1||attachment==2||attachment==4) {
        // The ordinary command owns profile-plane framing. Place the test
        // rectangle's centroid using that actual frame, not a duplicate of its
        // automatic Sketch-plane convention.
        const auto operations=live.open_part(id)->session.document().kernel_operations();
        const auto operation=std::ranges::find(operations,child.id,&kernel::HistoryOperation::owner_id);
        const auto& request=std::get<kernel::ExtrusionRequest>(operation->primitive);
        const auto& vertices=std::get<kernel::ExtrusionRequest::PolygonProfile>(request.outer_profile).vertices;
        std::array<sketcher::SketchPoint,3> local;
        for(std::size_t i=0;i<3;++i)local[i]=*contour.find_point(request.outer_vertex_source_ids.at(i));
        const double u1=local[1].x-local[0].x,v1=local[1].y-local[0].y,u2=local[2].x-local[0].x,v2=local[2].y-local[0].y,det=u1*v2-u2*v1;
        const gp_Vec w1(vertices[1].x-vertices[0].x,vertices[1].y-vertices[0].y,vertices[1].z-vertices[0].z);
        const gp_Vec w2(vertices[2].x-vertices[0].x,vertices[2].y-vertices[0].y,vertices[2].z-vertices[0].z);
        const auto x=(w1*v2-w2*v1)/det,y=(w2*u1-w1*u2)/det;
        kernel::Vec3 center{};for(const auto& p:vertices){center.x+=p.x/vertices.size();center.y+=p.y/vertices.size();center.z+=p.z/vertices.size();}
        const auto target=join_center;const gp_Vec delta(target.x-center.x,target.y-center.y,target.z-center.z);
        near(x.Magnitude(),1);near(y.Magnitude(),1);near(x.Dot(y),0);
        for(auto& point:contour.points){point.x+=delta.Dot(x);point.y+=delta.Dot(y);}
        workspace::commit_profile(live,kernel,id,*live.open_part(id)->session.document().find_container(child.id),workspace::ProfileEditMode::Replace,contour);
    }
    std::optional<document::HistoryContainer> descendant;
    std::map<std::string,kernel::Vec3> descendant_points;
    if(attachment==3||chain) {
        const auto& geometry=live.open_part(id)->session.calculated_boundaries().back().mesh.original_references;
        const kernel::Vec3 end{join_center.x+4*join_tangent.x,
            join_center.y+4*join_tangent.y,join_center.z+4*join_tangent.z};
        const auto face=std::ranges::find_if(geometry.triangle_references,[&](const auto& f) {
            if(f.owner_id!=child.id||!f.surface||
                (!f.semantic_key.starts_with("end:from:")&&!f.semantic_key.starts_with("start:from:")))return false;
            const auto o=f.surface->origin,n=f.surface->axis;
            return std::abs((end.x-o.x)*n.x+(end.y-o.y)*n.y+(end.z-o.z)*n.z)<1e-7;
        });
        check(face!=geometry.triangle_references.end(),"Continuation has no semantic end cap");
        auto section=sketcher::Sketch::create_default();static_cast<void>(section.add_rectangle(-.4,-.6,.4,.6));
        auto next=document::PartDocument::create_extrusion_container(section.id);
        next.extrusion.length_forward=next.extrusion.length_reverse=3;
        next.placement.x=end.x;next.placement.y=end.y;next.placement.z=end.z;
        if(invalid==4)next.placement.x+=1;
        document::ConstructionReference ref;ref.owner_id=child.id;ref.semantic_key=face->semantic_key;ref.supports_offset=true;ref.offset=-0.;
        auto top=ref;top.orientation_only=true;top.orientation_drives_rotation=true;top.orientation_role="top";top.supports_offset=false;
        next.placement.references={ref,top};next.placement.absolute_rotation_z=23;
        const auto triangle=static_cast<std::size_t>(face-geometry.triangle_references.begin())*3;
        const auto a=geometry.vertices.at(geometry.triangles.at(triangle)),b=geometry.vertices.at(geometry.triangles.at(triangle+1)),c=geometry.vertices.at(geometry.triangles.at(triangle+2));
        const auto normal=gp_Vec(b.x-a.x,b.y-a.y,b.z-a.z).Crossed(gp_Vec(c.x-a.x,c.y-a.y,c.z-a.z));
        next.extrusion.direction=normal.Dot(gp_Vec(join_tangent.x,join_tangent.y,join_tangent.z))<0?
            document::ExtrusionDirection::Reverse:document::ExtrusionDirection::Forward;
        workspace::commit_profile(live,kernel,id,next,workspace::ProfileEditMode::Create,section);
        descendant=*live.open_part(id)->session.document().find_container(next.id);
        for(const auto& p:live.open_part(id)->session.calculated_boundaries().back().mesh.original_references.points)
            if(p.reference.owner_id==next.id)descendant_points.emplace(p.reference.semantic_key,p.position);
        check(descendant_points.size()>=8,"Descendant has no semantic corners");
    }
    state=live.open_part(id);const auto authored=*state->session.document().find_container(child.id);
    std::map<std::string,kernel::Vec3> original;
    for(const auto& point:state->session.calculated_boundaries().back().mesh.original_references.points)
        if(point.reference.owner_id==child.id)original.emplace(point.reference.semantic_key,point.position);
    check(original.size()>=8,"Attached contour has no semantic corner vertices");
    gp_Trsf movement;
    if(sweep) {
        const auto& f=plan.end_transform;
        movement.SetValues(f[1].x,f[2].x,f[3].x,0,f[1].y,f[2].y,f[3].y,0,f[1].z,f[2].z,f[3].z,0);
    } else {
        const auto& direction=std::get<kernel::RevolutionRequest>(request).axis_direction;
        movement.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(direction.x,direction.y,direction.z)),std::numbers::pi/2);
    }
    const auto& a=plan.straight_end_centroid;const auto& b=plan.source_end_centroid;
    movement.SetTranslationPart(gp_Vec(gp_Pnt(a.x,a.y,a.z).Transformed(movement),gp_Pnt(b.x,b.y,b.z)));
    if(formed_first)movement.Invert();
    auto restore=formed_first?straight:document::PartDocument::create_solid_state_container(true);
    if(invalid==3) {
        // No material-frame change: a new state of an independent source must
        // not reject this off-centre attachment on the already straight source.
        auto other_sketch=sketcher::Sketch::create_default();static_cast<void>(other_sketch.add_rectangle(300,0,320,20));
        const auto axis=other_sketch.add_segment(0,-100,0,100,true);other_sketch.set_segment_centerline(axis,true);
        auto other=document::PartDocument::create_revolution_container(other_sketch.id);
        other.revolution.axis_segment_id=axis;other.revolution.angle_degrees=60;
        workspace::commit_profile(live,kernel,id,other,workspace::ProfileEditMode::Create,other_sketch);
        auto independent=document::PartDocument::create_solid_state_container();
        independent.solid_state.all=false;independent.solid_state.owners={other.id};
        check(workspace::commit_solid_state(live,kernel,id,independent),"Independent source rejected an unchanged attachment");
        const auto verify_unchanged=[&](const kernel::BodyResult& result) {
            check(result.calculation_errors.empty(),"Independent source calculation failed");
            const auto& packet=*result.solid_state_reference_views.at(independent.id);
            for(const auto& [key,position]:original) {
                const auto found=std::ranges::find_if(packet.points,[&](const auto& p){return p.reference.owner_id==child.id&&p.reference.semantic_key==key;});
                check(found!=packet.points.end(),"Independent source lost an unchanged child vertex");
                near(found->position.x,position.x);near(found->position.y,position.y);near(found->position.z,position.z);
            }
        };
        verify_unchanged(state->session.calculated_boundaries().back());
        auto cold_document=state->session.document();kernel::OcctKernel cold;
        verify_unchanged(workspace::calculate_part_with_resolved_references(cold,cold_document).back());
        check(*state->session.document().find_container(child.id)==authored,"Independent source changed authored child placement");
        std::cout<<"Independent source leaves an off-centre attachment unchanged, including cold calculation\n";return;
    }
    if(invalid) {
        const auto snapshot=state->session.document().serialized();
        const auto fingerprint=state->session.calculated_boundaries().back().source_fingerprint;
        bool rejected=false;
        try{static_cast<void>(workspace::commit_solid_state(live,kernel,id,restore));}
        catch(const std::exception& error){rejected=std::string(error.what()).find("coincident profile centroids")!=std::string::npos;}
        check(rejected,"Disconnected or reversed continuation was not rejected explicitly");
        check(state->session.document().serialized()==snapshot&&state->session.calculated_boundaries().back().source_fingerprint==fingerprint,
            "Rejected continuity changed the document or calculated geometry");
        std::cout<<"Invalid continuation "<<invalid<<" rejected atomically\n";return;
    }
    const auto started=std::chrono::steady_clock::now();
    check(workspace::commit_solid_state(live,kernel,id,restore),"Restore failed");
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();

    const auto verify_result=[&](const kernel::BodyResult& result) {
        check(result.calculation_errors.empty(),"Restore has a calculation error");
        const auto& packet=*result.solid_state_reference_views.at(restore.id);
        for(const auto& [key,position]:original) {
            const auto found=std::ranges::find_if(packet.points,[&](const auto& p){return p.reference.owner_id==child.id&&p.reference.semantic_key==key;});
            check(found!=packet.points.end(),"Restore lost attached contour identity");
            const auto expected=gp_Pnt(position.x,position.y,position.z).Transformed(movement);
            near(found->position.x,expected.X());near(found->position.y,expected.Y());near(found->position.z,expected.Z());
        }
        for(const auto& [key,position]:descendant_points) {
            const auto found=std::ranges::find_if(packet.points,[&](const auto& p){return p.reference.owner_id==descendant->id&&p.reference.semantic_key==key;});
            check(found!=packet.points.end(),"Restore lost a descendant corner identity");
            const auto expected=gp_Pnt(position.x,position.y,position.z).Transformed(movement);
            near(found->position.x,expected.X());near(found->position.y,expected.Y());near(found->position.z,expected.Z());
        }
    };
    verify_result(state->session.calculated_boundaries().back());
    check(*state->session.document().find_container(child.id)==authored,"Restore rewrote authored placement");
    if(descendant)check(*state->session.document().find_container(descendant->id)==*descendant,"Restore rewrote descendant placement");
    if(descendant) {
        const auto generation=state->session.data_generation();
        const auto input=workspace::placement_edit_geometry(live,id,descendant->id);
        auto pending=descendant->placement;
        check(document::resolve_placement(pending,input),"Descendant editor cannot resolve its creation boundary");
        near(pending.x,descendant->placement.x);near(pending.y,descendant->placement.y);near(pending.z,descendant->placement.z);
        check(pending.references==descendant->placement.references&&state->session.data_generation()==generation,
            "Descendant editor changed stored identity or calculation generation");
    }
    const auto revision=state->session.revision(),generation=state->session.data_generation();
    check(!workspace::commit_solid_state(live,kernel,id,restore)&&state->session.revision()==revision&&state->session.data_generation()==generation,
        "Unchanged Restore created a calculation or transaction");
    check(workspace::step_part_document_history(live,id,false)&&workspace::step_part_document_history(live,id,true),"Undo/Redo failed");
    verify_result(state->session.calculated_boundaries().back());
    const auto path=std::filesystem::temp_directory_path()/(id+"-face-transfer.prtz");
    state->session.document().save(path,state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> saved;auto loaded=document::PartDocument::load(path,&saved);verify_result(saved.back());
    kernel::OcctKernel cold;verify_result(workspace::calculate_part_with_resolved_references(cold,loaded).back());
    check(*loaded.find_container(child.id)==authored&&std::signbit(loaded.find_container(child.id)->placement.references.front().offset),"Native reopen changed reference side");
    std::filesystem::remove(path);
    auto changed=*state->session.document().find_container(straight.id);changed.solid_state.coefficient=1.1;
    check(workspace::commit_solid_state(live,kernel,id,changed),"Coefficient edit failed");
    if(formed_first) {
        const auto a=plan.source_start_centroid,b=plan.straight_end_centroid;
        movement.SetTranslationPart(gp_Vec(movement.TranslationPart())+gp_Vec((b.x-a.x)*(.2/.9),(b.y-a.y)*(.2/.9),(b.z-a.z)*(.2/.9)));
    }
    verify_result(state->session.calculated_boundaries().back());
    if(formed_first) {
        restore=document::PartDocument::create_solid_state_container(true);
        check(workspace::commit_solid_state(live,kernel,id,restore),"Formed-first chain Restore failed");
        movement=gp_Trsf{};verify_result(state->session.calculated_boundaries().back());
        check(workspace::step_part_document_history(live,id,false)&&workspace::step_part_document_history(live,id,true),
            "Formed-first Restore Undo/Redo failed");
        verify_result(state->session.calculated_boundaries().back());
        state->session.document().save(path,state->session.calculated_boundaries());
        saved.clear();loaded=document::PartDocument::load(path,&saved);verify_result(saved.back());
        kernel::OcctKernel restored_cold;
        verify_result(workspace::calculate_part_with_resolved_references(restored_cold,loaded).back());
        check(*loaded.find_container(child.id)==authored,"Formed-first Restore changed authored placement");
        std::filesystem::remove(path);
    }
    if(descendant&&!flip&&!sweep&&!formed_first) {
        workspace::Workspace family;
        family.add_part(state->session.document(),state->session.calculated_boundaries());family.activate(id);
        document::FamilyTable table;table.columns={"Restore"};table.bindings["Restore"]={"feature",restore.id,{}};
        table.instances={{"Formed",{{"Restore","yes"}}},{"Straight",{{"Restore","no"}}}};
        static_cast<void>(workspace::set_family_table(family,id,table));
        const auto& authored_points=state->session.calculated_boundaries().back().mesh.original_references.points;
        for(const auto* name:{"Formed","Straight"}) {
            const auto member=workspace::open_family_instance(family,kernel,id,name,false);
            const auto& result=family.open_part(member)->session.calculated_boundaries().back();
            if(std::string(name)=="Formed")verify_result(result);
            else for(const auto& expected:authored_points)if(expected.reference.owner_id==child.id||expected.reference.owner_id==descendant->id) {
                const auto& points=result.mesh.original_references.points;
                const auto found=std::ranges::find_if(points,[&](const auto& p){return p.reference==expected.reference;});
                check(found!=points.end(),"Straight Family lost a chain vertex");
                near(found->position.x,expected.position.x);near(found->position.y,expected.position.y);near(found->position.z,expected.position.z);
            }
        }
    }
    if(!circle&&!flip&&!attachment&&!sweep) {
        auto moved_document=state->session.document();
        const auto local_child=*moved_document.find_container(child.id);
        auto body=*moved_document.body_history.find(moved_document.body_history.active_body_id());
        auto& placement=body.scope.placement;
        placement.x=17;placement.y=-9;placement.z=31;placement.rotation_z=placement.absolute_rotation_z=90;
        moved_document.body_history.update_body(std::move(body));
        kernel::OcctKernel moved_kernel;
        const auto result=workspace::calculate_part_with_resolved_references(moved_kernel,moved_document);
        check(result.back().calculation_errors.empty(),"Moved Body failed to restore its attached profile");
        const auto& packet=*result.back().solid_state_reference_views.at(restore.id);
        for(const auto& [key,position]:original) {
            const auto found=std::ranges::find_if(packet.points,[&](const auto& p){return p.reference.owner_id==child.id&&p.reference.semantic_key==key;});
            check(found!=packet.points.end(),"Moved Body lost an attached vertex");
            const auto expected=gp_Pnt(position.x,position.y,position.z).Transformed(movement);
            near(found->position.x,17-expected.Y());near(found->position.y,expected.X()-9);near(found->position.z,expected.Z()+31);
        }
        check(*moved_document.find_container(child.id)==local_child,"Moved Body rewrote the child's local placement");
    }
    std::cout<<(helical?"Helical":sweep_kind==2?"2D spline":sweep_kind==3?"3D spline":circle?"Circular":"Rectangular")<<" source, flip="<<flip<<", attachment="<<attachment<<": Restore "<<elapsed<<" ms; unchanged placement, no-op, Undo/Redo, cold native reopen and coefficient edit passed\n";
}
}
int main(int argc,char** argv)try {
    if(argc==2&&std::string(argv[1])=="--curved-continuation") {
        native_curved_chain(false);native_curved_chain(true);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--sweep-continuation") {
        for(int kind:{1,2})for(bool straight:{false,true})native_curved_chain(straight,kind);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--helical-continuation") {
        native_curved_chain(false,3);native_curved_chain(true,3);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--spline-chains") {
        for(int sweep:{2,3})verify(false,false,3,0,sweep);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--formed-chains") {
        for(bool circle:{false,true})for(bool flip:{false,true})verify(circle,flip,3,0,0,true);
        for(int sweep:{1,2,3})verify(false,false,3,0,sweep,true);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--formed-points") {
        for(int attachment:{1,2,4})for(bool flip:{false,true})verify(false,flip,attachment,0,0,true);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--formed-point-chains") {
        for(int attachment:{1,2,4})for(bool flip:{false,true})verify(false,flip,attachment,0,0,true,true);return 0;
    }
    for(bool circle:{false,true})for(bool flip:{false,true})verify(circle,flip);
    verify(false,false,1);verify(false,false,2);verify(false,false,3);verify(false,true,3);
    verify(false,false,4);verify(false,true,4);
    verify(false,false,0,1);verify(false,false,0,2);verify(false,false,0,3);verify(false,false,3,4);
    verify(false,false,0,5);verify(false,true,0,5);
    verify(false,false,0,6);
    helical_frame();verify(false,false,0,0,1);for(int sweep:{1,2,3})verify(false,false,3,0,sweep);return 0;
}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
