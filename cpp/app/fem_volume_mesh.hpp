#pragma once

#include <zima/document/fem_definition.hpp>
#include <zima/kernel/geometry_kernel.hpp>
#include <array>

namespace zima::app {
struct FemMeshConditionRegion {
    document::FemRegionKind kind;
    std::vector<std::uint32_t> nodes;
    // Gmsh ordering for a point, a linear/quadratic line or triangle. Only
    // the first 1, 2/3 or 3/6 entries respectively belong to the element.
    std::vector<std::array<std::uint32_t,6>> elements;
};

struct FemVolumeMesh {
    kernel::ViewerMesh surface;
    std::vector<std::array<std::uint32_t,10>> tetrahedra;
    document::FemElementOrder order;
    std::map<std::string,FemMeshConditionRegion> condition_regions;
};

[[nodiscard]] FemVolumeMesh generate_fem_volume_mesh(
    const kernel::BodyResult& source,const document::FemDefinition& definition);
}
