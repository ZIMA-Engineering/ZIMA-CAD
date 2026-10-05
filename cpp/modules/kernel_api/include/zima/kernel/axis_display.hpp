#pragma once
#include <zima/kernel/curve_evaluation.hpp>
#include <zima/kernel/dimension_layout.hpp>
#include <zima/kernel/solid_state_ancestry.hpp>
#include <limits>

namespace zima::kernel {
// Presentation only. Exact reference endpoints and samples remain unchanged.
inline constexpr double axis_display_overhang = 1.0;
inline std::string axis_endpoint_parent_key(std::string_view key) {
    if(const auto parent=solid_state_parent(key)) {
        const auto source=axis_endpoint_parent_key(parent->second);
        return source.empty()?std::string{}:solid_state_child_key(parent->first,source);
    }
    for(const std::string_view prefix:{"profile:path-point:start:from:","profile:path-point:end:from:",
            "helical:axis-point:start:from:","helical:axis-point:end:from:"})
        if(key.starts_with(prefix))return std::string(key.substr(prefix.size()));
    for(const std::string_view prefix:{"sweep:path-point:start:from:","sweep:path-point:end:from:"})
        if(key.starts_with(prefix))return "centerline:from:"+std::string(key.substr(prefix.size()));
    if(key=="axis:point:start"||key=="axis:point:end")return "axis";
    return {};
}
inline std::optional<std::array<Vec3,2>> axis_curve_overhang(const ViewerEdge& edge) {
    if (edge.infinite || edge.points.size()<2) return {};
    const auto a=edge.points.front(),b=edge.points.back();
    double extent2=1.;
    for(const auto p:edge.points)extent2=std::max(extent2,dimension_dot(dimension_sub(p,a),dimension_sub(p,a)));
    constexpr double rounding=64*std::numeric_limits<double>::epsilon();
    if (dimension_dot(dimension_sub(b,a),dimension_sub(b,a))<=rounding*rounding*extent2) return {};
    Vec3 first,last;
    if(edge.exact_spline) {
        const auto& c=*edge.exact_spline;
        // Clamped endpoint tangents follow the first/last distinct control pole.
        // Avoid allocating rational derivative workspaces on ordinary repaint.
        if(c.knots.front()==c.knots[c.degree] && c.knots.back()==c.knots[c.poles.size()]) {
            for(std::size_t i=1;i<c.poles.size();++i) {
                first=dimension_sub(c.poles[i],c.poles.front());
                if(dimension_dot(first,first)>1e-24)break;
            }
            for(std::size_t i=c.poles.size()-1;i>0;--i) {
                last=dimension_sub(c.poles.back(),c.poles[i-1]);
                if(dimension_dot(last,last)>1e-24)break;
            }
        } else {first=bspline_derivative(c,0);last=bspline_derivative(c,1);}
    } else {
        first=last=dimension_sub(b,a);
        // A sampled curved path without exact geometry cannot supply an exact tangent.
        const double length2=dimension_dot(first,first);
        for(const auto p:edge.points) {
            const auto cross=dimension_cross(first,dimension_sub(p,a));
            if(dimension_dot(cross,cross)>length2*1e-16)return {};
        }
    }
    if(dimension_dot(first,first)<1e-24||dimension_dot(last,last)<1e-24)return {};
    return std::array{dimension_sub(a,dimension_scale(dimension_unit(first),axis_display_overhang)),
                      dimension_add(b,dimension_scale(dimension_unit(last),axis_display_overhang))};
}
inline double axis_display_length(const ViewerAxis& axis) {
    std::string storage;
    const auto key=solid_state_source_key(axis.reference.semantic_key,storage);
    // Calculated profile axes already include their fitted display margin.
    if(key=="axis:primary"||key.starts_with("axis:profile:")||key.starts_with("centerline:from:")||
       key.starts_with("origin:")||key.starts_with("sketch_axis:"))return axis.display_length;
    return axis.display_length+2*axis_display_overhang;
}
}
