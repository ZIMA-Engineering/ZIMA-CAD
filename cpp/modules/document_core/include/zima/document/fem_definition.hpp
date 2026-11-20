#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace zima::document {

enum class FemAnalysisKind { LinearElasticStatic };
enum class FemElementOrder { Linear = 1, Quadratic = 2 };

// All lengths use the native model's millimetres. These are explicit mesher
// inputs, not display tessellation controls. Sheet metal remains a volume.
struct FemMeshControls {
    double minimum_size_mm{0.1};
    double maximum_size_mm{5.0};
    unsigned elements_through_thickness{3};
    FemElementOrder order{FemElementOrder::Quadratic};
    bool operator==(const FemMeshControls&) const = default;
};

// The source is an earlier material Body/Boolean in the same Part. Geometry,
// material and CAD history belong to that source, never to this definition.
struct FemDefinition {
    std::string source_id;
    FemAnalysisKind analysis{FemAnalysisKind::LinearElasticStatic};
    FemMeshControls mesh;
    bool operator==(const FemDefinition&) const = default;
};

void validate_fem_definition(const FemDefinition&);
void to_json(nlohmann::json&, const FemDefinition&);
void from_json(const nlohmann::json&, FemDefinition&);

} // namespace zima::document
