#pragma once
#include "transition_surface.hpp"
#include <zima/sketcher/sketch.hpp>

namespace zima::research::transition {
// Resolved numerical frame for the isolated experiment. A future history
// feature must obtain these frames from the existing shared placement solver.
// This class neither resolves references nor changes production placement.
struct Frame {
    Vec3 origin{},x{1,0,0},y{0,1,0},z{0,0,1};
    [[nodiscard]] Vec3 point(Vec3) const;
    [[nodiscard]] Vec3 direction(Vec3) const;
    [[nodiscard]] bool valid() const;
};
struct Profile {
    sketcher::Sketch sketch;
    std::string curve_id;
};
struct Model {
    Frame first_origin;
    Frame second_relative{{0,0,150}};
    std::array<Profile,2> profiles;
    Options options;
    [[nodiscard]] std::array<QuarterArc,2> world_arcs() const;
    [[nodiscard]] Result evaluate() const;
    [[nodiscard]] static Model example();
};
// Disposable presentation geometry; not persistent topology or reference owners.
[[nodiscard]] kernel::ViewerMesh mesh(const Result&,bool unfolded=false);
}
