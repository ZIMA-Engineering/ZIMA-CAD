from __future__ import annotations

from dataclasses import dataclass, field, replace
from math import sqrt
from typing import Literal, Mapping

from zima_cad.viewer_data import Point3, ViewerMesh


CurveKind = Literal["line", "circle", "ellipse", "spline", "other"]
SurfaceKind = Literal["plane", "cylinder", "cone", "sphere", "other"]


@dataclass(frozen=True)
class CurveDescriptor:
    """Persistable curve data consumed by picking and Sketcher references."""

    reference_id: str
    kind: CurveKind
    points: tuple[Point3, ...]
    origin: Point3 | None = None
    direction: Point3 | None = None
    radius: float | None = None
    bounded: bool = True


@dataclass(frozen=True)
class SurfaceDescriptor:
    """Persistable surface data produced together with a calculated body."""

    reference_id: str
    kind: SurfaceKind
    origin: Point3 | None = None
    normal: Point3 | None = None
    axis: Point3 | None = None
    radius: float | None = None
    boundary_edge_ids: tuple[str, ...] = ()


@dataclass(frozen=True)
class PhysicalProperties:
    volume: float = 0.0
    surface_area: float = 0.0
    center_of_mass: Point3 = (0.0, 0.0, 0.0)


@dataclass(frozen=True)
class VertexDescriptor:
    """Persistable point data exposed without a TopoDS_Vertex."""

    reference_id: str
    position: Point3


@dataclass(frozen=True)
class BodyResult:
    """OCCT-independent result exposed by the body-calculation boundary."""

    mesh: ViewerMesh
    faces: dict[str, SurfaceDescriptor] = field(default_factory=dict)
    edges: dict[str, CurveDescriptor] = field(default_factory=dict)
    vertices: dict[str, VertexDescriptor] = field(default_factory=dict)
    physical: PhysicalProperties = field(default_factory=PhysicalProperties)
    source_bodies: dict[str, "BodyResult"] = field(default_factory=dict)

    def with_inferred_face_boundaries(
        self,
        face_keys: set[str] | None = None,
    ) -> "BodyResult":
        """Fill missing face/edge incidence from persisted viewer geometry."""
        missing = {
            key for key, descriptor in self.faces.items()
            if not descriptor.boundary_edge_ids
            and (face_keys is None or key in face_keys)
        }
        if not missing or not self.mesh.edges:
            return self
        vertices_by_face: dict[str, set[tuple[float, float, float]]] = {}
        for triangle_index, (owner_id, face_index) in enumerate(zip(
            self.mesh.triangle_owner_ids,
            self.mesh.triangle_face_indices,
        )):
            key = _reference_id(owner_id, "face", face_index)
            if key not in missing:
                continue
            offset = triangle_index * 9
            vertices_by_face.setdefault(key, set()).update(
                tuple(
                    round(float(self.mesh.triangle_positions[
                        offset + vertex * 3 + axis
                    ]), 7)
                    for axis in range(3)
                )
                for vertex in range(3)
            )
        boundaries: dict[str, list[str]] = {
            key: [] for key in vertices_by_face
        }
        for edge in self.mesh.edges:
            descriptor = self.curve(edge.owner_id, edge.edge_index)
            if descriptor is None or not edge.points:
                continue
            edge_points = {
                tuple(round(float(value), 7) for value in point)
                for point in edge.points
            }
            for face_key, face_vertices in vertices_by_face.items():
                if edge_points.issubset(face_vertices):
                    boundaries[face_key].append(descriptor.reference_id)
        faces = dict(self.faces)
        changed = False
        for key, reference_ids in boundaries.items():
            if not reference_ids:
                continue
            faces[key] = replace(
                faces[key],
                boundary_edge_ids=tuple(sorted(set(reference_ids))),
            )
            changed = True
        return replace(self, faces=faces) if changed else self

    def to_dict(self) -> dict:
        return {
            "mesh": self.mesh.to_dict(),
            "faces": {
                key: descriptor.__dict__
                for key, descriptor in self.faces.items()
            },
            "edges": {
                key: descriptor.__dict__
                for key, descriptor in self.edges.items()
            },
            "vertices": {
                key: descriptor.__dict__
                for key, descriptor in self.vertices.items()
            },
            "physical": self.physical.__dict__,
            "source_bodies": {
                key: result.to_dict()
                for key, result in self.source_bodies.items()
            },
        }

    @classmethod
    def from_dict(cls, value: dict) -> "BodyResult":
        def point3(point):
            return (
                tuple(float(item) for item in point)
                if point is not None
                else None
            )

        faces = {
            str(key): SurfaceDescriptor(
                reference_id=str(item["reference_id"]),
                kind=str(item.get("kind", "other")),
                origin=point3(item.get("origin")),
                normal=point3(item.get("normal")),
                axis=point3(item.get("axis")),
                radius=(float(item["radius"]) if item.get("radius") is not None else None),
                boundary_edge_ids=tuple(str(edge) for edge in item.get("boundary_edge_ids", ())),
            )
            for key, item in value.get("faces", {}).items()
        }
        edges = {
            str(key): CurveDescriptor(
                reference_id=str(item["reference_id"]),
                kind=str(item.get("kind", "other")),
                points=tuple(point3(point) for point in item.get("points", ())),
                origin=point3(item.get("origin")),
                direction=point3(item.get("direction")),
                radius=(float(item["radius"]) if item.get("radius") is not None else None),
                bounded=bool(item.get("bounded", True)),
            )
            for key, item in value.get("edges", {}).items()
        }
        vertices = {
            str(key): VertexDescriptor(
                reference_id=str(item["reference_id"]),
                position=point3(item["position"]),
            )
            for key, item in value.get("vertices", {}).items()
        }
        physical = value.get("physical", {})
        return cls(
            mesh=ViewerMesh.from_dict(value["mesh"]),
            faces=faces,
            edges=edges,
            vertices=vertices,
            physical=PhysicalProperties(
                volume=float(physical.get("volume", 0.0)),
                surface_area=float(physical.get("surface_area", 0.0)),
                center_of_mass=point3(
                    physical.get("center_of_mass", (0.0, 0.0, 0.0))
                ),
            ),
            source_bodies={
                str(key): cls.from_dict(item)
                for key, item in value.get("source_bodies", {}).items()
            },
        )

    @classmethod
    def from_mesh(
        cls,
        mesh: ViewerMesh,
        *,
        face_reference_ids: Mapping[tuple[str, int], str] | None = None,
        face_boundary_edge_ids: Mapping[
            tuple[str, int], tuple[str, ...]
        ] | None = None,
        edge_reference_ids: Mapping[tuple[str, int], str] | None = None,
        vertex_reference_ids: Mapping[tuple[str, int], str] | None = None,
        inherited: "BodyResult | None" = None,
        skip_triangle_count: int = 0,
    ) -> "BodyResult":
        """Create a scene-local ZIMA topology packet without traversing OCCT.

        ``inherited`` and ``skip_triangle_count`` retain descriptors for a
        cached body at the start of a rebuilt display mesh.  Opening Sketcher
        can therefore append its lightweight overlays without rescanning the
        body's complete triangulation.
        """
        face_triangles: dict[tuple[str, int], list[tuple[Point3, Point3, Point3]]] = {}
        triangle_data = zip(
            mesh.triangle_owner_ids[skip_triangle_count:],
            mesh.triangle_face_indices[skip_triangle_count:],
        )
        for triangle_index, (owner_id, face_index) in enumerate(
            triangle_data,
            start=skip_triangle_count,
        ):
            offset = triangle_index * 9
            triangle = tuple(
                tuple(mesh.triangle_positions[offset + vertex * 3 + axis] for axis in range(3))
                for vertex in range(3)
            )
            face_triangles.setdefault((owner_id, face_index), []).append(triangle)
        faces: dict[str, SurfaceDescriptor] = dict(
            inherited.faces if inherited is not None else {}
        )
        for (owner_id, face_index), triangles in face_triangles.items():
            lookup_id = _reference_id(owner_id, "face", face_index)
            reference_id = (face_reference_ids or {}).get(
                (owner_id, face_index), lookup_id
            )
            plane = _plane_from_triangles(triangles)
            faces[lookup_id] = SurfaceDescriptor(
                reference_id=reference_id,
                kind="plane" if plane is not None else "other",
                origin=plane[0] if plane is not None else None,
                normal=plane[1] if plane is not None else None,
                boundary_edge_ids=(face_boundary_edge_ids or {}).get(
                    (owner_id, face_index), ()
                ),
            )
        edges = dict(inherited.edges if inherited is not None else {})
        edges.update({
            (lookup_id := _reference_id(
                edge.owner_id, edge.element_kind, edge.edge_index
            )): CurveDescriptor(
                reference_id=(edge_reference_ids or {}).get(
                    (edge.owner_id, edge.edge_index), lookup_id
                ),
                kind=(
                    edge.curve_kind
                    if edge.curve_kind in ("line", "circle", "ellipse", "spline")
                    else "other"
                ),
                points=edge.points,
                origin=edge.curve_origin,
                direction=edge.curve_direction,
                radius=edge.curve_radius,
            )
            for edge in mesh.edges
            if edge.element_kind == "edge"
        })
        vertices = dict(inherited.vertices if inherited is not None else {})
        vertices.update({
            (lookup_id := _reference_id(
                point.owner_id, point.element_kind, point.point_index
            )): VertexDescriptor(
                (vertex_reference_ids or {}).get(
                    (point.owner_id, point.point_index), lookup_id
                ),
                point.position,
            )
            for point in mesh.points
            if point.element_kind in ("point", "vertex")
        })
        return cls(
            mesh=mesh,
            faces=faces,
            edges=edges,
            vertices=vertices,
            physical=(
                inherited.physical
                if inherited is not None
                else PhysicalProperties()
            ),
        )

    def surface(
        self, owner_id: str, face_index: int
    ) -> SurfaceDescriptor | None:
        return self.faces.get(_reference_id(owner_id, "face", face_index))

    def curve(
        self, owner_id: str, edge_index: int
    ) -> CurveDescriptor | None:
        return self.edges.get(_reference_id(owner_id, "edge", edge_index))

    def vertex(
        self, owner_id: str, point_index: int
    ) -> VertexDescriptor | None:
        return self.vertices.get(_reference_id(owner_id, "point", point_index))

    def with_owner(self, owner_id: str) -> "BodyResult":
        """Bind a persisted Part result to its newly created runtime root."""
        def rekey(values: dict[str, object], kind: str) -> dict[str, object]:
            result = {}
            marker = f":{kind}:"
            for key, descriptor in values.items():
                _prefix, separator, index = key.rpartition(marker)
                result[
                    f"{owner_id}{marker}{index}" if separator else key
                ] = descriptor
            return result

        return BodyResult(
            mesh=self.mesh.with_owner(owner_id),
            faces=rekey(self.faces, "face"),
            edges=rekey(self.edges, "edge"),
            vertices=rekey(self.vertices, "point"),
            physical=self.physical,
            # Historical source bodies keep their persisted container IDs.
            # Rebinding the calculated Part root must not discard or rename
            # them: viewport container/reference picking resolves directly
            # against those IDs after a saved document is reopened.
            source_bodies=self.source_bodies,
        )


def _reference_id(owner_id: str, kind: str, index: int) -> str:
    return f"{owner_id}:{kind}:{index}"


def _plane_from_triangles(
    triangles: list[tuple[Point3, Point3, Point3]],
) -> tuple[Point3, Point3] | None:
    origin: Point3 | None = None
    normal: Point3 | None = None
    for first, second, third in triangles:
        ab = tuple(second[index] - first[index] for index in range(3))
        ac = tuple(third[index] - first[index] for index in range(3))
        cross = (
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0],
        )
        length = sqrt(sum(value * value for value in cross))
        if length > 1.0e-12:
            origin = first
            normal = tuple(value / length for value in cross)
            break
    if origin is None or normal is None:
        return None
    scale = max(
        sqrt(sum(value * value for value in point))
        for triangle in triangles
        for point in triangle
    )
    tolerance = max(scale * 1.0e-8, 1.0e-7)
    for triangle in triangles:
        for point in triangle:
            distance = sum(
                (point[index] - origin[index]) * normal[index]
                for index in range(3)
            )
            if abs(distance) > tolerance:
                return None
    return origin, normal
