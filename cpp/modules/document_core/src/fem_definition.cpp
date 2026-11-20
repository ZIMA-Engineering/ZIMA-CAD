#include <zima/document/fem_definition.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace zima::document {
void validate_fem_definition(const FemDefinition& value) {
    if(value.source_id.empty())
        throw std::invalid_argument("FEM requires a source Body or Boolean result.");
    if(value.analysis!=FemAnalysisKind::LinearElasticStatic)
        throw std::invalid_argument("Unsupported FEM analysis type.");
    const auto& mesh=value.mesh;
    if(!std::isfinite(mesh.minimum_size_mm)||!std::isfinite(mesh.maximum_size_mm)||
            mesh.minimum_size_mm<=0||mesh.maximum_size_mm<mesh.minimum_size_mm||
            mesh.elements_through_thickness==0||
            (mesh.order!=FemElementOrder::Linear&&mesh.order!=FemElementOrder::Quadratic))
        throw std::invalid_argument("Invalid FEM volume mesh controls.");
}

void to_json(nlohmann::json& json,const FemDefinition& value) {
    validate_fem_definition(value);
    json={{"source",value.source_id},{"analysis","linear-elastic-static"},
        {"mesh",{{"minimum_size_mm",value.mesh.minimum_size_mm},
            {"maximum_size_mm",value.mesh.maximum_size_mm},
            {"elements_through_thickness",value.mesh.elements_through_thickness},
            {"order",static_cast<int>(value.mesh.order)}}}};
}

void from_json(const nlohmann::json& json,FemDefinition& value) {
    FemDefinition next;
    next.source_id=json.at("source").get<std::string>();
    if(json.at("analysis").get<std::string>()!="linear-elastic-static")
        throw std::invalid_argument("Unsupported FEM analysis type.");
    const auto& mesh=json.at("mesh");
    next.mesh.minimum_size_mm=mesh.at("minimum_size_mm").get<double>();
    next.mesh.maximum_size_mm=mesh.at("maximum_size_mm").get<double>();
    const auto& layers=mesh.at("elements_through_thickness");
    // Reject negative/fractional/overflowed integers before narrowing; JSON's
    // numeric conversion otherwise silently wraps or truncates these values.
    if(!layers.is_number_integer()||layers.get<double>()<1||
            layers.get<double>()>std::numeric_limits<unsigned>::max())
        throw std::invalid_argument("Invalid FEM volume mesh controls.");
    next.mesh.elements_through_thickness=layers.get<unsigned>();
    const auto& order=mesh.at("order");
    if(!order.is_number_integer()||(order!=1&&order!=2))
        throw std::invalid_argument("Invalid FEM volume mesh controls.");
    next.mesh.order=static_cast<FemElementOrder>(order.get<int>());
    validate_fem_definition(next);
    value=std::move(next);
}
} // namespace zima::document
