#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/solid_straightening.hpp>
#include <zima/kernel/solid_state_history.hpp>
#include <zima/kernel/solid_state_ancestry.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/solid_state_reference_view.hpp>
#include <Geom_BezierCurve.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <GeomFill_CorrectedFrenet.hxx>
#include <NCollection_Array1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Trsf.hxx>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

using namespace zima::kernel;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,double tolerance,const char* message) {
    require(std::isfinite(actual)&&std::abs(actual-expected)<=tolerance,message);
}
RevolutionRequest l_section() {
    RevolutionRequest r;
    r.outer_profile=ExtrusionRequest::PolygonProfile{{{100,0,0},{120,0,0},{120,4,0},{104,4,0},{104,20,0},{100,20,0}}};
    r.profile_region_id="L-section";r.outer_boundary_id="L-outline";
    r.outer_edge_source_ids={"c0","c1","c2","c3","c4","c5"};
    r.outer_vertex_source_ids={"p0","p1","p2","p3","p4","p5"};
    r.axis_direction={0,1,0};r.angle_degrees=90;return r;
}
Sweep3DRequest offset_arc(double direction, bool hollow) {
    Sweep3DRequest r;r.transported=true;r.linear_tolerance=.001;
    constexpr int count=8;
    const double step=direction*std::numbers::pi/(2*count);
    for(int i=0;i<=count;++i) {
        const double a=i*step;
        r.path_points.push_back({10*std::cos(a),10*std::sin(a),0});
        r.path_point_ids.push_back("station-"+std::to_string(i));
        if(!i)continue;
        const double previous=(i-1)*step,k=4*std::tan(step/4)/3;
        Sweep3DRequest::PathSegment segment;
        segment.source_id="arc";segment.start=r.path_points[i-1];segment.end=r.path_points[i];
        segment.bezier_control_points={segment.start,
            {10*(std::cos(previous)-k*std::sin(previous)),10*(std::sin(previous)+k*std::cos(previous)),0},
            {10*(std::cos(a)+k*std::sin(a)),10*(std::sin(a)-k*std::cos(a)),0},segment.end};
        r.path_segments.push_back(segment);
    }
    Sweep3DRequest::Section section;section.profile_id="offset-section";
    section.point_id=r.path_point_ids.front();section.profile_normal={0,direction,0};
    section.circle_radial_direction=Vec3{1,0,0};
    section.profile.region_id="offset-region";section.profile.outer_boundary_id="outer";
    section.profile.outer_profile=ExtrusionRequest::CircleProfile{{12,0,0},.5};
    section.profile.outer_edge_source_ids={"outer-circle"};
    if(hollow) {
        section.profile.inner_profiles={ExtrusionRequest::CircleProfile{{12,0,0},.25}};
        section.profile.inner_boundary_ids={"void"};
        section.profile.inner_edge_source_ids={{"inner-circle"}};
        section.profile.inner_vertex_source_ids={{}};
    }
    r.sections.push_back(section);return r;
}
void reference_frames(OcctKernel& kernel) {
    const auto source=l_section();
    const auto original=kernel.evaluate_history({{"source",source}}).back().mesh.original_references;
    const std::string cap="end:from:"+std::to_string(source.profile_region_id.size())+":"+source.profile_region_id;
    for(bool flip:{false,true})for(double coefficient:{.9,1.1}) {
        zima::document::PartDocument doc;doc.document_id="frames";
        auto child=zima::document::PartDocument::create_extrusion_container("child-sketch");
        zima::document::ConstructionReference point;point.owner_id="source";point.semantic_key="end:p0";
        auto front=point;front.semantic_key=cap;front.orientation_role="front";
        front.orientation_drives_rotation=true;front.orientation_only=true;front.flip=flip;
        child.placement.references={point,front};
        auto grandchild=zima::document::PartDocument::create_extrusion_container("grandchild-sketch");
        point.owner_id=child.id+":origin";point.semantic_key="origin:point";
        front.owner_id=point.owner_id;front.semantic_key="origin:axis:y";front.flip=false;
        auto top=front;top.semantic_key="origin:axis:z";top.orientation_role="top";
        grandchild.placement.references={point,front,top};
        doc.history={child,grandchild};
        doc.history_order={{zima::document::PartHistoryKind::Feature,child.id},
            {zima::document::PartHistoryKind::Feature,grandchild.id}};
        doc.resolve_constructions(original);
        require(doc.history[0].placement.reference_valid&&doc.history[1].placement.reference_valid,"Curved end attachment failed");
        const auto curved=doc.history[0].placement;
        const auto plan=kernel.prepare_straightening(source,coefficient);
        const auto straight=kernel.evaluate_history({{"source",plan.primitive}}).back().mesh.original_references;
        zima::document::HistoryReferenceViews views{{child.id,
            zima::document::solid_state_reference_view(original,straight,{{"source",""}},child.placement.references)}};
        doc.resolve_constructions(original,views);
        auto expected=child.placement;
        require(zima::document::resolve_placement(expected,straight),"Straight end attachment failed");
        for(const auto& feature:doc.history) {
            require(feature.placement.reference_valid,"Descendant lost state reference");
            const auto& p=feature.placement;
            near(p.x,expected.x,1e-8,"End attachment X mismatch");near(p.y,expected.y,1e-8,"End attachment Y mismatch");
            near(p.z,expected.z,1e-8,"End attachment Z mismatch");
            near(p.rotation_x,expected.rotation_x,1e-8,"End attachment RX mismatch");
            near(p.rotation_y,expected.rotation_y,1e-8,"End attachment RY mismatch");
            near(p.rotation_z,expected.rotation_z,1e-8,"End attachment RZ mismatch");
        }
        near(doc.history[0].placement.z,-plan.centroid_path_length*coefficient,1e-8,"Attachment did not follow developed length");
        require(doc.history[0].placement.references==child.placement.references,"End attachment identity or side changed");
        doc.resolve_constructions(original);
        const auto& restored=doc.history[0].placement;
        near(restored.x,curved.x,1e-8,"Restored X frame drift");
        near(restored.y,curved.y,1e-8,"Restored Y frame drift");
        near(restored.z,curved.z,1e-8,"Restored Z frame drift");
        near(restored.rotation_x,curved.rotation_x,1e-8,"Restored RX frame drift");
        near(restored.rotation_y,curved.rotation_y,1e-8,"Restored RY frame drift");
        near(restored.rotation_z,curved.rotation_z,1e-8,"Restored RZ frame drift");
        require(restored.references==curved.references,"Restoring frame changed authored references");
        for(int cycle=0;cycle<20;++cycle) {
            doc.resolve_constructions(original,views);doc.resolve_constructions(original);
            const auto& p=doc.history[0].placement;
            near(p.x,curved.x,1e-8,"Repeated state X drift");near(p.y,curved.y,1e-8,"Repeated state Y drift");
            near(p.z,curved.z,1e-8,"Repeated state Z drift");
            near(p.rotation_x,curved.rotation_x,1e-8,"Repeated state RX drift");
            near(p.rotation_y,curved.rotation_y,1e-8,"Repeated state RY drift");
            near(p.rotation_z,curved.rotation_z,1e-8,"Repeated state RZ drift");
            require(p.references==curved.references,"Repeated state changed reference identity or side");
        }
    }
}
void spline_sweeps(OcctKernel& kernel) {
    // x=20*t*t, z=100*t: exact polynomial, independently integrable length.
    const double guide_length=.5*std::sqrt(11600.)+125*std::asinh(.4);
    for(double offset:{0.,2.,-2.})for(bool hollow:{false,true}) {
        Sweep3DRequest source;source.linear_tolerance=1e-4;
        source.path_points={{0,0,0},{20,0,100}};source.path_point_ids={"start","end"};
        Sweep3DRequest::PathSegment segment;segment.source_id="parabola";
        segment.start=source.path_points[0];segment.end=source.path_points[1];
        segment.bezier_control_points={{0,0,0},{0,0,100./3},{20./3,0,200./3},{20,0,100}};
        source.path_segments={segment};
        Sweep3DRequest::Section section;section.point_id="start";section.profile_id="profile";
        section.profile.region_id="section";section.profile.outer_boundary_id="outer";
        section.profile.outer_profile=ExtrusionRequest::CircleProfile{{offset,0,0},.5};
        section.profile.outer_edge_source_ids={"outer-circle"};
        if(hollow) {
            section.profile.inner_profiles={ExtrusionRequest::CircleProfile{{offset,0,0},.25}};
            section.profile.inner_boundary_ids={"inner"};section.profile.inner_edge_source_ids={{"inner-circle"}};
            section.profile.inner_vertex_source_ids={{}};
        }
        source.sections={section};
        const auto fingerprint=history_fingerprint({{"spline",source}},1);
        const double expected=guide_length-offset*std::atan(.4);
        const auto plan=kernel.prepare_straightening(source,.9);
        near(plan.centroid_path_length,expected,1e-5,"Spline development differs from analytic offset parabola");
        near(plan.section_area,std::numbers::pi*(hollow?.1875:.25),1e-9,"Spline section lost its void or radius");
        const auto& straight=std::get<Sweep3DRequest>(plan.primitive);
        require(straight.path_point_ids==source.path_point_ids&&straight.path_segments[0].source_id=="parabola"&&
            straight.sections[0].profile.outer_edge_source_ids==section.profile.outer_edge_source_ids,"Spline source ancestry changed");
        const auto states=kernel.evaluate_history({{"spline",source},
            {"straight",SolidStateRequest{false,true,.9,{}}},{"restore",SolidStateRequest{true,true,1,{}}}});
        near(states.front().volume,plan.section_area*expected,.001,"Source spline transport differs from developed centroid trajectory");
        near(states[1].volume,plan.section_area*expected*.9,1e-5,"Straight spline volume differs from area times length");
        near(states.back().volume,states.front().volume,1e-7,"Spline Restore changed source volume");
        require(history_fingerprint({{"spline",source}},1)==fingerprint,"Spline preparation mutated authored source");
    }
}
void ordinary_sweeps(OcctKernel& kernel) {
    const auto l=l_section();auto polygon=std::get<ExtrusionRequest::PolygonProfile>(l.outer_profile);
    for(auto& p:polygon.vertices)p.x-=100;
    Sweep3DRequest sweep;sweep.path_points={{0,0,0},{0,0,100},{100,0,200}};
    sweep.path_point_ids={"start","bend-start","end"};
    Sweep3DRequest::PathSegment leg;leg.source_id="leg";leg.start=sweep.path_points[0];leg.end=sweep.path_points[1];
    Sweep3DRequest::PathSegment arc;arc.source_id="arc";arc.start=leg.end;arc.end=sweep.path_points[2];
    arc.arc_midpoint=Vec3{100-100/std::sqrt(2.),0,100+100/std::sqrt(2.)};sweep.path_segments={leg,arc};
    Sweep3DRequest::Section section;section.profile_id="section";section.point_id="start";
    section.profile.region_id=l.profile_region_id;section.profile.outer_boundary_id=l.outer_boundary_id;
    section.profile.outer_profile=polygon;section.profile.outer_edge_source_ids=l.outer_edge_source_ids;
    section.profile.outer_vertex_source_ids=l.outer_vertex_source_ids;sweep.sections={section};
    const double expected=100+(100-(80*10.+64*2.)/144)*std::numbers::pi/2;
    const auto fingerprint=history_fingerprint({{"source",sweep}},1);
    for(double coefficient:{.9,1.,1.1}) {
        const auto plan=kernel.prepare_straightening(sweep,coefficient);
        near(plan.section_area,144,1e-8,"Ordinary Sweep area changed");
        near(plan.centroid_path_length,expected,1e-8,"Ordinary Sweep used guide length instead of centroid path");
        const auto& straight=std::get<Sweep3DRequest>(plan.primitive);
        require(straight.path_point_ids==sweep.path_point_ids&&straight.path_segments[1].source_id=="arc"&&
            straight.sections[0].profile.outer_edge_source_ids==section.profile.outer_edge_source_ids,"Ordinary Sweep ancestry changed");
        const auto base=kernel.evaluate_history({{"source",straight}}).back();
        near(base.volume,144*expected*coefficient,.001,"Ordinary straight Sweep volume changed");
        ExtrusionRequest hole;hole.outer_profile=ExtrusionRequest::CircleProfile{{10,-5,50},1};hole.direction={0,14,0};
        hole.profile_region_id="hole";hole.outer_boundary_id="hole-rim";hole.outer_edge_source_ids={"hole-circle"};
        const auto cut=kernel.evaluate_history({{"source",straight},{"drill",hole,BooleanOperation::Subtract}}).back();
        near(base.volume-cut.volume,4*std::numbers::pi,.0001,"Straight portion lost its complete hole");
        FilletRequest fillet{{{"source","sweep:leg:profile:section:from:p0"}},1};
        require(kernel.evaluate_history({{"source",straight},{"round",fillet}}).back().volume<base.volume,"Straight Sweep fillet could not be reapplied");
    }
    require(history_fingerprint({{"source",sweep}},1)==fingerprint,"Ordinary preparation changed authored source");
    ExtrusionRequest retained_hole;retained_hole.outer_profile=ExtrusionRequest::CircleProfile{{10,-5,50},1};
    retained_hole.direction={0,14,0};retained_hole.profile_region_id="bore";
    retained_hole.outer_boundary_id="bore-rim";retained_hole.outer_edge_source_ids={"bore-circle"};
    const std::vector<HistoryOperation> hole_states{{"source",sweep},
        {"bore",retained_hole,BooleanOperation::Subtract},
        {"straight",SolidStateRequest{false,true,.9,{}}},
        {"restore",SolidStateRequest{true,true,1,{}}}};
    const auto hole_results=kernel.evaluate_history(hole_states);
    near(hole_results[2].volume,144*expected*.9-4*std::numbers::pi,.001,
        "State command changed a straight-portion bore");
    near(hole_results.back().volume,hole_results[1].volume,.001,"Restore lost the retained bore");
    auto new_hole=retained_hole;std::get<ExtrusionRequest::CircleProfile>(new_hole.outer_profile).center.z=45;
    const auto added_hole=kernel.evaluate_history({{"source",sweep},
        {"straight",SolidStateRequest{false,true,.9,{}}},{"new-bore",new_hole,BooleanOperation::Subtract},
        {"restore",SolidStateRequest{true,true,1,{}}}});
    near(added_hole.back().volume,hole_results[1].volume,.001,
        "A bore added after straightening was lost during Restore");
    auto crossing_hole=retained_hole;
    std::get<ExtrusionRequest::CircleProfile>(crossing_hole.outer_profile).center.z=99.5;
    bool crossing_rejected=false;
    try{static_cast<void>(kernel.evaluate_history({{"source",sweep},{"crossing",crossing_hole,BooleanOperation::Subtract},
        {"straight",SolidStateRequest{false,true,1,{}}}}));}catch(const std::invalid_argument&){crossing_rejected=true;}
    require(crossing_rejected,"A hole crossing the straight/curved boundary was accepted by center alone");
    auto tail_sweep=sweep;
    Sweep3DRequest::PathSegment tail;tail.source_id="tail";tail.start=tail_sweep.path_points.back();tail.end={200,0,200};
    tail_sweep.path_segments.push_back(tail);tail_sweep.path_points.push_back(tail.end);tail_sweep.path_point_ids.push_back("tail-end");
    auto tail_hole=retained_hole;
    std::get<ExtrusionRequest::CircleProfile>(tail_hole.outer_profile).center={150,-5,190};
    const auto tail_states=kernel.evaluate_history({{"source",tail_sweep},
        {"tail-bore",tail_hole,BooleanOperation::Subtract},
        {"straight",SolidStateRequest{false,true,.9,{}}},
        {"restore",SolidStateRequest{true,true,1,{}}}});
    near(tail_states[2].volume,144*(expected+100)*.9-4*std::numbers::pi,.001,
        "Bore in a rotated straight portion lost volume");
    near(tail_states.back().volume,tail_states[1].volume,.001,"Rotated portion bore did not restore");
    bool tail_axis=false;
    for(const auto& face:tail_states.back().mesh.original_references.triangle_references)
        if(face.owner_id=="straight" && face.surface && face.surface->kind==SurfaceGeometry::Kind::Cylinder) {
            const auto parent=solid_state_parent(face.semantic_key);
            if(parent && parent->first=="tail-bore") {
                tail_axis=true;near(face.surface->origin.x,10,1e-7,"Bore did not rotate with the straight portion");
                near(face.surface->origin.z,(expected+50)*.9,1e-7,"Bore did not follow its developed station");
                near(face.surface->radius,1,1e-9,"Length coefficient scaled the bore radius");
            }
        }
    require(tail_axis,"Transferred bore lost its exact cylindrical reference");
    Sweep3DRequest neighbor;
    neighbor.path_points={{10,-5,(expected+50)*.9},{10,9,(expected+50)*.9}};
    neighbor.path_point_ids={"neighbor-start","neighbor-end"};
    Sweep3DRequest::PathSegment neighbor_segment;neighbor_segment.source_id="neighbor-leg";
    neighbor_segment.start=neighbor.path_points[0];neighbor_segment.end=neighbor.path_points[1];neighbor.path_segments={neighbor_segment};
    Sweep3DRequest::Section neighbor_section;neighbor_section.profile_id="neighbor-profile";
    neighbor_section.point_id="neighbor-start";neighbor_section.profile_normal={0,1,0};
    neighbor_section.profile.region_id="neighbor-region";neighbor_section.profile.outer_boundary_id="neighbor-rim";
    neighbor_section.profile.outer_profile=ExtrusionRequest::CircleProfile{neighbor.path_points[0],2};
    neighbor_section.profile.outer_edge_source_ids={"neighbor-circle"};neighbor.sections={neighbor_section};
    bool extra_cut_rejected=false;
    try{static_cast<void>(kernel.evaluate_history({{"source",tail_sweep},{"neighbor",neighbor},
        {"tail-bore",tail_hole,BooleanOperation::Subtract},
        {"straight",SolidStateRequest{false,false,.9,{"source"}}}}));}
    catch(const std::invalid_argument&){extra_cut_rejected=true;}
    require(extra_cut_rejected,"Transferred bore removed material from a previously untouched source");
    auto separated=sweep;separated.separate_segments=true;
    separated.path_points={sweep.path_points[0],sweep.path_points[1],sweep.path_points[1],sweep.path_points[2]};
    separated.path_point_ids={"start","bend:in","bend:out","end"};
    const auto separated_plan=kernel.prepare_straightening(separated);
    const auto& separated_flat=std::get<Sweep3DRequest>(separated_plan.primitive);
    require(separated_flat.separate_segments&&separated_flat.path_point_ids==separated.path_point_ids,"Separate segment identities were merged");
    near(separated_plan.centroid_path_length,expected,1e-8,"Separate station development changed length");
    near(kernel.evaluate_history({{"separated",separated_flat}}).back().volume,144*expected,.001,"Separate station volume changed");
    // Exercise the actual document adapter, which emits paired stations.
    for(bool arc_path:{false,true}) {
        auto feature=zima::document::PartDocument::create_sweep2d_container();
        auto path=zima::sketcher::Sketch::from_serialized(feature.sweep2d.path_sketch);
        if(arc_path)static_cast<void>(path.add_arc(10,0,0,0,10,10,false,1e-6,true));
        else static_cast<void>(path.add_segment(0,0,0,20));
        feature.sweep2d.path_sketch=path.serialized();zima::document::PartDocument::reframe_sweep2d_sketches(feature);
        const auto station=zima::document::PartDocument::sweep2d_route(feature).stations.front();
        const auto index=zima::document::PartDocument::ensure_sweep2d_profile(feature,station.point_id,station.incoming);
        auto profile=zima::sketcher::Sketch::from_serialized(feature.sweep2d.profiles[index].sketch_serialized);
        static_cast<void>(profile.add_circle(0,0,2));feature.sweep2d.profiles[index].sketch_serialized=profile.serialized();
        const auto request=zima::document::PartDocument::sweep2d_request(feature);
        const auto plan=kernel.prepare_straightening(request);
        near(plan.centroid_path_length,arc_path?5*std::numbers::pi:20,1e-7,"Native 2D Sweep development length changed");
        near(kernel.evaluate_history({{"native-2d",plan.primitive}}).back().volume,4*std::numbers::pi*plan.centroid_path_length,.001,
            "Native 2D Sweep section volume changed");
    }
    // Rigidly rotate and translate the complete definition away from XY/XZ.
    auto spatial=sweep;
    const auto move=[](Vec3 p){return Vec3{p.z+13,p.x-5,p.y+7};};
    for(auto& p:spatial.path_points)p=move(p);
    for(auto& segment:spatial.path_segments){segment.start=move(segment.start);segment.end=move(segment.end);if(segment.arc_midpoint)segment.arc_midpoint=move(*segment.arc_midpoint);}
    auto& spatial_section=spatial.sections.front();spatial_section.profile_normal={1,0,0};
    for(auto& p:std::get<ExtrusionRequest::PolygonProfile>(spatial_section.profile.outer_profile).vertices)p=move(p);
    const auto spatial_plan=kernel.prepare_straightening(spatial);
    near(spatial_plan.centroid_path_length,expected,1e-8,"Spatial frame changed developed length");
    const auto& spatial_flat=std::get<Sweep3DRequest>(spatial_plan.primitive);
    near(spatial_flat.path_points.back().x,13+expected,1e-8,"Spatial development used a world axis");
    near(spatial_flat.path_points.back().y,-5,1e-8,"Spatial development lost translation");
    near(kernel.evaluate_history({{"spatial",spatial_flat}}).back().volume,144*expected,.001,"Spatial straight volume changed");
    auto hollow=sweep;auto& ring=hollow.sections.front().profile;
    ring.outer_profile=ExtrusionRequest::PolygonProfile{{{0,0,0},{20,0,0},{20,20,0},{0,20,0}}};
    ring.outer_edge_source_ids={"o0","o1","o2","o3"};ring.outer_vertex_source_ids={"v0","v1","v2","v3"};
    ring.inner_profiles={ExtrusionRequest::PolygonProfile{{{2,2,0},{18,2,0},{18,18,0},{2,18,0}}}};
    ring.inner_boundary_ids={"void"};ring.inner_edge_source_ids={{"i0","i1","i2","i3"}};
    ring.inner_vertex_source_ids={{"q0","q1","q2","q3"}};
    const auto hollow_plan=kernel.prepare_straightening(hollow);
    near(hollow_plan.section_area,144,1e-8,"Ordinary hollow section lost its void");
    near(hollow_plan.centroid_path_length,100+90*std::numbers::pi/2,1e-8,"Ordinary hollow centroid length changed");
    near(kernel.evaluate_history({{"hollow",hollow_plan.primitive}}).back().volume,144*hollow_plan.centroid_path_length,.001,"Ordinary hollow volume changed");
    auto thin=sweep;thin.thin=true;thin.thin_first=-.2;thin.thin_second=.2;
    thin.sections.front().profile.outer_profile=ExtrusionRequest::CircleProfile{{0,0,0},2};
    thin.sections.front().profile.outer_edge_source_ids={"circle"};thin.sections.front().profile.outer_vertex_source_ids.clear();
    const auto thin_plan=kernel.prepare_straightening(thin);
    near(thin_plan.section_area,1.6*std::numbers::pi,1e-8,"Ordinary Thin section changed thickness");
    near(thin_plan.centroid_path_length,100+100*std::numbers::pi/2,1e-8,"Ordinary Thin centroid path changed");
    near(kernel.evaluate_history({{"thin",thin_plan.primitive}}).back().volume,thin_plan.section_area*thin_plan.centroid_path_length,.001,"Ordinary Thin volume changed");
    // Equal area is insufficient: a 2x8 rectangle and a 4x4 square are different.
    auto variable=sweep;variable.path_points={{0,0,0},{0,0,10}};variable.path_point_ids={"start","end"};
    variable.path_segments={leg};variable.path_segments[0].end={0,0,10};
    auto a=section;a.profile.outer_profile=ExtrusionRequest::PolygonProfile{{{0,0,0},{2,0,0},{2,8,0},{0,8,0}}};
    a.profile.outer_edge_source_ids={"a","b","c","d"};a.profile.outer_vertex_source_ids={"A","B","C","D"};
    auto b=a;b.point_id="end";b.point_index=1;b.profile_id="other";
    b.profile.outer_profile=ExtrusionRequest::PolygonProfile{{{0,0,10},{4,0,10},{4,4,10},{0,4,10}}};
    variable.sections={a,b};
    bool rejected=false;try{static_cast<void>(kernel.prepare_straightening(variable));}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Equal-area variable section was silently replaced");
    b.profile.outer_profile=ExtrusionRequest::PolygonProfile{{{0,0,10},{2,0,10},{2,8,10},{0,8,10}}};
    variable.sections={a,b};
    near(kernel.evaluate_history({{"source",kernel.prepare_straightening(variable,1.1).primitive}}).back().volume,176,.001,
        "Explicit congruent stations were not preserved");
}
void spatial_and_joined_splines(OcctKernel& kernel) {
    Sweep3DRequest source;source.linear_tolerance=1e-4;
    source.path_points={{0,0,0},{5,0,50},{20,0,100}};source.path_point_ids={"a","b","c"};
    Sweep3DRequest::PathSegment a;a.source_id="first";a.start=source.path_points[0];a.end=source.path_points[1];
    a.bezier_control_points={{0,0,0},{0,0,50./3},{5./3,0,100./3},{5,0,50}};
    auto b=a;b.source_id="second";b.start=a.end;b.end=source.path_points[2];
    b.bezier_control_points={{5,0,50},{25./3,0,200./3},{40./3,0,250./3},{20,0,100}};
    source.path_segments={a,b};
    Sweep3DRequest::Section section;section.point_id="a";section.profile_id="rectangle";
    section.profile.region_id="region";section.profile.outer_boundary_id="rim";
    section.profile.outer_profile=ExtrusionRequest::PolygonProfile{{{1.5,-1,0},{2.5,-1,0},{2.5,1,0},{1.5,1,0}}};
    section.profile.outer_edge_source_ids={"e0","e1","e2","e3"};
    section.profile.outer_vertex_source_ids={"v0","v1","v2","v3"};source.sections={section};
    const double expected=.5*std::sqrt(11600.)+125*std::asinh(.4)-2*std::atan(.4);
    for(bool spatial:{false,true})for(bool separate:{false,true}) {
        auto request=source;
        if(separate) {
            request.separate_segments=true;request.path_points.insert(request.path_points.begin()+2,source.path_points[1]);
            request.path_point_ids={"a","b:in","b:out","c"};
        }
        if(spatial) {
            const auto move=[](Vec3 p){return Vec3{p.z+11,p.x-7,p.y+3};};
            for(auto& p:request.path_points)p=move(p);
            for(auto& segment:request.path_segments) {
                segment.start=move(segment.start);segment.end=move(segment.end);
                for(auto& p:segment.bezier_control_points)p=move(p);
            }
            for(auto& p:std::get<ExtrusionRequest::PolygonProfile>(request.sections[0].profile.outer_profile).vertices)p=move(p);
            request.sections[0].profile_normal={1,0,0};
        }
        const auto plan=kernel.prepare_straightening(request,1.1);
        near(plan.centroid_path_length,expected,1e-5,"Joined spline or transformed rectangle changed development length");
        near(plan.section_area,2,1e-9,"Spline rectangle section changed");
        const auto& flat=std::get<Sweep3DRequest>(plan.primitive);
        require(flat.path_point_ids==request.path_point_ids&&flat.separate_segments==separate&&
            flat.path_segments[1].source_id=="second","Joined spline lost independent station identities");
        const auto results=kernel.evaluate_history({{"source",request},{"straight",SolidStateRequest{false,true,1.1,{}}},
            {"restore",SolidStateRequest{true,true,1,{}}}});
        near(results.front().volume,2*expected,.002,"Joined spline source section transport changed volume");
        near(results[1].volume,2*expected*1.1,1e-5,"Joined spline straight volume changed");
        near(results.back().volume,results.front().volume,1e-7,"Joined spline Restore changed volume");
    }
    // A genuine spatial curve with nonzero torsion, not a rotated planar curve.
    // x=20*t^2, y=10*t^3, z=100*t; independent Simpson integration of |r'(t)|.
    source.path_points={{0,0,0},{20,10,100}};source.path_point_ids={"start","end"};
    a.start=source.path_points[0];a.end=source.path_points[1];a.source_id="spatial-cubic";
    a.bezier_control_points={{0,0,0},{0,0,100./3},{20./3,0,200./3},{20,10,100}};source.path_segments={a};
    section.point_id="start";section.profile.outer_profile=ExtrusionRequest::CircleProfile{{0,0,0},.5};
    section.profile.outer_edge_source_ids={"circle"};section.profile.outer_vertex_source_ids.clear();source.sections={section};
    constexpr int intervals=8192;
    const auto speed=[](double t){return std::sqrt(10000+1600*t*t+900*t*t*t*t);};
    double integral=speed(0)+speed(1);
    for(int i=1;i<intervals;++i)integral+=(i%2?4:2)*speed(double(i)/intervals);
    integral/=3*intervals;
    const auto plan=kernel.prepare_straightening(source);
    near(plan.centroid_path_length,integral,1e-5,"Spatial spline length differs from independent derivative integral");
    const auto results=kernel.evaluate_history({{"source",source},{"straight",SolidStateRequest{false,true,1,{}}},
        {"restore",SolidStateRequest{true,true,1,{}}}});
    near(results[1].volume,.25*std::numbers::pi*integral,1e-5,"Spatial spline straight volume changed");
    near(results.back().volume,results.front().volume,1e-7,"Spatial spline Restore changed source");
    // Explicit end section follows the actual pipe frame. A centered rectangle
    // exposes torsion which a circular profile cannot detect. The independent
    // derivative integral above remains the expected developed material length.
    NCollection_Array1<gp_Pnt> poles(1,4);
    for(int i=0;i<4;++i) {
        const auto p=a.bezier_control_points[i];poles.SetValue(i+1,gp_Pnt(p.x,p.y,p.z));
    }
    Handle(Geom_BezierCurve) curve=new Geom_BezierCurve(poles);
    GeomFill_CorrectedFrenet law;law.SetCurve(new GeomAdaptor_Curve(curve));
    const auto frame=[&](double t) {
        gp_Vec tangent,normal,binormal;
        require(law.D0(t,tangent,normal,binormal),"Spatial test frame failed");
        return gp_Ax3(curve->Value(t),gp_Dir(tangent),gp_Dir(normal));
    };
    gp_Trsf transport;transport.SetDisplacement(frame(0),frame(1));
    section.profile.outer_profile=ExtrusionRequest::PolygonProfile{{{-1,-.5,0},{1,-.5,0},{1,.5,0},{-1,.5,0}}};
    section.profile.outer_edge_source_ids={"e0","e1","e2","e3"};
    section.profile.outer_vertex_source_ids={"v0","v1","v2","v3"};
    auto end=section;end.point_index=1;end.point_id="end";end.profile_id="end-rectangle";
    for(auto& p:std::get<ExtrusionRequest::PolygonProfile>(end.profile.outer_profile).vertices) {
        const auto q=gp_Pnt(p.x,p.y,p.z).Transformed(transport);p={q.X(),q.Y(),q.Z()};
    }
    const auto normal=gp_Vec(0,0,1).Transformed(transport);
    end.profile_normal={normal.X(),normal.Y(),normal.Z()};source.sections={section,end};
    const auto authored=history_fingerprint({{"source",source}},1);
    const auto rectangle_plan=kernel.prepare_straightening(source,.9);
    near(rectangle_plan.centroid_path_length,integral,1e-5,"Spatial rectangle development changed centroid length");
    near(rectangle_plan.section_area,2,1e-9,"Spatial rectangle changed section area");
    const auto rectangle_states=kernel.evaluate_history({{"source",source},
        {"straight",SolidStateRequest{false,true,.9,{}}},{"restore",SolidStateRequest{true,true,1,{}}}});
    near(rectangle_states.front().volume,2*integral,.002,"Spatial rectangle source is not constant-section");
    near(rectangle_states[1].volume,2*integral*.9,1e-5,"Spatial rectangle straight volume changed");
    near(rectangle_states.back().volume,rectangle_states.front().volume,1e-7,"Spatial rectangle Restore changed source");
    require(history_fingerprint({{"source",source}},1)==authored,"Spatial rectangle mutated authored frames");
    auto variable=source;
    auto& changed_vertices=std::get<ExtrusionRequest::PolygonProfile>(variable.sections.back().profile.outer_profile).vertices;
    const auto& start_vertices=std::get<ExtrusionRequest::PolygonProfile>(section.profile.outer_profile).vertices;
    for(std::size_t i=0;i<changed_vertices.size();++i) {
        const auto p=start_vertices[i];
        const auto q=gp_Pnt(p.x*1.2,p.y,p.z).Transformed(transport);
        changed_vertices[i]={q.X(),q.Y(),q.Z()};
    }
    bool rejected=false;
    try{(void)kernel.prepare_straightening(variable);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Spatial spline accepted a changing section");
}
}
int main() {try {
    OcctKernel kernel;
    const auto original=l_section();
    const auto unchanged=history_fingerprint({{"source",original}},1);
    // Independent two-rectangle area/centroid, not the kernel's own measurement.
    const double area=80+64,radius=(80*110.+64*102.)/area;
    const double arc_length=radius*std::numbers::pi/2;
    for(double coefficient:{.9,1.,1.1})for(double start:{0.,37.})for(double sign:{-1.,1.}) {
        auto source=original;source.start_angle_degrees=start;source.axis_direction.y=sign;
        const auto plan=kernel.prepare_straightening(source,coefficient);
        near(plan.section_area,area,1e-9,"L section area changed");
        near(plan.centroid_path_length,arc_length,1e-9,"Wrong centroid trajectory length");
        const auto& straight=std::get<ExtrusionRequest>(plan.primitive);
        require(straight.outer_edge_source_ids==source.outer_edge_source_ids&&
            straight.outer_vertex_source_ids==source.outer_vertex_source_ids&&
            straight.profile_region_id==source.profile_region_id,"Source ancestry was replaced");
        const auto result=kernel.evaluate_history({{"source",straight}}).back();
        near(result.volume,area*arc_length*coefficient,1e-5,"Straight L volume is not area times length");
        const FilletRequest fillet{{{"source","generated:p0"}},1};
        const auto treated=kernel.evaluate_history({{"source",straight},{"fillet",fillet}}).back();
        const auto restored=kernel.evaluate_history({{"source",source},{"fillet",fillet}}).back();
        require(treated.volume<result.volume&&restored.volume>0,"Authored fillet cannot be reapplied");
    }
    require(history_fingerprint({{"source",original}},1)==unchanged,"Planning mutated the source");
    auto hollow=original;
    hollow.outer_profile=ExtrusionRequest::PolygonProfile{{{100,0,0},{120,0,0},{120,20,0},{100,20,0}}};
    hollow.outer_edge_source_ids={"o0","o1","o2","o3"};hollow.outer_vertex_source_ids={"v0","v1","v2","v3"};
    hollow.inner_profiles={ExtrusionRequest::PolygonProfile{{{102,2,0},{118,2,0},{118,18,0},{102,18,0}}}};
    hollow.inner_boundary_ids={"void"};hollow.inner_edge_source_ids={{"i0","i1","i2","i3"}};
    hollow.inner_vertex_source_ids={{"q0","q1","q2","q3"}};
    const auto hollow_plan=kernel.prepare_straightening(hollow);
    near(hollow_plan.section_area,144,1e-9,"Hollow section lost its void");
    near(kernel.evaluate_history({{"hollow",hollow_plan.primitive}}).back().volume,
        144*110*std::numbers::pi/2,1e-5,"Hollow straight volume changed");
    auto wire=original;wire.outer_profile=ExtrusionRequest::CircleProfile{{100,0,0},4};
    wire.outer_edge_source_ids={"circle"};wire.outer_vertex_source_ids.clear();
    const auto wire_plan=kernel.prepare_straightening(wire);
    near(wire_plan.section_area,16*std::numbers::pi,1e-9,"Exact circular profile area changed");
    near(kernel.evaluate_history({{"wire",wire_plan.primitive}}).back().volume,
        16*std::numbers::pi*100*std::numbers::pi/2,1e-5,"Circular wire volume changed");
    for(double invalid:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected=false;try{static_cast<void>(kernel.prepare_straightening(original,invalid));}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"Invalid coefficient accepted");
    }
    auto surface=original;surface.surface_result=true;
    bool rejected=false;try{static_cast<void>(kernel.prepare_straightening(surface));}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Surface was silently treated as a solid");
    auto helix=zima::document::PartDocument::create_helical_sweep_container();
    helix.helical.pitch=5;helix.sweep_precision.custom_tolerance=.001;
    auto base=zima::sketcher::Sketch::from_serialized(helix.helical.sketches[0]);
    base.plane=zima::sketcher::SketchPlane::XY;base.refresh_default_frame();
    helix.helical.circle_id=base.add_circle(0,0,10);helix.helical.start_point_id=base.add_point(10,0);
    helix.helical.sketches[0]=base.serialized();
    auto guide=zima::sketcher::Sketch::from_serialized(helix.helical.sketches[1]);
    static_cast<void>(guide.add_segment(0,0,0,12.5));helix.helical.sketches[1]=guide.serialized();
    auto profile=zima::sketcher::Sketch::from_serialized(helix.helical.sketches[2]);
    static_cast<void>(profile.add_circle(0,0,.5));helix.helical.sketches[2]=profile.serialized();
    const auto helical_request=zima::document::PartDocument::helical_sweep_request(helix);
    const auto helical_plan=kernel.prepare_straightening(helical_request);
    near(helical_plan.centroid_path_length,2.5*std::hypot(20*std::numbers::pi,5.),.001,
        "Helical centroid length differs from the independent analytic helix");
    near(helical_plan.section_area,.25*std::numbers::pi,1e-9,"Helical section changed");
    const auto& straight_helix=std::get<Sweep3DRequest>(helical_plan.primitive);
    require(straight_helix.path_point_ids==helical_request.path_point_ids&&
        straight_helix.sections.front().profile.outer_edge_source_ids==helical_request.sections.front().profile.outer_edge_source_ids,
        "Helical source identities changed");
    near(kernel.evaluate_history({{"helix",straight_helix}}).back().volume,
        helical_plan.section_area*helical_plan.centroid_path_length,.001,"Straight helical wire volume changed");
    const std::vector<HistoryOperation> helical_states{{"helix",helical_request},
        {"straight",SolidStateRequest{false,true,.9,{}}},
        {"restore",SolidStateRequest{true,true,1,{}}}};
    const auto helical_results=kernel.evaluate_history(helical_states);
    near(helical_results[1].volume,helical_plan.section_area*helical_plan.centroid_path_length*.9,.001,
        "Helical state command differs from preparation");
    near(helical_results.back().volume,helical_results.front().volume,.001,
        "Helical state restore changed the source volume");
    for(double direction:{-1.,1.})for(bool hollow:{false,true}) {
        const auto request=offset_arc(direction,hollow);
        const auto fingerprint=history_fingerprint({{"offset",request}},1);
        const auto plan=kernel.prepare_straightening(request,.9);
        // The profile centroid follows radius 12, while its guide has radius 10.
        // Eight cubic arc pieces approximate the quarter circle well below 1 um.
        near(plan.centroid_path_length,6*std::numbers::pi,.001,"Offset section incorrectly followed guide length");
        const double expected_area=std::numbers::pi*(hollow?.1875:.25);
        near(plan.section_area,expected_area,1e-9,"Transported hollow section area changed");
        near(kernel.evaluate_history({{"offset",plan.primitive}}).back().volume,
            expected_area*plan.centroid_path_length*.9,.001,"Transported offset section volume changed");
        require(history_fingerprint({{"offset",request}},1)==fingerprint,"Transport preparation mutated source");
    }
    auto thin=offset_arc(1,false);thin.thin=true;thin.thin_first=-.1;thin.thin_second=.1;
    const auto thin_plan=kernel.prepare_straightening(thin);
    near(thin_plan.section_area,std::numbers::pi*(.6*.6-.4*.4),1e-9,"Thin section wall thickness changed");
    near(thin_plan.centroid_path_length,6*std::numbers::pi,.001,"Thin section centroid length changed");
    near(kernel.evaluate_history({{"thin",thin_plan.primitive}}).back().volume,
        thin_plan.section_area*thin_plan.centroid_path_length,.001,"Straight thin section volume changed");
    reference_frames(kernel);
    ordinary_sweeps(kernel);
    spline_sweeps(kernel);
    spatial_and_joined_splines(kernel);
    // Drive real source/treatment replay from the state timeline. This exercises
    // calculation preparation, not native persistence or state-owned topology.
    const std::vector<HistoryOperation> authored{{"source",l_section()},
        {"fillet",FilletRequest{{{"source","generated:p0"}},1}}};
    const auto authored_fingerprint=history_fingerprint(authored,authored.size());
    const auto formed=kernel.evaluate_history(authored).back().volume;
    const std::vector<SolidStateChange> states{
        {1,"shorten","",false,true,false,.9,{}},
        {2,"lengthen","",false,true,false,1.1,{}},
        {2,"restore","",true,true,false,1,{}}};
    for(std::size_t count=1;count<=states.size();++count) {
        auto replay=authored;
        const auto targets=solid_states_before(authored,std::span(states).first(count),authored.size());
        for(const auto& [owner,state]:targets)if(state.straight)
            replay[state.source_index].primitive=kernel.prepare_straightening(
                std::get<RevolutionRequest>(authored[state.source_index].primitive),state.coefficient).primitive;
        const auto result=kernel.evaluate_history(replay).back();
        if(count==3)near(result.volume,formed,1e-7,"Timeline restore discarded the later Fillet");
        else {
            const auto bare=kernel.evaluate_history({replay.front()}).back().volume;
            require(result.volume<bare,"Timeline replay omitted the later Fillet");
            const auto expected=kernel.prepare_straightening(l_section(),count==1?.9:1.1);
            near(bare,expected.section_area*expected.centroid_path_length*(count==1?.9:1.1),1e-5,
                "Timeline replay accumulated length coefficients");
        }
    }
    require(history_fingerprint(authored,authored.size())==authored_fingerprint,
        "State timeline changed the authored geometry or treatment");
    std::cout<<"Solid straightening preparation: exact sections, centroid length, direction, ancestry and fillet replay passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
