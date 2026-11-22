#pragma once
#include "fem_volume_mesh.hpp"
#include <optional>

namespace zima::app {
struct FemProbeLocation {
    std::size_t element{};
    std::array<double,10> weights{};
};
struct FemDistanceResult {double original_mm{},deformed_mm{},change_mm{};};
[[nodiscard]] FemDistanceResult fem_distance_result(kernel::Vec3 first,kernel::Vec3 second,
    kernel::Vec3 first_displacement,kernel::Vec3 second_displacement);
[[nodiscard]] std::optional<FemProbeLocation> locate_fem_point(const FemVolumeMesh&,kernel::Vec3);
[[nodiscard]] kernel::Vec3 fem_probe_displacement(const FemVolumeMesh&,const FemProbeLocation&,
    const std::vector<kernel::Vec3>&);
[[nodiscard]] double fem_probe_scalar(const FemVolumeMesh&,const FemProbeLocation&,const std::vector<double>&);
}
