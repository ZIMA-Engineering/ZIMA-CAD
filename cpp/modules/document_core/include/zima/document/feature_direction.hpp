#pragma once
#include <zima/document/feature_parameters.hpp>
#include <cmath>

namespace zima::document {
inline constexpr const char* feature_direction_error="Feature direction must be +1 or -1.";
inline double feature_direction(const FeatureParameters& value) { return value.reversed_sides?-1.:1.; }
inline bool set_feature_direction(FeatureParameters& value,double direction) {
    if(direction!=1.&&direction!=-1.)return false;
    value.reversed_sides=direction<0;
    return true;
}
// A manual negative extent requests a reversal once. Relations author direction
// separately and deterministically through set_feature_direction instead.
inline bool set_feature_signed_length(FeatureParameters& value,std::size_t side,double length) {
    if(side>=value.sides.size()||!std::isfinite(length)||length==0)return false;
    value.sides[side].length=std::abs(length);
    if(length<0)value.reversed_sides=!value.reversed_sides;
    return true;
}
inline bool set_feature_signed_angle(FeatureParameters& value,std::size_t side,double angle) {
    if(side>=value.sides.size()||!std::isfinite(angle)||angle==0||std::abs(angle)>360)return false;
    value.sides[side].angle_degrees=std::abs(angle);
    if(angle<0)value.reversed_sides=!value.reversed_sides;
    return true;
}
}
