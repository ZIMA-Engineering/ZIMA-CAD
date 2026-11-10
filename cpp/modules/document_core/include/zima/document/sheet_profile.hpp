#pragma once

#include <zima/sketcher/sketch.hpp>

#include <array>
#include <string>
#include <vector>

namespace zima::document {

struct SheetProfileParameters;

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

// Seeds the owned route of a newly edge-attached Profile. The first circular
// span represents the Bend outer fibre (2T, hence the ordinary child Bend is
// R=T) and the following line owns the same native junction point plus T.
// Existing authored geometry is never replaced.
[[nodiscard]] std::string initialize_attached_sheet_profile(
    zima::sketcher::Sketch&, double thickness);

// Profile width intentionally consumes the ordinary Feature two-side model,
// but only finite linear extrusion extents are meaningful for the common
// width shared by all generated Flat/Bend children.
void validate_sheet_profile_width(const SheetProfileParameters&);

} // namespace zima::document
