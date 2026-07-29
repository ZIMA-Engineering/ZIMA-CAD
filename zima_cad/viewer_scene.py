from __future__ import annotations

from dataclasses import dataclass
from math import sqrt
from typing import Any

from zima_cad.model import (
    ContainerType,
    EntityKind,
    PartDocument,
    ZimaEntity,
    coordinate_system_transform,
    identity_transform,
    make_sketch_shape,
    make_datum_axis_shape,
    multiply_transforms,
)
from zima_cad.viewer_mesh import (
    BROWN,
    ViewerMesh,
    combine_viewer_meshes,
    datum_plane_mesh,
    origin_axes_mesh,
    point_marker_mesh,
    transform_viewer_mesh,
    topology_subshape,
    triangulate_shape,
)


SKETCH_COLOR = (0.231, 0.510, 0.965)


@dataclass(frozen=True)
class DocumentViewerScene:
    mesh: ViewerMesh
    shapes_by_owner_id: dict[str, Any]

    def resolve_topology(
        self,
        owner_id: str,
        element_kind: str,
        element_index: int,
    ) -> Any | None:
        return topology_subshape(
            self.shapes_by_owner_id.get(owner_id),
            element_kind=element_kind,
            element_index=element_index,
        )


def build_document_viewer_scene(
    document: PartDocument,
    *,
    history_boundary: int | None = None,
    show_document_origin: bool = False,
    show_document_planes: bool = False,
) -> ViewerMesh:
    """Build renderer-owned scene data from a real ZIMA-CAD document."""
    return build_document_viewer_scene_data(
        document,
        history_boundary=history_boundary,
        show_document_origin=show_document_origin,
        show_document_planes=show_document_planes,
    ).mesh


def build_document_viewer_scene_data(
    document: PartDocument,
    *,
    history_boundary: int | None = None,
    show_document_origin: bool = False,
    show_document_planes: bool = False,
    show_object_planes: bool = False,
    show_object_origins: bool = False,
    show_user_points: bool = False,
    show_user_axes: bool = False,
    show_user_planes: bool = False,
    editing_object_id: str | None = None,
) -> DocumentViewerScene:
    """Build a mesh plus the owner map used to resolve picked topology."""
    boundary = (
        document.history_cursor()
        if history_boundary is None
        else history_boundary
    )
    document.resolve_attachments()
    layers: list[ViewerMesh] = []
    shapes_by_owner_id: dict[str, Any] = {}

    if document.body_is_suppressed():
        for obj in document.history_objects_at(boundary):
            if not document.is_effectively_visible(obj.entity_id):
                continue
            shape = document.build_standalone_shape(obj)
            if shape is not None:
                shapes_by_owner_id[obj.entity_id] = shape
                layers.append(
                    triangulate_shape(shape, owner_id=obj.entity_id)
                )
    else:
        shape = document.build_shape_at(boundary)
        if shape is not None and document.body_is_visible():
            shapes_by_owner_id[document.root.entity_id] = shape
            layers.append(
                triangulate_shape(
                    shape,
                    owner_id=document.root.entity_id,
                )
            )

    for obj in document.visible_objects():
        _append_object_sketches(
            document,
            obj,
            identity_transform(),
            layers,
            shapes_by_owner_id,
            show_user_points,
            show_user_axes,
            show_user_planes,
        )
        _append_object_origins(
            document,
            obj,
            identity_transform(),
            layers,
            show_object_planes,
            show_object_origins,
            show_user_points,
            editing_object_id,
        )

    if show_document_origin:
        scene_size = _scene_diagonal(layers)
        origin_id = _document_origin_id(document)
        layers.append(
            origin_axes_mesh(
                owner_id=origin_id,
                length=max(scene_size * 0.12, 4.0),
                point_label=f"{document.root.name} · Origin",
            )
        )
    if show_document_planes:
        scene_size = _scene_diagonal(layers)
        origin_id = _document_origin_id(document)
        for plane_index, plane_name in enumerate(
            ("xy", "yz", "xz"),
            start=1,
        ):
            layers.append(
                datum_plane_mesh(
                    owner_id=origin_id,
                    plane_index=plane_index,
                    size=max(scene_size * 0.24, 8.0),
                    plane=plane_name,
                    label=plane_name.upper(),
                )
            )
    return DocumentViewerScene(
        mesh=combine_viewer_meshes(tuple(layers)),
        shapes_by_owner_id=shapes_by_owner_id,
    )


def _append_object_sketches(
    document: PartDocument,
    obj: ZimaEntity,
    parent_transform,
    layers: list[ViewerMesh],
    shapes_by_owner_id: dict[str, Any],
    show_user_points: bool,
    show_user_axes: bool,
    show_user_planes: bool,
) -> None:
    if not document.is_effectively_visible(obj.entity_id):
        return
    world_transform = multiply_transforms(
        parent_transform,
        coordinate_system_transform(obj.coordinate_system),
    )
    owner = document.find_owning_object(obj.entity_id)
    display_name = (
        owner.name
        if owner is not None and owner.kind == EntityKind.CONTAINER
        else obj.name
    )
    if (
        obj.kind == EntityKind.POINT
        and not obj.locked
        and show_user_points
    ):
        layers.append(
            transform_viewer_mesh(
                point_marker_mesh(
                    owner_id=obj.entity_id,
                    label=display_name,
                ),
                world_transform,
            )
        )
    if (
        obj.kind == EntityKind.AXIS
        and not obj.locked
        and show_user_axes
    ):
        shape = make_datum_axis_shape(obj, world_transform)
        if shape is not None:
            shapes_by_owner_id[obj.entity_id] = shape
            layers.append(
                triangulate_shape(
                    shape,
                    owner_id=obj.entity_id,
                    edge_kind="centerline",
                    edge_color=BROWN,
                    edge_label=display_name,
                )
            )
    if (
        obj.kind == EntityKind.PLANE
        and not obj.locked
        and show_user_planes
    ):
        layers.append(
            transform_viewer_mesh(
                datum_plane_mesh(
                    owner_id=obj.entity_id,
                    size=float(obj.parameters.get("size", 50.0)),
                    plane=str(obj.parameters.get("plane", "xy")),
                    label=display_name,
                ),
                world_transform,
            )
        )
    for child in obj.children:
        if child.kind == EntityKind.SKETCH:
            if not document.is_effectively_visible(child.entity_id):
                continue
            shape = make_sketch_shape(obj, child, world_transform)
            if shape is not None:
                shapes_by_owner_id[child.entity_id] = shape
                layers.append(
                    triangulate_shape(
                        shape,
                        owner_id=child.entity_id,
                        edge_kind="sketch",
                        edge_color=SKETCH_COLOR,
                    )
                )
        elif not child.locked:
            _append_object_sketches(
                document,
                child,
                world_transform,
                layers,
                shapes_by_owner_id,
                show_user_points,
                show_user_axes,
                show_user_planes,
            )


def _append_object_origins(
    document: PartDocument,
    obj: ZimaEntity,
    parent_transform,
    layers: list[ViewerMesh],
    show_object_planes: bool,
    show_object_origins: bool,
    show_user_points: bool,
    editing_object_id: str | None,
) -> None:
    if not document.is_effectively_visible(obj.entity_id):
        return
    world_transform = multiply_transforms(
        parent_transform,
        coordinate_system_transform(obj.coordinate_system),
    )
    origin = next(
        (child for child in obj.children if child.kind == EntityKind.ORIGIN),
        None,
    )
    if (
        origin is not None
        and obj.container_type == ContainerType.POINT
        and show_user_points
    ):
        layers.append(
            transform_viewer_mesh(
                point_marker_mesh(
                    owner_id=origin.entity_id,
                    label=obj.name,
                ),
                world_transform,
            )
        )
    if (
        origin is not None
        and obj.show_auxiliary_geometry
        and show_object_origins
    ):
        scene_size = _scene_diagonal(layers)
        reference_size = max(scene_size * 0.075, 2.5)
        layers.append(
            transform_viewer_mesh(
                origin_axes_mesh(
                    owner_id=origin.entity_id,
                    length=reference_size,
                    point_label=f"{obj.name} · Origin",
                ),
                world_transform,
            )
        )
        if show_object_planes:
            for plane_index, plane_name in enumerate(
                ("xy", "yz", "xz"),
                start=1,
            ):
                layers.append(
                    transform_viewer_mesh(
                        datum_plane_mesh(
                            owner_id=origin.entity_id,
                            plane_index=plane_index,
                            size=reference_size * 2.0,
                            plane=plane_name,
                            label=plane_name.upper(),
                        ),
                        world_transform,
                    )
                )
    elif (
        origin is not None
        and obj.entity_id == editing_object_id
        and obj.container_type != ContainerType.POINT
    ):
        layers.append(
            transform_viewer_mesh(
                point_marker_mesh(
                    owner_id=origin.entity_id,
                    label=f"{obj.name} · Origin",
                ),
                world_transform,
            )
        )
    for child in obj.children:
        if not child.locked:
            _append_object_origins(
                document,
                child,
                world_transform,
                layers,
                show_object_planes,
                show_object_origins,
                show_user_points,
                editing_object_id,
            )


def _scene_diagonal(layers: list[ViewerMesh]) -> float:
    visible = [layer for layer in layers if not layer.is_empty]
    if not visible:
        return 1.0
    bounds_min = tuple(
        min(layer.bounds_min[axis] for layer in visible)
        for axis in range(3)
    )
    bounds_max = tuple(
        max(layer.bounds_max[axis] for layer in visible)
        for axis in range(3)
    )
    return sqrt(
        sum(
            (bounds_max[axis] - bounds_min[axis]) ** 2
            for axis in range(3)
        )
    )


def _document_origin_id(document: PartDocument) -> str:
    origin = next(
        (
            child
            for child in document.root.children
            if child.kind == EntityKind.ORIGIN
        ),
        None,
    )
    return origin.entity_id if origin is not None else "document-origin"
