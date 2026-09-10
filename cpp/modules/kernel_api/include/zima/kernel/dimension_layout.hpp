#pragma once
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
#include <zima/kernel/geometry_kernel.hpp>

namespace zima::kernel {
inline std::string dimension_unit_text(const std::string& suffix) {
    const auto first=suffix.find_first_not_of(" \t");
    return first!=std::string::npos && suffix.compare(first,2,"mm")==0
        ? suffix.substr(first) : suffix;
}
inline ModelEnvelope model_envelope(const ViewerMesh &mesh) {
    ModelEnvelope bounds;
    for (auto index : mesh.triangles)
        if (index < mesh.vertices.size())
            bounds.include(mesh.vertices[index]);
    for (const auto &edge : mesh.edges)
        if (!edge.construction && !edge.reference.semantic_key.starts_with("origin:"))
            for (auto point : edge.points)
                bounds.include(point);
    return bounds;
}
inline std::map<ObjectEnvelopeKey, ModelEnvelope>
object_envelopes(const ViewerMesh &mesh, std::map<ObjectEnvelopeKey, ModelEnvelope> result = {}) {
    for (const auto &[key, frame] : mesh.annotation_frames)
        result.try_emplace(key, frame);
    if (!result.contains({}) || !result.at({}).valid)
        result[{}] = model_envelope(mesh);
    const auto add = [&](const auto &ref, Vec3 point) {
        if (ref.valid())
            result[{ref.owner_id, ref.instance_path}].include(point);
    };
    const auto triangles = [&](const auto &packet) {
        for (std::size_t i = 0; i < packet.triangles.size(); ++i)
            if (i / 3 < packet.triangle_references.size() &&
                packet.triangles[i] < packet.vertices.size())
                add(packet.triangle_references[i / 3], packet.vertices[packet.triangles[i]]);
    };
    triangles(mesh);
    triangles(mesh.original_references);
    for (const auto *edges : {&mesh.edges, &mesh.original_references.edges})
        for (const auto &edge : *edges)
            for (auto point : edge.points)
                add(edge.reference, point);
    for (const auto &point : mesh.points)
        add(point.reference, point.position);
    for (const auto &point : mesh.original_references.points)
        add(point.reference, point.position);
    // A display axis must not inflate its owner's geometric envelope.
    // Only axis-only objects without geometry derive bounds from display span.
    std::set<ObjectEnvelopeKey> geometric_frames;
    for(const auto& [key,frame]:result)if(frame.valid)geometric_frames.insert(key);
    for (const auto *axes : {&mesh.axes, &mesh.original_references.axes})
        for (const auto &axis : *axes) {
            if(geometric_frames.contains({axis.reference.owner_id,axis.reference.instance_path}))continue;
            const double length = std::hypot(axis.direction.x, axis.direction.y, axis.direction.z);
            if (length > 1e-12)
                for (double sign : {-1., 1.})
                    add(axis.reference, {axis.point.x + sign * axis.direction.x *
                                                            axis.display_length / (2 * length),
                                         axis.point.y + sign * axis.direction.y *
                                                            axis.display_length / (2 * length),
                                         axis.point.z + sign * axis.direction.z *
                                                            axis.display_length / (2 * length)});
        }
    return result;
}
// Consume resolved Euler placement (X, then Y, then Z); this does not resolve
// references or change the shared placement contract.
inline ModelEnvelope annotation_frame(Vec3 origin, Vec3 degrees) {
    const auto rotate = [&](Vec3 p) {
        const auto x = degrees.x * std::numbers::pi / 180, y = degrees.y * std::numbers::pi / 180,
                   z = degrees.z * std::numbers::pi / 180;
        p = {p.x, p.y * std::cos(x) - p.z * std::sin(x), p.y * std::sin(x) + p.z * std::cos(x)};
        p = {p.x * std::cos(y) + p.z * std::sin(y), p.y, -p.x * std::sin(y) + p.z * std::cos(y)};
        return Vec3{p.x * std::cos(z) - p.y * std::sin(z), p.x * std::sin(z) + p.y * std::cos(z),
                    p.z};
    };
    ModelEnvelope frame;
    frame.origin = origin;
    frame.axes = {rotate({1, 0, 0}), rotate({0, 1, 0}), rotate({0, 0, 1})};
    return frame;
}
inline ModelEnvelope composed_annotation_frame(const ModelEnvelope &parent, ModelEnvelope child) {
    const auto zero = parent.origin;
    child.origin = parent.world(child.origin);
    for (auto &axis : child.axes) {
        const auto p = parent.world(axis);
        axis = {p.x - zero.x, p.y - zero.y, p.z - zero.z};
    }
    return child;
}
// Stored separately from measuring/driving data. Plane rotation is about the
// measured direction, so it never changes the measured length.
struct DimensionLayout {
    int plane_quarter_turns{};
    std::optional<double> envelope_offset;
    double text_along{}, text_outward{};
    bool arrows_reversed{};
    double line_offset{};
    bool radius_center_line_hidden{};
    bool operator==(const DimensionLayout &) const = default;
};
inline void cycle_dimension_presentation(DimensionLayout &layout, ViewerDimensionKind kind) {
    if (kind != ViewerDimensionKind::Radius) {
        layout.arrows_reversed = !layout.arrows_reversed;
    } else if (layout.radius_center_line_hidden) {
        layout.radius_center_line_hidden = false;
        layout.arrows_reversed = false;
    } else if (layout.arrows_reversed) {
        layout.radius_center_line_hidden = true;
    } else {
        layout.arrows_reversed = true;
    }
}
struct DimensionLayoutEntry {
    std::string owner_id, semantic_key;
    DimensionLayout layout;
    bool operator==(const DimensionLayoutEntry &) const = default;
};
inline Vec3 dimension_add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 dimension_sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 dimension_scale(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dimension_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 dimension_cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 dimension_unit(Vec3 a) {
    const auto n = std::sqrt(dimension_dot(a, a));
    return n > 1e-12 ? dimension_scale(a, 1 / n) : Vec3{};
}
inline void validate_dimension_layout(const DimensionLayout &layout) {
    if (layout.plane_quarter_turns < 0 || layout.plane_quarter_turns > 3 ||
        (layout.envelope_offset &&
         (!std::isfinite(*layout.envelope_offset) || *layout.envelope_offset < 0)) ||
        !std::isfinite(layout.text_along) || !std::isfinite(layout.text_outward) || !std::isfinite(layout.line_offset))
        throw std::invalid_argument("Invalid dimension presentation");
}
inline ViewerDimension layout_dimension(ViewerDimension d, const ModelEnvelope &bounds,
                                        const DimensionLayout &layout) {
    validate_dimension_layout(layout);
    d.arrows_reversed = layout.arrows_reversed;
    d.radius_center_line_hidden = layout.radius_center_line_hidden;
    auto normal = dimension_unit(d.plane_normal);
    const bool angular = d.kind == ViewerDimensionKind::Angular;
    const auto direction =
        dimension_unit(dimension_sub(angular ? d.line_first : d.witness_second, d.witness_first));
    auto outward = dimension_unit(dimension_cross(normal, direction));
    if (dimension_dot(direction, direction) < .5 || dimension_dot(outward, outward) < .5)
        return d;
    if (angular && layout.plane_quarter_turns)
        throw std::invalid_argument("Angular dimension plane is defined by its measured rays");
    const bool radial=d.kind==ViewerDimensionKind::Radius || d.kind==ViewerDimensionKind::Diameter;
    const double angle = radial?0:layout.plane_quarter_turns * std::numbers::pi / 2;
    const auto rotate = [&](Vec3 p) {
        const auto v = dimension_sub(p, d.witness_first);
        return dimension_add(
            d.witness_first,
            dimension_add(
                dimension_scale(direction, dimension_dot(v, direction)),
                dimension_add(
                    dimension_scale(outward, dimension_dot(v, outward) * std::cos(angle) -
                                                 dimension_dot(v, normal) * std::sin(angle)),
                    dimension_scale(normal, dimension_dot(v, outward) * std::sin(angle) +
                                                dimension_dot(v, normal) * std::cos(angle)))));
    };
    d.line_first = rotate(d.line_first);
    d.line_second = rotate(d.line_second);
    if (d.label_position)
        d.label_position = rotate(*d.label_position);
    outward = dimension_unit(dimension_add(dimension_scale(outward, std::cos(angle)),
                                           dimension_scale(normal, std::sin(angle))));
    d.plane_normal = dimension_unit(dimension_cross(direction, outward));
    if (layout.envelope_offset && bounds.valid) {
        if (angular) {
            double radius = 0;
            for (auto corner : bounds.corners()) {
                auto v = dimension_sub(corner, d.witness_first);
                v = dimension_sub(v, dimension_scale(normal, dimension_dot(v, normal)));
                radius = std::max(radius, std::sqrt(dimension_dot(v, v)));
            }
            radius += *layout.envelope_offset;
            d.line_first = dimension_add(d.witness_first, dimension_scale(direction, radius));
            d.line_second = dimension_add(
                d.witness_first,
                dimension_scale(dimension_unit(dimension_sub(d.line_second, d.witness_first)),
                                radius));
            const auto theta = d.sweep_degrees * std::numbers::pi / 360;
            d.label_position = dimension_add(
                d.witness_first,
                dimension_scale(dimension_add(dimension_scale(direction, std::cos(theta)),
                                              dimension_scale(outward, std::sin(theta))),
                                radius + 2.0));
        } else {
            double support = -std::numeric_limits<double>::infinity();
            for (auto corner : bounds.corners())
                support = std::max(support, dimension_dot(corner, outward));
            support += *layout.envelope_offset;
            d.line_first = dimension_add(
                d.witness_first,
                dimension_scale(outward, support - dimension_dot(d.witness_first, outward)));
            d.line_second = dimension_add(
                d.witness_second,
                dimension_scale(outward, support - dimension_dot(d.witness_second, outward)));
            d.label_position =
                dimension_add(dimension_scale(dimension_add(d.line_first, d.line_second), .5),
                              dimension_scale(outward, 2.0));
        }
    }
    if (layout.line_offset) {
        if (angular) {
            const auto old=dimension_sub(d.line_first,d.witness_first);
            const auto radius=std::sqrt(dimension_dot(old,old));
            if(radius>1e-9){const double ratio=std::max(.1,radius+layout.line_offset)/radius;
                d.line_first=dimension_add(d.witness_first,dimension_scale(old,ratio));
                d.line_second=dimension_add(d.witness_first,dimension_scale(dimension_sub(d.line_second,d.witness_first),ratio));
                if(d.label_position)d.label_position=dimension_add(d.witness_first,dimension_scale(dimension_sub(*d.label_position,d.witness_first),ratio));}
        } else {
            const auto move=dimension_scale(outward,layout.line_offset);
            d.line_first=dimension_add(d.line_first,move);d.line_second=dimension_add(d.line_second,move);
            if(d.label_position)d.label_position=dimension_add(*d.label_position,move);
            else d.label_position=dimension_scale(dimension_add(d.line_first,d.line_second),.5);
        }
    }
    if (layout.text_along || layout.text_outward)
        d.label_position =
            dimension_add(d.label_position.value_or(
                              dimension_scale(dimension_add(d.line_first, d.line_second), .5)),
                          dimension_add(dimension_scale(direction, layout.text_along),
                                        dimension_scale(outward, layout.text_outward)));
    if (radial && d.label_position) {
        const auto delta = dimension_sub(*d.label_position, d.witness_first);
        d.label_position = dimension_sub(*d.label_position,
            dimension_scale(d.plane_normal, dimension_dot(delta, d.plane_normal)));
    }
    return d;
}
inline DimensionLayout dragged_dimension_layout(const ViewerDimension &shown,
                                                const ModelEnvelope &bounds,
                                                DimensionLayout initial, int handle, double along,
                                                double outward) {
    if (shown.kind == ViewerDimensionKind::Radius || shown.kind == ViewerDimensionKind::Diameter) {
        initial.text_along += along;
        initial.text_outward += outward;
        return initial;
    }
    if (handle == 0) {
        auto moved = dragged_dimension_layout(shown, bounds, initial, 1, 0, outward);
        moved.text_along += along;
        return moved;
    }
    if (!initial.envelope_offset || !bounds.valid) {
        initial.line_offset += outward;
        return initial;
    }
    const bool angular = shown.kind == ViewerDimensionKind::Angular;
    const auto direction = dimension_unit(
        dimension_sub(angular ? shown.line_first : shown.witness_second, shown.witness_first));
    const auto normal = dimension_unit(shown.plane_normal),
               side = dimension_unit(dimension_cross(normal, direction));
    double support = angular ? 0 : -std::numeric_limits<double>::infinity();
    for (auto corner : bounds.corners()) {
        if (angular) {
            auto r = dimension_sub(corner, shown.witness_first);
            r = dimension_sub(r, dimension_scale(normal, dimension_dot(r, normal)));
            support = std::max(support, std::sqrt(dimension_dot(r, r)));
        } else
            support = std::max(support, dimension_dot(corner, side));
    }
    double requested = dimension_dot(shown.line_first, side) + outward;
    if (angular) {
        auto r =
            dimension_sub(handle == 2 ? shown.line_second : shown.line_first, shown.witness_first);
        requested =
            std::hypot(dimension_dot(r, direction) + along, dimension_dot(r, side) + outward);
    }
    initial.envelope_offset = std::max(0., requested - support);
    return initial;
}
inline const DimensionLayout *
find_dimension_layout(const std::vector<DimensionLayoutEntry> &entries,
                      const EdgeReference &reference) {
    for (const auto &entry : entries)
        if (entry.owner_id == reference.owner_id && entry.semantic_key == reference.semantic_key)
            return &entry.layout;
    return nullptr;
}
inline void store_dimension_layout(std::vector<DimensionLayoutEntry> &entries,
                                   const EdgeReference &reference, DimensionLayout layout) {
    validate_dimension_layout(layout);
    for (auto &entry : entries)
        if (entry.owner_id == reference.owner_id && entry.semantic_key == reference.semantic_key) {
            entry.layout = layout;
            return;
        }
    entries.push_back({reference.owner_id, reference.semantic_key, layout});
}
} // namespace zima::kernel
