#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <zima/kernel/dimension_layout.hpp>
namespace zima::test {
struct CircularExtrusion {
    double radius{40},height{50};kernel::Vec3 translation,rotation_degrees;
    CircularExtrusion()=default;
    CircularExtrusion(double r,double h):radius(r),height(h){}
    operator kernel::ExtrusionRequest() const {
        kernel::ExtrusionRequest request;const auto frame=kernel::annotation_frame(translation,rotation_degrees);
        request.outer_profile=kernel::ExtrusionRequest::CircleProfile{translation,radius};
        const auto end=frame.world({0,0,height});
        request.direction={end.x-translation.x,end.y-translation.y,end.z-translation.z};
        request.profile_region_id="circle-region";request.outer_boundary_id="circle-boundary";
        request.outer_edge_source_ids={"circle"};return request;
    }
};
struct SphericalRevolution {
    double radius{40};kernel::Vec3 translation;
    SphericalRevolution()=default;
    SphericalRevolution(double r,kernel::Vec3 t={}):radius(r),translation(t){}
    operator kernel::RevolutionRequest() const {
        using R=kernel::ExtrusionRequest;kernel::RevolutionRequest request;
        const auto a=kernel::Vec3{translation.x,translation.y,translation.z-radius};
        const auto b=kernel::Vec3{translation.x+radius,translation.y,translation.z};
        const auto c=kernel::Vec3{translation.x,translation.y,translation.z+radius};
        request.outer_profile=R::CurvedProfile{{R::ArcCurve{a,b,c},R::LineCurve{c,a}}};
        request.axis_point=translation;request.axis_direction={0,0,1};request.profile_normal={0,-1,0};
        request.profile_region_id="semicircle";request.outer_boundary_id="semicircle";
        request.outer_edge_source_ids={"arc","diameter"};request.outer_vertex_source_ids={"south","north"};return request;
    }
};
inline kernel::ExtrusionRequest rectangular_request(double x=100,double y=80,double z=50) {
    kernel::ExtrusionRequest request;
    request.outer_profile=kernel::ExtrusionRequest::PolygonProfile{{{0,0,0},{x,0,0},{x,y,0},{0,y,0}}};
    request.direction={0,0,z};request.profile_region_id="region";request.outer_boundary_id="boundary";
    request.outer_edge_source_ids={"bottom","right","top","left"};
    request.outer_vertex_source_ids={"lower-left","lower-right","upper-right","upper-left"};
    return request;
}
// A parameterized test operand. Conversion always builds an ordinary native
// Extrusion request, with Sketch-parent topology identities, never a primitive.
struct ProfilePrism {
    double length{100},width{80},height{50};
    kernel::Vec3 translation,rotation_degrees;
    ProfilePrism()=default;
    ProfilePrism(double x,double y,double z):length(x),width(y),height(z){}
    operator kernel::ExtrusionRequest() const {
        auto request=rectangular_request(length,width,height);
        const auto frame=kernel::annotation_frame(translation,rotation_degrees);
        for(auto& point:std::get<kernel::ExtrusionRequest::PolygonProfile>(request.outer_profile).vertices)
            point=frame.world(point);
        const auto end=frame.world(request.direction);
        request.direction={end.x-translation.x,end.y-translation.y,end.z-translation.z};
        return request;
    }
};
template<class Kernel>
inline kernel::BodyResult profile_body(const Kernel& kernel,
        const ProfilePrism& profile) {
    return kernel.evaluate_history({{"box",static_cast<kernel::ExtrusionRequest>(profile)}}).back();
}
template<class Kernel>
inline kernel::BodyResult profile_history(const Kernel& kernel,
        std::vector<kernel::HistoryOperation> operations) {
    const auto result=kernel.evaluate_history(operations);
    return result.empty()?kernel::BodyResult{}:result.back();
}
template<class Kernel>
inline kernel::BodyResult rectangular_body(const Kernel& kernel,
        double x=100,double y=80,double z=50) {
    return kernel.evaluate_history({{"fixture",rectangular_request(x,y,z)}}).back();
}
} // namespace zima::test
