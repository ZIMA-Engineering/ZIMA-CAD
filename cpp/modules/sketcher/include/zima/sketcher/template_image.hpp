#pragma once
#include <array>
#include <string>
#include <set>

namespace zima::sketcher {
// Embedded raster in template coordinates: X points left, Y points up.
struct TemplateImage {
    std::string id, name, data_base64;
    std::string format{"png"};
    double x{}, y{}, width{30}, height{20};
    double pixel_width{}, pixel_height{}; // Intrinsic raster size or SVG viewBox size.
    std::string horizontal{"left"}, vertical{"bottom"};
    bool lock_aspect{true};
    std::set<std::string> value_locks;
    void validate() const;
    // Image pixel order: top-left, top-right, bottom-right, bottom-left.
    [[nodiscard]] std::array<std::array<double,2>,4> corners() const;
    bool operator==(const TemplateImage&) const = default;
};
} // namespace zima::sketcher
