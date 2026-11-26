#pragma once
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace zima::document {
// Calibrate from one measured bend. Canonical units are mm and degrees.
// L = |angle| * (inside_radius + K * thickness); radius cancels in delta L.
inline double calibrated_k_factor(double current,double thickness_mm,double angle_degrees,double correction_mm) {
    if(!std::isfinite(current)||current<0||current>1)
        throw std::invalid_argument("K factor must be a number from 0 to 1.");
    if(!std::isfinite(thickness_mm)||thickness_mm<=0||!std::isfinite(angle_degrees)||std::abs(angle_degrees)<1e-9)
        throw std::invalid_argument("Calibration requires a positive sheet thickness and a nonzero bend angle.");
    if(!std::isfinite(correction_mm)||std::abs(correction_mm)>1.)
        throw std::invalid_argument("Correction must be between -1 mm and 1 mm.");
    const double next=current+correction_mm/(thickness_mm*std::abs(angle_degrees)*std::numbers::pi/180.);
    if(!std::isfinite(next)||next<0||next>1)
        throw std::invalid_argument("The requested correction would put the K factor outside 0 to 1.");
    return next;
}
}
