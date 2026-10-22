#pragma once
#include <zima/kernel/boolean_face_ancestry.hpp>
#include <zima/kernel/geometry_kernel.hpp>

namespace zima::viewer {
// Presentation follows the same persisted owner as ordinary face picking.
// Keep reference ancestry and exact operational fragment IDs untouched.
inline std::string original_face_owner(const kernel::FaceReference& face) {
    auto owner=face.owner_id;auto key=face.semantic_key;
    while(const auto parent=kernel::boolean_face_parent(key)) {
        owner=parent->first;key=parent->second;
    }
    return owner;
}
inline std::string visible_face_owner(const kernel::FaceReference& face) {
    return face.display_owner_id.empty()?original_face_owner(face):face.display_owner_id;
}
inline bool feature_owns_display_edge(const kernel::ViewerEdge& edge,
        const std::string& owner,const std::string& path) {
    // An adjacent face does not transfer ownership of another feature's
    // edge. In particular a later cut must not recolor all borders of the
    // older supporting face (or the older feature all new cut borders).
    return edge.reference.instance_path==path&&
        (edge.display_owner_id.empty()?edge.reference.owner_id:edge.display_owner_id)==owner;
}
} // namespace zima::viewer
