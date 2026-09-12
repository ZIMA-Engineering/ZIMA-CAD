#include <zima/document/part_document.hpp>
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
std::vector<kernel::HistoryOperation> profile() {
    auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_circle(0,0,5));
    auto feature=document::PartDocument::create_extrusion_container(sketch.id);sketch.owner_container_id=feature.id;
    auto part=document::PartDocument::create_default();part.history={feature};part.sketches={sketch};
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Limits"));graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
    return part.kernel_operations();
}
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
    request.reverse_limit->reference.semantic_key="missing-native-face";bool rejected=false;
    try{static_cast<void>(kernel.evaluate_history(operations));}catch(const std::exception&){rejected=true;}
    check(rejected,"Missing original reverse surface was accepted");
}

}
int main(){try{kernel::OcctKernel kernel;limits(kernel);cuts(kernel);std::cout<<"Independent extrusion limits, inclined planes, Thin, identity and cache passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
