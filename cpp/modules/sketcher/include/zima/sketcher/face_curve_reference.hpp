#pragma once
#include <zima/sketcher/sketch.hpp>
#include <string_view>

namespace zima::sketcher {
// The selector is part of the persisted source identity, never a mesh index.
inline constexpr std::string_view face_curve_prefix="sketch-outline:";
inline bool is_face_curve_key(std::string_view key) { return key.starts_with(face_curve_prefix); }
// Fillet and Chamfer publish exact persisted result-edge identities. Unlike an
// anonymous Boolean/result fragment, these edges can be refreshed from the
// calculated Body before a later Sketch.
inline bool is_edge_treatment_body_edge_key(std::string_view key) {
    return key.starts_with("fillet:edge:")||key.starts_with("chamfer:edge:");
}
inline std::string face_curve_source_key(const std::string& key) {
    if(!is_face_curve_key(key))return key;
    const auto separator=key.find(':',face_curve_prefix.size());
    return separator==std::string::npos?std::string{}:key.substr(separator+1);
}
inline std::string face_curve_key(const std::string& key,int side) {
    return std::string(face_curve_prefix)+(side<0?"negative:":"positive:")+key;
}
// Adds only certified analytical generators. Actual rims retain their ordinary
// original Edge identities; parameter seams are never promoted to outlines.
std::vector<kernel::ViewerEdge> external_face_curve_edges(
    const kernel::ViewerReferenceGeometry&,kernel::Vec3 origin,
    kernel::Vec3 x_axis,kernel::Vec3 y_axis);
}
