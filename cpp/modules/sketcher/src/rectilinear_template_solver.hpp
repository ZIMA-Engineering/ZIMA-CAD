#pragma once
#include <string>
#include <vector>
namespace zima::sketcher {
class Sketch;
// Solve supported rectilinear equations together, preserving distance branches.
// Failed dimension edits may opt into bounded true-distance linearization.
// Unsupported graphs remain untouched; the common solver verifies every seed.
bool seed_rectilinear_equations(Sketch&,const std::vector<std::string>& anchored_points,bool allow_nonlinear_distances=false,bool allow_constraint_only=false);
bool seed_equal_length_components(Sketch&,const std::vector<std::string>& anchored_points);
bool seed_circular_equations(Sketch&,const std::vector<std::string>& anchored_points,bool line_component=false);
}
