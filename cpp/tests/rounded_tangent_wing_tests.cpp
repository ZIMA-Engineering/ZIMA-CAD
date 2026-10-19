#include "rounded_tangent_wing_study.hpp"
#include "sloping_line_incidence_study.hpp"
#include "angled_triangle_study.hpp"
int main() {
    try { run_angled_triangle_study();rounded_tangent_wing_study::run();run_sloping_line_incidence_study();return 0; }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
