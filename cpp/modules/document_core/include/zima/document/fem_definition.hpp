#pragma once
#include <zima/document/fem_condition.hpp>
#include <vector>

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace zima::document {

enum class FemAnalysisKind { LinearElasticStatic, SteadyStateThermal, ThermalMechanical };

inline bool fem_thermal_analysis(FemAnalysisKind kind) {
    return kind==FemAnalysisKind::SteadyStateThermal||kind==FemAnalysisKind::ThermalMechanical;
}

inline bool fem_condition_applies_to_analysis(FemConditionKind kind,FemAnalysisKind analysis,bool expansion=false) {
    if(fem_measurement(kind))return false;
    if(analysis==FemAnalysisKind::ThermalMechanical)return true;
    return analysis==FemAnalysisKind::SteadyStateThermal?
        (kind==FemConditionKind::HeatSource||(expansion&&kind==FemConditionKind::Constraint)):kind!=FemConditionKind::HeatSource;
}
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

struct FemThermalSettings {
    double ambient_C{20};
    // Explicit convection on the complete exterior, including loaded faces.
    double convection_W_m2K{10};
    bool expansion{false};
    bool operator==(const FemThermalSettings&) const = default;
};

// The source is an earlier material Body/Boolean in the same Part. Geometry,
// material and CAD history belong to that source, never to this definition.
struct FemDefinition {
    std::string source_id;
    FemAnalysisKind analysis{FemAnalysisKind::LinearElasticStatic};
    FemMeshControls mesh;
    std::vector<FemCondition> conditions;
    std::optional<FemThermalSettings> thermal;
    bool operator==(const FemDefinition&) const = default;
};

inline bool fem_thermal_expansion(const FemDefinition& value) {
    return value.thermal&&(value.analysis==FemAnalysisKind::ThermalMechanical||
        (value.analysis==FemAnalysisKind::SteadyStateThermal&&value.thermal->expansion));
}

void validate_fem_definition(const FemDefinition&);
void refresh_fem_distances(FemDefinition&);
void to_json(nlohmann::json&, const FemDefinition&);
void from_json(const nlohmann::json&, FemDefinition&);

} // namespace zima::document
