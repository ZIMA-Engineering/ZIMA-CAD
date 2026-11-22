#pragma once
#include "fem_volume_mesh.hpp"
#include <zima/document/engineering_metadata.hpp>

namespace zima::app {
struct FemThermalMaterial {
    double conductivity_W_mmK{};
    bool operator==(const FemThermalMaterial&) const = default;
};
struct FemThermalResult {
    std::vector<double> temperature_C;
    double minimum_C{},maximum_C{},input_W{},output_W{},relative_residual{};
};
[[nodiscard]] FemThermalMaterial fem_thermal_material(const document::MaterialData&);
[[nodiscard]] FemThermalResult solve_fem_steady_thermal(const FemVolumeMesh&,
    const document::FemDefinition&,FemThermalMaterial,const FemProgressCallback& progress = {});
}
