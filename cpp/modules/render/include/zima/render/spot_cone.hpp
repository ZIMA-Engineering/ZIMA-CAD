#pragma once
#include <algorithm>
#include <cmath>
#include <numbers>

namespace zima::render {
// Prepared once from validated Render parameters. Smoothstep is integrated in
// cos(theta), so total emitted flux equals the authored luminous power even
// when the cone angle or its soft rim changes.
struct SpotCone {
    double outer_cos{},inner_cos{},solid_angle{};
    SpotCone(double full_angle_degrees,double softness) {
        const double outer=full_angle_degrees*std::numbers::pi/360;
        const double inner=outer*(1-softness);
        outer_cos=std::cos(outer);inner_cos=std::cos(inner);
        const double a=std::sin(outer/2),b=std::sin(inner/2);
        solid_angle=2*std::numbers::pi*(a*a+b*b);
    }
    double attenuation(double cosine) const {
        if(cosine<outer_cos)return 0;
        if(cosine>=inner_cos)return 1;
        const double t=std::clamp((cosine-outer_cos)/(inner_cos-outer_cos),0.,1.);
        return t*t*(3-2*t);
    }
};
}
