#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::preview_sketch_segment_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_segment_active_ || !pending_segment_start_) return;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) {
        viewer_->set_transient_edges({});
        return;
    }
    if (sketch_polyline_active_ && sketch_polyline_arc_mode_ &&
        !pending_polyline_tangent_geometry_id_.empty()) {
        try {
            auto preview = *sketch;
            const auto endpoint_inference =
                inferred_sketch_segment_end(*position);
            std::optional<SketchCandidateSnap> endpoint_snap;
            if (!sketch_skip_candidate_snap_) {
                if (const auto candidate = viewer_->hovered_candidate()) {
                    endpoint_snap = sketch_candidate_snap_ray(
                        *candidate, origin, direction);
                }
            }
            const bool exact_endpoint = endpoint_snap.has_value() ||
                pending_sketch_snap_kind_.has_value();
            const bool point_alignment = !exact_endpoint &&
                endpoint_inference.kind.has_value() &&
                !endpoint_inference.reference_point_id.empty();
            const auto center_snap = !exact_endpoint && !point_alignment
                ? inferred_sketch_polyline_arc_center_snap(*position)
                : std::nullopt;
            const auto preview_end = exact_endpoint ? *position
                : point_alignment ? endpoint_inference.position
                : center_snap ? center_snap->end : *position;
            const auto start = std::find_if(
                preview.points.begin(), preview.points.end(), [&](const auto& point) {
                    return std::hypot(
                        point.x - (*pending_segment_start_)[0],
                        point.y - (*pending_segment_start_)[1]) <= 1.0e-6;
                });
            if (start == preview.points.end()) {
                viewer_->set_transient_edges({});
                return;
            }
            const auto arc_id = preview.add_tangent_arc(
                start->id, preview_end[0], preview_end[1],
                pending_polyline_tangent_geometry_id_);
            auto mesh = preview.viewer_mesh();
            const auto edge = std::find_if(mesh.edges.begin(), mesh.edges.end(),
                [&](const auto& value) {
                    return value.reference.semantic_key == "arc:" + arc_id;
                });
            if (edge != mesh.edges.end()) {
                auto transient = *edge;
                transient.reference = {};
                viewer_->set_transient_edges({std::move(transient)});
                const auto arc = std::ranges::find_if(
                    preview.arcs, [&](const auto& value) {
                        return value.id == arc_id;
                    });
                if (arc != preview.arcs.end()) {
                    const auto* center = preview.find_point(
                        arc->center_point_id);
                    const auto* start_point = preview.find_point(
                        arc->start_point_id);
                    std::vector<zima::kernel::Vec3> characteristic_points;
                    if (center != nullptr) {
                        characteristic_points.push_back(sketch->world_point(
                            center->x, center->y));
                        if (center_snap) {
                            viewer_->set_transient_labels({{
                                sketch->world_point(center->x, center->y),
                                "M"}});
                        } else if (point_alignment) {
                            viewer_->set_transient_labels({{
                                sketch->world_point(
                                    preview_end[0], preview_end[1]),
                                *endpoint_inference.kind ==
                                        zima::sketcher::ConstraintKind::Horizontal
                                    ? "H" : "V"}});
                        } else {
                            viewer_->set_transient_labels({});
                        }
                    }
                    if (start_point != nullptr) {
                        characteristic_points.push_back(sketch->world_point(
                            start_point->x, start_point->y));
                    }
                    viewer_->set_transient_points(
                        std::move(characteristic_points));
                } else {
                    viewer_->set_transient_points({});
                }
                return;
            }
        } catch (const std::exception&) {
            viewer_->set_transient_edges({});
            viewer_->set_transient_points({});
            return;
        }
    }
    auto inference = inferred_sketch_segment_end(*position);
    std::optional<SketchCandidateSnap> endpoint_snap;
    if (!sketch_skip_candidate_snap_) {
        if (const auto candidate = viewer_->hovered_candidate()) {
            endpoint_snap = sketch_candidate_snap_ray(
                *candidate, origin, direction);
        }
    }
    if (endpoint_snap && endpoint_snap->relation ==
            zima::sketcher::ConstraintKind::PointOnLine &&
        (endpoint_snap->support_geometry_id == "sketch_axis:x" ||
         endpoint_snap->support_geometry_id == "sketch_axis:y")) {
        // Once the endpoint is offered on a Sketch axis, the preview must
        // apply the same directional-intent rule as confirmation.  Generic
        // point alignment can otherwise find an unrelated point on that
        // axis and advertise C+H/V for a visibly oblique segment even though
        // confirmation correctly commits only the point-on-axis relation.
        const bool vertical =
            endpoint_snap->support_geometry_id == "sketch_axis:x";
        const double direction_error = vertical
            ? std::abs((*position)[0] - (*pending_segment_start_)[0])
            : std::abs((*position)[1] - (*pending_segment_start_)[1]);
        const double intent_tolerance = viewer_->world_tolerance_for_pixels(
            2.0 * viewer_->devicePixelRatioF());
        inference.kind.reset();
        inference.reference_point_id.clear();
        inference.equal_length_reference_id.clear();
        inference.symmetry_axis_id.clear();
        inference.tangent_reference_id.clear();
        inference.perpendicular_reference_id.clear();
        inference.parallel_reference_id.clear();
        inference.midpoint_line_reference_id.clear();
        inference.position = *position;
        if (automatic_constraint_enabled(vertical ? zima::sketcher::ConstraintKind::Vertical : zima::sketcher::ConstraintKind::Horizontal) && direction_error <= intent_tolerance) {
            inference.kind = vertical
                ? zima::sketcher::ConstraintKind::Vertical
                : zima::sketcher::ConstraintKind::Horizontal;
            if (vertical) {
                inference.position[0] = (*pending_segment_start_)[0];
            } else {
                inference.position[1] = (*pending_segment_start_)[1];
            }
        }
    }
    if (endpoint_snap && endpoint_snap->relation ==
            zima::sketcher::ConstraintKind::Coincident) {
        // The exact snapped point must not act as an independent H/V guide
        // for itself. Only the vector between the new segment's two endpoints
        // may offer Horizontal/Vertical in this state.
        const double intent_tolerance = viewer_->world_tolerance_for_pixels(
            2.0 * viewer_->devicePixelRatioF());
        const double dx = std::abs((*position)[0] - (*pending_segment_start_)[0]);
        const double dy = std::abs((*position)[1] - (*pending_segment_start_)[1]);
        inference.kind.reset();
        inference.reference_point_id.clear();
        inference.equal_length_reference_id.clear();
        inference.symmetry_axis_id.clear();
        inference.tangent_reference_id.clear();
        inference.perpendicular_reference_id.clear();
        inference.parallel_reference_id.clear();
        inference.midpoint_line_reference_id.clear();
        if (automatic_constraint_enabled(zima::sketcher::ConstraintKind::Horizontal) && dy <= intent_tolerance) {
            inference.kind = zima::sketcher::ConstraintKind::Horizontal;
            inference.position = *position;
        } else if (automatic_constraint_enabled(zima::sketcher::ConstraintKind::Vertical) && dx <= intent_tolerance) {
            inference.kind = zima::sketcher::ConstraintKind::Vertical;
            inference.position = *position;
        }
    }
    const std::string endpoint_snap_geometry = endpoint_snap
        ? endpoint_snap->support_geometry_id
        : pending_sketch_snap_geometry_id_;
    const auto endpoint_snap_kind = endpoint_snap
        ? endpoint_snap->relation : pending_sketch_snap_kind_;
    const auto endpoint_keypoint_curve =
        sketch_keypoint_curve_id(endpoint_snap_geometry);
    const auto endpoint_tangent_curve =
        (endpoint_snap_kind ==
             zima::sketcher::ConstraintKind::PointOnCircle &&
         !endpoint_snap_geometry.empty()) ||
        (endpoint_snap_kind ==
             zima::sketcher::ConstraintKind::Coincident &&
         endpoint_keypoint_curve.has_value())
        ? std::optional<std::string>{endpoint_keypoint_curve
              ? *endpoint_keypoint_curve : endpoint_snap_geometry}
        : std::nullopt;
    const double direction_tolerance = viewer_->world_tolerance_for_pixels(
        2.0 * viewer_->devicePixelRatioF());
    const double tangent_tolerance = viewer_->world_tolerance_for_pixels(
        sketch_tangent_intent_pixels * viewer_->devicePixelRatioF());
    const double cursor_x = (*position)[0] - (*pending_segment_start_)[0];
    const double cursor_y = (*position)[1] - (*pending_segment_start_)[1];
    bool endpoint_tangent = false;
    if (endpoint_tangent_curve) {
        if (const auto tangent = sketch->curve_tangent_at_point(
                *endpoint_tangent_curve, (*position)[0], (*position)[1])) {
            endpoint_tangent = std::abs(
                cursor_x * (*tangent)[1] - cursor_y * (*tangent)[0]) <=
                tangent_tolerance;
        }
    }
    bool endpoint_perpendicular = endpoint_snap_kind ==
            zima::sketcher::ConstraintKind::PointOnLine &&
        !endpoint_snap_geometry.empty() &&
        endpoint_snap_geometry != "sketch_axis:x" &&
        endpoint_snap_geometry != "sketch_axis:y";
    if (endpoint_perpendicular) {
        if (const auto support = sketch_line_support_direction(
                endpoint_snap_geometry)) {
            const double length = std::hypot((*support)[0], (*support)[1]);
            endpoint_perpendicular = length > 1.0e-12 && std::abs(
                cursor_x * (*support)[0] + cursor_y * (*support)[1]) / length <=
                direction_tolerance;
        } else {
            endpoint_perpendicular = false;
        }
    }
    if (!automatic_constraint_enabled(zima::sketcher::ConstraintKind::Tangent)) endpoint_tangent=false;
    if (!automatic_constraint_enabled(zima::sketcher::ConstraintKind::Perpendicular)) endpoint_perpendicular=false;
    if (endpoint_tangent) {
        // TC is exclusive. A single contact must never carry stale H/V,
        // equal-length, parallel, perpendicular, symmetry or midpoint
        // inference from the same cursor neighborhood.
        if (const auto exact = exact_circle_tangent_contact(
                *sketch, *pending_segment_start_,
                *endpoint_tangent_curve, *position)) {
            inference.position = *exact;
        }
        inference.kind.reset();
        inference.reference_point_id.clear();
        inference.equal_length_reference_id.clear();
        inference.symmetry_axis_id.clear();
        inference.perpendicular_reference_id.clear();
        inference.parallel_reference_id.clear();
        inference.midpoint_line_reference_id.clear();
        inference.tangent_reference_id = *endpoint_tangent_curve;
        inference.tangent_at_start = false;
    } else if (endpoint_perpendicular) {
        inference.perpendicular_reference_id = endpoint_snap_geometry;
    }
    sketch_segment_inference_variant_count_ = inference.variant_count;
    const auto& preview_position = inference.position;
    const auto active_point = sketch->world_point(
        preview_position[0], preview_position[1]);
    std::vector<zima::kernel::ViewerEdge> preview_edges{{{
        sketch->world_point((*pending_segment_start_)[0], (*pending_segment_start_)[1]),
        active_point}, {}}};
    if (sketch_segment_construction_) {
        preview_edges.front().construction = true;
        preview_edges.front().overlay = true;
        preview_edges.front().infinite = true;
        preview_edges.front().dash_dot = true;
    }
    if (!inference.equal_length_reference_id.empty()) {
        const auto reference = std::find_if(
            sketch->segments.begin(), sketch->segments.end(),
            [&](const auto& value) {
                return value.id == inference.equal_length_reference_id;
            });
        if (reference != sketch->segments.end()) {
            const auto* first = sketch->find_point(reference->first_point_id);
            const auto* second = sketch->find_point(reference->second_point_id);
            preview_edges.push_back({{
                sketch->world_point(first->x, first->y),
                sketch->world_point(second->x, second->y)},
                {active_sketch_id_, "inference:reference", {}}});
        }
    }
    if (!inference.symmetry_axis_id.empty()) {
        const auto axis = std::find_if(
            sketch->segments.begin(), sketch->segments.end(),
            [&](const auto& value) {
                return value.id == inference.symmetry_axis_id;
            });
        if (axis != sketch->segments.end()) {
            const auto* first = sketch->find_point(axis->first_point_id);
            const auto* second = sketch->find_point(axis->second_point_id);
            preview_edges.push_back({{
                sketch->world_point(first->x, first->y),
                sketch->world_point(second->x, second->y)},
                {active_sketch_id_, "inference:reference", {}}});
        }
    }
    if (!inference.tangent_reference_id.empty()) {
        const auto source_mesh = sketch->viewer_mesh();
        const auto curve = std::find_if(
            source_mesh.edges.begin(), source_mesh.edges.end(),
            [&](const auto& value) {
                return value.reference.semantic_key ==
                        "circle:" + inference.tangent_reference_id ||
                    value.reference.semantic_key ==
                        "arc:" + inference.tangent_reference_id ||
                    value.reference.semantic_key ==
                        "ellipse:" + inference.tangent_reference_id ||
                    value.reference.semantic_key ==
                        "elliptical_arc:" + inference.tangent_reference_id ||
                    value.reference.semantic_key ==
                        "bspline:" + inference.tangent_reference_id;
            });
        if (curve != source_mesh.edges.end()) {
            auto reference = *curve;
            reference.reference = {
                active_sketch_id_, "inference:reference", {}};
            preview_edges.push_back(std::move(reference));
        }
    }
    if (!inference.perpendicular_reference_id.empty()) {
        const auto source_mesh = sketch->viewer_mesh();
        const auto support_key = inference.perpendicular_reference_id ==
                "sketch_axis:x" ||
                inference.perpendicular_reference_id == "sketch_axis:y"
            ? inference.perpendicular_reference_id
            : std::any_of(sketch->segments.begin(), sketch->segments.end(),
                  [&](const auto& value) {
                      return value.id == inference.perpendicular_reference_id;
                  })
                ? "segment:" + inference.perpendicular_reference_id
                : "external_edge:" + inference.perpendicular_reference_id;
        if (const auto support = std::find_if(
                source_mesh.edges.begin(), source_mesh.edges.end(),
                [&](const auto& value) {
                    return value.reference.semantic_key == support_key ||
                        value.reference.semantic_key ==
                            "external_axis:" +
                                inference.perpendicular_reference_id;
                }); support != source_mesh.edges.end()) {
            auto reference = *support;
            reference.reference = {
                active_sketch_id_, "inference:reference", {}};
            preview_edges.push_back(std::move(reference));
        } else if (const auto axis = std::find_if(
                source_mesh.axes.begin(), source_mesh.axes.end(),
                [&](const auto& value) {
                    return value.reference.semantic_key == support_key;
                }); axis != source_mesh.axes.end()) {
            const double half = axis->display_length * 0.5;
            preview_edges.push_back({{
                {axis->point.x - axis->direction.x * half,
                 axis->point.y - axis->direction.y * half,
                 axis->point.z - axis->direction.z * half},
                {axis->point.x + axis->direction.x * half,
                 axis->point.y + axis->direction.y * half,
                 axis->point.z + axis->direction.z * half}},
                {active_sketch_id_, "inference:reference", {}}});
        }
    }
    if (!inference.parallel_reference_id.empty()) {
        const auto reference = std::find_if(
            sketch->segments.begin(), sketch->segments.end(),
            [&](const auto& value) {
                return value.id == inference.parallel_reference_id;
            });
        if (reference != sketch->segments.end()) {
            const auto* first = sketch->find_point(reference->first_point_id);
            const auto* second = sketch->find_point(reference->second_point_id);
            preview_edges.push_back({{
                sketch->world_point(first->x, first->y),
                sketch->world_point(second->x, second->y)},
                {active_sketch_id_, "inference:reference", {}}});
        }
    }
    if (!inference.midpoint_line_reference_id.empty()) {
        const auto source_mesh = sketch->viewer_mesh();
        const auto support_key = inference.midpoint_line_reference_id ==
                "sketch_axis:x" ||
                inference.midpoint_line_reference_id == "sketch_axis:y"
            ? inference.midpoint_line_reference_id
            : "segment:" + inference.midpoint_line_reference_id;
        if (const auto support = std::find_if(source_mesh.edges.begin(),
                source_mesh.edges.end(), [&](const auto& value) {
                    return value.reference.semantic_key == support_key;
                }); support != source_mesh.edges.end()) {
            auto reference = *support;
            reference.reference = {
                active_sketch_id_, "inference:reference", {}};
            preview_edges.push_back(std::move(reference));
        }
    }
    viewer_->set_transient_edges(std::move(preview_edges));
    // The cursor already draws the moving endpoint.  Keep the transient
    // point marker on the accepted first endpoint instead; drawing another
    // marker at the cursor made a snapped endpoint look like two competing
    // points and left the actual first input visually unmarked.
    viewer_->set_transient_points({sketch->world_point(
        (*pending_segment_start_)[0], (*pending_segment_start_)[1])});
    std::string marker;
    const bool endpoint_on_keypoint = endpoint_keypoint_curve.has_value();
    if (endpoint_snap) marker = endpoint_on_keypoint ? "K" : "C";
    const bool common_contact = endpoint_tangent && common_tangent_supports(
        *sketch,pending_segment_start_snap_geometry_id_,endpoint_snap_geometry,
        *pending_segment_start_,preview_position,tangent_tolerance).has_value();
    if (endpoint_tangent) marker = endpoint_on_keypoint && !common_contact ? "K  T" : "C  T";
    else if (endpoint_perpendicular) marker = "C  ⊥";
    if (marker == "C" && inference.kind ==
            zima::sketcher::ConstraintKind::Horizontal) {
        marker = "C  H";
    } else if (marker == "C" && inference.kind ==
            zima::sketcher::ConstraintKind::Vertical) {
        marker = "C  V";
    } else if (marker.empty() && inference.kind ==
               zima::sketcher::ConstraintKind::Horizontal) {
        marker = "H";
    } else if (marker.empty() && inference.kind ==
               zima::sketcher::ConstraintKind::Vertical) {
        marker = "V";
    }
    if (!inference.symmetry_axis_id.empty()) {
        const double local_dx = preview_position[0] - (*pending_segment_start_)[0];
        const double local_dy = preview_position[1] - (*pending_segment_start_)[1];
        const double marker_tolerance = viewer_->world_tolerance_for_pixels(
            1.5 * viewer_->devicePixelRatioF());
        marker = std::abs(local_dy) <= marker_tolerance ? "S  H"
            : std::abs(local_dx) <= marker_tolerance ? "S  V" : "S  ⊥";
    }
    std::vector<std::pair<zima::kernel::Vec3, std::string>> markers;
    if (!marker.empty()) markers.push_back({active_point, std::move(marker)});
    if (common_contact || (!inference.tangent_reference_id.empty() && !endpoint_tangent)) {
        markers.push_back({sketch->world_point(
            (*pending_segment_start_)[0], (*pending_segment_start_)[1]), "C  T"});
    }
    if (!inference.perpendicular_reference_id.empty() &&
        !endpoint_perpendicular) {
        markers.push_back({sketch->world_point(
            (*pending_segment_start_)[0], (*pending_segment_start_)[1]),
            "C  ⊥"});
    }
    if (!inference.equal_length_reference_id.empty()) {
        const auto start_point = sketch->world_point(
            (*pending_segment_start_)[0], (*pending_segment_start_)[1]);
        markers.push_back({
            {(start_point.x + active_point.x) * 0.5,
             (start_point.y + active_point.y) * 0.5,
             (start_point.z + active_point.z) * 0.5},
            inference.parallel_reference_id.empty() ? "=" : "=  ∥"});
    }
    if (!inference.parallel_reference_id.empty() &&
        inference.equal_length_reference_id.empty()) {
        const auto start_point = sketch->world_point(
            (*pending_segment_start_)[0], (*pending_segment_start_)[1]);
        markers.push_back({
            {(start_point.x + active_point.x) * 0.5,
             (start_point.y + active_point.y) * 0.5,
            (start_point.z + active_point.z) * 0.5}, "∥"});
    }
    if (!inference.midpoint_line_reference_id.empty()) {
        const auto start_point = sketch->world_point(
            (*pending_segment_start_)[0], (*pending_segment_start_)[1]);
        markers.push_back({
            {(start_point.x + active_point.x) * 0.5,
             (start_point.y + active_point.y) * 0.5,
             (start_point.z + active_point.z) * 0.5}, "M"});
    }
    // Publish the complete inference state on every mouse move, including
    // the empty state. Leaving the previous labels installed made a stale C
    // survive a small cursor movement and appear beside the current C H/C V.
    viewer_->set_transient_labels(std::move(markers));
}

std::optional<AssemblyWorkspaceWindow::SketchPolylineArcCenterSnap>
AssemblyWorkspaceWindow::inferred_sketch_polyline_arc_center_snap(
    const std::array<double, 2>& end) const {
    if (!automatic_constraint_enabled(zima::sketcher::ConstraintKind::Coincident)) return std::nullopt;
    const auto* sketch = active_sketch();
    if (sketch == nullptr || viewer_ == nullptr || !pending_segment_start_ ||
        pending_polyline_tangent_geometry_id_.empty()) {
        return std::nullopt;
    }
    try {
        auto provisional = *sketch;
        const auto start = std::ranges::find_if(
            provisional.points, [&](const auto& point) {
                return std::hypot(
                    point.x - (*pending_segment_start_)[0],
                    point.y - (*pending_segment_start_)[1]) <= 1.0e-6;
            });
        if (start == provisional.points.end()) return std::nullopt;
        const auto arc_id = provisional.add_tangent_arc(
            start->id, end[0], end[1], pending_polyline_tangent_geometry_id_);
        const auto arc = std::ranges::find_if(
            provisional.arcs, [&](const auto& value) {
                return value.id == arc_id;
            });
        if (arc == provisional.arcs.end()) return std::nullopt;
        const auto* center = provisional.find_point(arc->center_point_id);
        if (center == nullptr) return std::nullopt;
        const double tolerance = viewer_->world_tolerance_for_pixels(
            10.0 * viewer_->devicePixelRatioF());
        std::string axis_id;
        if (std::abs(center->y) <= tolerance) axis_id = "sketch_axis:x";
        if (std::abs(center->x) <= tolerance &&
            (axis_id.empty() || std::abs(center->x) < std::abs(center->y))) {
            axis_id = "sketch_axis:y";
        }
        if (axis_id.empty()) return std::nullopt;

        const double radial_x = center->x - start->x;
        const double radial_y = center->y - start->y;
        const double denominator = axis_id == "sketch_axis:x"
            ? radial_y : radial_x;
        if (std::abs(denominator) <= 1.0e-12) return std::nullopt;
        const double scale = axis_id == "sketch_axis:x"
            ? -start->y / denominator : -start->x / denominator;
        const std::array snapped_center{
            start->x + scale * radial_x,
            start->y + scale * radial_y};
        const double radius = std::hypot(
            snapped_center[0] - start->x,
            snapped_center[1] - start->y);
        const double end_dx = end[0] - snapped_center[0];
        const double end_dy = end[1] - snapped_center[1];
        const double end_length = std::hypot(end_dx, end_dy);
        if (radius <= 1.0e-9 || end_length <= 1.0e-9) return std::nullopt;
        return SketchPolylineArcCenterSnap{{
            snapped_center[0] + radius * end_dx / end_length,
            snapped_center[1] + radius * end_dy / end_length}, axis_id};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool AssemblyWorkspaceWindow::accept_sketch_rectangle_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_rectangle_active_) return false;
    if (sketch_rectangle_axis_selecting_) return true;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_rectangle_corner_) {
        pending_rectangle_corner_ = *position;
        pending_rectangle_corner_snap_geometry_id_ = std::exchange(
            pending_sketch_snap_geometry_id_, {});
        pending_rectangle_corner_snap_kind_ = std::exchange(
            pending_sketch_snap_kind_, std::nullopt);
        state_->setText(tr("Obdélník skici: určete protilehlý roh."));
        return true;
    }
    const auto midpoint_snap = pending_rectangle_axis_id_.empty()
        ? inferred_sketch_rectangle_midpoint_snap(*position)
        : std::nullopt;
    const auto confirmed_opposite = midpoint_snap
        ? midpoint_snap->opposite : *position;
    const auto opposite_snap_geometry_id = std::exchange(
        pending_sketch_snap_geometry_id_, {});
    const auto opposite_snap_kind = std::exchange(
        pending_sketch_snap_kind_, std::nullopt);
    if (pending_rectangle_axis_id_.empty() &&
        (std::abs(confirmed_opposite[0] - (*pending_rectangle_corner_)[0]) <= 1.0e-9 ||
         std::abs(confirmed_opposite[1] - (*pending_rectangle_corner_)[1]) <= 1.0e-9)) {
        state_->setText(tr("Obdélník musí mít nenulovou šířku i výšku."));
        return true;
    }
    if (!mutate_active_sketch([&](auto& target) {
            if (pending_rectangle_axis_id_.empty()) {
                const auto rectangle_ids = target.add_rectangle(
                    (*pending_rectangle_corner_)[0], (*pending_rectangle_corner_)[1],
                    confirmed_opposite[0], confirmed_opposite[1]);
                const auto first_segment = std::find_if(
                    target.segments.begin(), target.segments.end(),
                    [&](const auto& segment) {
                        return segment.id == rectangle_ids.front();
                    });
                const auto second_segment = std::find_if(
                    target.segments.begin(), target.segments.end(),
                    [&](const auto& segment) {
                        return segment.id == rectangle_ids[1];
                    });
                if (first_segment == target.segments.end() ||
                    second_segment == target.segments.end()) {
                    throw std::runtime_error(
                        "Created Sketch rectangle corners cannot be resolved");
                }
                // Adding a constraint solves transactionally and assigns the
                // solved Sketch back into `target`, invalidating every vector
                // iterator.  Copy both corner IDs before applying the first
                // snap; retaining `second_segment` across that solve caused a
                // use-after-free when a rectangle was snapped to an external
                // reference and could crash immediately or on Sketcher OK.
                const auto first_corner_point_id =
                    first_segment->first_point_id;
                const auto opposite_corner_point_id =
                    second_segment->second_point_id;
                const auto apply_snap = [&](const std::string& point_id,
                        const std::string& geometry_id,
                        const auto& kind) {
                    if (!kind || geometry_id.empty()) return;
                    try {
                        if (*kind == zima::sketcher::ConstraintKind::Coincident ||
                            *kind==zima::sketcher::ConstraintKind::Horizontal || *kind==zima::sketcher::ConstraintKind::Vertical) {
                            static_cast<void>(apply_sketch_point_snap(
                                target, point_id, geometry_id, kind));
                        } else if (*kind ==
                                   zima::sketcher::ConstraintKind::PointOnLine) {
                            static_cast<void>(target.add_point_on_line_constraint(
                                point_id, geometry_id));
                        } else if (*kind ==
                                   zima::sketcher::ConstraintKind::PointOnCircle) {
                            static_cast<void>(target.add_point_on_circle_constraint(
                                point_id, geometry_id));
                        }
                    } catch (const std::invalid_argument&) {
                        // A shared corner may already own the same relation.
                    }
                };
                apply_snap(first_corner_point_id,
                    pending_rectangle_corner_snap_geometry_id_,
                    pending_rectangle_corner_snap_kind_);
                apply_snap(opposite_corner_point_id,
                    opposite_snap_geometry_id, opposite_snap_kind);
                if (midpoint_snap) {
                    for (const auto& constraint : midpoint_snap->constraints) {
                        static_cast<void>(target.add_midpoint_on_line_constraint(
                            rectangle_ids[constraint.side_index],
                            constraint.axis_id));
                    }
                }
            } else {
                static_cast<void>(target.add_oriented_rectangle(
                    (*pending_rectangle_corner_)[0], (*pending_rectangle_corner_)[1],
                    (*position)[0], (*position)[1], pending_rectangle_axis_id_));
            }
        })) return true;
    pending_rectangle_corner_.reset();
    pending_rectangle_corner_snap_geometry_id_.clear();
    pending_rectangle_corner_snap_kind_.reset();
    pending_rectangle_axis_id_.clear();
    clear_completed_sketch_interaction();
    preserve_view_on_refresh_ = true;
    refresh_tabs();
    refresh_scene();
    state_->setText(tr("Obdélník vytvořen. Kliknutím určete první roh dalšího."));
    return true;
}

void AssemblyWorkspaceWindow::preview_sketch_rectangle_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_rectangle_active_ || !pending_rectangle_corner_) return;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return;
    const double x0 = (*pending_rectangle_corner_)[0];
    const double y0 = (*pending_rectangle_corner_)[1];
    const auto midpoint_snap = pending_rectangle_axis_id_.empty()
        ? inferred_sketch_rectangle_midpoint_snap(*position)
        : std::nullopt;
    const auto preview_opposite = midpoint_snap
        ? midpoint_snap->opposite : *position;
    const double x1 = preview_opposite[0];
    const double y1 = preview_opposite[1];
    viewer_->set_transient_labels({});
    if (!pending_rectangle_axis_id_.empty()) {
        double axis_x{};
        double axis_y{};
        double dx{};
        double dy{};
        if (pending_rectangle_axis_id_ == "sketch_axis:x") {
            dx = 1.0;
        } else if (pending_rectangle_axis_id_ == "sketch_axis:y") {
            dy = 1.0;
        } else {
            const auto axis = std::find_if(sketch->segments.begin(),
                sketch->segments.end(), [&](const auto& value) {
                    return value.id == pending_rectangle_axis_id_ &&
                        value.construction;
                });
            if (axis == sketch->segments.end()) return;
            const auto* axis_first = sketch->find_point(axis->first_point_id);
            const auto* axis_second = sketch->find_point(axis->second_point_id);
            if (axis_first == nullptr || axis_second == nullptr) return;
            axis_x = axis_first->x;
            axis_y = axis_first->y;
            dx = axis_second->x - axis_first->x;
            dy = axis_second->y - axis_first->y;
        }
        const double length = std::hypot(dx, dy);
        if (length <= 1.0e-12) return;
        const double ux = dx / length;
        const double uy = dy / length;
        const double along = (x1 - x0) * ux + (y1 - y0) * uy;
        const double projection =
            (x0 - axis_x) * ux + (y0 - axis_y) * uy;
        const double foot_x = axis_x + projection * ux;
        const double foot_y = axis_y + projection * uy;
        const std::array mirrored{2.0 * foot_x - x0, 2.0 * foot_y - y0};
        const std::array far_first{x0 + along * ux, y0 + along * uy};
        const std::array far_mirrored{
            mirrored[0] + along * ux, mirrored[1] + along * uy};
        const auto a = sketch->world_point(x0, y0);
        const auto b = sketch->world_point(far_first[0], far_first[1]);
        const auto c = sketch->world_point(far_mirrored[0], far_mirrored[1]);
        const auto d = sketch->world_point(mirrored[0], mirrored[1]);
        viewer_->set_transient_edges({
            {{a, b}, {}}, {{b, c}, {}}, {{c, d}, {}}, {{d, a}, {}}});
        viewer_->set_transient_points({b, d});
        return;
    }
    const auto a = sketch->world_point(x0, y0);
    const auto b = sketch->world_point(x1, y0);
    const auto c = sketch->world_point(x1, y1);
    const auto d = sketch->world_point(x0, y1);
    std::vector<zima::kernel::ViewerEdge> preview_edges{
        {{a, b}, {}}, {{b, c}, {}}, {{c, d}, {}}, {{d, a}, {}}};
    if (midpoint_snap) {
      for (const auto& constraint : midpoint_snap->constraints) {
        if (constraint.axis_id == "sketch_axis:x") {
            preview_edges.push_back({{
                sketch->world_point(std::min(x0, x1) - std::abs(x1 - x0), 0.0),
                sketch->world_point(std::max(x0, x1) + std::abs(x1 - x0), 0.0)},
                {active_sketch_id_, "inference:reference", {}}});
        } else if (constraint.axis_id == "sketch_axis:y") {
            preview_edges.push_back({{
                sketch->world_point(0.0, std::min(y0, y1) - std::abs(y1 - y0)),
                sketch->world_point(0.0, std::max(y0, y1) + std::abs(y1 - y0))},
                {active_sketch_id_, "inference:reference", {}}});
        } else if (const auto axis = std::ranges::find_if(sketch->segments,
                [&](const auto& value) {
                    return value.id == constraint.axis_id;
                }); axis != sketch->segments.end()) {
            const auto* first = sketch->find_point(axis->first_point_id);
            const auto* second = sketch->find_point(axis->second_point_id);
            if (first != nullptr && second != nullptr) {
                preview_edges.push_back({{
                    sketch->world_point(first->x, first->y),
                    sketch->world_point(second->x, second->y)},
                    {active_sketch_id_, "inference:reference", {}}});
            }
        }
      }
    }
    viewer_->set_transient_edges(std::move(preview_edges));
    viewer_->set_transient_points({b, d});
    if (midpoint_snap) {
        std::vector<std::pair<zima::kernel::Vec3, std::string>> labels;
        labels.reserve(midpoint_snap->constraints.size());
        for (const auto& constraint : midpoint_snap->constraints) {
            const std::array midpoint = constraint.side_index == 0
                ? std::array{(x0 + x1) * 0.5, y0}
                : std::array{x1, (y0 + y1) * 0.5};
            labels.emplace_back(
                sketch->world_point(midpoint[0], midpoint[1]), "M");
        }
        viewer_->set_transient_labels(std::move(labels));
    }
}

std::optional<AssemblyWorkspaceWindow::SketchRectangleMidpointSnap>
AssemblyWorkspaceWindow::inferred_sketch_rectangle_midpoint_snap(
    const std::array<double, 2>& opposite) const {
    if (!automatic_constraint_enabled(zima::sketcher::ConstraintKind::Midpoint)) return std::nullopt;
    const auto* sketch = active_sketch();
    if (sketch == nullptr || viewer_ == nullptr || !pending_rectangle_corner_) {
        return std::nullopt;
    }
    const auto& first = *pending_rectangle_corner_;
    const double tolerance = viewer_->world_tolerance_for_pixels(
        10.0 * viewer_->devicePixelRatioF());
    struct AxisCandidate {
        std::string axis_id;
        double coordinate{};
        double distance{};
        std::size_t side_index{};
    };
    std::array<std::optional<AxisCandidate>, 2> best;
    const auto offer = [&](const std::string& axis_id,
            const std::array<double, 2>& axis_origin,
            const std::array<double, 2>& axis_direction) {
        const double length = std::hypot(
            axis_direction[0], axis_direction[1]);
        if (length <= 1.0e-12) return;
        const double ux = axis_direction[0] / length;
        const double uy = axis_direction[1] / length;
        AxisCandidate candidate;
        candidate.axis_id = axis_id;
        double distance{};
        if (std::abs(uy) <= 1.0e-8) {
            const double axis_y = axis_origin[1];
            distance = std::abs((first[1] + opposite[1]) * 0.5 - axis_y);
            candidate.coordinate = 2.0 * axis_y - first[1];
            candidate.side_index = 1;
        } else if (std::abs(ux) <= 1.0e-8) {
            const double axis_x = axis_origin[0];
            distance = std::abs((first[0] + opposite[0]) * 0.5 - axis_x);
            candidate.coordinate = 2.0 * axis_x - first[0];
            candidate.side_index = 0;
        } else {
            return;
        }
        candidate.distance = distance;
        const std::size_t coordinate_index = candidate.side_index == 0 ? 0 : 1;
        if (distance <= tolerance &&
            std::abs(candidate.coordinate - first[coordinate_index]) > 1.0e-9 &&
            (!best[coordinate_index] ||
             distance < best[coordinate_index]->distance - 1.0e-12)) {
            best[coordinate_index] = std::move(candidate);
        }
    };
    offer("sketch_axis:x", {0.0, 0.0}, {1.0, 0.0});
    offer("sketch_axis:y", {0.0, 0.0}, {0.0, 1.0});
    for (const auto& segment : sketch->segments) {
        if (!segment.construction) continue;
        const auto* axis_first = sketch->find_point(segment.first_point_id);
        const auto* axis_second = sketch->find_point(segment.second_point_id);
        if (axis_first == nullptr || axis_second == nullptr) continue;
        offer(segment.id, {axis_first->x, axis_first->y},
            {axis_second->x - axis_first->x,
             axis_second->y - axis_first->y});
    }
    SketchRectangleMidpointSnap result{opposite, {}};
    for (std::size_t coordinate = 0; coordinate < best.size(); ++coordinate) {
        if (!best[coordinate]) continue;
        result.opposite[coordinate] = best[coordinate]->coordinate;
        result.constraints.push_back({
            best[coordinate]->axis_id, best[coordinate]->side_index});
    }
    if (result.constraints.empty() ||
        std::abs(result.opposite[0] - first[0]) <= 1.0e-9 ||
        std::abs(result.opposite[1] - first[1]) <= 1.0e-9) {
        return std::nullopt;
    }
    return result;
}

void AssemblyWorkspaceWindow::accept_sketch_rectangle_axis(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_rectangle_axis_selecting_ ||
        candidate.owner_id != active_sketch_id_ ||
        (candidate.kind != zima::viewer::CandidateKind::SketchSegment &&
         candidate.kind != zima::viewer::CandidateKind::SketchAxis)) return;
    if (candidate.kind == zima::viewer::CandidateKind::SketchAxis) {
        if (candidate.semantic_key != "sketch_axis:x" &&
            candidate.semantic_key != "sketch_axis:y") return;
        pending_rectangle_axis_id_ = candidate.semantic_key;
    } else {
        if (!candidate.semantic_key.starts_with("segment:")) return;
        pending_rectangle_axis_id_ = candidate.semantic_key.substr(8);
    }
    sketch_rectangle_axis_selecting_ = false;
    viewer_->set_candidate_filter({});
    preserve_view_on_refresh_ = true;
    refresh_scene();
    state_->setText(tr(
        "Orientovaný obdélník: určete délku podél vybrané konstrukční osy."));
}

bool AssemblyWorkspaceWindow::accept_sketch_polygon_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_polygon_active_) return false;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_polygon_center_) {
        pending_polygon_center_ = *position;
        state_->setText(tr("Pravidelný %1úhelník: určete vrchol na pomocné kružnici.")
            .arg(sketch_polygon_sides_));
        return true;
    }
    const double radius = std::hypot(
        (*position)[0] - (*pending_polygon_center_)[0],
        (*position)[1] - (*pending_polygon_center_)[1]);
    if (radius <= 1.0e-9) {
        state_->setText(tr("Mnohoúhelník musí mít nenulový poloměr."));
        return true;
    }
    try {
        if (!mutate_active_sketch([&](auto& target) {
                static_cast<void>(target.add_regular_polygon(
                    (*pending_polygon_center_)[0], (*pending_polygon_center_)[1],
                    (*position)[0], (*position)[1], sketch_polygon_sides_));
            })) return true;
        pending_polygon_center_.reset();
        clear_completed_sketch_interaction();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr(
            "Pravidelný %1úhelník vytvořen. Kliknutím určete střed dalšího.")
            .arg(sketch_polygon_sides_));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
    return true;
}

void AssemblyWorkspaceWindow::preview_sketch_polygon_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_polygon_active_ || !pending_polygon_center_) return;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) {
        viewer_->set_transient_edges({});
        return;
    }
    const double dx = (*position)[0] - (*pending_polygon_center_)[0];
    const double dy = (*position)[1] - (*pending_polygon_center_)[1];
    const double radius = std::hypot(dx, dy);
    if (radius <= 1.0e-9) {
        viewer_->set_transient_edges({});
        return;
    }
    const double start_angle = std::atan2(dy, dx);
    constexpr double full_turn = 2.0 * 3.14159265358979323846;
    std::vector<zima::kernel::Vec3> vertices;
    vertices.reserve(sketch_polygon_sides_);
    for (unsigned index = 0; index < sketch_polygon_sides_; ++index) {
        const double angle = start_angle + full_turn *
            static_cast<double>(index) /
            static_cast<double>(sketch_polygon_sides_);
        vertices.push_back(sketch->world_point(
            (*pending_polygon_center_)[0] + radius * std::cos(angle),
            (*pending_polygon_center_)[1] + radius * std::sin(angle)));
    }
    std::vector<zima::kernel::ViewerEdge> preview;
    preview.reserve(sketch_polygon_sides_ + 1);
    for (unsigned index = 0; index < sketch_polygon_sides_; ++index) {
        preview.push_back({{
            vertices[index], vertices[(index + 1) % sketch_polygon_sides_]}, {}});
    }
    zima::kernel::ViewerEdge support_circle;
    constexpr std::size_t samples = 96;
    support_circle.points.reserve(samples + 1);
    for (std::size_t sample = 0; sample <= samples; ++sample) {
        const double angle = full_turn * static_cast<double>(sample) /
            static_cast<double>(samples);
        support_circle.points.push_back(sketch->world_point(
            (*pending_polygon_center_)[0] + radius * std::cos(angle),
            (*pending_polygon_center_)[1] + radius * std::sin(angle)));
    }
    preview.push_back(std::move(support_circle));
    viewer_->set_transient_edges(std::move(preview));
}

bool AssemblyWorkspaceWindow::accept_sketch_circle_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_circle_active_) return false;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_circle_center_) {
        pending_circle_center_ = *position;
        pending_circle_tangent_inference_.reset();
        pending_curve_point_snaps_.clear();
        pending_curve_point_snaps_.push_back({
            std::exchange(pending_sketch_snap_geometry_id_, {}),
            std::exchange(pending_sketch_snap_kind_, std::nullopt)});
        state_->setText(tr("Kružnice skici: určete bod na obvodu."));
        return true;
    }
    pending_curve_point_snaps_.push_back({
        std::exchange(pending_sketch_snap_geometry_id_, {}),
        std::exchange(pending_sketch_snap_kind_, std::nullopt)});
    const auto& rim_snap = pending_curve_point_snaps_.back();
    const bool has_rim_contact_snap = !rim_snap.first.empty() &&
        rim_snap.second.has_value();
    double radius = std::hypot(
        (*position)[0] - (*pending_circle_center_)[0],
        (*position)[1] - (*pending_circle_center_)[1]);
    // A concrete C or C+T contact has precedence over a coincidentally equal
    // numeric radius. Equality must not move the circle away from that contact.
    const auto tangent = pending_circle_tangent_inference_
        ? pending_circle_tangent_inference_
        : inferred_sketch_circle_tangent(*position);
    const auto equal_radius = tangent || has_rim_contact_snap
        ? std::optional<std::pair<double, std::string>>{}
        : inferred_sketch_circle_radius(*position);
    if (equal_radius) radius = equal_radius->first;
    else if (tangent) radius = tangent->radius;
    if (radius <= 1.0e-9) {
        state_->setText(tr("Kružnice musí mít nenulový poloměr."));
        return true;
    }
    try {
        if (!mutate_active_sketch([&](auto& target) {
            const auto created = target.add_circle(
                (*pending_circle_center_)[0], (*pending_circle_center_)[1], radius);
            const auto circle = std::ranges::find_if(target.circles,
                [&](const auto& value) { return value.id == created; });
            if (circle == target.circles.end()) return;
            if (!pending_curve_point_snaps_.empty()) {
                apply_sketch_point_snap(target, circle->center_point_id,
                    pending_curve_point_snaps_[0].first,
                    pending_curve_point_snaps_[0].second);
            }
            // A free second click only defines the radius. Persist a separate
            // contact point when the cursor explicitly offered a snap or C+T.
            if (has_rim_contact_snap || tangent) {
                std::array rim_position{(*position)[0], (*position)[1]};
                if (tangent) rim_position = tangent->contact;
                auto rim_point = zima::sketcher::Sketch::create_point(
                    rim_position[0], rim_position[1]);
                const auto rim_point_id = rim_point.id;
                target.points.push_back(std::move(rim_point));
                // Tangency is added before the two incidence relations because
                // the incremental solver can otherwise classify it as redundant.
                if (tangent) {
                    static_cast<void>(target.add_tangent_constraint(
                        tangent->support_id, created, rim_point_id));
                }
                static_cast<void>(target.add_point_on_circle_constraint(
                    rim_point_id, created));
                const bool tangent_snap_already_owned = tangent &&
                    has_rim_contact_snap &&
                    rim_snap.first == tangent->support_id &&
                    rim_snap.second ==
                        zima::sketcher::ConstraintKind::PointOnLine;
                if (has_rim_contact_snap && !tangent_snap_already_owned) {
                    apply_sketch_point_snap(target, rim_point_id,
                        rim_snap.first, rim_snap.second);
                }
                if (tangent) {
                    apply_sketch_point_snap(target, rim_point_id,
                        tangent->support_id,
                        zima::sketcher::ConstraintKind::PointOnLine);
                }
            }
            if (equal_radius) {
                static_cast<void>(target.add_equal_radius_constraint(
                    equal_radius->second, created));
            }
        })) return true;
    } catch (const std::exception& error) {
        pending_circle_tangent_inference_.reset();
        if (pending_curve_point_snaps_.size() > 1) {
            pending_curve_point_snaps_.resize(1);
        }
        state_->setText(tr("Kružnici nelze vytvořit: %1")
            .arg(QString::fromUtf8(error.what())));
        return true;
    }
    pending_circle_center_.reset();
    pending_circle_tangent_inference_.reset();
    pending_curve_point_snaps_.clear();
    clear_completed_sketch_interaction();
    preserve_view_on_refresh_ = true;
    refresh_tabs();
    refresh_scene();
    state_->setText(tr("Kružnice vytvořena. Kliknutím určete střed další kružnice."));
    return true;
}

void AssemblyWorkspaceWindow::preview_sketch_circle_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_circle_active_ || !pending_circle_center_) return;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return;
    double radius = std::hypot(
        (*position)[0] - (*pending_circle_center_)[0],
        (*position)[1] - (*pending_circle_center_)[1]);
    const auto tangent = inferred_sketch_circle_tangent(*position);
    pending_circle_tangent_inference_ = tangent;
    const auto equal_radius = tangent
        ? std::optional<std::pair<double, std::string>>{}
        : inferred_sketch_circle_radius(*position);
    if (equal_radius) radius = equal_radius->first;
    else if (tangent) radius = tangent->radius;
    zima::kernel::ViewerEdge preview;
    constexpr std::size_t samples = 96;
    for (std::size_t sample = 0; sample <= samples; ++sample) {
        const double angle = 2.0 * 3.14159265358979323846 *
            static_cast<double>(sample) / static_cast<double>(samples);
        preview.points.push_back(sketch->world_point(
            (*pending_circle_center_)[0] + radius * std::cos(angle),
            (*pending_circle_center_)[1] + radius * std::sin(angle)));
    }
    viewer_->set_transient_edges({std::move(preview)});
    if (equal_radius) {
        std::array rim_position{
            (*pending_circle_center_)[0] + radius,
            (*pending_circle_center_)[1]};
        const auto rim = sketch->world_point(rim_position[0], rim_position[1]);
        viewer_->set_transient_labels({{rim, "="}});
    } else {
        viewer_->set_transient_labels({});
    }
}

std::optional<std::pair<double, std::string>>
AssemblyWorkspaceWindow::inferred_sketch_circle_radius(
    const std::array<double, 2>& rim_position) const {
    if (!automatic_constraint_enabled(zima::sketcher::ConstraintKind::EqualLength)) return std::nullopt;
    const auto* sketch = active_sketch();
    if (sketch == nullptr || !pending_circle_center_ || viewer_ == nullptr) {
        return std::nullopt;
    }
    const double requested = std::hypot(
        rim_position[0] - (*pending_circle_center_)[0],
        rim_position[1] - (*pending_circle_center_)[1]);
    const double tolerance = viewer_->world_tolerance_for_pixels(
        16.0 * viewer_->devicePixelRatioF());
    std::optional<std::pair<double, std::string>> best;
    double best_difference = tolerance;
    const auto offer = [&](double radius, const std::string& id) {
        const double difference = std::abs(radius - requested);
        if (radius > 1.0e-12 && difference <= best_difference) {
            best_difference = difference;
            best = std::pair{radius, id};
        }
    };
    for (const auto& circle : sketch->circles) offer(circle.radius, circle.id);
    for (const auto& arc : sketch->arcs) offer(arc.radius, arc.id);
    for (const auto& reference : sketch->external_references) {
        if (const auto radius = sketch->circular_radius(reference.id))
            offer(*radius, reference.id);
    }
    return best;
}

std::optional<AssemblyWorkspaceWindow::SketchCircleTangentInference>
AssemblyWorkspaceWindow::inferred_sketch_circle_tangent(
    const std::array<double, 2>& rim_position) const {
    if (!automatic_constraint_enabled(zima::sketcher::ConstraintKind::Tangent)) return std::nullopt;
    const auto* sketch = active_sketch();
    if (sketch == nullptr || !pending_circle_center_ || viewer_ == nullptr) {
        return std::nullopt;
    }
    const double tolerance = viewer_->world_tolerance_for_pixels(
        sketch_circle_tangent_contact_pixels * viewer_->devicePixelRatioF());
    std::optional<SketchCircleTangentInference> best;
    double best_distance = tolerance;
    const auto offer_line = [&](const std::array<double, 2>& first,
            const std::array<double, 2>& second, const std::string& id,
            bool bounded) {
        const double dx = second[0] - first[0];
        const double dy = second[1] - first[1];
        const double length_squared = dx * dx + dy * dy;
        if (length_squared <= 1.0e-18) return;
        double parameter =
            (((*pending_circle_center_)[0] - first[0]) * dx +
             ((*pending_circle_center_)[1] - first[1]) * dy) /
            length_squared;
        if (bounded && (parameter < 0.0 || parameter > 1.0)) return;
        const std::array foot{
            first[0] + parameter * dx, first[1] + parameter * dy};
        const double cursor_distance = std::hypot(
            rim_position[0] - foot[0], rim_position[1] - foot[1]);
        if (cursor_distance > best_distance) return;
        const double radius = std::hypot(
            foot[0] - (*pending_circle_center_)[0],
            foot[1] - (*pending_circle_center_)[1]);
        if (radius <= 1.0e-12) return;
        best_distance = cursor_distance;
        best = SketchCircleTangentInference{radius, id, foot};
    };
    offer_line({0.0, 0.0}, {1.0, 0.0}, "sketch_axis:x", false);
    offer_line({0.0, 0.0}, {0.0, 1.0}, "sketch_axis:y", false);
    for (const auto& line : sketch->segments) {
        const auto* first = sketch->find_point(line.first_point_id);
        const auto* second = sketch->find_point(line.second_point_id);
        if (first == nullptr || second == nullptr) continue;
        offer_line({first->x, first->y}, {second->x, second->y}, line.id,
            !line.centerline);
    }
    return best;
}

bool AssemblyWorkspaceWindow::accept_sketch_arc_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_arc_active_) return false;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_arc_center_) {
        pending_arc_center_ = *position;
        pending_curve_point_snaps_.push_back({
            std::exchange(pending_sketch_snap_geometry_id_, {}),
            std::exchange(pending_sketch_snap_kind_, std::nullopt)});
        state_->setText(tr("Oblouk skici: určete počáteční bod."));
        return true;
    }
    if (!pending_arc_start_) {
        if (std::hypot((*position)[0] - (*pending_arc_center_)[0],
                       (*position)[1] - (*pending_arc_center_)[1]) <= 1.0e-9) {
            state_->setText(tr("Počáteční bod oblouku nesmí ležet ve středu."));
            return true;
        }
        pending_arc_start_ = *position;
        pending_curve_point_snaps_.push_back({
            std::exchange(pending_sketch_snap_geometry_id_, {}),
            std::exchange(pending_sketch_snap_kind_, std::nullopt)});
        state_->setText(tr(
            "Oblouk skici: určete koncový bod; RMB přepíná směr."));
        return true;
    }
    if (std::hypot((*position)[0] - (*pending_arc_center_)[0],
                   (*position)[1] - (*pending_arc_center_)[1]) <= 1.0e-9) {
        state_->setText(tr("Koncový bod oblouku nesmí ležet ve středu."));
        return true;
    }
    // The cursor chooses only the terminal angle. The live preview already
    // lies on the circle fixed by center + start; commit that exact projected
    // point as well. Passing the raw cursor position produced an endpoint at
    // a different radius and made an ordinary third LMB click fail model
    // validation unless it happened to land mathematically on the circle.
    const double radius = std::hypot(
        (*pending_arc_start_)[0] - (*pending_arc_center_)[0],
        (*pending_arc_start_)[1] - (*pending_arc_center_)[1]);
    const double end_dx = (*position)[0] - (*pending_arc_center_)[0];
    const double end_dy = (*position)[1] - (*pending_arc_center_)[1];
    const double end_length = std::hypot(end_dx, end_dy);
    const std::array projected_end{
        (*pending_arc_center_)[0] + radius * end_dx / end_length,
        (*pending_arc_center_)[1] + radius * end_dy / end_length};
    pending_curve_point_snaps_.push_back({
        std::exchange(pending_sketch_snap_geometry_id_, {}),
        std::exchange(pending_sketch_snap_kind_, std::nullopt)});
    try {
        if (!mutate_active_sketch([&](auto& target) {
                const auto arc_id = target.add_arc(
                    (*pending_arc_center_)[0], (*pending_arc_center_)[1],
                    (*pending_arc_start_)[0], (*pending_arc_start_)[1],
                    projected_end[0], projected_end[1], false, 1.0e-6,
                    sketch_arc_clockwise_);
                const auto arc = std::ranges::find_if(target.arcs,
                    [&](const auto& value) { return value.id == arc_id; });
                if (arc == target.arcs.end()) return;
                const std::array point_ids{arc->center_point_id,
                    arc->start_point_id, arc->end_point_id};
                for (std::size_t index = 0;
                     index < point_ids.size() &&
                         index < pending_curve_point_snaps_.size(); ++index) {
                    apply_sketch_point_snap(target, point_ids[index],
                        pending_curve_point_snaps_[index].first,
                        pending_curve_point_snaps_[index].second);
                }
            })) return true;
        pending_arc_center_.reset();
        pending_arc_start_.reset();
        sketch_arc_clockwise_ = false;
        pending_curve_point_snaps_.clear();
        clear_completed_sketch_interaction();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr("Oblouk vytvořen. Kliknutím určete střed dalšího."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
    return true;
}

void AssemblyWorkspaceWindow::preview_sketch_arc_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_arc_active_ || !pending_arc_center_) return;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return;
    if (!pending_arc_start_) {
        viewer_->set_transient_edges({{{
            sketch->world_point((*pending_arc_center_)[0], (*pending_arc_center_)[1]),
            sketch->world_point((*position)[0], (*position)[1])}, {}}});
        viewer_->set_transient_points({sketch->world_point(
            (*pending_arc_center_)[0], (*pending_arc_center_)[1])});
        return;
    }
    const double radius = std::hypot(
        (*pending_arc_start_)[0] - (*pending_arc_center_)[0],
        (*pending_arc_start_)[1] - (*pending_arc_center_)[1]);
    double start_angle = std::atan2(
        (*pending_arc_start_)[1] - (*pending_arc_center_)[1],
        (*pending_arc_start_)[0] - (*pending_arc_center_)[0]);
    double end_angle = std::atan2(
        (*position)[1] - (*pending_arc_center_)[1],
        (*position)[0] - (*pending_arc_center_)[0]);
    constexpr double full_turn = 2.0 * 3.14159265358979323846;
    if (sketch_arc_clockwise_) std::swap(start_angle, end_angle);
    while (end_angle <= start_angle) end_angle += full_turn;
    const double sweep = end_angle - start_angle;
    const auto samples = std::max<std::size_t>(2,
        static_cast<std::size_t>(std::ceil(96.0 * sweep / full_turn)));
    zima::kernel::ViewerEdge preview;
    for (std::size_t sample = 0; sample <= samples; ++sample) {
        const double angle = start_angle + sweep *
            static_cast<double>(sample) / static_cast<double>(samples);
        preview.points.push_back(sketch->world_point(
            (*pending_arc_center_)[0] + radius * std::cos(angle),
            (*pending_arc_center_)[1] + radius * std::sin(angle)));
    }
    viewer_->set_transient_edges({std::move(preview)});
    viewer_->set_transient_points({
        sketch->world_point(
            (*pending_arc_center_)[0], (*pending_arc_center_)[1]),
        sketch->world_point(
            (*pending_arc_start_)[0], (*pending_arc_start_)[1])});
}

bool AssemblyWorkspaceWindow::accept_sketch_ellipse_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_ellipse_active_) return false;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_ellipse_center_) {
        pending_ellipse_center_ = *position;
        pending_curve_point_snaps_.push_back({
            std::exchange(pending_sketch_snap_geometry_id_, {}),
            std::exchange(pending_sketch_snap_kind_, std::nullopt)});
        state_->setText(tr("Elipsa skici: určete konec hlavní poloosy."));
        return true;
    }
    if (!pending_ellipse_major_) {
        if (std::hypot((*position)[0] - (*pending_ellipse_center_)[0],
                       (*position)[1] - (*pending_ellipse_center_)[1]) <= 1.0e-9) {
            state_->setText(tr("Hlavní poloosa elipsy musí mít nenulovou délku."));
            return true;
        }
        pending_ellipse_major_ = *position;
        pending_curve_point_snaps_.push_back({
            std::exchange(pending_sketch_snap_geometry_id_, {}),
            std::exchange(pending_sketch_snap_kind_, std::nullopt)});
        state_->setText(tr("Elipsa skici: určete délku vedlejší poloosy."));
        return true;
    }
    pending_curve_point_snaps_.push_back({
        std::exchange(pending_sketch_snap_geometry_id_, {}),
        std::exchange(pending_sketch_snap_kind_, std::nullopt)});
    try {
        if (!mutate_active_sketch([&](auto& target) {
                const auto ellipse_id = target.add_ellipse(
                    (*pending_ellipse_center_)[0], (*pending_ellipse_center_)[1],
                    (*pending_ellipse_major_)[0], (*pending_ellipse_major_)[1],
                    (*position)[0], (*position)[1]);
                const auto ellipse = std::ranges::find_if(target.ellipses,
                    [&](const auto& value) { return value.id == ellipse_id; });
                if (ellipse == target.ellipses.end()) return;
                const std::array point_ids{ellipse->center_point_id,
                    ellipse->major_point_id, ellipse->minor_point_id};
                for (std::size_t index = 0;
                     index < point_ids.size() &&
                         index < pending_curve_point_snaps_.size(); ++index) {
                    apply_sketch_point_snap(target, point_ids[index],
                        pending_curve_point_snaps_[index].first,
                        pending_curve_point_snaps_[index].second);
                }
            })) return true;
        pending_ellipse_center_.reset();
        pending_ellipse_major_.reset();
        pending_curve_point_snaps_.clear();
        clear_completed_sketch_interaction();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr("Elipsa vytvořena. Kliknutím určete střed další elipsy."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
    return true;
}

void AssemblyWorkspaceWindow::preview_sketch_ellipse_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_ellipse_active_ || !pending_ellipse_center_) return;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return;
    if (!pending_ellipse_major_) {
        viewer_->set_transient_edges({{{
            sketch->world_point((*pending_ellipse_center_)[0], (*pending_ellipse_center_)[1]),
            sketch->world_point((*position)[0], (*position)[1])}, {}}});
        viewer_->set_transient_points({sketch->world_point(
            (*pending_ellipse_center_)[0], (*pending_ellipse_center_)[1])});
        return;
    }
    const double dx = (*pending_ellipse_major_)[0] - (*pending_ellipse_center_)[0];
    const double dy = (*pending_ellipse_major_)[1] - (*pending_ellipse_center_)[1];
    const double major_radius = std::hypot(dx, dy);
    const double rotation = std::atan2(dy, dx);
    const double minor_radius = std::abs(
        -((*position)[0] - (*pending_ellipse_center_)[0]) * std::sin(rotation) +
         ((*position)[1] - (*pending_ellipse_center_)[1]) * std::cos(rotation));
    if (major_radius <= 1.0e-9 || minor_radius <= 1.0e-9) return;
    zima::kernel::ViewerEdge preview;
    constexpr std::size_t samples = 96;
    preview.points.reserve(samples + 1);
    for (std::size_t sample = 0; sample <= samples; ++sample) {
        const double parameter = 2.0 * 3.14159265358979323846 *
            static_cast<double>(sample) / static_cast<double>(samples);
        const double local_x = major_radius * std::cos(parameter);
        const double local_y = minor_radius * std::sin(parameter);
        preview.points.push_back(sketch->world_point(
            (*pending_ellipse_center_)[0] + local_x * std::cos(rotation) -
                local_y * std::sin(rotation),
            (*pending_ellipse_center_)[1] + local_x * std::sin(rotation) +
                local_y * std::cos(rotation)));
    }
    viewer_->set_transient_edges({std::move(preview)});
    viewer_->set_transient_points({
        sketch->world_point(
            (*pending_ellipse_center_)[0], (*pending_ellipse_center_)[1]),
        sketch->world_point(
            (*pending_ellipse_major_)[0], (*pending_ellipse_major_)[1])});
}

bool AssemblyWorkspaceWindow::accept_sketch_elliptical_arc_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_elliptical_arc_active_) return false;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_elliptical_arc_center_) {
        pending_elliptical_arc_center_ = *position;
        pending_curve_point_snaps_.push_back({
            std::exchange(pending_sketch_snap_geometry_id_, {}),
            std::exchange(pending_sketch_snap_kind_, std::nullopt)});
        state_->setText(tr(
            "Eliptický oblouk: určete konec hlavní poloosy."));
        return true;
    }
    if (!pending_elliptical_arc_major_) {
        if (std::hypot(
                (*position)[0] - (*pending_elliptical_arc_center_)[0],
                (*position)[1] - (*pending_elliptical_arc_center_)[1]) <=
            1.0e-9) {
            state_->setText(tr(
                "Hlavní poloosa eliptického oblouku musí mít nenulovou délku."));
            return true;
        }
        pending_elliptical_arc_major_ = *position;
        pending_curve_point_snaps_.push_back({
            std::exchange(pending_sketch_snap_geometry_id_, {}),
            std::exchange(pending_sketch_snap_kind_, std::nullopt)});
        state_->setText(tr(
            "Eliptický oblouk: určete délku a stranu vedlejší poloosy."));
        return true;
    }
    if (!pending_elliptical_arc_minor_) {
        const bool exact_keypoint =
            pending_sketch_snap_geometry_id_.starts_with("sketch_keypoint:");
        const auto minor = exact_keypoint
            ? std::optional{*position}
            : projected_ellipse_minor(
                *pending_elliptical_arc_center_,
                *pending_elliptical_arc_major_, *position);
        if (!minor) {
            state_->setText(tr(
                "Vedlejší poloosa eliptického oblouku musí mít nenulovou délku."));
            return true;
        }
        pending_elliptical_arc_minor_ = *minor;
        pending_curve_point_snaps_.push_back({
            std::exchange(pending_sketch_snap_geometry_id_, {}),
            std::exchange(pending_sketch_snap_kind_, std::nullopt)});
        const double major_x =
            (*pending_elliptical_arc_major_)[0] -
            (*pending_elliptical_arc_center_)[0];
        const double major_y =
            (*pending_elliptical_arc_major_)[1] -
            (*pending_elliptical_arc_center_)[1];
        const double minor_x =
            (*pending_elliptical_arc_minor_)[0] -
            (*pending_elliptical_arc_center_)[0];
        const double minor_y =
            (*pending_elliptical_arc_minor_)[1] -
            (*pending_elliptical_arc_center_)[1];
        pending_elliptical_arc_reversed_ =
            major_x * minor_y - major_y * minor_x < 0.0;
        state_->setText(tr(
            "Eliptický oblouk: určete počáteční bod na elipse."));
        return true;
    }
    const auto projected = projected_ellipse_position(
        *pending_elliptical_arc_center_, *pending_elliptical_arc_major_,
        *pending_elliptical_arc_minor_, *position);
    if (!projected) {
        state_->setText(tr(
            "Bod eliptického oblouku nesmí ležet ve středu."));
        return true;
    }
    const bool exact_keypoint =
        pending_sketch_snap_geometry_id_.starts_with("sketch_keypoint:");
    const auto confirmed_position = exact_keypoint
        ? *position : projected->position;
    if (!pending_elliptical_arc_start_) {
        pending_elliptical_arc_start_ = confirmed_position;
        pending_curve_point_snaps_.push_back({
            std::exchange(pending_sketch_snap_geometry_id_, {}),
            std::exchange(pending_sketch_snap_kind_, std::nullopt)});
        state_->setText(pending_elliptical_arc_reversed_
            ? tr("Eliptický oblouk: určete koncový bod ve směru hodinových ručiček.")
            : tr("Eliptický oblouk: určete koncový bod proti směru hodinových ručiček."));
        return true;
    }
    if (std::hypot(
            confirmed_position[0] - (*pending_elliptical_arc_start_)[0],
            confirmed_position[1] - (*pending_elliptical_arc_start_)[1]) <=
        1.0e-9) {
        state_->setText(tr(
            "Počáteční a koncový bod eliptického oblouku musí být odlišné."));
        return true;
    }
    pending_curve_point_snaps_.push_back({
        std::exchange(pending_sketch_snap_geometry_id_, {}),
        std::exchange(pending_sketch_snap_kind_, std::nullopt)});
    try {
        if (!mutate_active_sketch([&](auto& target) {
            const auto arc_id = target.add_elliptical_arc(
            (*pending_elliptical_arc_center_)[0],
            (*pending_elliptical_arc_center_)[1],
            (*pending_elliptical_arc_major_)[0],
            (*pending_elliptical_arc_major_)[1],
            (*pending_elliptical_arc_minor_)[0],
            (*pending_elliptical_arc_minor_)[1],
            (*pending_elliptical_arc_start_)[0],
            (*pending_elliptical_arc_start_)[1],
            confirmed_position[0], confirmed_position[1],
            pending_elliptical_arc_reversed_);
            const auto arc = std::ranges::find_if(target.elliptical_arcs,
                [&](const auto& value) { return value.id == arc_id; });
            if (arc == target.elliptical_arcs.end()) return;
            const std::array point_ids{arc->center_point_id,
                arc->major_point_id, arc->minor_point_id,
                arc->start_point_id, arc->end_point_id};
            for (std::size_t index = 0;
                 index < point_ids.size() &&
                     index < pending_curve_point_snaps_.size(); ++index) {
                apply_sketch_point_snap(target, point_ids[index],
                    pending_curve_point_snaps_[index].first,
                    pending_curve_point_snaps_[index].second);
            }
        })) return true;
        pending_elliptical_arc_center_.reset();
        pending_elliptical_arc_major_.reset();
        pending_elliptical_arc_minor_.reset();
        pending_elliptical_arc_start_.reset();
        pending_elliptical_arc_reversed_ = false;
        pending_curve_point_snaps_.clear();
        clear_completed_sketch_interaction();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr(
            "Eliptický oblouk vytvořen. Kliknutím určete střed dalšího."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
    return true;
}

void AssemblyWorkspaceWindow::preview_sketch_elliptical_arc_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_elliptical_arc_active_ ||
        !pending_elliptical_arc_center_) return;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
    const auto cursor = sketch->intersect_ray(origin, direction);
    if (!cursor) return;
    if (!pending_elliptical_arc_major_) {
        viewer_->set_transient_edges({{{
            sketch->world_point(
                (*pending_elliptical_arc_center_)[0],
                (*pending_elliptical_arc_center_)[1]),
            sketch->world_point((*cursor)[0], (*cursor)[1])}, {}}});
        viewer_->set_transient_points({sketch->world_point(
            (*pending_elliptical_arc_center_)[0],
            (*pending_elliptical_arc_center_)[1])});
        return;
    }
    SketchPosition minor;
    if (pending_elliptical_arc_minor_) {
        minor = *pending_elliptical_arc_minor_;
    } else {
        const auto projected_minor = projected_ellipse_minor(
            *pending_elliptical_arc_center_, *pending_elliptical_arc_major_,
            *cursor);
        if (!projected_minor) {
            viewer_->set_transient_edges({});
            return;
        }
        minor = *projected_minor;
    }
    std::vector<zima::kernel::ViewerEdge> preview;
    zima::kernel::ViewerEdge axes;
    axes.construction = true;
    axes.points = {
        sketch->world_point(
            (*pending_elliptical_arc_center_)[0],
            (*pending_elliptical_arc_center_)[1]),
        sketch->world_point(
            (*pending_elliptical_arc_major_)[0],
            (*pending_elliptical_arc_major_)[1]),
        sketch->world_point(
            (*pending_elliptical_arc_center_)[0],
            (*pending_elliptical_arc_center_)[1]),
        sketch->world_point(minor[0], minor[1])};
    preview.push_back(std::move(axes));
    std::vector<zima::kernel::Vec3> accepted_points{
        sketch->world_point(
            (*pending_elliptical_arc_center_)[0],
            (*pending_elliptical_arc_center_)[1]),
        sketch->world_point(
            (*pending_elliptical_arc_major_)[0],
            (*pending_elliptical_arc_major_)[1])};
    if (pending_elliptical_arc_minor_) {
        accepted_points.push_back(sketch->world_point(minor[0], minor[1]));
    }
    if (pending_elliptical_arc_start_) {
        accepted_points.push_back(sketch->world_point(
            (*pending_elliptical_arc_start_)[0],
            (*pending_elliptical_arc_start_)[1]));
    }
    if (!pending_elliptical_arc_minor_) {
        preview.push_back(ellipse_preview_edge(
            *sketch, *pending_elliptical_arc_center_,
            *pending_elliptical_arc_major_, minor));
        viewer_->set_transient_edges(std::move(preview));
        viewer_->set_transient_points(std::move(accepted_points));
        return;
    }
    const auto projected = projected_ellipse_position(
        *pending_elliptical_arc_center_, *pending_elliptical_arc_major_,
        minor, *cursor);
    if (!projected) {
        viewer_->set_transient_edges(std::move(preview));
        viewer_->set_transient_points(std::move(accepted_points));
        return;
    }
    if (!pending_elliptical_arc_start_) {
        preview.push_back(ellipse_preview_edge(
            *sketch, *pending_elliptical_arc_center_,
            *pending_elliptical_arc_major_, minor));
        zima::kernel::ViewerEdge radial;
        radial.construction = true;
        radial.points = {
            sketch->world_point(
                (*pending_elliptical_arc_center_)[0],
                (*pending_elliptical_arc_center_)[1]),
            sketch->world_point(
                projected->position[0], projected->position[1])};
        preview.push_back(std::move(radial));
        viewer_->set_transient_edges(std::move(preview));
        viewer_->set_transient_points(std::move(accepted_points));
        return;
    }
    const auto start = projected_ellipse_position(
        *pending_elliptical_arc_center_, *pending_elliptical_arc_major_,
        minor, *pending_elliptical_arc_start_);
    if (!start) {
        viewer_->set_transient_edges(std::move(preview));
        return;
    }
    constexpr double full_turn = 2.0 * 3.14159265358979323846;
    double end_parameter = projected->parameter;
    while (end_parameter <= start->parameter) end_parameter += full_turn;
    const double sweep = end_parameter - start->parameter;
    if (sweep >= full_turn - 1.0e-12) {
        viewer_->set_transient_edges(std::move(preview));
        return;
    }
    zima::kernel::ViewerEdge arc;
    const auto samples = std::max<std::size_t>(8,
        static_cast<std::size_t>(std::ceil(192.0 * sweep / full_turn)));
    arc.points.reserve(samples + 1);
    for (std::size_t sample = 0; sample <= samples; ++sample) {
        const double parameter = start->parameter + sweep *
            static_cast<double>(sample) / static_cast<double>(samples);
        arc.points.push_back(sketch->world_point(
            (*pending_elliptical_arc_center_)[0] +
                ((*pending_elliptical_arc_major_)[0] -
                 (*pending_elliptical_arc_center_)[0]) * std::cos(parameter) +
                (minor[0] - (*pending_elliptical_arc_center_)[0]) *
                    std::sin(parameter),
            (*pending_elliptical_arc_center_)[1] +
                ((*pending_elliptical_arc_major_)[1] -
                 (*pending_elliptical_arc_center_)[1]) * std::cos(parameter) +
                (minor[1] - (*pending_elliptical_arc_center_)[1]) *
                    std::sin(parameter)));
    }
    preview.push_back(std::move(arc));
    viewer_->set_transient_edges(std::move(preview));
    viewer_->set_transient_points(std::move(accepted_points));
}

} // namespace zima::app
