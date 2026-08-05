from __future__ import annotations

from dataclasses import dataclass
from math import sqrt
from typing import Any

from OCC.Core.BRep import BRep_Tool
from OCC.Core.BRepAdaptor import BRepAdaptor_Curve
from OCC.Core.BRepMesh import BRepMesh_IncrementalMesh
from OCC.Core.BRepLib import BRepLib_ToolTriangulatedShape
from OCC.Core.GCPnts import GCPnts_QuasiUniformDeflection
from OCC.Core.TopAbs import (
    TopAbs_EDGE,
    TopAbs_FACE,
    TopAbs_REVERSED,
    TopAbs_VERTEX,
)
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
    topology_role: str = "sharp"


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


@dataclass(frozen=True)
class SilhouetteEdge:
    first: Point3
    second: Point3
    adjacent_normals: tuple[Point3, ...]
    owner_id: str = ""


def triangulate_shape(
    shape: Any,
    *,
    owner_id: str = "",
    edge_kind: str = "edge",
    edge_color: Point3 = (0.086, 0.098, 0.118),
    edge_label: str = "",
    linear_deflection: float = 0.2,
    angular_deflection: float = 0.35,
    edge_linear_deflection: float = 0.025,
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
            try:
                BRepLib_ToolTriangulatedShape.ComputeNormals(
                    face,
                    triangulation,
                )
                has_smooth_normals = triangulation.HasNormals()
            except (RuntimeError, TypeError, ValueError):
                has_smooth_normals = False
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
                for point, node_index in zip(points, node_indices):
                    triangle_positions.extend(point)
                    if has_smooth_normals:
                        node_normal = triangulation.Normal(
                            node_index
                        ).Transformed(transform)
                        smooth_normal = (
                            node_normal.X(),
                            node_normal.Y(),
                            node_normal.Z(),
                        )
                        if reversed_face:
                            smooth_normal = tuple(
                                -value for value in smooth_normal
                            )
                        triangle_normals.extend(smooth_normal)
                    else:
                        triangle_normals.extend(normal)
                    all_points.append(point)
                triangle_face_indices.append(face_index)
                triangle_owner_ids.append(owner_id)
        face_explorer.Next()

    edges: list[EdgePolyline] = []
    seen_edge_hashes: set[int] = set()
    faces: list[Any] = []
    edge_faces: dict[int, list[Any]] = {}
    face_explorer = TopExp_Explorer(shape, TopAbs_FACE)
    while face_explorer.More():
        face = face_explorer.Current()
        faces.append(face)
        face_edge_explorer = TopExp_Explorer(face, TopAbs_EDGE)
        face_edge_hashes: set[int] = set()
        while face_edge_explorer.More():
            edge_hash = hash(face_edge_explorer.Current())
            if edge_hash not in face_edge_hashes:
                edge_faces.setdefault(edge_hash, []).append(face)
                face_edge_hashes.add(edge_hash)
            face_edge_explorer.Next()
        face_explorer.Next()

    def topology_role(edge: Any, adjacent_faces: list[Any]) -> str:
        if edge_kind != "edge":
            return "auxiliary"
        for face in adjacent_faces:
            try:
                if BRep_Tool.IsClosed(edge, face):
                    return "seam"
            except (RuntimeError, TypeError, ValueError):
                continue
        if len(adjacent_faces) <= 1:
            return "boundary"
        try:
            if int(BRep_Tool.Continuity(
                edge, adjacent_faces[0], adjacent_faces[1]
            )) >= 1:
                return "tangent"
        except (RuntimeError, TypeError, ValueError):
            pass
        return "sharp"

    edge_index = 0
    edge_explorer = TopExp_Explorer(shape, TopAbs_EDGE)
    while edge_explorer.More():
        edge = edge_explorer.Current()
        edge_hash = hash(edge)
        if edge_hash in seen_edge_hashes:
            edge_explorer.Next()
            continue
        seen_edge_hashes.add(edge_hash)
        edge_index += 1
        adjacent_faces = edge_faces.get(edge_hash, [])
        points = (
            _triangulation_edge_points(edge, adjacent_faces)
            or _sample_edge(edge, edge_linear_deflection)
        )
        if len(points) >= 2:
            edges.append(
                EdgePolyline(
                    edge_index=edge_index,
                    points=tuple(points),
                    owner_id=owner_id,
                    element_kind=edge_kind,
                    base_color=edge_color,
                    label=edge_label,
                    topology_role=topology_role(edge, adjacent_faces),
                )
            )
            all_points.extend(points)
        edge_explorer.Next()

    vertices: list[PointMarker] = []
    seen_vertex_hashes: set[int] = set()
    vertex_index = 0
    vertex_explorer = TopExp_Explorer(shape, TopAbs_VERTEX)
    while vertex_explorer.More():
        vertex = vertex_explorer.Current()
        vertex_hash = hash(vertex)
        if vertex_hash in seen_vertex_hashes:
            vertex_explorer.Next()
            continue
        seen_vertex_hashes.add(vertex_hash)
        vertex_index += 1
        try:
            position = _point_tuple(BRep_Tool.Pnt(vertex))
        except (TypeError, RuntimeError):
            vertex_explorer.Next()
            continue
        vertices.append(
            PointMarker(
                point_index=vertex_index,
                position=position,
                owner_id=owner_id,
                element_kind="vertex",
            )
        )
        vertex_explorer.Next()

    if not all_points:
        return _empty_mesh()
    return ViewerMesh(
        triangle_positions=tuple(triangle_positions),
        triangle_normals=tuple(triangle_normals),
        triangle_face_indices=tuple(triangle_face_indices),
        triangle_owner_ids=tuple(triangle_owner_ids),
        edges=tuple(edges),
        points=tuple(vertices),
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
        "point": TopAbs_VERTEX,
        "vertex": TopAbs_VERTEX,
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
        if shape_type in (TopAbs_EDGE, TopAbs_VERTEX) and any(
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


def edge_visible_in_display(edge: EdgePolyline, display_mode: str) -> bool:
    """Apply the shared model/assembly edge visibility convention."""
    if edge.element_kind != "edge":
        return True
    if display_mode == "shaded":
        return False
    if edge.topology_role in {"seam", "periodic_tangent"}:
        return False
    if (
        display_mode == "shaded_with_edges"
        and edge.topology_role == "tangent"
    ):
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
            first_key = tuple(round(value, 7) for value in first)
            second_key = tuple(round(value, 7) for value in second)
            low, high = sorted((first_key, second_key))
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
            first_key = tuple(round(value, 7) for value in first)
            second_key = tuple(round(value, 7) for value in second)
            low, high = sorted((first_key, second_key))
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
        if len(records) < 2:
            continue
        # Real CAD edges are rendered by the regular edge pass with a proper
        # depth test. Drawing them again as painter silhouettes lets concave
        # rear edges shine through the solid, especially around sketch fillets.
        if key in topology_segments:
            continue
        if any(
            _point_polyline_distance(records[0][0], polyline)
            <= seam_tolerance
            and _point_polyline_distance(records[0][1], polyline)
            <= seam_tolerance
            for polyline in seam_polylines
        ):
            continue
        # A real boundary between two CAD faces (for example the circular
        # cap/side edge of a cylinder) is not a generated surface silhouette.
        # It is already drawn by the topology edge pass as one smooth curve.
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
        # At exact orthographic/45-degree views the silhouette can coincide
        # with a tessellation facet whose facing is numerically zero.  Treat
        # that limiting tangent as a silhouette too, but do not expose edges
        # where both adjacent facets are merely edge-on.
        crosses_view_plane = minimum < -epsilon and maximum > epsilon
        touches_from_back = minimum < -epsilon and maximum >= -epsilon
        touches_from_front = maximum > epsilon and minimum <= epsilon
        if crosses_view_plane or touches_from_back or touches_from_front:
            result.append((edge.first, edge.second))
    return tuple(result)


def silhouette_segments(
    mesh: ViewerMesh,
    view_direction: Point3,
) -> tuple[tuple[Point3, Point3], ...]:
    """Return triangulation edges separating front- and back-facing facets."""
    return silhouette_segments_from_edges(
        build_silhouette_edges(mesh),
        view_direction,
    )


def _point_polyline_distance(
    point: Point3,
    polyline: tuple[Point3, ...],
) -> float:
    best = float("inf")
    for first, second in zip(polyline, polyline[1:]):
        direction = tuple(second[axis] - first[axis] for axis in range(3))
        length_squared = sum(value * value for value in direction)
        if length_squared <= 1e-20:
            fraction = 0.0
        else:
            fraction = max(0.0, min(1.0, sum(
                (point[axis] - first[axis]) * direction[axis]
                for axis in range(3)
            ) / length_squared))
        closest = tuple(
            first[axis] + fraction * direction[axis]
            for axis in range(3)
        )
        best = min(best, sqrt(sum(
            (point[axis] - closest[axis]) ** 2
            for axis in range(3)
        )))
    return best


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
            topology_role=edge.topology_role,
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
            screen_constant=plane.screen_constant,
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
    screen_constant: bool = False,
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
                screen_constant=screen_constant,
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


def _triangulation_edge_points(
    edge: Any,
    faces: list[Any],
) -> list[Point3]:
    """Use the same boundary nodes as the surface depth geometry."""
    candidates: list[list[Point3]] = []
    for face in faces:
        explorer = TopExp_Explorer(face, TopAbs_EDGE)
        belongs_to_face = False
        while explorer.More():
            if edge.IsSame(explorer.Current()):
                belongs_to_face = True
                break
            explorer.Next()
        if not belongs_to_face:
            continue
        try:
            location = TopLoc_Location()
            triangulation = BRep_Tool.Triangulation(face, location)
            if triangulation is None:
                continue
            polygon = BRep_Tool.PolygonOnTriangulation(
                edge,
                triangulation,
                location,
            )
            if polygon is None:
                continue
            transform = location.Transformation()
            points = [
                _point_tuple(
                    triangulation.Node(polygon.Node(index)).Transformed(
                        transform
                    )
                )
                for index in range(1, polygon.NbNodes() + 1)
            ]
            points = [
                point for index, point in enumerate(points)
                if index == 0 or point != points[index - 1]
            ]
            if len(points) >= 2:
                candidates.append(points)
        except (RuntimeError, TypeError, ValueError):
            continue
    return max(candidates, key=len, default=[])


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
