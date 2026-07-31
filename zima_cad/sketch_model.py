"""Point-based domain model for a ZIMA-CAD sketch.

The module is deliberately independent of Qt and OpenCascade.  The current
editor still works with flat dictionaries during an edit session;
``from_editor_data`` and ``to_editor_data`` are an in-memory UI boundary, not
a persistence compatibility layer.
"""

from __future__ import annotations

import math
import copy
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Iterable, Mapping


SKETCH_SCHEMA_VERSION = 3


class SketchModelError(ValueError):
    """Raised when a sketch violates a structural invariant."""


class GeometryType(str, Enum):
    SEGMENT = "segment"
    CONSTRUCTION = "construction"
    ARC = "arc"
    SPLINE = "spline"
    CIRCLE = "circle"


_POINT_COUNTS: dict[GeometryType, tuple[int, int | None]] = {
    GeometryType.SEGMENT: (2, 2),
    GeometryType.CONSTRUCTION: (2, 2),
    GeometryType.ARC: (3, 3),
    GeometryType.SPLINE: (2, None),
    # A circle owns one centre point and a scalar radius attribute.
    GeometryType.CIRCLE: (1, 1),
}

_CONSTRAINT_POINT_COUNTS = {
    "horizontal": 2,
    "vertical": 2,
    "coincident": 2,
    "perpendicular": None,
    "parallel": 4,
    "equal_length": 4,
    "point_on_reference": 1,
    "point_on_line": 3,
    "midpoint": 3,
    # Line start/end, circle centre and the explicit contact point.
    "tangent": 4,
}


def classify_linear_dimension(
    first: tuple[float, float],
    second: tuple[float, float],
    cursor: tuple[float, float],
) -> str:
    """Choose a two-point dimension from the A-B bounding rectangle."""

    minimum_x, maximum_x = sorted((first[0], second[0]))
    minimum_y, maximum_y = sorted((first[1], second[1]))
    x, y = cursor
    outside_x = x < minimum_x or x > maximum_x
    outside_y = y < minimum_y or y > maximum_y
    if not outside_x and not outside_y:
        return "distance"
    if outside_y and not outside_x:
        return "distance_x"
    if outside_x and not outside_y:
        return "distance_y"
    horizontal_gap = min(abs(x - minimum_x), abs(x - maximum_x))
    vertical_gap = min(abs(y - minimum_y), abs(y - maximum_y))
    return "distance_y" if horizontal_gap < vertical_gap else "distance_x"


@dataclass
class SketchPoint:
    point_id: str
    x: float
    y: float
    construction: bool = False
    external_reference_id: str | None = None
    attributes: dict[str, Any] = field(default_factory=dict)

    def position(self) -> tuple[float, float]:
        return self.x, self.y


@dataclass
class SketchGeometry:
    geometry_id: str
    geometry_type: GeometryType
    point_ids: tuple[str, ...]
    attributes: dict[str, Any] = field(default_factory=dict)


@dataclass
class SketchConstraint:
    constraint_id: str
    constraint_type: str
    point_ids: tuple[str, ...] = ()
    reference_ids: tuple[str, ...] = ()
    attributes: dict[str, Any] = field(default_factory=dict)


@dataclass
class SketchDimension:
    dimension_id: str
    dimension_type: str
    value: float
    point_ids: tuple[str, ...] = ()
    driving: bool = True
    attributes: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class DofAnalysis:
    variables: int
    equations: int
    rank: int

    @property
    def degrees_of_freedom(self) -> int:
        return max(0, self.variables - self.rank)


@dataclass
class SketchModel:
    """A sketch graph with points as its only positional state."""

    points: dict[str, SketchPoint] = field(default_factory=dict)
    geometry: dict[str, SketchGeometry] = field(default_factory=dict)
    constraints: dict[str, SketchConstraint] = field(default_factory=dict)
    dimensions: dict[str, SketchDimension] = field(default_factory=dict)
    schema_version: int = SKETCH_SCHEMA_VERSION

    def to_dict(self) -> dict[str, Any]:
        """Return the canonical, JSON-serializable schema."""

        self.validate()
        return {
            "version": self.schema_version,
            "points": {
                point_id: {
                    **point.attributes,
                    "x": point.x,
                    "y": point.y,
                    **({"construction": True} if point.construction else {}),
                    **(
                        {"external_reference_id": point.external_reference_id}
                        if point.external_reference_id is not None
                        else {}
                    ),
                }
                for point_id, point in self.points.items()
            },
            "geometry": {
                geometry_id: {
                    **geometry.attributes,
                    "type": geometry.geometry_type.value,
                    "points": list(geometry.point_ids),
                }
                for geometry_id, geometry in self.geometry.items()
            },
            "constraints": {
                constraint_id: {
                    **constraint.attributes,
                    "type": constraint.constraint_type,
                    **(
                        {"points": list(constraint.point_ids)}
                        if constraint.point_ids
                        else {}
                    ),
                    **(
                        {"references": list(constraint.reference_ids)}
                        if constraint.reference_ids
                        else {}
                    ),
                }
                for constraint_id, constraint in self.constraints.items()
            },
            "dimensions": {
                dimension_id: {
                    **dimension.attributes,
                    "type": dimension.dimension_type,
                    "value": dimension.value,
                    **(
                        {"points": list(dimension.point_ids)}
                        if dimension.point_ids
                        else {}
                    ),
                    "driving": dimension.driving,
                }
                for dimension_id, dimension in self.dimensions.items()
            },
        }

    @classmethod
    def from_dict(cls, data: Mapping[str, Any]) -> "SketchModel":
        version = int(data.get("version", 0))
        if version not in (2, SKETCH_SCHEMA_VERSION):
            raise SketchModelError(
                f"unsupported sketch schema version {version}"
            )
        model = cls(schema_version=SKETCH_SCHEMA_VERSION)
        legacy_circle_rim_ids: set[str] = set()
        raw_points = cls._mapping(data.get("points"), "points")
        for point_id, value in raw_points.items():
            raw = cls._mapping(value, f"point {point_id}")
            attributes = dict(raw)
            x = float(attributes.pop("x", 0.0))
            y = float(attributes.pop("y", 0.0))
            construction = bool(attributes.pop("construction", False))
            external_reference_id = attributes.pop(
                "external_reference_id", None
            )
            model.add_point(
                SketchPoint(
                    str(point_id),
                    x,
                    y,
                    construction,
                    (
                        str(external_reference_id)
                        if external_reference_id is not None
                        else None
                    ),
                    attributes,
                )
            )
        raw_geometry = cls._mapping(data.get("geometry"), "geometry")
        for geometry_id, value in raw_geometry.items():
            raw = dict(cls._mapping(value, f"geometry {geometry_id}"))
            geometry_type = GeometryType(str(raw.pop("type", "")))
            point_ids = tuple(map(str, raw.pop("points", ())))
            # Compatibility with the initial centre-plus-rim circle format.
            if geometry_type == GeometryType.CIRCLE and len(point_ids) == 2:
                centre = model.points.get(point_ids[0])
                rim = model.points.get(point_ids[1])
                if centre is not None and rim is not None:
                    raw.setdefault(
                        "radius",
                        math.dist(centre.position(), rim.position()),
                    )
                    legacy_circle_rim_ids.add(point_ids[1])
                    point_ids = point_ids[:1]
            model.add_geometry(
                SketchGeometry(
                    str(geometry_id), geometry_type, point_ids, raw
                )
            )
        raw_constraints = cls._mapping(
            data.get("constraints"), "constraints"
        )
        for constraint_id, value in raw_constraints.items():
            raw = dict(
                cls._mapping(value, f"constraint {constraint_id}")
            )
            if "geometry" in raw:
                raise SketchModelError(
                    f"constraint {constraint_id!r} must reference points, "
                    "not geometry"
                )
            model.add_constraint(
                SketchConstraint(
                    str(constraint_id),
                    str(raw.pop("type", "")),
                    tuple(map(str, raw.pop("points", ()))),
                    tuple(map(str, raw.pop("references", ()))),
                    raw,
                )
            )
        raw_dimensions = cls._mapping(data.get("dimensions"), "dimensions")
        for dimension_id, value in raw_dimensions.items():
            raw = dict(cls._mapping(value, f"dimension {dimension_id}"))
            model.add_dimension(
                SketchDimension(
                    str(dimension_id),
                    str(raw.pop("type", "")),
                    float(raw.pop("value", 0.0)),
                    tuple(map(str, raw.pop("points", ()))),
                    bool(raw.pop("driving", True)),
                    raw,
                )
            )
        for point_id in legacy_circle_rim_ids:
            if point_id in model.points and not model.point_users(point_id):
                model.remove_point(point_id)
        model.validate()
        return model

    @staticmethod
    def _mapping(value: Any, name: str) -> Mapping[str, Any]:
        if not isinstance(value, Mapping):
            raise SketchModelError(f"{name} must be an object")
        return value

    def to_editor_data(
        self,
    ) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
        """Return mutable flat data consumed by the current editor."""

        self.validate()
        entities: list[dict[str, Any]] = []
        by_id: dict[str, dict[str, Any]] = {}
        for point_id, point in self.points.items():
            entity = {
                **point.attributes,
                "type": "point",
                "id": point_id,
                "x": point.x,
                "y": point.y,
                **({"construction": True} if point.construction else {}),
            }
            by_id[point_id] = entity
            entities.append(entity)
        for geometry_id, geometry in self.geometry.items():
            entity = {
                **geometry.attributes,
                "id": geometry_id,
                "type": geometry.geometry_type.value,
                "point_ids": list(geometry.point_ids),
            }
            by_id[geometry_id] = entity
            entities.append(entity)

        def geometry_for_points(
            first_id: str,
            second_id: str,
        ) -> SketchGeometry | None:
            expected = {first_id, second_id}
            return next(
                (
                    geometry
                    for geometry in self.geometry.values()
                    if len(geometry.point_ids) == 2
                    and set(geometry.point_ids) == expected
                ),
                None,
            )

        for constraint in self.constraints.values():
            points = constraint.point_ids
            raw = {
                **constraint.attributes,
                "id": constraint.constraint_id,
                "type": constraint.constraint_type,
            }
            owner_id: str
            if constraint.constraint_type == "perpendicular":
                if len(points) == 2 and constraint.reference_ids:
                    owner_geometry = geometry_for_points(
                        points[0],
                        points[1],
                    )
                    if owner_geometry is None:
                        raise SketchModelError(
                            f"perpendicular constraint "
                            f"{constraint.constraint_id!r} has no connector"
                        )
                    owner_id = owner_geometry.geometry_id
                elif len(points) not in (3, 4):
                    raise SketchModelError(
                        f"perpendicular constraint "
                        f"{constraint.constraint_id!r} requires 3 or 4 "
                        "points "
                        "or 2 points and an external reference"
                    )
                else:
                    reference = geometry_for_points(points[0], points[1])
                    owner_geometry = geometry_for_points(
                        points[1] if len(points) == 3 else points[2],
                        points[2] if len(points) == 3 else points[3],
                    )
                    if reference is None or owner_geometry is None:
                        raise SketchModelError(
                            f"perpendicular constraint "
                            f"{constraint.constraint_id!r} is not backed by "
                            "two connected geometries"
                        )
                    owner_id = owner_geometry.geometry_id
                    raw["geometry_id"] = reference.geometry_id
            elif constraint.constraint_type == "parallel":
                if len(points) != 4:
                    raise SketchModelError(
                        f"parallel constraint "
                        f"{constraint.constraint_id!r} requires 4 points"
                    )
                reference = geometry_for_points(points[0], points[1])
                owner_geometry = geometry_for_points(points[2], points[3])
                if reference is None or owner_geometry is None:
                    raise SketchModelError(
                        f"parallel constraint "
                        f"{constraint.constraint_id!r} has unresolved points"
                    )
                owner_id = owner_geometry.geometry_id
                raw["geometry_id"] = reference.geometry_id
            elif constraint.constraint_type == "equal_length":
                if len(points) != 4:
                    raise SketchModelError(
                        f"equal_length constraint "
                        f"{constraint.constraint_id!r} requires 4 points"
                    )
                reference = geometry_for_points(points[0], points[1])
                owner_geometry = geometry_for_points(points[2], points[3])
                if reference is None or owner_geometry is None:
                    raise SketchModelError(
                        f"equal_length constraint "
                        f"{constraint.constraint_id!r} has unresolved points"
                    )
                owner_id = owner_geometry.geometry_id
                raw["geometry_id"] = reference.geometry_id
            elif constraint.constraint_type == "tangent":
                line_id = str(
                    constraint.attributes.get("line_geometry_id", "")
                )
                circle_id = str(
                    constraint.attributes.get("circle_geometry_id", "")
                )
                line = self.geometry.get(line_id)
                circle = self.geometry.get(circle_id)
                if (
                    line is None
                    or line.geometry_type
                    not in {GeometryType.SEGMENT, GeometryType.CONSTRUCTION}
                    or circle is None
                    or circle.geometry_type != GeometryType.CIRCLE
                ):
                    raise SketchModelError(
                        f"tangent constraint "
                        f"{constraint.constraint_id!r} has invalid geometry"
                    )
                owner_id = line_id
                raw["geometry_id"] = circle_id
                raw.pop("line_geometry_id", None)
                raw.pop("circle_geometry_id", None)
            elif constraint.constraint_type in {"horizontal", "vertical"}:
                if len(points) != 2:
                    raise SketchModelError(
                        f"{constraint.constraint_type} constraint "
                        f"{constraint.constraint_id!r} requires 2 points"
                    )
                owner_geometry = geometry_for_points(points[0], points[1])
                if owner_geometry is None:
                    raise SketchModelError(
                        f"{constraint.constraint_type} constraint "
                        f"{constraint.constraint_id!r} has no connector"
                    )
                owner_id = owner_geometry.geometry_id
            elif constraint.constraint_type in ("point_on_line", "midpoint"):
                if len(points) != 3:
                    raise SketchModelError(
                        f"{constraint.constraint_type} constraint "
                        f"{constraint.constraint_id!r} requires 3 points"
                    )
                owner_id = points[0]
                raw["point_ids"] = list(points[1:])
            else:
                owner_id = points[0]
                if len(points) > 1:
                    raw["point_id"] = points[1]
            if constraint.reference_ids:
                raw["reference_id"] = constraint.reference_ids[0]
            owner = by_id.get(owner_id)
            if owner is None:
                raise SketchModelError(
                    f"constraint {constraint.constraint_id!r} has no owner"
                )
            owner.setdefault("constraints", []).append(raw)
        for dimension in self.dimensions.values():
            if dimension.dimension_type not in {
                "coordinate_x",
                "coordinate_y",
            }:
                continue
            if not dimension.point_ids:
                continue
            point = by_id[dimension.point_ids[0]]
            coordinate = dimension.dimension_type[-1]
            locks = set(map(str, point.get("dimension_locks", ())))
            if dimension.driving:
                locks.add(coordinate)
            if locks:
                point["dimension_locks"] = sorted(locks)
        dimensions = [
            {
                **dimension.attributes,
                "id": dimension_id,
                "type": dimension.dimension_type,
                "value": dimension.value,
                **(
                    {"point_ids": list(dimension.point_ids)}
                    if dimension.point_ids
                    else {}
                ),
                "locked": dimension.driving,
            }
            for dimension_id, dimension in self.dimensions.items()
            if dimension.dimension_type not in {
                "coordinate_x",
                "coordinate_y",
            }
        ]
        return entities, dimensions

    def add_point(self, point: SketchPoint) -> None:
        self._require_new_id(point.point_id)
        if not math.isfinite(point.x) or not math.isfinite(point.y):
            raise SketchModelError(
                f"point {point.point_id!r} has non-finite coordinates"
            )
        self.points[point.point_id] = point

    def add_geometry(self, geometry: SketchGeometry) -> None:
        self._require_new_id(geometry.geometry_id)
        self._validate_geometry(geometry)
        self.geometry[geometry.geometry_id] = geometry

    def add_constraint(self, constraint: SketchConstraint) -> None:
        self._require_new_id(constraint.constraint_id)
        self._validate_constraint(constraint)
        self.constraints[constraint.constraint_id] = constraint

    def add_dimension(self, dimension: SketchDimension) -> None:
        self._require_new_id(dimension.dimension_id)
        if not dimension.dimension_type:
            raise SketchModelError(
                f"dimension {dimension.dimension_id!r} has no type"
            )
        if not math.isfinite(dimension.value):
            raise SketchModelError(
                f"dimension {dimension.dimension_id!r} has non-finite value"
            )
        self._validate_references(
            dimension.point_ids,
            f"dimension {dimension.dimension_id}",
        )
        self.dimensions[dimension.dimension_id] = dimension

    def remove_point(self, point_id: str) -> None:
        users = self.point_users(point_id)
        if users:
            raise SketchModelError(
                f"point {point_id!r} is still referenced by: "
                + ", ".join(users)
            )
        try:
            del self.points[point_id]
        except KeyError as error:
            raise SketchModelError(f"unknown point {point_id!r}") from error

    def point_users(self, point_id: str) -> list[str]:
        users = [
            geometry_id
            for geometry_id, geometry in self.geometry.items()
            if point_id in geometry.point_ids
        ]
        users.extend(
            constraint_id
            for constraint_id, constraint in self.constraints.items()
            if point_id in constraint.point_ids
        )
        users.extend(
            dimension_id
            for dimension_id, dimension in self.dimensions.items()
            if point_id in dimension.point_ids
        )
        return users

    def constraint_residuals(
        self,
        constraint_id: str,
    ) -> tuple[float, ...]:
        """Evaluate a point constraint without consulting drawn geometry."""

        try:
            constraint = self.constraints[constraint_id]
        except KeyError as error:
            raise SketchModelError(
                f"unknown constraint {constraint_id!r}"
            ) from error
        self._validate_constraint(constraint)
        positions = [
            self.points[point_id].position()
            for point_id in constraint.point_ids
        ]
        if constraint.constraint_type == "horizontal":
            return (positions[1][1] - positions[0][1],)
        if constraint.constraint_type == "vertical":
            return (positions[1][0] - positions[0][0],)
        if constraint.constraint_type == "coincident":
            return (
                positions[1][0] - positions[0][0],
                positions[1][1] - positions[0][1],
            )
        if constraint.constraint_type == "midpoint":
            point, first, second = positions
            return (
                point[0] - (first[0] + second[0]) * 0.5,
                point[1] - (first[1] + second[1]) * 0.5,
            )
        if constraint.constraint_type == "perpendicular":
            if len(positions) == 2:
                direction = constraint.attributes.get(
                    "reference_direction",
                    (),
                )
                if (
                    not isinstance(direction, (list, tuple))
                    or len(direction) < 2
                ):
                    raise SketchModelError(
                        "external perpendicular constraint has no "
                        "reference direction"
                    )
                first, second = positions
                return (
                    (second[0] - first[0]) * float(direction[0])
                    + (second[1] - first[1]) * float(direction[1]),
                )
            if len(positions) == 3:
                first, vertex, third = positions
                return (
                    (first[0] - vertex[0]) * (third[0] - vertex[0])
                    + (first[1] - vertex[1]) * (third[1] - vertex[1]),
                )
            first, second, third, fourth = positions
            return (
                (second[0] - first[0]) * (fourth[0] - third[0])
                + (second[1] - first[1]) * (fourth[1] - third[1]),
            )
        if constraint.constraint_type == "parallel":
            first, second, third, fourth = positions
            return (
                (second[0] - first[0]) * (fourth[1] - third[1])
                - (second[1] - first[1]) * (fourth[0] - third[0]),
            )
        if constraint.constraint_type == "equal_length":
            first, second, third, fourth = positions
            return (
                (second[0] - first[0]) ** 2
                + (second[1] - first[1]) ** 2
                - (fourth[0] - third[0]) ** 2
                - (fourth[1] - third[1]) ** 2,
            )
        if constraint.constraint_type == "tangent":
            first, second, centre, contact = positions
            circle_id = str(
                constraint.attributes.get("circle_geometry_id", "")
            )
            circle = self.geometry.get(circle_id)
            if circle is None:
                raise SketchModelError(
                    f"tangent constraint {constraint_id!r} has no circle"
                )
            radius = float(circle.attributes.get("radius", 0.0))
            dx = second[0] - first[0]
            dy = second[1] - first[1]
            radial_x = contact[0] - centre[0]
            radial_y = contact[1] - centre[1]
            return (
                (contact[0] - first[0]) * dy
                - (contact[1] - first[1]) * dx,
                radial_x * radial_x + radial_y * radial_y
                - radius * radius,
                radial_x * dx + radial_y * dy,
            )
        if constraint.constraint_type == "point_on_line":
            point, first, second = positions
            return (
                (point[0] - first[0]) * (second[1] - first[1])
                - (point[1] - first[1]) * (second[0] - first[0]),
            )
        raise SketchModelError(
            f"constraint {constraint.constraint_type!r} has no residual"
        )

    def dof_analysis(self) -> DofAnalysis:
        equations = self._equation_values()
        jacobian = self._numerical_jacobian()
        return DofAnalysis(
            variables=len(self._solver_variables()),
            equations=len(equations),
            rank=self._matrix_rank(jacobian),
        )

    def dimension_dof_reduction(
        self,
        dimension: SketchDimension,
    ) -> int:
        """Return how many independent freedoms a new driving dimension uses."""

        if not dimension.driving:
            return 0
        candidate = copy.deepcopy(self)
        before = self.dof_analysis().degrees_of_freedom
        candidate.add_dimension(copy.deepcopy(dimension))
        after = candidate.dof_analysis().degrees_of_freedom
        return max(0, before - after)

    def _dimension_angle(self, dimension: SketchDimension) -> float | None:
        positions = [
            self.points[point_id].position()
            for point_id in dimension.point_ids
        ]
        if len(positions) == 3:
            first = (
                positions[0][0] - positions[1][0],
                positions[0][1] - positions[1][1],
            )
            second = (
                positions[2][0] - positions[1][0],
                positions[2][1] - positions[1][1],
            )
        elif len(positions) >= 4:
            first = (
                positions[1][0] - positions[0][0],
                positions[1][1] - positions[0][1],
            )
            second = (
                positions[3][0] - positions[2][0],
                positions[3][1] - positions[2][1],
            )
        elif len(positions) == 2:
            first = (
                positions[1][0] - positions[0][0],
                positions[1][1] - positions[0][1],
            )
            reference = str(
                dimension.attributes.get("reference_id", "")
            )
            second = (
                (1.0, 0.0)
                if reference == "sketch_axis:x"
                else (0.0, 1.0)
                if reference == "sketch_axis:y"
                else (0.0, 0.0)
            )
        else:
            return None
        first_length = math.hypot(*first)
        second_length = math.hypot(*second)
        if first_length <= 1.0e-12 or second_length <= 1.0e-12:
            return None
        cosine = max(
            -1.0,
            min(
                1.0,
                (
                    first[0] * second[0] + first[1] * second[1]
                ) / (first_length * second_length),
            ),
        )
        return math.degrees(math.acos(cosine))

    def violated_equations(
        self,
        linear_tolerance: float = 1.0e-6,
        angular_tolerance: float = 1.0e-8,
    ) -> tuple[str, ...]:
        """Return IDs of equations not satisfied by the current points."""

        violated: list[str] = []
        for constraint_id, constraint in self.constraints.items():
            positions = [
                self.points[point_id].position()
                for point_id in constraint.point_ids
            ]
            constraint_type = constraint.constraint_type
            errors: tuple[float, ...] = ()
            tolerance = linear_tolerance
            if constraint_type == "point_on_reference":
                x, y = positions[0]
                reference = (
                    constraint.reference_ids[0]
                    if constraint.reference_ids
                    else ""
                )
                if reference == "sketch_origin":
                    errors = (x, y)
                elif reference == "sketch_axis:x":
                    errors = (y,)
                elif reference == "sketch_axis:y":
                    errors = (x,)
            elif constraint_type in {
                "horizontal",
                "vertical",
                "coincident",
                "point_on_line",
                "midpoint",
            }:
                errors = self.constraint_residuals(constraint_id)
            elif constraint_type in {
                "perpendicular",
                "parallel",
                "equal_length",
            }:
                raw = self.constraint_residuals(constraint_id)[0]
                if constraint_type == "equal_length":
                    errors = (raw,)
                    tolerance = linear_tolerance
                    if any(abs(error) > tolerance for error in errors):
                        violated.append(constraint_id)
                    continue
                if constraint_type == "perpendicular":
                    if len(positions) == 2:
                        direction = constraint.attributes.get(
                            "reference_direction",
                            (),
                        )
                        first_length = math.dist(
                            positions[0],
                            positions[1],
                        )
                        second_length = (
                            math.hypot(
                                float(direction[0]),
                                float(direction[1]),
                            )
                            if isinstance(direction, (list, tuple))
                            and len(direction) >= 2
                            else 0.0
                        )
                    elif len(positions) == 3:
                        first, vertex, third = positions
                        first_length = math.dist(first, vertex)
                        second_length = math.dist(third, vertex)
                    else:
                        first, second, third, fourth = positions
                        first_length = math.dist(first, second)
                        second_length = math.dist(third, fourth)
                else:
                    first, second, third, fourth = positions
                    first_length = math.dist(first, second)
                    second_length = math.dist(third, fourth)
                scale = first_length * second_length
                errors = (
                    (raw / scale if scale > 1.0e-12 else math.inf),
                )
                tolerance = angular_tolerance
            elif constraint_type == "tangent":
                raw = self.constraint_residuals(constraint_id)
                first, second, _centre, contact = positions
                line_length = math.dist(first, second)
                scale = max(line_length, 1.0e-12)
                errors = (
                    raw[0] / scale,
                    raw[1] / max(scale * scale, 1.0e-12),
                    raw[2] / scale,
                )
                if line_length > 1.0e-12:
                    dx = second[0] - first[0]
                    dy = second[1] - first[1]
                    factor = (
                        (contact[0] - first[0]) * dx
                        + (contact[1] - first[1]) * dy
                    ) / (line_length * line_length)
                    if not (
                        -linear_tolerance
                        <= factor
                        <= 1.0 + linear_tolerance
                    ):
                        errors = (*errors, math.inf)
            if any(abs(error) > tolerance for error in errors):
                violated.append(constraint_id)
        for dimension_id, dimension in self.dimensions.items():
            if not dimension.driving:
                continue
            positions = [
                self.points[point_id].position()
                for point_id in dimension.point_ids
            ]
            actual: float | None = None
            if dimension.dimension_type == "angle":
                actual = self._dimension_angle(dimension)
            elif dimension.dimension_type == "coordinate_x" and positions:
                actual = positions[0][0]
            elif dimension.dimension_type == "coordinate_y" and positions:
                actual = positions[0][1]
            elif dimension.dimension_type == "distance_axis" and positions:
                reference = str(dimension.attributes.get("reference_id", ""))
                coordinate_index = 1 if reference == "sketch_axis:x" else 0
                actual = abs(sum(point[coordinate_index] for point in positions) / len(positions))
            elif len(positions) >= 2:
                dx = positions[1][0] - positions[0][0]
                dy = positions[1][1] - positions[0][1]
                if dimension.dimension_type == "distance_x":
                    actual = abs(dx)
                elif dimension.dimension_type == "distance_y":
                    actual = abs(dy)
                elif dimension.dimension_type == "distance":
                    actual = math.hypot(dx, dy)
                elif (
                    dimension.dimension_type == "distance_line"
                    and len(positions) >= 3
                ):
                    line_dx = positions[2][0] - positions[1][0]
                    line_dy = positions[2][1] - positions[1][1]
                    line_length = math.hypot(line_dx, line_dy)
                    if line_length > 1.0e-12:
                        actual = abs(
                            line_dx * (positions[0][1] - positions[1][1])
                            - line_dy * (positions[0][0] - positions[1][0])
                        ) / line_length
            if actual is not None:
                error = (
                    abs(
                        math.cos(math.radians(actual))
                        - math.cos(math.radians(dimension.value))
                    )
                    if dimension.dimension_type == "angle"
                    else abs(actual - dimension.value)
                )
                if error > linear_tolerance:
                    violated.append(dimension_id)
        return tuple(violated)

    def solve(self, max_iterations: int = 50) -> bool:
        """Solve all supported constraints and driving dimensions together."""

        if not self.points:
            return True
        solver_variables = self._solver_variables()
        distance_line_minimum_lengths = tuple(
            (
                dimension.point_ids[1],
                dimension.point_ids[2],
                max(
                    math.dist(
                        self.points[dimension.point_ids[1]].position(),
                        self.points[dimension.point_ids[2]].position(),
                    )
                    * 1.0e-3,
                    1.0e-6,
                ),
            )
            for dimension in self.dimensions.values()
            if (
                dimension.driving
                and dimension.dimension_type == "distance_line"
                and len(dimension.point_ids) >= 3
            )
        )
        damping = 1.0e-10
        for _iteration in range(max_iterations):
            residuals = list(self._equation_values())
            if not residuals:
                return True
            jacobian = self._numerical_jacobian()
            normalized_residuals: list[float] = []
            normalized_jacobian: list[list[float]] = []
            row_scales: list[float] = []
            for residual, row in zip(residuals, jacobian):
                scale = max(
                    1.0,
                    abs(residual),
                    *(abs(value) for value in row),
                )
                normalized_residuals.append(residual / scale)
                row_scales.append(scale)
                normalized_jacobian.append(
                    [value / scale for value in row]
                )
            if max(map(abs, normalized_residuals), default=0.0) < 1.0e-10:
                break
            variables = len(solver_variables)
            normal = [
                [0.0] * variables for _ in range(variables)
            ]
            right = [0.0] * variables
            for row, residual in zip(
                normalized_jacobian,
                normalized_residuals,
            ):
                for first in range(variables):
                    right[first] -= row[first] * residual
                    for second in range(variables):
                        normal[first][second] += (
                            row[first] * row[second]
                        )
            for index in range(variables):
                normal[index][index] += damping
            step = self._solve_linear_system(normal, right)
            if step is None:
                return False
            original = [
                self._solver_value(variable)
                for variable in solver_variables
            ]
            original_error = sum(
                residual * residual
                for residual in normalized_residuals
            )
            accepted = False
            factor = 1.0
            while factor >= 1.0e-4:
                for index, variable in enumerate(solver_variables):
                    self._set_solver_value(
                        variable,
                        original[index] + factor * step[index],
                    )
                if any(
                    self._solver_value(variable) <= 1.0e-12
                    for variable in solver_variables
                    if variable[0] == "circle_radius"
                ) or any(
                    math.dist(
                        self.points[first_id].position(),
                        self.points[second_id].position(),
                    )
                    <= minimum_length
                    for first_id, second_id, minimum_length
                    in distance_line_minimum_lengths
                ):
                    factor *= 0.5
                    continue
                candidate = self._equation_values()
                candidate_error = sum(
                    (value / scale) ** 2
                    for value, scale in zip(candidate, row_scales)
                )
                if candidate_error < original_error:
                    accepted = True
                    break
                factor *= 0.5
            if not accepted:
                for index, variable in enumerate(solver_variables):
                    self._set_solver_value(variable, original[index])
                return False
            if max(map(abs, step), default=0.0) * factor < 1.0e-10:
                break
        return not self.violated_equations(
            linear_tolerance=1.0e-5,
            angular_tolerance=1.0e-7,
        )

    def drive_all_dimensions_at_current_values(self) -> None:
        """Make every dimension driving while preserving current geometry."""
        for dimension in self.dimensions.values():
            positions = [
                self.points[point_id].position()
                for point_id in dimension.point_ids
            ]
            actual: float | None = None
            if dimension.dimension_type == "angle":
                actual = self._dimension_angle(dimension)
            elif dimension.dimension_type == "coordinate_x" and positions:
                actual = positions[0][0]
            elif dimension.dimension_type == "coordinate_y" and positions:
                actual = positions[0][1]
            elif dimension.dimension_type == "distance_axis" and positions:
                reference = str(dimension.attributes.get("reference_id", ""))
                coordinate_index = 1 if reference == "sketch_axis:x" else 0
                actual = abs(
                    sum(point[coordinate_index] for point in positions)
                    / len(positions)
                )
            elif len(positions) >= 2:
                dx = positions[1][0] - positions[0][0]
                dy = positions[1][1] - positions[0][1]
                if dimension.dimension_type == "distance_x":
                    actual = abs(dx)
                elif dimension.dimension_type == "distance_y":
                    actual = abs(dy)
                elif dimension.dimension_type == "distance":
                    actual = math.hypot(dx, dy)
                elif (
                    dimension.dimension_type == "distance_line"
                    and len(positions) >= 3
                ):
                    line_dx = positions[2][0] - positions[1][0]
                    line_dy = positions[2][1] - positions[1][1]
                    line_length = math.hypot(line_dx, line_dy)
                    if line_length > 1.0e-12:
                        actual = abs(
                            line_dx * (positions[0][1] - positions[1][1])
                            - line_dy * (positions[0][0] - positions[1][0])
                        ) / line_length
            if actual is not None:
                dimension.value = actual
            dimension.driving = True

    @staticmethod
    def _solve_linear_system(
        matrix: list[list[float]],
        right: list[float],
        tolerance: float = 1.0e-14,
    ) -> list[float] | None:
        size = len(right)
        augmented = [
            [*matrix[row], right[row]]
            for row in range(size)
        ]
        for column in range(size):
            pivot = max(
                range(column, size),
                key=lambda row: abs(augmented[row][column]),
            )
            if abs(augmented[pivot][column]) <= tolerance:
                return None
            augmented[column], augmented[pivot] = (
                augmented[pivot],
                augmented[column],
            )
            pivot_value = augmented[column][column]
            for item in range(column, size + 1):
                augmented[column][item] /= pivot_value
            for row in range(size):
                if row == column:
                    continue
                factor = augmented[row][column]
                if abs(factor) <= tolerance:
                    continue
                for item in range(column, size + 1):
                    augmented[row][item] -= (
                        factor * augmented[column][item]
                    )
        return [augmented[row][size] for row in range(size)]

    def _equation_values(self) -> tuple[float, ...]:
        values: list[float] = []
        for constraint in self.constraints.values():
            point_ids = constraint.point_ids
            positions = [
                self.points[point_id].position()
                for point_id in point_ids
            ]
            constraint_type = constraint.constraint_type
            if constraint_type == "point_on_reference":
                x, y = positions[0]
                reference = (
                    constraint.reference_ids[0]
                    if constraint.reference_ids
                    else ""
                )
                if reference == "sketch_origin":
                    values.extend((x, y))
                elif reference == "sketch_axis:x":
                    values.append(y)
                elif reference == "sketch_axis:y":
                    values.append(x)
            elif constraint_type in {
                "horizontal",
                "vertical",
                "coincident",
                "perpendicular",
                "parallel",
                "equal_length",
                "point_on_line",
                "midpoint",
                "tangent",
            }:
                values.extend(
                    self.constraint_residuals(constraint.constraint_id)
                )
        for dimension in self.dimensions.values():
            if not dimension.driving:
                continue
            positions = [
                self.points[point_id].position()
                for point_id in dimension.point_ids
            ]
            dimension_type = dimension.dimension_type
            target = dimension.value
            if dimension_type == "angle":
                actual = self._dimension_angle(dimension)
                values.append(
                    (
                        math.cos(math.radians(actual))
                        - math.cos(math.radians(target))
                    )
                    if actual is not None
                    else math.inf
                )
            elif dimension_type == "coordinate_x" and positions:
                values.append(positions[0][0] - target)
            elif dimension_type == "coordinate_y" and positions:
                values.append(positions[0][1] - target)
            elif dimension_type == "distance_axis" and positions:
                reference = str(dimension.attributes.get("reference_id", ""))
                coordinate_index = 1 if reference == "sketch_axis:x" else 0
                side = float(dimension.attributes.get("side_sign", 1.0))
                values.extend(
                    point[coordinate_index] - side * target
                    for point in positions[:2]
                )
            elif len(positions) >= 2:
                dx = positions[1][0] - positions[0][0]
                dy = positions[1][1] - positions[0][1]
                if dimension_type == "distance_x":
                    values.append(dx * dx - target * target)
                elif dimension_type == "distance_y":
                    values.append(dy * dy - target * target)
                elif dimension_type == "distance":
                    values.append(
                        dx * dx + dy * dy - target * target
                    )
                elif dimension_type == "distance_line" and len(positions) >= 3:
                    line_dx = positions[2][0] - positions[1][0]
                    line_dy = positions[2][1] - positions[1][1]
                    line_length_squared = (
                        line_dx * line_dx + line_dy * line_dy
                    )
                    cross = (
                        line_dx * (positions[0][1] - positions[1][1])
                        - line_dy * (positions[0][0] - positions[1][0])
                    )
                    values.append(
                        abs(cross)
                        / max(math.sqrt(line_length_squared), 1.0e-12)
                        - target
                    )
        return tuple(values)

    def _numerical_jacobian(self) -> list[list[float]]:
        variables = self._solver_variables()
        base = self._equation_values()
        if not base:
            return []
        jacobian = [
            [0.0] * len(variables)
            for _ in range(len(base))
        ]
        for column, variable in enumerate(variables):
            original = self._solver_value(variable)
            step = 1.0e-6 * max(1.0, abs(original))
            self._set_solver_value(variable, original + step)
            positive = self._equation_values()
            self._set_solver_value(variable, original - step)
            negative = self._equation_values()
            self._set_solver_value(variable, original)
            for row in range(len(base)):
                jacobian[row][column] = (
                    positive[row] - negative[row]
                ) / (2.0 * step)
        return jacobian

    def _solver_variables(self) -> list[tuple[str, str, str]]:
        variables = [
            ("point", point_id, coordinate)
            for point_id in self.points
            for coordinate in ("x", "y")
        ]
        variables.extend(
            ("circle_radius", geometry_id, "radius")
            for geometry_id, geometry in self.geometry.items()
            if geometry.geometry_type == GeometryType.CIRCLE
        )
        return variables

    def _solver_value(self, variable: tuple[str, str, str]) -> float:
        kind, entity_id, coordinate = variable
        if kind == "point":
            return float(getattr(self.points[entity_id], coordinate))
        return float(self.geometry[entity_id].attributes["radius"])

    def _set_solver_value(
        self,
        variable: tuple[str, str, str],
        value: float,
    ) -> None:
        kind, entity_id, coordinate = variable
        if kind == "point":
            setattr(self.points[entity_id], coordinate, value)
        else:
            self.geometry[entity_id].attributes["radius"] = value

    @staticmethod
    def _matrix_rank(
        matrix: list[list[float]],
        tolerance: float = 1.0e-9,
    ) -> int:
        if not matrix:
            return 0
        reduced = [row[:] for row in matrix]
        for row in reduced:
            scale = max((abs(value) for value in row), default=0.0)
            if scale > 0.0:
                for column in range(len(row)):
                    row[column] /= scale
        rows = len(reduced)
        columns = len(reduced[0])
        rank = 0
        for column in range(columns):
            pivot = max(
                range(rank, rows),
                key=lambda row: abs(reduced[row][column]),
                default=rank,
            )
            if abs(reduced[pivot][column]) <= tolerance:
                continue
            reduced[rank], reduced[pivot] = (
                reduced[pivot],
                reduced[rank],
            )
            pivot_value = reduced[rank][column]
            for item in range(column, columns):
                reduced[rank][item] /= pivot_value
            for row in range(rows):
                if row == rank:
                    continue
                factor = reduced[row][column]
                if abs(factor) <= tolerance:
                    continue
                for item in range(column, columns):
                    reduced[row][item] -= factor * reduced[rank][item]
            rank += 1
            if rank == rows:
                break
        return rank

    def validate(self) -> None:
        if self.schema_version != SKETCH_SCHEMA_VERSION:
            raise SketchModelError(
                f"unsupported sketch schema version {self.schema_version}"
            )
        all_ids: set[str] = set()
        for collection_name, collection in (
            ("point", self.points),
            ("geometry", self.geometry),
            ("constraint", self.constraints),
            ("dimension", self.dimensions),
        ):
            for entity_id in collection:
                if not entity_id:
                    raise SketchModelError(f"{collection_name} ID is empty")
                if entity_id in all_ids:
                    raise SketchModelError(f"duplicate ID {entity_id!r}")
                all_ids.add(entity_id)
        for geometry in self.geometry.values():
            self._validate_geometry(geometry)
        for constraint in self.constraints.values():
            self._validate_constraint(constraint)
        for dimension in self.dimensions.values():
            self._validate_references(
                dimension.point_ids,
                f"dimension {dimension.dimension_id}",
            )

    def _require_new_id(self, entity_id: str) -> None:
        if not entity_id:
            raise SketchModelError("entity ID is empty")
        if any(
            entity_id in collection
            for collection in (
                self.points,
                self.geometry,
                self.constraints,
                self.dimensions,
            )
        ):
            raise SketchModelError(f"duplicate ID {entity_id!r}")

    def _validate_geometry(self, geometry: SketchGeometry) -> None:
        minimum, maximum = _POINT_COUNTS[geometry.geometry_type]
        count = len(geometry.point_ids)
        if count < minimum or (maximum is not None and count > maximum):
            expected = str(minimum) if minimum == maximum else f"{minimum}+"
            raise SketchModelError(
                f"{geometry.geometry_type.value} {geometry.geometry_id!r} "
                f"requires {expected} points, got {count}"
            )
        missing = [
            point_id
            for point_id in geometry.point_ids
            if point_id not in self.points
        ]
        if missing:
            raise SketchModelError(
                f"geometry {geometry.geometry_id!r} references missing "
                f"points: {', '.join(missing)}"
            )
        if geometry.geometry_type == GeometryType.CIRCLE:
            try:
                radius = float(geometry.attributes["radius"])
            except (KeyError, TypeError, ValueError) as error:
                raise SketchModelError(
                    f"circle {geometry.geometry_id!r} requires a radius"
                ) from error
            if not math.isfinite(radius) or radius <= 1.0e-12:
                raise SketchModelError(
                    f"circle {geometry.geometry_id!r} requires a positive "
                    "radius"
                )

    def _validate_constraint(self, constraint: SketchConstraint) -> None:
        if not constraint.constraint_type:
            raise SketchModelError(
                f"constraint {constraint.constraint_id!r} has no type"
            )
        if not constraint.point_ids:
            raise SketchModelError(
                f"constraint {constraint.constraint_id!r} has no points"
            )
        expected = _CONSTRAINT_POINT_COUNTS.get(
            constraint.constraint_type
        )
        if (
            constraint.constraint_type == "perpendicular"
            and len(constraint.point_ids) not in (2, 3, 4)
        ):
            raise SketchModelError(
                f"perpendicular constraint "
                f"{constraint.constraint_id!r} requires 2, 3 or 4 points, "
                f"got {len(constraint.point_ids)}"
            )
        if expected is not None and len(constraint.point_ids) != expected:
            raise SketchModelError(
                f"{constraint.constraint_type} constraint "
                f"{constraint.constraint_id!r} requires {expected} points, "
                f"got {len(constraint.point_ids)}"
            )
        self._validate_references(
            constraint.point_ids,
            f"constraint {constraint.constraint_id}",
        )

    def _validate_references(
        self,
        point_ids: Iterable[str],
        owner: str,
    ) -> None:
        missing_points = [
            point_id for point_id in point_ids if point_id not in self.points
        ]
        if missing_points:
            raise SketchModelError(
                f"{owner} references missing points: "
                + ", ".join(missing_points)
            )

    @classmethod
    def from_editor_data(
        cls,
        entities: Iterable[Mapping[str, Any]],
        dimensions: Iterable[Mapping[str, Any]] = (),
    ) -> "SketchModel":
        """Build the canonical model from the current editor's flat data."""

        model = cls()
        legacy_circle_rim_ids: set[str] = set()
        raw_entities = [dict(entity) for entity in entities]
        for entity in raw_entities:
            if entity.get("type") != "point":
                continue
            point_id = str(entity.get("id", ""))
            known = {
                "id",
                "type",
                "x",
                "y",
                "construction",
                "constraints",
                "dimension_locks",
            }
            model.add_point(
                SketchPoint(
                    point_id=point_id,
                    x=float(entity.get("x", 0.0)),
                    y=float(entity.get("y", 0.0)),
                    construction=bool(entity.get("construction", False)),
                    attributes={
                        key: value
                        for key, value in entity.items()
                        if key not in known
                    },
                )
            )
        for entity in raw_entities:
            if entity.get("type") != "point":
                continue
            point_id = str(entity.get("id", ""))
            raw_locks = entity.get("dimension_locks", ())
            locks = (
                {str(coordinate) for coordinate in raw_locks}
                if isinstance(raw_locks, list)
                else set()
            )
            point = model.points[point_id]
            for coordinate in sorted(locks & {"x", "y"}):
                model.add_dimension(
                    SketchDimension(
                        dimension_id=f"coordinate:{point_id}:{coordinate}",
                        dimension_type=f"coordinate_{coordinate}",
                        value=float(getattr(point, coordinate)),
                        point_ids=(point_id,),
                        driving=True,
                    )
                )
        for entity in raw_entities:
            if entity.get("type") == "point":
                continue
            owner_id = str(entity.get("id", ""))
            raw_type = str(entity.get("type", ""))
            try:
                geometry_type = GeometryType(raw_type)
            except ValueError as error:
                raise SketchModelError(
                    f"unknown geometry type {raw_type!r}"
                ) from error
            known = {"id", "type", "point_ids", "constraints"}
            point_ids = tuple(map(str, entity.get("point_ids", ())))
            attributes = {
                key: value
                for key, value in entity.items()
                if key not in known
            }
            if geometry_type == GeometryType.CIRCLE and len(point_ids) == 2:
                centre = model.points.get(point_ids[0])
                rim = model.points.get(point_ids[1])
                if centre is not None and rim is not None:
                    attributes.setdefault(
                        "radius",
                        math.dist(centre.position(), rim.position()),
                    )
                    legacy_circle_rim_ids.add(point_ids[1])
                    point_ids = point_ids[:1]
            model.add_geometry(
                SketchGeometry(
                    geometry_id=owner_id,
                    geometry_type=geometry_type,
                    point_ids=point_ids,
                    attributes=attributes,
                )
            )
        used_constraint_ids = {
            str(constraint.get("id"))
            for entity in raw_entities
            for constraint in (
                entity.get("constraints", ())
                if isinstance(entity.get("constraints"), list)
                else ()
            )
            if isinstance(constraint, Mapping) and constraint.get("id")
        }
        constraint_index = 1
        for entity in raw_entities:
            owner_kind = (
                "point" if entity.get("type") == "point" else "geometry"
            )
            owner_id = str(entity.get("id", ""))
            raw_constraints = entity.get("constraints", ())
            if not isinstance(raw_constraints, list):
                continue
            for raw_constraint in raw_constraints:
                if not isinstance(raw_constraint, Mapping):
                    continue
                constraint = dict(raw_constraint)
                raw_constraint_id = constraint.pop("id", None)
                if raw_constraint_id:
                    constraint_id = str(raw_constraint_id)
                else:
                    while f"c{constraint_index}" in used_constraint_ids:
                        constraint_index += 1
                    constraint_id = f"c{constraint_index}"
                    used_constraint_ids.add(constraint_id)
                    constraint_index += 1
                constraint_type = str(constraint.pop("type", ""))
                target_point = constraint.pop("point_id", None)
                target_points = constraint.pop("point_ids", ())
                target_geometry = constraint.pop("geometry_id", None)
                target_reference = constraint.pop("reference_id", None)
                if owner_kind == "point":
                    point_ids = [owner_id]
                    if target_point is not None:
                        point_ids.append(str(target_point))
                    if isinstance(target_points, (list, tuple)):
                        point_ids.extend(map(str, target_points))
                else:
                    owner_geometry = model.geometry[owner_id]
                    owner_points = list(owner_geometry.point_ids)
                    if constraint_type in {"horizontal", "vertical"}:
                        point_ids = owner_points
                    elif constraint_type == "perpendicular":
                        if target_geometry is None:
                            if target_reference is None:
                                raise SketchModelError(
                                    "a perpendicular constraint requires "
                                    "a connector or external reference"
                                )
                            point_ids = owner_points
                        else:
                            reference = model.geometry[
                                str(target_geometry)
                            ]
                            shared = (
                                set(reference.point_ids) & set(owner_points)
                            )
                            if len(shared) == 1:
                                vertex = next(iter(shared))
                                reference_outer = next(
                                    point_id
                                    for point_id in reference.point_ids
                                    if point_id != vertex
                                )
                                owner_outer = next(
                                    point_id
                                    for point_id in owner_points
                                    if point_id != vertex
                                )
                                point_ids = [
                                    reference_outer,
                                    vertex,
                                    owner_outer,
                                ]
                            elif not shared:
                                point_ids = [
                                    *reference.point_ids,
                                    *owner_points,
                                ]
                            else:
                                raise SketchModelError(
                                    "perpendicular connectors must be "
                                    "different"
                                )
                    elif constraint_type == "parallel":
                        if target_geometry is None:
                            raise SketchModelError(
                                "a parallel constraint requires two "
                                "point pairs"
                            )
                        reference = model.geometry[str(target_geometry)]
                        point_ids = [
                            *reference.point_ids,
                            *owner_points,
                        ]
                    elif constraint_type == "equal_length":
                        if target_geometry is None:
                            raise SketchModelError(
                                "an equal-length constraint requires "
                                "two point pairs"
                            )
                        reference = model.geometry[str(target_geometry)]
                        point_ids = [
                            *reference.point_ids,
                            *owner_points,
                        ]
                    elif constraint_type == "tangent":
                        if target_geometry is None:
                            raise SketchModelError(
                                "a tangent constraint requires a line and "
                                "a circle"
                            )
                        target = model.geometry[str(target_geometry)]
                        if (
                            owner_geometry.geometry_type
                            in {
                                GeometryType.SEGMENT,
                                GeometryType.CONSTRUCTION,
                            }
                            and target.geometry_type == GeometryType.CIRCLE
                        ):
                            line = owner_geometry
                            circle = target
                        elif (
                            owner_geometry.geometry_type
                            == GeometryType.CIRCLE
                            and target.geometry_type
                            in {
                                GeometryType.SEGMENT,
                                GeometryType.CONSTRUCTION,
                            }
                        ):
                            line = target
                            circle = owner_geometry
                        else:
                            raise SketchModelError(
                                "tangent currently requires one line and "
                                "one circle"
                            )
                        point_ids = [
                            *line.point_ids,
                            circle.point_ids[0],
                        ]
                        contact_point_id = str(
                            constraint.get("contact_point_id", "")
                        )
                        if contact_point_id not in model.points:
                            raise SketchModelError(
                                "tangent constraint has no contact point"
                            )
                        point_ids.append(contact_point_id)
                        constraint["line_geometry_id"] = line.geometry_id
                        constraint["circle_geometry_id"] = (
                            circle.geometry_id
                        )
                    else:
                        point_ids = owner_points
                model.add_constraint(
                    SketchConstraint(
                        constraint_id=constraint_id,
                        constraint_type=constraint_type,
                        point_ids=tuple(point_ids),
                        reference_ids=(
                            (str(target_reference),)
                            if target_reference is not None
                            else ()
                        ),
                        attributes=constraint,
                    )
                )
        for index, raw_dimension in enumerate(dimensions, 1):
            dimension = dict(raw_dimension)
            dimension_id = str(dimension.pop("id", f"d{index}"))
            dimension_type = str(dimension.pop("type", ""))
            value = float(dimension.pop("value", 0.0))
            point_ids = tuple(map(str, dimension.pop("point_ids", ())))
            geometry_id = dimension.pop("geometry_id", None)
            if geometry_id is not None and not point_ids:
                geometry = model.geometry.get(str(geometry_id))
                if geometry is None:
                    raise SketchModelError(
                        f"dimension {dimension_id!r} references missing "
                        f"connector {geometry_id!r}"
                    )
                point_ids = geometry.point_ids
            model.add_dimension(
                SketchDimension(
                    dimension_id=dimension_id,
                    dimension_type=dimension_type,
                    value=value,
                    point_ids=point_ids,
                    driving=bool(
                        dimension.pop(
                            "driving",
                            dimension.pop("locked", True),
                        )
                    ),
                    attributes=dimension,
                )
            )
        for point_id in legacy_circle_rim_ids:
            if point_id in model.points and not model.point_users(point_id):
                model.remove_point(point_id)
        model.validate()
        return model
