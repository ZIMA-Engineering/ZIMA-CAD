#pragma once
#include <zima/sketcher/sketch.hpp>
namespace zima::sketcher {
[[nodiscard]] bool has_vector_profiles(const Sketch&);
// Private calculation input only. Authored text/images and their native IDs
// remain single objects in the document, Tree and View.
[[nodiscard]] Sketch expanded_vector_profiles(const Sketch&);
void append_exact_profile(Sketch&,const std::string& source_id,
    const std::vector<kernel::BSplineGeometry>& boundaries);
}
