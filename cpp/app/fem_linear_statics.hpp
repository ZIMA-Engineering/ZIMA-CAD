#pragma once

#include "fem_volume_mesh.hpp"

namespace zima::app {
struct FemElasticMaterial {
    double young_MPa{}, poisson{};
    bool operator==(const FemElasticMaterial&) const = default;
};

struct FemStaticResult {
    std::vector<kernel::Vec3> displacement, reaction;
    double maximum_displacement_mm{};
};

[[nodiscard]] FemStaticResult solve_fem_linear_statics(const FemVolumeMesh&,
    const document::FemDefinition&, FemElasticMaterial);
[[nodiscard]] kernel::ViewerMesh fem_deformed_mesh(const FemVolumeMesh&,const FemStaticResult&);
}
