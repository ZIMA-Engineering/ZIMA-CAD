#pragma once
#include <zima/kernel/geometry_kernel.hpp>

namespace zima::app {
inline bool native_sketch_profile_edge(const kernel::ViewerEdge& edge) {
    const auto& key=edge.reference.semantic_key;
    const bool native=key.starts_with("segment:")||key.starts_with("circle:")||
        key.starts_with("arc:")||key.starts_with("corner_radius:")||
        key.starts_with("ellipse:")||key.starts_with("elliptical_arc:")||
        key.starts_with("bspline:")||key.starts_with("text:");
    return native&&(!edge.construction||edge.dash_dot);
}
}
