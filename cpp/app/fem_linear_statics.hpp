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
    // Volume-averaged Cauchy stress per tetrahedron; no nodal smoothing.
    std::vector<double> element_von_mises_MPa;
    double maximum_von_mises_MPa{};
};

struct FemElementStress {
    // xx, yy, zz, xy, yz, zx (physical shear stress, in MPa).
    std::array<double,6> components_MPa{};
    double von_mises_MPa{};
};
[[nodiscard]] std::vector<FemElementStress> fem_element_stresses(const FemVolumeMesh&,
    const std::vector<kernel::Vec3>& displacement,FemElasticMaterial);

[[nodiscard]] FemStaticResult solve_fem_linear_statics(const FemVolumeMesh&,
    const document::FemDefinition&, FemElasticMaterial, const FemProgressCallback& progress = {});
[[nodiscard]] kernel::ViewerMesh fem_deformed_mesh(const FemVolumeMesh&,const FemStaticResult&);
}
