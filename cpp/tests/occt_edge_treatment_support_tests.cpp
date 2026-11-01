#include "profile_request_fixture.hpp"

#include <zima/kernel/occt_kernel.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using namespace zima;

void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

std::vector<kernel::EdgeReference> selectable_edges(
        const kernel::BodyResult& body) {
    std::vector<kernel::EdgeReference> result;
    for(const auto& edge:body.mesh.edges)
        if(!edge.parameter_seam&&edge.reference.valid()&&
           std::ranges::find(result,edge.reference)==result.end())
            result.push_back(edge.reference);
    return result;
}

void verify_box(kernel::OcctKernel& occt) {
    test::ProfilePrism request{20,30,40};
    request.translation={4,-7,3};
    request.rotation_degrees={13,21,37};
    const auto body=occt.evaluate_history({{"box",request}}).back();
    const auto references=selectable_edges(body);
    require(references.size()==12,
        "Box fixture did not publish twelve selectable edges");
    const auto inspected=occt.inspect_edge_treatment_supports(
        body,references);
    require(inspected.segments.size()==references.size(),
        "Box support inspection omitted an edge");
    for(std::size_t index=0;index<inspected.segments.size();++index) {
        const auto& segment=inspected.segments[index];
        require(segment.edge==references[index],
            "Support inspection changed persisted edge order or identity");
        require(segment.curve_kind==
                kernel::OcctEdgeTreatmentCurveKind::Line,
            "A box edge was not classified as a line");
        require(segment.adjacent_faces.size()==2,
            "A box edge was not classified as manifold");
        require(std::ranges::all_of(segment.adjacent_faces,
                [](const auto& face) {
                    return face.kind==
                        kernel::OcctEdgeTreatmentSurfaceKind::Plane&&
                        !face.references.empty()&&
                        std::ranges::all_of(face.references,
                            &kernel::FaceReference::valid);
                }),
            "A box edge lost planar stable support ancestry");
        require(segment.continuity_order==0,
            "A sharp box edge was classified as smooth");
        require(segment.outward_normal_angle_degrees&&
                std::abs(*segment.outward_normal_angle_degrees-90.)<1.0e-7,
            "A box edge has an incorrect oriented support angle");
    }
}

void verify_cylinder(kernel::OcctKernel& occt) {
    const auto body=occt.evaluate_history(
        {{"cylinder",test::CircularExtrusion{12,35}}}).back();
    const auto references=selectable_edges(body);
    require(references.size()==2,
        "Cylinder fixture did not publish two selectable rim edges");
    const auto inspected=occt.inspect_edge_treatment_supports(
        body,references);
    require(inspected.segments.size()==2,
        "Cylinder support inspection omitted a rim");
    for(const auto& segment:inspected.segments) {
        require(segment.curve_kind==
                kernel::OcctEdgeTreatmentCurveKind::Circle,
            "A cylinder rim was not classified as a circle");
        require(segment.adjacent_faces.size()==2,
            "A cylinder rim was not classified as manifold");
        std::vector<kernel::OcctEdgeTreatmentSurfaceKind> kinds;
        for(const auto& face:segment.adjacent_faces) {
            require(!face.references.empty(),
                "A cylinder support lost stable ancestry");
            kinds.push_back(face.kind);
        }
        std::ranges::sort(kinds);
        require(kinds==std::vector{
                kernel::OcctEdgeTreatmentSurfaceKind::Plane,
                kernel::OcctEdgeTreatmentSurfaceKind::Cylinder},
            "A cylinder rim has incorrect support kinds");
        require(segment.continuity_order==0&&
                segment.outward_normal_angle_degrees&&
                std::abs(*segment.outward_normal_angle_degrees-90.)<1.0e-7,
            "A cylinder rim has incorrect sharp support geometry");
    }
}

void verify_rejection(kernel::OcctKernel& occt) {
    const auto body=occt.evaluate_history(
        {{"box",test::ProfilePrism{10,10,10}}}).back();
    const auto references=selectable_edges(body);
    bool missing_rejected=false;
    try {
        const std::array missing{
            kernel::EdgeReference{"missing","edge",{}}};
        static_cast<void>(occt.inspect_edge_treatment_supports(
            body,missing));
    } catch(const std::runtime_error&) {
        missing_rejected=true;
    }
    require(missing_rejected,
        "Support inspection accepted a missing persisted edge");
    bool duplicate_rejected=false;
    try {
        const std::array duplicate{references.front(),references.front()};
        static_cast<void>(occt.inspect_edge_treatment_supports(
            body,duplicate));
    } catch(const std::invalid_argument&) {
        duplicate_rejected=true;
    }
    require(duplicate_rejected,
        "Support inspection accepted a duplicate persisted edge");
}

} // namespace

int main() try {
    zima::kernel::OcctKernel occt;
    verify_box(occt);
    verify_cylinder(occt);
    verify_rejection(occt);
    std::cout<<"OCCT edge treatment support inspection: rotated plane/plane, plane/cylinder, stable ancestry and rejection checks passed\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
}
