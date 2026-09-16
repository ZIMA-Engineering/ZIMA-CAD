#pragma once
#include <string>
#include <vector>
namespace zima::sketcher {
class Sketch;
// Solve supported rectilinear equations together, preserving distance branches.
// Unsupported graphs remain untouched; the common solver verifies every seed.
bool seed_rectilinear_equations(Sketch&,const std::vector<std::string>& anchored_points);
}
