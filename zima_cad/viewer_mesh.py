from __future__ import annotations

from math import sqrt
from typing import Any

from OCC.Core.BRep import BRep_Tool
from OCC.Core.BRepAdaptor import BRepAdaptor_Curve
from OCC.Core.BRepMesh import BRepMesh_IncrementalMesh
from OCC.Core.BRepLib import BRepLib_ToolTriangulatedShape
from OCC.Core.GCPnts import GCPnts_QuasiUniformDeflection
from OCC.Core.GeomAbs import GeomAbs_Circle
from OCC.Core.TopAbs import (
    TopAbs_EDGE,
    TopAbs_FACE,
    TopAbs_REVERSED,
    TopAbs_VERTEX,
)
from OCC.Core.TopExp import TopExp_Explorer
from OCC.Core.TopLoc import TopLoc_Location


from zima_cad.viewer_data import (
    BLACK,
    BROWN,
    X_AXIS_COLOR,
    Y_AXIS_COLOR,
    Z_AXIS_COLOR,
    EdgePolyline,
    PlanePatch,
    Point3,
    PointMarker,
    SilhouetteEdge,
    ViewerMesh,
    build_silhouette_edges,
    edge_visible_in_display,
    silhouette_segments,
    silhouette_segments_from_edges,
)


def triangulate_shape(
    shape: Any,
    *,
    owner_id: str = "",
    edge_kind: str = "edge",
    edge_color: Point3 = (1.0, 1.0, 1.0),
    edge_label: str = "",
    linear_deflection: float = 0.2,
    angular_deflection: float = 0.35,
    edge_linear_deflection: float = 0.025,
    include_topology: bool = True,
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

    if not include_topology:
        if not all_points:
            return _empty_mesh()
        return ViewerMesh(
            triangle_positions=tuple(triangle_positions),
            triangle_normals=tuple(triangle_normals),
            triangle_face_indices=tuple(triangle_face_indices),
            triangle_owner_ids=tuple(triangle_owner_ids),
            edges=(),
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

    edges: list[EdgePolyline] = []
    # Use OCCT identity, just like the topology registry. Python hashes can
    # collide and used to shift displayed indices on complex curved bodies.
    unique_edges: list[Any] = []
    faces: list[Any] = []
    edge_faces: list[list[Any]] = []
    face_explorer = TopExp_Explorer(shape, TopAbs_FACE)
    while face_explorer.More():
        face = face_explorer.Current()
        faces.append(face)
        face_edge_explorer = TopExp_Explorer(face, TopAbs_EDGE)
        face_edges: list[Any] = []
        while face_edge_explorer.More():
            edge = face_edge_explorer.Current()
            if not any(edge.IsSame(candidate) for candidate in face_edges):
                face_edges.append(edge)
                index = next((
                    item
                    for item, candidate in enumerate(unique_edges)
                    if edge.IsSame(candidate)
                ), None)
                if index is None:
                    unique_edges.append(edge)
                    edge_faces.append([])
                    index = len(unique_edges) - 1
                edge_faces[index].append(face)
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

    edge_explorer = TopExp_Explorer(shape, TopAbs_EDGE)
    ordered_edges: list[Any] = []
    while edge_explorer.More():
        edge = edge_explorer.Current()
        if not any(edge.IsSame(candidate) for candidate in ordered_edges):
            ordered_edges.append(edge)
        edge_explorer.Next()

    for edge_index, edge in enumerate(ordered_edges, 1):
        adjacency_index = next((
            item
            for item, candidate in enumerate(unique_edges)
            if edge.IsSame(candidate)
        ), None)
        adjacent_faces = (
            edge_faces[adjacency_index]
            if adjacency_index is not None
            else []
        )
        points = (
            _triangulation_edge_points(edge, adjacent_faces)
            or _sample_edge(edge, edge_linear_deflection)
        )
        if len(points) >= 2:
            curve_kind, curve_origin, curve_direction, curve_radius = (
                _edge_curve_descriptor(edge)
            )
            edges.append(
                EdgePolyline(
                    edge_index=edge_index,
                    points=tuple(points),
                    owner_id=owner_id,
                    element_kind=edge_kind,
                    base_color=edge_color,
                    label=edge_label,
                    topology_role=topology_role(edge, adjacent_faces),
                    curve_kind=curve_kind,
                    curve_origin=curve_origin,
                    curve_direction=curve_direction,
                    curve_radius=curve_radius,
                )
            )
            all_points.extend(points)

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


def combine_viewer_meshes(meshes: tuple[ViewerMesh, ...]) -> ViewerMesh:
    """Combine independently owned shapes into one GPU-ready scene mesh."""
    visible_meshes = tuple(mesh for mesh in meshes if not mesh.is_empty)
    if not visible_meshes:
        return _empty_mesh()
    if len(visible_meshes) == 1:
        # Avoid duplicating every Python float/tuple of a large imported mesh
        # merely to wrap a one-layer scene.
        return visible_meshes[0]
    positions: list[float] = []
    normals: list[float] = []
    face_indices: list[int] = []
    owner_ids: list[str] = []
    edges: list[EdgePolyline] = []
    points: list[PointMarker] = []
    planes: list[PlanePatch] = []
    triangle_meshes = tuple(
        mesh for mesh in visible_meshes if mesh.triangle_positions
    )
    all_bounds_min = [mesh.bounds_min for mesh in visible_meshes]
    all_bounds_max = [mesh.bounds_max for mesh in visible_meshes]
    for mesh in visible_meshes:
        if len(triangle_meshes) != 1:
            positions.extend(mesh.triangle_positions)
            normals.extend(mesh.triangle_normals)
            face_indices.extend(mesh.triangle_face_indices)
            owner_ids.extend(mesh.triangle_owner_ids)
        edges.extend(mesh.edges)
        points.extend(mesh.points)
        planes.extend(mesh.planes)
    # Datum/sketch overlays contain no triangles. Preserve the large body's
    # immutable tuples by identity so changing a tiny overlay neither copies
    # hundreds of thousands of vertices nor invalidates the GPU surface VBO.
    triangle_source = triangle_meshes[0] if len(triangle_meshes) == 1 else None
    return ViewerMesh(
        triangle_positions=(
            triangle_source.triangle_positions
            if triangle_source is not None else tuple(positions)
        ),
        triangle_normals=(
            triangle_source.triangle_normals
            if triangle_source is not None else tuple(normals)
        ),
        triangle_face_indices=(
            triangle_source.triangle_face_indices
            if triangle_source is not None else tuple(face_indices)
        ),
        triangle_owner_ids=(
            triangle_source.triangle_owner_ids
            if triangle_source is not None else tuple(owner_ids)
        ),
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

    triangle_points = tuple(
        transformed(tuple(mesh.triangle_positions[offset:offset + 3]))
        for offset in range(0, len(mesh.triangle_positions), 3)
    )
    triangle_positions = tuple(
        coordinate
        for point in triangle_points
        for coordinate in point
    )
    triangle_normals = tuple(
        coordinate
        for offset in range(0, len(mesh.triangle_normals), 3)
        for coordinate in _transformed_direction(
            transform,
            tuple(mesh.triangle_normals[offset:offset + 3]),
        )
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
            curve_kind=edge.curve_kind,
            curve_origin=(
                transformed(edge.curve_origin)
                if edge.curve_origin is not None
                else None
            ),
            curve_direction=(
                _transformed_direction(transform, edge.curve_direction)
                if edge.curve_direction is not None
                else None
            ),
            curve_radius=edge.curve_radius,
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
    all_points = list(triangle_points) + [
        point for edge in edges for point in edge.points
    ] + [point.position for point in points] + [
        point for plane in planes for point in plane.corners
    ]
    return ViewerMesh(
        triangle_positions=triangle_positions,
        triangle_normals=triangle_normals,
        triangle_face_indices=mesh.triangle_face_indices,
        triangle_owner_ids=mesh.triangle_owner_ids,
        edges=edges,
        points=points,
        planes=planes,
        bounds_min=tuple(
            min((point[axis] for point in all_points), default=0.0)
            for axis in range(3)
        ),
        bounds_max=tuple(
            max((point[axis] for point in all_points), default=0.0)
            for axis in range(3)
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


def _edge_curve_descriptor(
    edge: Any,
) -> tuple[str, Point3 | None, Point3 | None, float | None]:
    """Extract analytic data once, inside the OCCT calculation adapter."""
    try:
        curve = BRepAdaptor_Curve(edge)
        if curve.GetType() != GeomAbs_Circle:
            return "other", None, None, None
        circle = curve.Circle()
        center = circle.Location()
        direction = circle.Axis().Direction()
        return (
            "circle",
            (center.X(), center.Y(), center.Z()),
            (direction.X(), direction.Y(), direction.Z()),
            float(circle.Radius()),
        )
    except (RuntimeError, TypeError, ValueError):
        return "other", None, None, None


def _transformed_direction(transform, direction: Point3) -> Point3:
    vector = tuple(
        sum(transform[row][column] * direction[column] for column in range(3))
        for row in range(3)
    )
    length = sqrt(sum(value * value for value in vector))
    return (
        tuple(value / length for value in vector)
        if length > 1.0e-12
        else direction
    )


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
