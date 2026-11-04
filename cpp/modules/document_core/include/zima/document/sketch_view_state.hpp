#pragma once
#include <map>
#include <string>

namespace zima::document {
// Relative camera presentation; never part of a Sketch's modeling frame.
struct SketchViewState {
    bool back{};
    int sector{}; // 0, 1, 2, 3 correspond to 0, 90, 180, 270 degrees.
    bool operator==(const SketchViewState&) const = default;
};
using SketchViewStates = std::map<std::string, SketchViewState>;
[[nodiscard]] SketchViewStates parse_sketch_views(const std::string&);
[[nodiscard]] std::string serialize_sketch_views(const SketchViewStates&);
}
