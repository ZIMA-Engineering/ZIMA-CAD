from __future__ import annotations

from collections.abc import Sequence
from math import atan2, cos, hypot, pi, sin
from typing import Any

from OCC.Core.GeomAPI import GeomAPI_Interpolate
from OCC.Core.gp import gp_Pnt, gp_Vec
from OCC.Core.TColgp import TColgp_Array1OfVec, TColgp_HArray1OfPnt
from OCC.Core.TColStd import TColStd_HArray1OfBoolean


Point2 = tuple[float, float]


def stored_spline_tangent(entity: dict[str, Any], name: str) -> Point2 | None:
    raw = entity.get(name)
    if not isinstance(raw, (list, tuple)) or len(raw) < 2:
        return None
    tangent = (float(raw[0]), float(raw[1]))
    return tangent if hypot(*tangent) > 1.0e-12 else None


def spline_endpoint_support_tangent(
    entities: Sequence[dict[str, Any]],
    point_id: str,
    points: dict[str, Point2],
) -> tuple[str, Point2] | None:
    """Return a tangent only when exactly one open curve ends at point_id."""
    matches: list[tuple[str, Point2]] = []
    point_entity = next((
        entity for entity in entities
        if entity.get("type") == "point"
        and str(entity.get("id", "")) == point_id
    ), None)
    attachment = (
        point_entity.get("curve_attachment")
        if isinstance(point_entity, dict)
        and isinstance(point_entity.get("curve_attachment"), dict)
        else {}
    )
    for entity in entities:
        entity_type = str(entity.get("type", ""))
        ids = tuple(map(str, entity.get("point_ids", ())))
        endpoints: tuple[str, str] | None = None
        if entity_type in ("segment", "construction") and len(ids) == 2:
            endpoints = (ids[0], ids[1])
        elif entity_type == "arc" and len(ids) >= 3:
            endpoints = (ids[1], ids[2]) if entity.get("arc_mode") == "center" else (ids[0], ids[-1])
        elif entity_type == "elliptical_arc" and len(ids) == 5:
            endpoints = (ids[3], ids[4])
        elif entity_type == "spline" and len(ids) >= 2 and ids[0] != ids[-1]:
            endpoints = (ids[0], ids[-1])
        attached_circle = (
            entity_type == "circle"
            and attachment.get("type") == "circle"
            and str(attachment.get("geometry_id", ""))
            == str(entity.get("id", ""))
        )
        if not attached_circle and (
            endpoints is None or point_id not in endpoints
        ):
            continue
        tangent: Point2 | None = None
        if entity_type in ("segment", "construction"):
            first, second = points.get(ids[0]), points.get(ids[1])
            if first is not None and second is not None:
                tangent = (second[0] - first[0], second[1] - first[1])
        elif entity_type == "arc" and entity.get("arc_mode") == "center":
            center, endpoint = points.get(ids[0]), points.get(point_id)
            if center is not None and endpoint is not None:
                radial = (endpoint[0] - center[0], endpoint[1] - center[1])
                tangent = ((radial[1], -radial[0]) if entity.get("clockwise") else (-radial[1], radial[0]))
        elif attached_circle and ids:
            center, endpoint = points.get(ids[0]), points.get(point_id)
            if center is not None and endpoint is not None:
                radial = (endpoint[0] - center[0], endpoint[1] - center[1])
                tangent = (-radial[1], radial[0])
        elif entity_type == "elliptical_arc":
            center, major, minor, endpoint = (
                points.get(ids[0]), points.get(ids[1]), points.get(ids[2]), points.get(point_id)
            )
            if None not in (center, major, minor, endpoint):
                ux, uy = major[0] - center[0], major[1] - center[1]
                vx, vy = minor[0] - center[0], minor[1] - center[1]
                major_length, minor_length = hypot(ux, uy), hypot(vx, vy)
                if major_length > 1.0e-12 and minor_length > 1.0e-12:
                    u = (ux / major_length, uy / major_length)
                    v = (vx / minor_length, vy / minor_length)
                    relative = (endpoint[0] - center[0], endpoint[1] - center[1])
                    angle = atan2((relative[0] * v[0] + relative[1] * v[1]) / minor_length,
                                  (relative[0] * u[0] + relative[1] * u[1]) / major_length)
                    tangent = (-major_length * sin(angle) * u[0] + minor_length * cos(angle) * v[0],
                               -major_length * sin(angle) * u[1] + minor_length * cos(angle) * v[1])
                    if entity.get("clockwise"):
                        tangent = (-tangent[0], -tangent[1])
        elif entity_type == "spline":
            tangent = stored_spline_tangent(entity, "start_tangent" if point_id == ids[0] else "end_tangent")
            if tangent is None:
                neighbour_id = ids[1] if point_id == ids[0] else ids[-2]
                endpoint, neighbour = points.get(point_id), points.get(neighbour_id)
                if endpoint is not None and neighbour is not None:
                    tangent = (neighbour[0] - endpoint[0], neighbour[1] - endpoint[1])
        if tangent is not None and hypot(*tangent) > 1.0e-12:
            matches.append((str(entity.get("id", "")), tangent))
    return matches[0] if len(matches) == 1 else None


def orient_tangent(tangent: Point2, direction: Point2) -> Point2:
    return tangent if tangent[0] * direction[0] + tangent[1] * direction[1] >= 0.0 else (-tangent[0], -tangent[1])


def sample_tangent_start_arc(
    start: Point2,
    end: Point2,
    tangent: Point2,
    segments: int = 48,
) -> tuple[Point2, ...]:
    """Preview the unique circular arc leaving start along tangent to end."""
    tangent_length = hypot(*tangent)
    if tangent_length <= 1.0e-12:
        return (start, end)
    tx, ty = tangent[0] / tangent_length, tangent[1] / tangent_length
    nx, ny = -ty, tx
    dx, dy = end[0] - start[0], end[1] - start[1]
    denominator = 2.0 * (dx * nx + dy * ny)
    if abs(denominator) <= 1.0e-10:
        return (start, end)
    signed_radius = (dx * dx + dy * dy) / denominator
    center = (start[0] + nx * signed_radius, start[1] + ny * signed_radius)
    radius = abs(signed_radius)
    if radius <= 1.0e-12:
        return (start, end)
    start_angle = atan2(start[1] - center[1], start[0] - center[0])
    end_angle = atan2(end[1] - center[1], end[0] - center[0])
    radial = (start[0] - center[0], start[1] - center[1])
    ccw_tangent = (-radial[1], radial[0])
    ccw = ccw_tangent[0] * tx + ccw_tangent[1] * ty >= 0.0
    sweep = (
        (end_angle - start_angle) % (2.0 * pi)
        if ccw
        else -((start_angle - end_angle) % (2.0 * pi))
    )
    count = max(8, int(segments))
    return tuple(
        (
            center[0] + radius * cos(start_angle + sweep * index / count),
            center[1] + radius * sin(start_angle + sweep * index / count),
        )
        for index in range(count + 1)
    )


def interpolated_spline_curve(
    points: Sequence[Point2],
    start_tangent: Point2 | None = None,
    end_tangent: Point2 | None = None,
):
    if len(points) < 2:
        return None
    periodic = len(points) >= 4 and points[0] == points[-1]
    interpolation_points: Sequence[Point2] = (
        points[:-1] if periodic else points
    )
    if not periodic and start_tangent is not None:
        # The first two inputs initially define the visible tangent arc.
        # Preserve that arc when the operation grows into a spline instead
        # of discarding it and globally re-interpolating from scratch. A
        # compact set of arc stations turns the guide into the initial spline
        # span while the remaining user points extend it naturally.
        seed_arc = sample_tangent_start_arc(
            interpolation_points[0],
            interpolation_points[1],
            start_tangent,
            segments=8,
        )
        if len(seed_arc) > 2:
            interpolation_points = (
                *seed_arc[:-1],
                *interpolation_points[1:],
            )
    poles = TColgp_HArray1OfPnt(1, len(interpolation_points))
    for index, point in enumerate(interpolation_points, 1):
        poles.SetValue(index, gp_Pnt(float(point[0]), float(point[1]), 0.0))
    interpolation = GeomAPI_Interpolate(poles, periodic, 1.0e-7)
    if not periodic and (start_tangent is not None or end_tangent is not None):
        tangents = TColgp_Array1OfVec(1, len(interpolation_points))
        flags = TColStd_HArray1OfBoolean(1, len(interpolation_points))
        for index in range(1, len(interpolation_points) + 1):
            tangents.SetValue(index, gp_Vec(0.0, 0.0, 0.0))
            flags.SetValue(index, False)
        for index, tangent in (
            (1, start_tangent),
            (len(interpolation_points), end_tangent),
        ):
            if tangent is None:
                continue
            tangents.SetValue(index, gp_Vec(float(tangent[0]), float(tangent[1]), 0.0))
            flags.SetValue(index, True)
        # Stored support tangents carry a reliable direction, but their
        # magnitude is merely the length of the source segment/curve
        # derivative. Feeding that magnitude directly into the interpolator
        # makes a long supporting line launch the spline into loops. Let OCC
        # scale constrained derivatives to the interpolation chord lengths
        # while preserving their directions.
        interpolation.Load(tangents, flags, True)
    interpolation.Perform()
    return interpolation.Curve() if interpolation.IsDone() else None


def sample_interpolated_spline(
    points: Sequence[Point2],
    start_tangent: Point2 | None = None,
    end_tangent: Point2 | None = None,
) -> tuple[Point2, ...]:
    try:
        curve = interpolated_spline_curve(points, start_tangent, end_tangent)
        if curve is None:
            return tuple(points)
        first = curve.FirstParameter()
        span = curve.LastParameter() - first
        count = max(32, min(256, len(points) * 32))
        return tuple(
            (
                curve.Value(first + span * index / count).X(),
                curve.Value(first + span * index / count).Y(),
            )
            for index in range(count + 1)
        )
    except (RuntimeError, TypeError, ValueError):
        return tuple(points)
