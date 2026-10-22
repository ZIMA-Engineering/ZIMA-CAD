#pragma once
#include <zima/sketcher/sketch.hpp>
#include <algorithm>

namespace zima::sketcher::detail {
// Certify identical rational bases, without fitting or sampling. Call once per
// candidate curve pair, outside all display-chord intersection loops.
inline bool coincident_exact_spline_support(const Sketch& sketch,
        const std::string& first,const std::string& second) {
    const auto external=[&](const auto& id)->const kernel::BSplineGeometry* {
        const auto it=std::ranges::find(sketch.external_references,id,&SketchExternalReference::id);
        return it!=sketch.external_references.end()&&!it->broken&&it->exact_spline?&*it->exact_spline:nullptr;
    };
    const auto a=external(first),b=external(second);
    if(a&&b)return *a==*b;
    if(!a&&!b)return false;
    const auto& native=a?second:first;
    if(std::ranges::find(sketch.bsplines,native,&SketchBSpline::id)==sketch.bsplines.end())return false;
    return sketch.supporting_curve(native)==*(a?a:b);
}
}
