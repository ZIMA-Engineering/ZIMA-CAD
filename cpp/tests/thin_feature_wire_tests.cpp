#include "../app/authored_feature_wire.hpp"
#include "../app/feature_view_cue_policy.hpp"
#include "thin_wire_verification.hpp"
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/edge_treatment_operations.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <QCoreApplication>
#include <cstdint>
#include <iostream>
#include <set>
#include <cmath>

using namespace zima;
namespace {
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void sketch_wire_matrix() {
    unsigned cases{};
    for(int kind=0;kind<3;++kind)for(bool closed:{false,true})for(bool interpolating:{false,true})
    for(bool placed:{false,true})for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ}) {
        auto doc=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        sketch.plane=plane;sketch.plane_auto=false;sketch.refresh_default_frame();
        static_cast<void>(sketch.add_bspline({{-15,-12},{-5,-4},{1,7},{9,22},{25,25}},3,closed,false,1e-6,interpolating));
        auto feature=kind==0?document::PartDocument::create_sketch_container():document::PartDocument::create_feature_container(sketch.id);
        if(kind==1)feature.feature.type=document::FeatureType::Sketch;
        if(kind==2)for(auto& side:feature.feature.sides)side.operation=document::FeatureSideOperation::None;
        sketch.owner_container_id=feature.id;doc.history={feature};doc.sketches={sketch};
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Native Sketch wire");graph.insert({document::PartHistoryKind::Feature,feature.id});
        if(placed){auto positioned=*graph.find(body);positioned.scope.placement={11,7,-3,20,30,40};graph.update_body(std::move(positioned));}
        doc.set_body_history(graph);doc.resolve_constructions();
        kernel::BodyResult stale;stale.mesh.original_references.edges={{{{-999,0,0},{999,0,0}},{feature.id,"stale-support",{}}}};
        document::DocumentSession session(doc,{stale});
        const auto verify=[&] {
            const auto before=session.document().serialized();const auto& native=session.document().sketches.front();
            auto expected=native.viewer_mesh();native.filter_hidden_3d_geometry(expected);expected=session.document().place_body_mesh(std::move(expected),body);
            const auto wire=app::authored_feature_wire(session,feature.id);
            require(wire.size()==1&&wire.front().points==expected.edges.front().points,"Pure Sketch wire differs from native interpolation or includes stale datum geometry");
            const auto child=app::authored_feature_wire(session,native.id);
            require(child.size()==wire.size()&&child.front().points==wire.front().points,"Explicit Sketch child used a different profile wire");
            require(session.document().serialized()==before,"Sketch wire preparation changed the document");
        };
        verify();
        for(int edit=0;edit<3;++edit) {
            auto changed=session.document();auto& source=changed.sketches.front();const auto p=source.points.front();
            require(source.move_point(p.id,p.x+.3,p.y+.2),"Free spline point did not move in selection-wire matrix");
            source=sketcher::Sketch::from_serialized(source.serialized());session.commit(changed,session.calculated_boundaries());verify();
            require(session.undo(),"Sketch wire edit Undo failed");verify();require(session.redo(),"Sketch wire edit Redo failed");verify();
        }
        ++cases;
    }
    std::cout<<cases<<" pure Sketch wire cases: legacy/current/empty Modeling roles, open/closed control/interpolating splines, XY/XZ and placed Body frames, repeated edits/native readback/Undo/Redo passed"<<std::endl;
}
void copy_wire_matrix() {
    for(bool placed:{false,true})for(bool pattern:{false,true}) {
        auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        static_cast<void>(sketch.add_rectangle(2,3,12,9));
        auto source=document::PartDocument::create_feature_container(sketch.id);sketch.owner_container_id=source.id;
        source.feature.sides[0].operation=document::FeatureSideOperation::Extrusion;source.feature.sides[0].length=20;
        auto copy=document::PartDocument::create_feature_container(sketch.id);copy.feature_kind=document::FeatureKind::DerivedCopy;
        copy.derived_copy.source_ids={source.id};copy.placement.x=30;
        copy.derived_copy.reference.owner_id=copy.container_origin.id;copy.derived_copy.reference.semantic_key="origin:plane:yz";
        if(pattern) {
            kernel::PatternRequest request;request.circular=true;request.count=3;request.angle_degrees=45;
            copy.derived_copy.pattern=request;copy.derived_copy.reference.semantic_key="origin:axis:z";
        }
        part.history={source,copy};part.sketches={sketch};document::BodyHistoryGraph graph;
        const auto body=graph.create_body("Authored copy wire");graph.insert({document::PartHistoryKind::Feature,source.id});graph.insert({document::PartHistoryKind::Feature,copy.id});
        if(placed){auto positioned=*graph.find(body);positioned.scope.placement={11,7,-3,20,30,40};graph.update_body(positioned);}
        part.set_body_history(graph);part.resolve_constructions();
        // Deliberately incomplete later result geometry cannot define a copy's
        // authored wire. Selection must recover every source preview stroke.
        kernel::BodyResult later;later.mesh.original_references.edges={{{{0,0,0},{1,0,0}},{source.id,"later-fragment",{}}}};
        document::DocumentSession session(part,{later});const auto before=session.document().serialized();
        kernel::ViewerMesh expected;expected.edges=app::authored_feature_wire(session,source.id);
        kernel::ViewerReferenceGeometry local;local.edges=expected.edges;expected.edges=part.construction_reference_geometry_for(body,local).edges;
        const auto& definition=*part.find_container(copy.id);auto parameters=definition.derived_copy;
        document::PartDocument::resolve_copy_reference(parameters,copy.id,definition.placement,{});
        std::vector<kernel::ViewerEdge> local_expected;
        if(pattern)for(unsigned i=1;i<3;++i){auto mesh=kernel::pattern_copy_mesh(expected,*parameters.pattern,i);local_expected.insert(local_expected.end(),mesh.edges.begin(),mesh.edges.end());}
        else local_expected=kernel::mirrored_viewer_mesh(expected,parameters.resolved_plane,copy.id).edges;
        expected.edges=std::move(local_expected);expected=part.place_body_mesh(std::move(expected),body);
        const auto actual=app::authored_feature_wire(session,copy.id);
        require(!actual.empty()&&actual.size()==expected.edges.size(),"Mirror/Pattern selection lost source preview strokes");
        for(std::size_t i=0;i<actual.size();++i)require(actual[i].points==expected.edges[i].points,"Mirror/Pattern wire differs from its creation preview");
        require(session.document().serialized()==before,"Copy wire inspection changed the native document");
    }
    std::cout<<"Mirror/Pattern authored wire: local/placed Body and incomplete downstream geometry passed"<<std::endl;
}
void line_bend_wire(const kernel::OcctKernel& engine) {
    auto part=document::PartDocument::create_default();
    const auto body=part.body_history.create_body("Line Bend authored wire");
    auto flat=document::PartDocument::create_sketch_container();
    flat.feature_kind=document::FeatureKind::Flat;
    flat.flat.thickness_override=true;flat.flat.thickness=2;
    flat.flat.direction=document::ExtrusionDirection::Reverse;
    auto profile=sketcher::Sketch::create_default();
    profile.owner_container_id=flat.id;flat.flat.sketch_id=profile.id;
    static_cast<void>(profile.add_rectangle(-50,-40,50,40));
    part.insert_history_entry(document::PartHistoryKind::Feature,flat.id);
    part.history.push_back(flat);part.sketches.push_back(profile);
    auto bend=document::PartDocument::create_sketch_container();
    bend.feature_kind=document::FeatureKind::LineBend;
    bend.line_bend.radius=5;bend.line_bend.angle_degrees=90;
    bend.line_bend.thickness_override=true;bend.line_bend.thickness=2;
    bend.line_bend.k_factor_override=true;bend.line_bend.k_factor=.5;
    auto line=sketcher::Sketch::create_default();
    line.owner_container_id=bend.id;bend.line_bend.sketch_id=line.id;
    bend.line_bend.segment_id=line.add_segment(-50,0,50,0);
    part.insert_history_entry(document::PartHistoryKind::Feature,bend.id);
    part.history.push_back(bend);part.sketches.push_back(line);
    const auto calculated=engine.evaluate_history(part.kernel_operations());
    require(!calculated.empty()&&calculated.back().calculation_errors.empty(),
        "Line Bend authored-wire fixture did not calculate");
    document::DocumentSession session(part,calculated);
    const auto wire=app::authored_feature_wire(session,bend.id);
    require(wire.size()>1&&std::ranges::all_of(wire,[&](const auto& edge) {
            return edge.reference.owner_id==bend.id;
        })&&std::ranges::any_of(wire,[](const auto& edge) {
            return edge.points.size()>3;
        }),"Line Bend selection reused only its source line or the old Flat wire");
    const auto& output=calculated.back().body_outputs.at(body)->mesh;
    require(std::ranges::all_of(output.triangle_references,
            [&](const auto& face) {
                return face.display_owner_id==bend.id;
            }),"Line Bend faces do not present the authored Line Bend owner");
}
void planar_surface_wire_and_cues(const kernel::OcctKernel& engine) {
    auto part=document::PartDocument::create_default();
    auto sketch=sketcher::Sketch::create_default();
    static_cast<void>(sketch.add_rectangle(-10,-5,20,10));
    auto feature=document::PartDocument::create_feature_container(sketch.id);
    feature.feature.result_type=document::ProfileResultType::Surface;
    feature.feature.profile_plane_offset=3;
    for(auto& side:feature.feature.sides)
        side.operation=document::FeatureSideOperation::None;
    sketch.owner_container_id=feature.id;sketch.plane_offset=3;
    part.history={feature};part.sketches={sketch};
    document::BodyHistoryGraph graph;
    static_cast<void>(graph.create_body("Planar surface wire"));
    graph.insert({document::PartHistoryKind::Feature,feature.id});
    part.set_body_history(graph);part.resolve_constructions();
    const auto calculated=engine.evaluate_history(part.kernel_operations());
    require(!calculated.empty()&&calculated.back().calculation_errors.empty(),
        "Planar surface cue fixture did not calculate");
    document::DocumentSession session(part,calculated);
    const auto wire=app::authored_feature_wire(session,feature.id);
    require(wire.size()==4&&std::ranges::all_of(wire,[&](const auto& edge) {
        return edge.reference.owner_id==feature.id&&
            edge.reference.semantic_key.starts_with("surface-preview:");
    }),"Planar surface selection did not use its authored Sketch boundary");
    require(!app::workspace_detail::shows_feature_direction_label(feature.feature),
        "Planar surface inspection exposed an extrusion direction label");
}
void actual_sketch_wire(const std::filesystem::path& path) {
    std::vector<kernel::BodyResult> bodies;const auto doc=document::PartDocument::load(path,&bodies);
    const auto before=doc.serialized();document::DocumentSession session(doc,bodies);unsigned checked{};
    for(const auto& feature:doc.history)if(app::authored_sketch_profile(feature)) {
        const auto found=std::ranges::find(doc.sketches,feature.id,&sketcher::Sketch::owner_container_id);
        if(found==doc.sketches.end()||found->bsplines.empty())continue;
        auto expected=found->viewer_mesh();found->filter_hidden_3d_geometry(expected);
        std::erase_if(expected.edges,[&](const auto& edge){return edge.reference.semantic_key!="bspline:"+found->bsplines.front().id;});
        if(const auto* body=doc.body_owner_for_object(feature.id))expected=doc.place_body_mesh(std::move(expected),body->scope.id);
        const auto actual=app::authored_feature_wire(session,feature.id);
        std::cout<<"Sketch wire "<<feature.name<<": native="<<expected.edges.front().points.size()<<" samples, selected="
            <<(actual.empty()?0:actual.front().points.size())<<" samples"<<std::endl;
        require(actual.size()==expected.edges.size(),"Sketch selection includes a foreign or duplicate curve");
        require(actual.front().points==expected.edges.front().points,"Sketch selection wire differs from the actual native Sketch curve");
        const auto child=app::authored_feature_wire(session,found->id);
        require(child.size()==actual.size()&&child.front().points==actual.front().points,"Actual native Sketch child used a different selection curve");
        const auto exact=sketcher::sketch_curve_geometry(*found,found->bsplines.front().id);
        const auto native=found->viewer_mesh();const auto edge=std::ranges::find_if(native.edges,[&](const auto& value){return value.reference.semantic_key=="bspline:"+found->bsplines.front().id;});
        for(std::size_t i=0;i<edge->points.size();++i) {
            const auto p=kernel::bspline_value(exact,double(i)/(edge->points.size()-1));const auto world=found->world_point(p.x,p.y),shown=edge->points[i];
            require(std::hypot(world.x-shown.x,world.y-shown.y,world.z-shown.z)<1e-7,"Exact intersection source differs from the native Sketch interpolation");
        }
        ++checked;
    }
    require(checked>0,"No standalone spline Sketch found in the actual review input");
    require(session.document().serialized()==before,"Sketch selection changed the native document");
}
bool longitudinal(const kernel::ViewerEdge& e,kernel::Vec3 normal) {
    if(e.points.size()!=2)return false;
    const auto a=e.points.front(),b=e.points.back();
    const kernel::Vec3 d{b.x-a.x,b.y-a.y,b.z-a.z};
    const auto length=std::hypot(d.x,d.y,d.z);
    return length>1e-7&&std::abs(std::abs(d.x*normal.x+d.y*normal.y+d.z*normal.z)-length)<1e-7;
}
void rectangular_matrix(const kernel::OcctKernel& engine) {
    unsigned fillets{};
    for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ})
    for(auto mode:{document::ThinMode::OneSide,document::ThinMode::OtherSide,document::ThinMode::Symmetric})
    for(bool reversed:{false,true}) {
        auto doc=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        sketch.plane=plane;sketch.plane_auto=false;sketch.refresh_default_frame();
        static_cast<void>(sketch.add_rectangle(0,0,20,12));
        auto feature=document::PartDocument::create_feature_container(sketch.id);
        sketch.owner_container_id=feature.id;feature.feature.result_type=document::ProfileResultType::Thin;
        feature.feature.thin_mode=mode;feature.feature.thin_thickness=1;feature.feature.sides[0].length=20;
        feature.feature.reversed_sides=reversed;
        doc.history={feature};doc.sketches={sketch};document::BodyHistoryGraph graph;
        static_cast<void>(graph.create_body("Thin wire"));graph.insert({document::PartHistoryKind::Feature,feature.id});
        doc.set_body_history(graph);doc.resolve_constructions();sketch=doc.sketches.front();
        const auto wire=doc.feature_preview_edges(feature,{});
        tests::verify_rectangular_thin_start(doc,feature,wire);
        std::vector<kernel::ViewerEdge> rails;for(const auto& edge:wire)if(longitudinal(edge,sketch.normal()))rails.push_back(edge);
        require(rails.size()==8,"A rectangular Thin preview must contain all eight longitudinal edges, without duplicates");
        const double outer=mode==document::ThinMode::OneSide?0:mode==document::ThinMode::OtherSide?-1:-.5;
        const double inner=mode==document::ThinMode::OneSide?1:mode==document::ThinMode::OtherSide?0:.5;
        std::set<std::array<long long,2>> expected,actual;
        for(double offset:{outer,inner})for(double x:{offset,20-offset})for(double y:{offset,12-offset})
            expected.insert({std::llround(x*1e7),std::llround(y*1e7)});
        for(const auto& edge:rails) {
            const auto p=sketch.local_point(edge.points.front());actual.insert({std::llround(p[0]*1e7),std::llround(p[1]*1e7)});
            const auto a=edge.points.front(),b=edge.points.back();
            require(std::abs(std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)-20)<1e-7,"Thin rail does not end at its authored length");
        }
        require(actual==expected,"Thin rails do not match the independently calculated inner/outer rectangle corners");
        auto boundaries=engine.evaluate_history(doc.kernel_operations());
        require(!boundaries.empty()&&boundaries.back().calculation_errors.empty(),"Thin fillet input failed");
        const auto& local=boundaries.back().body_boundaries.at(graph.active_body_id()).back();
        std::vector<kernel::EdgeReference> edges;
        for(const auto& edge:local.mesh.edges)if(edge.reference.owner_id==feature.id&&longitudinal(edge,sketch.normal())&&
            edge.reference.semantic_key.starts_with("generated:thin:outside:from:"))edges.push_back(edge.reference);
        require(edges.size()==4,"Thin native input did not expose all four outside longitudinal edges");
        for(double radius:{.1,.25,.4}) {
            workspace::Workspace live;live.add_part(doc,boundaries);live.activate(doc.document_id);
            auto treatment=document::PartDocument::create_fillet_container({edges[0],edges[1]});
            treatment.edge_treatment.primary_size=treatment.edge_treatment.secondary_size=radius;
            treatment.edge_treatment.routes={{edges[0]},{edges[1]}};
            treatment.edge_treatment.route_end_faces.resize(treatment.edge_treatment.routes.size());
            require(workspace::commit_edge_treatment(live,engine,doc.document_id,treatment,workspace::EdgeTreatmentEditMode::Create),
                "Two-edge Thin fillet did not commit");
            auto& state=*live.open_part(doc.document_id);
            require(state.session.calculated_boundaries().back().calculation_errors.empty(),"Two-edge Thin fillet retained a calculation failure");
            const auto* treatment_input=workspace::calculated_operation_input(
                state.session,treatment.id);
            require(treatment_input,"Two-edge Thin fillet lost its calculated input");
            std::vector<std::vector<kernel::ViewerEdge>> expected_groups;
            for(std::size_t route=0;route<treatment.edge_treatment.routes.size();++route) {
                expected_groups.push_back(document::treatment_selection_wire(
                    treatment.edge_treatment,route,std::nullopt,
                    treatment_input->mesh));
            }
            const auto expected_wire=app::workspace_detail::
                edge_treatment_preview_wire(expected_groups,
                    treatment.edge_treatment,treatment.feature_kind,
                    treatment.id,&treatment_input->mesh).edges;
            require(expected_wire.size()==
                    4*treatment.edge_treatment.routes.size(),
                "Fillet analytical cage omitted a rail or terminal arc");
            const auto same_point=[](const kernel::Vec3& first,
                                     const kernel::Vec3& second) {
                return std::hypot(first.x-second.x,first.y-second.y,
                    first.z-second.z)<=1.0e-7;
            };
            for(std::size_t route=0;
                    route<treatment.edge_treatment.routes.size();++route) {
                const auto offset=4*route;
                const auto& first_rail=expected_wire[offset];
                const auto& second_rail=expected_wire[offset+1];
                const auto& first_arc=expected_wire[offset+2];
                const auto& last_arc=expected_wire[offset+3];
                require(first_rail.overlay&&second_rail.overlay&&
                        first_arc.overlay&&last_arc.overlay&&
                        first_rail.points.size()>=2&&
                        second_rail.points.size()>=2&&
                        first_arc.points.size()>2&&last_arc.points.size()>2&&
                        same_point(first_arc.points.front(),
                            first_rail.points.front())&&
                        same_point(first_arc.points.back(),
                            second_rail.points.front())&&
                        same_point(last_arc.points.front(),
                            first_rail.points.back())&&
                        same_point(last_arc.points.back(),
                            second_rail.points.back()),
                    "Fillet analytical cage is not two rails closed by two arcs");
            }
            const auto authored_wire=app::authored_feature_wire(
                state.session,treatment.id);
            require(authored_wire.size()==expected_wire.size(),
                "Fillet authored wire omitted one of its persisted routes");
            for(std::size_t edge=0;edge<expected_wire.size();++edge)
                require(authored_wire[edge].points==expected_wire[edge].points,
                    "Fillet authored wire differs from its rollback-input route");
            require(state.session.undo()&&state.session.redo(),"Two-edge Thin fillet Undo/Redo failed");++fillets;
        }
    }
    std::cout<<"Thin rectangle matrix: 12 plane/mode/direction variants, eight exact rails each, "<<fillets<<" two-edge fillets passed\n";
}
void rolling_edge_treatment_preview() {
    using Vec3=kernel::Vec3;
    kernel::ViewerMesh mesh;
    const kernel::FaceReference plane_face{"preview-plane","face",{}};
    const kernel::FaceReference curved_face{"preview-circle","face",{}};
    const auto append_strip=[&](const std::vector<Vec3>& section,
            const kernel::FaceReference& face) {
        for(std::size_t index=1;index<section.size();++index) {
            const auto first=mesh.vertices.size();
            for(const double z:{0.,20.}) {
                mesh.vertices.push_back(
                    {section[index-1].x,section[index-1].y,z});
                mesh.vertices.push_back(
                    {section[index].x,section[index].y,z});
            }
            const auto vertex=static_cast<std::uint32_t>(first);
            mesh.triangles.insert(mesh.triangles.end(),{
                vertex,vertex+1,vertex+3,vertex,vertex+3,vertex+2});
            mesh.triangle_references.push_back(face);
            mesh.triangle_references.push_back(face);
        }
    };
    append_strip({{0,0,0},{40,0,0}},plane_face);
    std::vector<Vec3> circle;
    constexpr std::size_t samples=64;
    for(std::size_t index=0;index<=samples;++index) {
        const double angle=std::acos(-1.)-
            (std::acos(-1.)/2.)*static_cast<double>(index)/samples;
        circle.push_back({20.+20.*std::cos(angle),
            20.*std::sin(angle),0.});
    }
    append_strip(circle,curved_face);
    kernel::ViewerEdge route;
    route.reference={"preview-route","edge",{}};
    route.points={{0,0,5},{0,0,15}};
    route.edge_treatment_side_references={plane_face,curved_face};
    route.edge_treatment_side_directions={
        std::vector<Vec3>{{1,0,0},{1,0,0}},
        std::vector<Vec3>{{0,1,0},{0,1,0}}};
    document::EdgeTreatmentParameters parameters;
    parameters.primary_size=5.;
    const auto preview=app::workspace_detail::edge_treatment_preview_wire(
        {{{route}}},parameters,document::FeatureKind::Fillet,
        "rolling-preview",&mesh);
    require(preview.edges.size()==4&&preview.edges[2].points.size()>2,
        "Rolling-support Fillet preview lost its two rails and end arcs");
    const auto& first_section=preview.edges[2];
    const auto first_contact=first_section.points.front();
    const auto curved_contact=first_section.points.back();
    require(std::abs(first_contact.y)<1.0e-6&&
            std::abs(first_contact.z-5.)<1.0e-6,
        "Rolling-support preview lost its planar contact");
    const auto point_segment_distance=[](const Vec3& point,
            const Vec3& first,const Vec3& second) {
        const Vec3 segment{second.x-first.x,second.y-first.y,
            second.z-first.z};
        const Vec3 delta{point.x-first.x,point.y-first.y,
            point.z-first.z};
        const double squared=segment.x*segment.x+segment.y*segment.y+
            segment.z*segment.z;
        const double ratio=squared<=1.0e-18?0.:std::clamp(
            (delta.x*segment.x+delta.y*segment.y+delta.z*segment.z)/
                squared,0.,1.);
        return std::hypot(std::hypot(
            point.x-(first.x+ratio*segment.x),
            point.y-(first.y+ratio*segment.y)),
            point.z-(first.z+ratio*segment.z));
    };
    double curved_distance=1.0e100;
    for(std::size_t index=1;index<circle.size();++index)
        curved_distance=std::min(curved_distance,point_segment_distance(
            curved_contact,
            {circle[index-1].x,circle[index-1].y,5.},
            {circle[index].x,circle[index].y,5.}));
    require(curved_distance<1.0e-6&&curved_contact.x>.05,
        "Rolling-support preview fell back to the old tangent-line cage");
    std::cout<<"Rolling-support Fillet preview: calculated plane/circle sections and exact lightweight boundary contacts passed\n";
}
void revolution_matrix(const kernel::OcctKernel& engine) {
    unsigned cases{};
    for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ})
    for(auto mode:{document::ThinMode::OneSide,document::ThinMode::OtherSide,document::ThinMode::Symmetric})
    for(bool reversed:{false,true}) {
        auto doc=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        sketch.plane=plane;sketch.plane_auto=false;sketch.refresh_default_frame();
        static_cast<void>(sketch.add_rectangle(5,0,25,12));const auto axis=sketch.add_segment(0,0,0,12,1e-6,true);
        sketch.segments.back().centerline=true;
        auto feature=document::PartDocument::create_feature_container(sketch.id);sketch.owner_container_id=feature.id;
        feature.feature.axis_segment_id=axis;feature.feature.result_type=document::ProfileResultType::Thin;
        feature.feature.thin_mode=mode;feature.feature.thin_thickness=1;feature.feature.reversed_sides=reversed;
        feature.feature.sides[0].operation=document::FeatureSideOperation::Revolution;feature.feature.sides[0].angle_degrees=90;
        doc.history={feature};doc.sketches={sketch};document::BodyHistoryGraph graph;
        static_cast<void>(graph.create_body("Thin revolution wire"));graph.insert({document::PartHistoryKind::Feature,feature.id});
        doc.set_body_history(graph);doc.resolve_constructions();sketch=doc.sketches.front();
        const auto wire=doc.feature_preview_edges(feature,{});unsigned sweeps{};
        std::set<std::array<long long,2>> expected,actual;
        const double outside=mode==document::ThinMode::OneSide?0:mode==document::ThinMode::OtherSide?1:.5;
        const double inside=mode==document::ThinMode::OneSide?1:mode==document::ThinMode::OtherSide?0:.5;
        for(double d:{-outside,inside})for(double x:{5+d,25-d})for(double y:{d,12-d})expected.insert({std::llround(x*1e7),std::llround(y*1e7)});
        for(const auto& edge:wire)if(edge.reference.semantic_key.starts_with("preview:sweep")) {
            ++sweeps;const auto p=sketch.local_point(edge.points.front());actual.insert({std::llround(p[0]*1e7),std::llround(p[1]*1e7)});
            for(const auto point:edge.points) {
                const auto q=sketch.local_point(point);const auto origin=sketch.world_point(0,q[1]);
                require(std::abs(std::hypot(point.x-origin.x,point.y-origin.y,point.z-origin.z)-p[0])<1e-7,
                    "Thin revolution sweep does not retain its native radius");
            }
        }
        require(sweeps==8&&actual==expected,"Thin revolution omitted or duplicated an authored swept junction");
        auto results=engine.evaluate_history(doc.kernel_operations());require(results.back().calculation_errors.empty(),"Thin revolution failed");
        auto body=results.back();engine.ensure_body_properties(body);
        const double area=(20+2*outside)*(12+2*outside)-(20-2*inside)*(12-2*inside);
        require(std::abs(body.volume-area*15*std::acos(-1.)/2)<1e-5,"Thin revolution volume disagrees with the independent section moment");++cases;
    }
    std::cout<<"Thin revolution: "<<cases<<" plane/mode/direction cases, eight swept junctions and independent volume passed"<<std::endl;
}
void review_fillets(const kernel::OcctKernel& engine,const std::filesystem::path& file) {
    std::vector<kernel::BodyResult> calculated;auto source=document::PartDocument::load(file,&calculated);
    workspace::Workspace regenerated;regenerated.add_part(source,calculated);regenerated.activate(source.document_id);
    static_cast<void>(workspace::regenerate_part(regenerated,engine,source.document_id));
    source=regenerated.open_part(source.document_id)->session.document();
    calculated=regenerated.open_part(source.document_id)->session.calculated_boundaries();
    require(calculated.back().calculation_errors.empty(),"Actual review document still fails explicit regeneration");
    const auto review_file=file.parent_path()/"review-regenerated.prtz";
    source.save(review_file,calculated);
    std::cout<<"Regenerated private review: "<<review_file.generic_string()<<std::endl;
    const auto rounded=std::ranges::find(source.history,std::string("Vytažení 007"),&document::HistoryContainer::name);
    require(rounded!=source.history.end(),"Actual rounded review feature 007 is missing");
    const auto profile=std::ranges::find(source.sketches,rounded->feature.sketch_id,&sketcher::Sketch::id);
    require(profile!=source.sketches.end()&&profile->corner_radii.size()==1,"Actual rounded review source is not the reported one-corner profile");
    for(unsigned corners:{1u,2u})for(double radius:{6.,8.619847297682325,10.}) {
        auto edited=*profile;const auto corner=edited.corner_radii.front();
        static_cast<void>(edited.add_corner_fillet(corner.first_segment_id,corner.second_segment_id,radius));
        if(corners==2)static_cast<void>(edited.add_corner_fillet(edited.segments[0].id,edited.segments[3].id,radius));
        const auto before_document=regenerated.open_part(source.document_id)->session.document();
        const auto before=before_document.serialized();
        workspace::commit_profile(regenerated,engine,source.document_id,*rounded,workspace::ProfileEditMode::Replace,edited);
        auto& session=regenerated.open_part(source.document_id)->session;
        require(session.calculated_boundaries().back().calculation_errors.empty(),"Actual 007 one/two-corner radius edit failed");
        const auto after=session.document().serialized();
        if(after!=before) {
            require(session.undo(),"Actual 007 radius edit Undo is unavailable");
            // New corner dimensions reserve identifiers permanently, including
            // across Undo; every authored geometric field must restore exactly.
            auto expected=before_document;expected.dimension_identifiers.retain(session.document().dimension_identifiers);
            require(session.document().serialized()==expected.serialized(),"Actual 007 radius edit Undo lost its source definition");
            require(session.redo()&&session.document().serialized()==after,"Actual 007 radius edit Redo changed its source definition");
        }
        const auto saved=file.parent_path()/("review-007-"+std::to_string(corners)+"-"+std::to_string(radius)+".prtz");
        session.document().save(saved,session.calculated_boundaries());
        std::vector<kernel::BodyResult> reopened;const auto loaded=document::PartDocument::load(saved,&reopened);
        require(loaded.serialized()==after&&reopened.back().calculation_errors.empty(),"Actual 007 save/reopen changed or lost the valid rounded profile");
    }
    std::cout<<"Actual 007: six one/two-corner radius edits, Undo/Redo and native save/reopen passed"<<std::endl;
    for(const auto& feature:source.history)if(feature.feature_kind==document::FeatureKind::Feature&&
            feature.feature.result_type==document::ProfileResultType::Thin) {
        const auto* body=source.body_owner_for_object(feature.id);require(body,"Review Thin feature has no Body");
        auto doc=source;auto graph=doc.body_history;graph.activate(body->scope.id);
        const auto entry=std::ranges::find(body->entries,feature.id,&document::PartHistoryEntry::id);
        graph.set_history_cursor(body->scope.id,static_cast<std::size_t>(entry-body->entries.begin())+1);
        doc.set_body_history(graph);
        document::DocumentSession snapshot(doc,calculated);
        const auto* input=workspace::calculated_operation_input(snapshot,{});require(input,"Review Thin input missing");
        const auto sketch=std::ranges::find(doc.sketches,feature.feature.sketch_id,&sketcher::Sketch::id);
        require(sketch!=doc.sketches.end(),"Review Thin profile missing");
        if(sketch->corner_radii.empty()&&sketch->segments.size()==4) {
            const auto wire=app::authored_feature_wire(snapshot,feature.id);
            tests::verify_rectangular_thin_start(doc,feature,wire);
        }
        std::vector<kernel::ViewerEdge> candidates;
        for(const auto& edge:input->mesh.edges)if(edge.reference.owner_id==feature.id&&longitudinal(edge,sketch->normal()))
            candidates.push_back(edge);
        std::cout<<"Review "<<feature.name<<" longitudinal input edges="<<candidates.size()<<std::endl;
        if(candidates.size()<2){std::cout<<"Review has no calculated material edges; input errors:";
            for(const auto& [id,error]:input->calculation_errors)std::cout<<' '<<id<<": "<<error;
            std::cout<<'\n';continue;}
        for(double radius:{.1,.25,.4,1.}) {
            workspace::Workspace live;live.add_part(doc,calculated);live.activate(doc.document_id);
            auto treatment=document::PartDocument::create_fillet_container({candidates[0].reference,candidates[1].reference});
            treatment.edge_treatment.primary_size=treatment.edge_treatment.secondary_size=radius;
            treatment.edge_treatment.routes={{candidates[0].reference},{candidates[1].reference}};
            const auto before=live.open_part(doc.document_id)->session.document().serialized();
            bool committed=false;
            try {
                committed=workspace::commit_edge_treatment(live,engine,doc.document_id,treatment,workspace::EdgeTreatmentEditMode::Create);
            }catch(const std::exception& error) {
                require(live.open_part(doc.document_id)->session.document().serialized()==before,"Rejected review fillet mutated its document");
                std::cout<<"Review "<<feature.name<<" two-edge radius="<<radius<<" rejected: "<<error.what()<<'\n';
                throw;
            }
            require(committed,"Review fillet did not commit");
            const auto& errors=live.open_part(doc.document_id)->session.calculated_boundaries().back().calculation_errors;
            std::cout<<"Review "<<feature.name<<" two-edge radius="<<radius<<" errors="<<errors.size()<<'\n';
            for(const auto& [id,error]:errors)std::cout<<"  "<<(id==treatment.id?"FILLET":"OTHER")<<": "<<error<<'\n';
            require(errors.empty(),"Actual review two-edge fillet retained a calculation error");

        }
    }
}
void rounded_profile_matrix(const kernel::OcctKernel& engine) {
    unsigned cases{};
    for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ})
    for(bool reversed:{false,true})
    for(auto mode:{document::ThinMode::OneSide,document::ThinMode::OtherSide,document::ThinMode::Symmetric})
    for(unsigned corners:{1u,2u}) {
        auto doc=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        sketch.plane=plane;sketch.plane_auto=false;sketch.refresh_default_frame();
        static_cast<void>(sketch.add_rectangle(0,0,30,40));const auto segments=sketch.segments;
        auto feature=document::PartDocument::create_feature_container(sketch.id);sketch.owner_container_id=feature.id;
        feature.feature.result_type=document::ProfileResultType::Thin;feature.feature.thin_mode=mode;
        feature.feature.thin_thickness=1;feature.feature.sides[0].length=20;feature.feature.reversed_sides=reversed;
        doc.history={feature};document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Rounded Thin profile"));
        graph.insert({document::PartHistoryKind::Feature,feature.id});doc.set_body_history(graph);
        std::vector<std::string> corner_ids;
        for(double radius:{8.619847297682325,6.,10.}) {
            static_cast<void>(sketch.add_corner_fillet(segments[0].id,segments[1].id,radius));
            if(corners==2)static_cast<void>(sketch.add_corner_fillet(segments[2].id,segments[3].id,radius));
            if(corner_ids.empty())for(const auto& corner:sketch.corner_radii)corner_ids.push_back(corner.id);
            else for(std::size_t i=0;i<corner_ids.size();++i)require(sketch.corner_radii[i].id==corner_ids[i],"Radius edit replaced a source corner identity");
            doc.sketches={sketch};doc.resolve_constructions();
            const auto wire=doc.feature_preview_edges(feature,{});unsigned rails{};
            std::set<std::array<long long,2>> actual,expected_points;
            for(const auto& edge:wire)if(longitudinal(edge,doc.sketches.front().normal()))++rails;
            require(rails==2*(4+corners),"Rounded Thin preview omitted or duplicated native tangent-junction rails");
            auto results=engine.evaluate_history(doc.kernel_operations());
            require(!results.empty()&&results.back().calculation_errors.empty(),"A valid rounded Thin profile failed to calculate");
            auto measured=results.back();engine.ensure_body_properties(measured);
            const double outside=mode==document::ThinMode::OneSide?0:mode==document::ThinMode::OtherSide?1:.5;
            const double inside=mode==document::ThinMode::OneSide?1:mode==document::ThinMode::OtherSide?0:.5;
            const auto quantize=[](double x,double y){return std::array{std::llround(x*1e7),std::llround(y*1e7)};};
            for(double offset:{-outside,inside}) {
                for(const auto& point:std::vector<std::array<double,2>>{{offset,offset},{30-radius,offset},{30-offset,radius},{30-offset,40-offset}})
                    expected_points.insert(quantize(point[0],point[1]));
                if(corners==1)expected_points.insert(quantize(offset,40-offset));
                else {expected_points.insert(quantize(radius,40-offset));expected_points.insert(quantize(offset,40-radius));}
            }
            for(const auto& edge:wire)if(longitudinal(edge,doc.sketches.front().normal())) {
                const auto p=doc.sketches.front().local_point(edge.points.front());actual.insert(quantize(p[0],p[1]));
            }
            require(actual==expected_points,"Rounded Thin rails do not match exact offset line/arc junctions");
            const auto area=[&](double offset){return (30+2*offset)*(40+2*offset)-corners*(1-std::acos(-1.)/4)*std::pow(radius+offset,2);};
            const auto expected=20*(area(outside)-area(-inside));
            require(std::abs(measured.volume-expected)<1e-5,"Rounded Thin volume disagrees with the independent line/arc area integral");
            sketch=sketcher::Sketch::from_serialized(sketch.serialized());++cases;
        }
    }
    std::cout<<"Rounded Thin profiles: "<<cases<<" native volume/rail/identity/radius-edit cases passed"<<std::endl;
}

}
int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    try{
        if(argc>1&&std::string_view(argv[1])=="--sketch-profile-wire"){sketch_wire_matrix();copy_wire_matrix();return 0;}
        if(const auto* file=std::getenv("ZIMA_SKETCH_WIRE_REVIEW_FILE")){actual_sketch_wire(std::filesystem::u8path(file));return 0;}
        kernel::OcctKernel engine;line_bend_wire(engine);
        planar_surface_wire_and_cues(engine);rectangular_matrix(engine);
        rolling_edge_treatment_preview();
        revolution_matrix(engine);rounded_profile_matrix(engine);
        if(const auto* file=std::getenv("ZIMA_THIN_REVIEW_FILE"))review_fillets(engine,std::filesystem::u8path(file));
        return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
