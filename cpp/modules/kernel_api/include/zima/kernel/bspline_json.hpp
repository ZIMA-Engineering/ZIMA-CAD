#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <nlohmann/json.hpp>

namespace zima::kernel {
inline nlohmann::json spline_json(const std::optional<BSplineGeometry>& curve) {
    if (!curve) return nullptr;
    curve->validate();
    auto poles = nlohmann::json::array();
    for (const auto& p : curve->poles) poles.push_back({p.x, p.y, p.z});
    return {{"degree",curve->degree},{"poles",std::move(poles)},
        {"knots",curve->knots},{"weights",curve->weights}};
}
inline std::optional<BSplineGeometry> spline_from_json(const nlohmann::json& value) {
    if (value.is_null()) return {};
    BSplineGeometry curve;
    curve.degree=value.at("degree").get<unsigned>();
    curve.knots=value.at("knots").get<std::vector<double>>();
    curve.weights=value.at("weights").get<std::vector<double>>();
    for (const auto& p : value.at("poles"))
        curve.poles.push_back({p.at(0).get<double>(),p.at(1).get<double>(),p.at(2).get<double>()});
    curve.validate(); return curve;
}
}
