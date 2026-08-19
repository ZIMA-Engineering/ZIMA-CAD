#include <zima/sketcher/sketch_trim.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace zima::sketcher {
namespace {

using Point2 = std::array<double, 2>;

enum class SampledKind { Segment, Circle, Arc, Ellipse, BSpline, Auxiliary };

struct SampledCurve {
    std::string geometry_id;
    SampledKind kind{SampledKind::Auxiliary};
    std::vector<Point2> points;
    std::vector<double> parameters;
    bool closed{};
    bool trimmable{};
    bool creates_trim_points{true};
};

Point2 local_point(const Sketch& sketch, const zima::kernel::Vec3& point) {
    if (sketch.plane == SketchPlane::XY) return {point.x, point.y};
    if (sketch.plane == SketchPlane::XZ) return {point.x, point.z};
    return {point.y, point.z};
}

std::vector<SampledCurve> sample_curves(const Sketch& sketch) {
    const auto is_imported = [&](const std::string& geometry_id) {
        return std::any_of(sketch.import_blocks.begin(), sketch.import_blocks.end(),
            [&](const auto& block) {
                return std::find(block.geometry_ids.begin(), block.geometry_ids.end(),
                                 geometry_id) != block.geometry_ids.end();
            });
    };
    double model_extent = 20.0;
    for (const auto& point : sketch.points) {
        model_extent = std::max(
            model_extent, 20.0 * std::max(std::abs(point.x), std::abs(point.y)));
    }
    std::vector<SampledCurve> result;
    result.reserve(sketch.segments.size() + sketch.circles.size() +
        sketch.arcs.size() + sketch.ellipses.size() + sketch.bsplines.size());
    for (const auto& segment : sketch.segments) {
        const auto* first = sketch.find_point(segment.first_point_id);
        const auto* second = sketch.find_point(segment.second_point_id);
        Point2 start{first->x, first->y};
        Point2 end{second->x, second->y};
        if (segment.construction) {
            const double dx = end[0] - start[0];
            const double dy = end[1] - start[1];
            const double length = std::hypot(dx, dy);
            const Point2 middle{
                (start[0] + end[0]) * 0.5, (start[1] + end[1]) * 0.5};
            start = {middle[0] - dx * model_extent / length,
                     middle[1] - dy * model_extent / length};
            end = {middle[0] + dx * model_extent / length,
                   middle[1] + dy * model_extent / length};
        }
        result.push_back({segment.id, SampledKind::Segment,
            {start, end}, {0.0, 1.0}, false,
            !segment.construction && !is_imported(segment.id), true});
    }
    constexpr std::size_t circle_samples = 128;
    for (const auto& circle : sketch.circles) {
        const auto* center = sketch.find_point(circle.center_point_id);
        SampledCurve curve{circle.id, SampledKind::Circle};
        curve.closed = true;
        curve.trimmable = !circle.construction && !is_imported(circle.id);
        curve.points.reserve(circle_samples + 1);
        curve.parameters.reserve(circle_samples + 1);
        for (std::size_t index = 0; index <= circle_samples; ++index) {
            const double parameter = static_cast<double>(index) / circle_samples;
            const double angle = 2.0 * 3.14159265358979323846 * parameter;
            curve.points.push_back({
                center->x + circle.radius * std::cos(angle),
                center->y + circle.radius * std::sin(angle)});
            curve.parameters.push_back(parameter);
        }
        result.push_back(std::move(curve));
    }
    for (const auto& arc : sketch.arcs) {
        SampledCurve curve{arc.id, SampledKind::Arc};
        curve.trimmable = !arc.construction && !is_imported(arc.id);
        const double sweep = arc.end_angle - arc.start_angle;
        const auto samples = std::max<std::size_t>(8,
            static_cast<std::size_t>(std::ceil(
                128.0 * sweep / (2.0 * 3.14159265358979323846))));
        const auto* center = sketch.find_point(arc.center_point_id);
        curve.points.reserve(samples + 1);
        curve.parameters.reserve(samples + 1);
        for (std::size_t index = 0; index <= samples; ++index) {
            const double parameter = static_cast<double>(index) / samples;
            const double angle = arc.start_angle + sweep * parameter;
            curve.points.push_back({
                center->x + arc.radius * std::cos(angle),
                center->y + arc.radius * std::sin(angle)});
            curve.parameters.push_back(parameter);
        }
        result.push_back(std::move(curve));
    }
    for (const auto& ellipse : sketch.ellipses) {
        const auto* center = sketch.find_point(ellipse.center_point_id);
        const double orientation = ellipse.reversed ? -1.0 : 1.0;
        SampledCurve curve{ellipse.id, SampledKind::Ellipse};
        curve.closed = true;
        // A partial ellipse needs a persisted elliptical-arc entity.  Until that
        // exact type exists, an ellipse may split other geometry but is not offered.
        curve.trimmable = false;
        constexpr std::size_t samples = 192;
        curve.points.reserve(samples + 1);
        curve.parameters.reserve(samples + 1);
        for (std::size_t index = 0; index <= samples; ++index) {
            const double parameter = static_cast<double>(index) / samples;
            const double angle = 2.0 * 3.14159265358979323846 * parameter;
            const double local_x = ellipse.major_radius * std::cos(angle);
            const double local_y = ellipse.minor_radius * std::sin(angle);
            curve.points.push_back({
                center->x + local_x * std::cos(ellipse.rotation) -
                    orientation * local_y * std::sin(ellipse.rotation),
                center->y + local_x * std::sin(ellipse.rotation) +
                    orientation * local_y * std::cos(ellipse.rotation)});
            curve.parameters.push_back(parameter);
        }
        result.push_back(std::move(curve));
    }
    const auto viewer = sketch.viewer_mesh();
    for (const auto& spline : sketch.bsplines) {
        const auto edge = std::find_if(viewer.edges.begin(), viewer.edges.end(),
            [&](const auto& value) {
                return value.reference.semantic_key == "bspline:" + spline.id;
            });
        if (edge == viewer.edges.end() || edge->points.size() < 2) continue;
        SampledCurve curve{spline.id, SampledKind::BSpline};
        curve.closed = spline.closed;
        curve.trimmable = !spline.construction && !is_imported(spline.id);
        curve.points.reserve(edge->points.size());
        curve.parameters.reserve(edge->points.size());
        for (std::size_t index = 0; index < edge->points.size(); ++index) {
            curve.points.push_back(local_point(sketch, edge->points[index]));
            curve.parameters.push_back(static_cast<double>(index) /
                static_cast<double>(edge->points.size() - 1));
        }
        result.push_back(std::move(curve));
    }
    return result;
}

std::optional<std::array<double, 2>> segment_intersection(
    const Point2& a, const Point2& b, const Point2& c, const Point2& d) {
    const Point2 ab{b[0] - a[0], b[1] - a[1]};
    const Point2 cd{d[0] - c[0], d[1] - c[1]};
    const double denominator = ab[0] * cd[1] - ab[1] * cd[0];
    if (std::abs(denominator) <= 1.0e-12) return std::nullopt;
    const Point2 offset{c[0] - a[0], c[1] - a[1]};
    const double first =
        (offset[0] * cd[1] - offset[1] * cd[0]) / denominator;
    const double second =
        (offset[0] * ab[1] - offset[1] * ab[0]) / denominator;
    if (first < -1.0e-9 || first > 1.0 + 1.0e-9 ||
        second < -1.0e-9 || second > 1.0 + 1.0e-9) return std::nullopt;
    return std::array{
        std::clamp(first, 0.0, 1.0), std::clamp(second, 0.0, 1.0)};
}

std::vector<std::array<double, 2>> curve_intersections(
    const SampledCurve& first, const SampledCurve& second) {
    std::vector<std::array<double, 2>> result;
    for (std::size_t first_index = 0;
         first_index + 1 < first.points.size(); ++first_index) {
        for (std::size_t second_index = 0;
             second_index + 1 < second.points.size(); ++second_index) {
            const auto hit = segment_intersection(
                first.points[first_index], first.points[first_index + 1],
                second.points[second_index], second.points[second_index + 1]);
            if (!hit) continue;
            const double first_parameter = first.parameters[first_index] + (*hit)[0] *
                (first.parameters[first_index + 1] - first.parameters[first_index]);
            const double second_parameter = second.parameters[second_index] + (*hit)[1] *
                (second.parameters[second_index + 1] - second.parameters[second_index]);
            if (std::none_of(result.begin(), result.end(), [&](const auto& old) {
                    return std::abs(old[0] - first_parameter) < 1.0e-5 &&
                        std::abs(old[1] - second_parameter) < 1.0e-5;
                })) {
                result.push_back({first_parameter, second_parameter});
            }
        }
    }
    return result;
}

Point2 curve_point(const SampledCurve& curve, double parameter) {
    if (curve.closed && parameter > 1.0) parameter -= std::floor(parameter);
    parameter = std::clamp(parameter, 0.0, 1.0);
    const auto upper = std::upper_bound(
        curve.parameters.begin(), curve.parameters.end(), parameter);
    const std::size_t second = upper == curve.parameters.end()
        ? curve.parameters.size() - 1
        : static_cast<std::size_t>(upper - curve.parameters.begin());
    const std::size_t first = second == 0 ? 0 : second - 1;
    if (first == second) return curve.points[first];
    const double denominator = curve.parameters[second] - curve.parameters[first];
    const double fraction = denominator <= 1.0e-15 ? 0.0
        : (parameter - curve.parameters[first]) / denominator;
    return {
        curve.points[first][0] + fraction *
            (curve.points[second][0] - curve.points[first][0]),
        curve.points[first][1] + fraction *
            (curve.points[second][1] - curve.points[first][1])};
}

std::vector<Point2> sample_interval(
    const SampledCurve& curve, double start, double end,
    std::size_t minimum_points = 3) {
    const double span = end - start;
    const auto segments = std::max<std::size_t>(minimum_points - 1,
        static_cast<std::size_t>(std::ceil(
            span * static_cast<double>(curve.points.size() - 1))));
    std::vector<Point2> result;
    result.reserve(segments + 1);
    for (std::size_t index = 0; index <= segments; ++index) {
        result.push_back(curve_point(
            curve, start + span * static_cast<double>(index) / segments));
    }
    return result;
}

double point_segment_distance(
    const Point2& point, const Point2& first, const Point2& second) {
    const double dx = second[0] - first[0];
    const double dy = second[1] - first[1];
    const double denominator = dx * dx + dy * dy;
    const double factor = denominator <= 1.0e-20 ? 0.0 : std::clamp(
        ((point[0] - first[0]) * dx + (point[1] - first[1]) * dy) /
            denominator,
        0.0, 1.0);
    return std::hypot(
        point[0] - first[0] - factor * dx,
        point[1] - first[1] - factor * dy);
}

Point2 exact_geometry_point(
    const Sketch& sketch, const SampledCurve& sampled,
    double parameter) {
    if (sampled.closed && parameter > 1.0) parameter -= std::floor(parameter);
    parameter = std::clamp(parameter, 0.0, 1.0);
    if (sampled.kind == SampledKind::Segment) {
        const auto segment = std::find_if(
            sketch.segments.begin(), sketch.segments.end(),
            [&](const auto& value) { return value.id == sampled.geometry_id; });
        const auto* first = sketch.find_point(segment->first_point_id);
        const auto* second = sketch.find_point(segment->second_point_id);
        return {first->x + parameter * (second->x - first->x),
                first->y + parameter * (second->y - first->y)};
    }
    if (sampled.kind == SampledKind::Circle) {
        const auto circle = std::find_if(
            sketch.circles.begin(), sketch.circles.end(),
            [&](const auto& value) { return value.id == sampled.geometry_id; });
        const auto* center = sketch.find_point(circle->center_point_id);
        const double angle = 2.0 * 3.14159265358979323846 * parameter;
        return {center->x + circle->radius * std::cos(angle),
                center->y + circle->radius * std::sin(angle)};
    }
    if (sampled.kind == SampledKind::Arc) {
        const auto arc = std::find_if(
            sketch.arcs.begin(), sketch.arcs.end(),
            [&](const auto& value) { return value.id == sampled.geometry_id; });
        const auto* center = sketch.find_point(arc->center_point_id);
        const double angle = arc->start_angle +
            parameter * (arc->end_angle - arc->start_angle);
        return {center->x + arc->radius * std::cos(angle),
                center->y + arc->radius * std::sin(angle)};
    }
    return curve_point(sampled, parameter);
}

template <typename Values>
void rename_geometry(Values& values, const std::string& from, const std::string& to) {
    const auto found = std::find_if(values.begin(), values.end(),
        [&](const auto& value) { return value.id == from; });
    if (found == values.end()) throw std::runtime_error("Trim output geometry is missing");
    found->id = to;
}

}  // namespace

std::vector<SketchTrimPiece> sketch_trim_topology(
    const Sketch& sketch, bool include_base_axes) {
    sketch.validate();
    auto curves = sample_curves(sketch);
    if (include_base_axes) {
        double extent = 2.0;
        for (const auto& curve : curves) {
            for (const auto& point : curve.points) {
                extent = std::max(extent,
                    2.0 * std::max(std::abs(point[0]), std::abs(point[1])));
            }
        }
        curves.push_back({"__sketch_axis_x__", SampledKind::Auxiliary,
            {{-extent, 0.0}, {extent, 0.0}}, {0.0, 1.0}, false, false, true});
        curves.push_back({"__sketch_axis_y__", SampledKind::Auxiliary,
            {{0.0, -extent}, {0.0, extent}}, {0.0, 1.0}, false, false, true});
    }
    std::map<std::string, std::vector<double>> cuts;
    for (const auto& curve : curves) cuts[curve.geometry_id] = {};
    for (std::size_t first_index = 0; first_index < curves.size(); ++first_index) {
        for (std::size_t second_index = first_index + 1;
             second_index < curves.size(); ++second_index) {
            const auto& first = curves[first_index];
            const auto& second = curves[second_index];
            if (!first.creates_trim_points || !second.creates_trim_points) continue;
            for (const auto& hit : curve_intersections(first, second)) {
                cuts[first.geometry_id].push_back(hit[0]);
                cuts[second.geometry_id].push_back(hit[1]);
            }
        }
    }
    std::vector<SketchTrimPiece> pieces;
    for (const auto& curve : curves) {
        if (!curve.trimmable) continue;
        std::vector<double> parameters;
        for (const double value : cuts[curve.geometry_id]) {
            const double normalized = std::round(value * 1.0e8) / 1.0e8;
            if (std::none_of(parameters.begin(), parameters.end(), [&](double old) {
                    return std::abs(old - normalized) <= 1.0e-10;
                })) {
                parameters.push_back(normalized);
            }
        }
        std::sort(parameters.begin(), parameters.end());
        std::vector<std::array<double, 2>> intervals;
        if (curve.closed) {
            if (parameters.size() < 2) {
                intervals.push_back({0.0, 1.0});
            } else {
                for (std::size_t index = 1; index < parameters.size(); ++index) {
                    intervals.push_back({parameters[index - 1], parameters[index]});
                }
                intervals.push_back({parameters.back(), parameters.front() + 1.0});
            }
        } else {
            parameters.push_back(0.0);
            parameters.push_back(1.0);
            std::sort(parameters.begin(), parameters.end());
            parameters.erase(std::unique(parameters.begin(), parameters.end(),
                [](double first, double second) {
                    return std::abs(first - second) <= 1.0e-10;
                }), parameters.end());
            for (std::size_t index = 1; index < parameters.size(); ++index) {
                intervals.push_back({parameters[index - 1], parameters[index]});
            }
        }
        for (const auto& interval : intervals) {
            if (interval[1] - interval[0] <= 1.0e-7) continue;
            pieces.push_back({curve.geometry_id, interval[0], interval[1],
                sample_interval(curve, interval[0], interval[1]), curve.closed});
        }
    }
    return pieces;
}

std::optional<SketchTrimPiece> nearest_sketch_trim_piece(
    const std::vector<SketchTrimPiece>& pieces,
    const Point2& position, double tolerance) {
    if (!std::isfinite(position[0]) || !std::isfinite(position[1]) ||
        !std::isfinite(tolerance) || tolerance < 0.0) return std::nullopt;
    std::optional<std::pair<double, SketchTrimPiece>> best;
    for (const auto& piece : pieces) {
        for (std::size_t index = 1; index < piece.points.size(); ++index) {
            const double distance = point_segment_distance(
                position, piece.points[index - 1], piece.points[index]);
            if (distance <= tolerance && (!best || distance < best->first)) {
                best = std::pair{distance, piece};
            }
        }
    }
    return best ? std::optional{best->second} : std::nullopt;
}

std::vector<SketchTrimPiece> sketch_trim_pieces_crossed_by_path(
    const std::vector<SketchTrimPiece>& pieces,
    const std::vector<Point2>& path, double tolerance) {
    if (path.size() < 2) {
        const auto piece = path.empty() ? std::nullopt
            : nearest_sketch_trim_piece(pieces, path.front(), tolerance);
        return piece ? std::vector{*piece} : std::vector<SketchTrimPiece>{};
    }
    std::vector<SketchTrimPiece> selected;
    for (const auto& piece : pieces) {
        bool crossed = false;
        for (std::size_t piece_index = 1;
             piece_index < piece.points.size() && !crossed; ++piece_index) {
            for (std::size_t path_index = 1; path_index < path.size(); ++path_index) {
                if (segment_intersection(
                        piece.points[piece_index - 1], piece.points[piece_index],
                        path[path_index - 1], path[path_index])) {
                    crossed = true;
                    break;
                }
            }
        }
        if (crossed) selected.push_back(piece);
    }
    return selected;
}

SketchTrimResult apply_sketch_trim(
    Sketch& sketch, const std::vector<SketchTrimPiece>& removed,
    double snap_tolerance) {
    if (!std::isfinite(snap_tolerance) || snap_tolerance < 0.0) {
        throw std::invalid_argument("Sketch trim snap tolerance is invalid");
    }
    SketchTrimResult result;
    if (removed.empty()) return result;
    const auto topology_with_axes = sketch_trim_topology(sketch, true);
    const auto topology_without_axes = sketch_trim_topology(sketch, false);
    const auto is_current_piece = [&](const SketchTrimPiece& piece) {
        const auto matches = [&](const SketchTrimPiece& current) {
            return current.geometry_id == piece.geometry_id &&
                std::abs(current.start - piece.start) <= 1.0e-8 &&
                std::abs(current.end - piece.end) <= 1.0e-8 &&
                current.closed == piece.closed;
        };
        return std::any_of(topology_with_axes.begin(), topology_with_axes.end(), matches) ||
            std::any_of(topology_without_axes.begin(), topology_without_axes.end(), matches);
    };
    const auto curves = sample_curves(sketch);
    std::map<std::string, const SampledCurve*> curve_by_id;
    for (const auto& curve : curves) curve_by_id[curve.geometry_id] = &curve;
    std::map<std::string, std::vector<std::array<double, 2>>> removed_by_id;
    for (const auto& piece : removed) {
        if (!std::isfinite(piece.start) || !std::isfinite(piece.end) ||
            piece.end <= piece.start || !curve_by_id.contains(piece.geometry_id) ||
            !curve_by_id.at(piece.geometry_id)->trimmable ||
            !is_current_piece(piece)) {
            throw std::invalid_argument("Sketch trim piece is invalid or stale");
        }
        if (std::any_of(removed_by_id[piece.geometry_id].begin(),
                removed_by_id[piece.geometry_id].end(), [&](const auto& old) {
                    return std::abs(old[0] - piece.start) <= 1.0e-8 &&
                        std::abs(old[1] - piece.end) <= 1.0e-8;
                })) {
            throw std::invalid_argument("Sketch trim piece is duplicated");
        }
        if (std::any_of(sketch.import_blocks.begin(), sketch.import_blocks.end(),
                [&](const auto& block) {
                    return std::find(block.geometry_ids.begin(), block.geometry_ids.end(),
                                     piece.geometry_id) != block.geometry_ids.end();
                })) {
            throw std::invalid_argument(
                "Imported Sketch block geometry cannot be trimmed separately");
        }
        removed_by_id[piece.geometry_id].push_back({piece.start, piece.end});
    }

    auto next = sketch;
    for (const auto& [geometry_id, removed_intervals] : removed_by_id) {
        const auto& curve = *curve_by_id.at(geometry_id);
        std::vector<std::array<double, 2>> domains{{0.0, 1.0}};
        for (const auto& interval : removed_intervals) {
            std::vector<std::array<double, 2>> normalized;
            if (interval[1] > 1.0) {
                normalized.push_back({interval[0], 1.0});
                normalized.push_back({0.0, interval[1] - 1.0});
            } else {
                normalized.push_back(interval);
            }
            for (const auto& cut : normalized) {
                std::vector<std::array<double, 2>> revised;
                for (const auto& domain : domains) {
                    if (cut[1] <= domain[0] + 1.0e-8 ||
                        cut[0] >= domain[1] - 1.0e-8) {
                        revised.push_back(domain);
                        continue;
                    }
                    if (cut[0] > domain[0] + 1.0e-8) {
                        revised.push_back({domain[0], cut[0]});
                    }
                    if (cut[1] < domain[1] - 1.0e-8) {
                        revised.push_back({cut[1], domain[1]});
                    }
                }
                domains = std::move(revised);
            }
        }
        if (curve.closed && domains.size() >= 2 &&
            domains.front()[0] <= 1.0e-8 &&
            domains.back()[1] >= 1.0 - 1.0e-8) {
            const std::array<double, 2> wrapped{
                domains.back()[0], domains.front()[1] + 1.0};
            domains.erase(domains.begin());
            domains.pop_back();
            domains.insert(domains.begin(), wrapped);
        }

        std::vector<ConstraintKind> reusable_segment_constraints;
        if (curve.kind == SampledKind::Segment) {
            for (const auto& constraint : next.constraints) {
                if (!constraint.suppressed && constraint.geometry_id == geometry_id &&
                    (constraint.kind == ConstraintKind::Horizontal ||
                     constraint.kind == ConstraintKind::Vertical)) {
                    reusable_segment_constraints.push_back(constraint.kind);
                }
            }
        }
        Point2 center{};
        double radius{};
        bool construction{};
        unsigned spline_degree{3};
        bool spline_closed{};
        std::vector<std::string> source_point_ids;
        if (curve.kind == SampledKind::Circle) {
            const auto source = std::find_if(next.circles.begin(), next.circles.end(),
                [&](const auto& value) { return value.id == geometry_id; });
            const auto* source_center = next.find_point(source->center_point_id);
            center = {source_center->x, source_center->y};
            radius = source->radius;
            construction = source->construction;
            source_point_ids = {source->center_point_id};
        } else if (curve.kind == SampledKind::Arc) {
            const auto source = std::find_if(next.arcs.begin(), next.arcs.end(),
                [&](const auto& value) { return value.id == geometry_id; });
            const auto* source_center = next.find_point(source->center_point_id);
            center = {source_center->x, source_center->y};
            radius = source->radius;
            construction = source->construction;
            source_point_ids = {source->center_point_id, source->start_point_id,
                                source->end_point_id};
        } else if (curve.kind == SampledKind::Segment) {
            const auto source = std::find_if(next.segments.begin(), next.segments.end(),
                [&](const auto& value) { return value.id == geometry_id; });
            construction = source->construction;
            source_point_ids = {source->first_point_id, source->second_point_id};
        } else if (curve.kind == SampledKind::BSpline) {
            const auto source = std::find_if(next.bsplines.begin(), next.bsplines.end(),
                [&](const auto& value) { return value.id == geometry_id; });
            construction = source->construction;
            spline_degree = source->degree;
            spline_closed = source->closed;
            source_point_ids = source->control_point_ids;
        }
        next.remove_geometry(geometry_id);
        const auto restore_source_point = [&](const std::string& point_id) {
            if (point_id.empty() || next.find_point(point_id) != nullptr) return;
            const auto* source_point = sketch.find_point(point_id);
            if (source_point == nullptr) {
                throw std::runtime_error("Trim source point is missing");
            }
            next.points.push_back(*source_point);
        };
        if (!domains.empty()) {
            if (curve.kind == SampledKind::Circle || curve.kind == SampledKind::Arc) {
                restore_source_point(source_point_ids.front());
            }
            const bool keeps_start = std::any_of(domains.begin(), domains.end(),
                [](const auto& domain) { return domain[0] <= 1.0e-8; });
            const bool keeps_end = std::any_of(domains.begin(), domains.end(),
                [](const auto& domain) { return domain[1] >= 1.0 - 1.0e-8 &&
                                                domain[1] <= 1.0 + 1.0e-8; });
            if (curve.kind == SampledKind::Segment) {
                if (keeps_start) restore_source_point(source_point_ids[0]);
                if (keeps_end) restore_source_point(source_point_ids[1]);
            } else if (curve.kind == SampledKind::Arc) {
                if (keeps_start) restore_source_point(source_point_ids[1]);
                if (keeps_end) restore_source_point(source_point_ids[2]);
            } else if (curve.kind == SampledKind::BSpline && !spline_closed) {
                if (keeps_start) restore_source_point(source_point_ids.front());
                if (keeps_end) restore_source_point(source_point_ids.back());
            }
        }
        std::vector<std::string> generated;
        for (std::size_t survivor_index = 0;
             survivor_index < domains.size(); ++survivor_index) {
            const auto domain = domains[survivor_index];
            std::string new_id;
            if (curve.kind == SampledKind::Segment) {
                const auto first = exact_geometry_point(sketch, curve, domain[0]);
                const auto second = exact_geometry_point(sketch, curve, domain[1]);
                new_id = next.add_segment(
                    first[0], first[1], second[0], second[1],
                    snap_tolerance, construction);
            } else if (curve.kind == SampledKind::Circle ||
                       curve.kind == SampledKind::Arc) {
                const auto first = exact_geometry_point(sketch, curve, domain[0]);
                const auto second = exact_geometry_point(sketch, curve, domain[1]);
                new_id = next.add_arc(
                    center[0], center[1], first[0], first[1],
                    second[0], second[1], construction, snap_tolerance);
                const auto created = std::find_if(next.arcs.begin(), next.arcs.end(),
                    [&](const auto& value) { return value.id == new_id; });
                created->radius = radius;
            } else if (curve.kind == SampledKind::BSpline) {
                const std::size_t minimum = static_cast<std::size_t>(spline_degree) + 1;
                auto controls = sample_interval(curve, domain[0], domain[1], minimum);
                const unsigned degree = std::min<unsigned>(
                    spline_degree, static_cast<unsigned>(controls.size() - 1));
                new_id = next.add_bspline(
                    controls, degree, false, construction, snap_tolerance);
            } else {
                throw std::runtime_error("Sketch trim reconstruction is unsupported");
            }
            const std::string stable_id = survivor_index == 0 ? geometry_id : new_id;
            if (survivor_index == 0) {
                if (curve.kind == SampledKind::Segment) {
                    rename_geometry(next.segments, new_id, stable_id);
                } else if (curve.kind == SampledKind::Circle ||
                           curve.kind == SampledKind::Arc) {
                    rename_geometry(next.arcs, new_id, stable_id);
                } else {
                    rename_geometry(next.bsplines, new_id, stable_id);
                }
            }
            generated.push_back(stable_id);
        }
        for (const auto& generated_id : generated) {
            if (curve.kind != SampledKind::Segment) break;
            for (const auto kind : reusable_segment_constraints) {
                static_cast<void>(next.add_segment_constraint(generated_id, kind));
            }
        }
        result.geometry_mapping.emplace(geometry_id, std::move(generated));
    }
    next.validate();
    sketch = std::move(next);
    return result;
}

}  // namespace zima::sketcher
