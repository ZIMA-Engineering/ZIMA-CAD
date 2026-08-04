"""Pure geometric evaluation helpers shared by the sketch model and viewer."""

from __future__ import annotations

from dataclasses import dataclass
import math


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


Point2 = tuple[float, float]


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
