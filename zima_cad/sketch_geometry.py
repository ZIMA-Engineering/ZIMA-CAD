"""Pure geometric evaluation helpers shared by the sketch model and viewer."""

from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Any


def center_arc_points(
    center: tuple[float, float],
    start: tuple[float, float],
    end: tuple[float, float],
    segments: int = 32,
    *,
    clockwise: bool = False,
) -> tuple[tuple[float, float], ...]:
    """Sample the counter-clockwise centre/start/end circular arc."""

    radius = math.dist(center, start)
    if radius <= 1.0e-12:
        return ()
    start_angle = math.atan2(start[1] - center[1], start[0] - center[0])
    end_angle = math.atan2(end[1] - center[1], end[0] - center[0])
    sweep = (
        -((start_angle - end_angle) % (2.0 * math.pi))
        if clockwise
        else (end_angle - start_angle) % (2.0 * math.pi)
    )
    count = max(
        2,
        round(int(segments) * max(1.0, abs(sweep) / math.pi)),
    )
    sampled = tuple(
        (
            center[0] + radius * math.cos(start_angle + sweep * index / count),
            center[1] + radius * math.sin(start_angle + sweep * index / count),
        )
        for index in range(count + 1)
    )
    return sampled


def arc_cardinal_keypoints(
    center: tuple[float, float],
    start: tuple[float, float],
    end: tuple[float, float],
    *,
    clockwise: bool = False,
) -> tuple[tuple[int, tuple[float, float]], ...]:
    """Exact 0/90/180/270-degree points that lie on a finite arc."""
    radius = math.dist(center, start)
    if radius <= 1.0e-12:
        return ()
    start_angle = math.atan2(start[1] - center[1], start[0] - center[0])
    end_angle = math.atan2(end[1] - center[1], end[0] - center[0])
    total = (
        (start_angle - end_angle) % (2.0 * math.pi)
        if clockwise
        else (end_angle - start_angle) % (2.0 * math.pi)
    )
    result = []
    for degrees_value in (0, 90, 180, 270):
        angle = math.radians(degrees_value)
        travelled = (
            (start_angle - angle) % (2.0 * math.pi)
            if clockwise
            else (angle - start_angle) % (2.0 * math.pi)
        )
        if travelled <= total + 1.0e-10:
            result.append((degrees_value, (
                center[0] + radius * math.cos(angle),
                center[1] + radius * math.sin(angle),
            )))
    return tuple(result)


def elliptical_arc_cardinal_keypoints(
    center: tuple[float, float],
    major: tuple[float, float],
    minor: tuple[float, float],
    start: tuple[float, float],
    end: tuple[float, float],
    *,
    clockwise: bool = False,
) -> tuple[tuple[int, tuple[float, float]], ...]:
    """Parametric 0/90/180/270-degree points on a finite elliptic arc."""
    ax, ay = major[0] - center[0], major[1] - center[1]
    bx, by = minor[0] - center[0], minor[1] - center[1]
    determinant = ax * by - ay * bx
    if abs(determinant) <= 1.0e-12:
        return ()

    def parameter(point: tuple[float, float]) -> float:
        dx, dy = point[0] - center[0], point[1] - center[1]
        cosine = (dx * by - dy * bx) / determinant
        sine = (ax * dy - ay * dx) / determinant
        return math.atan2(sine, cosine)

    start_angle = parameter(start)
    end_angle = parameter(end)
    total = (
        (start_angle - end_angle) % (2.0 * math.pi)
        if clockwise
        else (end_angle - start_angle) % (2.0 * math.pi)
    )
    result = []
    for degrees_value in (0, 90, 180, 270):
        angle = math.radians(degrees_value)
        travelled = (
            (start_angle - angle) % (2.0 * math.pi)
            if clockwise
            else (angle - start_angle) % (2.0 * math.pi)
        )
        if travelled <= total + 1.0e-10:
            result.append((degrees_value, (
                center[0] + ax * math.cos(angle) + bx * math.sin(angle),
                center[1] + ay * math.cos(angle) + by * math.sin(angle),
            )))
    return tuple(result)


Point2 = tuple[float, float]


def outward_minor_arc_endpoint(
    start: Point2,
    tangent_direction: Point2,
    endpoint: Point2,
) -> Point2:
    """Keep a tangent arc on the forward, at-most-180-degree side.

    Reflecting only the backwards tangent component preserves which side of
    the source line the user selected while preventing a major arc from
    curling back across that source line.
    """
    length = math.hypot(*tangent_direction)
    if length <= 1.0e-12:
        return endpoint
    tx, ty = tangent_direction[0] / length, tangent_direction[1] / length
    dx, dy = endpoint[0] - start[0], endpoint[1] - start[1]
    forward = dx * tx + dy * ty
    if forward >= 0.0:
        return endpoint
    return (
        endpoint[0] - 2.0 * forward * tx,
        endpoint[1] - 2.0 * forward * ty,
    )


def polyline_arc_start_context(
    entities: list[dict[str, Any]], start_point_id: str,
) -> tuple[Point2, str | None, str | None] | None:
    """Resolve tangent direction and persistent centre support for an arc."""
    points = {
        str(item.get("id", "")): (float(item.get("x", 0.0)), float(item.get("y", 0.0)))
        for item in entities if item.get("type") == "point"
    }
    start = points.get(start_point_id)
    if start is None:
        return None
    incident = []
    for item in entities:
        if item.get("type") not in ("segment", "construction", "arc"):
            continue
        ids = tuple(map(str, item.get("point_ids", ())))
        endpoints = ids[1:3] if item.get("type") == "arc" else ids[:2]
        if start_point_id not in endpoints:
            continue
        priority = 0 if endpoints[-1:] == (start_point_id,) else 1
        if item.get("type") == "construction":
            priority += 2
        incident.append((priority, item, ids))
    if incident:
        _priority, item, ids = min(incident, key=lambda value: value[0])
        geometry_id = str(item.get("id", ""))
        if item.get("type") == "arc" and len(ids) == 3 and ids[0] in points:
            center = points[ids[0]]
            radial = (start[0] - center[0], start[1] - center[1])
            tangent = ((radial[1], -radial[0]) if item.get("clockwise")
                       else (-radial[1], radial[0]))
            return tangent, geometry_id, None
        if len(ids) == 2 and all(point_id in points for point_id in ids):
            other = points[ids[0] if ids[1] == start_point_id else ids[1]]
            line = (start[0] - other[0], start[1] - other[1])
            if item.get("type") == "construction":
                return (-line[1], line[0]), None, f"sketch_geometry:{geometry_id}"
            return line, geometry_id, None
    point = next((item for item in entities if item.get("type") == "point"
                  and str(item.get("id", "")) == start_point_id), None)
    constraints = point.get("constraints", ()) if point else ()
    if isinstance(constraints, list):
        for constraint in constraints:
            reference_id = str(constraint.get("reference_id", "")) if isinstance(constraint, dict) else ""
            if reference_id == "sketch_axis:x":
                return (0.0, 1.0), None, reference_id
            if reference_id == "sketch_axis:y":
                return (1.0, 0.0), None, reference_id
            if not isinstance(constraint, dict) or constraint.get("type") != "point_on_line":
                continue
            support_ids = tuple(map(str, constraint.get("point_ids", ())))
            support = next(
                (item for item in entities
                 if item.get("type") == "construction"
                 and tuple(map(str, item.get("point_ids", ()))) == support_ids),
                None,
            )
            if support is not None and len(support_ids) == 2 and all(pid in points for pid in support_ids):
                first, second = (points[pid] for pid in support_ids)
                line = (second[0] - first[0], second[1] - first[1])
                return (-line[1], line[0]), None, f"sketch_geometry:{support.get('id', '')}"
    return None


def valid_automatic_tangent(
    entities: list[dict[str, Any]], constraint: dict[str, Any],
) -> bool:
    """Whether a tangent candidate's contact really belongs to its curve."""
    if constraint.get("type") != "tangent":
        return True
    curve_id = str(constraint.get("geometry_id", ""))
    contact_id = str(constraint.get("contact_point_id", ""))
    curve = next((item for item in entities if str(item.get("id", "")) == curve_id
                  and item.get("type") in ("circle", "arc", "ellipse", "elliptical_arc")), None)
    contact = next((item for item in entities if item.get("type") == "point"
                    and str(item.get("id", "")) == contact_id), None)
    if curve is None or contact is None:
        return False
    ids = tuple(map(str, curve.get("point_ids", ())))
    if contact_id in ids[1:]:
        return True
    attachment = contact.get("curve_attachment")
    if isinstance(attachment, dict) and str(attachment.get("geometry_id", "")) == curve_id:
        return True
    points = {str(item.get("id", "")): (float(item.get("x", 0.0)), float(item.get("y", 0.0)))
              for item in entities if item.get("type") == "point"}
    if not ids or ids[0] not in points or contact_id not in points:
        return False
    center, position = points[ids[0]], points[contact_id]
    if curve.get("type") not in ("circle", "arc"):
        return False
    radius = float(curve.get("radius", 0.0))
    if radius <= 1.0e-12 and len(ids) >= 2 and ids[1] in points:
        radius = math.dist(center, points[ids[1]])
    if abs(math.dist(center, position) - radius) > max(1.0e-7, radius * 1.0e-7):
        return False
    if curve.get("type") == "arc" and len(ids) == 3 and all(pid in points for pid in ids):
        sampled = center_arc_points(center, points[ids[1]], points[ids[2]], segments=256,
                                    clockwise=bool(curve.get("clockwise", False)))
        return min((math.dist(position, point) for point in sampled), default=float("inf")) <= max(1.0e-5, radius * 1.0e-4)
    return True


@dataclass(frozen=True)
class CornerRadiusGeometry:
    first_tangent: Point2
    second_tangent: Point2
    center: Point2
    arc_points: tuple[Point2, ...]
    tangent_distance: float


def evaluate_corner_radius(
    vertex: Point2,
    first_outer: Point2,
    second_outer: Point2,
    radius: float,
    *,
    samples: int = 32,
) -> CornerRadiusGeometry | None:
    """Evaluate a fillet inside the two finite rays leaving ``vertex``."""

    first_vector = (
        first_outer[0] - vertex[0],
        first_outer[1] - vertex[1],
    )
    second_vector = (
        second_outer[0] - vertex[0],
        second_outer[1] - vertex[1],
    )
    first_length = math.hypot(*first_vector)
    second_length = math.hypot(*second_vector)
    if (
        not math.isfinite(radius)
        or radius <= 1.0e-12
        or first_length <= 1.0e-12
        or second_length <= 1.0e-12
    ):
        return None
    first_direction = (
        first_vector[0] / first_length,
        first_vector[1] / first_length,
    )
    second_direction = (
        second_vector[0] / second_length,
        second_vector[1] / second_length,
    )
    cosine = max(
        -1.0,
        min(
            1.0,
            first_direction[0] * second_direction[0]
            + first_direction[1] * second_direction[1],
        ),
    )
    angle = math.acos(cosine)
    if angle <= 1.0e-6 or math.pi - angle <= 1.0e-6:
        return None
    tangent = math.tan(angle * 0.5)
    sine = math.sin(angle * 0.5)
    if abs(tangent) <= 1.0e-12 or abs(sine) <= 1.0e-12:
        return None
    tangent_distance = radius / tangent
    if tangent_distance >= min(first_length, second_length):
        return None
    first_tangent = (
        vertex[0] + first_direction[0] * tangent_distance,
        vertex[1] + first_direction[1] * tangent_distance,
    )
    second_tangent = (
        vertex[0] + second_direction[0] * tangent_distance,
        vertex[1] + second_direction[1] * tangent_distance,
    )
    bisector = (
        first_direction[0] + second_direction[0],
        first_direction[1] + second_direction[1],
    )
    bisector_length = math.hypot(*bisector)
    if bisector_length <= 1.0e-12:
        return None
    center_distance = radius / sine
    center = (
        vertex[0] + bisector[0] / bisector_length * center_distance,
        vertex[1] + bisector[1] / bisector_length * center_distance,
    )
    first_angle = math.atan2(
        first_tangent[1] - center[1],
        first_tangent[0] - center[0],
    )
    second_angle = math.atan2(
        second_tangent[1] - center[1],
        second_tangent[0] - center[0],
    )
    sweep = (second_angle - first_angle + math.pi) % (2.0 * math.pi) - math.pi
    count = max(2, samples)
    arc_points = tuple(
        (
            center[0] + radius * math.cos(first_angle + sweep * index / count),
            center[1] + radius * math.sin(first_angle + sweep * index / count),
        )
        for index in range(count + 1)
    )
    return CornerRadiusGeometry(
        first_tangent,
        second_tangent,
        center,
        arc_points,
        tangent_distance,
    )


def corner_radius_from_drag(
    vertex: Point2,
    first_outer: Point2,
    second_outer: Point2,
    cursor: Point2,
) -> tuple[float, float] | None:
    """Return ``(radius, maximum_radius)`` for a drag along either ray."""

    vectors = (
        (
            first_outer[0] - vertex[0],
            first_outer[1] - vertex[1],
        ),
        (
            second_outer[0] - vertex[0],
            second_outer[1] - vertex[1],
        ),
    )
    lengths = tuple(math.hypot(*vector) for vector in vectors)
    if min(lengths) <= 1.0e-12:
        return None
    directions = tuple(
        (vector[0] / length, vector[1] / length)
        for vector, length in zip(vectors, lengths)
    )
    cosine = max(
        -1.0,
        min(
            1.0,
            directions[0][0] * directions[1][0]
            + directions[0][1] * directions[1][1],
        ),
    )
    angle = math.acos(cosine)
    tangent = math.tan(angle * 0.5)
    if (
        angle <= 1.0e-6
        or math.pi - angle <= 1.0e-6
        or tangent <= 1.0e-12
    ):
        return None
    cursor_vector = (cursor[0] - vertex[0], cursor[1] - vertex[1])
    distance = max(
        0.0,
        max(
            cursor_vector[0] * direction[0]
            + cursor_vector[1] * direction[1]
            for direction in directions
        ),
    )
    maximum_radius = min(lengths) * tangent * (1.0 - 1.0e-6)
    return min(distance * tangent, maximum_radius), maximum_radius
def ellipse_points(
    center: tuple[float, float],
    major: tuple[float, float],
    minor: tuple[float, float],
    *,
    segments: int = 96,
) -> tuple[tuple[float, float], ...]:
    """Sample an ellipse defined by its centre and two semi-axis vectors."""
    sampled = tuple(
        (
            center[0]
            + (major[0] - center[0]) * math.cos(angle)
            + (minor[0] - center[0]) * math.sin(angle),
            center[1]
            + (major[1] - center[1]) * math.cos(angle)
            + (minor[1] - center[1]) * math.sin(angle),
        )
        for angle in (
            math.tau * index / max(8, segments)
            for index in range(max(8, segments) + 1)
        )
    )
    return (*sampled[:-1], sampled[0])


def elliptical_arc_points(
    center: tuple[float, float],
    major: tuple[float, float],
    minor: tuple[float, float],
    start: tuple[float, float],
    end: tuple[float, float],
    *,
    clockwise: bool = False,
    segments: int = 64,
) -> tuple[tuple[float, float], ...]:
    """Sample an elliptical arc through projected start/end parameters."""
    ax, ay = major[0] - center[0], major[1] - center[1]
    bx, by = minor[0] - center[0], minor[1] - center[1]
    determinant = ax * by - ay * bx
    if abs(determinant) <= 1.0e-12:
        return ()

    def parameter(point: tuple[float, float]) -> float:
        px, py = point[0] - center[0], point[1] - center[1]
        cosine = (px * by - py * bx) / determinant
        sine = (ax * py - ay * px) / determinant
        return math.atan2(sine, cosine)

    first = parameter(start)
    last = parameter(end)
    if clockwise:
        while last >= first:
            last -= math.tau
    else:
        while last <= first:
            last += math.tau
    count = max(4, int(abs(last - first) / math.tau * segments))
    sampled = tuple(
        (
            center[0] + ax * math.cos(angle) + bx * math.sin(angle),
            center[1] + ay * math.cos(angle) + by * math.sin(angle),
        )
        for angle in (
            first + (last - first) * index / count
            for index in range(count + 1)
        )
    )
    return (start, *sampled[1:-1], end)
