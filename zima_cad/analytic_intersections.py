from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Literal


Point3 = tuple[float, float, float]
IntersectionError = Literal[
    "",
    "invalid_surface",
    "parallel",
    "miss",
    "behind",
]


@dataclass(frozen=True)
class RaySurfaceIntersections:
    distances: tuple[float, ...] = ()
    error: IntersectionError = ""


def _vector(value) -> Point3 | None:
    if not isinstance(value, (list, tuple)) or len(value) != 3:
        return None
    try:
        result = tuple(float(component) for component in value)
    except (TypeError, ValueError):
        return None
    return result if all(math.isfinite(component) for component in result) else None


def _normalized(value) -> Point3 | None:
    vector = _vector(value)
    if vector is None:
        return None
    length = math.sqrt(sum(component * component for component in vector))
    return (
        tuple(component / length for component in vector)
        if length > 1.0e-12 else None
    )


def _dot(first: Point3, second: Point3) -> float:
    return sum(first[index] * second[index] for index in range(3))


def _positive_roots(
    quadratic: float,
    linear: float,
    constant: float,
) -> RaySurfaceIntersections:
    if abs(quadratic) <= 1.0e-14:
        if abs(linear) <= 1.0e-14:
            return RaySurfaceIntersections(error="parallel")
        roots = (-constant / linear,)
    else:
        discriminant = linear * linear - 4.0 * quadratic * constant
        if discriminant < -1.0e-9:
            return RaySurfaceIntersections(error="miss")
        square_root = math.sqrt(max(0.0, discriminant))
        roots = (
            (-linear - square_root) / (2.0 * quadratic),
            (-linear + square_root) / (2.0 * quadratic),
        )
    positive = tuple(sorted(
        root for root in roots
        if math.isfinite(root) and root > 1.0e-7
    ))
    return (
        RaySurfaceIntersections(positive)
        if positive
        else RaySurfaceIntersections(error="behind")
    )


def ray_surface_intersections(
    surface_kind: str,
    ray_origin,
    ray_direction,
    *,
    center=None,
    plane_origin=None,
    plane_normal=None,
    axis_origin=None,
    axis_direction=None,
    radius=None,
    apex=None,
    semi_angle=None,
) -> RaySurfaceIntersections:
    """Intersect one forward ray with an infinite analytic support surface."""
    origin = _vector(ray_origin)
    direction = _normalized(ray_direction)
    if origin is None or direction is None:
        return RaySurfaceIntersections(error="invalid_surface")
    if surface_kind == "plane":
        surface_origin = _vector(plane_origin)
        normal = _normalized(plane_normal)
        if surface_origin is None or normal is None:
            return RaySurfaceIntersections(error="invalid_surface")
        denominator = _dot(normal, direction)
        if abs(denominator) <= 1.0e-12:
            return RaySurfaceIntersections(error="parallel")
        distance = _dot(tuple(
            surface_origin[index] - origin[index] for index in range(3)
        ), normal) / denominator
        return (
            RaySurfaceIntersections((distance,))
            if distance > 1.0e-7
            else RaySurfaceIntersections(error="behind")
        )
    if surface_kind == "sphere":
        surface_origin = _vector(center)
        try:
            surface_radius = float(radius)
        except (TypeError, ValueError):
            surface_radius = 0.0
        if surface_origin is None or surface_radius <= 1.0e-9:
            return RaySurfaceIntersections(error="invalid_surface")
        offset = tuple(
            origin[index] - surface_origin[index] for index in range(3)
        )
        return _positive_roots(
            1.0,
            2.0 * _dot(offset, direction),
            _dot(offset, offset) - surface_radius * surface_radius,
        )
    if surface_kind == "cylinder":
        surface_origin = _vector(axis_origin)
        axis = _normalized(axis_direction)
        try:
            surface_radius = float(radius)
        except (TypeError, ValueError):
            surface_radius = 0.0
        if (
            surface_origin is None
            or axis is None
            or surface_radius <= 1.0e-9
        ):
            return RaySurfaceIntersections(error="invalid_surface")
        offset = tuple(
            origin[index] - surface_origin[index] for index in range(3)
        )
        offset_axis = _dot(offset, axis)
        direction_axis = _dot(direction, axis)
        radial = tuple(
            offset[index] - offset_axis * axis[index] for index in range(3)
        )
        perpendicular = tuple(
            direction[index] - direction_axis * axis[index]
            for index in range(3)
        )
        quadratic = _dot(perpendicular, perpendicular)
        if quadratic <= 1.0e-14:
            return RaySurfaceIntersections(error="parallel")
        return _positive_roots(
            quadratic,
            2.0 * _dot(radial, perpendicular),
            _dot(radial, radial) - surface_radius * surface_radius,
        )
    if surface_kind == "cone":
        surface_origin = _vector(apex)
        axis = _normalized(axis_direction)
        try:
            angle = float(semi_angle)
        except (TypeError, ValueError):
            angle = 0.0
        if angle < 0.0 and axis is not None:
            axis = tuple(-component for component in axis)
            angle = -angle
        if (
            surface_origin is None
            or axis is None
            or not 1.0e-7 < angle < math.pi / 2.0 - 1.0e-7
        ):
            return RaySurfaceIntersections(error="invalid_surface")
        offset = tuple(
            origin[index] - surface_origin[index] for index in range(3)
        )
        cosine_squared = math.cos(angle) ** 2
        offset_axis = _dot(offset, axis)
        direction_axis = _dot(direction, axis)
        result = _positive_roots(
            direction_axis ** 2 - cosine_squared,
            2.0 * (
                offset_axis * direction_axis
                - cosine_squared * _dot(offset, direction)
            ),
            offset_axis ** 2 - cosine_squared * _dot(offset, offset),
        )
        if not result.distances:
            return result
        positive_nappe = tuple(
            distance for distance in result.distances
            if offset_axis + distance * direction_axis > 1.0e-7
        )
        return (
            RaySurfaceIntersections(positive_nappe)
            if positive_nappe
            else RaySurfaceIntersections(error="behind")
        )
    return RaySurfaceIntersections(error="invalid_surface")


def analytic_surface_side(
    surface_kind: str,
    point,
    *,
    center=None,
    axis_origin=None,
    axis_direction=None,
    radius=None,
    apex=None,
    semi_angle=None,
) -> Literal["inside", "outside", "on", "invalid"]:
    """Classify a point against the finite-side meaning used by Up-to."""
    location = _vector(point)
    if location is None:
        return "invalid"
    if surface_kind == "sphere":
        origin = _vector(center)
        axis = None
    elif surface_kind == "cylinder":
        origin = _vector(axis_origin)
        axis = _normalized(axis_direction)
    elif surface_kind == "cone":
        origin = _vector(apex)
        axis = _normalized(axis_direction)
    else:
        return "invalid"
    try:
        surface_radius = float(radius) if radius is not None else None
        angle = float(semi_angle) if semi_angle is not None else None
    except (TypeError, ValueError):
        return "invalid"
    if origin is None:
        return "invalid"
    offset = tuple(location[index] - origin[index] for index in range(3))
    scale = max(1.0, _dot(offset, offset), (surface_radius or 0.0) ** 2)
    tolerance = max(1.0e-8, scale * 1.0e-9)
    if surface_kind == "sphere":
        if surface_radius is None or surface_radius <= 1.0e-9:
            return "invalid"
        value = _dot(offset, offset) - surface_radius ** 2
    elif surface_kind == "cylinder":
        if axis is None or surface_radius is None or surface_radius <= 1.0e-9:
            return "invalid"
        axial = _dot(offset, axis)
        radial = tuple(
            offset[index] - axial * axis[index] for index in range(3)
        )
        value = _dot(radial, radial) - surface_radius ** 2
    else:
        if axis is None or angle is None:
            return "invalid"
        if angle < 0.0:
            axis = tuple(-component for component in axis)
            angle = -angle
        if not 1.0e-7 < angle < math.pi / 2.0 - 1.0e-7:
            return "invalid"
        axial = _dot(offset, axis)
        radial_squared = max(0.0, _dot(offset, offset) - axial ** 2)
        value = radial_squared - (axial * math.tan(angle)) ** 2
        if axial <= 0.0:
            return "outside"
    if abs(value) <= tolerance:
        return "on"
    return "inside" if value < 0.0 else "outside"
