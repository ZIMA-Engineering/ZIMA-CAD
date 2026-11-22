#pragma once
#include "fem_volume_mesh.hpp"
#include <optional>

namespace zima::app {
struct FemProbeLocation {
    std::size_t element{};
    std::array<double,10> weights{};
};
[[nodiscard]] std::optional<FemProbeLocation> locate_fem_point(const FemVolumeMesh&,kernel::Vec3);
[[nodiscard]] kernel::Vec3 fem_probe_displacement(const FemVolumeMesh&,const FemProbeLocation&,
    const std::vector<kernel::Vec3>&);
[[nodiscard]] double fem_probe_scalar(const FemVolumeMesh&,const FemProbeLocation&,const std::vector<double>&);
}
