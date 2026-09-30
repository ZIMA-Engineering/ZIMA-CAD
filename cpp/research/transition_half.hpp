#pragma once
#include "transition_model.hpp"

namespace zima::research::transition {
// Explicit zero-thickness study parameters, not a production placement solver.
struct HalfModel {
    Frame first_origin;
    Frame second_relative{{0,0,150}};
    double radius{80},width{200},depth{160},corner_radius{20};
    std::array<std::size_t,2> corner_facets{4,4};
};
struct RectangularModel {
    Frame first_origin;
    Frame second_relative{{0,0,150}};
    std::array<double,2> width{200,140},depth{160,100};
    unsigned sides{3}; // Two adjacent walls (L), or three walls (U).
};
struct HalfFace {std::vector<Vec3> folded,unfolded;Vec3 normal;bool corner{};};
struct HalfResult {
    Failure failure{Failure::None};
    std::vector<HalfFace> faces;
    std::vector<Fold> folds; // Includes zero-angle panel boundaries.
    std::array<Deviation,2> boundary_deviation{};
    double maximum_metric_error{},maximum_planarity_error{},folded_area{},unfolded_area{};
    [[nodiscard]] bool valid()const{return failure==Failure::None;}
};
[[nodiscard]] HalfResult calculate(const HalfModel&);
[[nodiscard]] HalfResult calculate(const RectangularModel&);
[[nodiscard]] kernel::ViewerMesh mesh(const HalfResult&,bool unfolded=false);
}
