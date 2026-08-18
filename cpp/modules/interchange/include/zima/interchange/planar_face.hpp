#pragma once

#include <zima/sketcher/sketch.hpp>

#include <array>
#include <string>
#include <variant>
#include <vector>

namespace zima::interchange {

struct PlanarLine { std::array<double, 2> first; std::array<double, 2> second; };
struct PlanarCircle { std::array<double, 2> center; double radius{}; };
struct PlanarArc {
    std::array<double, 2> center;
    std::array<double, 2> start;
    std::array<double, 2> end;
};
struct PlanarEllipse {
    std::array<double, 2> center;
    std::array<double, 2> major_point;
    std::array<double, 2> minor_point;
};
struct PlanarBSpline {
    std::vector<std::array<double, 2>> control_points;
    unsigned degree{3};
    bool closed{};
};
using PlanarCurve = std::variant<
    PlanarLine, PlanarCircle, PlanarArc, PlanarEllipse, PlanarBSpline>;

struct PlanarFaceProfile {
    std::string source_name;
    std::string source_face_key;
    std::vector<PlanarCurve> curves;
};

[[nodiscard]] std::string append_planar_face_as_sketch_block(
    const PlanarFaceProfile& profile, zima::sketcher::Sketch& target);

}  // namespace zima::interchange
