#include <zima/document/holes.hpp>
#include <algorithm>
#include <cmath>

namespace zima::document {
kernel::FeatureGroupRequest holes_request(const HistoryContainer& feature,
    const sketcher::Sketch& sketch) {
    if (feature.feature_kind != FeatureKind::Holes ||
        feature.combine_mode != CombineMode::Subtract)
        throw std::invalid_argument("Otvory mohou pouze odečítat materiál.");
    if (sketch.id != feature.holes.sketch_id || sketch.owner_container_id != feature.id)
        throw std::invalid_argument("Otvory nemají platnou vlastní skicu.");
    if (!std::isfinite(feature.holes.diameter) || feature.holes.diameter < 0.001 || feature.holes.diameter > 1000000)
        throw std::invalid_argument("Průměr otvorů musí být v rozsahu 0,001 až 1 000 000 mm.");
    sketch.validate();
    const auto unsupported = [](const auto& curves) {
        return std::ranges::any_of(curves, [](const auto& curve) { return !curve.construction; });
    };
    if (unsupported(sketch.circles) || unsupported(sketch.arcs) ||
        unsupported(sketch.ellipses) || unsupported(sketch.elliptical_arcs) ||
        unsupported(sketch.bsplines) || !sketch.texts.empty())
        throw std::invalid_argument("Otvory podporují pouze úsečky; ostatní geometrii označte jako konstrukční.");
    kernel::FeatureGroupRequest group;
    for (const auto& segment : sketch.segments) {
        if (segment.construction) continue;
        const auto* first = sketch.find_point(segment.first_point_id);
        const auto* last = sketch.find_point(segment.second_point_id);
        if (!first || !last) throw std::invalid_argument("Úsečce otvoru chybí koncový bod.");
        const auto start = sketch.world_point(first->x, first->y);
        const auto end = sketch.world_point(last->x, last->y);
        kernel::Vec3 direction{end.x-start.x, end.y-start.y, end.z-start.z};
        const double length = std::hypot(direction.x, direction.y, direction.z);
        if (!std::isfinite(length) || length < 0.001)
            throw std::invalid_argument("Úsečka otvoru musí mít délku alespoň 0,001 mm.");
        kernel::ExtrusionRequest bore;
        bore.outer_profile = kernel::ExtrusionRequest::CircleProfile{start, feature.holes.diameter * 0.5};
        bore.direction = direction;
        // All ancestry is defined before OCCT: the bore derives from this
        // segment, and its seam from the segment's persisted starting point.
        bore.profile_region_id = "holes:" + feature.feature_id + ":from:" + segment.id;
        bore.outer_boundary_id = segment.id;
        bore.outer_edge_source_ids = {segment.id};
        // Adjacent segments may share the same Sketch point. Their distinct
        // seam edges still need separate children of that persisted point.
        bore.outer_vertex_source_ids = {"holes:" + feature.feature_id + ":axis:" +
            segment.id + ":from:" + segment.first_point_id};
        group.children.emplace_back(std::move(bore));
    }
    if (group.children.empty()) throw std::invalid_argument("Nakreslete alespoň jednu nekonstrukční úsečku otvoru.");
    return group;
}
}
