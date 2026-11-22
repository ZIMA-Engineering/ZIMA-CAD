#pragma once

#include "fem_volume_mesh.hpp"
#include <zima/document/engineering_metadata.hpp>
#include <span>

namespace zima::app {
struct FemElasticMaterial {
    double young_MPa{}, poisson{};
    bool operator==(const FemElasticMaterial&) const = default;
};

struct FemExpansionMaterial {
    double coefficient_per_K{},reference_C{};
    bool operator==(const FemExpansionMaterial&) const = default;
};
[[nodiscard]] FemExpansionMaterial fem_expansion_material(const document::MaterialData&);

// A borrowed nodal temperature field on the same mesh. The caller owns it
// throughout the solve. ThermalMechanical combines its strain with forces and
// pressure; pure thermal expansion uses mechanical restraints only.
struct FemThermalStrain {
    FemExpansionMaterial material;
    std::span<const double> temperature_C;
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
    const std::vector<kernel::Vec3>& displacement,FemElasticMaterial,const FemThermalStrain* thermal=nullptr);

[[nodiscard]] FemStaticResult solve_fem_linear_statics(const FemVolumeMesh&,
    const document::FemDefinition&, FemElasticMaterial, const FemProgressCallback& progress = {},
    const FemThermalStrain* thermal=nullptr);
[[nodiscard]] kernel::ViewerMesh fem_deformed_mesh(const FemVolumeMesh&,const FemStaticResult&);
}
