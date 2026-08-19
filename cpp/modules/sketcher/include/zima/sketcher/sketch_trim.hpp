#pragma once

#include <zima/sketcher/sketch.hpp>

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace zima::sketcher {

struct SketchTrimPiece {
    std::string geometry_id;
    double start{};
    double end{};
    std::vector<std::array<double, 2>> points;
    bool closed{};

    bool operator==(const SketchTrimPiece&) const = default;
};

struct SketchTrimResult {
    std::map<std::string, std::vector<std::string>> geometry_mapping;
};

[[nodiscard]] std::vector<SketchTrimPiece> sketch_trim_topology(
    const Sketch& sketch, bool include_base_axes = true);

[[nodiscard]] std::optional<SketchTrimPiece> nearest_sketch_trim_piece(
    const std::vector<SketchTrimPiece>& pieces,
    const std::array<double, 2>& position,
    double tolerance);

[[nodiscard]] std::vector<SketchTrimPiece> sketch_trim_pieces_crossed_by_path(
    const std::vector<SketchTrimPiece>& pieces,
    const std::vector<std::array<double, 2>>& path,
    double tolerance);

[[nodiscard]] SketchTrimResult apply_sketch_trim(
    Sketch& sketch, const std::vector<SketchTrimPiece>& removed,
    double snap_tolerance = 1.0e-7);

}  // namespace zima::sketcher
