#pragma once
#include <zima/document/body_history.hpp>
#include <utility>
#include <stdexcept>

namespace zima::document {
// Body creation policy only; consumes the existing placement/reference model.
inline Placement body_origin_attachment(const std::string& document_id, Placement initial = {}) {
    if (!initial.references.empty()) throw std::invalid_argument("Body already has placement references");
    const auto add = [&](const char* plane, double offset) {
        ConstructionReference reference;
        reference.owner_id = document_id + ":origin";
        reference.semantic_key = std::string("origin:plane:") + plane;
        reference.offset = offset;
        reference.supports_offset = true;
        initial.references.push_back(std::move(reference));
    };
    add("xy", initial.z); add("xz", initial.y); add("yz", initial.x);
    for (const auto& [plane, role] : {std::pair{"xz", "front"}, std::pair{"xy", "top"}}) {
        ConstructionReference reference;
        reference.owner_id = document_id + ":origin";
        reference.semantic_key = std::string("origin:plane:") + plane;
        reference.orientation_role = role;
        reference.orientation_drives_rotation = true;
        reference.orientation_only = true;
        initial.references.push_back(std::move(reference));
    }
    initial.rotation_offset_x = initial.rotation_x;
    initial.rotation_offset_y = initial.rotation_y;
    initial.rotation_offset_z = initial.rotation_z;
    initial.absolute_rotation_x = initial.rotation_x;
    initial.absolute_rotation_y = initial.rotation_y;
    initial.absolute_rotation_z = initial.rotation_z;
    return initial;
}
inline std::string create_origin_bound_body(BodyHistoryGraph& graph,
        const std::string& document_id, std::string name, Placement initial = {}) {
    const auto placement = body_origin_attachment(document_id, std::move(initial));
    const auto id = graph.create_body(std::move(name));
    auto body = *graph.find(id);
    body.scope.placement = placement;
    graph.update_body(std::move(body));
    return id;
}
} // namespace zima::document
