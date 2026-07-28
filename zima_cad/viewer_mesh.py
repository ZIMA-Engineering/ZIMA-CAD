from __future__ import annotations

from dataclasses import dataclass
from math import sqrt
from typing import Any

from OCC.Core.BRep import BRep_Tool
from OCC.Core.BRepAdaptor import BRepAdaptor_Curve
from OCC.Core.BRepMesh import BRepMesh_IncrementalMesh
from OCC.Core.GCPnts import GCPnts_QuasiUniformDeflection
from OCC.Core.TopAbs import TopAbs_EDGE, TopAbs_FACE, TopAbs_REVERSED
from OCC.Core.TopExp import TopExp_Explorer
from OCC.Core.TopLoc import TopLoc_Location


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


@dataclass(frozen=True)
class ViewerMesh:
    """Renderer-owned shape data with stable OCCT topology indices."""

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


def triangulate_shape(
    shape: Any,
    *,
    owner_id: str = "",
    edge_kind: str = "edge",
    edge_color: Point3 = (0.086, 0.098, 0.118),
    edge_label: str = "",
    linear_deflection: float = 0.2,
    angular_deflection: float = 0.35,
) -> ViewerMesh:
    """Convert a TopoDS shape into ZIMA-CAD surface and edge buffers."""
    if shape is None or shape.IsNull():
        return _empty_mesh()

    BRepMesh_IncrementalMesh(
        shape,
        max(float(linear_deflection), 1e-6),
        False,
        max(float(angular_deflection), 1e-6),
        True,
    ).Perform()

    triangle_positions: list[float] = []
    triangle_normals: list[float] = []
    triangle_face_indices: list[int] = []
    triangle_owner_ids: list[str] = []
    all_points: list[Point3] = []

    face_index = 0
    face_explorer = TopExp_Explorer(shape, TopAbs_FACE)
    while face_explorer.More():
        face_index += 1
        face = face_explorer.Current()
        location = TopLoc_Location()
        triangulation = BRep_Tool.Triangulation(face, location)
        if triangulation is not None:
            transform = location.Transformation()
            reversed_face = face.Orientation() == TopAbs_REVERSED
            for triangle_index in range(
                1,
                triangulation.NbTriangles() + 1,
            ):
                node_indices = list(
                    triangulation.Triangle(triangle_index).Get()
                )
                if reversed_face:
                    node_indices[1], node_indices[2] = (
                        node_indices[2],
                        node_indices[1],
                    )
                points = [
                    _point_tuple(
                        triangulation.Node(node_index).Transformed(transform)
                    )
                    for node_index in node_indices
                ]
                normal = _triangle_normal(*points)
                for point in points:
                    triangle_positions.extend(point)
                    triangle_normals.extend(normal)
                    all_points.append(point)
                triangle_face_indices.append(face_index)
                triangle_owner_ids.append(owner_id)
        face_explorer.Next()

    edges: list[EdgePolyline] = []
    seen_edges: list[Any] = []
    edge_index = 0
    edge_explorer = TopExp_Explorer(shape, TopAbs_EDGE)
    while edge_explorer.More():
        edge = edge_explorer.Current()
        if any(edge.IsSame(existing) for existing in seen_edges):
            edge_explorer.Next()
            continue
        seen_edges.append(edge)
        edge_index += 1
        points = _sample_edge(edge, linear_deflection)
        if len(points) >= 2:
            edges.append(
                EdgePolyline(
                    edge_index=edge_index,
                    points=tuple(points),
                    owner_id=owner_id,
                    element_kind=edge_kind,
                    base_color=edge_color,
                    label=edge_label,
                )
            )
            all_points.extend(points)
        edge_explorer.Next()

    if not all_points:
        return _empty_mesh()
    return ViewerMesh(
        triangle_positions=tuple(triangle_positions),
        triangle_normals=tuple(triangle_normals),
        triangle_face_indices=tuple(triangle_face_indices),
        triangle_owner_ids=tuple(triangle_owner_ids),
        edges=tuple(edges),
        points=(),
        planes=(),
        bounds_min=tuple(
            min(point[axis] for point in all_points)
            for axis in range(3)
        ),
        bounds_max=tuple(
            max(point[axis] for point in all_points)
            for axis in range(3)
        ),
    )


def topology_subshape(
    shape: Any,
    *,
    element_kind: str,
    element_index: int,
) -> Any | None:
    """Resolve a Viewer topology index back to its OCCT subshape."""
    shape_type = {
        "edge": TopAbs_EDGE,
        "face": TopAbs_FACE,
    }.get(element_kind)
    if (
        shape is None
        or shape.IsNull()
        or shape_type is None
        or element_index <= 0
    ):
        return None
    explorer = TopExp_Explorer(shape, shape_type)
    seen: list[Any] = []
    current_index = 0
    while explorer.More():
        candidate = explorer.Current()
        if shape_type == TopAbs_EDGE and any(
            candidate.IsSame(existing)
            for existing in seen
        ):
            explorer.Next()
            continue
        seen.append(candidate)
        current_index += 1
        if current_index == element_index:
            return candidate
        explorer.Next()
    return None


def combine_viewer_meshes(meshes: tuple[ViewerMesh, ...]) -> ViewerMesh:
    """Combine independently owned shapes into one GPU-ready scene mesh."""
    visible_meshes = tuple(mesh for mesh in meshes if not mesh.is_empty)
    if not visible_meshes:
        return _empty_mesh()
    positions: list[float] = []
    normals: list[float] = []
    face_indices: list[int] = []
    owner_ids: list[str] = []
    edges: list[EdgePolyline] = []
    points: list[PointMarker] = []
    planes: list[PlanePatch] = []
    all_bounds_min = [mesh.bounds_min for mesh in visible_meshes]
    all_bounds_max = [mesh.bounds_max for mesh in visible_meshes]
    for mesh in visible_meshes:
        positions.extend(mesh.triangle_positions)
        normals.extend(mesh.triangle_normals)
        face_indices.extend(mesh.triangle_face_indices)
        owner_ids.extend(mesh.triangle_owner_ids)
        edges.extend(mesh.edges)
        points.extend(mesh.points)
        planes.extend(mesh.planes)
    return ViewerMesh(
        triangle_positions=tuple(positions),
        triangle_normals=tuple(normals),
        triangle_face_indices=tuple(face_indices),
        triangle_owner_ids=tuple(owner_ids),
        edges=tuple(edges),
        points=tuple(points),
        planes=tuple(planes),
        bounds_min=tuple(
            min(bounds[axis] for bounds in all_bounds_min)
            for axis in range(3)
        ),
        bounds_max=tuple(
            max(bounds[axis] for bounds in all_bounds_max)
            for axis in range(3)
        ),
    )


def origin_axes_mesh(
    *,
    owner_id: str,
    length: float,
    center: Point3 = (0.0, 0.0, 0.0),
    point_label: str = "0,0,0",
) -> ViewerMesh:
    """Create renderer-owned X/Y/Z origin axes without an OCCT presentation."""
    axis_length = max(float(length), 1e-6)
    colors = (
        X_AXIS_COLOR,
        Y_AXIS_COLOR,
        Z_AXIS_COLOR,
    )
    axes = (
        ((center[0] + axis_length, center[1], center[2]), "X"),
        ((center[0], center[1] + axis_length, center[2]), "Y"),
        ((center[0], center[1], center[2] + axis_length), "Z"),
    )
    edges = tuple(
        EdgePolyline(
            edge_index=index,
            points=(center, endpoint),
            owner_id=owner_id,
            element_kind="axis",
            base_color=color,
            label=label,
            screen_constant=True,
        )
        for index, ((endpoint, label), color) in enumerate(
            zip(axes, colors),
            start=1,
        )
    )
    endpoints = tuple(endpoint for endpoint, _label in axes)
    all_points = (center, *endpoints)
    return ViewerMesh(
        triangle_positions=(),
        triangle_normals=(),
        triangle_face_indices=(),
        triangle_owner_ids=(),
        edges=edges,
        points=(
            PointMarker(
                point_index=1,
                position=center,
                owner_id=owner_id,
                label=point_label,
            ),
        ),
        planes=(),
        bounds_min=tuple(
            min(point[axis] for point in all_points)
            for axis in range(3)
        ),
        bounds_max=tuple(
            max(point[axis] for point in all_points)
            for axis in range(3)
        ),
    )


def transform_viewer_mesh(mesh: ViewerMesh, transform) -> ViewerMesh:
    """Apply a model affine transform to renderer-owned reference geometry."""
    def transformed(point: Point3) -> Point3:
        return tuple(
            sum(transform[row][column] * point[column] for column in range(3))
            + transform[row][3]
            for row in range(3)
        )

    edges = tuple(
        EdgePolyline(
            edge_index=edge.edge_index,
            points=tuple(transformed(point) for point in edge.points),
            owner_id=edge.owner_id,
            element_kind=edge.element_kind,
            base_color=edge.base_color,
            label=edge.label,
            screen_constant=edge.screen_constant,
        )
        for edge in mesh.edges
    )
    points = tuple(
        PointMarker(
            point_index=point.point_index,
            position=transformed(point.position),
            owner_id=point.owner_id,
            element_kind=point.element_kind,
            base_color=point.base_color,
            label=point.label,
        )
        for point in mesh.points
    )
    planes = tuple(
        PlanePatch(
            plane_index=plane.plane_index,
            corners=tuple(transformed(point) for point in plane.corners),
            owner_id=plane.owner_id,
            base_color=plane.base_color,
            label=plane.label,
        )
        for plane in mesh.planes
    )
    all_points = [
        point for edge in edges for point in edge.points
    ] + [point.position for point in points] + [
        point for plane in planes for point in plane.corners
    ]
    return ViewerMesh(
        triangle_positions=mesh.triangle_positions,
        triangle_normals=mesh.triangle_normals,
        triangle_face_indices=mesh.triangle_face_indices,
        triangle_owner_ids=mesh.triangle_owner_ids,
        edges=edges,
        points=points,
        planes=planes,
        bounds_min=tuple(
            min(point[axis] for point in all_points) for axis in range(3)
        ),
        bounds_max=tuple(
            max(point[axis] for point in all_points) for axis in range(3)
        ),
    )


def point_marker_mesh(
    *,
    owner_id: str,
    position: Point3 = (0.0, 0.0, 0.0),
    label: str = "",
) -> ViewerMesh:
    return ViewerMesh(
        triangle_positions=(),
        triangle_normals=(),
        triangle_face_indices=(),
        triangle_owner_ids=(),
        edges=(),
        points=(
            PointMarker(
                point_index=1,
                position=position,
                owner_id=owner_id,
                label=label,
            ),
        ),
        planes=(),
        bounds_min=position,
        bounds_max=position,
    )


def datum_plane_mesh(
    *,
    owner_id: str,
    plane_index: int = 1,
    size: float,
    center: Point3 = (0.0, 0.0, 0.0),
    plane: str = "xy",
    label: str = "",
) -> ViewerMesh:
    """Create one square datum plane owned by the native Viewer."""
    half = max(float(size), 1e-6) * 0.5
    if plane == "xy":
        corners = (
            (center[0] - half, center[1] - half, center[2]),
            (center[0] + half, center[1] - half, center[2]),
            (center[0] + half, center[1] + half, center[2]),
            (center[0] - half, center[1] + half, center[2]),
        )
    elif plane == "yz":
        corners = (
            (center[0], center[1] - half, center[2] - half),
            (center[0], center[1] + half, center[2] - half),
            (center[0], center[1] + half, center[2] + half),
            (center[0], center[1] - half, center[2] + half),
        )
    elif plane == "xz":
        corners = (
            (center[0] - half, center[1], center[2] - half),
            (center[0] + half, center[1], center[2] - half),
            (center[0] + half, center[1], center[2] + half),
            (center[0] - half, center[1], center[2] + half),
        )
    else:
        raise ValueError(f"Unknown datum plane orientation: {plane}")
    return ViewerMesh(
        triangle_positions=(),
        triangle_normals=(),
        triangle_face_indices=(),
        triangle_owner_ids=(),
        edges=(),
        points=(),
        planes=(
            PlanePatch(
                plane_index=plane_index,
                corners=corners,
                owner_id=owner_id,
                label=label,
            ),
        ),
        bounds_min=tuple(
            min(point[axis] for point in corners)
            for axis in range(3)
        ),
        bounds_max=tuple(
            max(point[axis] for point in corners)
            for axis in range(3)
        ),
    )


def polyline_mesh(
    *,
    owner_id: str,
    polylines: tuple[tuple[Point3, ...], ...],
    element_kind: str,
    color: Point3,
) -> ViewerMesh:
    """Create a line-only Viewer layer, for example sketch geometry."""
    edges = tuple(
        EdgePolyline(
            edge_index=index,
            points=points,
            owner_id=owner_id,
            element_kind=element_kind,
            base_color=color,
        )
        for index, points in enumerate(polylines, start=1)
        if len(points) >= 2
    )
    all_points = tuple(
        point
        for edge in edges
        for point in edge.points
    )
    if not all_points:
        return _empty_mesh()
    return ViewerMesh(
        triangle_positions=(),
        triangle_normals=(),
        triangle_face_indices=(),
        triangle_owner_ids=(),
        edges=edges,
        points=(),
        planes=(),
        bounds_min=tuple(
            min(point[axis] for point in all_points)
            for axis in range(3)
        ),
        bounds_max=tuple(
            max(point[axis] for point in all_points)
            for axis in range(3)
        ),
    )


def _sample_edge(edge: Any, linear_deflection: float) -> list[Point3]:
    try:
        curve = BRepAdaptor_Curve(edge)
        sampler = GCPnts_QuasiUniformDeflection(
            curve,
            max(float(linear_deflection), 1e-6),
        )
        if not sampler.IsDone():
            return []
        return [
            _point_tuple(sampler.Value(index))
            for index in range(1, sampler.NbPoints() + 1)
        ]
    except (RuntimeError, TypeError, ValueError):
        return []


def _point_tuple(point: Any) -> Point3:
    return (float(point.X()), float(point.Y()), float(point.Z()))


def _triangle_normal(
    first: Point3,
    second: Point3,
    third: Point3,
) -> Point3:
    first_edge = tuple(second[i] - first[i] for i in range(3))
    second_edge = tuple(third[i] - first[i] for i in range(3))
    cross = (
        first_edge[1] * second_edge[2] - first_edge[2] * second_edge[1],
        first_edge[2] * second_edge[0] - first_edge[0] * second_edge[2],
        first_edge[0] * second_edge[1] - first_edge[1] * second_edge[0],
    )
    length = sqrt(sum(component * component for component in cross))
    if length <= 1e-15:
        return (0.0, 0.0, 1.0)
    return tuple(component / length for component in cross)


def _empty_mesh() -> ViewerMesh:
    return ViewerMesh(
        triangle_positions=(),
        triangle_normals=(),
        triangle_face_indices=(),
        triangle_owner_ids=(),
        edges=(),
        points=(),
        planes=(),
        bounds_min=(0.0, 0.0, 0.0),
        bounds_max=(0.0, 0.0, 0.0),
    )
