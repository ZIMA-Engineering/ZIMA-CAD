#pragma once

#include <zima/kernel/geometry_kernel.hpp>

namespace zima::kernel {
[[nodiscard]] std::vector<std::string> validate_projected_mesh_region(
    const BodyResult& source,const ProjectionRequest& request,const std::vector<FaceReference>& references,
    const std::vector<Vec3>& surface_seeds,const std::vector<std::string>& expected_region_keys);
}
