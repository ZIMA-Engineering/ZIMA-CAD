#pragma once
#include <zima/document/profile_parameters.hpp>

namespace zima::document {
enum class WrappingOutput { Curves, Surface, Relief };
struct WrappingParameters {
    WrappingOutput output{WrappingOutput::Curves};
    std::optional<ExtrusionParameters::EndTarget> target;
    double height{1};
    bool reverse_u{},reverse_v{};
    bool operator==(const WrappingParameters&) const = default;
};
void validate_wrapping_parameters(const WrappingParameters&);
[[nodiscard]] nlohmann::json serialize_wrapping_parameters(const WrappingParameters&);
[[nodiscard]] WrappingParameters load_wrapping_parameters(const nlohmann::json&);
}
