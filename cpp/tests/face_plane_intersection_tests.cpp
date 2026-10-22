#include <zima/document/part_document.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/face_plane_intersection.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>

using namespace zima;
namespace {
void check(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
struct Fixture {document::PartDocument part;std::vector<kernel::BodyResult> bodies;std::string owner;};
Fixture make(kernel::OcctKernel& kernel,bool circular) {
    kernel::FaceMeasurementScope measurements(false); // Ordinary native GUI calculation policy.
    Fixture result;result.part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
    if(circular)static_cast<void>(sketch.add_circle(0,0,5));else static_cast<void>(sketch.add_rectangle(-5,-4,5,4));
    auto feature=document::PartDocument::create_feature_container(sketch.id);sketch.owner_container_id=feature.id;
    feature.feature.sides[0].length=10;result.owner=feature.id;result.part.history={feature};result.part.sketches={sketch};
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Intersection source"));graph.insert({document::PartHistoryKind::Feature,feature.id});
    result.part.set_body_history(graph);result.part.resolve_constructions();result.bodies=kernel.evaluate_history(result.part.kernel_operations());
    check(!result.bodies.empty()&&result.bodies.back().calculation_errors.empty(),"Intersection source calculation failed");return result;
}
kernel::FaceReference face(const Fixture& fixture,bool circular) {
    const auto& mesh=fixture.bodies.back().mesh;
    for(std::size_t i=0;i<mesh.triangle_references.size();++i) {
        const auto& ref=mesh.triangle_references[i];if(ref.owner_id!=fixture.owner)continue;
        if(circular) {if(ref.surface&&ref.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder)return ref;continue;}
        bool selected=true;for(int j=0;j<3;++j)selected&=std::abs(mesh.vertices.at(mesh.triangles.at(3*i+j)).y+4)<1e-9;
        if(selected)return ref;
    }
    throw std::runtime_error("Expected native source face was not published");
}
std::set<std::string> identities(const kernel::ViewerReferenceGeometry& packet){std::set<std::string> result;for(const auto& edge:packet.edges)result.insert(edge.reference.semantic_key);for(const auto& point:packet.points)result.insert(point.reference.semantic_key);return result;}
kernel::BoundarySurfaceRequest concave_face() {
    kernel::BoundarySurfaceRequest request;request.region_id="concave-region";
    const std::array<kernel::Vec3,8> points{{{0,0,0},{100,0,0},{100,20,0},{20,20,0},{20,60,0},{100,60,0},{100,80,0},{0,80,0}}};
    request.boundaries.resize(points.size());request.source_owners.resize(points.size());
    for(std::size_t i=0;i<points.size();++i) {
        auto& boundary=request.boundaries[i];request.source_owners[i]="authored-boundary-"+std::to_string(i);
        boundary.outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::LineCurve{points[i],points[(i+1)%points.size()]}}};
        boundary.outer_edge_source_ids={"curve"};boundary.outer_vertex_source_ids={"first"};boundary.open_profile_end_id="last";
    }
    return request;
}
}
int main(){try {
    kernel::OcctKernel kernel;unsigned cases=0;
    for(bool circular:{false,true}) {
        const auto fixture=make(kernel,circular);const auto& body=fixture.bodies.back();const auto before=fixture.part.serialized();const auto shape=body.kernel_shape;
        const auto source=face(fixture,circular);std::set<std::string> keys;
        for(double height:{2.,7.,5.})for(double side:{-1.,1.}) {
            kernel::FacePlaneIntersectionRequest request{source,"owning-sketch",{0,0,height},{0,0,side},1e-6};
            const auto result=kernel.intersect_face_plane(body,request);check(result.edges.size()==1,"Unique face/plane intersection did not create one curve");
            const auto& edge=result.edges.front();check(edge.reference.valid()&&edge.exact_spline.has_value(),"Intersection lost exact geometry or native ancestry");
            for(int i=0;i<=100;++i){const auto p=kernel::bspline_value(*edge.exact_spline,i/100.);check(std::abs(p.z-height)<1e-7,"Intersection escaped the actual Sketch plane");
                check(circular?std::abs(std::hypot(p.x,p.y)-5)<1e-7:std::abs(p.y+4)<1e-7&&p.x>=-5-1e-7&&p.x<=5+1e-7,"Intersection escaped its exact bounded source face");}
            if(keys.empty())keys=identities(result);else check(identities(result)==keys,"Plane displacement/direction renamed a unique branch");
            check(fixture.part.serialized()==before&&body.kernel_shape==shape,"Reference calculation modified the source");++cases;
        }
        const auto empty=kernel.intersect_face_plane(body,{source,"owning-sketch",{0,0,20},{0,0,1},1e-6});
        check(empty.edges.empty()&&empty.points.empty(),"Disjoint plane invented an intersection");++cases;
        const auto stored=document::serialize_body_result(body);const auto cold_body=document::load_body_result(stored);
        kernel::OcctKernel reopened_engine;
        if(circular) {
            const auto curve=reopened_engine.intersect_face_plane(cold_body,{source,"owning-sketch",{0,0,5},{0,0,1},1e-6});
            check(curve.edges.size()==1&&identities(curve)==keys,"Cold solid circle lost its source identity");
        } else {
            // Ordinary solids currently save face-only bindings. Do not turn
            // missing boundary ancestry into guessed endpoints or hidden replay.
            bool missing_ancestry=false;
            try{static_cast<void>(reopened_engine.intersect_face_plane(cold_body,{source,"owning-sketch",{0,0,5},{0,0,1},1e-6}));}
            catch(const std::runtime_error& error){missing_ancestry=std::string(error.what())=="Intersection endpoint ancestry is ambiguous.";}
            check(missing_ancestry,"Cold solid open section bypassed its missing persisted boundary ancestry");
        }
        check(document::serialize_body_result(cold_body)==stored,"Cold solid intersection changed the source packet");++cases;
        auto moved=fixture.part;auto owner=moved.body_history.bodies().front();
        owner.scope.placement.x=12;owner.scope.placement.y=-7;owner.scope.placement.z=20;
        owner.scope.placement.rotation_x=owner.scope.placement.absolute_rotation_x=90;
        moved.body_history.update_body(owner);moved.resolve_constructions();
        const auto placed=kernel.evaluate_history(moved.kernel_operations());
        check(placed.back().calculation_errors.empty(),"Placed source calculation failed");
        for(double height:{2.,7.,5.})for(double side:{-1.,1.}) {
            const auto result=kernel.intersect_face_plane(placed.back(),{source,"owning-sketch",{12,-7-height,20},{0,side,0},1e-6});
            check(result.edges.size()==1&&identities(result)==keys,"Body placement renamed or lost the unique branch");
            for(int i=0;i<=100;++i) {const auto p=kernel::bspline_value(*result.edges.front().exact_spline,i/100.);
                check(std::abs(p.y+7+height)<1e-7,"Intersection used the unplaced Sketch plane");
                check(circular?std::abs(std::hypot(p.x-12,p.z-20)-5)<1e-7:std::abs(p.z-16)<1e-7&&p.x>=7-1e-7&&p.x<=17+1e-7,"Intersection used an unplaced source face");}
            ++cases;
        }
    }
    // Reuse the established surface-intersection ancestry for two bounded
    // branches. Native archive addresses survive a cold calculation service;
    // no OCCT traversal index or geometric sorting defines a branch identity.
    const auto source=kernel.evaluate_history({{"concave-face",concave_face()}}).back();
    const auto packet=document::serialize_body_result(source);
    const auto reopened=document::load_body_result(packet);kernel::OcctKernel cold;
    const kernel::FaceReference face{"concave-face","surface:from:concave-region",{}};
    std::set<std::string> branches;
    for(const auto* engine:{&kernel,&cold})for(double x:{40.,70.,50.})for(double side:{-1.,1.}) {
        const auto curves=engine->intersect_face_plane(reopened,{face,"section-sketch",{x,0,0},{side,0,0},1e-6});
        check(curves.edges.size()==2,"A bounded concave face lost a disconnected branch");
        const auto keys=identities(curves);if(branches.empty())branches=keys;else check(keys==branches,"Moving the Sketch plane renamed a disconnected branch");
        double length=0;
        for(const auto& edge:curves.edges) {
            check(edge.exact_spline.has_value()&&edge.measured_length.has_value(),"A disconnected branch lost exact geometry");length+=*edge.measured_length;
            for(int i=0;i<=100;++i) {const auto p=kernel::bspline_value(*edge.exact_spline,i/100.);
                check(std::abs(p.x-x)<1e-7&&std::abs(p.z)<1e-7&&((p.y>=-1e-7&&p.y<=20+1e-7)||(p.y>=60-1e-7&&p.y<=80+1e-7)),"A branch crossed the empty concave domain");}
        }
        check(std::abs(length-40)<1e-7,"Disconnected branches were replaced by their bounding box");++cases;
    }
    const auto merged=cold.intersect_face_plane(reopened,{face,"section-sketch",{10,0,0},{1,0,0},1e-6});
    check(merged.edges.size()==1&&!branches.contains(merged.edges.front().reference.semantic_key),"A merged branch silently inherited a previously selected branch");++cases;
    bool coincident_rejected=false;
    try{static_cast<void>(cold.intersect_face_plane(reopened,{face,"section-sketch",{0,0,0},{0,0,1},1e-6}));}
    catch(const std::runtime_error&){coincident_rejected=true;}
    check(coincident_rejected,"Coincident face area invented a unique curve");++cases;
    check(document::serialize_body_result(reopened)==packet,"Cold intersection changed the persisted source");
    kernel::RevolutionRequest sphere;sphere.surface_result=true;sphere.axis_direction={0,1,0};sphere.profile_region_id="sphere-region";
    sphere.outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::ArcCurve{{0,-20,0},{20,0,0},{0,20,0}}}};
    sphere.outer_edge_source_ids={"meridian"};sphere.outer_vertex_source_ids={"south"};sphere.open_profile_end_id="north";
    const auto spherical=kernel.evaluate_history({{"sphere",sphere}}).back();
    check(spherical.calculation_errors.empty(),"Sphere intersection source failed");
    std::set<std::string> circle_keys;
    for(double side:{-1.,1.}) {
        for(double x:{5.,10.,-5.}) {
            const auto curves=cold.intersect_face_plane(spherical,{{"sphere","generated:meridian",{}},"sphere-sketch",{x,0,0},{side,0,0},1e-6});
            check(curves.edges.size()==1&&curves.points.empty(),"Spherical circle invented a parameter-seam point");
            const auto& edge=curves.edges.front();check(edge.exact_spline.has_value(),"Spherical section lost exact circle geometry");
            const auto ids=identities(curves);if(circle_keys.empty())circle_keys=ids;else check(ids==circle_keys,"Sphere displacement renamed its closed branch");
            for(int i=0;i<=100;++i) {const auto p=kernel::bspline_value(*edge.exact_spline,i/100.);
                check(std::abs(p.x-x)<1e-7&&std::abs(p.x*p.x+p.y*p.y+p.z*p.z-400)<1e-5,"Spherical section escaped its supports");}++cases;
        }
        const auto contact=cold.intersect_face_plane(spherical,{{"sphere","generated:meridian",{}},"sphere-sketch",{20,0,0},{side,0,0},1e-6});
        check(contact.edges.empty()&&contact.points.size()==1&&std::hypot(contact.points.front().position.x-20,contact.points.front().position.y,contact.points.front().position.z)<1e-7,"Tangent sphere contact was lost or replaced by a curve");++cases;
        const auto none=cold.intersect_face_plane(spherical,{{"sphere","generated:meridian",{}},"sphere-sketch",{21,0,0},{side,0,0},1e-6});
        check(none.edges.empty()&&none.points.empty(),"Disjoint sphere invented a contact");++cases;
    }
    kernel::RevolutionRequest torus;torus.surface_result=true;torus.axis_direction={0,0,1};torus.profile_normal={0,1,0};torus.profile_region_id="torus-region";
    torus.outer_profile=kernel::ExtrusionRequest::CircleProfile{{30,0,0},5};torus.outer_edge_source_ids={"circle"};
    const auto toroidal=kernel.evaluate_history({{"torus",torus}}).back();check(toroidal.calculation_errors.empty(),"Torus ambiguity fixture failed");
    const auto torus_packet=document::serialize_body_result(toroidal);
    const auto torus_reopened=document::load_body_result(torus_packet);
    std::set<std::string> torus_keys;
    for(const auto* engine:{&kernel,&cold})for(double height:{0.,2.,-2.,4.,-4.})for(double side:{-1.,1.}) {
        const auto curves=engine->intersect_face_plane(torus_reopened,{{"torus","generated:circle",{}},"torus-sketch",{0,0,height},{0,0,side},1e-6});
        check(curves.edges.size()==2&&curves.points.empty(),"Coaxial torus section lost either closed branch");
        const auto keys=identities(curves);if(torus_keys.empty())torus_keys=keys;else check(keys==torus_keys,"Torus height/normal changed semantic branch identity");
        std::set<std::string> roles;
        for(const auto& edge:curves.edges) {
            const bool inner=edge.reference.semantic_key.ends_with(":closed:domain:torus-inner");
            check(inner||edge.reference.semantic_key.ends_with(":closed:domain:torus-outer"),"Torus branch has no native domain role");
            roles.insert(inner?"inner":"outer");check(edge.exact_spline.has_value(),"Closed torus section lost exact geometry");
            const double radius=30+(inner?-1:1)*std::sqrt(25-height*height);
            for(int i=0;i<=100;++i) {const auto p=kernel::bspline_value(*edge.exact_spline,i/100.);
                check(std::abs(p.z-height)<1e-7&&std::abs(std::hypot(p.x,p.y)-radius)<1e-7,"Torus branch escaped its analytic radial domain");}
        }
        check(roles.size()==2,"Torus section duplicated a semantic branch");++cases;
    }
    for(double height:{-5.,5.}) {
        const auto tangent=cold.intersect_face_plane(torus_reopened,{{"torus","generated:circle",{}},"torus-sketch",{0,0,height},{0,0,1},1e-6});
        check(tangent.edges.size()==1&&!torus_keys.contains(tangent.edges.front().reference.semantic_key),"Merged torus branch inherited a disappearing radial domain");++cases;
    }
    const auto disjoint=cold.intersect_face_plane(torus_reopened,{{"torus","generated:circle",{}},"torus-sketch",{0,0,6},{0,0,1},1e-6});
    check(disjoint.edges.empty()&&disjoint.points.empty(),"Disjoint torus section invented a branch");++cases;
    check(document::serialize_body_result(torus_reopened)==torus_packet,"Torus branch classification changed its source");
    // Change both source radii and its rigid frame; identity follows native
    // radial domains, independently of world-space direction and coordinates.
    for(const auto size:{std::pair{40.,7.},std::pair{25.,3.}}) {
        auto changed=torus;changed.axis_point={12,-7,20};changed.axis_direction={0,-1,0};changed.profile_normal={0,0,1};
        changed.outer_profile=kernel::ExtrusionRequest::CircleProfile{{12+size.first,-7,20},size.second};
        const auto calculated=kernel.evaluate_history({{"torus",changed}}).back();check(calculated.calculation_errors.empty(),"Changed torus source failed");
        const auto serialized=document::serialize_body_result(calculated);const auto reopened_source=document::load_body_result(serialized);
        for(const auto* engine:{&kernel,&cold})for(double height:{0.,size.second*.6})for(double side:{-1.,1.}) {
            const auto curves=engine->intersect_face_plane(reopened_source,{{"torus","generated:circle",{}},"torus-sketch",{12,-7-height,20},{0,side,0},1e-6});
            check(curves.edges.size()==2&&identities(curves)==torus_keys,"Source size/frame change rebound a torus branch");
            for(const auto& edge:curves.edges) {
                const bool inner=edge.reference.semantic_key.ends_with(":closed:domain:torus-inner");
                const double radius=size.first+(inner?-1:1)*std::sqrt(size.second*size.second-height*height);
                check(edge.exact_spline.has_value(),"Placed torus section lost exact geometry");
                for(int i=0;i<=100;++i) {const auto p=kernel::bspline_value(*edge.exact_spline,i/100.);
                    check(std::abs(p.y+7+height)<1e-7&&std::abs(std::hypot(p.x-12,p.z-20)-radius)<1e-7,"Placed torus section used an unplaced support");}
            }++cases;
        }
        check(document::serialize_body_result(reopened_source)==serialized,"Placed torus calculation changed the source");
    }
    // The same shared calculation also serves ordinary surface Intersection.
    // Reversing its two reference fields must keep both curve identities.
    kernel::ExtrusionRequest section;section.surface_result=true;section.direction={0,100,0};
    section.outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::LineCurve{{-50,-50,0},{50,-50,0}}}};
    section.profile_region_id="section-region";section.outer_edge_source_ids={"span"};section.outer_vertex_source_ids={"first"};section.open_profile_end_id="last";
    kernel::SurfaceIntersectionRequest intersection{{kernel::FaceReference{"torus","generated:circle",{}},kernel::FaceReference{"plane","generated:span",{}}},1e-6};
    std::set<std::string> ordinary_keys;
    for(bool reverse:{false,true}) {
        if(reverse)std::swap(intersection.faces[0],intersection.faces[1]);
        const auto result=kernel.evaluate_history({{"torus",torus},{"plane",section},{"intersection",intersection}}).back();
        check(result.calculation_errors.empty(),"Ordinary two-branch surface Intersection failed");
        kernel::ViewerReferenceGeometry curves;
        for(const auto& edge:result.mesh.original_references.edges)if(edge.reference.owner_id=="intersection")curves.edges.push_back(edge);
        check(curves.edges.size()==2,"Ordinary Intersection lost a torus branch");
        const auto keys=identities(curves);if(ordinary_keys.empty())ordinary_keys=keys;else check(keys==ordinary_keys,"Swapping source faces renamed closed branches");++cases;
    }
    bool ambiguous_rejected=false;
    try{static_cast<void>(cold.intersect_face_plane(torus_reopened,{{"torus","generated:circle",{}},"torus-sketch",{0,0,0},{1,0,0},1e-6}));}
    catch(const std::runtime_error& error){ambiguous_rejected=std::string(error.what())=="Intersection branch ancestry is ambiguous.";}
    check(ambiguous_rejected,"Two closed branches with identical ancestry silently acquired an arbitrary identity");++cases;
    std::cout<<cases<<" exact bounded face/Sketch-plane, both normal directions, source displacement and read-only cases passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}}
