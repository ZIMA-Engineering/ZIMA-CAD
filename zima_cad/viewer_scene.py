from __future__ import annotations

from dataclasses import dataclass
from math import sqrt
from typing import Any

from zima_cad.model import (
    CoordinateSystem,
    ContainerType,
    EntityKind,
    PartDocument,
    ZimaEntity,
    coordinate_system_transform,
    identity_transform,
    make_sketch_shape,
    make_datum_axis_shape,
    multiply_transforms,
    transform_shape,
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
from zima_cad.step_import import INTERACTIVE_TOPOLOGY_FACE_LIMIT


SKETCH_COLOR = (1.0, 0.843, 0.251)
CONTAINER_PREVIEW_ORIGIN_ID = "__container_preview_origin__"


@dataclass(frozen=True)
class DocumentViewerScene:
    mesh: ViewerMesh
    shapes_by_owner_id: dict[str, Any]
    surface_colors_by_owner_id: dict[str, str]
    body_mesh: ViewerMesh | None = None

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
    show_component_origins: bool = False,
    show_sketches: bool = True,
    show_user_points: bool = False,
    show_user_axes: bool = False,
    show_user_planes: bool = False,
    editing_object_id: str | None = None,
    preview_coordinate_system: CoordinateSystem | None = None,
    preview_origin_label: str = "Preview",
    preview_plane: str | None = None,
    preview_plane_size: float | None = None,
    preview_plane_offset: float = 0.0,
    uncut_component_id: str | None = None,
    uncut_component_shape: Any | None = None,
    component_documents: dict[str, PartDocument] | None = None,
    cached_body_shape: Any | None = None,
    cached_body_mesh: ViewerMesh | None = None,
) -> DocumentViewerScene:
    """Build a mesh plus the owner map used to resolve picked topology."""
    boundary = (
        document.history_cursor()
        if history_boundary is None
        else history_boundary
    )
    document.sync_generated_axes()
    document.resolve_attachments()
    layers: list[ViewerMesh] = []
    shapes_by_owner_id: dict[str, Any] = {}
    surface_colors_by_owner_id: dict[str, str] = {}
    body_mesh: ViewerMesh | None = None

    is_assembly = document.document_settings.get("type") == "assembly"
    if is_assembly:
        assembly_objects = document.history_objects_at(boundary)
        for obj in assembly_objects:
            if not document.is_effectively_visible(obj.entity_id):
                continue
            if obj.container_type != ContainerType.COMPONENT:
                continue
            if (
                obj.entity_id == uncut_component_id
                and uncut_component_shape is not None
            ):
                shape = transform_shape(
                    uncut_component_shape,
                    coordinate_system_transform(obj.coordinate_system),
                )
            else:
                source_document = (component_documents or {}).get(
                    obj.entity_id
                )
                shape = document.build_assembly_component_shape(
                    obj,
                    assembly_objects,
                    source_document=source_document,
                )
            if shape is not None:
                shapes_by_owner_id[obj.entity_id] = shape
                source_document = (component_documents or {}).get(
                    obj.entity_id
                )
                inherited_color = (
                    source_document.document_settings.get(
                        "body_color", "#B9C2CC"
                    )
                    if source_document is not None
                    else obj.parameters.get("body_color", "#B9C2CC")
                )
                surface_colors_by_owner_id[obj.entity_id] = str(
                    obj.parameters.get("body_color", inherited_color)
                    if str(
                        obj.parameters.get(
                            "body_color_override", "false"
                        )
                    ).lower() == "true"
                    else inherited_color
                )
                layers.append(triangulate_shape(shape, owner_id=obj.entity_id))
            source_document = (component_documents or {}).get(obj.entity_id)
            if show_user_axes and source_document is not None:
                source_document.sync_generated_axes()
                component_transform = coordinate_system_transform(
                    obj.coordinate_system
                )
                for source_object in source_document.visible_objects():
                    _append_component_axes(
                        source_document,
                        source_object,
                        component_transform,
                        obj.entity_id,
                        layers,
                        shapes_by_owner_id,
                    )
    elif document.body_is_suppressed():
        for obj in document.history_objects_at(boundary):
            if not document.is_effectively_visible(obj.entity_id):
                continue
            shape = document.build_standalone_shape(obj)
            if shape is not None:
                shapes_by_owner_id[obj.entity_id] = shape
                surface_colors_by_owner_id[obj.entity_id] = str(
                    document.document_settings.get(
                        "body_color", "#B9C2CC"
                    )
                )
                layers.append(
                    triangulate_shape(shape, owner_id=obj.entity_id)
                )
    else:
        history_objects = document.history_objects_at(boundary)
        shape = (
            cached_body_shape
            if cached_body_shape is not None
            else document.build_shape_at(boundary)
        )
        if shape is not None and document.body_is_visible():
            shapes_by_owner_id[document.root.entity_id] = shape
            surface_colors_by_owner_id[document.root.entity_id] = str(
                document.document_settings.get("body_color", "#B9C2CC")
            )
            if cached_body_mesh is not None:
                body_mesh = cached_body_mesh
            else:
                imported_face_count = max(
                    (
                        int(child.parameters.get("face_count", 0) or 0)
                        for obj in history_objects
                        for child in obj.children
                        if child.kind == EntityKind.IMPORTED_STEP
                    ),
                    default=0,
                )
                imported_index = next(
                    (
                        index
                        for index, obj in enumerate(history_objects)
                        if obj.container_type == ContainerType.IMPORTED_STEP
                        and any(
                            child.kind == EntityKind.IMPORTED_STEP
                            and int(child.parameters.get("face_count", 0) or 0)
                            > INTERACTIVE_TOPOLOGY_FACE_LIMIT
                            for child in obj.children
                        )
                    ),
                    None,
                )

                def is_layerable_addition(obj: ZimaEntity) -> bool:
                    if obj.container_type in (
                        ContainerType.POINT,
                        ContainerType.AXIS,
                        ContainerType.PLANE,
                        ContainerType.SKETCH,
                    ):
                        return True
                    if obj.container_type in (
                        ContainerType.PROTRUSION,
                        ContainerType.REVOLVE,
                    ):
                        feature = next(
                            (
                                child for child in obj.children
                                if child.kind in (
                                    EntityKind.PROTRUSION,
                                    EntityKind.REVOLVE,
                                )
                                and not child.locked
                            ),
                            None,
                        )
                        return (
                            feature is not None
                            and str(feature.parameters.get("operation", "+"))
                            == "+"
                        )
                    return (
                        obj.container_type != ContainerType.FILLET
                        and str(obj.combine_mode.value) == "+"
                    )

                layered_large_import = (
                    imported_index is not None
                    and all(
                        is_layerable_addition(obj)
                        for obj in history_objects[imported_index + 1:]
                    )
                )
                if layered_large_import:
                    imported_container = history_objects[imported_index]
                    imported_entity = next(
                        child for child in imported_container.children
                        if child.kind == EntityKind.IMPORTED_STEP
                    )
                    imported_mesh = getattr(
                        imported_entity,
                        "_imported_viewer_mesh_cache",
                        None,
                    )
                    if imported_mesh is None:
                        imported_shape = document.build_standalone_shape(
                            imported_container
                        )
                        imported_mesh = triangulate_shape(
                            imported_shape,
                            owner_id=document.root.entity_id,
                            linear_deflection=5.0,
                            angular_deflection=1.2,
                            include_topology=False,
                        )
                        imported_entity._imported_viewer_mesh_cache = (
                            imported_mesh
                        )
                    body_layers = [imported_mesh]
                    for addition in history_objects[imported_index + 1:]:
                        addition_shape = document.build_standalone_shape(
                            addition
                        )
                        if addition_shape is not None:
                            body_layers.append(
                                triangulate_shape(
                                    addition_shape,
                                    owner_id=document.root.entity_id,
                                )
                            )
                    body_mesh = combine_viewer_meshes(tuple(body_layers))
                else:
                    body_mesh = triangulate_shape(
                        shape,
                        owner_id=document.root.entity_id,
                        linear_deflection=(
                            5.0
                            if imported_face_count > INTERACTIVE_TOPOLOGY_FACE_LIMIT
                            else 0.2
                        ),
                        angular_deflection=(
                            1.2
                            if imported_face_count > INTERACTIVE_TOPOLOGY_FACE_LIMIT
                            else 0.35
                        ),
                        include_topology=(
                            imported_face_count
                            <= INTERACTIVE_TOPOLOGY_FACE_LIMIT
                        ),
                    )
            layers.append(body_mesh)

    reference_scene_size = _scene_diagonal(layers)
    for obj in document.visible_objects():
        _append_object_sketches(
            document,
            obj,
            identity_transform(),
            layers,
            shapes_by_owner_id,
            show_sketches,
            show_user_points,
            show_user_axes,
            show_user_planes,
            editing_object_id,
        )
        _append_object_origins(
            document,
            obj,
            identity_transform(),
            layers,
            show_object_planes,
            show_object_origins,
            show_component_origins,
            show_user_points,
            editing_object_id,
            reference_scene_size,
        )

    if preview_coordinate_system is not None:
        preview_owner_id = CONTAINER_PREVIEW_ORIGIN_ID
        preview_transform = coordinate_system_transform(
            preview_coordinate_system
        )
        layers.append(
            transform_viewer_mesh(
                origin_axes_mesh(
                    owner_id=preview_owner_id,
                    length=max(reference_scene_size * 0.075, 2.5),
                    point_label=f"{preview_origin_label} · Origin",
                ),
                preview_transform,
            )
        )
        preview_planes = (
            ()
            if preview_plane == ""
            else (preview_plane,)
            if preview_plane in ("xy", "yz", "xz")
            else ("xy", "yz", "xz")
        )
        plane_indices = {"xy": 1, "yz": 2, "xz": 3}
        for plane_name in preview_planes:
            local_plane_offset = {
                "xy": (0.0, 0.0, preview_plane_offset),
                "yz": (preview_plane_offset, 0.0, 0.0),
                "xz": (0.0, preview_plane_offset, 0.0),
            }[plane_name]
            layers.append(
                transform_viewer_mesh(
                    transform_viewer_mesh(
                        datum_plane_mesh(
                            owner_id=preview_owner_id,
                            plane_index=plane_indices[plane_name],
                            size=(
                                max(float(preview_plane_size), 0.001)
                                if preview_plane_size is not None
                                and len(preview_planes) == 1
                                else max(reference_scene_size * 0.12, 4.0)
                            ),
                            plane=plane_name,
                            label=plane_name.upper(),
                            screen_constant=preview_plane_size is None,
                        ),
                        coordinate_system_transform(
                            CoordinateSystem(origin=local_plane_offset)
                        ),
                    ),
                    preview_transform,
                )
            )

    if show_document_origin:
        origin_id = _document_origin_id(document)
        layers.append(
            origin_axes_mesh(
                owner_id=origin_id,
                length=max(reference_scene_size * 0.12, 4.0),
                point_label=f"{document.root.name} · Origin",
            )
        )
    if show_document_planes:
        origin_id = _document_origin_id(document)
        for plane_index, plane_name in enumerate(
            ("xy", "yz", "xz"),
            start=1,
        ):
            layers.append(
                datum_plane_mesh(
                    owner_id=origin_id,
                    plane_index=plane_index,
                    size=max(reference_scene_size * 0.24, 8.0),
                    plane=plane_name,
                    label=plane_name.upper(),
                    screen_constant=True,
                )
            )
    return DocumentViewerScene(
        mesh=combine_viewer_meshes(tuple(layers)),
        shapes_by_owner_id=shapes_by_owner_id,
        surface_colors_by_owner_id=surface_colors_by_owner_id,
        body_mesh=body_mesh,
    )


def _append_object_sketches(
    document: PartDocument,
    obj: ZimaEntity,
    parent_transform,
    layers: list[ViewerMesh],
    shapes_by_owner_id: dict[str, Any],
    show_sketches: bool,
    show_user_points: bool,
    show_user_axes: bool,
    show_user_planes: bool,
    editing_object_id: str | None,
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
        and (not obj.locked or obj.parameters.get("generated_axis") == "true")
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
            if not show_sketches and obj.entity_id != editing_object_id:
                continue
            if obj.container_type in (
                ContainerType.PROTRUSION,
                ContainerType.REVOLVE,
            ):
                continue
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
        elif not child.locked or child.parameters.get("generated_axis") == "true":
            _append_object_sketches(
                document,
                child,
                world_transform,
                layers,
                shapes_by_owner_id,
                show_sketches,
                show_user_points,
                show_user_axes,
                show_user_planes,
                editing_object_id,
            )


def _append_component_axes(
    source_document: PartDocument,
    obj: ZimaEntity,
    parent_transform,
    component_id: str,
    layers: list[ViewerMesh],
    shapes_by_owner_id: dict[str, Any],
) -> None:
    """Append part datum/generated axes in one assembly component frame."""
    if not source_document.is_effectively_visible(obj.entity_id):
        return
    world_transform = multiply_transforms(
        parent_transform,
        coordinate_system_transform(obj.coordinate_system),
    )
    if (
        obj.kind == EntityKind.AXIS
        and (
            not obj.locked
            or obj.parameters.get("generated_axis") == "true"
        )
    ):
        shape = make_datum_axis_shape(obj, world_transform)
        if shape is not None:
            owner_id = f"{component_id}:{obj.entity_id}"
            shapes_by_owner_id[owner_id] = shape
            layers.append(
                triangulate_shape(
                    shape,
                    owner_id=owner_id,
                    edge_kind="centerline",
                    edge_color=BROWN,
                    edge_label=obj.name,
                )
            )
    for child in obj.children:
        if not child.locked or child.parameters.get("generated_axis") == "true":
            _append_component_axes(
                source_document,
                child,
                world_transform,
                component_id,
                layers,
                shapes_by_owner_id,
            )


def _append_object_origins(
    document: PartDocument,
    obj: ZimaEntity,
    parent_transform,
    layers: list[ViewerMesh],
    show_object_planes: bool,
    show_object_origins: bool,
    show_component_origins: bool,
    show_user_points: bool,
    editing_object_id: str | None,
    reference_scene_size: float,
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
        and obj.entity_id != editing_object_id
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
    show_component_origin = (
        obj.container_type == ContainerType.COMPONENT
        and document.document_settings.get("type") == "assembly"
        and show_component_origins
    )
    if origin is not None and (
        show_component_origin
        or (
            obj.container_type != ContainerType.COMPONENT
            and obj.show_auxiliary_geometry
            and show_object_origins
        )
        or obj.entity_id == editing_object_id
    ):
        reference_size = max(reference_scene_size * 0.075, 2.5)
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
        if show_object_planes or obj.entity_id == editing_object_id:
            for plane_index, plane_name in enumerate(
                ("xy", "yz", "xz"),
                start=1,
            ):
                layers.append(
                    transform_viewer_mesh(
                        datum_plane_mesh(
                            owner_id=origin.entity_id,
                            plane_index=plane_index,
                            size=max(reference_scene_size * 0.12, 4.0),
                            plane=plane_name,
                            label=plane_name.upper(),
                            screen_constant=True,
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
                show_component_origins,
                show_user_points,
                editing_object_id,
                reference_scene_size,
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
