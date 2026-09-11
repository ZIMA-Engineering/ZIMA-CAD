#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


std::optional<SketchPointAlignment> AssemblyWorkspaceWindow::sketch_point_alignment(
        const zima::kernel::Vec3& origin,const zima::kernel::Vec3& direction) const {
    const auto* sketch=active_sketch();
    if (!sketch || !viewer_ || (sketch_segment_active_ && pending_segment_start_)) return std::nullopt;
    // Only free defining points can own H/V. A circle rim is not a persisted
    // point and later arc/ellipse steps are already confined to their curve.
    if ((sketch_circle_active_ && pending_circle_center_) ||
        (sketch_polygon_active_ && pending_polygon_center_) ||
        (sketch_arc_active_ && pending_arc_start_) ||
        (sketch_ellipse_active_ && pending_ellipse_major_) ||
        (sketch_elliptical_arc_active_ && pending_elliptical_arc_major_)) return std::nullopt;

    if (!(sketch_point_active_ || sketch_segment_active_ || sketch_rectangle_active_ || sketch_polygon_active_ ||
          sketch_circle_active_ || sketch_arc_active_ || sketch_ellipse_active_ || sketch_elliptical_arc_active_ || sketch_bspline_active_)) return std::nullopt;
    const auto position=sketch->intersect_ray(origin,direction);
    if (!position) return std::nullopt;
    return infer_sketch_point_alignment(*sketch,*position,viewer_->world_tolerance_for_pixels(2.0*viewer_->devicePixelRatioF()),sketch_inference_settings_);
}

bool AssemblyWorkspaceWindow::accept_sketch_point_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    const auto* sketch = active_sketch();
    if (!sketch_point_active_ || sketch == nullptr) {
        return false;
    }
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    const auto snap_geometry_id = std::exchange(
        pending_sketch_snap_geometry_id_, {});
    const auto snap_kind = std::exchange(
        pending_sketch_snap_kind_, std::nullopt);
    try {
        bool created = false;
        if (!mutate_active_sketch([&](auto& target) {
                const auto previous_size = target.points.size();
                const auto point_id = target.add_point(
                    (*position)[0], (*position)[1]);
                created = target.points.size() != previous_size;
                if (created && snap_kind && !snap_geometry_id.empty()) {
                    if (*snap_kind==zima::sketcher::ConstraintKind::Horizontal || *snap_kind==zima::sketcher::ConstraintKind::Vertical) {
                        static_cast<void>(apply_sketch_point_snap(target,point_id,snap_geometry_id,snap_kind));
                    } else if (*snap_kind ==
                        zima::sketcher::ConstraintKind::Midpoint) {
                        static_cast<void>(target.add_midpoint_constraint(
                            point_id, snap_geometry_id));
                    } else if (*snap_kind ==
                               zima::sketcher::ConstraintKind::Coincident) {
                        static_cast<void>(apply_sketch_point_snap(
                            target, point_id, snap_geometry_id, snap_kind));
                    } else if (*snap_kind ==
                        zima::sketcher::ConstraintKind::PointOnLine) {
                        const auto separator = snap_geometry_id.find("||");
                        if (separator == std::string::npos) {
                            static_cast<void>(target.add_point_on_line_constraint(
                                point_id, snap_geometry_id));
                        } else {
                            const auto apply_support = [&](const std::string& id) {
                                const auto* point = target.find_point(point_id);
                                if (point != nullptr && target.project_point_to_curve(
                                        id, point->x, point->y)) {
                                    static_cast<void>(
                                        target.add_point_on_circle_constraint(
                                            point_id, id));
                                } else {
                                    static_cast<void>(
                                        target.add_point_on_line_constraint(
                                            point_id, id));
                                }
                            };
                            apply_support(snap_geometry_id.substr(0, separator));
                            apply_support(snap_geometry_id.substr(separator + 2));
                        }
                    } else if (*snap_kind ==
                               zima::sketcher::ConstraintKind::PointOnCircle) {
                        static_cast<void>(target.add_point_on_circle_constraint(
                            point_id, snap_geometry_id));
                    }
                }
            })) return true;
        if (!created) {
            state_->setText(tr("V této poloze již bod skici existuje."));
            return true;
        }
        clear_completed_sketch_interaction();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr("Bod vytvořen. Kliknutím můžete vytvořit další."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
    return true;
}

std::optional<AssemblyWorkspaceWindow::SketchCandidateSnap>
AssemblyWorkspaceWindow::sketch_candidate_snap_ray(
    const zima::viewer::ViewerCandidate& candidate,
    const zima::kernel::Vec3& cursor_origin,
    const zima::kernel::Vec3& cursor_direction) const {
    if (candidate.owner_id != active_sketch_id_) return std::nullopt;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return std::nullopt;
    std::optional<std::array<double, 2>> position;
    std::string support_geometry_id;
    std::optional<zima::sketcher::ConstraintKind> relation;
    if (candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
        candidate.semantic_key.starts_with("point:")) {
        support_geometry_id = candidate.semantic_key.substr(6);
        if (support_geometry_id=="pending-spline-start" && sketch_bspline_active_ &&
            pending_bspline_points_.size()>=3) {
            position=pending_bspline_points_.front();
            relation=zima::sketcher::ConstraintKind::Coincident;
        } else         if (const auto* point = sketch->find_point(support_geometry_id)) {
            position = std::array{point->x, point->y};
            relation = zima::sketcher::ConstraintKind::Coincident;
        }
    } else if (candidate.kind ==
                   zima::viewer::CandidateKind::SketchExternalReference &&
               candidate.semantic_key.starts_with("sketch_midpoint:")) {
        support_geometry_id = candidate.semantic_key.substr(16);
        const auto segment = std::find_if(
            sketch->segments.begin(), sketch->segments.end(),
            [&](const auto& value) { return value.id == support_geometry_id; });
        if (segment == sketch->segments.end()) return std::nullopt;
        const auto visible = sketch->visible_segment_endpoints(
            support_geometry_id);
        if (!visible) return std::nullopt;
        position = std::array{
            (visible->first[0] + visible->second[0]) * 0.5,
            (visible->first[1] + visible->second[1]) * 0.5};
        relation = zima::sketcher::ConstraintKind::Midpoint;
    } else if (candidate.kind ==
                   zima::viewer::CandidateKind::SketchExternalReference &&
               candidate.semantic_key.starts_with("sketch_intersection:")) {
        support_geometry_id = candidate.semantic_key.substr(20);
        const auto separator = support_geometry_id.find("||");
        if (separator == std::string::npos) return std::nullopt;
        struct SnapLine {
            std::array<double, 2> origin;
            std::array<double, 2> direction;
            bool bounded{};
        };
        const auto line_for = [&](const std::string& line_id)
            -> std::optional<SnapLine> {
            if (line_id == "sketch_axis:x") {
                return SnapLine{{0.0, 0.0}, {1.0, 0.0}, false};
            }
            if (line_id == "sketch_axis:y") {
                return SnapLine{{0.0, 0.0}, {0.0, 1.0}, false};
            }
            const auto segment = std::find_if(
                sketch->segments.begin(), sketch->segments.end(),
                [&](const auto& value) { return value.id == line_id; });
            if (segment == sketch->segments.end()) return std::nullopt;
            const auto* first = sketch->find_point(segment->first_point_id);
            const auto* second = sketch->find_point(segment->second_point_id);
            if (first == nullptr || second == nullptr) return std::nullopt;
            return SnapLine{{first->x, first->y},
                {second->x - first->x, second->y - first->y},
                !segment->construction};
        };
        const auto first_id = support_geometry_id.substr(0, separator);
        const auto second_id = support_geometry_id.substr(separator + 2);
        const auto first = line_for(first_id);
        const auto second = line_for(second_id);
        if (!first) return std::nullopt;
        if (second) {
            const double denominator =
                first->direction[0] * second->direction[1] -
                first->direction[1] * second->direction[0];
            if (std::abs(denominator) <= 1.0e-12) return std::nullopt;
            const double offset_x = second->origin[0] - first->origin[0];
            const double offset_y = second->origin[1] - first->origin[1];
            const double parameter =
                (offset_x * second->direction[1] -
                 offset_y * second->direction[0]) / denominator;
            position = std::array{
                first->origin[0] + parameter * first->direction[0],
                first->origin[1] + parameter * first->direction[1]};
        } else {
            const auto intersections = sketch->curve_line_intersections(
                second_id, first->origin, first->direction, first->bounded);
            if (intersections.empty()) return std::nullopt;
            const auto cursor = sketch->intersect_ray(
                cursor_origin, cursor_direction);
            if (!cursor) return std::nullopt;
            position = *std::min_element(
                intersections.begin(), intersections.end(),
                [&](const auto& left, const auto& right) {
                    return std::hypot(left[0] - (*cursor)[0],
                                      left[1] - (*cursor)[1]) <
                        std::hypot(right[0] - (*cursor)[0],
                                   right[1] - (*cursor)[1]);
                });
        }
        relation = zima::sketcher::ConstraintKind::PointOnLine;
    } else if (candidate.kind ==
                   zima::viewer::CandidateKind::SketchExternalReference &&
               candidate.semantic_key.starts_with("sketch_curve_keypoint:")) {
        constexpr std::string_view prefix{"sketch_curve_keypoint:"};
        const auto payload = candidate.semantic_key.substr(prefix.size());
        const auto first_separator = payload.find(':');
        const auto last_separator = payload.rfind(':');
        if (first_separator == std::string::npos ||
            last_separator == first_separator) return std::nullopt;
        const auto curve_kind = payload.substr(0, first_separator);
        support_geometry_id = payload.substr(
            first_separator + 1, last_separator - first_separator - 1);
        int quarter{};
        try {
            quarter = std::stoi(payload.substr(last_separator + 1));
        } catch (const std::exception&) {
            return std::nullopt;
        }
        if (quarter < 0 || quarter > 3) return std::nullopt;
        const double angle = 0.5 * 3.14159265358979323846 *
            static_cast<double>(quarter);
        std::string center_id;
        double radius{};
        std::optional<std::array<double, 2>> major_vector;
        std::optional<std::array<double, 2>> minor_vector;
        if (curve_kind == "circle") {
            const auto circle = std::find_if(
                sketch->circles.begin(), sketch->circles.end(),
                [&](const auto& value) {
                    return value.id == support_geometry_id;
                });
            if (circle == sketch->circles.end()) return std::nullopt;
            center_id = circle->center_point_id;
            radius = circle->radius;
        } else if (curve_kind == "arc") {
            const auto arc = std::find_if(
                sketch->arcs.begin(), sketch->arcs.end(),
                [&](const auto& value) {
                    return value.id == support_geometry_id;
                });
            if (arc == sketch->arcs.end()) return std::nullopt;
            center_id = arc->center_point_id;
            radius = arc->radius;
        } else if (curve_kind == "ellipse" ||
                   curve_kind == "elliptical_arc") {
            std::string major_id;
            std::string minor_id;
            if (curve_kind == "ellipse") {
                const auto ellipse = std::find_if(
                    sketch->ellipses.begin(), sketch->ellipses.end(),
                    [&](const auto& value) {
                        return value.id == support_geometry_id;
                    });
                if (ellipse == sketch->ellipses.end()) return std::nullopt;
                center_id = ellipse->center_point_id;
                major_id = ellipse->major_point_id;
                minor_id = ellipse->minor_point_id;
            } else {
                const auto arc = std::find_if(
                    sketch->elliptical_arcs.begin(),
                    sketch->elliptical_arcs.end(), [&](const auto& value) {
                        return value.id == support_geometry_id;
                    });
                if (arc == sketch->elliptical_arcs.end()) return std::nullopt;
                center_id = arc->center_point_id;
                major_id = arc->major_point_id;
                minor_id = arc->minor_point_id;
            }
            const auto* center = sketch->find_point(center_id);
            const auto* major = sketch->find_point(major_id);
            const auto* minor = sketch->find_point(minor_id);
            if (center == nullptr || major == nullptr || minor == nullptr)
                return std::nullopt;
            major_vector = std::array{major->x - center->x,
                                      major->y - center->y};
            minor_vector = std::array{minor->x - center->x,
                                      minor->y - center->y};
        } else {
            return std::nullopt;
        }
        const auto* center = sketch->find_point(center_id);
        if (center == nullptr) return std::nullopt;
        position = major_vector && minor_vector
            ? std::array{
                  center->x + (*major_vector)[0] * std::cos(angle) +
                      (*minor_vector)[0] * std::sin(angle),
                  center->y + (*major_vector)[1] * std::cos(angle) +
                      (*minor_vector)[1] * std::sin(angle)}
            : std::array{
                  center->x + radius * std::cos(angle),
                  center->y + radius * std::sin(angle)};
        support_geometry_id = "sketch_keypoint:" + curve_kind + ":" +
            support_geometry_id + ":" + std::to_string(quarter);
        relation = zima::sketcher::ConstraintKind::Coincident;
    } else if (candidate.kind ==
                   zima::viewer::CandidateKind::SketchExternalReference &&
               candidate.semantic_key.starts_with("external_point:")) {
        const auto reference_id = sketch_external_reference_id_from_key(
            candidate.semantic_key);
        if (!reference_id) return std::nullopt;
        support_geometry_id = *reference_id;
        relation = zima::sketcher::ConstraintKind::Coincident;
        if (*reference_id == "sketch_origin") {
            position = std::array{0.0, 0.0};
        } else {
            const auto reference = std::find_if(sketch->external_references.begin(),
                sketch->external_references.end(), [&](const auto& value) {
                    return value.id == *reference_id &&
                        value.kind == zima::sketcher::ExternalReferenceKind::Point &&
                        value.cached_points.size() == 1;
                });
            if (reference != sketch->external_references.end()) {
                position = reference->cached_points.front();
            }
        }
    } else {
        const auto cursor = sketch->intersect_ray(cursor_origin, cursor_direction);
        if (!cursor) return std::nullopt;
        std::optional<std::pair<std::array<double, 2>, std::array<double, 2>>> line;
        bool finite_line = true;
        if (candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
            candidate.semantic_key.starts_with("segment:")) {
            support_geometry_id = candidate.semantic_key.substr(8);
            const auto segment = std::find_if(
                sketch->segments.begin(), sketch->segments.end(),
                [&](const auto& value) { return value.id == support_geometry_id; });
            if (segment != sketch->segments.end()) {
                const auto* first = sketch->find_point(segment->first_point_id);
                const auto* second = sketch->find_point(segment->second_point_id);
                if (first != nullptr && second != nullptr) {
                    line = std::pair{std::array{first->x, first->y},
                        std::array{second->x - first->x, second->y - first->y}};
                }
            }
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
                   (candidate.semantic_key == "sketch_axis:x" ||
                    candidate.semantic_key == "sketch_axis:y")) {
            support_geometry_id = candidate.semantic_key;
            finite_line = false;
            line = candidate.semantic_key == "sketch_axis:x"
                ? std::pair{std::array{0.0, 0.0}, std::array{1.0, 0.0}}
                : std::pair{std::array{0.0, 0.0}, std::array{0.0, 1.0}};
        } else if (candidate.kind ==
                       zima::viewer::CandidateKind::SketchExternalReference &&
                   (candidate.semantic_key.starts_with("external_edge:") ||
                    candidate.semantic_key.starts_with("external_axis:") ||
                    candidate.semantic_key.starts_with("external_face:"))) {
            const auto reference_id = sketch_external_reference_id_from_key(
                candidate.semantic_key);
            const auto reference = reference_id
                ? std::find_if(sketch->external_references.begin(),
                    sketch->external_references.end(), [&](const auto& value) {
                        return value.id == *reference_id &&
                            (value.kind ==
                                 zima::sketcher::ExternalReferenceKind::Edge ||
                             value.kind ==
                                 zima::sketcher::ExternalReferenceKind::Axis ||
                             value.kind ==
                                 zima::sketcher::ExternalReferenceKind::Face) &&
                            (value.cached_points.size() >= 2 ||
                             !value.cached_paths.empty());
                    })
                : sketch->external_references.end();
            if (reference != sketch->external_references.end()) {
                support_geometry_id = reference->id;
                if (reference->kind ==
                        zima::sketcher::ExternalReferenceKind::Axis ||
                    (reference->kind ==
                         zima::sketcher::ExternalReferenceKind::Face &&
                     reference->cached_points.size() >= 2)) {
                    const auto& first = reference->cached_points.front();
                    const auto& second = reference->cached_points.back();
                    line = std::pair{first,
                        std::array{second[0] - first[0],
                                   second[1] - first[1]}};
                    finite_line = false;
                } else {
                    double closest_distance =
                        std::numeric_limits<double>::infinity();
                    const auto consider_path = [&](const auto& path) {
                    for (std::size_t index = 1; index < path.size(); ++index) {
                        const auto& first = path[index - 1];
                        const auto& second = path[index];
                        const double dx = second[0] - first[0];
                        const double dy = second[1] - first[1];
                        const double length_squared = dx * dx + dy * dy;
                        if (length_squared <= 1.0e-18) continue;
                        const double parameter = std::clamp(
                            (((*cursor)[0] - first[0]) * dx +
                             ((*cursor)[1] - first[1]) * dy) /
                                length_squared,
                            0.0, 1.0);
                        const std::array candidate_position{
                            first[0] + parameter * dx,
                            first[1] + parameter * dy};
                        const double distance = std::hypot(
                            (*cursor)[0] - candidate_position[0],
                            (*cursor)[1] - candidate_position[1]);
                        if (distance < closest_distance) {
                            closest_distance = distance;
                            position = candidate_position;
                        }
                    }
                    };
                    if (reference->kind ==
                        zima::sketcher::ExternalReferenceKind::Face) {
                        for (const auto& path : reference->cached_paths)
                            consider_path(path);
                    } else {
                        consider_path(reference->cached_points);
                    }
                    if (position) {
                        relation =
                            zima::sketcher::ConstraintKind::PointOnLine;
                    }
                }
            }
        }
        if (line) {
            const double length_squared =
                line->second[0] * line->second[0] +
                line->second[1] * line->second[1];
            if (length_squared <= 1.0e-18) return std::nullopt;
            double parameter =
                (((*cursor)[0] - line->first[0]) * line->second[0] +
                 ((*cursor)[1] - line->first[1]) * line->second[1]) /
                length_squared;
            if (finite_line) parameter = std::clamp(parameter, 0.0, 1.0);
            position = std::array{
                line->first[0] + parameter * line->second[0],
                line->first[1] + parameter * line->second[1]};
            relation = zima::sketcher::ConstraintKind::PointOnLine;
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchCurve) {
            for (const auto prefix : {std::string_view{"circle:"},
                     std::string_view{"arc:"}, std::string_view{"ellipse:"},
                     std::string_view{"elliptical_arc:"},
                     std::string_view{"bspline:"}}) {
                if (candidate.semantic_key.starts_with(prefix)) {
                    support_geometry_id =
                        candidate.semantic_key.substr(prefix.size());
                    break;
                }
            }
            if (support_geometry_id.empty()) return std::nullopt;
            position = sketch->project_point_to_curve(
                support_geometry_id, (*cursor)[0], (*cursor)[1]);
            if (!position) return std::nullopt;
            relation = zima::sketcher::ConstraintKind::PointOnCircle;
        }
    }
    if (!position) return std::nullopt;
    if (support_geometry_id.starts_with("sketch_keypoint:") &&
        sketch_elliptical_arc_active_ && pending_elliptical_arc_center_ &&
        pending_elliptical_arc_major_) {
        // Never advertise K and then silently project the confirmed point to
        // another location. Only exact keypoints compatible with the current
        // elliptical-arc definition are valid candidates for steps 3 and 4.
        constexpr double exact_tolerance = 1.0e-7;
        if (!pending_elliptical_arc_minor_) {
            const auto projected = projected_ellipse_minor(
                *pending_elliptical_arc_center_,
                *pending_elliptical_arc_major_, *position);
            if (!projected || std::hypot(
                    (*projected)[0] - (*position)[0],
                    (*projected)[1] - (*position)[1]) > exact_tolerance) {
                return std::nullopt;
            }
        } else if (!pending_elliptical_arc_start_) {
            const auto projected = projected_ellipse_position(
                *pending_elliptical_arc_center_,
                *pending_elliptical_arc_major_,
                *pending_elliptical_arc_minor_, *position);
            if (!projected || std::hypot(
                    projected->position[0] - (*position)[0],
                    projected->position[1] - (*position)[1]) > exact_tolerance) {
                return std::nullopt;
            }
        }
    }
    // A Sketch may be placed or reference an arbitrary Plane. The enum is
    // only its base convention; snapping must use the resolved frame that is
    // also consumed by world_point() and intersect_ray().
    auto [origin, direction] =
        sketch->normal_ray((*position)[0], (*position)[1]);
    if (relation && !automatic_constraint_enabled(*relation)) return std::nullopt;
    const bool new_center=(sketch_circle_active_ && !pending_circle_center_) ||
        (sketch_arc_active_ && !pending_arc_center_) ||
        (sketch_ellipse_active_ && !pending_ellipse_center_) ||
        (sketch_elliptical_arc_active_ && !pending_elliptical_arc_center_);
    if (new_center && !automatic_constraint_enabled(zima::sketcher::ConstraintKind::Concentric)) {
        const auto is_center=[&](const auto& curves) {
            return std::ranges::any_of(curves,[&](const auto& curve){return curve.center_point_id==support_geometry_id;});
        };
        if (is_center(sketch->circles) || is_center(sketch->arcs) || is_center(sketch->ellipses) || is_center(sketch->elliptical_arcs))
            return std::nullopt;
    }
    return SketchCandidateSnap{
        origin, direction, std::move(support_geometry_id), relation};
}

bool AssemblyWorkspaceWindow::accept_sketch_external_snap(
    const zima::viewer::ViewerCandidate& candidate,
    const zima::kernel::Vec3& origin,
    const zima::kernel::Vec3& direction) {
    if (!(sketch_point_active_ || sketch_segment_active_ ||
          sketch_rectangle_active_ || sketch_polygon_active_ ||
          sketch_circle_active_ || sketch_arc_active_ || sketch_ellipse_active_ ||
          sketch_elliptical_arc_active_ || sketch_bspline_active_)) return false;
    const auto snap = sketch_candidate_snap_ray(candidate, origin, direction);
    if (!snap) return false;
    pending_sketch_snap_geometry_id_ = snap->support_geometry_id;
    pending_sketch_snap_kind_ = snap->relation;
    const bool accepted =
        accept_sketch_point_ray(snap->origin, snap->direction) ||
        accept_sketch_segment_ray(snap->origin, snap->direction) ||
        accept_sketch_rectangle_ray(snap->origin, snap->direction) ||
        accept_sketch_polygon_ray(snap->origin, snap->direction) ||
        accept_sketch_circle_ray(snap->origin, snap->direction) ||
        accept_sketch_arc_ray(snap->origin, snap->direction) ||
        accept_sketch_ellipse_ray(snap->origin, snap->direction) ||
        accept_sketch_elliptical_arc_ray(snap->origin, snap->direction) ||
        accept_sketch_bspline_ray(snap->origin, snap->direction);
    pending_sketch_snap_geometry_id_.clear();
    pending_sketch_snap_kind_.reset();
    return accepted;
}

bool AssemblyWorkspaceWindow::accept_sketch_segment_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    const auto* sketch = active_sketch();
    if (!sketch_segment_active_ || sketch == nullptr) {
        return false;
    }
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_segment_start_) {
        pending_segment_start_ = *position;
        pending_segment_start_snap_geometry_id_ = std::exchange(
            pending_sketch_snap_geometry_id_, {});
        pending_segment_start_snap_kind_ = std::exchange(
            pending_sketch_snap_kind_, std::nullopt);
        state_->setText(sketch_polyline_active_
            ? tr("Lomená čára: určete další bod.")
            : sketch_segment_construction_
                ? tr("Konstrukční čára: určete druhý bod.")
                : tr("Úsečka skici: určete druhý bod. Escape příkaz zruší."));
        return true;
    }
    auto inferred_end = inferred_sketch_segment_end(*position);
    auto confirmed_position = inferred_end.position;
    auto direction_inference = inferred_end.kind;
    const auto offered_direction_inference = direction_inference;
    const auto end_snap_geometry_id = std::exchange(
        pending_sketch_snap_geometry_id_, {});
    const auto end_snap_kind = std::exchange(
        pending_sketch_snap_kind_, std::nullopt);
    if (end_snap_kind) {
        confirmed_position = *position;
        direction_inference.reset();
        inferred_end.reference_point_id.clear();
        inferred_end.equal_length_reference_id.clear();
        inferred_end.symmetry_axis_id.clear();
        inferred_end.tangent_reference_id.clear();
        inferred_end.perpendicular_reference_id.clear();
        inferred_end.parallel_reference_id.clear();
        inferred_end.midpoint_line_reference_id.clear();
        // Candidate snapping and directional inference are independent. A
        // line endpoint confirmed on a curve/line owns both its support
        // relation and the natural tangent/perpendicular relation.
        if (*end_snap_kind == zima::sketcher::ConstraintKind::PointOnCircle) {
            if (const auto tangent = sketch->curve_tangent_at_point(
                    end_snap_geometry_id, confirmed_position[0],
                    confirmed_position[1])) {
                const double segment_x =
                    confirmed_position[0] - (*pending_segment_start_)[0];
                const double segment_y =
                    confirmed_position[1] - (*pending_segment_start_)[1];
                const double tangent_error = std::abs(
                    segment_x * (*tangent)[1] - segment_y * (*tangent)[0]);
                if (automatic_constraint_enabled(zima::sketcher::ConstraintKind::Tangent) && tangent_error <= viewer_->world_tolerance_for_pixels(
                        sketch_tangent_intent_pixels *
                        viewer_->devicePixelRatioF())) {
                    inferred_end.tangent_reference_id = end_snap_geometry_id;
                    inferred_end.tangent_at_start = false;
                    if (const auto exact = exact_circle_tangent_contact(
                            *sketch, *pending_segment_start_,
                            end_snap_geometry_id, confirmed_position)) {
                        confirmed_position = *exact;
                    }
                }
            }
        } else if (*end_snap_kind ==
                       zima::sketcher::ConstraintKind::Coincident) {
            if (const auto curve_id =
                    sketch_keypoint_curve_id(end_snap_geometry_id)) {
                if (const auto tangent = sketch->curve_tangent_at_point(
                        *curve_id, confirmed_position[0], confirmed_position[1])) {
                    const double segment_x =
                        confirmed_position[0] - (*pending_segment_start_)[0];
                    const double segment_y =
                        confirmed_position[1] - (*pending_segment_start_)[1];
                    const double tangent_error = std::abs(
                        segment_x * (*tangent)[1] -
                        segment_y * (*tangent)[0]);
                    if (automatic_constraint_enabled(zima::sketcher::ConstraintKind::Tangent) && tangent_error <= viewer_->world_tolerance_for_pixels(
                            sketch_tangent_intent_pixels *
                            viewer_->devicePixelRatioF())) {
                        inferred_end.tangent_reference_id = *curve_id;
                        inferred_end.tangent_at_start = false;
                        if (const auto exact = exact_circle_tangent_contact(
                                *sketch, *pending_segment_start_, *curve_id,
                                confirmed_position)) {
                            confirmed_position = *exact;
                        }
                    }
                }
            } else if (offered_direction_inference ==
                           zima::sketcher::ConstraintKind::Horizontal ||
                       offered_direction_inference ==
                           zima::sketcher::ConstraintKind::Vertical) {
                // A coincident endpoint is itself present in the Sketch point
                // list. Generic point-alignment inference can therefore find
                // that very same point at zero X/Y distance and advertise V/H
                // "to itself", even when the new segment is visibly oblique.
                // Preserve H/V only when the snapped endpoint is genuinely
                // aligned with the new segment's accepted first endpoint.
                const double intent_tolerance =
                    viewer_->world_tolerance_for_pixels(
                        2.0 * viewer_->devicePixelRatioF());
                const double dx = std::abs(confirmed_position[0] -
                    (*pending_segment_start_)[0]);
                const double dy = std::abs(confirmed_position[1] -
                    (*pending_segment_start_)[1]);
                inferred_end.reference_point_id.clear();
                if (offered_direction_inference ==
                        zima::sketcher::ConstraintKind::Horizontal &&
                    dy <= intent_tolerance) {
                    direction_inference = offered_direction_inference;
                } else if (offered_direction_inference ==
                               zima::sketcher::ConstraintKind::Vertical &&
                           dx <= intent_tolerance) {
                    direction_inference = offered_direction_inference;
                }
            }
        } else if (*end_snap_kind == zima::sketcher::ConstraintKind::Midpoint &&
                   (offered_direction_inference ==
                        zima::sketcher::ConstraintKind::Horizontal ||
                    offered_direction_inference ==
                        zima::sketcher::ConstraintKind::Vertical)) {
            direction_inference = offered_direction_inference;
        } else if (*end_snap_kind ==
                   zima::sketcher::ConstraintKind::PointOnLine) {
            // Landing on a line means coincidence with that support.  Add
            // perpendicularity only when the cursor also expresses a clear
            // directional intent.  Making it unconditional prevented an
            // ordinary oblique segment from starting or ending on an axis.
            if (end_snap_geometry_id == "sketch_axis:x" ||
                end_snap_geometry_id == "sketch_axis:y") {
                const bool vertical = end_snap_geometry_id == "sketch_axis:x";
                const double segment_x =
                    confirmed_position[0] - (*pending_segment_start_)[0];
                const double segment_y =
                    confirmed_position[1] - (*pending_segment_start_)[1];
                const double direction_error = vertical
                    ? std::abs(segment_x) : std::abs(segment_y);
                const double intent_tolerance =
                    viewer_->world_tolerance_for_pixels(
                        2.0 * viewer_->devicePixelRatioF());
                if (automatic_constraint_enabled(vertical ? zima::sketcher::ConstraintKind::Vertical : zima::sketcher::ConstraintKind::Horizontal) && direction_error <= intent_tolerance) {
                    direction_inference = vertical
                        ? zima::sketcher::ConstraintKind::Vertical
                        : zima::sketcher::ConstraintKind::Horizontal;
                    // This H/V belongs to the new segment endpoints. A nearby
                    // unrelated point alignment found before the axis snap
                    // must not steal the directional relation.
                    inferred_end.reference_point_id.clear();
                    if (vertical) {
                        confirmed_position[0] = (*pending_segment_start_)[0];
                    } else {
                        confirmed_position[1] = (*pending_segment_start_)[1];
                    }
                }
            } else if (const auto support_direction =
                    sketch_line_support_direction(end_snap_geometry_id)) {
                const double support_length = std::hypot(
                    (*support_direction)[0], (*support_direction)[1]);
                const double segment_x =
                    confirmed_position[0] - (*pending_segment_start_)[0];
                const double segment_y =
                    confirmed_position[1] - (*pending_segment_start_)[1];
                const double perpendicular_error = std::abs(
                    segment_x * (*support_direction)[0] +
                    segment_y * (*support_direction)[1]) /
                    std::max(support_length, 1.0e-12);
                const double intent_tolerance =
                    viewer_->world_tolerance_for_pixels(
                        2.0 * viewer_->devicePixelRatioF());
                if (automatic_constraint_enabled(zima::sketcher::ConstraintKind::Perpendicular) && perpendicular_error <= intent_tolerance) {
                    inferred_end.perpendicular_reference_id =
                        end_snap_geometry_id;
                }
            }
        }
    }
    const bool polyline_arc = sketch_polyline_active_ && sketch_polyline_arc_mode_;
    const bool polyline_arc_endpoint_snap = polyline_arc && end_snap_kind.has_value();
    const bool polyline_arc_point_alignment = polyline_arc &&
        !polyline_arc_endpoint_snap && direction_inference.has_value() &&
        !inferred_end.reference_point_id.empty();
    const auto polyline_arc_center_snap =
        polyline_arc && !polyline_arc_endpoint_snap &&
            !polyline_arc_point_alignment
        ? inferred_sketch_polyline_arc_center_snap(*position)
        : std::nullopt;
    if (polyline_arc) {
        if (polyline_arc_center_snap) {
            confirmed_position = polyline_arc_center_snap->end;
            direction_inference.reset();
        } else if (polyline_arc_endpoint_snap) {
            // An explicitly offered K/C candidate owns the endpoint exactly.
            // Center-on-axis inference is lower priority and must not move it.
            confirmed_position = *position;
            direction_inference.reset();
        } else if (!polyline_arc_point_alignment) {
            confirmed_position = *position;
            direction_inference.reset();
        }
    }
    const double dx = confirmed_position[0] - (*pending_segment_start_)[0];
    const double dy = confirmed_position[1] - (*pending_segment_start_)[1];
    if (std::hypot(dx, dy) <= 1.0e-9) {
        state_->setText(tr("Úsečka musí mít nenulovou délku."));
        return true;
    }
    const auto common_supports = !polyline_arc &&
            automatic_constraint_enabled(zima::sketcher::ConstraintKind::Tangent)
        ? common_tangent_supports(*sketch,pending_segment_start_snap_geometry_id_,
            end_snap_geometry_id,*pending_segment_start_,confirmed_position,
            viewer_->world_tolerance_for_pixels(sketch_tangent_intent_pixels*viewer_->devicePixelRatioF()))
        : std::nullopt;
    std::string created_geometry_id;
    try {
        if (!mutate_active_sketch([&](auto& target) {
            if (sketch_polyline_active_ && sketch_polyline_arc_mode_) {
                const auto start = std::find_if(
                    target.points.begin(), target.points.end(), [&](const auto& point) {
                        return std::hypot(
                            point.x - (*pending_segment_start_)[0],
                            point.y - (*pending_segment_start_)[1]) <= 1.0e-6;
                    });
                if (start == target.points.end() ||
                    pending_polyline_tangent_geometry_id_.empty()) {
                    throw std::runtime_error(
                        "Tangent polyline arc has no connected start geometry");
                }
                created_geometry_id = target.add_tangent_arc(
                    start->id, confirmed_position[0], confirmed_position[1],
                    pending_polyline_tangent_geometry_id_);
                const auto created_arc = std::ranges::find_if(
                    target.arcs, [&](const auto& value) {
                        return value.id == created_geometry_id;
                    });
                if (created_arc == target.arcs.end()) {
                    throw std::runtime_error(
                        "Created tangent arc cannot be resolved");
                }
                if (polyline_arc_center_snap) {
                    static_cast<void>(target.add_point_on_line_constraint(
                        created_arc->center_point_id,
                        polyline_arc_center_snap->axis_id));
                }
                if (polyline_arc_point_alignment && direction_inference) {
                    static_cast<void>(target.add_point_pair_constraint(
                        inferred_end.reference_point_id,
                        created_arc->end_point_id, *direction_inference));
                }
                if (end_snap_kind && !end_snap_geometry_id.empty()) {
                    if (*end_snap_kind ==
                            zima::sketcher::ConstraintKind::PointOnLine) {
                        static_cast<void>(target.add_point_on_line_constraint(
                            created_arc->end_point_id, end_snap_geometry_id));
                    } else if (*end_snap_kind ==
                            zima::sketcher::ConstraintKind::PointOnCircle) {
                        static_cast<void>(target.add_point_on_circle_constraint(
                            created_arc->end_point_id, end_snap_geometry_id));
                    } else if (*end_snap_kind ==
                            zima::sketcher::ConstraintKind::Midpoint) {
                        static_cast<void>(target.add_midpoint_constraint(
                            created_arc->end_point_id, end_snap_geometry_id));
                    }
                    // Coincident K reuses the exact persisted point in
                    // add_arc(), so no duplicate C relation is required.
                }
            } else if (common_supports) {
                // The picked quadrants choose the branch; C+T owns each
                // contact without locking it to that quadrant forever.
                created_geometry_id=target.add_common_tangent_segment(
                    common_supports->first,*pending_segment_start_,
                    common_supports->second,confirmed_position);
                if(sketch_segment_construction_)
                    target.set_segment_centerline(created_geometry_id,true);
            } else {
                created_geometry_id = target.add_segment(
                    (*pending_segment_start_)[0], (*pending_segment_start_)[1],
                    confirmed_position[0], confirmed_position[1], 1.0e-6,
                    sketch_segment_construction_, true, true);
                if (sketch_segment_construction_) {
                    target.set_segment_centerline(created_geometry_id, true);
                }
                const auto segment = std::find_if(
                    target.segments.begin(), target.segments.end(),
                    [&](const auto& value) {
                        return value.id == created_geometry_id;
                    });
                if (segment == target.segments.end()) {
                    throw std::runtime_error(
                        "Created Sketch segment cannot be resolved");
                }
                const auto first_point_id = segment->first_point_id;
                const auto second_point_id = segment->second_point_id;
                const auto apply_snap = [&](const std::string& point_id,
                        const std::string& geometry_id,
                        const auto& kind) -> std::string {
                    if (!kind || geometry_id.empty()) return point_id;
                    try {
                        if (*kind==zima::sketcher::ConstraintKind::Horizontal || *kind==zima::sketcher::ConstraintKind::Vertical)
                            return apply_sketch_point_snap(target,point_id,geometry_id,kind);
                        if (*kind ==
                            zima::sketcher::ConstraintKind::Midpoint) {
                            static_cast<void>(target.add_midpoint_constraint(
                                point_id, geometry_id));
                        } else if (*kind ==
                                   zima::sketcher::ConstraintKind::Coincident) {
                            return apply_sketch_point_snap(
                                target, point_id, geometry_id, kind);
                        } else if (*kind ==
                                   zima::sketcher::ConstraintKind::PointOnLine) {
                            const auto separator = geometry_id.find("||");
                            if (separator == std::string::npos) {
                                static_cast<void>(target.add_point_on_line_constraint(
                                    point_id, geometry_id));
                            } else {
                                const auto apply_support = [&](const std::string& id) {
                                    const auto* point = target.find_point(point_id);
                                    if (point != nullptr && target.project_point_to_curve(
                                            id, point->x, point->y)) {
                                        static_cast<void>(
                                            target.add_point_on_circle_constraint(
                                                point_id, id));
                                    } else {
                                        static_cast<void>(
                                            target.add_point_on_line_constraint(
                                                point_id, id));
                                    }
                                };
                                apply_support(geometry_id.substr(0, separator));
                                apply_support(geometry_id.substr(separator + 2));
                            }
                        } else if (*kind ==
                                   zima::sketcher::ConstraintKind::PointOnCircle) {
                            static_cast<void>(target.add_point_on_circle_constraint(
                                point_id, geometry_id));
                        }
                    } catch (const std::invalid_argument&) {
                        // A shared endpoint can already own the same support.
                    }
                    return point_id;
                };
                const auto effective_first_point_id = apply_snap(first_point_id,
                    pending_segment_start_snap_geometry_id_,
                    pending_segment_start_snap_kind_);
                const auto effective_second_point_id = apply_snap(second_point_id,
                    end_snap_geometry_id, end_snap_kind);
                if (!inferred_end.symmetry_axis_id.empty()) {
                    static_cast<void>(target.add_symmetric_constraint(
                        effective_first_point_id, effective_second_point_id,
                        inferred_end.symmetry_axis_id));
                }
                if (!inferred_end.tangent_reference_id.empty()) {
                    try {
                        static_cast<void>(target.add_tangent_constraint(
                            inferred_end.tangent_reference_id, created_geometry_id,
                            inferred_end.tangent_at_start
                                ? effective_first_point_id : effective_second_point_id));
                    } catch (const zima::sketcher::RedundantConstraint&) {
                        // Exact snapped endpoints can already imply tangency.
                        // Keep the segment and its support references.
                    }
                }
                if (!inferred_end.perpendicular_reference_id.empty()) {
                    static_cast<void>(target.add_segment_pair_constraint(
                        inferred_end.perpendicular_reference_id,
                        created_geometry_id,
                        zima::sketcher::ConstraintKind::Perpendicular));
                }
                if (!inferred_end.parallel_reference_id.empty()) {
                    static_cast<void>(target.add_segment_pair_constraint(
                        inferred_end.parallel_reference_id,
                        created_geometry_id,
                        zima::sketcher::ConstraintKind::Parallel));
                }
                if (!inferred_end.midpoint_line_reference_id.empty()) {
                    static_cast<void>(target.add_midpoint_on_line_constraint(
                        created_geometry_id,
                        inferred_end.midpoint_line_reference_id));
                }
                if (direction_inference) {
                    try {
                        static_cast<void>(target.add_point_pair_constraint(
                            inferred_end.reference_point_id.empty()
                                ? effective_first_point_id
                                : inferred_end.reference_point_id,
                            effective_second_point_id,
                            *direction_inference));
                    } catch (const std::invalid_argument&) {
                        // Shared/coincident endpoints can already determine
                        // this direction. In that case the inferred H/V would
                        // be redundant and must not be persisted a second time.
                    }
                }
                if (!inferred_end.equal_length_reference_id.empty()) {
                    try {
                        static_cast<void>(target.add_segment_pair_constraint(
                            inferred_end.equal_length_reference_id,
                            created_geometry_id,
                            zima::sketcher::ConstraintKind::EqualLength));
                    } catch (const std::invalid_argument&) {
                        // A previously implied equality must not be stored a
                        // second time merely because it was also offered by
                        // the interactive inference.
                    }
                }
            }
            })) return true;
    } catch (const std::exception& error) {
        state_->setText(tr("Úsečku nelze vytvořit: %1")
            .arg(QString::fromUtf8(error.what())));
        return true;
    }
    preserve_view_on_refresh_ = true;
    if (sketch_polyline_active_) {
        pending_segment_start_ = confirmed_position;
        pending_polyline_tangent_geometry_id_ = created_geometry_id;
    }
    else pending_segment_start_.reset();
    pending_segment_start_snap_geometry_id_.clear();
    pending_segment_start_snap_kind_.reset();
    sketch_segment_inference_cycle_ = 0;
    sketch_skip_candidate_snap_ = false;
    clear_completed_sketch_interaction();
    refresh_tabs();
    refresh_scene();
    state_->setText(sketch_polyline_active_
        ? sketch_polyline_arc_mode_
            ? tr("Tečný oblouk vytvořen. Určete konec dalšího oblouku, RMB přepne úsečku.")
            : tr("Úsek lomené čáry vytvořen. Určete další bod; RMB přepne tečný oblouk.")
        : sketch_segment_construction_
            ? tr("Konstrukční čára vytvořena. Určete první bod další čáry.")
            : tr("Úsečka vytvořena. Kliknutím určete první bod další úsečky."));
    return true;
}

AssemblyWorkspaceWindow::SketchSegmentInference
AssemblyWorkspaceWindow::inferred_sketch_segment_end(
    const std::array<double, 2>& position) const {
    if (!pending_segment_start_ || viewer_ == nullptr) {
        return {position, std::nullopt, {}};
    }
    const double dx = std::abs(position[0] - (*pending_segment_start_)[0]);
    const double dy = std::abs(position[1] - (*pending_segment_start_)[1]);
    const double tolerance = viewer_->world_tolerance_for_pixels(
        9.0 * viewer_->devicePixelRatioF());
    // H/V is a screen-space alignment with the accepted first endpoint.
    // Its capture band must stay a few pixels wide regardless of segment
    // length; an angular tolerance becomes enormous on long geometry.
    const double direction_tolerance = viewer_->world_tolerance_for_pixels(
        2.0 * viewer_->devicePixelRatioF());
    const double tangent_tolerance = viewer_->world_tolerance_for_pixels(
        sketch_tangent_intent_pixels * viewer_->devicePixelRatioF());
    std::vector<std::pair<double, SketchSegmentInference>> point_alignments;
    std::vector<std::pair<double, SketchSegmentInference>> directions;
    std::vector<std::pair<double, SketchSegmentInference>> symmetries;
    std::vector<SketchSegmentInference> tangencies;
    std::vector<SketchSegmentInference> perpendiculars;
    std::vector<std::pair<double, SketchSegmentInference>> parallels;
    std::vector<std::pair<double, SketchSegmentInference>> midpoint_lines;
    // Alignment to another persisted point outranks direction from the
    // segment's own first point. The perpendicular screen distance selects
    // the closest guide, while the other coordinate remains cursor-driven.
    if (const auto* sketch = active_sketch()) {
        std::vector<std::string> start_tangent_curves;
        // A polyline continuation already owns its contact with the preceding
        // curve. Ordinary endpoint snap state is cleared after each segment,
        // so recover this support from the chain for preview and confirmation.
        if(sketch_polyline_active_&&!sketch_polyline_arc_mode_&&
                !pending_polyline_tangent_geometry_id_.empty())
            start_tangent_curves.push_back(pending_polyline_tangent_geometry_id_);

        if (pending_segment_start_snap_kind_ ==
                zima::sketcher::ConstraintKind::PointOnCircle &&
            !pending_segment_start_snap_geometry_id_.empty()) {
            start_tangent_curves.push_back(
                pending_segment_start_snap_geometry_id_);
        } else if (pending_segment_start_snap_kind_ ==
                zima::sketcher::ConstraintKind::Coincident &&
            !pending_segment_start_snap_geometry_id_.empty()) {
            if (const auto keypoint_curve = sketch_keypoint_curve_id(
                    pending_segment_start_snap_geometry_id_)) {
                start_tangent_curves.push_back(*keypoint_curve);
            } else {
                const auto& point_id = pending_segment_start_snap_geometry_id_;
                for (const auto& arc : sketch->arcs) {
                    if (arc.start_point_id == point_id ||
                        arc.end_point_id == point_id) {
                        start_tangent_curves.push_back(arc.id);
                    }
                }
                for (const auto& ellipse : sketch->ellipses) {
                    if (ellipse.major_point_id == point_id ||
                        ellipse.minor_point_id == point_id) {
                        start_tangent_curves.push_back(ellipse.id);
                    }
                }
                for (const auto& arc : sketch->elliptical_arcs) {
                    if (arc.start_point_id == point_id ||
                        arc.end_point_id == point_id ||
                        arc.major_point_id == point_id ||
                        arc.minor_point_id == point_id) {
                        start_tangent_curves.push_back(arc.id);
                    }
                }
            }
        }
        std::ranges::sort(start_tangent_curves);
        start_tangent_curves.erase(std::unique(
            start_tangent_curves.begin(), start_tangent_curves.end()),
            start_tangent_curves.end());
        for (const auto& tangent_curve_id : start_tangent_curves) {
            if (const auto tangent = sketch->curve_tangent_at_point(
                    tangent_curve_id,
                    (*pending_segment_start_)[0], (*pending_segment_start_)[1])) {
                    const double tangent_x = (*tangent)[0];
                    const double tangent_y = (*tangent)[1];
                    const double cursor_x =
                        position[0] - (*pending_segment_start_)[0];
                    const double cursor_y =
                        position[1] - (*pending_segment_start_)[1];
                    const double tangent_error = std::abs(
                        cursor_x * tangent_y - cursor_y * tangent_x);
                    if (tangent_error <= tangent_tolerance) {
                        double along = cursor_x * tangent_x + cursor_y * tangent_y;
                        if (std::abs(along) <= 1.0e-9) {
                            along = std::copysign(
                                std::max(std::hypot(cursor_x, cursor_y), tolerance),
                                along == 0.0 ? 1.0 : along);
                        }
                        SketchSegmentInference tangent_candidate{{
                            (*pending_segment_start_)[0] + along * tangent_x,
                            (*pending_segment_start_)[1] + along * tangent_y},
                            std::nullopt, {}};
                        tangent_candidate.tangent_reference_id =
                            tangent_curve_id;
                        tangent_candidate.tangent_at_start = true;
                        tangencies.push_back(std::move(tangent_candidate));
                    }
            }
        }
        if (pending_segment_start_snap_kind_ ==
                zima::sketcher::ConstraintKind::PointOnLine &&
            !pending_segment_start_snap_geometry_id_.empty() &&
            pending_segment_start_snap_geometry_id_ != "sketch_axis:x" &&
            pending_segment_start_snap_geometry_id_ != "sketch_axis:y") {
            const auto support_id = pending_segment_start_snap_geometry_id_;
            const auto support_direction =
                sketch_line_support_direction(support_id);
            if (support_direction) {
                const double length = std::hypot(
                    (*support_direction)[0], (*support_direction)[1]);
                if (length > 1.0e-12) {
                    const double normal_x = -(*support_direction)[1] / length;
                    const double normal_y = (*support_direction)[0] / length;
                    const double cursor_x =
                        position[0] - (*pending_segment_start_)[0];
                    const double cursor_y =
                        position[1] - (*pending_segment_start_)[1];
                    const double perpendicular_error = std::abs(
                        cursor_x * (*support_direction)[0] +
                        cursor_y * (*support_direction)[1]) / length;
                    const double intent_tolerance = direction_tolerance;
                    if (perpendicular_error <= intent_tolerance) {
                        double along = cursor_x * normal_x + cursor_y * normal_y;
                        if (std::abs(along) <= 1.0e-9) {
                            along = std::copysign(
                                std::max(std::hypot(cursor_x, cursor_y), tolerance),
                                along == 0.0 ? 1.0 : along);
                        }
                        SketchSegmentInference candidate{{
                            (*pending_segment_start_)[0] + along * normal_x,
                            (*pending_segment_start_)[1] + along * normal_y},
                            std::nullopt, {}};
                        candidate.perpendicular_reference_id = support_id;
                        perpendiculars.push_back(std::move(candidate));
                    }
                }
            }
        }
        const zima::sketcher::SketchPoint* horizontal_reference = nullptr;
        const zima::sketcher::SketchPoint* vertical_reference = nullptr;
        double horizontal_distance = direction_tolerance;
        double vertical_distance = direction_tolerance;
        for (const auto& point : sketch->points) {
            if (std::hypot(point.x - (*pending_segment_start_)[0],
                           point.y - (*pending_segment_start_)[1]) <= 1.0e-9) {
                continue;
            }
            const double h_distance = std::abs(position[1] - point.y);
            const double v_distance = std::abs(position[0] - point.x);
            if (h_distance <= horizontal_distance) {
                horizontal_distance = h_distance;
                horizontal_reference = &point;
            }
            if (v_distance <= vertical_distance) {
                vertical_distance = v_distance;
                vertical_reference = &point;
            }
        }
        if (horizontal_reference != nullptr) {
            auto aligned = position;
            aligned[1] = horizontal_reference->y;
            point_alignments.push_back({horizontal_distance,
                {aligned, zima::sketcher::ConstraintKind::Horizontal,
                 horizontal_reference->id}});
        }
        if (vertical_reference != nullptr) {
            auto aligned = position;
            aligned[0] = vertical_reference->x;
            point_alignments.push_back({vertical_distance,
                {aligned, zima::sketcher::ConstraintKind::Vertical,
                 vertical_reference->id}});
        }
        for (const auto& axis : sketch->segments) {
            if (!axis.construction) continue;
            const auto* first = sketch->find_point(axis.first_point_id);
            const auto* second = sketch->find_point(axis.second_point_id);
            if (first == nullptr || second == nullptr) continue;
            const double ax = second->x - first->x;
            const double ay = second->y - first->y;
            const double length_squared = ax * ax + ay * ay;
            if (length_squared <= 1.0e-18) continue;
            const double parameter =
                (((*pending_segment_start_)[0] - first->x) * ax +
                 ((*pending_segment_start_)[1] - first->y) * ay) /
                length_squared;
            const double foot_x = first->x + parameter * ax;
            const double foot_y = first->y + parameter * ay;
            const std::array mirrored{
                2.0 * foot_x - (*pending_segment_start_)[0],
                2.0 * foot_y - (*pending_segment_start_)[1]};
            const double difference_x = std::abs(position[0] - mirrored[0]);
            const double difference_y = std::abs(position[1] - mirrored[1]);
            if (difference_x <= direction_tolerance &&
                difference_y <= direction_tolerance) {
                const double distance = std::hypot(difference_x, difference_y);
                SketchSegmentInference candidate{mirrored, std::nullopt, {}};
                candidate.symmetry_axis_id = axis.id;
                symmetries.push_back({distance, std::move(candidate)});
            }
        }
        const double cursor_x = position[0] - (*pending_segment_start_)[0];
        const double cursor_y = position[1] - (*pending_segment_start_)[1];
        const auto offer_midpoint_on_line = [&](const std::string& reference_id,
                const std::array<double, 2>& first,
                const std::array<double, 2>& direction, bool finite) {
            const double squared = direction[0] * direction[0] +
                direction[1] * direction[1];
            if (squared <= 1.0e-18) return;
            const std::array cursor_midpoint{
                ((*pending_segment_start_)[0] + position[0]) * 0.5,
                ((*pending_segment_start_)[1] + position[1]) * 0.5};
            double parameter =
                ((cursor_midpoint[0] - first[0]) * direction[0] +
                 (cursor_midpoint[1] - first[1]) * direction[1]) / squared;
            if (finite && (parameter < 0.0 || parameter > 1.0)) return;
            const std::array foot{
                first[0] + parameter * direction[0],
                first[1] + parameter * direction[1]};
            const double distance = std::hypot(
                cursor_midpoint[0] - foot[0], cursor_midpoint[1] - foot[1]);
            if (distance > tolerance) return;
            SketchSegmentInference candidate{{
                2.0 * foot[0] - (*pending_segment_start_)[0],
                2.0 * foot[1] - (*pending_segment_start_)[1]},
                std::nullopt, {}};
            candidate.midpoint_line_reference_id = reference_id;
            midpoint_lines.push_back({distance, std::move(candidate)});
        };
        offer_midpoint_on_line(
            "sketch_axis:x", {0.0, 0.0}, {1.0, 0.0}, false);
        offer_midpoint_on_line(
            "sketch_axis:y", {0.0, 0.0}, {0.0, 1.0}, false);
        for (const auto& reference : sketch->segments) {
            const auto* first = sketch->find_point(reference.first_point_id);
            const auto* second = sketch->find_point(reference.second_point_id);
            if (first == nullptr || second == nullptr) continue;
            const double rx = second->x - first->x;
            const double ry = second->y - first->y;
            offer_midpoint_on_line(reference.id, {first->x, first->y},
                {rx, ry}, !reference.centerline);
            const double length = std::hypot(rx, ry);
            if (length <= 1.0e-12) continue;
            const double ux = rx / length;
            const double uy = ry / length;
            const double along = cursor_x * ux + cursor_y * uy;
            const double normal_distance = std::abs(cursor_x * uy - cursor_y * ux);
            if (std::abs(along) <= 1.0e-9 ||
                normal_distance > direction_tolerance) continue;
            SketchSegmentInference candidate{{
                (*pending_segment_start_)[0] + along * ux,
                (*pending_segment_start_)[1] + along * uy},
                std::nullopt, {}};
            candidate.parallel_reference_id = reference.id;
            parallels.push_back({normal_distance, std::move(candidate)});
        }
    }
    if (dy <= direction_tolerance) {
        auto aligned = position;
        aligned[1] = (*pending_segment_start_)[1];
        directions.push_back({dy,
            {aligned, zima::sketcher::ConstraintKind::Horizontal, {}}});
    }
    if (dx <= direction_tolerance) {
        auto aligned = position;
        aligned[0] = (*pending_segment_start_)[0];
        directions.push_back({dx,
            {aligned, zima::sketcher::ConstraintKind::Vertical, {}}});
    }
    const auto by_distance = [](const auto& left, const auto& right) {
        return left.first < right.first;
    };
    std::ranges::sort(point_alignments, by_distance);
    std::ranges::sort(directions, by_distance);
    std::ranges::sort(symmetries, by_distance);
    std::ranges::sort(parallels, by_distance);
    std::ranges::sort(midpoint_lines, by_distance);
    struct EqualCandidate {
        double difference{};
        double length{};
        std::string geometry_id;
    };
    const auto equal_candidates_for = [&](const std::array<double, 2>& endpoint) {
        std::vector<EqualCandidate> result;
        const auto* sketch = active_sketch();
        if (sketch == nullptr) return result;
        const double requested = std::hypot(
            endpoint[0] - (*pending_segment_start_)[0],
            endpoint[1] - (*pending_segment_start_)[1]);
        if (requested <= 1.0e-12) return result;
        for (const auto& segment : sketch->segments) {
            const auto* first = sketch->find_point(segment.first_point_id);
            const auto* second = sketch->find_point(segment.second_point_id);
            if (first == nullptr || second == nullptr) continue;
            const double length = std::hypot(
                second->x - first->x, second->y - first->y);
            const double difference = std::abs(length - requested);
            if (length > 1.0e-12 && difference <= tolerance) {
                result.push_back({difference, length, segment.id});
            }
        }
        std::ranges::sort(result, [](const auto& left, const auto& right) {
            return left.difference < right.difference;
        });
        return result;
    };
    const auto exact_equal = [&](SketchSegmentInference candidate,
            const EqualCandidate& equal) {
        const double vx = candidate.position[0] - (*pending_segment_start_)[0];
        const double vy = candidate.position[1] - (*pending_segment_start_)[1];
        const double length = std::hypot(vx, vy);
        if (length > 1.0e-12) {
            candidate.position = {
                (*pending_segment_start_)[0] + vx * equal.length / length,
                (*pending_segment_start_)[1] + vy * equal.length / length};
        }
        candidate.equal_length_reference_id = equal.geometry_id;
        return candidate;
    };
    std::vector<SketchSegmentInference> variants;
    const auto* current_sketch = active_sketch();
    variants.reserve(point_alignments.size() + directions.size() +
        tangencies.size() + perpendiculars.size() + symmetries.size() +
        parallels.size() + midpoint_lines.size() +
        (current_sketch == nullptr ? 0 : current_sketch->segments.size()) *
            (directions.size() + 1) + 1);
    for (auto& candidate : tangencies) {
        variants.push_back(std::move(candidate));
    }
    for (auto& candidate : perpendiculars) {
        variants.push_back(std::move(candidate));
    }
    for (auto& candidate : midpoint_lines) {
        variants.push_back(std::move(candidate.second));
    }
    for (auto& candidate : point_alignments) {
        variants.push_back(std::move(candidate.second));
    }
    for (auto& candidate : symmetries) {
        variants.push_back(std::move(candidate.second));
    }
    for (auto& candidate : directions) {
        for (const auto& equal : equal_candidates_for(candidate.second.position)) {
            variants.push_back(exact_equal(candidate.second, equal));
        }
        variants.push_back(std::move(candidate.second));
    }
    for (auto& candidate : parallels) {
        for (const auto& equal : equal_candidates_for(candidate.second.position)) {
            variants.push_back(exact_equal(candidate.second, equal));
        }
        variants.push_back(std::move(candidate.second));
    }
    SketchSegmentInference free_candidate{position, std::nullopt, {}};
    for (const auto& equal : equal_candidates_for(position)) {
        variants.push_back(exact_equal(free_candidate, equal));
    }
    variants.push_back(std::move(free_candidate));
    std::erase_if(variants,[&](const auto& value) {
        using K=zima::sketcher::ConstraintKind;
        return (value.kind && !automatic_constraint_enabled(*value.kind)) ||
            (!value.equal_length_reference_id.empty() && !automatic_constraint_enabled(K::EqualLength)) ||
            (!value.tangent_reference_id.empty() && !automatic_constraint_enabled(K::Tangent)) ||
            (!value.perpendicular_reference_id.empty() && !automatic_constraint_enabled(K::Perpendicular)) ||
            (!value.parallel_reference_id.empty() && !automatic_constraint_enabled(K::Parallel)) ||
            (!value.symmetry_axis_id.empty() && !automatic_constraint_enabled(K::Symmetric)) ||
            (!value.midpoint_line_reference_id.empty() && !automatic_constraint_enabled(K::Midpoint));
    });
    auto selected = variants[sketch_segment_inference_cycle_ % variants.size()];
    selected.variant_count = variants.size();
    return selected;
}

std::optional<std::array<double, 2>>
AssemblyWorkspaceWindow::sketch_line_support_direction(
    const std::string& support_id) const {
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return std::nullopt;
    if (support_id == "sketch_axis:x") return std::array{1.0, 0.0};
    if (support_id == "sketch_axis:y") return std::array{0.0, 1.0};
    if (const auto segment = std::find_if(sketch->segments.begin(),
            sketch->segments.end(), [&](const auto& value) {
                return value.id == support_id;
            }); segment != sketch->segments.end()) {
        const auto* first = sketch->find_point(segment->first_point_id);
        const auto* second = sketch->find_point(segment->second_point_id);
        if (first != nullptr && second != nullptr) {
            return std::array{second->x - first->x, second->y - first->y};
        }
        return std::nullopt;
    }
    const auto reference = std::find_if(sketch->external_references.begin(),
        sketch->external_references.end(), [&](const auto& value) {
            return value.id == support_id &&
                (value.kind == zima::sketcher::ExternalReferenceKind::Edge ||
                 value.kind == zima::sketcher::ExternalReferenceKind::Axis) &&
                value.cached_points.size() >= 2;
        });
    if (reference == sketch->external_references.end()) return std::nullopt;
    const auto& first = reference->cached_points.front();
    const auto& last = reference->cached_points.back();
    const double dx = last[0] - first[0];
    const double dy = last[1] - first[1];
    const double length = std::hypot(dx, dy);
    if (length <= 1.0e-12) return std::nullopt;
    if (reference->kind == zima::sketcher::ExternalReferenceKind::Edge) {
        for (const auto& point : reference->cached_points) {
            const double deviation = std::abs(
                (point[0] - first[0]) * dy -
                (point[1] - first[1]) * dx) / length;
            if (deviation > 1.0e-8) return std::nullopt;
        }
    }
    return std::array{dx, dy};
}

} // namespace zima::app
