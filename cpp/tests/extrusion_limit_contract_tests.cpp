#include <zima/document/part_document.hpp>
#include <zima/document/profile_targets.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <set>
using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){if(!std::isfinite(actual)||std::abs(actual-expected)>1e-5)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
document::PartDocument profile_document() {
    auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_circle(0,0,5));
    auto feature=document::PartDocument::create_extrusion_container(sketch.id);sketch.owner_container_id=feature.id;
    auto part=document::PartDocument::create_default();part.history={feature};part.sketches={sketch};
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Limits"));graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
    return part;
}
std::vector<kernel::HistoryOperation> profile(){return profile_document().kernel_operations();}
kernel::ExtrusionRequest& extrusion(std::vector<kernel::HistoryOperation>& operations) {
    for(auto& operation:operations)if(auto* request=std::get_if<kernel::ExtrusionRequest>(&operation.primitive))return *request;
    throw std::runtime_error("Missing fixture extrusion");
}
std::set<std::string> faces(const kernel::BodyResult& body){std::set<std::string> result;for(const auto& ref:body.mesh.original_references.triangle_references)result.insert(ref.semantic_key);return result;}
void limits(const kernel::OcctKernel& kernel) {
    auto operations=profile();auto& request=extrusion(operations);
    request.start_offset=-2;request.direction={0,0,12};
    request.extent=kernel::ExtrusionRequest::Extent::UpToPlane;
    request.target_face={"upper-datum","plane",{}};request.target_is_datum=true;
    request.target_plane_origin={0,0,7};request.target_plane_normal={-.2,0,1};
    request.reverse_limit=kernel::ExtrusionLimit{true,{"lower-datum","plane",{}},true,{0,0,-3},{.1,0,1},{}};
    auto body=kernel.evaluate_history(operations).back();near(body.volume,250*std::numbers::pi);
    const auto original=faces(body);check(original.size()==3,"Clipping lost original cap or side identities");
    const auto fingerprint=body.source_fingerprint;
    request.reverse_limit->origin.z=-4;
    body=kernel.evaluate_history(operations).back();near(body.volume,275*std::numbers::pi);
    check(body.source_fingerprint!=fingerprint,"Changing the second end did not invalidate cache");check(faces(body)==original,"Changing the second end changed source identities");
    request.extent=kernel::ExtrusionRequest::Extent::Blind;
    body=kernel.evaluate_history(operations).back();near(body.volume,350*std::numbers::pi);
    request.wall=kernel::ProfileWall{-.5,.5,{}};
    body=kernel.evaluate_history(operations).back();near(body.volume,140*std::numbers::pi);
    request.wall.reset();request.reverse_limit->origin.z=3;bool rejected=false;
    try{static_cast<void>(kernel.evaluate_history(operations));}catch(const std::exception&){rejected=true;}
    check(rejected,"Reverse end on the wrong side was accepted");
    request.reverse_limit.reset();request.extent=kernel::ExtrusionRequest::Extent::UpToPlane;
    request.target_plane_origin={0,0,2};request.target_plane_normal={-.8,0,1};rejected=false;
    try{static_cast<void>(kernel.evaluate_history(operations));}catch(const std::exception&){rejected=true;}
    check(rejected,"Inclined plane crossing a circle away from its seam was accepted");
}
void cuts(const kernel::OcctKernel& kernel) {
    auto operations=profile();
    kernel::BoxRequest stock;stock.length=20;stock.width=20;stock.height=20;stock.translation={-10,-10,-10};
    const auto iterator=std::ranges::find_if(operations,[](const auto& op){return std::holds_alternative<kernel::ExtrusionRequest>(op.primitive);});
    check(iterator!=operations.end(),"Missing cut fixture");
    const auto inserted=operations.insert(iterator,{"test-stock",stock,kernel::BooleanOperation::Add});
    inserted->body=std::next(inserted)->body;
    std::next(inserted)->operation=kernel::BooleanOperation::Subtract;
    auto& request=extrusion(operations);request.extent=kernel::ExtrusionRequest::Extent::UpToPlane;
    request.target_face={"cut-end-datum","plane",{}};request.target_is_datum=true;request.target_plane_origin={0,0,7};
    request.through_all_forward=false;request.through_all_reverse=true;
    auto body=kernel.evaluate_history(operations).back();near(body.volume,8000-425*std::numbers::pi);
    request.through_all_forward=true;request.through_all_reverse=false;request.extent=kernel::ExtrusionRequest::Extent::ThroughAll;
    request.reverse_limit=kernel::ExtrusionLimit{true,{"cut-start-datum","plane",{}},true,{0,0,-3},{0,0,1},{}};
    body=kernel.evaluate_history(operations).back();near(body.volume,8000-325*std::numbers::pi);
    // Query the persisted original bottom face rather than inventing an OCCT index.
    const auto packet=body.mesh.original_references;
    const auto face=std::ranges::find_if(packet.triangle_references,[](const auto& ref){return ref.owner_id=="test-stock" && ref.surface && ref.surface->kind==kernel::SurfaceGeometry::Kind::Plane && std::abs(ref.surface->origin.z+10)<1e-8 && std::abs(ref.surface->axis.z)>.9;});
    check(face!=packet.triangle_references.end(),"Missing original stock bottom plane");
    request.reverse_limit->reference=*face;request.reverse_limit->datum=false;
    request.reverse_limit->origin={0,0,-200}; // An old numerical snapshot must not drive the current original plane.
    request.extent=kernel::ExtrusionRequest::Extent::Blind;
    body=kernel.evaluate_history(operations).back();near(body.volume,8000-500*std::numbers::pi);
    request.reverse_limit->planar=false;
    request.reverse_limit->triangles.clear();
    for(std::size_t i=0;i<packet.triangle_references.size();++i)if(packet.triangle_references[i]==*face)
        for(std::size_t j=0;j<3;++j)request.reverse_limit->triangles.push_back(packet.vertices[packet.triangles[i*3+j]]);
    body=kernel.evaluate_history(operations).back();near(body.volume,8000-500*std::numbers::pi);
    auto& stock_request=std::get<kernel::BoxRequest>(operations.front().primitive);stock_request.translation.z=-8;
    body=kernel.evaluate_history(operations).back();near(body.volume,8000-450*std::numbers::pi);
    double minimum=1e100;
    const auto& references=body.mesh.original_references;
    for(std::size_t i=0;i<references.triangle_references.size();++i)if(references.triangle_references[i].owner_id!= "test-stock")
        for(std::size_t j=0;j<3;++j)minimum=std::min(minimum,references.vertices[references.triangles[i*3+j]].z);
    near(minimum,-8); // Changing the source must also replace the derived original-face mesh.
    request.reverse_limit->reference.semantic_key="missing-native-face";bool rejected=false;
    try{static_cast<void>(kernel.evaluate_history(operations));}catch(const std::exception&){rejected=true;}
    check(rejected,"Missing original reverse surface was accepted");
}

void native_limits(const kernel::OcctKernel& kernel) {
    auto part=profile_document();
    auto upper=document::PartDocument::create_construction(document::ConstructionKind::Plane);
    auto lower=document::PartDocument::create_construction(document::ConstructionKind::Plane);
    upper.base_plane=lower.base_plane=document::LocalDatumPlane::XY;upper.offset=7;lower.offset=-3;
    part.constructions={upper,lower};document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Native limits"));
    graph.insert({document::PartHistoryKind::Construction,upper.id});graph.insert({document::PartHistoryKind::Construction,lower.id});
    graph.insert({document::PartHistoryKind::Feature,part.history.front().id});part.set_body_history(graph);
    auto& p=part.history.front().extrusion;p.extent_mode=document::ProfileExtentMode::TwoSides;
    p.end_condition_forward=p.end_condition_reverse=document::EndCondition::UpTo;
    p.end_targets_forward={{document::EndTargetKind::Plane,{upper.entity_id,"plane",{}},"Upper",{0,0,400},{0,0,1},{}}};
    p.end_targets_reverse={{document::EndTargetKind::Plane,{lower.entity_id,"plane",{}},"Lower",{0,0,-400},{0,0,1},{}}};
    const auto calculate=[&](double expected,double low,double high) {
        auto resolved=part;resolved.resolve_constructions();const auto body=kernel.evaluate_history(resolved.kernel_operations()).back();near(body.volume,expected);
        const auto preview=resolved.extrusion_preview_edges(resolved.history.front());kernel::ModelEnvelope bounds;
        for(const auto& edge:preview)for(const auto& point:edge.points)bounds.include(point);
        check(bounds.valid,"Native target preview is empty");near(bounds.minimum.z,low);near(bounds.maximum.z,high);
    };
    calculate(250*std::numbers::pi,-3,7);
    part.constructions.front().offset=9;calculate(300*std::numbers::pi,-3,9);
    p.extent_mode=document::ProfileExtentMode::Symmetric;calculate(450*std::numbers::pi,-9,9);
    p.result_type=document::ProfileResultType::Thin;p.thin_mode=document::ThinMode::Symmetric;p.thin_thickness=1;
    calculate(180*std::numbers::pi,-9,9);
    p.direction=document::ExtrusionDirection::Reverse;part.constructions.front().offset=-9;calculate(180*std::numbers::pi,-9,9);
    p.end_targets_reverse.front().reference.owner_id="missing-plane";
    calculate(180*std::numbers::pi,-9,9); // Symmetry does not evaluate the unused reverse reference.
    p.extent_mode=document::ProfileExtentMode::TwoSides;
    bool rejected=false;try{auto resolved=part;resolved.resolve_constructions();static_cast<void>(resolved.kernel_operations());}catch(const std::exception&){rejected=true;}
    check(rejected,"Native missing datum end reference was accepted");
}

void coarse_target_classification() {
    kernel::ViewerReferenceGeometry packet;
    packet.vertices={{0,0,5},{1,0,5},{0,1,5}};packet.triangles={0,1,2};
    packet.triangle_references={{"source","original-face",{}}};
    document::ExtrusionParameters::EndTarget requested;requested.reference=packet.triangle_references.front();
    const auto curved=document::resolve_profile_target(requested,packet);
    check(curved && curved->kind==document::EndTargetKind::Face && curved->fallback_triangles.size()==3,
        "A coarse original face was guessed to be planar from its rendering");
    packet.triangle_references.front().surface=std::make_shared<kernel::SurfaceGeometry>();
    const auto plane=document::resolve_profile_target(requested,packet);
    check(plane && plane->kind==document::EndTargetKind::Plane && plane->fallback_triangles.empty(),"Persisted analytic plane was not resolved");
    requested.reference.semantic_key="plane";packet.triangle_references={requested.reference};
    check(document::resolve_profile_target(requested,packet)->kind==document::EndTargetKind::Plane,"Native datum plane requires no OCCT metadata");
}
void original_body_targets(const kernel::OcctKernel& kernel) {
    kernel::BoxRequest stock;stock.length=stock.width=stock.height=20;stock.translation={-10,-10,0};
    kernel::HistoryOperation source{"original-stock",stock,kernel::BooleanOperation::Add};source.body.id="source-body";
    const auto source_packet=kernel.evaluate_history({source}).back().mesh.original_references;
    const auto top=std::ranges::find_if(source_packet.triangle_references,[](const auto& ref) {
        return ref.surface && ref.surface->kind==kernel::SurfaceGeometry::Kind::Plane && std::abs(ref.surface->origin.z-20)<1e-8 && std::abs(ref.surface->axis.z)>.9;
    });check(top!=source_packet.triangle_references.end(),"Missing original source top");
    auto profile_ops=profile();
    auto feature=*std::ranges::find_if(profile_ops,[](const auto& op){return std::holds_alternative<kernel::ExtrusionRequest>(op.primitive);});
    auto& request=std::get<kernel::ExtrusionRequest>(feature.primitive);
    request.extent=kernel::ExtrusionRequest::Extent::UpToPlane;request.target_face=*top;request.target_is_datum=false;
    request.target_plane_origin={0,0,400};request.target_plane_normal={0,0,1};
    feature.body.id="profile-body";feature.body.translation={5,0,0};feature.body.rotation_degrees={0,90,0};
    source.body.translation={10,0,0};source.body.rotation_degrees={0,90,0};
    std::vector<kernel::HistoryOperation> operations{source,feature};
    auto calculated=kernel.evaluate_history(operations);near(calculated.back().volume,8000+625*std::numbers::pi);
    const auto first_key=calculated.back().body_boundaries.at("profile-body").back().source_fingerprint;
    operations.front().body.translation.x=12;
    calculated=kernel.evaluate_history_incremental(operations,calculated);near(calculated.back().volume,8000+675*std::numbers::pi);
    check(first_key!=calculated.back().body_boundaries.at("profile-body").back().source_fingerprint,"Source body placement reused an obsolete dependent result");
    operations.back().body.translation.x=7;
    calculated=kernel.evaluate_history_incremental(operations,calculated);near(calculated.back().volume,8000+625*std::numbers::pi);
    // A new process has only persisted boundaries, not the live original-face chain.
    kernel::OcctKernel cold;
    std::get<kernel::BoxRequest>(operations.front().primitive).height=22;
    calculated=cold.evaluate_history_incremental(operations,calculated);near(calculated.back().volume,8800+675*std::numbers::pi);
    operations.front().suppressed=true;bool rejected=false;
    try{static_cast<void>(cold.evaluate_history_incremental(operations,calculated));}catch(const std::exception&){rejected=true;}
    check(rejected,"A suppressed source reused an old original target");

    // Removing the visible top of a stock does not remove its original face.
    source.body={};feature.body={};
    kernel::BoxRequest trim=stock;trim.translation.z=10;
    kernel::HistoryOperation cut{"top-removal",trim,kernel::BooleanOperation::Subtract};
    operations={source,cut,feature};
    calculated=kernel.evaluate_history(operations);near(calculated.back().volume,4000+250*std::numbers::pi);
    std::get<kernel::BoxRequest>(operations[1].primitive).translation.z=8;
    calculated=kernel.evaluate_history_incremental(operations,calculated);near(calculated.back().volume,3200+300*std::numbers::pi);
    check(std::ranges::any_of(calculated.back().mesh.original_references.triangle_references,[&](const auto& ref){return ref==*top;}),"Original stock reference disappeared after a cut");
}

void curved_body_target(const kernel::OcctKernel& kernel) {
    kernel::SphereRequest sphere;sphere.radius=20;sphere.translation={0,0,30};
    kernel::HistoryOperation source{"sphere-target",sphere,kernel::BooleanOperation::Add};source.body.id="sphere-body";source.body.translation={0,0,10};
    const auto packet=kernel.evaluate_history({source}).back().mesh.original_references;
    auto profile_ops=profile();auto feature=*std::ranges::find_if(profile_ops,[](const auto& op){return std::holds_alternative<kernel::ExtrusionRequest>(op.primitive);});
    feature.body.id="curved-profile-body";feature.body.translation={0,0,5};
    auto& request=std::get<kernel::ExtrusionRequest>(feature.primitive);request.extent=kernel::ExtrusionRequest::Extent::UpToSurface;
    request.centerlines.origin_enabled=request.centerlines.centroid_enabled=true;
    request.target_face=packet.triangle_references.front();request.target_is_datum=false;
    for(std::size_t i=0;i<packet.triangle_references.size();++i)if(packet.triangle_references[i]==request.target_face)
        for(std::size_t j=0;j<3;++j){auto point=packet.vertices[packet.triangles[i*3+j]];point.z-=5;request.target_surface_triangles.push_back(point);}
    const double sphere_volume=4.0/3*std::numbers::pi*8000;
    const double profile_volume=25*std::numbers::pi*35-2*std::numbers::pi/3*(8000-std::pow(375,1.5));
    auto body=kernel.evaluate_history({source,feature}).back();near(body.volume,sphere_volume+profile_volume);
    std::set<std::string> automatic_endpoints;
    for(const auto& point:body.mesh.original_references.points) {
        if(point.reference.owner_id!=feature.owner_id)continue;
        if(point.reference.semantic_key.starts_with("profile:path-point:end:")) {
            near(point.position.z,20);automatic_endpoints.insert(point.reference.semantic_key);
        }
        if(point.reference.semantic_key.starts_with("profile:path-point:start:")) {
            near(point.position.z,5);automatic_endpoints.insert(point.reference.semantic_key);
        }
    }
    check(automatic_endpoints.size()==4,"Curved limit lost automatic origin/centroid endpoints");
    request.symmetric_limit=true;
    body=kernel.evaluate_history({source,feature}).back();near(body.volume,sphere_volume+2*profile_volume);
    check(faces(body).size()==4,"Curved extrusion lost the original sphere, two caps or profile side");
    const auto& original=body.mesh.original_references;std::set<std::string> caps;
    for(std::size_t i=0;i<original.triangle_references.size();++i) {
        const auto& ref=original.triangle_references[i];if(ref.owner_id!=feature.owner_id)continue;
        const bool end=ref.semantic_key.starts_with("end:from:"),start=ref.semantic_key.starts_with("start:from:");
        if(!end&&!start)continue;
        caps.insert(ref.semantic_key);
        for(std::size_t j=0;j<3;++j) {
            const auto point=original.vertices[original.triangles[i*3+j]];
            near(std::hypot(point.x,point.y,point.z-(end?40:-30)),20);
        }
    }
    check(caps.size()==2,"Mirrored sphere limits lost a cap identity");
}

}
int main(){try{kernel::OcctKernel kernel;limits(kernel);cuts(kernel);native_limits(kernel);coarse_target_classification();original_body_targets(kernel);curved_body_target(kernel);std::cout<<"Independent extrusion limits, inclined planes, Thin, identity and cache passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
