#include <zima/document/mech_keyway.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/keyway_catalog.hpp>
#include <zima/document/general_surface.hpp>
#include <zima/workspace/mech_keyway_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/modeling_confirmation.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/derived_copy_operations.hpp>
#include <zima/workspace/symbol_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <TopoDS.hxx>
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <source_location>
#include <stdexcept>

namespace {
using namespace zima;
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void near(double a,double b,const char* message){check(std::abs(a-b)<1e-8,message);}
template<class F> void rejected(F f,std::source_location source=std::source_location::current()){
    bool failed=false;try{f();}catch(const std::invalid_argument&){failed=true;}
    if(!failed)throw std::runtime_error("Invalid keyway accepted at line "+std::to_string(source.line()));
}
kernel::SurfaceGeometry cylinder(bool inside=false){kernel::SurfaceGeometry s;
    s.kind=kernel::SurfaceGeometry::Kind::Cylinder;s.radius=17.5;s.reversed=inside;return s;}
void placement_orientation(const document::HistoryContainer& source,const kernel::ViewerReferenceGeometry& refs){
    using namespace kernel;
    const auto calls=body_calculation_count.load();
    {
        const auto draft=workspace::resolve_mech_keyway_draft(source,"preview",refs);
        const auto dimensions=workspace::mech_keyway_dimensions(draft);
        check(std::ranges::none_of(dimensions,[](const auto& value){return value.reference.semantic_key=="parameter:mech_angle";}),
            "Keyway still exposes an independent angular dimension");
        const auto& p=draft.parent.placement;const auto& surface=*draft.parent.mech_keyway.support.surface;
        auto expected=dimension_sub(Vec3{p.x,p.y,p.z},surface.origin);
        expected=dimension_sub(expected,dimension_scale(draft.frame.axis,dimension_dot(expected,draft.frame.axis)));
        if(dimension_dot(expected,expected)<1e-14)expected=annotation_frame({}, {p.rotation_x,p.rotation_y,p.rotation_z}).axes[0];
        expected=dimension_unit(expected);
        near(dimension_dot(draft.frame.radial,expected),1,"Keyway does not follow its container placement");
        near(dimension_dot(draft.frame.radial,draft.frame.axis),0,"Keyway radial direction leaves the axial section plane");
    }
    check(body_calculation_count.load()==calls,"Placement inspection calculated material");
}
void editable_profile(const sketcher::Sketch& source){
    for(const bool locked:{false,true}) {
        auto profile=source;
        for(auto& d:profile.dimensions)d.locked=locked;
        const bool disc=!profile.circles.empty();
        const auto width_dimension=std::ranges::find(profile.dimensions,profile.id+":width",&sketcher::SketchDimension::id);
        const bool along_y=!disc&&width_dimension->kind==sketcher::DimensionKind::DistanceX;
        const auto longitudinal=[&](const auto* point){return along_y?point->y:point->x;};
        const auto dimension=profile.id+(disc?":disc-diameter":":width");
        const auto verify_extent=[&]{
            if(disc)return;
            const auto length=std::ranges::find(profile.dimensions,profile.id+":overall-length",&sketcher::SketchDimension::id);
            check(length!=profile.dimensions.end(),"Exploded slot has no overall-length dimension");
            const auto tip=[&](bool last){
                const auto arc=std::ranges::find(profile.arcs,profile.id+(last?":last-end":":first-end"),&sketcher::SketchArc::id);
                const double sense=longitudinal(profile.find_point(profile.id+":last-left"))>
                    longitudinal(profile.find_point(profile.id+":first-left"))?1.:-1.;
                return arc==profile.arcs.end()?longitudinal(profile.find_point(profile.id+(last?":last-left":":first-left"))):
                    longitudinal(profile.find_point(arc->center_point_id))+(last?arc->radius:-arc->radius)*sense;
            };
            near(std::abs(tip(true)-tip(false)),length->value,"Slot length is not tangent-to-tangent");
            near(longitudinal(profile.find_point(length->first_point_id)),std::min(tip(false),tip(true)),"First extension does not touch the end");
            near(longitudinal(profile.find_point(length->second_point_id)),std::max(tip(false),tip(true)),"Last extension does not touch the end");
        };
        const double original=std::ranges::find(profile.dimensions,dimension,&sketcher::SketchDimension::id)->value;
        for(const double value:{original+1,original-.5,original+2,original}) {
            check(profile.set_dimension_value(dimension,value),"Exploded profile dimension rejected an edit");
            const auto solved=profile.solve();
            check(solved.status==sketcher::SolveStatus::UnderConstrained&&solved.maximum_residual<1e-8,
                "Exploded profile constraints do not solve");
            if(disc)near(profile.circles.front().radius,value/2,"Editable disc diameter equation");
            else {
                for(const auto& point:profile.points)if(point.id.ends_with("-left")||point.id.ends_with("-right"))
                    near(std::abs(along_y?point.x:point.y),value/2,"Slot wall symmetry");
                for(const auto& arc:profile.arcs)near(arc.radius,value/2,"Slot radius no longer equals half width");
            }
            verify_extent();
        }
        if(!disc){
            const auto length=profile.id+":overall-length";
            const double original_length=std::ranges::find(profile.dimensions,length,&sketcher::SketchDimension::id)->value;
            for(double delta:{2.,-.5,1.,0.}){
                check(profile.set_dimension_value(length,original_length+delta),"Overall slot length edit failed");
                check(profile.solve().maximum_residual<1e-8,"Overall slot length residual");verify_extent();
            }
        }
        const auto points=profile.points;
        for(const auto& point:points)for(const double delta:{.5,-.25,1.}) {
            const auto before_point=*profile.find_point(point.id);
            const double x=before_point.x+(along_y?0:delta),y=before_point.y+(along_y?delta:0);
            if(point.id.ends_with("-extent")){
                // The native C/C intersection follows the authored curve;
                // its own drag cannot slide away from either support.
                const auto before=profile.serialized();
                check(!profile.move_point(point.id,x,y)&&profile.serialized()==before,
                    "An extent intersection escaped its supports during drag");
                verify_extent();continue;
            }
            if(!profile.move_point(point.id,x,y))
                throw std::runtime_error("Free exploded profile did not move during drag: "+point.id+" locked="+std::to_string(locked));
            near(longitudinal(profile.find_point(point.id)),longitudinal(&before_point)+delta,"Exploded profile did not reach the allowed axial position");
            check(profile.solve().maximum_residual<1e-8,"Profile drag escaped its constraints");
            verify_extent();
        }
        auto reopened=sketcher::Sketch::from_serialized(profile.serialized());
        check(reopened.serialized()==profile.serialized(),"Editable profile roundtrip changed constraints");
        const auto before=profile.serialized();
        check(!profile.set_dimension_value(dimension,0)&&profile.serialized()==before,"Degenerate profile edit changed geometry");
    }
}
void shaft_frame_matrix(){
    using namespace kernel;
    OcctKernel kernel;std::optional<double> removed;
    for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ})
    for(bool reverse:{false,true})for(bool placed:{false,true}) {
        auto part=document::PartDocument::create_default();
        document::BodyHistoryGraph graph;const auto body_id=graph.create_body("Shaft plane matrix");
        auto profile=sketcher::Sketch::create_default();profile.plane=plane;profile.plane_auto=false;
        static_cast<void>(profile.add_circle(0,0,17.5));
        auto shaft=document::PartDocument::create_feature_container(profile.id);
        shaft.feature.sides[0].length=80;profile.owner_container_id=shaft.id;
        part.sketches.push_back(profile);part.history.push_back(shaft);
        auto body=*graph.find(body_id);body.entries.push_back({document::PartHistoryKind::Feature,shaft.id});body.cursor=1;
        if(placed) {
            body.scope.placement.x=31;body.scope.placement.y=-17;body.scope.placement.z=53;
            body.scope.placement.absolute_rotation_x=31;body.scope.placement.absolute_rotation_y=22;
            body.scope.placement.absolute_rotation_z=-19;
        }
        graph.update_body(body);part.set_body_history(graph);
        workspace::PartCalculationPolicy policy;policy.reject_errors=true;
        auto base=workspace::calculate_part_with_resolved_references(kernel,part,nullptr,policy);
        const auto* input=workspace::calculated_operation_input(part,base,{});
        const auto axis=part.sketches.front().normal();
        auto parent=workspace::create_mech_keyway();kernel::FaceReference end;
        for(const auto& binding:input->kernel_faces)if(binding.reference.surface) {
            const auto& surface=*binding.reference.surface;
            if(surface.kind==SurfaceGeometry::Kind::Cylinder) {
                parent.mech_keyway.support=binding.reference;
                parent.mech_keyway.axial_direction=(dimension_dot(surface.axis,axis)>0?1:-1)*(reverse?-1:1);
            } else if(surface.kind==SurfaceGeometry::Kind::Plane&&
                    std::abs(dimension_dot(surface.origin,axis)-(reverse?80.:0.))<1e-7)end=binding.reference;
        }
        parent.mech_keyway.axial_offset=10;
        workspace::initialize_mech_keyway_placement(parent,input->mesh.original_references,end);
        const auto draft=workspace::resolve_mech_keyway_draft(parent,"plane-preview",input->mesh.original_references);
        near(dimension_dot(draft.cut.profile.resolved_normal,draft.frame.radial),1.,"Shaft local XY is not tangent to its surface");
        near(std::abs(dimension_dot(draft.cut.profile.resolved_y_axis,axis)),1.,"Shaft local Y does not follow the shaft axis");
        check(draft.cut_placement&&draft.cut_placement->references.size()>=2,"Shaft profile lost its source placement");
        workspace::Workspace live;live.add_part(part,base);
        check(workspace::commit_mech_keyway(live,kernel,part.document_id,parent),"Shaft frame matrix commit failed");
        auto* state=live.open_part(part.document_id);
        auto before=base.back(),after=state->session.calculated_boundaries().back();
        kernel.ensure_body_properties(before);kernel.ensure_body_properties(after);
        const double volume=before.volume-after.volume;
        if(removed)check(std::abs(volume-*removed)<1e-6,"Shaft orientation changed removed material");else removed=volume;
        check(workspace::explode_mech_keyway(live,part.document_id,parent.id),"Shaft frame matrix decomposition failed");
        auto reopened=document::PartDocument::from_serialized(state->session.document().serialized());
        auto replay=workspace::calculate_part_with_resolved_references(kernel,reopened,nullptr,policy);
        auto final=replay.back();kernel.ensure_body_properties(final);
        check(std::abs(final.volume-after.volume)<1e-6,"Shaft decomposition/reopen changed material");
    }
    std::cout<<"Shaft XY/XZ/YZ, both axial directions and placed Bodies: 12 native cases passed\n";
}
void definitions(){
    document::MechKeywayParameters p;
    p.support={"shaft","cylinder",{}};
    p.depth_upper_tolerance="0";p.depth_lower_tolerance="-0.2";
    const auto f=document::keyway_frame(cylinder(),{1,0,0},0);
    auto cut=document::keyway_cut_definition("cut",p,f,5);
    near(cut.measurement,30.3,"Opposite-side shaft dimension");
    near(cut.profile.resolved_origin.x,17.5,"Shaft Sketch must start on the support surface");
    near(cut.forward,0,"Shaft cut must not extrude outward");
    near(cut.reverse,p.depth,"Shaft cut must extrude inward by the table depth");
    check(cut.profile.arcs.size()==2&&cut.profile.segments.size()==2,"Closed slot topology");
    near(cut.nominal_start,5,"Start datum");near(cut.nominal_end,50,"Overall length");
    const auto left_id=cut.profile.segments.front().id;
    p.length=60;auto edited=document::keyway_cut_definition("cut",p,f,5);
    check(edited.profile.segments.front().id==left_id,"Wall identity changed on length edit");
    p.first_end=document::KeywayEnd::Open;p.first_overrun=10;
    auto opened=document::keyway_cut_definition("cut",p,f,5);
    check(opened.profile.arcs.size()==1&&opened.profile.segments.size()==3,"One open end topology");
    near(opened.profile.find_point("cut:profile:first-left")->x,-5,"Cutter overrun");
    p.retaining_screws=2;
    auto holes=document::keyway_cut_definition("cut",p,f,5);
    near(holes.retaining_centres[0].z,15,"First screw must follow key, not overrun");
    near(holes.retaining_centres[1].z,55,"Second screw position");
    p.last_end=document::KeywayEnd::Open;
    check(document::keyway_cut_definition("cut",p,f,5).profile.arcs.empty(),"Two open ends");
    check(document::load_mech_keyway(document::serialize_mech_keyway(p))==p,"Typed definition roundtrip");
    auto encoded=document::serialize_mech_keyway(p);
    check(encoded.at("version")==2&&!encoded.contains("angular_offset"),"Keyway persists an independent angle");
    encoded.erase("version");
    rejected([&]{static_cast<void>(document::load_mech_keyway(encoded));});
    encoded["version"]=1;
    rejected([&]{static_cast<void>(document::load_mech_keyway(encoded));});
    p.retaining_screws=0;p.position=document::KeywayPosition::Symmetric;
    auto symmetric=document::keyway_cut_definition("cut",p,f,30);
    near(symmetric.nominal_start,0,"Symmetric start");near(symmetric.nominal_end,60,"Symmetric end");
    p.axial_direction=-1;
    auto reverse=document::keyway_cut_definition("cut",p,f,30);
    near(reverse.nominal_start,60,"Reversed symmetric start");near(reverse.nominal_end,0,"Reversed symmetric end");
    p.position=document::KeywayPosition::TwoSided;p.limits[0].termination=document::KeywayTermination::UpTo;
    near(document::keyway_cut_definition("cut",p,f,60,{10,{}}).nominal_end,10,"Up To endpoint");
    rejected([&]{static_cast<void>(document::keyway_cut_definition("cut",p,f,60,{70,{}}));});
    p.limits[1].termination=document::KeywayTermination::UpTo;
    p.limits[0].correction=3;p.limits[1].correction=-2;
    auto two_sided=document::keyway_cut_definition("cut",p,f,60,{10,80});
    near(two_sided.nominal_start,78,"Reverse limit correction");near(two_sided.nominal_end,7,"Forward limit correction");
    check(document::load_mech_keyway(document::serialize_mech_keyway(p))==p,"Two-sided limits roundtrip");
    p.width=std::numeric_limits<double>::quiet_NaN();rejected([&]{document::validate_mech_keyway(p);});
}
void measurements(){
    for(bool internal:{false,true})for(double angle:{-135.,0.,90.}){
        document::MechKeywayParameters p;p.internal=internal;p.depth=internal?3.3:4.7;
        p.depth_upper_tolerance=internal?"+0.2":"0";p.depth_lower_tolerance=internal?"+0.1":"-0.2";
        const double radians=angle*std::numbers::pi/180;
        auto f=document::keyway_frame(cylinder(internal),{std::cos(radians),std::sin(radians),0},0);
        auto s=document::keyway_measurement_sketch("annotation",p,f);
        auto restored=sketcher::Sketch::from_serialized(s.serialized());
        near(restored.dimensions.front().value,internal?38.3:30.3,"Measured value");
        check(!restored.dimensions.front().driving&&!restored.dimensions.front().locked,"Reference dimension became driving");
        auto opposite=s.world_point(0,-17.5),floor=s.world_point(0,17.5+(internal?3.3:-4.7));
        near(std::hypot(std::hypot(floor.x-opposite.x,floor.y-opposite.y),floor.z-opposite.z),s.dimensions.front().value,"Independent world distance");
        check(restored.dimensions.front().lower_tolerance==p.depth_lower_tolerance,"Signed tolerance lost");
    }
}
void woodruff(){
    auto cone=cylinder();cone.kind=kernel::SurfaceGeometry::Kind::Cone;cone.radius=10;cone.semi_angle=.1;
    document::MechKeywayParameters p;p.type=document::KeywayType::Woodruff;
    p.width=4;p.key_height=7.5;p.woodruff_diameter=19;p.depth=5;p.axial_offset=20;p.measurement_station=20;
    const double radians=35*std::numbers::pi/180;
    auto f=document::keyway_frame(cone,{std::cos(radians),std::sin(radians),0},20);
    auto cut=document::keyway_cut_definition("cut",p,f,0);
    near(cut.forward+cut.reverse,4,"Woodruff cutter width");
    near(cut.profile.circles.front().radius,9.5,"Woodruff cutter radius");
    near(cut.nominal_end-cut.nominal_start,2*std::sqrt(7.5*11.5),"Woodruff chord");
    near(cut.measurement,5,"Woodruff direct depth");
    auto measured=document::keyway_measurement_sketch("annotation",p,f);
    near(measured.dimensions.front().value,5,"Woodruff annotation depth");
    near(measured.find_point("annotation:measurement:surface")->y,10+20*std::tan(.1),"Cone nominal surface at cutter centre");
    p.type=document::KeywayType::Parallel;
    rejected([&]{static_cast<void>(document::keyway_cut_definition("cut",p,f,0));});
    rejected([&]{static_cast<void>(document::keyway_frame(cone,{0,0,1},20));});
    rejected([&]{static_cast<void>(document::keyway_frame(cone,{1,0,0},-200));});
}
void native_cut(){
    kernel::OcctKernel kernel;
    auto part=document::PartDocument::create_default();
    document::BodyHistoryGraph graph;auto body_id=graph.create_body("Shaft");
    auto s=sketcher::Sketch::create_default();static_cast<void>(s.add_circle(0,0,17.5));
    auto shaft=document::PartDocument::create_extrusion_container(s.id);shaft.extrusion.height=100;
    shaft.extrusion.length_forward=100;s.owner_container_id=shaft.id;
    part.history.push_back(shaft);part.sketches.push_back(s);
    auto body=*graph.find(body_id);body.entries.push_back({document::PartHistoryKind::Feature,shaft.id});body.cursor=body.entries.size();
    graph.update_body(body);part.set_body_history(graph);
    auto calculated=kernel.evaluate_history(part.kernel_operations());
    check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Shaft calculation");
    auto refs=calculated.back().mesh.original_references;
    auto owner=workspace::create_mech_keyway();owner.mech_keyway.axial_offset=5;
    kernel::FaceReference axial_plane;
    for(const auto& face:refs.triangle_references)if(face.surface){
        if(face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder){owner.mech_keyway.support=face;owner.mech_keyway.internal=face.surface->reversed;
            owner.mech_keyway.axial_direction=face.surface->axis.z>0?1:-1;}
        if(face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&std::abs(face.surface->origin.z)<1e-8)
            axial_plane=face;
    }
    workspace::initialize_mech_keyway_placement(owner,refs,axial_plane);
    placement_orientation(owner,refs);
    {
        const auto original=workspace::resolve_mech_keyway_draft(owner,"preview",refs);
        const auto wire=workspace::mech_keyway_preview_edges(original);
        check(!wire.empty(),"Native keyway preview is empty");
        auto free=owner;free.placement.references.clear();
        for(const double shift:{3.,-2.,7.}){
            free.placement.z=owner.placement.z+shift;
            const auto moved=workspace::resolve_mech_keyway_draft(free,"preview",refs);
            near(moved.cut.profile.resolved_origin.z,original.cut.profile.resolved_origin.z+shift,"Numeric axial placement was ignored");
        }
        free.placement.x+=1;
        rejected([&]{static_cast<void>(workspace::resolve_mech_keyway_draft(free,"preview",refs));});
        free=owner;free.placement.references.clear();free.placement.absolute_rotation_x+=15;
        rejected([&]{static_cast<void>(workspace::resolve_mech_keyway_draft(free,"preview",refs));});
        free=owner;free.placement.references.clear();free.placement.x=free.placement.y=0;
        free.placement.absolute_rotation_x=free.placement.absolute_rotation_y=0;
        for(double degrees:{0.,-.01,.01,-90.,90.,180.,360.}){
            free.placement.absolute_rotation_z=degrees;
            const auto calls=kernel::body_calculation_count.load();
            const auto moved=workspace::resolve_mech_keyway_draft(free,"preview",refs);
            const double radians=degrees*std::numbers::pi/180;
            near(moved.frame.radial.x,std::cos(radians),"Numeric Origin rotation did not orient the keyway X direction");
            near(moved.frame.radial.y,std::sin(radians),"Numeric Origin rotation did not orient the keyway Y direction");
            placement_orientation(free,refs);
            check(kernel::body_calculation_count.load()==calls,"Numeric Origin rotation calculated material");
        }
    }
    auto prepared=workspace::prepare_mech_keyway(part,owner,refs);
    auto encoded=prepared.serialized();
    auto restored=document::PartDocument::from_serialized(encoded);
    check(restored.serialized()==encoded,"Native MECH history roundtrip");
    auto result=kernel.evaluate_history(restored.kernel_operations());
    if(!result.empty())for(const auto& [id,error]:result.back().calculation_errors)std::cerr<<id<<": "<<error<<'\n';
    check(result.size()==2&&result.back().calculation_errors.empty(),"Native keyway cut failed");
    // Independent exact cylinder/capsule integral; smooth substitution avoids
    // fitting the display mesh or reusing the profile builder as the oracle.
    const double R=17.5,r=5,floor=R-4.7;
    double removed=0;constexpr int intervals=2048;
    for(int i=0;i<=intervals;++i){
        const double a=-std::numbers::pi/2+i*std::numbers::pi/intervals;
        const double y=r*std::sin(a),c=std::cos(a);
        const double value=(45-2*r+2*r*c)*(std::sqrt(R*R-y*y)-floor)*r*c;
        removed+=(i==0||i==intervals?1:i%2?4:2)*value;
    }
    removed*=std::numbers::pi/(3*intervals);
    check(std::abs(calculated.back().volume-result.back().volume-removed)<1e-5,"Cut volume does not match the analytic keyway");
    const auto* parent=restored.find_container(owner.id);
    near(parent->mech_keyway.measurement_station,owner.mech_keyway.axial_direction*27.5,"Manufacturing section must intersect the actual floor");
    auto again=workspace::prepare_mech_keyway(restored,*parent,refs);
    check(again.serialized()==encoded,"Unchanged preparation changed native identities");
    {
        auto second=workspace::create_mech_keyway();second.mech_keyway=owner.mech_keyway;
        second.mech_keyway.axial_offset=65;second.mech_keyway.length=25;
        workspace::initialize_mech_keyway_placement(second,refs,axial_plane);
        auto pair=workspace::prepare_mech_keyway(restored,second,refs);
        const auto unchanged=workspace::prepare_mech_keyway(pair,*pair.find_container(owner.id),refs);
        check(unchanged.serialized()==pair.serialized(),"Preparing the first of two keyways reordered unchanged history or Sketches");
        workspace::PartCalculationPolicy policy;policy.reject_errors=true;
        auto pair_result=workspace::calculate_part_with_resolved_references(kernel,pair,nullptr,policy);
        const auto cold=document::PartDocument::from_serialized(pair.serialized(pair_result));
        for(const double offset:{60.,65.,62.}){
            auto changed=*pair.find_container(second.id);changed.mech_keyway.axial_offset=offset;
            pair=workspace::prepare_mech_keyway(pair,changed,refs);
            pair_result=workspace::calculate_part_with_resolved_references(kernel,pair,&pair_result,policy);
            check(pair_result.back().calculation_errors.empty(),"Successive second-keyway edits did not converge");
        }
        auto reopened=cold;
        check(workspace::calculate_part_with_resolved_references(kernel,reopened,nullptr,policy).back().calculation_errors.empty(),"Two keyways did not regenerate after native reopen");
    }
    for(const auto first:{document::KeywayEnd::Round,document::KeywayEnd::Open}) {
        auto edited=owner;edited.mech_keyway.first_end=first;edited.mech_keyway.bottom_radius=.4;
        if(first==document::KeywayEnd::Open)edited.mech_keyway.first_overrun=10;
        auto rounded=workspace::prepare_mech_keyway(part,edited,refs);
        auto r=kernel.evaluate_history(rounded.kernel_operations());
        if(!r.empty())for(const auto& [id,error]:r.back().calculation_errors)std::cerr<<id<<": "<<error<<'\n';
        check(r.size()==3&&r.back().calculation_errors.empty(),"Native bottom fillet failed");
    }
    auto invalid=prepared;
    invalid.history.back().mech_keyway_binding.role="cut";
    rejected([&]{invalid.validate_mech_keyways();});
    owner.mech_keyway.table_id="zima-csn-parallel";
    workspace::Workspace live;live.add_part(part,calculated);
    check(workspace::commit_mech_keyway(live,kernel,part.document_id,owner),"Keyway commit failed");
    auto* state=live.open_part(part.document_id);
    const auto annotation_id=std::ranges::find_if(state->session.document().history,[](const auto& item){
        return item.mech_keyway_binding.role=="annotation";})->id;
    const auto measurement=[&](const document::PartDocument& doc)->const sketcher::Sketch& {
        return *std::ranges::find(doc.sketches,annotation_id,&sketcher::Sketch::owner_container_id);
    };
    const auto& measured=measurement(state->session.document());
    check(measured.external_references.size()==2&&measured.constraints.size()==4,"Annotation lacks ordinary native references");
    near(measured.dimensions.front().value,30.3,"Bound floor measurement");
    const auto opposite_reference=measured.external_references.front().id;
    auto constrained=measured;
    for(const auto& point:measured.points)for(const double delta:{.5,-1.,2.}) {
        static_cast<void>(constrained.move_point(point.id,point.x+delta,point.y+delta));
        near(constrained.find_point(point.id)->x,point.x,"Fully constrained measurement point moved horizontally");
        near(constrained.find_point(point.id)->y,point.y,"Fully constrained measurement point moved vertically");
        check(constrained.external_references==measured.external_references,"Dragging changed an external source");
    }
    const auto committed=state->session.document().serialized();
    const auto revision=state->session.revision();
    const auto current=*state->session.document().find_container(owner.id);
    check(!workspace::commit_mech_keyway(live,kernel,part.document_id,current),"Unchanged OK created a transaction");
    check(state->session.revision()==revision,"Unchanged OK changed revision");
    auto broken=state->session.document();
    broken.find_container(owner.id)->mech_keyway.support.semantic_key="missing-support";
    const auto last_measurement=measurement(broken).serialized();
    const auto last_cut=std::ranges::find_if(broken.history,[](const auto& c){return c.mech_keyway_binding.role=="cut";})->placement;
    auto broken_result=workspace::calculate_part_with_resolved_references(kernel,broken,nullptr);
    check(broken.reference_errors.contains(owner.id),"Missing support was not diagnosed");
    check(measurement(broken).serialized()==last_measurement,"Missing support changed the last valid annotation");
    check(std::ranges::find_if(broken.history,[](const auto& c){return c.mech_keyway_binding.role=="cut";})->placement==last_cut,
        "Missing support moved the cut");
    auto broken_reopened=document::PartDocument::from_serialized(broken.serialized(broken_result));
    auto broken_replay=workspace::calculate_part_with_resolved_references(kernel,broken_reopened,nullptr);
    check(broken_reopened.reference_errors.contains(owner.id),"Reopening lost the broken dependency");
    broken_reopened.find_container(owner.id)->mech_keyway.support=current.mech_keyway.support;
    workspace::PartCalculationPolicy repair_policy;repair_policy.reject_errors=true;
    auto repaired=workspace::calculate_part_with_resolved_references(kernel,broken_reopened,nullptr,repair_policy);
    check(broken_reopened.reference_errors.empty()&&!repaired.empty(),"Replacing the support did not repair the group");
    auto resized=state->session.document();
    auto source=std::ranges::find(resized.sketches,s.id,&sketcher::Sketch::id);
    source->circles.front().radius=20;
    workspace::PartCalculationPolicy policy;policy.reject_errors=true;
    auto enlarged=workspace::calculate_part_with_resolved_references(kernel,resized,&state->session.calculated_boundaries(),policy);
    const auto* enlarged_parent=resized.find_container(owner.id);
    near(enlarged_parent->mech_keyway.width,12,"Native regeneration did not update table width");
    near(enlarged_parent->mech_keyway.length,45,"Native regeneration changed authored length");
    near(measurement(resized).dimensions.front().value,35.1,"Annotation did not follow shaft and floor changes");
    check(measurement(resized).external_references.front().id==opposite_reference,"Diameter edit replaced annotation identity");
    check(enlarged.back().calculation_errors.empty(),"Diameter change broke keyway calculation");
    workspace::commit_part_document(live,part.document_id,resized,enlarged);
    check(workspace::step_part_document_history(live,part.document_id,false),"Diameter Undo failed");
    check(state->session.document().serialized()==committed,"Diameter Undo changed authored references");
    check(workspace::step_part_document_history(live,part.document_id,true),"Diameter Redo failed");
    near(state->session.document().find_container(owner.id)->mech_keyway.width,12,"Diameter Redo lost automatic width");
    const auto compound=state->session.document().serialized();
    const auto shape=state->session.calculated_boundaries().back().kernel_shape;
    check(workspace::explode_mech_keyway(live,part.document_id,owner.id),"Full decomposition failed");
    check(!state->session.document().find_container(owner.id),"Decomposition retained the compound parent");
    check(state->session.calculated_boundaries().back().kernel_shape==shape,"Decomposition changed material");
    auto decomposed=state->session.document();
    const auto cut_id=std::ranges::find_if(resized.history,[](const auto& item){return item.mech_keyway_binding.role=="cut";})->id;
    editable_profile(*std::ranges::find(decomposed.sketches,cut_id,&sketcher::Sketch::owner_container_id));
    // A free cut remains a normal feature. Moving its floor must update the
    // reference dimension without the removed MECH parent doing the work.
    for(const double shift:{.5,-.25,1.}) {
        auto* free_cut=decomposed.find_container(cut_id);
        check(!free_cut->placement.references.empty(),"Exploded shaft lost its placement references");
        free_cut->feature.sides[1].length-=shift;
        const double expected=measurement(decomposed).dimensions.front().value+shift;
        auto updated=workspace::calculate_part_with_resolved_references(kernel,decomposed,nullptr,policy);
        near(measurement(decomposed).dimensions.front().value,expected,"Exploded annotation stopped following its real floor");
        check(measurement(decomposed).external_references.front().id==opposite_reference,"Exploded annotation identity changed");
        check(updated.back().calculation_errors.empty(),"Independent floor edit failed");
    }
    const auto file=std::filesystem::temp_directory_path()/("zima-mech-"+part.document_id+".prtz");
    auto disk_calculated=workspace::calculate_part_with_resolved_references(kernel,decomposed,nullptr,policy);
    decomposed.save(file,disk_calculated);
    std::vector<kernel::BodyResult> disk_boundaries;
    auto reopened=document::PartDocument::load(file,&disk_boundaries);
    std::filesystem::remove(file);
    check(measurement(reopened).serialized()==measurement(decomposed).serialized(),"Native file lost annotation bindings");
    auto replay=workspace::calculate_part_with_resolved_references(kernel,reopened,&disk_boundaries,policy);
    near(replay.back().volume,disk_calculated.back().volume,"Native file changed decomposed material");
    const auto allocations=state->session.document().dimension_identifiers.allocation_count();
    check(workspace::step_part_document_history(live,part.document_id,false),"Decomposition Undo failed");
    check(state->session.document().dimension_identifiers.allocation_count()==allocations,"Undo recycled reserved dimension numbers");
    auto restored_compound=state->session.document().serialized();
    auto original_compound=compound;
    // Dimension numbers intentionally remain reserved across Undo/Redo;
    // the derived calculation-definition hash includes those allocations.
    restored_compound.erase("dimension_identifiers");original_compound.erase("dimension_identifiers");
    restored_compound.erase("calculation_definition_fingerprint");original_compound.erase("calculation_definition_fingerprint");
    check(restored_compound==original_compound,"Decomposition Undo did not restore the full definition");
}
void catalogue(){
    document::MechKeywayParameters p;p.table_id="zima-csn-parallel";p.length=60;
    document::select_keyway_size(p,35);near(p.width,10,"Initial table width");near(p.depth,4.7,"CSN shaft depth");
    document::select_keyway_size(p,40);near(p.width,12,"Width did not follow changed diameter");near(p.depth,4.9,"Depth did not follow changed diameter");
    near(p.length,60,"Diameter change overwrote authored length");
    p.table_id="din6885-1-maedler";document::select_keyway_size(p,35);
    near(p.width,10,"DIN width");near(p.depth,5,"DIN and historical CSN depths were conflated");
    near(document::keyway_size("zima-csn-parallel",38).width,10,"Inclusive upper bound");
    near(document::keyway_size("zima-csn-parallel",38.001).width,12,"Exclusive lower bound");
    rejected([&]{static_cast<void>(document::keyway_size("din6885-1-maedler",100));});
    p.type=document::KeywayType::Woodruff;
    rejected([&]{document::select_keyway_size(p,35);});
    p.table_id="din6888-a-i";p.table_row_id="6x9x22";document::select_keyway_size(p,20);
    near(p.width,6,"DIN 6888 assignment I width");near(p.depth,6.6,"DIN 6888 shaft A depth");
    check(p.depth_upper_tolerance=="+0.1"&&p.depth_lower_tolerance=="0","Direct Woodruff depth deviations");
    p.table_id="din6888-b-i";document::select_keyway_size(p,20);near(p.depth,7.5,"DIN 6888 shaft B depth");
    p.internal=true;document::select_keyway_size(p,20);near(p.depth,1.7,"DIN 6888 hub B depth");
    p.table_id="din6888-a-ii";p.table_row_id="8x11x28";document::select_keyway_size(p,120);
    near(p.width,8,"Open-ended assignment II interval");near(p.depth,3,"DIN 6888 hub A depth");
    const auto& corrected=document::keyway_size("din6888-a-i",7,"2x3.7x10");
    near(corrected.height,3.7,"Manufacturer typo was copied into catalogue");
    near(corrected.disc_diameter,10,"Corrected Woodruff disc");
}
void native_variants(){
    kernel::OcctKernel kernel;
    const auto texture=symbols::Definition::load(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/
        "config/symbols/surface-texture/ZE-SURFACE-TEXTURE-ISO21920.symz");
    for(const auto variant:{"bore","bore-fillet","open-first","open-last","woodruff","woodruff-fillet","cone","cone-bore","cone-bore-generator","cone-bore-generator-fillet","retaining","retaining-precise","placed-bore","placed-cone-bore","placed-cone-bore-generator","placed-woodruff","woodruff-bore","cone-woodruff-bore"}) {
        std::cout<<"Variant "<<variant<<'\n';
        const bool bore=std::string_view(variant).find("bore")!=std::string_view::npos;
        const bool cone=std::string_view(variant).find("cone")!=std::string_view::npos;
        const bool woodruff=(cone&&!bore)||std::string_view(variant).find("woodruff")!=std::string_view::npos;
        auto part=document::PartDocument::create_default();
        if(std::string_view(variant)=="retaining-precise")part.document_precision["linear_tolerance"]="0.0000001";
        document::BodyHistoryGraph graph;const auto body_id=graph.create_body("Test Body");
        auto profile=sketcher::Sketch::create_default();
        static_cast<void>(profile.add_circle(0,0,bore?30:17.5));
        if(bore)static_cast<void>(profile.add_circle(0,0,17.5));
        auto shaft=document::PartDocument::create_feature_container(profile.id);
        shaft.feature.sides[0].length=80;
        if(cone)shaft.feature.sides[0].draft_angle_degrees=2;
        profile.owner_container_id=shaft.id;
        part.sketches.push_back(profile);part.history.push_back(shaft);
        auto body=*graph.find(body_id);body.entries.push_back({document::PartHistoryKind::Feature,shaft.id});body.cursor=1;
        if(std::string_view(variant).starts_with("placed-")){
            auto& placement=body.scope.placement;placement.x=31;placement.y=-17;placement.z=53;
            placement.rotation_x=placement.absolute_rotation_x=placement.rotation_offset_x=31;
            placement.rotation_y=placement.absolute_rotation_y=placement.rotation_offset_y=22;
            placement.rotation_z=placement.absolute_rotation_z=placement.rotation_offset_z=-19;
        }
        graph.update_body(body);part.set_body_history(graph);
        workspace::PartCalculationPolicy base_policy;base_policy.reject_errors=true;
        auto base=workspace::calculate_part_with_resolved_references(kernel,part,nullptr,base_policy);
        check(!base.empty()&&base.back().calculation_errors.empty(),"Variant support failed");
        auto parent=workspace::create_mech_keyway();auto& p=parent.mech_keyway;
        kernel::FaceReference axial_plane;
        p.internal=bore;p.depth=bore?3.3:4.7;p.axial_offset=20;
        const auto* input=workspace::calculated_operation_input(part,base,{});
        check(input!=nullptr,"Variant input boundary missing");
        for(const auto& face:input->mesh.original_references.triangle_references)if(face.surface) {
            if(!std::ranges::any_of(input->kernel_faces,[&](const auto& binding){return binding.reference==face;}))continue;
            const auto& s=*face.surface;
            if((s.kind==kernel::SurfaceGeometry::Kind::Cylinder||s.kind==kernel::SurfaceGeometry::Kind::Cone)&&s.reversed==bore) {
                p.support=face;p.axial_direction=s.axis.z>0?1:-1;
            }
            if(s.kind==kernel::SurfaceGeometry::Kind::Plane) {
                if(std::abs(s.origin.z)<1e-8)axial_plane=face;
                else if(std::abs(s.origin.z-80)<1e-8)p.limits[0].reference=face;
            }
        }
        if(bore){p.position=document::KeywayPosition::TwoSided;
            p.limits[0].termination=p.limits[1].termination=document::KeywayTermination::UpTo;
            p.limits[1].reference=axial_plane;
            p.first_end=p.last_end=document::KeywayEnd::Open;p.first_overrun=p.last_overrun=5;}
        if(woodruff){p.type=document::KeywayType::Woodruff;p.width=6;p.key_height=9;p.woodruff_diameter=22;p.depth=6.6;}
        if(std::string_view(variant).ends_with("fillet"))p.bottom_radius=.2;
        if(std::string_view(variant).find("generator")!=std::string_view::npos)p.floor=document::KeywayFloor::ParallelToGenerator;
        if(std::string_view(variant)=="open-first"){p.first_end=document::KeywayEnd::Open;p.first_overrun=25;}
        if(std::string_view(variant)=="open-last"){p.last_end=document::KeywayEnd::Open;p.last_overrun=25;}
        if(std::string_view(variant).starts_with("retaining")){
            p.retaining_screws=2;p.retaining_diameter=4;p.retaining_pitch=.7;p.retaining_depth=10;
        }
        workspace::Workspace live;live.add_part(part,base);
        workspace::initialize_mech_keyway_placement(parent,input->mesh.original_references,axial_plane);
        placement_orientation(parent,input->mesh.original_references);
        if(std::string_view(variant)=="bore"||std::string_view(variant)=="cone-bore-generator"||
                std::string_view(variant)=="woodruff"||std::string_view(variant)=="cone"||std::string_view(variant)=="open-first"){
            auto refs=input->mesh.original_references;
            document::history_reference_detail::append(refs,part.origin_viewer_mesh().original_references);
            document::history_reference_detail::append(refs,part.construction_viewer_mesh().original_references);
            refs=part.construction_reference_geometry_for(body_id,std::move(refs));
            for(double angle:{0.,90.,-90.,37.})for(bool reverse:{false,true}){
                std::cout<<"Placement matrix "<<variant<<": angle "<<angle<<", reverse "<<reverse<<'\n';
                auto placed=parent;placed.mech_keyway.axial_offset=0;
                placed.placement.references[1].offset=-20;
                document::ConstructionReference azimuth;
                azimuth.owner_id=body.origin().id;azimuth.semantic_key="origin:plane:yz";
                azimuth.supports_offset=true;azimuth.use_angle=true;azimuth.offset=angle;
                azimuth.orientation_role="position_only";placed.placement.references.push_back(azimuth);
                if(reverse&&bore){placed.mech_keyway.axial_direction*=-1;std::swap(placed.mech_keyway.limits[0],placed.mech_keyway.limits[1]);}
                if(!bore&&reverse)placed.placement.references.back().flip=true;
                check(document::resolve_placement(placed.placement,refs),"Angular hub placement failed");
                const auto draft=workspace::resolve_mech_keyway_draft(placed,"angular-preview",refs);
                placement_orientation(placed,refs);
                if(bore){
                    near(std::abs(kernel::dimension_dot(draft.cut.profile.resolved_normal,draft.frame.axis)),1.,"Angular hub Sketch is not transverse");
                    check(std::ranges::all_of(draft.cut_placement->references,[](const auto& r){return !r.use_angle||!r.orientation_drives_rotation;}),"Hub angle was promoted to orientation");
                }
                workspace::Workspace angular;angular.add_part(part,base);angular.activate(part.document_id);
                std::vector<symbols::Placement> angular_textures;
                if(woodruff&&!bore){
                    auto& parameters=placed.mech_keyway;
                    parameters.show_roughness=true;parameters.walls_symbol_id=placed.id+":walls";parameters.floor_symbol_id=placed.id+":floor";
                    for(int side=0;side<2;++side){symbols::Placement value;value.symbol.id=side?parameters.floor_symbol_id:parameters.walls_symbol_id;
                        value.symbol.definition=texture.serialized();value.symbol.variant=texture.default_variant;
                        value.symbol.text_values["Specification"]=side?"Ra 6.3":"Ra 3.2";angular_textures.push_back(std::move(value));}
                }
                check(workspace::commit_mech_keyway(angular,kernel,part.document_id,placed,angular_textures),"Angular keyway commit failed");
                auto* current=angular.open_part(part.document_id);
                const auto verify_woodruff_measurement=[&](const document::PartDocument& value){
                    if(!woodruff||bore)return;
                    const auto annotation=std::ranges::find_if(value.history,[&](const auto& c){return c.mech_keyway_binding.parent_id==placed.id&&c.mech_keyway_binding.role=="annotation";});
                    const auto measured=std::ranges::find(value.sketches,annotation->id,&sketcher::Sketch::owner_container_id);
                    const auto* floor=measured->find_point(measured->id+":floor");
                    const auto* surface=measured->find_point(measured->id+":surface");
                    const auto delta=kernel::dimension_sub(measured->world_point(floor->x,floor->y),measured->world_point(surface->x,surface->y));
                    near(std::sqrt(kernel::dimension_dot(delta,delta)),value.find_container(placed.id)->mech_keyway.depth,"Rotated Woodruff depth equation is wrong");
                    const auto ref=std::ranges::find(measured->external_references,measured->id+":reference:floor",&sketcher::SketchExternalReference::id);
                    check(ref!=measured->external_references.end()&&!ref->broken&&ref->source_semantic_key.starts_with("sketch-outline:")&&
                        ref->source_semantic_key.find("boolean:")==std::string::npos,"Woodruff depth depends on a Boolean seam fragment");
                    for(const auto& symbol:value.symbol_annotations)check(symbol.reference&&!symbol.unresolved,"Rotated Woodruff texture lost its support");
                };
                verify_woodruff_measurement(current->session.document());
                check(current->session.undo()&&current->session.redo(),"Angular hub Undo/Redo failed");
                auto reopened=document::PartDocument::from_serialized(current->session.document().serialized(current->session.calculated_boundaries()));
                auto cold=workspace::calculate_part_with_resolved_references(kernel,reopened,nullptr,base_policy);
                check(cold.back().calculation_errors.empty(),"Angular hub cold reopening failed");
                verify_woodruff_measurement(reopened);
                for(double delta:{.5,-.25}){
                    auto& source=*std::ranges::find(reopened.sketches,profile.id,&sketcher::Sketch::id);source.circles.back().radius+=delta;
                    cold=workspace::calculate_part_with_resolved_references(kernel,reopened,&cold,base_policy);
                    check(cold.back().calculation_errors.empty(),"Angular hub lost references after diameter edit");
                    verify_woodruff_measurement(reopened);
                }
            }
            std::cout<<"Angular keyway: four angles, both reference sides/directions, 20 mm datum, Undo/Redo, cold reopen and repeated diameter edits passed\n";
        }
        if(bore){
            for(int invalid=0;invalid<3;++invalid){
                auto blind=parent;
                if(invalid==0)blind.mech_keyway.limits[0].termination=document::KeywayTermination::Length;
                if(invalid==1)blind.mech_keyway.limits[1].reference={};
                if(invalid==2)blind.mech_keyway.limits[1].correction=-1;
                rejected([&]{static_cast<void>(workspace::resolve_mech_keyway_draft(blind,"preview",input->mesh.original_references));});
            }
        }
        {
            const auto calls=kernel::body_calculation_count.load();
            const auto draft=workspace::resolve_mech_keyway_draft(parent,"preview",input->mesh.original_references);
            const auto wire=workspace::mech_keyway_preview_edges(draft);
            check(!wire.empty(),"Variant authored preview is empty");
            const auto& surface=*p.support.surface;
            const auto residual=[&](kernel::Vec3 point) {
                const auto d=kernel::dimension_sub(point,surface.origin);
                const double t=kernel::dimension_dot(d,surface.axis);
                const auto r=kernel::dimension_sub(d,kernel::dimension_scale(surface.axis,t));
                return std::sqrt(kernel::dimension_dot(r,r))-surface.radius-(cone?t*std::tan(surface.semi_angle):0.);
            };
            near(residual({parent.placement.x,parent.placement.y,parent.placement.z}),0.,"Variant Origin is not on the exact support");
            if(!woodruff||bore)near(residual(draft.cut.profile.resolved_origin),0.,"Generated Sketch is not on the support");
            if(bore) {
                near(std::abs(kernel::dimension_dot(draft.cut.profile.resolved_normal,surface.axis)),1.,
                    "Hub profile must be perpendicular to the bore axis");
                unsigned samples=0;
                for(const auto& edge:wire)for(const auto& point:edge.points) {
                    const auto delta=kernel::dimension_sub(point,draft.cut.profile.resolved_origin);
                    const double depth=kernel::dimension_dot(delta,draft.cut.profile.resolved_normal);
                    check(std::abs(depth+draft.cut.reverse)<1e-8||std::abs(depth-draft.cut.forward)<1e-8||std::abs(depth)<1e-8,
                        "Hub authored wire leaves its exact axial stations");++samples;
                }
                check(samples>=4,"Hub authored preview has no transverse sections");
            }
            check(kernel::body_calculation_count.load()==calls,"Authored surface preview calculated material");
        }
        p.show_roughness=true;p.walls_symbol_id=parent.id+":walls";p.floor_symbol_id=parent.id+":floor";
        std::vector<symbols::Placement> textures;
        if(bore&&p.floor==document::KeywayFloor::ParallelToGenerator) {
            const auto prepared=workspace::prepare_mech_keyway(part,parent,input->mesh.original_references);
            const auto& cut=*std::ranges::find_if(prepared.history,[](const auto& c){return c.feature_kind==document::FeatureKind::Loft;});
            const auto draft=workspace::resolve_mech_keyway_draft(parent,cut.id,input->mesh.original_references);
            for(std::size_t i=0;i<cut.loft.sketches.size();++i) {
                const auto section=document::general_surface_display_sketch(cut,cut.loft.sketches[i]);
                const auto& expected=draft.cut.loft_sections[i];
                std::cout<<"Section "<<i<<" origin "<<section.resolved_origin.x<<','<<section.resolved_origin.y<<','<<section.resolved_origin.z
                    <<" expected "<<expected.resolved_origin.x<<','<<expected.resolved_origin.y<<','<<expected.resolved_origin.z<<'\n';
                for(std::size_t j=0;j<section.points.size();++j) {
                    const auto a=section.world_point(section.points[j].x,section.points[j].y);
                    const auto b=expected.world_point(expected.points[j].x,expected.points[j].y);
                    if(std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)>1e-8)std::cout<<"Point "<<j<<" actual "<<a.x<<','<<a.y<<','<<a.z<<" expected "<<b.x<<','<<b.y<<','<<b.z<<" axes "<<section.resolved_x_axis.x<<','<<section.resolved_x_axis.y<<','<<section.resolved_x_axis.z<<" / "<<section.resolved_y_axis.x<<','<<section.resolved_y_axis.y<<','<<section.resolved_y_axis.z<<'\n';
                    near(a.x,b.x,"Referenced section changed point X");near(a.y,b.y,"Referenced section changed point Y");near(a.z,b.z,"Referenced section changed point Z");
                }
            }
        }
        for(int side=0;side<2;++side){symbols::Placement value;value.symbol.id=side?p.floor_symbol_id:p.walls_symbol_id;
            value.symbol.definition=texture.serialized();value.symbol.variant=texture.default_variant;
            value.symbol.text_values["Specification"]=side?"Ra 6.3":"Ra 3.2";textures.push_back(std::move(value));}
        check(workspace::commit_mech_keyway(live,kernel,part.document_id,parent,textures),"Variant commit failed");
        const auto* state=live.open_part(part.document_id);
        const auto& next=state->session.document();const auto& result=state->session.calculated_boundaries();
        check(!result.empty()&&result.back().calculation_errors.empty(),"Variant calculation failed");
        check(next.symbol_annotations.size()==2,"MECH roughness instances were not persisted");
        {
            const auto calls=kernel::body_calculation_count.load();
            const auto revision=state->session.document().serialized();
            const auto annotations=workspace::drawing_annotation_sources(&live,part.document_id,{});
            bool width=false,depth=false;std::size_t symbols=0;
            for(const auto& source:annotations){
                symbols+=source.symbols.size();
                for(const auto& dimension:source.dimensions){
                    if(dimension.reference.owner_id==parent.id&&dimension.reference.semantic_key=="parameter:mech_width"){
                        near(dimension.value,next.find_container(parent.id)->mech_keyway.width,"Drawing keyway width is wrong");
                        check(!dimension.driving&&dimension.unit_suffix==p.width_fit,"Drawing keyway width lost its reference state or fit");width=true;
                    }
                    if(dimension.reference.semantic_key.ends_with(":floor-to-opposite")||dimension.reference.semantic_key.ends_with(":surface-to-floor"))depth=true;
                }
            }
            check(width&&depth&&symbols==2,"Drawing omitted MECH dimensions or real surface textures");
            check(kernel::body_calculation_count.load()==calls&&state->session.document().serialized()==revision,"Drawing MECH inspection calculated or changed the Part");
        }
        for(const auto& value:next.symbol_annotations)check(value.reference&&value.reference->owner_id!=parent.id&&!value.unresolved,
            "MECH roughness is not attached to a real generated native face");
        {
            // Open a compact native cache with no pre-existing annotations.
            // Enabling roughness must use saved exact faces without regeneration.
            auto bare=next;bare.symbol_annotations.clear();
            bare.find_container(parent.id)->mech_keyway.show_roughness=false;
            std::vector<kernel::BodyResult> cached;
            auto disk=document::PartDocument::from_serialized(bare.serialized(result,{},true),&cached);
            workspace::Workspace reopened_live;reopened_live.add_part(disk,cached);reopened_live.activate(disk.document_id);
            auto* reopened_state=reopened_live.open_part(disk.document_id);
            auto edit=*disk.find_container(parent.id);edit.mech_keyway.show_roughness=true;
            const auto calls=kernel::body_calculation_count.load();
            check(workspace::commit_mech_keyway(reopened_live,kernel,disk.document_id,edit,textures),"Cold annotation-only edit failed");
            check(kernel::body_calculation_count.load()==calls,"Cold annotation-only edit recalculated geometry");
            for(const auto& symbol:reopened_state->session.document().symbol_annotations)
                check(symbol.reference&&!symbol.unresolved,"Cold annotation-only edit lost its native attachment");
            const auto snapshot=reopened_state->session.document().serialized();
            rejected([&]{workspace::remove_symbol_annotation(reopened_live,disk.document_id,p.walls_symbol_id);});
            check(!workspace::commit_mech_keyway(reopened_live,kernel,disk.document_id,edit),"Native unchanged OK committed");
            check(reopened_state->session.document().serialized()==snapshot,"Native unchanged OK rewrote data");
            auto erased=reopened_state->session.document();erased.erase_history_object(parent.id);
            check(erased.symbol_annotations.empty(),"Deleting MECH left its owned surface textures behind");
            workspace::commit_part_document_metadata(reopened_live,disk.document_id,std::move(erased));
            check(reopened_state->session.undo()&&reopened_state->session.document().symbol_annotations.size()==2,"Undo did not restore the owned surface textures");
        }
        const auto* output=workspace::calculated_operation_input(state->session,{});
        check(output!=nullptr,"Variant output boundary missing");
        auto input_properties=*input,output_properties=*output;
        kernel.ensure_body_properties(input_properties);kernel.ensure_body_properties(output_properties);
        check(input_properties.volume-output_properties.volume>1,"Variant did not remove material");
        if(std::string_view(variant).starts_with("retaining")||std::string_view(variant)=="woodruff-fillet"||
                std::string_view(variant)=="placed-cone-bore"){
            workspace::Workspace pattern_live;pattern_live.add_part(next,result);
            auto edit=workspace::prepare_derived_copy_edit(pattern_live,part.document_id,{},true);
            check(std::ranges::any_of(edit.sources.items,[&](const auto& item){return item.id==parent.id;}),"MECH is missing from ordinary Pattern sources");
            for(const auto& item:edit.sources.items)check(next.find_container(item.id)->mech_keyway_binding.parent_id.empty(),"Pattern offers a managed MECH child independently");
            auto pattern=edit.initial;pattern.parameters.source_ids={parent.id};
            pattern.parameters.pattern->circular=true;pattern.parameters.pattern->count=2;pattern.parameters.pattern->angle_degrees=180;
            check(workspace::commit_derived_copy(pattern_live,kernel,edit,pattern),"MECH Pattern commit failed");
            auto* pattern_state=pattern_live.open_part(part.document_id);
            auto patterned=*workspace::calculated_operation_input(pattern_state->session,{});kernel.ensure_body_properties(patterned);
            const auto precise_volume=[](const auto& result){GProp_GProps mass;
                const auto shape=kernel::read_calculated_body_shape(result);
                // The previous 1e-11 integration request left an 8e-5 mm^3
                // quadrature difference at the bore/slot intersection rims.
                const auto error=BRepGProp::VolumePropertiesGK(shape,mass,1.e-13,true,true);
                check(error>=0&&error<1.e-11,"Independent mass integration did not converge");return mass.Mass();};
            const double before_volume=precise_volume(input_properties),after_volume=precise_volume(output_properties),copy_volume=precise_volume(patterned);
            double volume_tolerance=2e-5;
            if(std::string_view(variant)=="woodruff-fillet"){
                const auto fillet=std::ranges::find_if(next.history,[](const auto& c){return c.mech_keyway_binding.role=="bottom-fillet";});
                std::vector<kernel::CalculatedTopologyReference> faces;
                std::set<std::string> face_keys;
                for(const auto& face:output_properties.kernel_faces)if(face.reference.owner_id==fillet->id&&face_keys.insert(face.reference.semantic_key).second)faces.push_back(face.reference);
                check(!faces.empty(),"Woodruff fillet has no authored face identity");
                const auto topology=kernel::read_calculated_body_topology(output_properties,faces);
                const auto draft=workspace::resolve_mech_keyway_draft(*next.find_container(parent.id),"equation",input->mesh.original_references);
                const auto& f=draft.frame;const double r=p.bottom_radius,R=p.woodruff_diameter/2;
                const auto centre=kernel::Vec3{f.origin.x+f.radial.x*(f.radius-p.depth+R),f.origin.y+f.radial.y*(f.radius-p.depth+R),f.origin.z+f.radial.z*(f.radius-p.depth+R)};
                const double linear=std::stod(next.document_precision.at("linear_tolerance"));
                double area=0,maximum_error=0;unsigned samples=0;
                for(const auto& group:topology.referenced_shapes)for(const auto& shape:group){
                    const auto face=TopoDS::Face(shape);BRepAdaptor_Surface surface(face);
                    double face_error=0;
                    GProp_GProps properties;BRepGProp::SurfaceProperties(face,properties,1e-10);area+=properties.Mass();
                    if(surface.GetType()==GeomAbs_Plane)continue;
                    for(int u=1;u<32;++u)for(int v=1;v<32;++v){
                        const double a=surface.FirstUParameter()+(surface.LastUParameter()-surface.FirstUParameter())*u/32;
                        const double b=surface.FirstVParameter()+(surface.LastVParameter()-surface.FirstVParameter())*v/32;
                        if(BRepClass_FaceClassifier(face,gp_Pnt2d(a,b),1e-9).State()!=TopAbs_IN)continue;
                        const auto q=surface.Value(a,b);
                        const auto component=[&](const auto& axis){return (q.X()-centre.x)*axis.x+(q.Y()-centre.y)*axis.y+(q.Z()-centre.z)*axis.z;};
                        const double rho=std::hypot(component(f.radial),component(f.axis));
                        const double side=std::abs(component(f.tangent))-(p.width/2-r);
                        const double error=std::abs(std::hypot(rho-(R-r),side)-r);
                        face_error=std::max(face_error,error);maximum_error=std::max(maximum_error,error);++samples;
                    }
                    std::cout<<"Fillet surface type "<<int(surface.GetType())<<" equation error "<<face_error<<'\n';
                }
                check(samples>20&&maximum_error<=linear,"Woodruff floor fillet violates its independent torus equation");
                // The ordinary Fillet publishes a tolerance-bounded surface.
                // Bound the volume comparison by its affected area, while
                // keeping the strict analytic gate for cuts without a fillet.
                volume_tolerance+=2*area*linear;
                std::cout<<"Woodruff fillet: "<<samples<<" exact-surface samples, maximum radial error "<<maximum_error<<", Pattern volume error "<<std::abs(before_volume-copy_volume-2*(before_volume-after_volume))<<", bound "<<volume_tolerance<<'\n';
            }
            std::cout<<"Pattern "<<variant<<" volume error "<<std::abs(before_volume-copy_volume-2*(before_volume-after_volume))<<" tolerance "<<volume_tolerance<<'\n';
            check(std::abs(before_volume-copy_volume-2*(before_volume-after_volume))<volume_tolerance,
                "Pattern did not preserve the complete MECH cut/fillet/hole material");
            auto roundtrip=document::PartDocument::from_serialized(pattern_state->session.document().serialized(pattern_state->session.calculated_boundaries()));
            workspace::PartCalculationPolicy verify;verify.reject_errors=true;
            auto replay=workspace::calculate_part_with_resolved_references(kernel,roundtrip,nullptr,verify);
            check(!replay.empty()&&replay.back().calculation_errors.empty(),"MECH Pattern cold regeneration failed");
            check(workspace::explode_mech_keyway(pattern_live,part.document_id,parent.id),"Pattern source could not be decomposed");
            auto decomposed=pattern_state->session.document();
            auto replay_exploded=workspace::calculate_part_with_resolved_references(kernel,decomposed,nullptr,verify);
            check(!replay_exploded.empty()&&replay_exploded.back().calculation_errors.empty(),"Decomposition broke the dependent Pattern");
        }
        if(bore){
            const auto& surface=*p.support.surface;
            const auto& value=next.find_container(parent.id)->mech_keyway;
            const double half_width=value.width/2;
            const double slope=cone?std::tan(surface.semi_angle):0;
            const double middle_radius=surface.radius+value.measurement_station*slope;
            const auto area=[&](double z){
                const double axial=(z-surface.origin.z)*surface.axis.z;
                const double radius=surface.radius+axial*slope;
                const double floor=(value.floor==document::KeywayFloor::ParallelToGenerator?radius:middle_radius)+value.depth;
                return value.width*floor-half_width*std::sqrt(radius*radius-half_width*half_width)-
                    radius*radius*std::asin(half_width/radius);
            };
            constexpr int subdivisions=512;double volume=0;
            for(int i=0;i<=subdivisions;++i)volume+=(i==0||i==subdivisions?1:(i%2?4:2))*area(80.*i/subdivisions);
            volume*=80./(3*subdivisions);
            const double floor_slope=value.floor==document::KeywayFloor::ParallelToGenerator?slope:0.;
            volume-=2*(1-std::numbers::pi/4)*value.bottom_radius*value.bottom_radius*80*std::hypot(1.,floor_slope);
            const auto exact_volume=[&](const auto& result) {
                if(value.bottom_radius==0)return result.volume;
                GProp_GProps mass;
                const auto shape=kernel::read_calculated_body_shape(result);
                const auto error=BRepGProp::VolumePropertiesGK(shape,mass,1.e-13,true,true);
                check(error>=0&&error<1.e-11,"Hub mass integration did not converge");return mass.Mass();
            };
            const auto removed=exact_volume(input_properties)-exact_volume(output_properties);
            double volume_tolerance=2e-5;
            if(value.bottom_radius>0) {
                const auto fillet=std::ranges::find_if(next.history,[](const auto& c){return c.mech_keyway_binding.role=="bottom-fillet";});
                std::vector<kernel::CalculatedTopologyReference> faces;std::set<std::string> keys;
                for(const auto& face:output_properties.kernel_faces)if(face.reference.owner_id==fillet->id&&keys.insert(face.reference.semantic_key).second)faces.push_back(face.reference);
                const auto topology=kernel::read_calculated_body_topology(output_properties,faces);
                const auto draft=workspace::resolve_mech_keyway_draft(*next.find_container(parent.id),"equation",input->mesh.original_references);
                const auto& frame=draft.frame;const double r=value.bottom_radius;
                const auto normal=kernel::dimension_unit(kernel::dimension_sub(frame.radial,kernel::dimension_scale(frame.axis,floor_slope)));
                const auto floor=kernel::dimension_add(frame.origin,kernel::dimension_scale(frame.radial,frame.radius+value.depth));
                const double linear=std::stod(next.document_precision.at("linear_tolerance"));
                double area=0,maximum_error=0;unsigned samples=0;
                for(const auto& group:topology.referenced_shapes)for(const auto& shape:group) {
                    const auto face=TopoDS::Face(shape);BRepAdaptor_Surface adapted(face);
                    GProp_GProps properties;BRepGProp::SurfaceProperties(face,properties,1e-10);area+=properties.Mass();
                    for(int u=1;u<32;++u)for(int v=1;v<32;++v) {
                        const double a=adapted.FirstUParameter()+(adapted.LastUParameter()-adapted.FirstUParameter())*u/32;
                        const double b=adapted.FirstVParameter()+(adapted.LastVParameter()-adapted.FirstVParameter())*v/32;
                        if(BRepClass_FaceClassifier(face,gp_Pnt2d(a,b),1e-9).State()!=TopAbs_IN)continue;
                        const auto q=adapted.Value(a,b);
                        const auto delta=kernel::dimension_sub(kernel::Vec3{q.X(),q.Y(),q.Z()},floor);
                        const double across=std::abs(kernel::dimension_dot(delta,frame.tangent))-(value.width/2-r);
                        const double depth=kernel::dimension_dot(delta,normal)+r;
                        maximum_error=std::max(maximum_error,std::abs(std::hypot(across,depth)-r));++samples;
                    }
                }
                std::cout<<"Hub fillet: "<<samples<<" exact-surface samples, cylinder residual "<<maximum_error<<", linear tolerance "<<linear<<'\n';
                check(samples>20&&maximum_error<=linear,"Hub fillet violates its independent cylinder equation");
                // OCCT represents a fillet on Loft's spline faces within the
                // document tolerance; retain that existing geometric contract.
                volume_tolerance+=area*linear;
            }
            std::cout<<"Hub removed volume "<<removed<<" expected "<<volume<<" error "<<std::abs(removed-volume)<<'\n';
            check(std::abs(removed-volume)<volume_tolerance,
                "Hub keyway material disagrees with independent cylinder/cone integral");
        }
        const auto annotation=std::ranges::find_if(next.history,[](const auto& c){return c.mech_keyway_binding.role=="annotation";});
        const auto measured=std::ranges::find(next.sketches,annotation->id,&sketcher::Sketch::owner_container_id);
        check(measured->external_references.size()==2,"Variant annotation is not referenced");
        const auto& current=next.find_container(parent.id)->mech_keyway;
        const auto& surface=*p.support.surface;
        const double radius=surface.radius+current.measurement_station*(cone?std::tan(surface.semi_angle):0);
        near(measured->dimensions.front().value,woodruff&&!bore?p.depth:2*radius+(bore?p.depth:-p.depth),"Variant manufacturing dimension is not exact");
        auto reopened=document::PartDocument::from_serialized(next.serialized(result));
        workspace::PartCalculationPolicy policy;policy.reject_errors=true;
        auto regenerated=workspace::calculate_part_with_resolved_references(kernel,reopened,nullptr,policy);
        check(regenerated.back().calculation_errors.empty(),"Variant cold regeneration failed");
        const auto annotation_id=annotation->id;
        const auto measured_id=measured->dimensions.front().id;
        double expected=measured->dimensions.front().value;
        for(const double radius_change:{.5,-.25,.75}) {
            auto& source=*std::ranges::find(reopened.sketches,profile.id,&sketcher::Sketch::id);
            source.circles.back().radius+=radius_change;
            regenerated=workspace::calculate_part_with_resolved_references(kernel,reopened,&regenerated,policy);
            const auto updated=std::ranges::find(reopened.sketches,annotation_id,&sketcher::Sketch::owner_container_id);
            expected+=woodruff&&!bore?0:2*radius_change;
            near(updated->dimensions.front().value,expected,"Repeated diameter edits broke manufacturing dimension");
            check(updated->dimensions.front().id==measured_id,"Repeated edits changed dimension identity");
        }
        const auto cut_id=std::ranges::find_if(next.history,[](const auto& item){return item.mech_keyway_binding.role=="cut";})->id;
        check(workspace::explode_mech_keyway(live,part.document_id,parent.id),"Variant decomposition failed");
        const auto& decomposed=live.open_part(part.document_id)->session.document();
        const auto* free_cut=decomposed.find_container(cut_id);
        if(free_cut->feature_kind==document::FeatureKind::Loft) {
            for(const auto& owned:free_cut->loft.sketches) {
                check(owned.sketch_feature&&owned.sketch_feature->placement.references.size()>=2&&
                    std::ranges::any_of(owned.sketch_feature->placement.references,[&](const auto& r){return r.owner_id==p.support.owner_id&&r.semantic_key==p.support.semantic_key;})&&
                    std::ranges::any_of(owned.sketch_feature->placement.references,[&](const auto& r){return std::ranges::any_of(p.limits,[&](const auto& limit){return r.owner_id==limit.reference.owner_id&&r.semantic_key==limit.reference.semantic_key;});}),
                    "Decomposed hub section lost its source/end references");
                editable_profile(sketcher::Sketch::from_serialized(owned.sketch_serialized));
            }
            auto edited=decomposed;
            for(double delta:{.5,-.25}) {
                auto& source=*std::ranges::find(edited.sketches,profile.id,&sketcher::Sketch::id);
                source.circles.back().radius+=delta;
                edited.find_container(shaft.id)->feature.sides[0].length+=2;
                edited.find_container(shaft.id)->feature.sides[0].draft_angle_degrees+=delta;
                auto replay=workspace::calculate_part_with_resolved_references(kernel,edited,nullptr,policy);
                check(replay.back().calculation_errors.empty(),"Decomposed cone hub lost editable source references");
                const auto& current_cut=*edited.find_container(cut_id);
                const auto source_geometry=workspace::construction_reference_source_geometry(replay);
                const auto& source_faces=source_geometry.triangle_references;
                const auto current_face=std::ranges::find_if(source_faces,[&](const auto& face){return face.owner_id==p.support.owner_id&&face.semantic_key==p.support.semantic_key&&face.instance_path==p.support.instance_path;});
                check(current_face!=source_faces.end()&&current_face->surface,"Edited cone support is unavailable");
                const auto& current_surface=*current_face->surface;
                // Owned sections are Body-local; the complete source packet
                // carries the displayed Part frame, including Body placement.
                const auto& scope=edited.body_owner_for_object(cut_id)->scope;
                const auto body_frame=kernel::annotation_frame(scope.translation(),scope.rotation_degrees());
                const auto world_direction=[&](kernel::Vec3 v){return kernel::dimension_add(
                    kernel::dimension_add(kernel::dimension_scale(body_frame.axes[0],v.x),
                        kernel::dimension_scale(body_frame.axes[1],v.y)),
                    kernel::dimension_scale(body_frame.axes[2],v.z));};
                for(const auto& owned:current_cut.loft.sketches) {
                    check(owned.sketch_feature->placement.reference_valid,"Decomposed section references are unresolved");
                    const auto section=document::general_surface_display_sketch(current_cut,owned);
                    near(std::abs(kernel::dimension_dot(world_direction(section.resolved_normal),current_surface.axis)),1.,
                        "Changing cone angle tilted the transverse section");
                    const auto world_origin=kernel::dimension_add(scope.translation(),world_direction(section.resolved_origin));
                    const auto displacement=kernel::dimension_sub(world_origin,current_surface.origin);
                    const auto station=kernel::dimension_dot(displacement,current_surface.axis);
                    const auto radial=kernel::dimension_sub(displacement,kernel::dimension_scale(current_surface.axis,station));
                    near(std::sqrt(kernel::dimension_dot(radial,radial)),current_surface.radius+station*std::tan(current_surface.semi_angle),
                        "Decomposed section left the changed exact cone");
                }
                auto disk=document::PartDocument::from_serialized(edited.serialized(replay));
                auto cold=workspace::calculate_part_with_resolved_references(kernel,disk,nullptr,policy);
                auto before=replay.back(),after=cold.back();kernel.ensure_body_properties(before);kernel.ensure_body_properties(after);
                check(std::abs(before.volume-after.volume)<1e-7,"Edited decomposed cone hub changed on reopening");
            }
        } else editable_profile(*std::ranges::find(decomposed.sketches,cut_id,&sketcher::Sketch::owner_container_id));
    }
}
}
#include "mech_keyway_confirmation_contract.inc"
int main(int argc,char** argv){try{
    if(argc==4&&std::string_view(argv[1])=="--repair-keyway"){
        std::vector<kernel::BodyResult> cache;auto part=document::PartDocument::load(argv[2],&cache);
        const auto owner_it=std::ranges::find_if(part.history,[](const auto& c){return c.feature_kind==document::FeatureKind::MechKeyway;});
        check(owner_it!=part.history.end(),"User document has no keyway");
        auto owner=*owner_it;
        owner.name+=" verified";
        workspace::Workspace live;live.add_part(part,cache);live.activate(part.document_id);kernel::OcctKernel engine;
        check(workspace::commit_mech_keyway(live,engine,part.document_id,owner),"User keyway repair did not commit");
        const auto* state=live.open_part(part.document_id);
        check(state->session.calculated_boundaries().back().calculation_errors.empty(),"User keyway repair failed");
        state->session.document().save(argv[3],state->session.calculated_boundaries());
        auto reopened=document::PartDocument::load(argv[3]);workspace::PartCalculationPolicy policy;policy.reject_errors=true;
        auto cold=workspace::calculate_part_with_resolved_references(engine,reopened,nullptr,policy);
        check(cold.back().calculation_errors.empty(),"Repaired keyway cold regeneration failed");
        std::cout<<"Saved user keyway repaired and cold regenerated\n";return 0;
    }
    if(argc==2&&std::string_view(argv[1])=="--confirmation"){keyway_confirmation();return 0;}
    if(argc==5&&std::string_view(argv[1])=="--source-prefix") {
        auto part=document::PartDocument::load(argv[2]);
        const auto count=static_cast<std::size_t>(std::stoull(argv[3]));
        check(count>0&&count<=part.history.size(),"Invalid source history boundary");
        while(part.history.size()>count)part.erase_history_object(part.history.back().id);
        kernel::OcctKernel engine;workspace::PartCalculationPolicy policy;policy.reject_errors=true;
        auto calculated=workspace::calculate_part_with_resolved_references(engine,part,nullptr,policy);
        part.save(argv[4],calculated);std::cout<<"Saved calculated source prefix "<<count<<'\n';return 0;
    }
    if(argc==2&&std::string_view(argv[1])=="--shaft-frames"){shaft_frame_matrix();return 0;}
    if(argc==2&&std::string_view(argv[1])=="--placement-orientation"){native_cut();return 0;}
    if(argc==2){
        const std::filesystem::path path=argv[1];
        auto part=document::PartDocument::load(path);
        kernel::OcctKernel engine;workspace::PartCalculationPolicy policy;policy.reject_errors=true;
        auto calculated=workspace::calculate_part_with_resolved_references(engine,part,nullptr,policy);
        check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"User copy regeneration failed");
        auto before=calculated.back();engine.ensure_body_properties(before);
        auto saved=path;saved.replace_filename(path.stem().string()+"-verified.prtz");part.save(saved,calculated);
        auto reopened=document::PartDocument::load(saved);
        auto again=workspace::calculate_part_with_resolved_references(engine,reopened,nullptr,policy);
        auto after=again.back();engine.ensure_body_properties(after);
        check(std::abs(before.volume-after.volume)<1e-7,"User copy cold reopen changed material");
        std::cout<<"User document copy: cold regeneration, save/reopen and material identity passed; volume "<<std::setprecision(15)<<after.volume<<'\n';return 0;
    }
    definitions();measurements();woodruff();native_cut();catalogue();shaft_frame_matrix();native_variants();keyway_confirmation();std::cout<<"MECH equations, catalogue and native cut passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
