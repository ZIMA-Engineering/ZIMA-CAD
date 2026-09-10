#pragma once
#include <zima/document/part_document.hpp>
namespace zima::document {
inline kernel::ModelEnvelope annotation_frame(const Placement &p) {
    return kernel::annotation_frame({p.x, p.y, p.z}, {p.rotation_x, p.rotation_y, p.rotation_z});
}
inline std::map<kernel::ObjectEnvelopeKey, kernel::ModelEnvelope>
part_annotation_frames(const PartDocument &part) {
    std::map<kernel::ObjectEnvelopeKey, kernel::ModelEnvelope> frames;
    const auto body_frame = [&](const std::string &object, kernel::ModelEnvelope frame) {
        if (const auto *body = part.body_owner_for_object(object))
            return kernel::composed_annotation_frame(annotation_frame(body->scope.placement),
                                                     frame);
        return frame;
    };
    for (const auto &body : part.body_history.bodies())
        frames[{body.scope.id, {}}] = annotation_frame(body.scope.placement);
    for (const auto &c : part.history) {
        auto f = body_frame(c.id, annotation_frame(c.placement));
        for (const auto &id : {c.id, c.feature_id, c.container_origin.id})
            if (!id.empty())
                frames[{id, {}}] = f;
    }
    const auto construction = [&](const auto &self, const auto &c) -> void {
        const auto f = body_frame(c.id, kernel::annotation_frame(c.origin, c.rotation));
        for (const auto &id : {c.id, c.entity_id, c.container_origin.id})
            if (!id.empty())
                frames[{id, {}}] = f;
        for (const auto &child : c.curve_points)
            self(self, child);
    };
    for (const auto &c : part.constructions)
        construction(construction, c);
    for (const auto &c : part.history)
        if (c.feature_kind == FeatureKind::Sweep3D)
            construction(construction, c.sweep3d.path);
    for (const auto &sketch : part.sketches) {
        kernel::ModelEnvelope f;
        f.origin = sketch.resolved_origin;
        f.axes = {sketch.resolved_x_axis, sketch.resolved_y_axis, sketch.resolved_normal};
        frames[{sketch.id, {}}] = body_frame(sketch.id, f);
    }
    return frames;
}
inline std::map<kernel::ObjectEnvelopeKey, kernel::ModelEnvelope>
part_annotation_envelopes(const PartDocument &part, const kernel::ViewerMesh &mesh) {
    auto frames = kernel::object_envelopes(mesh, part_annotation_frames(part));
    for (const auto &c : part.history)
        if (auto found = frames.find({c.feature_id, {}});
            found != frames.end() && found->second.valid)
            frames[{c.id, {}}] = found->second;
    for (const auto &c : part.constructions)
        if (auto found = frames.find({c.entity_id, {}});
            found != frames.end() && found->second.valid)
            frames[{c.id, {}}] = found->second;
    // A Body's own frame accumulates its children's geometric bounds without
    // changing their local frames or incorporating presentation offsets.
    const auto accumulate = [&](const std::string &id) {
        auto found = frames.find({id, {}});
        if (found == frames.end() || !found->second.valid)
            return;
        if (const auto *body = part.body_owner_for_object(id))
            for (auto corner : found->second.corners())
                frames[{body->scope.id, {}}].include(corner);
    };
    for (const auto &c : part.history)
        accumulate(c.feature_id);
    for (const auto &c : part.constructions)
        accumulate(c.entity_id);
    for (const auto &sketch : part.sketches)
        accumulate(sketch.id);
    return frames;
}
} // namespace zima::document
