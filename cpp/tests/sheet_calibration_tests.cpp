#include <zima/document/part_document.hpp>
#include <zima/document/sheet_calibration.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <iostream>
#include <limits>
using namespace zima;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(){try {
    kernel::OcctKernel engine;
    for(double angle:{30.,90.,135.})for(double delta:{-.2,0.,.5}) {
        auto part=document::PartDocument::create_default();
        auto bend=document::PartDocument::create_sketch_container();bend.feature_kind=document::FeatureKind::Bend;
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=bend.id;static_cast<void>(sketch.add_segment(0,0,40,0));
        bend.bend.sketch_id=sketch.id;bend.bend.thickness_override=true;bend.bend.thickness=2.;bend.bend.k_factor_override=true;bend.bend.k_factor=.4;
        bend.bend.radius_follows_thickness=false;bend.bend.radius=8.;bend.bend.angle_degrees=angle;
        part.history={bend};part.sketches={sketch};part.resolve_constructions();
        const auto flat=[&] {
            auto operations=part.kernel_operations();kernel::HistoryOperation unbend;unbend.owner_id="calibration-flat";unbend.primitive=kernel::SheetStateRequest{};operations.push_back(unbend);
            return engine.evaluate_history(operations).back();
        };
        const auto before=flat();
        part.history.front().bend.k_factor=document::calibrated_k_factor(.4,2.,angle,delta);
        const auto after=flat();
        // Exact solid volume / unchanged width and thickness measures the
        // developed length independently of the calibration formula.
        require(std::abs((after.volume-before.volume)/80.-delta)<1e-7,"Calibrated solid developed length is incorrect");
        require(after.mesh.original_references.edges.size()==before.mesh.original_references.edges.size(),"Calibration changed boundary topology");
        const auto restored=document::load_body_result(document::serialize_body_result(after));
        require(restored.volume==after.volume,"Native result changed the calibrated solid volume");
        for(const auto& edge:before.mesh.original_references.edges)
            require(std::ranges::any_of(after.mesh.original_references.edges,[&](const auto& other){return other.reference==edge.reference;}),"Calibration lost an exact edge identity");
        std::cout<<"angle="<<angle<<" correction="<<delta<<" measured="<<(after.volume-before.volume)/80.<<'\n';
    }
    for(const auto& input:{std::array{.4,0.,90.,.1},std::array{.4,2.,0.,.1},std::array{.4,2.,90.,1.1},std::array{.4,.1,10.,1.},std::array{.4,2.,90.,std::numeric_limits<double>::quiet_NaN()}}) {
        bool rejected=false;try{static_cast<void>(document::calibrated_k_factor(input[0],input[1],input[2],input[3]));}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"Invalid calibration was accepted");
    }
    std::cout<<"Single-bend K calibration and exact unfolded solids passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
