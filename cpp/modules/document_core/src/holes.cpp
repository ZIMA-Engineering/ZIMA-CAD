#include <zima/document/holes.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>

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
        group.axes.push_back({{(start.x+end.x)*.5,(start.y+end.y)*.5,(start.z+end.z)*.5},
            {direction.x/length,direction.y/length,direction.z/length}, length+2.0,
            {feature.id,"axis:profile:holes:"+feature.feature_id+":from:"+segment.id,{}},"Osa otvoru"});
        group.children.emplace_back(std::move(bore));
    }
    if (group.children.empty()) throw std::invalid_argument("Nakreslete alespoň jednu nekonstrukční úsečku otvoru.");
    return group;
}

kernel::ViewerMesh holes_preview(const HistoryContainer& feature,
    const sketcher::Sketch& sketch) {
    kernel::ViewerMesh result;
    if (std::ranges::none_of(sketch.segments, [](const auto& segment) { return !segment.construction; }))
        return result;
    const auto request = holes_request(feature, sketch);
    result.axes = request.axes;
    result.edges.reserve(request.children.size() * 6);
    std::string dimension_segment;
    const auto cross = [](const kernel::Vec3& a, const kernel::Vec3& b) {
        return kernel::Vec3{a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
    };
    for (const auto& child : request.children) {
        const auto& bore = std::get<kernel::ExtrusionRequest>(child);
        const auto& circle = std::get<kernel::ExtrusionRequest::CircleProfile>(bore.outer_profile);
        const double length = std::hypot(bore.direction.x, bore.direction.y, bore.direction.z);
        const kernel::Vec3 axis{bore.direction.x/length, bore.direction.y/length, bore.direction.z/length};
        auto radial = cross(axis, std::abs(axis.z) < 0.9 ? kernel::Vec3{0,0,1} : kernel::Vec3{0,1,0});
        const double norm = std::hypot(radial.x, radial.y, radial.z);
        radial = {radial.x/norm, radial.y/norm, radial.z/norm};
        const auto tangent = cross(axis, radial);
        // Stable choice across segment reordering, recomputed from the live
        // draft so no stale point or segment reference survives deletion.
        if (dimension_segment.empty() || bore.outer_boundary_id < dimension_segment) {
            dimension_segment = bore.outer_boundary_id;
            const kernel::Vec3 rim{circle.center.x+circle.radius*radial.x,
                                   circle.center.y+circle.radius*radial.y,
                                   circle.center.z+circle.radius*radial.z};
            kernel::ViewerDimension dimension{circle.center, rim, circle.center, rim,
                feature.holes.diameter, {feature.id, "parameter:diameter", {}}, "⌀ "};
            dimension.kind = kernel::ViewerDimensionKind::Diameter;
            dimension.plane_normal = axis;
            dimension.value_lock_key = "diameter";
            dimension.locked = feature.value_locks.contains("diameter");
            result.dimensions.assign(1, std::move(dimension));
        }
        constexpr int samples = 64;
        std::array<std::vector<kernel::Vec3>, 2> rings;
        for (int end = 0; end < 2; ++end) {
            auto& points = rings[end];
            points.reserve(samples+1);
            for (int sample = 0; sample < samples; ++sample) {
                const double angle = 2*std::numbers::pi*sample/samples;
                const double u = circle.radius*std::cos(angle), v = circle.radius*std::sin(angle);
                points.push_back({circle.center.x+end*bore.direction.x+u*radial.x+v*tangent.x,
                                  circle.center.y+end*bore.direction.y+u*radial.y+v*tangent.y,
                                  circle.center.z+end*bore.direction.z+u*radial.z+v*tangent.z});
            }
            points.push_back(points.front());
        }
        const auto append = [&](std::vector<kernel::Vec3> points, const std::string& role) {
            kernel::ViewerEdge edge;
            edge.reference = {feature.id, "preview:holes:"+bore.outer_boundary_id+":"+role, {}};
            edge.points = std::move(points);
            result.edges.push_back(std::move(edge));
        };
        for (int side = 0; side < 4; ++side) {
            const auto index = side*samples/4;
            append({rings[0][index], rings[1][index]}, "side:"+std::to_string(side));
        }
        append(std::move(rings[0]), "start");
        append(std::move(rings[1]), "end");
    }
    return result;
}
}
