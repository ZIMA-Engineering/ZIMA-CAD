#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <tuple>
#include <zima/document/dimension_layout_json.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/kernel/stable_id.hpp>
namespace zima::drawing {
namespace {
constexpr double pi = std::numbers::pi;
Point2 add(Point2 a, Point2 b) { return {a.x + b.x, a.y + b.y}; }
Point2 sub(Point2 a, Point2 b) { return {a.x - b.x, a.y - b.y}; }
Point2 mul(Point2 a, double t) { return {a.x * t, a.y * t}; }
double dot(Point2 a, Point2 b) { return a.x * b.x + a.y * b.y; }
double cross(Point2 a, Point2 b) { return a.x * b.y - a.y * b.x; }
double length(Point2 a) { return std::hypot(a.x, a.y); }
Point2 unit(Point2 a) {
    const auto l = length(a);
    return l > 1e-12 ? mul(a, 1 / l) : Point2{};
}
Point2 perp(Point2 a) { return {-a.y, a.x}; }
Point2 project(kernel::Vec3 p, const ProjectionCamera &c) {
    return {kernel::dimension_dot(p, c.horizontal), kernel::dimension_dot(p, c.vertical)};
}
bool finite(Point2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
auto key(const kernel::EdgeReference &r) { return std::tuple{r.owner_id, r.semantic_key, r.instance_path}; }
std::optional<MeasurementCircle> circle_geometry(const std::vector<kernel::Vec3> &points) {
    if (points.size() < 5)
        return {};
    const auto a = points.front();
    auto b = a, c = a;
    double distance = 0, area = 0;
    for (auto p : points) {
        const auto v = kernel::dimension_sub(p, a);
        const auto d = kernel::dimension_dot(v, v);
        if (d > distance) {
            distance = d;
            b = p;
        }
    }
    const auto u = kernel::dimension_sub(b, a);
    for (auto p : points) {
        auto n = kernel::dimension_cross(u, kernel::dimension_sub(p, a));
        const auto d = kernel::dimension_dot(n, n);
        if (d > area) {
            area = d;
            c = p;
        }
    }
    if (area < 1e-16 * distance * distance || distance < 1e-18)
        return {};
    const auto v = kernel::dimension_sub(c, a), n = kernel::dimension_cross(u, v);
    const auto center = kernel::dimension_add(
        a, kernel::dimension_scale(
               kernel::dimension_add(
                   kernel::dimension_scale(kernel::dimension_cross(v, n), kernel::dimension_dot(u, u)),
                   kernel::dimension_scale(kernel::dimension_cross(n, u), kernel::dimension_dot(v, v))),
               1 / (2 * area)));
    const auto normal = kernel::dimension_unit(n), radial = kernel::dimension_sub(a, center);
    const auto radius = std::sqrt(kernel::dimension_dot(radial, radial));
    const double tolerance = std::max(1e-7, radius * 1e-7);
    for (auto p : points) {
        const auto delta = kernel::dimension_sub(p, center);
        if (std::abs(kernel::dimension_dot(delta, normal)) > tolerance ||
            std::abs(std::sqrt(kernel::dimension_dot(delta, delta)) - radius) > tolerance)
            return {};
    }
    return MeasurementCircle{center, normal, kernel::dimension_unit(radial), radius};
}
Point2 point_at(const ProjectedMeasurementCurve &c, double t) {
    if (c.points.empty())
        return {};
    if (c.points.size() == 1)
        return c.points[0];
    if (c.center && std::abs(c.sweep) > 1e-12)
        return add(*c.center,
                   add(mul(c.cosine_axis, std::cos(t * c.sweep)), mul(c.sine_axis, std::sin(t * c.sweep))));
    if (c.parameters.size() == c.points.size()) {
        for (std::size_t i = 1; i < c.parameters.size(); ++i)
            if (t <= c.parameters[i] || i + 1 == c.parameters.size()) {
                const auto span = c.parameters[i] - c.parameters[i - 1];
                return add(c.points[i - 1], mul(sub(c.points[i], c.points[i - 1]),
                                                span > 1e-15 ? (t - c.parameters[i - 1]) / span : 0.));
            }
    }
    std::vector<double> spans;
    double total = 0;
    for (std::size_t i = 1; i < c.points.size(); ++i) {
        total += length(sub(c.points[i], c.points[i - 1]));
        spans.push_back(total);
    }
    if (total < 1e-12)
        return c.points.front();
    const auto target = std::clamp(t, 0., 1.) * total;
    for (std::size_t i = 0; i < spans.size(); ++i)
        if (target <= spans[i] || i + 1 == spans.size()) {
            const auto previous = i ? spans[i - 1] : 0.;
            const auto value =
                add(c.points[i], mul(sub(c.points[i + 1], c.points[i]),
                                     (target - previous) / std::max(1e-15, spans[i] - previous)));
            if (c.circular && c.center)
                return add(*c.center, mul(unit(sub(value, *c.center)), length(c.cosine_axis)));
            return value;
        }
    return c.points.back();
}
double parameter_at(const ProjectedMeasurementCurve &c, Point2 point) {
    double total = 0, best = std::numeric_limits<double>::infinity(), result = 0, passed = 0;
    for (std::size_t i = 1; i < c.points.size(); ++i)
        total += length(sub(c.points[i], c.points[i - 1]));
    for (std::size_t i = 1; i < c.points.size(); ++i) {
        const auto v = sub(c.points[i], c.points[i - 1]);
        const double l = length(v);
        if (l < 1e-12)
            continue;
        const double t = std::clamp(dot(sub(point, c.points[i - 1]), v) / (l * l), 0., 1.);
        const double distance = length(sub(point, add(c.points[i - 1], mul(v, t))));
        if (distance < best) {
            best = distance;
            result = c.parameters.size() == c.points.size()
                         ? c.parameters[i - 1] + t * (c.parameters[i] - c.parameters[i - 1])
                         : (passed + t * l) / std::max(1e-12, total);
        }
        passed += l;
    }
    return result;
}
bool on_arc(const ProjectedMeasurementCurve &c, Point2 p) {
    if (!c.center || c.points.size() < 3)
        return true;
    const double det = cross(c.cosine_axis, c.sine_axis);
    if (std::abs(det) < 1e-16)
        return false;
    const auto angle = [&](Point2 q) {
        const auto d = sub(q, *c.center);
        return std::atan2(cross(c.cosine_axis, d) / det, cross(d, c.sine_axis) / det);
    };
    const double wanted = angle(p);
    double accumulated = 0, previous = angle(c.points[0]);
    const double start = previous;
    for (std::size_t i = 1; i < c.points.size(); ++i) {
        double next = angle(c.points[i]), delta = std::remainder(next - previous, 2 * pi);
        accumulated += delta;
        previous = next;
    }
    if (std::abs(accumulated) > 2 * pi - 1e-5)
        return true;
    double relative = std::remainder(wanted - start, 2 * pi);
    if (accumulated > 0 && relative < -1e-7)
        relative += 2 * pi;
    if (accumulated < 0 && relative > 1e-7)
        relative -= 2 * pi;
    return accumulated >= 0 ? relative >= -1e-6 && relative <= accumulated + 1e-6
                            : relative <= 1e-6 && relative >= accumulated - 1e-6;
}
std::optional<Point2> conic_coordinates(const ProjectedMeasurementCurve &c, Point2 p) {
    if (!c.center)
        return {};
    const auto v = sub(p, *c.center);
    const auto det = cross(c.cosine_axis, c.sine_axis);
    if (std::abs(det) < 1e-15)
        return {};
    return Point2{cross(v, c.sine_axis) / det, cross(c.cosine_axis, v) / det};
}

// Split a degree <= 4 polynomial into monotone intervals at the roots of
// its derivative. This also retains double roots (tangent conic contacts).
std::vector<double> polynomial_roots(std::vector<double> coefficients) {
    std::vector<double> roots;
    double scale = 0;
    for (auto c : coefficients)
        scale = std::max(scale, std::abs(c));
    if (scale == 0)
        return roots;
    while (coefficients.size() > 1 && std::abs(coefficients.back()) < scale * 1e-13)
        coefficients.pop_back();
    if (coefficients.size() < 2)
        return roots;
    if (coefficients.size() == 2)
        return {-coefficients[0] / coefficients[1]};
    const auto value = [&](double x) {
        double sum = 0;
        for (auto i = coefficients.rbegin(); i != coefficients.rend(); ++i)
            sum = sum * x + *i;
        return sum;
    };
    std::vector<double> derivative;
    for (std::size_t i = 1; i < coefficients.size(); ++i)
        derivative.push_back(i * coefficients[i]);
    auto extrema = polynomial_roots(derivative);
    double bound = 1;
    for (std::size_t i = 0; i + 1 < coefficients.size(); ++i)
        bound = std::max(bound, 1 + std::abs(coefficients[i] / coefficients.back()));
    std::vector<double> cuts{-bound};
    for (auto x : extrema)
        if (x > -bound && x < bound)
            cuts.push_back(x);
    cuts.push_back(bound);
    std::ranges::sort(cuts);
    const auto append = [&](double x) {
        if (std::ranges::none_of(
                roots, [&](double p) { return std::abs(p - x) < 1e-9 * std::max(1., std::abs(x)); }))
            roots.push_back(x);
    };
    for (auto x : cuts)
        if (std::abs(value(x)) < scale * 1e-10)
            append(x);
    for (std::size_t i = 1; i < cuts.size(); ++i) {
        double low = cuts[i - 1], high = cuts[i], left = value(low), right = value(high);
        if ((left < 0) == (right < 0))
            continue;
        for (int step = 0; step < 90; ++step) {
            const double middle = low + (high - low) * .5, m = value(middle);
            if ((m < 0) == (left < 0)) {
                low = middle;
                left = m;
            } else
                high = middle;
        }
        append((low + high) * .5);
    }
    std::ranges::sort(roots);
    return roots;
}
const ProjectedMeasurementCurve *find_curve(const std::vector<ProjectedMeasurementCurve> &curves,
                                            const kernel::EdgeReference &ref) {
    const auto i = std::ranges::find(curves, ref, &ProjectedMeasurementCurve::source);
    return i == curves.end() ? nullptr : &*i;
}
std::optional<Point2> resolve(const DrawingView &view, const std::vector<ProjectedMeasurementCurve> &curves,
                              const DimensionAttachment &a, Point2 direction) {
    if (!a.reference.valid())
        return {};
    if (a.kind == DimensionAttachmentKind::Point) {
        const auto p = std::ranges::find(view.measurement_points, a.reference, &MeasurementPoint::source);
        return p == view.measurement_points.end() ? std::nullopt
                                                  : std::optional(project(p->position, view.camera));
    }
    const auto *curve = find_curve(curves, a.reference);
    if (!curve || curve->points.empty())
        return {};
    switch (a.kind) {
    case DimensionAttachmentKind::CurvePoint:
        return point_at(*curve, a.parameter);
    case DimensionAttachmentKind::Line:
        if (curve->line && length(sub(curve->points.back(), curve->points.front())) > 1e-9)
            return point_at(*curve, a.parameter);
        return {};
    case DimensionAttachmentKind::Center:
        return curve->center;
    case DimensionAttachmentKind::Tangent: {
        if (!curve->center)
            return {};
        const double x = dot(curve->cosine_axis, direction), y = dot(curve->sine_axis, direction),
                     norm = std::hypot(x, y);
        if (norm < 1e-10)
            return {};
        auto p = add(*curve->center,
                     mul(add(mul(curve->cosine_axis, x / norm), mul(curve->sine_axis, y / norm)), a.side));
        return on_arc(*curve, p) ? std::optional(p) : std::nullopt;
    }
    case DimensionAttachmentKind::Intersection: {
        const auto *other = find_curve(curves, a.other_reference);
        if (!other)
            return {};
        auto points = dimension_intersections(*curve, *other);
        if (points.empty())
            return {};
        const auto selected = std::ranges::min_element(points, [&](const auto &x, const auto &y) {
            return std::abs(x.second - a.parameter) < std::abs(y.second - a.parameter);
        });
        return selected->first;
    }
    default:
        return {};
    }
}
} // namespace
void capture_measurement_geometry(DrawingView &view, const kernel::ViewerMesh &mesh) {
    view.measurement_curves.clear();
    view.measurement_points.clear();
    std::set<std::tuple<std::string, std::string, std::string>> seen;
    auto edges = mesh.original_references.edges;
    edges.insert(edges.end(), mesh.edges.begin(), mesh.edges.end());
    for (const auto &edge : edges) {
        if (!edge.reference.valid() || edge.parameter_seam || edge.construction || edge.overlay ||
            edge.points.size() < 2)
            continue;
        if (!seen.insert(key(edge.reference)).second)
            continue;
        MeasurementCurve c;
        c.source = edge.reference;
        c.points = edge.points;
        c.circle = circle_geometry(c.points);
        const auto start = c.points.front(), axis = kernel::dimension_sub(c.points.back(), start);
        const double size = std::sqrt(kernel::dimension_dot(axis, axis));
        c.line =
            size > 1e-9 && std::ranges::all_of(c.points, [&](auto p) {
                auto cross = kernel::dimension_cross(axis, kernel::dimension_sub(p, start));
                return std::sqrt(kernel::dimension_dot(cross, cross)) <= size * std::max(1e-7, size * 1e-8);
            });
        view.measurement_curves.push_back(std::move(c));
    }
    seen.clear();
    for (const auto *points : {&mesh.points, &mesh.original_references.points})
        for (const auto &p : *points) {
            kernel::EdgeReference r{p.reference.owner_id, p.reference.semantic_key,
                                    p.reference.instance_path};
            if (r.valid() && seen.insert(key(r)).second)
                view.measurement_points.push_back({r, p.position});
        }
}
std::vector<ProjectedMeasurementCurve> projected_measurement_curves(const DrawingView &view) {
    std::vector<ProjectedMeasurementCurve> result;
    for (const auto &curve : view.measurement_curves) {
        ProjectedMeasurementCurve c;
        c.source = curve.source;
        c.line = curve.line;
        double total = 0;
        c.parameters.push_back(0);
        for (std::size_t i = 0; i < curve.points.size(); ++i) {
            c.points.push_back(project(curve.points[i], view.camera));
            if (i) {
                const auto delta = kernel::dimension_sub(curve.points[i], curve.points[i - 1]);
                total += std::sqrt(kernel::dimension_dot(delta, delta));
                c.parameters.push_back(total);
            }
        }
        if (total > 1e-12)
            for (auto &t : c.parameters)
                t /= total;
        if (curve.circle) {
            const auto &circle = *curve.circle;
            c.center = project(circle.center, view.camera);
            const auto side = kernel::dimension_cross(circle.normal, circle.radial);
            double previous = 0;
            for (auto p : curve.points) {
                const auto delta = kernel::dimension_sub(p, circle.center);
                const double angle = std::atan2(kernel::dimension_dot(delta, side),
                                                kernel::dimension_dot(delta, circle.radial));
                c.sweep += std::remainder(angle - previous, 2 * pi);
                previous = angle;
            }
            c.cosine_axis = mul(project(circle.radial, view.camera), circle.radius);
            c.sine_axis = mul(project(kernel::dimension_cross(circle.normal, circle.radial), view.camera),
                              circle.radius);
            c.circular = std::abs(kernel::dimension_dot(circle.normal, view.camera.depth)) > 1 - 1e-7;
        }
        result.push_back(std::move(c));
    }
    return result;
}
std::vector<std::pair<Point2, double>> dimension_intersections(const ProjectedMeasurementCurve &a,
                                                               const ProjectedMeasurementCurve &b) {
    std::vector<std::pair<Point2, double>> result;
    if (a.points.size() < 2 || b.points.size() < 2 || a.source == b.source)
        return result;
    const auto add_point = [&](Point2 p) {
        if (!finite(p) || !on_arc(a, p) || !on_arc(b, p))
            return;
        if (std::ranges::none_of(result,
                                 [&](const auto &other) { return length(sub(other.first, p)) < 1e-7; })) {
            const auto delta = sub(a.points.back(), a.points.front());
            const auto parameter = a.line && dot(delta, delta) > 1e-18
                                       ? dot(sub(p, a.points.front()), delta) / dot(delta, delta)
                                       : parameter_at(a, p);
            result.push_back({p, parameter});
        }
    };
    if (a.line && b.line) {
        const auto u = sub(a.points.back(), a.points.front()), v = sub(b.points.back(), b.points.front());
        const auto det = cross(u, v);
        if (std::abs(det) > 1e-10 * length(u) * length(v))
            add_point(add(a.points.front(), mul(u, cross(sub(b.points.front(), a.points.front()), v) / det)));
    } else if ((a.line && b.center) || (b.line && a.center)) {
        const auto &line = a.line ? a : b;
        const auto &conic = a.line ? b : a;
        auto origin = conic_coordinates(conic, line.points.front()),
             end = conic_coordinates(conic, line.points.back());
        if (origin && end) {
            const auto v = sub(*end, *origin);
            const double A = dot(v, v), B = 2 * dot(*origin, v), C = dot(*origin, *origin) - 1,
                         disc = B * B - 4 * A * C;
            if (A > 1e-18 && disc >= -1e-10)
                for (double sign : {-1., 1.})
                    add_point(
                        add(line.points.front(), mul(sub(line.points.back(), line.points.front()),
                                                     (-B + sign * std::sqrt(std::max(0., disc))) / (2 * A))));
        }
    } else if (a.circular && b.circular && a.center && b.center) {
        const auto delta = sub(*b.center, *a.center);
        const double distance = length(delta), r1 = length(a.cosine_axis), r2 = length(b.cosine_axis);
        if (distance > 1e-12 && distance <= r1 + r2 + 1e-9 && distance >= std::abs(r1 - r2) - 1e-9) {
            const auto axis = mul(delta, 1 / distance);
            const double x = (r1 * r1 - r2 * r2 + distance * distance) / (2 * distance);
            const double h = std::sqrt(std::max(0., r1 * r1 - x * x));
            const auto midpoint = add(*a.center, mul(axis, x));
            add_point(add(midpoint, mul(perp(axis), h)));
            add_point(sub(midpoint, mul(perp(axis), h)));
        }
    } else if (a.center && b.center) {
        const auto center = conic_coordinates(b, *a.center);
        const auto cos_point = conic_coordinates(b, add(*a.center, a.cosine_axis)),
                   sin_point = conic_coordinates(b, add(*a.center, a.sine_axis));
        if (center && cos_point && sin_point) {
            const auto u = sub(*cos_point, *center), v = sub(*sin_point, *center);
            const double A = dot(u, u), B = 2 * dot(u, v), C = dot(v, v), D = 2 * dot(*center, u),
                         E = 2 * dot(*center, v), F = dot(*center, *center) - 1;
            const auto at = [&](double t) {
                return add(*a.center, add(mul(a.cosine_axis, std::cos(t)), mul(a.sine_axis, std::sin(t))));
            };
            const auto accept = [&](double t) {
                const auto p = at(t);
                const auto q = conic_coordinates(b, p);
                if (q && std::abs(dot(*q, *q) - 1) < 1e-7)
                    add_point(p);
            };
            // tan(t/2) makes the conic equation a quartic.
            const std::vector<double> coefficients{A + D + F, 2 * (B + E), -2 * A + 4 * C + 2 * F,
                                                   2 * (E - B), A - D + F};
            if (std::ranges::any_of(coefficients, [](double c) { return std::abs(c) > 1e-12; })) {
                for (auto root : polynomial_roots(coefficients))
                    accept(2 * std::atan(root));
                accept(pi);
            } // The half-angle coordinate is infinite here.
        }
    } else {
        for (std::size_t i = 1; i < a.points.size(); ++i)
            for (std::size_t j = 1; j < b.points.size(); ++j) {
                const auto u = sub(a.points[i], a.points[i - 1]), v = sub(b.points[j], b.points[j - 1]);
                const double det = cross(u, v);
                if (std::abs(det) < 1e-14)
                    continue;
                const auto delta = sub(b.points[j - 1], a.points[i - 1]);
                const double t = cross(delta, v) / det, s = cross(delta, u) / det;
                if (t >= -1e-10 && t <= 1 + 1e-10 && s >= -1e-10 && s <= 1 + 1e-10)
                    add_point(add(a.points[i - 1], mul(u, t)));
            }
    }
    std::ranges::sort(result, {}, &std::pair<Point2, double>::second);
    return result;
}
std::optional<Point2> resolve_dimension_attachment(const DrawingView &view,
                                                   const DimensionAttachment &attachment, Point2 direction) {
    return resolve(view, projected_measurement_curves(view), attachment, direction);
}

DrawingDimension make_drawing_dimension(std::string view, DrawingDimensionKind kind) {
    DrawingDimension d;
    d.id = kernel::make_stable_id();
    d.view_id = std::move(view);
    d.kind = kind;
    d.attachments.resize(kind == DrawingDimensionKind::Radius || kind == DrawingDimensionKind::Diameter ? 1
                                                                                                        : 2);
    resize_dimension_segments(d);
    return d;
}
void resize_dimension_segments(DrawingDimension &d) {
    const auto size = d.kind == DrawingDimensionKind::Radius || d.kind == DrawingDimensionKind::Diameter ? 1
                      : d.attachments.empty() ? 0
                                              : d.attachments.size() - 1;
    while (d.segments.size() < size) {
        DrawingDimensionSegment s;
        s.id = kernel::make_stable_id();
        d.segments.push_back(std::move(s));
    }
    d.segments.resize(size);
}
void extend_dimension_chain(DrawingDimension &d, bool at_first, DimensionAttachment a) {
    if (d.kind != DrawingDimensionKind::Linear && d.kind != DrawingDimensionKind::Chain)
        throw std::invalid_argument("Řetězec lze rozšířit z lineární kóty.");
    d.kind = DrawingDimensionKind::Chain;
    DrawingDimensionSegment segment;
    segment.id = kernel::make_stable_id();
    if (!d.segments.empty())
        segment.layout = at_first ? d.segments.front().layout : d.segments.back().layout;
    if (at_first) {
        ++d.anchor_attachment;
        d.attachments.insert(d.attachments.begin(), std::move(a));
        d.segments.insert(d.segments.begin(), std::move(segment));
    } else {
        d.attachments.push_back(std::move(a));
        d.segments.push_back(std::move(segment));
    }
}
void validate_drawing_dimension(const DrawingDimension &d) {
    const bool radial = d.kind == DrawingDimensionKind::Radius || d.kind == DrawingDimensionKind::Diameter;
    if (d.id.empty() || d.view_id.empty() || int(d.kind) < 0 || int(d.kind) > 3 || int(d.direction) < 0 ||
        int(d.direction) > 3 ||
        d.attachments.size() != (radial                                   ? 1
                                 : d.kind == DrawingDimensionKind::Linear ? 2
                                                                          : d.attachments.size()) ||
        (!radial && (d.attachments.size() < 2 || d.anchor_attachment + 1 >= d.attachments.size())) ||
        d.attachments.size() > 4096 || d.segments.size() != (radial ? 1 : d.attachments.size() - 1) ||
        d.style.decimals < 0 || d.style.decimals > 12)
        throw std::invalid_argument("Neplatná výkresová kóta.");
    std::set<std::string> ids;
    for (const auto &s : d.segments) {
        if (s.id.empty() || !ids.insert(s.id).second)
            throw std::invalid_argument("Neplatná identita úseku kóty.");
        kernel::validate_dimension_layout(s.layout);
    }
    for (const auto &a : d.attachments)
        if (int(a.kind) < 0 || int(a.kind) > 5 || !std::isfinite(a.parameter) ||
            (a.kind != DimensionAttachmentKind::Intersection && (a.parameter < 0 || a.parameter > 1)) ||
            (a.side != 1 && a.side != -1))
            throw std::invalid_argument("Neplatná vazba kóty.");
    if (d.style.tolerance_mode != "" && d.style.tolerance_mode != "symmetric" &&
        d.style.tolerance_mode != "single_deviation" && d.style.tolerance_mode != "deviations")
        throw std::invalid_argument("Neplatná tolerance kóty.");
}

std::vector<MeasurementCandidate> measurement_candidates(const DrawingView &view, Point2 cursor,
                                                         double tolerance,
                                                         const MeasurementPickRequest &request) {
    std::vector<MeasurementCandidate> offered;
    const auto curves = projected_measurement_curves(view);
    const auto distance_to = [&](Point2 p, Point2 a, Point2 b) {
        const auto delta = sub(b, a);
        const double size = dot(delta, delta);
        return length(
            sub(p, add(a, mul(delta, size > 1e-18 ? std::clamp(dot(sub(p, a), delta) / size, 0., 1.) : 0.))));
    };
    const auto add_candidate = [&](DimensionAttachment a, Point2 point, double distance) {
        if (distance > tolerance)
            return;
        if (std::ranges::any_of(offered, [&](const auto &c) { return c.attachment == a; }))
            return;
        offered.push_back({std::move(a), point, distance});
    };
    const auto *parallel = find_curve(curves, request.parallel_line);
    for (const auto &curve : curves) {
        if (request.lines_only && !curve.line)
            continue;
        if (request.circles_only && (!curve.center || !curve.circular))
            continue;
        double distance = std::numeric_limits<double>::infinity();
        bool visible = false;
        for (const auto &edge : view.projected_edges)
            if (edge.source == curve.source && !edge.hatch && !edge.silhouette &&
                drawing_edge_visible(view, edge)) {
                visible = true;
                for (std::size_t i = 1; i < edge.points.size(); ++i)
                    distance = std::min(distance, distance_to(cursor, edge.points[i - 1], edge.points[i]));
            }
        if (!visible)
            continue;
        DimensionAttachment a;
        a.reference = curve.source;
        a.parameter = parameter_at(curve, cursor);
        if (request.intersection_first.valid()) {
            const auto *first = find_curve(curves, request.intersection_first);
            if (!first || first->source == curve.source)
                continue;
            for (const auto &[point, parameter] : dimension_intersections(*first, curve)) {
                a.kind = DimensionAttachmentKind::Intersection;
                a.reference = first->source;
                a.other_reference = curve.source;
                a.parameter = parameter;
                add_candidate(a, point,
                              std::min(distance, length(sub(cursor, point))) +
                                  .01 * std::min(tolerance, length(sub(cursor, point))));
            }
            continue;
        }
        if (request.circles_only) {
            a.kind = DimensionAttachmentKind::CurvePoint;
            add_candidate(a, point_at(curve, a.parameter), distance);
            continue;
        }
        const auto mode = request.mode;
        if (mode == int(DimensionAttachmentKind::Center)) {
            if (curve.center) {
                a.kind = DimensionAttachmentKind::Center;
                add_candidate(a, *curve.center, std::min(distance, length(sub(cursor, *curve.center))));
            }
            continue;
        }
        if (mode == int(DimensionAttachmentKind::Tangent)) {
            if (curve.center)
                for (int side : {1, -1}) {
                    a.kind = DimensionAttachmentKind::Tangent;
                    a.side = side;
                    if (auto p =
                            resolve(view, curves, a,
                                    request.tangent_origin ? unit(sub(*curve.center, *request.tangent_origin))
                                                           : request.tangent_direction))
                        add_candidate(a, *p,
                                      std::min(distance, length(sub(cursor, *p))) +
                                          .01 * std::min(tolerance, length(sub(cursor, *p))));
                }
            continue;
        }
        if (mode == int(DimensionAttachmentKind::Intersection) || request.lines_only) {
            a.kind = DimensionAttachmentKind::Line;
            add_candidate(a, point_at(curve, a.parameter), distance);
            continue;
        }
        if (mode < 0 || mode == int(DimensionAttachmentKind::Point) ||
            mode == int(DimensionAttachmentKind::CurvePoint)) {
            for (double parameter : {0., .5, 1.}) {
                a.kind = DimensionAttachmentKind::CurvePoint;
                a.parameter = parameter;
                const auto point = point_at(curve, parameter);
                for (const auto &stored : view.measurement_points)
                    if (length(sub(point, project(stored.position, view.camera))) < 1e-7) {
                        a.kind = DimensionAttachmentKind::Point;
                        a.reference = stored.source;
                        break;
                    }
                add_candidate(a, point, length(sub(cursor, point)) * .75);
                a.reference = curve.source;
            }
        }
        if (mode == int(DimensionAttachmentKind::Point))
            continue;
        a.parameter = parameter_at(curve, cursor);
        if (curve.line && (mode < 0 || mode == int(DimensionAttachmentKind::Line))) {
            if (parallel && std::abs(cross(unit(sub(parallel->points.back(), parallel->points.front())),
                                           unit(sub(curve.points.back(), curve.points.front())))) > 1e-7)
                continue;
            a.kind = DimensionAttachmentKind::Line;
            a.parameter = .5;
            add_candidate(a, point_at(curve, .5), distance + .05 * tolerance);
        } else if (mode < 0 || mode == int(DimensionAttachmentKind::CurvePoint)) {
            a.kind = DimensionAttachmentKind::CurvePoint;
            add_candidate(a, point_at(curve, a.parameter), distance + .1 * tolerance);
        }
    }
    std::stable_sort(offered.begin(), offered.end(),
                     [](const auto &a, const auto &b) { return a.distance < b.distance; });
    return offered;
}
DimensionEvaluation evaluate_drawing_dimension(const DrawingView &view, const DrawingDimension &d) {
    DimensionEvaluation result;
    result.resolved_attachments.resize(d.attachments.size(), false);
    const auto missing = [&](const char *message) {
        result.state = MeasurementState::Unresolved;
        result.message = message;
        result.presentations.clear();
        result.cached_segment_indices.clear();
        for (std::size_t i = 0; i < d.segments.size(); ++i)
            if (d.segments[i].last_presentation) {
                result.presentations.push_back(*d.segments[i].last_presentation);
                result.cached_segment_indices.push_back(i);
            }
        return result;
    };
    if (d.view_id != view.id || d.attachments.empty())
        return missing("Vyberte geometrické vazby kóty.");
    const auto curves = projected_measurement_curves(view);
    const bool radial = d.kind == DrawingDimensionKind::Radius || d.kind == DrawingDimensionKind::Diameter;
    if (radial) {
        const auto *curve = find_curve(curves, d.attachments[0].reference);
        if (!curve || !curve->center || d.segments.empty())
            return missing("Vyberte kružnici nebo kruhový oblouk.");
        result.resolved_attachments[0] = true;
        if (!curve->circular) {
            result.state = MeasurementState::Hidden;
            result.message = "R/Ø je skrytá v nekruhovém průmětu.";
            return result;
        }
        const double radius = length(curve->cosine_axis);
        if (radius < 1e-9)
            return missing("Kružnice má nulový poloměr.");
        auto rim = point_at(*curve, d.attachments[0].parameter);
        auto direction = unit(sub(rim, *curve->center));
        if (length(direction) < .5)
            direction = unit(curve->cosine_axis);
        kernel::ViewerDimension value;
        value.kind = d.kind == DrawingDimensionKind::Radius ? kernel::ViewerDimensionKind::Radius
                                                            : kernel::ViewerDimensionKind::Diameter;
        value.witness_first = {curve->center->x, curve->center->y, 0};
        value.witness_second = {curve->center->x + direction.x * radius,
                                curve->center->y + direction.y * radius, 0};
        value.line_first = value.witness_first;
        value.line_second = value.witness_second;
        value.value = radial && d.kind == DrawingDimensionKind::Diameter ? radius * 2 : radius;
        value.label_prefix = d.kind == DrawingDimensionKind::Radius ? "R" : "Ø";
        value.driving = false;
        value.label_position =
            kernel::dimension_add(value.witness_first, {direction.x * (radius + 8 / view.scale),
                                                        direction.y * (radius + 8 / view.scale), 0});
        value = kernel::layout_dimension(value, {}, d.segments[0].layout);
        result.presentations = {value};
        result.state = MeasurementState::Resolved;
        return result;
    }
    if (d.attachments.size() < 2 || d.segments.size() + 1 != d.attachments.size())
        return missing("Doplňte konce kóty.");
    const auto anchor = d.anchor_attachment;
    Point2 direction{1, 0};
    if (d.direction == DimensionDirection::Vertical)
        direction = {0, 1};
    else if (d.direction == DimensionDirection::Parallel) {
        const auto *curve = find_curve(curves, d.parallel_reference);
        result.direction_resolved =
            curve && curve->line && length(sub(curve->points.back(), curve->points.front())) > 1e-9;
        if (!result.direction_resolved)
            return missing("Chybí platná úsečka pro rovnoběžnost.");
        direction = unit(sub(curve->points.back(), curve->points.front()));
    } else if (d.direction == DimensionDirection::Automatic &&
               d.attachments[anchor].kind == DimensionAttachmentKind::Line) {
        const auto *curve = find_curve(curves, d.attachments[anchor].reference);
        if (!curve || !curve->line || length(sub(curve->points.back(), curve->points.front())) < 1e-9)
            return missing("První úsečka není v pohledu dostupná.");
        direction = perp(unit(sub(curve->points.back(), curve->points.front())));
    } else if (d.direction == DimensionDirection::Automatic) {
        const auto preliminary = [&](const auto &a) -> std::optional<Point2> {
            if (a.kind == DimensionAttachmentKind::Tangent) {
                const auto *c = find_curve(curves, a.reference);
                return c ? c->center : std::nullopt;
            }
            return resolve(view, curves, a, {1, 0});
        };
        const auto a = preliminary(d.attachments[anchor]), b = preliminary(d.attachments[anchor + 1]);
        if (a && b && length(sub(*b, *a)) > 1e-9)
            direction = unit(sub(*b, *a));
    }
    std::vector<Point2> points;
    for (std::size_t i = 0; i < d.attachments.size(); ++i) {
        const auto p = resolve(view, curves, d.attachments[i], direction);
        result.resolved_attachments[i] = p.has_value();
        points.push_back(p.value_or(Point2{}));
    }
    if (std::ranges::any_of(result.resolved_attachments, [](bool ok) { return !ok; }))
        return missing("Některá vazba není dostupná. Vyberte její náhradu.");
    if (d.attachments[anchor].kind == DimensionAttachmentKind::Line) {
        const auto *first = find_curve(curves, d.attachments[anchor].reference);
        const auto line = unit(sub(first->points.back(), first->points.front()));
        for (std::size_t i = 0; i < d.attachments.size(); ++i)
            if (d.attachments[i].kind == DimensionAttachmentKind::Line) {
                const auto *next = find_curve(curves, d.attachments[i].reference);
                if (std::abs(cross(line, unit(sub(next->points.back(), next->points.front())))) > 1e-7) {
                    result.resolved_attachments[i] = false;
                    return missing("Druhá úsečka musí být rovnoběžná s první.");
                }
            }
    }
    const auto outward = perp(direction);
    const double base = dot(points[anchor], outward) + 8 / view.scale;
    bool visible = false;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const auto &layout = d.segments[i].layout;
        const auto a = points[i], b = points[i + 1];
        const auto line_point = [&](Point2 p) {
            return add(mul(direction, dot(p, direction)), mul(outward, base + layout.line_offset));
        };
        const auto start = line_point(a), end = line_point(b), middle = mul(add(start, end), .5);
        const auto text =
            add(middle, add(mul(direction, layout.text_along), mul(outward, layout.text_outward)));
        kernel::ViewerDimension value;
        value.witness_first = {a.x, a.y, 0};
        value.witness_second = {b.x, b.y, 0};
        value.line_first = {start.x, start.y, 0};
        value.line_second = {end.x, end.y, 0};
        value.label_position = kernel::Vec3{text.x, text.y, 0};
        value.value = std::abs(dot(sub(b, a), direction));
        value.arrows_reversed = layout.arrows_reversed;
        value.driving = false;
        visible |= value.value > 1e-9;
        result.presentations.push_back(value);
    }
    result.state = visible ? MeasurementState::Resolved : MeasurementState::Hidden;
    return result;
}
void refresh_drawing_dimension(const DrawingView &view, DrawingDimension &d) {
    const auto result = evaluate_drawing_dimension(view, d);
    if (result.state != MeasurementState::Resolved)
        return;
    for (std::size_t i = 0; i < d.segments.size(); ++i)
        d.segments[i].last_presentation = result.presentations[i];
}
std::string drawing_dimension_text(const DrawingDimension &d, const kernel::ViewerDimension &value,
                                   bool unresolved) {
    if (unresolved)
        return "?";
    return kernel::dimension_text(value, d.style);
}
void drag_drawing_dimension(const DrawingView &view, DrawingDimension &d, std::size_t index, int handle,
                            Point2 delta) {
    const auto result = evaluate_drawing_dimension(view, d);
    if (result.state != MeasurementState::Resolved || index >= result.presentations.size())
        return;
    const auto &source = result.presentations[index];
    const bool radial = source.kind == kernel::ViewerDimensionKind::Radius ||
                        source.kind == kernel::ViewerDimensionKind::Diameter;
    const auto a = radial ? source.witness_first : source.line_first,
               b = radial ? source.witness_second : source.line_second;
    auto direction = unit({b.x - a.x, b.y - a.y});
    // The layout basis is independent of the measuring direction's sign.
    if (!radial) {
        const auto copy = [&] {
            auto v = d;
            v.segments[index].layout.text_along += 1;
            return evaluate_drawing_dimension(view, v).presentations[index];
        }();
        direction = {copy.label_position->x - source.label_position->x,
                     copy.label_position->y - source.label_position->y};
    }
    auto &layout = d.segments[index].layout;
    if (radial) {
        layout = kernel::dragged_dimension_layout(source, {}, layout, handle, dot(delta, direction),
                                                  dot(delta, perp(direction)));
    } else {
        layout.line_offset += dot(delta, perp(direction));
        if (handle == 0)
            layout.text_along += dot(delta, direction);
    }
    refresh_drawing_dimension(view, d);
}
void place_drawing_dimension(const DrawingView &view, DrawingDimension &d, std::size_t segment,
                             Point2 point) {
    const auto result = evaluate_drawing_dimension(view, d);
    if (result.state != MeasurementState::Resolved || segment >= result.presentations.size())
        return;
    const auto label = result.presentations[segment].label_position.value();
    drag_drawing_dimension(view, d, segment, 0, {point.x - label.x, point.y - label.y});
}

namespace {
using nlohmann::json;
json ref_json(const kernel::EdgeReference &r) {
    return {{"owner", r.owner_id}, {"key", r.semantic_key}, {"instance_path", r.instance_path}};
}
kernel::EdgeReference ref_from(const json &j) { return {j.at("owner"), j.at("key"), j.at("instance_path")}; }
} // namespace
std::string serialize_drawing_dimensions(const std::vector<DrawingDimension> &dimensions) {
    auto rows = json::array();
    std::set<std::string> ids;
    for (const auto &d : dimensions) {
        validate_drawing_dimension(d);
        if (!ids.insert(d.id).second)
            throw std::invalid_argument("Duplicate drawing dimension");
        json j{{"id", d.id},
               {"view_id", d.view_id},
               {"anchor_attachment", d.anchor_attachment},
               {"kind", int(d.kind)},
               {"direction", int(d.direction)},
               {"parallel_reference", ref_json(d.parallel_reference)},
               {"style", document::dimension_text_style_json(d.style)}};
        j["attachments"] = json::array();
        for (const auto &a : d.attachments)
            j["attachments"].push_back({{"kind", int(a.kind)},
                                        {"reference", ref_json(a.reference)},
                                        {"other_reference", ref_json(a.other_reference)},
                                        {"parameter", a.parameter},
                                        {"side", a.side}});
        j["segments"] = json::array();
        for (const auto &s : d.segments)
            j["segments"].push_back(
                {{"id", s.id},
                 {"layout", document::dimension_layout_json(s.layout)},
                 {"last_presentation", s.last_presentation
                                           ? document::dimension_geometry_json(*s.last_presentation)
                                           : json(nullptr)}});
        rows.push_back(std::move(j));
    }
    return rows.dump();
}
std::vector<DrawingDimension> deserialize_drawing_dimensions(const std::string &text) {
    std::vector<DrawingDimension> result;
    std::set<std::string> ids;
    for (const auto &j : json::parse(text)) {
        DrawingDimension d;
        d.anchor_attachment = j.at("anchor_attachment");
        d.id = j.at("id");
        d.view_id = j.at("view_id");
        d.kind = DrawingDimensionKind(j.at("kind").get<int>());
        d.direction = DimensionDirection(j.at("direction").get<int>());
        d.parallel_reference = ref_from(j.at("parallel_reference"));
        d.style = document::dimension_text_style_from_json(j.at("style"));
        for (const auto &a : j.at("attachments"))
            d.attachments.push_back({DimensionAttachmentKind(a.at("kind").get<int>()),
                                     ref_from(a.at("reference")), ref_from(a.at("other_reference")),
                                     a.at("parameter"), a.at("side")});
        for (const auto &s : j.at("segments")) {
            DrawingDimensionSegment segment;
            segment.id = s.at("id");
            segment.layout = document::dimension_layout_from_json(s.at("layout"));
            if (!s.at("last_presentation").is_null())
                segment.last_presentation = document::dimension_geometry_from_json(s.at("last_presentation"));
            d.segments.push_back(std::move(segment));
        }
        validate_drawing_dimension(d);
        if (!ids.insert(d.id).second)
            throw std::invalid_argument("Duplicate drawing dimension");
        result.push_back(std::move(d));
    }
    return result;
}
std::string serialize_measurement_geometry(const DrawingView &view) {
    json j{{"curves", json::array()}, {"points", json::array()}};
    for (const auto &c : view.measurement_curves) {
        json curve{
            {"source", ref_json(c.source)}, {"line", c.line}, {"points", json::array()}, {"circle", nullptr}};
        for (auto p : c.points)
            curve["points"].push_back(document::dimension_vec_json(p));
        if (c.circle)
            curve["circle"] = {{"center", document::dimension_vec_json(c.circle->center)},
                               {"normal", document::dimension_vec_json(c.circle->normal)},
                               {"radial", document::dimension_vec_json(c.circle->radial)},
                               {"radius", c.circle->radius}};
        j["curves"].push_back(std::move(curve));
    }
    for (const auto &p : view.measurement_points)
        j["points"].push_back(
            {{"source", ref_json(p.source)}, {"position", document::dimension_vec_json(p.position)}});
    return j.dump();
}
void deserialize_measurement_geometry(DrawingView &view, const std::string &text) {
    const auto j = json::parse(text);
    view.measurement_curves.clear();
    view.measurement_points.clear();
    std::set<std::tuple<std::string, std::string, std::string>> keys;
    for (const auto &c : j.at("curves")) {
        MeasurementCurve curve;
        curve.source = ref_from(c.at("source"));
        curve.line = c.at("line");
        if (!curve.source.valid() || !keys.insert(key(curve.source)).second)
            throw std::invalid_argument("Invalid measuring curve identity");
        for (const auto &p : c.at("points"))
            curve.points.push_back(document::dimension_vec_from_json(p));
        if (!c.at("circle").is_null()) {
            const auto &a = c.at("circle");
            curve.circle =
                MeasurementCircle{document::dimension_vec_from_json(a.at("center")),
                                  document::dimension_vec_from_json(a.at("normal")),
                                  document::dimension_vec_from_json(a.at("radial")), a.at("radius")};
            if (!std::isfinite(curve.circle->radius) || curve.circle->radius <= 0 ||
                std::abs(kernel::dimension_dot(curve.circle->normal, curve.circle->normal) - 1) > 1e-6 ||
                std::abs(kernel::dimension_dot(curve.circle->radial, curve.circle->radial) - 1) > 1e-6 ||
                std::abs(kernel::dimension_dot(curve.circle->normal, curve.circle->radial)) > 1e-6)
                throw std::invalid_argument("Invalid measuring circle");
            for (auto p : curve.points) {
                const auto delta = kernel::dimension_sub(p, curve.circle->center);
                const double tolerance = std::max(1e-7, curve.circle->radius * 1e-7);
                if (std::abs(kernel::dimension_dot(delta, curve.circle->normal)) > tolerance ||
                    std::abs(std::sqrt(kernel::dimension_dot(delta, delta)) - curve.circle->radius) >
                        tolerance)
                    throw std::invalid_argument("Measuring circle does not match its source");
            }
        }
        if (curve.points.size() < 2)
            throw std::invalid_argument("Invalid measuring curve");
        view.measurement_curves.push_back(std::move(curve));
    }
    keys.clear();
    for (const auto &p : j.at("points")) {
        MeasurementPoint point{ref_from(p.at("source")), document::dimension_vec_from_json(p.at("position"))};
        if (!point.source.valid() || !keys.insert(key(point.source)).second)
            throw std::invalid_argument("Invalid measuring point identity");
        view.measurement_points.push_back(std::move(point));
    }
}
} // namespace zima::drawing
