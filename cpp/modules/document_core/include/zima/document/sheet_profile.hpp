#pragma once

#include <zima/sketcher/sketch.hpp>

#include <array>
#include <string>
#include <vector>

namespace zima::document {

enum class SheetProfileSpanKind { Flat, Bend };

struct SheetProfileSpan {
    SheetProfileSpanKind kind{SheetProfileSpanKind::Flat};
    std::string curve_id;
    std::string start_point_id;
    std::string end_point_id;
    bool reversed{};
    std::array<double,2> start{};
    std::array<double,2> end{};
    std::array<double,2> start_tangent{};
    std::array<double,2> end_tangent{};
    std::array<double,2> center{};
    double length{};
    double radius{};
    // Signed turn from the incoming tangent to the outgoing tangent. The
    // ordinary Bend owns the positive magnitude; its attachment side carries
    // the sign without inventing a second bend algorithm.
    double turn_degrees{};
    bool operator==(const SheetProfileSpan&) const = default;
};

struct SheetProfileRoute {
    zima::sketcher::Sketch evaluated;
    std::string start_point_id;
    std::string end_point_id;
    std::vector<SheetProfileSpan> spans;
};

// Accepts exactly one open connected chain of authored straight segments and
// circular arcs. Corner radii materialized by Sketch evaluation retain their
// persisted radius-record IDs and participate as ordinary Bend spans.
[[nodiscard]] SheetProfileRoute sheet_profile_route(
    const zima::sketcher::Sketch&, const std::string& start_point_id = {});

} // namespace zima::document
