#pragma once
#include <zima/kernel/geometry_kernel.hpp>

namespace zima::kernel {
// Transient explicit calculation request. The caller supplies an already
// calculated source and the actual resolved Sketch plane in the same frame.
struct FacePlaneIntersectionRequest {
    FaceReference face;
    std::string plane_owner_id;
    Vec3 origin;
    Vec3 normal{0,0,1};
    double tolerance{0.001};
};
}
