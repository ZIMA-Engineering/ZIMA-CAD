"""Transient topology and reconstruction helpers for Sketcher trimming."""

from __future__ import annotations

from dataclasses import dataclass
import copy
import math
from typing import Any, Iterable

from zima_cad.sketch_geometry import (
    center_arc_points,
    ellipse_points,
    elliptical_arc_points,
    evaluate_corner_radius,
)

Point2 = tuple[float, float]
TRIMMABLE_TYPES = {
    "segment", "arc", "circle", "ellipse", "elliptical_arc", "spline",
}


@dataclass(frozen=True)
class SampledCurve:
    entity_id: str
    entity_type: str
    points: tuple[Point2, ...]
    parameters: tuple[float, ...]
    closed: bool
    trimmable: bool
    creates_trim_points: bool


@dataclass(frozen=True)
class TrimPiece:
    entity_id: str
    start: float
    end: float
    points: tuple[Point2, ...]
    closed: bool = False


def _point_map(entities: Iterable[dict[str, Any]]) -> dict[str, Point2]:
    return {
        str(entity.get("id", "")): (
            float(entity.get("x", 0.0)), float(entity.get("y", 0.0))
        )
        for entity in entities if entity.get("type") == "point"
    }


def _spline_points(points: tuple[Point2, ...], samples: int = 128) -> tuple[Point2, ...]:
    """Dependency-free Catmull-Rom preview used only for trim topology."""
    if len(points) < 3:
        return points
    closed = len(points) >= 4 and points[0] == points[-1]
    source = points[:-1] if closed else points
    if len(source) < 3:
        return points
    result: list[Point2] = []
    spans = len(source) if closed else len(source) - 1
    per_span = max(4, samples // max(1, spans))
    for index in range(spans):
        p0 = source[(index - 1) % len(source)] if closed else source[max(0, index - 1)]
        p1 = source[index]
        p2 = source[(index + 1) % len(source)]
        p3 = source[(index + 2) % len(source)] if closed else source[min(len(source) - 1, index + 2)]
        for sample in range(per_span):
            t = sample / per_span
            t2, t3 = t * t, t * t * t
            result.append((
                0.5 * ((2 * p1[0]) + (-p0[0] + p2[0]) * t
                       + (2*p0[0] - 5*p1[0] + 4*p2[0] - p3[0]) * t2
                       + (-p0[0] + 3*p1[0] - 3*p2[0] + p3[0]) * t3),
                0.5 * ((2 * p1[1]) + (-p0[1] + p2[1]) * t
                       + (2*p0[1] - 5*p1[1] + 4*p2[1] - p3[1]) * t2
                       + (-p0[1] + 3*p1[1] - 3*p2[1] + p3[1]) * t3),
            ))
    result.append(result[0] if closed else source[-1])
    return tuple(result)


def sample_sketch_curves(entities: list[dict[str, Any]]) -> tuple[SampledCurve, ...]:
    points = _point_map(entities)
    model_extent = max(
        [1.0]
        + [abs(coordinate) for point in points.values() for coordinate in point]
    ) * 20.0
    curves: list[SampledCurve] = []
    for entity in entities:
        entity_id = str(entity.get("id", ""))
        entity_type = str(entity.get("type", ""))
        ids = tuple(map(str, entity.get("point_ids", ())))
        sampled: tuple[Point2, ...] = ()
        closed = False
        if entity_type in ("segment", "construction") and len(ids) == 2:
            if all(point_id in points for point_id in ids):
                sampled = (points[ids[0]], points[ids[1]])
                if entity_type == "construction":
                    first, second = sampled
                    dx, dy = second[0] - first[0], second[1] - first[1]
                    length = math.hypot(dx, dy)
                    if length > 1.0e-12:
                        ux, uy = dx / length, dy / length
                        midpoint = (
                            (first[0] + second[0]) * 0.5,
                            (first[1] + second[1]) * 0.5,
                        )
                        sampled = (
                            (midpoint[0] - ux * model_extent,
                             midpoint[1] - uy * model_extent),
                            (midpoint[0] + ux * model_extent,
                             midpoint[1] + uy * model_extent),
                        )
        elif entity_type == "circle" and ids and ids[0] in points:
            center = points[ids[0]]
            radius = float(entity.get("radius", 0.0))
            sampled = tuple(
                (center[0] + radius * math.cos(math.tau * i / 128),
                 center[1] + radius * math.sin(math.tau * i / 128))
                for i in range(129)
            )
            closed = True
        elif entity_type == "arc" and len(ids) >= 3 and all(pid in points for pid in ids[:3]):
            sampled = center_arc_points(
                points[ids[0]], points[ids[1]], points[ids[2]],
                segments=128, clockwise=bool(entity.get("clockwise", False)),
            )
        elif entity_type == "ellipse" and len(ids) >= 3 and all(pid in points for pid in ids[:3]):
            sampled = ellipse_points(*(points[pid] for pid in ids[:3]), segments=192)
            closed = True
        elif entity_type == "elliptical_arc" and len(ids) >= 5 and all(pid in points for pid in ids[:5]):
            sampled = elliptical_arc_points(
                *(points[pid] for pid in ids[:5]),
                clockwise=bool(entity.get("clockwise", False)), segments=192,
            )
        elif entity_type == "spline" and len(ids) >= 2 and all(pid in points for pid in ids):
            sampled = _spline_points(tuple(points[pid] for pid in ids))
            closed = len(ids) >= 4 and ids[0] == ids[-1]
        if len(sampled) < 2 or not entity_id:
            continue
        parameters = tuple(i / (len(sampled) - 1) for i in range(len(sampled)))
        is_axis = entity_type == "construction"
        is_auxiliary = entity.get("role") == "construction"
        curves.append(SampledCurve(
            entity_id, entity_type, tuple(sampled), parameters, closed,
            entity_type in TRIMMABLE_TYPES and not is_axis and not is_auxiliary,
            not is_auxiliary,
        ))
    return tuple(curves)


def _profile_curve_endpoints(
    entity: dict[str, Any],
) -> tuple[str, str] | None:
    """Return the two graph endpoints owned by an open sketch curve."""
    entity_type = str(entity.get("type", ""))
    point_ids = tuple(map(str, entity.get("point_ids", ())))
    if entity_type == "segment" and len(point_ids) == 2:
        return point_ids
    if entity_type == "arc" and len(point_ids) >= 3:
        return (
            (point_ids[1], point_ids[2])
            if entity.get("arc_mode") == "center"
            else (point_ids[0], point_ids[-1])
        )
    if entity_type == "elliptical_arc" and len(point_ids) >= 5:
        return point_ids[3], point_ids[4]
    if entity_type == "spline" and len(point_ids) >= 2:
        return point_ids[0], point_ids[-1]
    return None


def _effective_profile_curve_data(
    entities: list[dict[str, Any]],
) -> tuple[
    dict[str, SampledCurve],
    dict[tuple[str, str, str], SampledCurve],
]:
    """Evaluate persisted corner radii into trimmed lines and real arcs."""
    points = _point_map(entities)
    entity_by_id = {
        str(entity.get("id", "")): entity
        for entity in entities
        if isinstance(entity, dict) and str(entity.get("id", ""))
    }
    sampled_by_id = {
        curve.entity_id: curve
        for curve in sample_sketch_curves(entities)
        if entity_by_id.get(curve.entity_id, {}).get("role")
        != "construction"
        and curve.entity_type != "construction"
    }
    segments = {
        entity_id: entity
        for entity_id, entity in entity_by_id.items()
        if entity.get("type") == "segment"
        and entity.get("role") != "construction"
        and len(entity.get("point_ids", ())) == 2
    }
    trim_points: dict[tuple[str, str], Point2] = {}
    corner_curves: dict[tuple[str, str, str], SampledCurve] = {}
    seen: set[tuple[str, str, str]] = set()
    for first_id, first in segments.items():
        first_points = tuple(map(str, first.get("point_ids", ())))
        records = first.get("corner_radii", ())
        if not isinstance(records, list):
            continue
        for record in records:
            if not isinstance(record, dict) or bool(record.get("suppressed", False)):
                continue
            second_id = str(record.get("other_geometry_id", ""))
            vertex_id = str(record.get("vertex_id", ""))
            key = (*sorted((first_id, second_id)), vertex_id)
            if key in seen:
                continue
            second = segments.get(second_id)
            second_points = (
                tuple(map(str, second.get("point_ids", ())))
                if second is not None else ()
            )
            if (
                len(first_points) != 2
                or len(second_points) != 2
                or vertex_id not in first_points
                or vertex_id not in second_points
                or vertex_id not in points
            ):
                continue
            first_outer_id = next(
                point_id for point_id in first_points
                if point_id != vertex_id
            )
            second_outer_id = next(
                point_id for point_id in second_points
                if point_id != vertex_id
            )
            if first_outer_id not in points or second_outer_id not in points:
                continue
            evaluated = evaluate_corner_radius(
                points[vertex_id],
                points[first_outer_id],
                points[second_outer_id],
                float(record.get("radius", 0.0)),
                samples=64,
            )
            if evaluated is None:
                continue
            seen.add(key)
            trim_points[(first_id, vertex_id)] = evaluated.first_tangent
            trim_points[(second_id, vertex_id)] = evaluated.second_tangent
            radius_id = str(
                record.get("id")
                or f"radius:{key[0]}:{key[1]}:{vertex_id}"
            )
            sampled = tuple(evaluated.arc_points)
            corner_curves[key] = SampledCurve(
                radius_id,
                "arc",
                sampled,
                tuple(
                    index / (len(sampled) - 1)
                    for index in range(len(sampled))
                ),
                False,
                False,
                False,
            )

    effective: dict[str, SampledCurve] = {}
    for entity_id, curve in sampled_by_id.items():
        entity = entity_by_id.get(entity_id, {})
        if curve.entity_type != "segment":
            effective[entity_id] = curve
            continue
        point_ids = tuple(map(str, entity.get("point_ids", ())))
        if len(point_ids) != 2 or any(point_id not in points for point_id in point_ids):
            continue
        sampled = (
            trim_points.get((entity_id, point_ids[0]), points[point_ids[0]]),
            trim_points.get((entity_id, point_ids[1]), points[point_ids[1]]),
        )
        if math.dist(*sampled) <= 1.0e-12:
            continue
        effective[entity_id] = SampledCurve(
            curve.entity_id,
            curve.entity_type,
            sampled,
            (0.0, 1.0),
            False,
            curve.trimmable,
            curve.creates_trim_points,
        )
    return effective, corner_curves


def sample_effective_profile_curves(
    entities: list[dict[str, Any]],
) -> tuple[SampledCurve, ...]:
    """Sample visible profile curves, including persisted corner radii."""
    effective, corner_curves = _effective_profile_curve_data(entities)
    ordered_ids = [
        str(entity.get("id", ""))
        for entity in entities
        if isinstance(entity, dict)
        and str(entity.get("id", "")) in effective
    ]
    return tuple(
        [effective[entity_id] for entity_id in ordered_ids]
        + [corner_curves[key] for key in sorted(corner_curves)]
    )


def ordered_effective_profile_curves(
    entities: list[dict[str, Any]],
) -> tuple[SampledCurve, ...]:
    """Return one consistently oriented, unbranched effective profile."""
    effective, corner_curves = _effective_profile_curve_data(entities)
    entity_by_id = {
        str(entity.get("id", "")): entity
        for entity in entities
        if isinstance(entity, dict)
        and str(entity.get("id", "")) in effective
    }
    if not entity_by_id:
        return ()

    closed_ids = [
        entity_id for entity_id, curve in effective.items()
        if curve.closed and _profile_curve_endpoints(entity_by_id[entity_id]) is None
    ]
    if closed_ids:
        return (
            (effective[closed_ids[0]],)
            if len(closed_ids) == 1 and len(entity_by_id) == 1
            else ()
        )

    endpoints = {
        entity_id: _profile_curve_endpoints(entity)
        for entity_id, entity in entity_by_id.items()
    }
    if any(value is None for value in endpoints.values()):
        return ()
    adjacency: dict[str, list[str]] = {}
    for entity_id, endpoint_pair in endpoints.items():
        assert endpoint_pair is not None
        first, second = endpoint_pair
        adjacency.setdefault(first, []).append(entity_id)
        adjacency.setdefault(second, []).append(entity_id)
    if any(len(linked) > 2 for linked in adjacency.values()):
        return ()
    ends = sorted(
        point_id for point_id, linked in adjacency.items()
        if len(linked) == 1
    )
    if len(ends) not in (0, 2):
        return ()
    start = ends[0] if ends else min(adjacency)
    current = start
    used: set[str] = set()
    chain: list[tuple[str, str, str]] = []
    while len(used) < len(entity_by_id):
        candidates = sorted(
            entity_id for entity_id in adjacency.get(current, ())
            if entity_id not in used
        )
        if not candidates:
            return ()
        entity_id = candidates[0]
        pair = endpoints[entity_id]
        assert pair is not None
        next_point = pair[1] if pair[0] == current else pair[0]
        chain.append((entity_id, current, next_point))
        used.add(entity_id)
        current = next_point
    closed = not ends
    if (closed and current != start) or (not closed and current != ends[1]):
        return ()

    result: list[SampledCurve] = []
    oriented: list[SampledCurve] = []
    for entity_id, start_id, _end_id in chain:
        curve = effective[entity_id]
        pair = endpoints[entity_id]
        assert pair is not None
        if pair[0] != start_id:
            sampled = tuple(reversed(curve.points))
            curve = SampledCurve(
                curve.entity_id,
                curve.entity_type,
                sampled,
                tuple(
                    index / (len(sampled) - 1)
                    for index in range(len(sampled))
                ),
                curve.closed,
                curve.trimmable,
                curve.creates_trim_points,
            )
        oriented.append(curve)

    for index, curve in enumerate(oriented):
        result.append(curve)
        if not closed and index + 1 == len(oriented):
            continue
        next_index = (index + 1) % len(oriented)
        first_id, _start_id, vertex_id = chain[index]
        second_id = chain[next_index][0]
        radius_curve = corner_curves.get(
            (*sorted((first_id, second_id)), vertex_id)
        )
        if radius_curve is None:
            continue
        sampled = radius_curve.points
        if math.dist(sampled[0], curve.points[-1]) > math.dist(
            sampled[-1], curve.points[-1]
        ):
            sampled = tuple(reversed(sampled))
        result.append(SampledCurve(
            radius_curve.entity_id,
            radius_curve.entity_type,
            sampled,
            tuple(
                item / (len(sampled) - 1)
                for item in range(len(sampled))
            ),
            False,
            False,
            False,
        ))
    return tuple(result)


def offset_ordered_profile_curves(
    curves: tuple[SampledCurve, ...],
    distance: float,
) -> tuple[SampledCurve, ...]:
    """Offset one ordered sampled profile while preserving curve boundaries."""
    if not curves:
        return ()
    tolerance = 1.0e-8
    points: list[Point2] = []
    curve_indices: list[list[int]] = []
    for curve in curves:
        indices: list[int] = []
        for point in curve.points:
            if points and math.dist(point, points[-1]) <= tolerance:
                index = len(points) - 1
            elif points and math.dist(point, points[0]) <= tolerance:
                index = 0
            else:
                points.append(point)
                index = len(points) - 1
            indices.append(index)
        curve_indices.append(indices)
    closed = (
        len(points) >= 3
        and math.dist(curves[0].points[0], curves[-1].points[-1])
        <= tolerance
    )
    minimum = 3 if closed else 2
    if len(points) < minimum:
        return ()
    if abs(distance) <= 1.0e-12:
        return curves

    segment_pairs = [
        (points[index], points[index + 1])
        for index in range(len(points) - 1)
    ]
    if closed:
        segment_pairs.append((points[-1], points[0]))
    lines: list[tuple[Point2, Point2]] = []
    for first, second in segment_pairs:
        dx, dy = second[0] - first[0], second[1] - first[1]
        length = math.hypot(dx, dy)
        if length <= 1.0e-12:
            return ()
        lines.append((
            (
                first[0] - dy / length * distance,
                first[1] + dx / length * distance,
            ),
            (dx / length, dy / length),
        ))

    def intersection(
        first_line: tuple[Point2, Point2],
        second_line: tuple[Point2, Point2],
        fallback: Point2,
    ) -> Point2:
        first_point, first_direction = first_line
        second_point, second_direction = second_line
        denominator = (
            first_direction[0] * second_direction[1]
            - first_direction[1] * second_direction[0]
        )
        if abs(denominator) <= 1.0e-10:
            return second_point
        delta = (
            second_point[0] - first_point[0],
            second_point[1] - first_point[1],
        )
        factor = (
            delta[0] * second_direction[1]
            - delta[1] * second_direction[0]
        ) / denominator
        candidate = (
            first_point[0] + factor * first_direction[0],
            first_point[1] + factor * first_direction[1],
        )
        if math.dist(candidate, fallback) > abs(distance) * 100.0:
            raise ValueError("profile offset creates an unbounded mitre")
        return candidate

    try:
        if closed:
            offset_points = [
                intersection(lines[index - 1], lines[index], points[index])
                for index in range(len(points))
            ]
        else:
            offset_points = [lines[0][0]]
            offset_points.extend(
                intersection(lines[index - 1], lines[index], points[index])
                for index in range(1, len(points) - 1)
            )
            last_point, last_direction = lines[-1]
            last_length = math.dist(points[-2], points[-1])
            offset_points.append((
                last_point[0] + last_direction[0] * last_length,
                last_point[1] + last_direction[1] * last_length,
            ))
    except ValueError:
        return ()

    def direction_dot(
        original_first: Point2,
        original_second: Point2,
        offset_first: Point2,
        offset_second: Point2,
    ) -> float:
        return (
            (original_second[0] - original_first[0])
            * (offset_second[0] - offset_first[0])
            + (original_second[1] - original_first[1])
            * (offset_second[1] - offset_first[1])
        )

    # A mitre at a real junction between two sketch entities may legitimately
    # trim a short piece from either parallel curve.  Applying the ordinary
    # local-curvature reversal check to that last sampled piece rejects a
    # valid offset (notably a spline meeting a line at a sharp corner).  Trim
    # only those boundary-adjacent samples; a reversal anywhere inside one
    # entity still means that the requested offset crossed its local centre
    # of curvature.
    sampled_curves: list[
        tuple[SampledCurve, list[Point2], list[Point2], list[float]]
    ] = []
    may_trim_junctions = len(curves) > 1
    for curve, indices in zip(curves, curve_indices):
        original = list(curve.points)
        sampled = [offset_points[index] for index in indices]
        parameters = list(curve.parameters)
        if len(parameters) != len(original):
            parameters = [
                index / (len(original) - 1)
                for index in range(len(original))
            ]
        if may_trim_junctions:
            while len(sampled) > 2 and direction_dot(
                original[0], original[1], sampled[0], sampled[1]
            ) <= 1.0e-14:
                del original[1]
                del sampled[1]
                del parameters[1]
            while len(sampled) > 2 and direction_dot(
                original[-2], original[-1], sampled[-2], sampled[-1]
            ) <= 1.0e-14:
                del original[-2]
                del sampled[-2]
                del parameters[-2]
        if any(
            direction_dot(
                original_first,
                original_second,
                offset_first,
                offset_second,
            ) <= 1.0e-14
            for (original_first, original_second),
            (offset_first, offset_second) in zip(
                zip(original, original[1:]),
                zip(sampled, sampled[1:]),
            )
        ):
            return ()
        sampled_curves.append((curve, original, sampled, parameters))

    contour: list[Point2] = []
    for _curve, _original, sampled, _parameters in sampled_curves:
        for point in sampled:
            if not contour or math.dist(point, contour[-1]) > tolerance:
                contour.append(point)
    if closed and len(contour) > 1 and math.dist(
        contour[0], contour[-1]
    ) <= tolerance:
        contour.pop()
    edges = tuple(zip(
        contour,
        (*contour[1:], contour[0]) if closed else contour[1:],
    ))
    for first_index, (a, b) in enumerate(edges):
        for second_index, (c, d) in enumerate(edges):
            if second_index <= first_index + 1 or (
                closed
                and first_index == 0
                and second_index == len(edges) - 1
            ):
                continue
            ab = (b[0] - a[0], b[1] - a[1])
            cd = (d[0] - c[0], d[1] - c[1])
            denominator = ab[0] * cd[1] - ab[1] * cd[0]
            if abs(denominator) <= 1.0e-12:
                continue
            delta = (c[0] - a[0], c[1] - a[1])
            first = (delta[0] * cd[1] - delta[1] * cd[0]) / denominator
            second = (delta[0] * ab[1] - delta[1] * ab[0]) / denominator
            if (
                1.0e-9 < first < 1.0 - 1.0e-9
                and 1.0e-9 < second < 1.0 - 1.0e-9
            ):
                return ()

    result: list[SampledCurve] = []
    for curve, _original, sampled, parameters in sampled_curves:
        result.append(SampledCurve(
            curve.entity_id,
            curve.entity_type,
            tuple(sampled),
            tuple(parameters),
            curve.closed,
            curve.trimmable,
            curve.creates_trim_points,
        ))
    return tuple(result)


def _segment_intersection(a: Point2, b: Point2, c: Point2, d: Point2) -> tuple[float, float] | None:
    ab = (b[0] - a[0], b[1] - a[1])
    cd = (d[0] - c[0], d[1] - c[1])
    denominator = ab[0] * cd[1] - ab[1] * cd[0]
    if abs(denominator) <= 1.0e-12:
        return None
    offset = (c[0] - a[0], c[1] - a[1])
    first = (offset[0] * cd[1] - offset[1] * cd[0]) / denominator
    second = (offset[0] * ab[1] - offset[1] * ab[0]) / denominator
    if -1.0e-9 <= first <= 1.0 + 1.0e-9 and -1.0e-9 <= second <= 1.0 + 1.0e-9:
        return max(0.0, min(1.0, first)), max(0.0, min(1.0, second))
    return None


def _curve_intersections(first: SampledCurve, second: SampledCurve) -> tuple[tuple[float, float], ...]:
    result: list[tuple[float, float]] = []
    for i, (a, b) in enumerate(zip(first.points, first.points[1:])):
        for j, (c, d) in enumerate(zip(second.points, second.points[1:])):
            hit = _segment_intersection(a, b, c, d)
            if hit is None:
                continue
            ta = first.parameters[i] + hit[0] * (first.parameters[i + 1] - first.parameters[i])
            tb = second.parameters[j] + hit[1] * (second.parameters[j + 1] - second.parameters[j])
            if not any(abs(ta - old_a) < 1.0e-5 and abs(tb - old_b) < 1.0e-5 for old_a, old_b in result):
                result.append((ta, tb))
    return tuple(result)


def _nearest_curve_parameter(curve: SampledCurve, point: Point2) -> float:
    """Project a persisted sketch contact onto the transient trim curve."""
    best: tuple[float, float] | None = None
    for index, (first, second) in enumerate(
        zip(curve.points, curve.points[1:])
    ):
        dx = second[0] - first[0]
        dy = second[1] - first[1]
        length_squared = dx * dx + dy * dy
        factor = (
            0.0
            if length_squared <= 1.0e-20
            else max(
                0.0,
                min(
                    1.0,
                    (
                        (point[0] - first[0]) * dx
                        + (point[1] - first[1]) * dy
                    ) / length_squared,
                ),
            )
        )
        closest = (
            first[0] + factor * dx,
            first[1] + factor * dy,
        )
        candidate = (
            math.dist(point, closest),
            curve.parameters[index]
            + factor
            * (curve.parameters[index + 1] - curve.parameters[index]),
        )
        if best is None or candidate[0] < best[0]:
            best = candidate
    return best[1] if best is not None else 0.0


def _persisted_curve_contacts(
    entities: list[dict[str, Any]],
    curves: dict[str, SampledCurve],
) -> tuple[tuple[str, float, str], ...]:
    """Return exact model contacts which do not geometrically cross.

    A tangent merely touches a curve, so polyline intersection tests are not
    allowed to decide whether it creates a trim boundary.  Sketcher already
    persists that boundary as a contact point / curve attachment.
    """
    points = _point_map(entities)
    contacts: set[tuple[str, str]] = set()
    for entity in entities:
        entity_id = str(entity.get("id", ""))
        attachment = entity.get("curve_attachment")
        if entity_id in points and isinstance(attachment, dict):
            geometry_id = str(attachment.get("geometry_id", ""))
            if geometry_id in curves:
                contacts.add((geometry_id, entity_id))
        for constraint in entity.get("constraints", ()):
            if not isinstance(constraint, dict):
                continue
            contact_id = str(constraint.get("contact_point_id", ""))
            if not contact_id or contact_id not in points:
                continue
            for key in (
                "geometry_id",
                "curve_geometry_id",
                "circle_geometry_id",
                "first_curve_geometry_id",
                "second_curve_geometry_id",
            ):
                geometry_id = str(constraint.get(key, ""))
                if geometry_id in curves:
                    contacts.add((geometry_id, contact_id))
    return tuple(
        (
            geometry_id,
            _nearest_curve_parameter(curves[geometry_id], points[point_id]),
            point_id,
        )
        for geometry_id, point_id in sorted(contacts)
    )


def trim_topology(
    entities: list[dict[str, Any]],
    *,
    include_base_axes: bool = False,
) -> tuple[TrimPiece, ...]:
    curves = list(sample_sketch_curves(entities))
    if include_base_axes:
        extent = max(
            [1.0]
            + [
                abs(coordinate)
                for curve in curves
                for point in curve.points
                for coordinate in point
            ]
        ) * 2.0
        curves.extend((
            SampledCurve(
                "__sketch_axis_x__", "base_axis", 
                ((-extent, 0.0), (extent, 0.0)), (0.0, 1.0),
                False, False, True,
            ),
            SampledCurve(
                "__sketch_axis_y__", "base_axis",
                ((0.0, -extent), (0.0, extent)), (0.0, 1.0),
                False, False, True,
            ),
        ))
    cuts: dict[str, list[float]] = {curve.entity_id: [] for curve in curves}
    for index, first in enumerate(curves):
        for second in curves[index + 1:]:
            if not first.creates_trim_points or not second.creates_trim_points:
                continue
            for first_t, second_t in _curve_intersections(first, second):
                cuts[first.entity_id].append(first_t)
                cuts[second.entity_id].append(second_t)
    curves_by_id = {curve.entity_id: curve for curve in curves}
    for geometry_id, parameter, _point_id in _persisted_curve_contacts(
        entities, curves_by_id
    ):
        cuts[geometry_id].append(parameter)
    pieces: list[TrimPiece] = []
    for curve in curves:
        if not curve.trimmable:
            continue
        parameters = sorted({round(value, 8) for value in cuts[curve.entity_id]})
        if curve.closed:
            if len(parameters) < 2:
                intervals = ((0.0, 1.0),)
            else:
                intervals = tuple(zip(parameters, parameters[1:])) + ((parameters[-1], parameters[0] + 1.0),)
        else:
            boundaries = sorted({0.0, 1.0, *parameters})
            intervals = tuple(zip(boundaries, boundaries[1:]))
        for start, end in intervals:
            if end - start <= 1.0e-7:
                continue
            pieces.append(TrimPiece(
                curve.entity_id, start, end,
                sample_curve_interval(curve, start, end), curve.closed,
            ))
    return tuple(pieces)


def _curve_point(curve: SampledCurve, parameter: float) -> Point2:
    value = parameter % 1.0 if curve.closed and parameter > 1.0 else parameter
    value = max(0.0, min(1.0, value))
    scaled = value * (len(curve.points) - 1)
    index = min(len(curve.points) - 2, int(math.floor(scaled)))
    fraction = scaled - index
    a, b = curve.points[index], curve.points[index + 1]
    return (a[0] + fraction * (b[0] - a[0]), a[1] + fraction * (b[1] - a[1]))


def sample_curve_interval(curve: SampledCurve, start: float, end: float) -> tuple[Point2, ...]:
    span = end - start
    count = max(2, int(math.ceil(span * (len(curve.points) - 1))))
    return tuple(_curve_point(curve, start + span * index / count) for index in range(count + 1))


def nearest_trim_piece(pieces: Iterable[TrimPiece], position: Point2, tolerance: float) -> TrimPiece | None:
    best: tuple[float, TrimPiece] | None = None
    for piece in pieces:
        for a, b in zip(piece.points, piece.points[1:]):
            dx, dy = b[0] - a[0], b[1] - a[1]
            length_squared = dx * dx + dy * dy
            factor = 0.0 if length_squared <= 1.0e-20 else max(0.0, min(1.0, ((position[0]-a[0])*dx + (position[1]-a[1])*dy) / length_squared))
            closest = (a[0] + factor * dx, a[1] + factor * dy)
            distance = math.dist(position, closest)
            if distance <= tolerance and (best is None or distance < best[0]):
                best = (distance, piece)
    return best[1] if best else None


def pieces_crossed_by_path(pieces: Iterable[TrimPiece], path: tuple[Point2, ...], tolerance: float) -> tuple[TrimPiece, ...]:
    if len(path) < 2:
        piece = nearest_trim_piece(pieces, path[0], tolerance) if path else None
        return (piece,) if piece else ()
    selected: list[TrimPiece] = []
    for piece in pieces:
        if any(
            _segment_intersection(a, b, c, d) is not None
            for a, b in zip(piece.points, piece.points[1:])
            for c, d in zip(path, path[1:])
        ):
            selected.append(piece)
    return tuple(selected)


def apply_trim_pieces(
    entities: list[dict[str, Any]],
    removed: Iterable[TrimPiece],
    referenced_point_ids: Iterable[str] = (),
) -> tuple[list[dict[str, Any]], dict[str, list[str]]]:
    """Reconstruct entities after removing transient topology pieces."""
    removed_by_id: dict[str, list[tuple[float, float]]] = {}
    for piece in removed:
        removed_by_id.setdefault(piece.entity_id, []).append((piece.start, piece.end))
    if not removed_by_id:
        return copy.deepcopy(entities), {}
    curves = {curve.entity_id: curve for curve in sample_sketch_curves(entities)}
    source_by_id = {str(entity.get("id", "")): entity for entity in entities}
    persisted_contacts = _persisted_curve_contacts(entities, curves)
    output = [copy.deepcopy(entity) for entity in entities if str(entity.get("id", "")) not in removed_by_id]
    used_ids = {str(entity.get("id", "")) for entity in output}

    def next_id(prefix: str) -> str:
        index = 1
        while f"{prefix}{index}" in used_ids:
            index += 1
        value = f"{prefix}{index}"
        used_ids.add(value)
        return value

    point_cache = {
        (round(float(entity.get("x", 0.0)), 9), round(float(entity.get("y", 0.0)), 9)): str(entity.get("id", ""))
        for entity in output if entity.get("type") == "point"
    }

    def point_id(position: Point2, preferred: str = "") -> str:
        if (
            preferred
            and preferred in used_ids
            and source_by_id.get(preferred, {}).get("type") == "point"
        ):
            return preferred
        key = (round(position[0], 9), round(position[1], 9))
        existing = point_cache.get(key)
        if existing:
            return existing
        identifier = preferred if preferred and preferred not in used_ids else next_id("p")
        used_ids.add(identifier)
        output.append({"id": identifier, "type": "point", "x": position[0], "y": position[1]})
        point_cache[key] = identifier
        return identifier

    def contact_point_id(entity_id: str, parameter: float) -> str:
        curve = curves.get(entity_id)
        matches: list[tuple[float, str]] = []
        for geometry_id, contact_parameter, identifier in persisted_contacts:
            if geometry_id != entity_id:
                continue
            distance = abs(parameter - contact_parameter)
            if curve is not None and curve.closed:
                distance = min(distance, abs(distance - 1.0))
            matches.append((distance, identifier))
        if not matches:
            return ""
        distance, identifier = min(matches)
        return identifier if distance <= 1.0e-6 else ""

    mapping: dict[str, list[str]] = {}
    for entity_id, intervals in removed_by_id.items():
        source = source_by_id.get(entity_id)
        curve = curves.get(entity_id)
        if source is None or curve is None:
            continue
        cuts = sorted(intervals)
        survivors: list[tuple[float, float]] = []
        domains = [(0.0, 1.0)]
        for cut_start, cut_end in cuts:
            normalized_cuts = (
                ((cut_start, 1.0), (0.0, cut_end - 1.0))
                if cut_end > 1.0 else ((cut_start, cut_end),)
            )
            for normalized_start, normalized_end in normalized_cuts:
                revised: list[tuple[float, float]] = []
                for start, end in domains:
                    if normalized_end <= start + 1e-8 or normalized_start >= end - 1e-8:
                        revised.append((start, end))
                    else:
                        if normalized_start > start + 1e-8:
                            revised.append((start, normalized_start))
                        if normalized_end < end - 1e-8:
                            revised.append((normalized_end, end))
                domains = revised
        survivors = domains
        if curve.closed and len(survivors) >= 2 and survivors[0][0] <= 1e-8 and survivors[-1][1] >= 1.0 - 1e-8:
            survivors = [(survivors[-1][0], survivors[0][1] + 1.0), *survivors[1:-1]]
        source_ids = tuple(map(str, source.get("point_ids", ())))
        generated: list[str] = []
        for survivor_index, (start, end) in enumerate(survivors):
            sampled = sample_curve_interval(curve, start, end)
            if len(sampled) < 2:
                continue
            geometry_id = entity_id if survivor_index == 0 else next_id("g")
            # The original ID belongs to the first surviving piece as well.
            # Reserve it before another survivor asks next_id("g"); otherwise
            # trimming more than one piece in a gesture can allocate that same
            # ID again (for example two geometries named ``g2``).
            used_ids.add(geometry_id)
            generated.append(geometry_id)
            geometry = copy.deepcopy(source)
            geometry["id"] = geometry_id
            geometry.pop("rim_coincident", None)
            if survivor_index:
                reusable_constraints = [
                    copy.deepcopy(constraint)
                    for constraint in source.get("constraints", ())
                    if isinstance(constraint, dict)
                    and source.get("type") == "segment"
                    and constraint.get("type") in ("horizontal", "vertical")
                ]
                if reusable_constraints:
                    geometry["constraints"] = reusable_constraints
                else:
                    geometry.pop("constraints", None)
            if source.get("type") == "segment":
                first_preferred = source_ids[0] if start <= 1e-8 and source_ids else ""
                second_preferred = source_ids[1] if end >= 1.0 - 1e-8 and len(source_ids) > 1 else ""
                geometry["point_ids"] = [point_id(sampled[0], first_preferred), point_id(sampled[-1], second_preferred)]
            elif source.get("type") in ("circle", "arc"):
                center_id = source_ids[0]
                geometry["type"] = "arc"
                geometry["arc_mode"] = "center"
                geometry["clockwise"] = bool(source.get("clockwise", False))
                geometry["point_ids"] = [
                    center_id,
                    point_id(
                        sampled[0], contact_point_id(entity_id, start)
                    ),
                    point_id(
                        sampled[-1], contact_point_id(entity_id, end)
                    ),
                ]
            elif source.get("type") in ("ellipse", "elliptical_arc"):
                geometry["type"] = "elliptical_arc"
                geometry["clockwise"] = bool(source.get("clockwise", False))
                geometry["point_ids"] = [
                    *source_ids[:3],
                    point_id(
                        sampled[0], contact_point_id(entity_id, start)
                    ),
                    point_id(
                        sampled[-1], contact_point_id(entity_id, end)
                    ),
                ]
            elif source.get("type") == "spline":
                step = max(1, len(sampled) // 12)
                controls = [sampled[index] for index in range(0, len(sampled), step)]
                if controls[-1] != sampled[-1]:
                    controls.append(sampled[-1])
                geometry["point_ids"] = [point_id(position) for position in controls]
            output.append(geometry)
        mapping[entity_id] = generated

    # Remove constraints that point to a deleted/ambiguous geometry and remap
    # the unambiguous one-piece case.
    valid_geometry_ids = {
        str(entity.get("id", "")) for entity in output
        if entity.get("type") != "point"
    }
    reference_keys = (
        "geometry_id", "line_geometry_id", "curve_geometry_id",
        "circle_geometry_id", "first_geometry_id", "second_geometry_id",
        "first_curve_geometry_id", "second_curve_geometry_id",
    )
    for entity in output:
        raw_constraints = entity.get("constraints")
        if not isinstance(raw_constraints, list):
            continue
        kept: list[dict[str, Any]] = []
        for constraint in raw_constraints:
            if not isinstance(constraint, dict):
                continue
            updated = copy.deepcopy(constraint)
            valid = True
            for key in reference_keys:
                reference = str(updated.get(key, ""))
                if not reference:
                    continue
                if reference in mapping:
                    replacements = mapping[reference]
                    if len(replacements) != 1:
                        valid = False
                        break
                    updated[key] = replacements[0]
                elif reference not in valid_geometry_ids:
                    valid = False
                    break
            if valid:
                kept.append(updated)
        if kept:
            entity["constraints"] = kept
        else:
            entity.pop("constraints", None)

    used_point_ids = {
        point_id for entity in output if entity.get("type") != "point"
        for point_id in map(str, entity.get("point_ids", ()))
    }
    used_point_ids.update(map(str, referenced_point_ids))
    for entity in output:
        if entity.get("type") != "point":
            continue
        entity_id = str(entity.get("id", ""))
        raw_constraints = entity.get("constraints")
        if isinstance(raw_constraints, list) and raw_constraints:
            used_point_ids.add(entity_id)
            for constraint in raw_constraints:
                if not isinstance(constraint, dict):
                    continue
                used_point_ids.update(
                    map(str, constraint.get("point_ids", ()))
                )
                for key in ("point_id", "contact_point_id", "vertex_id"):
                    reference = str(constraint.get(key, ""))
                    if reference:
                        used_point_ids.add(reference)
        attachment = entity.get("curve_attachment")
        if (
            isinstance(attachment, dict)
            and str(attachment.get("geometry_id", "")) in valid_geometry_ids
        ):
            used_point_ids.add(entity_id)
    for entity in output:
        if entity.get("type") != "point" or not isinstance(
            entity.get("constraints"), list
        ):
            continue
        constraints = [
            constraint
            for constraint in entity["constraints"]
            if not isinstance(constraint, dict)
            or not isinstance(constraint.get("point_ids"), list)
            or all(
                str(point_id) in used_point_ids
                for point_id in constraint["point_ids"]
            )
        ]
        if constraints:
            entity["constraints"] = constraints
        else:
            entity.pop("constraints", None)
    output = [
        entity for entity in output
        if entity.get("type") != "point"
        or str(entity.get("id", "")) in used_point_ids
        or bool(entity.get("constraints"))
        or bool(entity.get("dimension_locks"))
    ]
    return output, mapping
