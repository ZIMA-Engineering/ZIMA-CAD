from __future__ import annotations

from dataclasses import dataclass
from math import sqrt


Point3 = tuple[float, float, float]
BROWN: Point3 = (0.68, 0.43, 0.18)
BLACK: Point3 = (0.0, 0.0, 0.0)
X_AXIS_COLOR: Point3 = (0.91, 0.30, 0.24)
Y_AXIS_COLOR: Point3 = (0.18, 0.80, 0.44)
Z_AXIS_COLOR: Point3 = (0.20, 0.60, 0.86)


@dataclass(frozen=True)
class EdgePolyline:
    edge_index: int
    points: tuple[Point3, ...]
    owner_id: str = ""
    element_kind: str = "edge"
    base_color: Point3 = (0.086, 0.098, 0.118)
    label: str = ""
    screen_constant: bool = False
    topology_role: str = "sharp"
    curve_kind: str = "other"
    curve_origin: Point3 | None = None
    curve_direction: Point3 | None = None
    curve_radius: float | None = None


@dataclass(frozen=True)
class PointMarker:
    point_index: int
    position: Point3
    owner_id: str = ""
    element_kind: str = "point"
    base_color: Point3 = BLACK
    label: str = ""


@dataclass(frozen=True)
class PlanePatch:
    plane_index: int
    corners: tuple[Point3, Point3, Point3, Point3]
    owner_id: str = ""
    base_color: Point3 = BROWN
    label: str = ""
    screen_constant: bool = False


@dataclass(frozen=True)
class ViewerMesh:
    """OCCT-independent renderer data with ZIMA-owned element indices."""

    triangle_positions: tuple[float, ...]
    triangle_normals: tuple[float, ...]
    triangle_face_indices: tuple[int, ...]
    triangle_owner_ids: tuple[str, ...]
    edges: tuple[EdgePolyline, ...]
    points: tuple[PointMarker, ...]
    planes: tuple[PlanePatch, ...]
    bounds_min: Point3
    bounds_max: Point3

    @property
    def triangle_count(self) -> int:
        return len(self.triangle_face_indices)

    @property
    def is_empty(self) -> bool:
        return (
            not self.triangle_positions
            and not self.edges
            and not self.points
            and not self.planes
        )

    def to_dict(self) -> dict:
        return {
            "triangle_positions": self.triangle_positions,
            "triangle_normals": self.triangle_normals,
            "triangle_face_indices": self.triangle_face_indices,
            "triangle_owner_ids": self.triangle_owner_ids,
            "edges": tuple(edge.__dict__ for edge in self.edges),
            "points": tuple(point.__dict__ for point in self.points),
            "planes": tuple(plane.__dict__ for plane in self.planes),
            "bounds_min": self.bounds_min,
            "bounds_max": self.bounds_max,
        }

    @classmethod
    def from_dict(cls, value: dict) -> "ViewerMesh":
        def point3(point) -> Point3:
            return tuple(float(item) for item in point)

        return cls(
            triangle_positions=tuple(
                float(item) for item in value.get("triangle_positions", ())
            ),
            triangle_normals=tuple(
                float(item) for item in value.get("triangle_normals", ())
            ),
            triangle_face_indices=tuple(
                int(item) for item in value.get("triangle_face_indices", ())
            ),
            triangle_owner_ids=tuple(
                str(item) for item in value.get("triangle_owner_ids", ())
            ),
            edges=tuple(EdgePolyline(
                edge_index=int(item["edge_index"]),
                points=tuple(point3(point) for point in item.get("points", ())),
                owner_id=str(item.get("owner_id", "")),
                element_kind=str(item.get("element_kind", "edge")),
                base_color=point3(item.get("base_color", (0.086, 0.098, 0.118))),
                label=str(item.get("label", "")),
                screen_constant=bool(item.get("screen_constant", False)),
                topology_role=str(item.get("topology_role", "sharp")),
                curve_kind=str(item.get("curve_kind", "other")),
                curve_origin=(point3(item["curve_origin"]) if item.get("curve_origin") is not None else None),
                curve_direction=(point3(item["curve_direction"]) if item.get("curve_direction") is not None else None),
                curve_radius=(float(item["curve_radius"]) if item.get("curve_radius") is not None else None),
            ) for item in value.get("edges", ())),
            points=tuple(PointMarker(
                point_index=int(item["point_index"]),
                position=point3(item["position"]),
                owner_id=str(item.get("owner_id", "")),
                element_kind=str(item.get("element_kind", "point")),
                base_color=point3(item.get("base_color", BLACK)),
                label=str(item.get("label", "")),
            ) for item in value.get("points", ())),
            planes=tuple(PlanePatch(
                plane_index=int(item["plane_index"]),
                corners=tuple(point3(point) for point in item.get("corners", ())),
                owner_id=str(item.get("owner_id", "")),
                base_color=point3(item.get("base_color", BROWN)),
                label=str(item.get("label", "")),
                screen_constant=bool(item.get("screen_constant", False)),
            ) for item in value.get("planes", ())),
            bounds_min=point3(value.get("bounds_min", (0.0, 0.0, 0.0))),
            bounds_max=point3(value.get("bounds_max", (0.0, 0.0, 0.0))),
        )

    def with_owner(self, owner_id: str) -> "ViewerMesh":
        """Rebind one calculated Part mesh to an Assembly component."""
        return ViewerMesh(
            triangle_positions=self.triangle_positions,
            triangle_normals=self.triangle_normals,
            triangle_face_indices=self.triangle_face_indices,
            triangle_owner_ids=tuple(owner_id for _ in self.triangle_owner_ids),
            edges=tuple(EdgePolyline(
                **{**edge.__dict__, "owner_id": owner_id}
            ) for edge in self.edges),
            points=tuple(PointMarker(
                **{**point.__dict__, "owner_id": owner_id}
            ) for point in self.points),
            planes=tuple(PlanePatch(
                **{**plane.__dict__, "owner_id": owner_id}
            ) for plane in self.planes),
            bounds_min=self.bounds_min,
            bounds_max=self.bounds_max,
        )

    def face_mesh(self, owner_id: str, face_index: int) -> "ViewerMesh":
        """Return renderer data for one persisted face, without OCCT."""
        triangle_indices = tuple(
            index
            for index, (candidate_owner, candidate_face) in enumerate(zip(
                self.triangle_owner_ids,
                self.triangle_face_indices,
            ))
            if candidate_owner == owner_id and candidate_face == face_index
        )
        positions = tuple(
            value
            for index in triangle_indices
            for value in self.triangle_positions[index * 9:index * 9 + 9]
        )
        normals = tuple(
            value
            for index in triangle_indices
            for value in self.triangle_normals[index * 9:index * 9 + 9]
        )
        coordinates = tuple(zip(
            positions[0::3],
            positions[1::3],
            positions[2::3],
        ))
        boundary_segments: dict[
            tuple[Point3, Point3], tuple[Point3, Point3, int]
        ] = {}
        for triangle_offset in range(0, len(coordinates), 3):
            triangle = coordinates[triangle_offset:triangle_offset + 3]
            if len(triangle) != 3:
                continue
            for first_index, second_index in ((0, 1), (1, 2), (2, 0)):
                first = triangle[first_index]
                second = triangle[second_index]
                first_key = tuple(round(value, 9) for value in first)
                second_key = tuple(round(value, 9) for value in second)
                key = tuple(sorted((first_key, second_key)))
                previous = boundary_segments.get(key)
                boundary_segments[key] = (
                    first,
                    second,
                    1 if previous is None else previous[2] + 1,
                )
        boundary_edges = tuple(
            EdgePolyline(
                edge_index=edge_index,
                points=(first, second),
                owner_id=owner_id,
                element_kind="face_boundary",
            )
            for edge_index, (first, second, count) in enumerate(
                boundary_segments.values(),
                start=1,
            )
            if count == 1
        )
        bounds_min = tuple(
            min((point[axis] for point in coordinates), default=0.0)
            for axis in range(3)
        )
        bounds_max = tuple(
            max((point[axis] for point in coordinates), default=0.0)
            for axis in range(3)
        )
        return ViewerMesh(
            triangle_positions=positions,
            triangle_normals=normals,
            triangle_face_indices=tuple(face_index for _ in triangle_indices),
            triangle_owner_ids=tuple(owner_id for _ in triangle_indices),
            edges=boundary_edges,
            points=(),
            planes=(),
            bounds_min=bounds_min,
            bounds_max=bounds_max,
        )

    def edge_mesh(self, owner_id: str, edge_index: int) -> "ViewerMesh":
        """Return renderer data for one persisted edge, without OCCT."""
        edges = tuple(
            edge for edge in self.edges
            if edge.owner_id == owner_id and edge.edge_index == edge_index
        )
        coordinates = tuple(point for edge in edges for point in edge.points)
        return ViewerMesh(
            triangle_positions=(),
            triangle_normals=(),
            triangle_face_indices=(),
            triangle_owner_ids=(),
            edges=edges,
            points=(),
            planes=(),
            bounds_min=tuple(
                min((point[axis] for point in coordinates), default=0.0)
                for axis in range(3)
            ),
            bounds_max=tuple(
                max((point[axis] for point in coordinates), default=0.0)
                for axis in range(3)
            ),
        )

    def point_mesh(self, owner_id: str, point_index: int) -> "ViewerMesh":
        """Return renderer data for one persisted vertex, without OCCT."""
        points = tuple(
            point for point in self.points
            if point.owner_id == owner_id and point.point_index == point_index
        )
        coordinates = tuple(point.position for point in points)
        return ViewerMesh(
            triangle_positions=(),
            triangle_normals=(),
            triangle_face_indices=(),
            triangle_owner_ids=(),
            edges=(),
            points=points,
            planes=(),
            bounds_min=tuple(
                min((point[axis] for point in coordinates), default=0.0)
                for axis in range(3)
            ),
            bounds_max=tuple(
                max((point[axis] for point in coordinates), default=0.0)
                for axis in range(3)
            ),
        )


@dataclass(frozen=True)
class SilhouetteEdge:
    first: Point3
    second: Point3
    adjacent_normals: tuple[Point3, ...]
    owner_id: str = ""


def edge_visible_in_display(edge: EdgePolyline, display_mode: str) -> bool:
    """Apply the shared model/assembly edge visibility convention."""
    if edge.element_kind != "edge":
        return True
    if display_mode == "shaded":
        return False
    if edge.topology_role in {"seam", "periodic_tangent"}:
        return False
    if display_mode == "shaded_with_edges" and edge.topology_role == "tangent":
        return False
    return True


def build_silhouette_edges(mesh: ViewerMesh) -> tuple[SilhouetteEdge, ...]:
    """Precompute internal triangulation edges eligible for silhouettes."""
    topology_segments: set[tuple[str, Point3, Point3]] = set()
    seam_polylines: list[tuple[Point3, ...]] = []
    for edge in mesh.edges:
        if edge.element_kind != "edge":
            continue
        if edge.topology_role == "seam":
            seam_polylines.append(edge.points)
        for first, second in zip(edge.points, edge.points[1:]):
            low, high = sorted((_rounded_point(first), _rounded_point(second)))
            topology_segments.add((edge.owner_id, low, high))
    positions = mesh.triangle_positions
    owners = mesh.triangle_owner_ids
    shared: dict[
        tuple[str, Point3, Point3],
        list[tuple[Point3, Point3, Point3, int]],
    ] = {}
    for triangle_index, offset in enumerate(range(0, len(positions), 9)):
        points = tuple(
            (
                positions[offset + vertex * 3],
                positions[offset + vertex * 3 + 1],
                positions[offset + vertex * 3 + 2],
            )
            for vertex in range(3)
        )
        normal = _triangle_normal(*points)
        owner = owners[triangle_index]
        face_index = mesh.triangle_face_indices[triangle_index]
        for first, second in (
            (points[0], points[1]),
            (points[1], points[2]),
            (points[2], points[0]),
        ):
            low, high = sorted((_rounded_point(first), _rounded_point(second)))
            shared.setdefault((owner, low, high), []).append(
                (first, second, normal, face_index)
            )
    result: list[SilhouetteEdge] = []
    diagonal = sqrt(sum(
        (mesh.bounds_max[axis] - mesh.bounds_min[axis]) ** 2
        for axis in range(3)
    ))
    seam_tolerance = max(diagonal * 1e-3, 1e-7)
    for key, records in shared.items():
        if len(records) < 2 or key in topology_segments:
            continue
        if any(
            _point_polyline_distance(records[0][0], polyline) <= seam_tolerance
            and _point_polyline_distance(records[0][1], polyline) <= seam_tolerance
            for polyline in seam_polylines
        ):
            continue
        if len({record[3] for record in records}) != 1:
            continue
        result.append(SilhouetteEdge(
            owner_id=key[0],
            first=records[0][0],
            second=records[0][1],
            adjacent_normals=tuple(record[2] for record in records),
        ))
    return tuple(result)


def silhouette_segments_from_edges(
    edges: tuple[SilhouetteEdge, ...],
    view_direction: Point3,
) -> tuple[tuple[Point3, Point3], ...]:
    """Select cached edges separating front- and back-facing facets."""
    result: list[tuple[Point3, Point3]] = []
    for edge in edges:
        facings = tuple(
            sum(normal[axis] * view_direction[axis] for axis in range(3))
            for normal in edge.adjacent_normals
        )
        minimum = min(facings)
        maximum = max(facings)
        epsilon = 1.0e-9
        if (
            minimum < -epsilon and maximum > epsilon
            or minimum < -epsilon and maximum >= -epsilon
            or maximum > epsilon and minimum <= epsilon
        ):
            result.append((edge.first, edge.second))
    return tuple(result)


def silhouette_segments(
    mesh: ViewerMesh,
    view_direction: Point3,
) -> tuple[tuple[Point3, Point3], ...]:
    return silhouette_segments_from_edges(
        build_silhouette_edges(mesh), view_direction
    )


def _rounded_point(point: Point3) -> Point3:
    return tuple(round(value, 7) for value in point)


def _triangle_normal(first: Point3, second: Point3, third: Point3) -> Point3:
    ab = tuple(second[index] - first[index] for index in range(3))
    ac = tuple(third[index] - first[index] for index in range(3))
    cross = (
        ab[1] * ac[2] - ab[2] * ac[1],
        ab[2] * ac[0] - ab[0] * ac[2],
        ab[0] * ac[1] - ab[1] * ac[0],
    )
    length = sqrt(sum(value * value for value in cross))
    return (
        tuple(value / length for value in cross)
        if length > 1e-12
        else (0.0, 0.0, 1.0)
    )


def _point_polyline_distance(
    point: Point3,
    polyline: tuple[Point3, ...],
) -> float:
    best = float("inf")
    for first, second in zip(polyline, polyline[1:]):
        direction = tuple(second[axis] - first[axis] for axis in range(3))
        length_squared = sum(value * value for value in direction)
        fraction = 0.0 if length_squared <= 1e-20 else max(0.0, min(1.0, sum(
            (point[axis] - first[axis]) * direction[axis]
            for axis in range(3)
        ) / length_squared))
        closest = tuple(
            first[axis] + fraction * direction[axis] for axis in range(3)
        )
        best = min(best, sqrt(sum(
            (point[axis] - closest[axis]) ** 2 for axis in range(3)
        )))
    return best
