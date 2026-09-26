#pragma once
#include "transition_half.hpp"
namespace zima::research::transition {
struct SketchInputs {HalfModel model;std::array<std::string,2> arc_parents,corner_parents;std::array<std::string,3> straight_parents;};
[[nodiscard]] SketchInputs read_sketches(const sketcher::Sketch& round,const sketcher::Sketch& rectangle);
struct RectangularSketchInputs {RectangularModel model;std::vector<std::string> parents;};
[[nodiscard]] RectangularSketchInputs read_rectangular_sketches(const sketcher::Sketch& first,const sketcher::Sketch& second);
}
