#pragma once
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>

namespace zima::document {
inline void validate_sweep_tolerance(double value) {
    if (!std::isfinite(value) || value < 1e-9 || value > 1e6)
        throw std::invalid_argument("Sweep approximation tolerance must be between 0.000000001 and 1000000 mm.");
}
struct SweepPrecision {
    // Snapshot the configured default when creating the feature.
    double default_tolerance{0.001};
    std::optional<double> custom_tolerance;
    [[nodiscard]] double effective() const {
        validate_sweep_tolerance(default_tolerance);
        if (custom_tolerance) validate_sweep_tolerance(*custom_tolerance);
        return custom_tolerance.value_or(default_tolerance);
    }
    bool operator==(const SweepPrecision&) const = default;
};
struct SweepPrecisionDefaults {
    double sweep2d{0.001}, sweep3d{0.001}, helical{0.1};
    void validate() const {
        validate_sweep_tolerance(sweep2d);
        validate_sweep_tolerance(sweep3d);
        validate_sweep_tolerance(helical);
    }
};
inline double parse_sweep_tolerance(const std::string& text) {
    std::size_t consumed{};
    const double value=std::stod(text,&consumed);
    if(consumed!=text.size())throw std::invalid_argument("Invalid sweep tolerance configuration");
    validate_sweep_tolerance(value);
    return value;
}
}
