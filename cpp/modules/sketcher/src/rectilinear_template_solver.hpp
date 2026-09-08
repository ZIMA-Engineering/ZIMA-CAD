#pragma once
#include <string>
#include <vector>
namespace zima::sketcher {
class Sketch;
// Seed a fully rectilinear template graph exactly; the common solver still verifies it.
bool seed_rectilinear_template(Sketch&,const std::vector<std::string>& anchored_points);
}
