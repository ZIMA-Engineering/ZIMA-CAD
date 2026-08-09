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
