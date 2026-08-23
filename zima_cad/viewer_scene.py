from __future__ import annotations

from dataclasses import dataclass, field, replace
import json
from math import sqrt
from typing import Any

from OCC.Core.BRepAdaptor import BRepAdaptor_Surface
from OCC.Core.GeomAbs import GeomAbs_Cone, GeomAbs_Cylinder, GeomAbs_Sphere
from OCC.Core.TopAbs import TopAbs_EDGE
from OCC.Core.TopExp import TopExp_Explorer, topexp
from OCC.Core.TopTools import TopTools_IndexedMapOfShape

from zima_cad.body_result import BodyResult
from zima_cad.model import (
    CoordinateSystem,
    ContainerType,
    EntityKind,
    PartDocument,
    ZimaEntity,
    active_face_registry,
    coordinate_system_transform,
    geometric_edge_reference,
    identity_transform,
    make_sketch_shape,
    make_datum_axis_shape,
    multiply_transforms,
    transform_shape,
    face_registry_at,
)
from zima_cad.topology import (
    AssemblyEdgeRef,
    AssemblyFaceRef,
    EdgeRef,
    FaceRef,
    assembly_edge_descriptor,
    assembly_face_descriptor,
    parse_edge_reference,
    parse_face_reference,
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


def with_occt_analytic_surfaces(
    result: BodyResult,
    shape: Any,
    owner_id: str,
) -> BodyResult:
    """Persist curved analytics only at an explicit OCCT calculation boundary."""
    faces = dict(result.faces)
    changed = False
    for face_index in set(result.mesh.triangle_face_indices):
        face = topology_subshape(
            shape,
            element_kind="face",
            element_index=face_index,
        )
        descriptor = result.surface(owner_id, face_index)
        if face is None or descriptor is None:
            continue
        try:
            adaptor = BRepAdaptor_Surface(face)
            surface_type = adaptor.GetType()
            if surface_type == GeomAbs_Cylinder:
                cylinder = adaptor.Cylinder()
                location = cylinder.Axis().Location()
                direction = cylinder.Axis().Direction()
                updated = replace(
                    descriptor,
                    kind="cylinder",
                    origin=(location.X(), location.Y(), location.Z()),
                    axis=(direction.X(), direction.Y(), direction.Z()),
                    radius=float(cylinder.Radius()),
                )
            elif surface_type == GeomAbs_Sphere:
                sphere = adaptor.Sphere()
                center = sphere.Location()
                updated = replace(
                    descriptor,
                    kind="sphere",
                    origin=(center.X(), center.Y(), center.Z()),
                    radius=float(sphere.Radius()),
                )
            elif surface_type == GeomAbs_Cone:
                cone = adaptor.Cone()
                apex = cone.Apex()
                direction = cone.Axis().Direction()
                updated = replace(
                    descriptor,
                    kind="cone",
                    origin=(apex.X(), apex.Y(), apex.Z()),
                    axis=(direction.X(), direction.Y(), direction.Z()),
                    semi_angle=float(cone.SemiAngle()),
                )
            else:
                continue
        except (AttributeError, RuntimeError, TypeError, ValueError):
            continue
        faces[f"{owner_id}:face:{face_index}"] = updated
        changed = True
    return replace(result, faces=faces) if changed else result


@dataclass(frozen=True)
class DocumentViewerScene:
    mesh: ViewerMesh
    shapes_by_owner_id: dict[str, Any]
    surface_colors_by_owner_id: dict[str, str]
    body_result: BodyResult | None = None
    calculated_body_result: BodyResult | None = None
    calculated_body_mesh: ViewerMesh | None = field(
        default=None,
        compare=False,
        repr=False,
    )
    _resolved_topology: dict[tuple[str, str, int], Any] = field(
        default_factory=dict,
        compare=False,
        repr=False,
    )

    @property
    def body_mesh(self) -> ViewerMesh | None:
        """Compatibility view while callers migrate to ``body_result``."""
        return self.calculated_body_mesh

    def resolve_topology(
        self,
        owner_id: str,
        element_kind: str,
        element_index: int,
    ) -> Any | None:
        cache_key = (owner_id, element_kind, element_index)
        if cache_key in self._resolved_topology:
            return self._resolved_topology[cache_key]
        resolved = topology_subshape(
            self.shapes_by_owner_id.get(owner_id),
            element_kind=element_kind,
            element_index=element_index,
        )
        self._resolved_topology[cache_key] = resolved
        return resolved

    def surface_reference(self, owner_id: str, face_index: int):
        result = self._body_result_for_owner(owner_id)
        return result.surface(owner_id, face_index) if result is not None else None

    def curve_reference(self, owner_id: str, edge_index: int):
        result = self._body_result_for_owner(owner_id)
        return result.curve(owner_id, edge_index) if result is not None else None

    def vertex_reference(self, owner_id: str, point_index: int):
        result = self._body_result_for_owner(owner_id)
        return result.vertex(owner_id, point_index) if result is not None else None

    def _body_result_for_owner(self, owner_id: str) -> BodyResult | None:
        """Find the persisted packet that owns source topology data."""
        checked: set[int] = set()
        for result in (self.body_result, self.calculated_body_result):
            if result is None or id(result) in checked:
                continue
            checked.add(id(result))
            if any(
                key.startswith(f"{owner_id}:face:")
                for key in result.faces
            ) or any(
                key.startswith(f"{owner_id}:edge:")
                for key in result.edges
            ) or any(
                key.startswith(f"{owner_id}:point:")
                for key in result.vertices
            ):
                return result
            source = result.source_bodies.get(owner_id)
            if source is not None:
                return source
        return None

    def source_body_result(self, owner_id: str) -> BodyResult | None:
        """Expose the persisted source packet for reference projection."""
        for result in (self.body_result, self.calculated_body_result):
            if result is None:
                continue
            source = result.source_bodies.get(owner_id)
            if source is not None:
                return source
        return None


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
    uncut_component_mesh: ViewerMesh | None = None,
    component_mesh_overrides: dict[str, ViewerMesh] | None = None,
    component_documents: dict[str, PartDocument] | None = None,
    active_reference_document: PartDocument | None = None,
    active_reference_transform: Any | None = None,
    cached_body_shape: Any | None = None,
    cached_body_mesh: ViewerMesh | None = None,
    cached_body_result: BodyResult | None = None,
    reference_owner_ids: frozenset[str] = frozenset(),
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
    shape = None
    face_reference_ids: dict[tuple[str, int], str] = {}
    face_boundary_edge_ids: dict[tuple[str, int], tuple[str, ...]] = {}
    edge_reference_ids: dict[tuple[str, int], str] = {}
    vertex_reference_ids: dict[tuple[str, int], str] = {}
    imported_face_count = max(
        (
            int(child.parameters.get("face_count", 0) or 0)
            for obj in document.history_objects_at(boundary)
            for child in obj.children
            if child.kind == EntityKind.IMPORTED_STEP
        ),
        default=0,
    )

    is_assembly = document.document_settings.get("type") == "assembly"
    if is_assembly:
        assembly_objects = document.history_objects_at(boundary)
        if editing_object_id is not None:
            editing_entity = document.find_entity(editing_object_id)
            if (
                editing_entity is not None
                and editing_entity.container_type == ContainerType.COMPONENT
                and all(
                    obj.entity_id != editing_entity.entity_id
                    for obj in assembly_objects
                )
            ):
                # The rollback body stops before the edited component, but
                # the active component remains visible as its transient
                # Properties preview.
                assembly_objects = [*assembly_objects, editing_entity]
        assembly_keys = document._shape_history_cache_keys(assembly_objects)
        assembly_cached_result = (
            cached_body_result
            if cached_body_result is not None
            and uncut_component_shape is None
            and (
                uncut_component_mesh is None
                or uncut_component_mesh.is_empty
            )
            else document.cached_body_result_at(assembly_objects)
            if assembly_keys
            and uncut_component_shape is None
            and (
                uncut_component_mesh is None
                or uncut_component_mesh.is_empty
            )
            else None
        )
        # A cached parent packet may contain a single owner for an entire
        # nested Assembly. Rebuild its viewer layers from leaf packets so
        # their persisted colors remain addressable.
        if assembly_cached_result is not None and any(
            obj.container_type == ContainerType.COMPONENT
            and (component_documents or {}).get(obj.entity_id) is not None
            and (component_documents or {})[obj.entity_id].document_settings.get(
                "type"
            ) == "assembly"
            for obj in assembly_objects
        ):
            assembly_cached_result = None
        component_body_layers: list[ViewerMesh] = []
        dependency_suppressed_ids = (
            document.dependency_suppressed_component_ids()
        )
        dependency_graph = document.assembly_component_dependencies()
        component_ids = {
            obj.entity_id
            for obj in assembly_objects
            if obj.container_type == ContainerType.COMPONENT
        }
        dependency_error_ids = {
            obj.entity_id
            for obj in assembly_objects
            if obj.container_type == ContainerType.COMPONENT
            and (
                dependency_graph.get(obj.entity_id, set()) - component_ids
                or str(obj.parameters.get(
                    "assembly_reference_error", "false"
                )).lower() == "true"
            )
        }

        def component_is_visible(component: ZimaEntity) -> bool:
            # Preserve history-boundary and active-preview semantics from the
            # document, while reusing the one-pass dependency cascade above.
            if component.entity_id in dependency_suppressed_ids:
                return False
            return document.is_effectively_visible(component.entity_id)

        def component_display_color(component: ZimaEntity) -> str:
            if component.entity_id in dependency_error_ids:
                return "#C04020"
            source_document = (component_documents or {}).get(
                component.entity_id
            )
            inherited_color = (
                source_document.document_settings.get(
                    "body_color", "#B9C2CC"
                )
                if source_document is not None
                else component.parameters.get("body_color", "#B9C2CC")
            )
            return str(
                component.parameters.get("body_color", inherited_color)
                if str(component.parameters.get(
                    "body_color_override", "false"
                )).lower() == "true"
                else inherited_color
            )

        def has_later_assembly_cut(component: ZimaEntity) -> bool:
            component_index = next(
                (
                    index for index, candidate in enumerate(assembly_objects)
                    if candidate is component
                ),
                -1,
            )
            if component_index < 0:
                return False
            for feature in assembly_objects[component_index + 1:]:
                if feature.container_type not in (
                    ContainerType.PROTRUSION,
                    ContainerType.REVOLVE,
                ):
                    continue
                try:
                    excluded_ids = json.loads(str(
                        feature.parameters.get(
                            "assembly_excluded_component_ids", "[]"
                        )
                    ))
                except (TypeError, ValueError, json.JSONDecodeError):
                    continue
                if component.entity_id not in excluded_ids:
                    return True
            return False

        def nested_component_layers(
            component: ZimaEntity,
            source_document: PartDocument,
            transform: Any,
            inherited_color: str | None = None,
            instance_path: tuple[str, ...] = (),
            occurrence_root: str | None = None,
        ) -> tuple[list[ViewerMesh], dict[str, str]]:
            """Flatten nested Assembly viewer packets while retaining colors.

            A nested Assembly is a display container, not a single painted
            body.  Its source mesh has already been rebound to the nested
            component owner by the time it reaches this scene, so rebinding
            it again to the parent would erase the leaf ownership/color
            information.  Walk the persisted viewer packets instead.
            """
            override = str(component.parameters.get(
                "body_color_override", "false"
            )).lower() == "true"
            local_color = component_display_color(component)
            subtree_color = local_color if override else inherited_color
            occurrence_root = occurrence_root or component.entity_id
            meshes: list[ViewerMesh] = []
            colors: dict[str, str] = {}
            children = source_document.history_objects_at(
                source_document.history_cursor()
            )
            for child in children:
                if (
                    child.container_type != ContainerType.COMPONENT
                    or not source_document.is_effectively_visible(child.entity_id)
                ):
                    continue
                child_document = (component_documents or {}).get(
                    child.entity_id
                )
                if child_document is None:
                    continue
                child_world = coordinate_system_transform(
                    child.coordinate_system
                )
                # Compose transforms through the existing mesh transform
                # helper by first transforming the child packet locally.
                # The parent transform is applied below to preserve the
                # established assembly coordinate convention.
                if child_document.document_settings.get("type") == "assembly":
                    nested_meshes, nested_colors = nested_component_layers(
                        child,
                        child_document,
                        child_world,
                        subtree_color,
                        (*instance_path, child.entity_id),
                        occurrence_root,
                    )
                    meshes.extend(
                        transform_viewer_mesh(mesh, transform)
                        for mesh in nested_meshes
                    )
                    colors.update(nested_colors)
                    continue
                child_result = child_document.cached_body_result_at(
                    child_document.history_objects()
                )
                if child_result is None:
                    continue
                # Match the occurrence keys used by Assembly picking.  A
                # local child ID is not unique when the same Part appears
                # more than once through a nested Assembly.
                owner_id = "assembly-occurrence:" + "/".join((
                    occurrence_root,
                    *instance_path,
                    child.entity_id,
                ))
                mesh = transform_viewer_mesh(
                    child_result.mesh.with_owner(owner_id), child_world
                )
                meshes.append(transform_viewer_mesh(mesh, transform))
                colors[owner_id] = subtree_color or component_display_color(
                    child
                )
            return meshes, colors

        if assembly_cached_result is not None and any(
            obj.container_type == ContainerType.COMPONENT
            and not component_is_visible(obj)
            for obj in assembly_objects
        ):
            # The persisted packet represents the complete calculated
            # Assembly. Presentation hiding and dependency suppression are
            # applied by rebuilding its lightweight component mesh layers.
            assembly_cached_result = None
        if assembly_cached_result is not None:
            cached_body_result = assembly_cached_result
            cached_body_mesh = assembly_cached_result.mesh
            body_mesh = assembly_cached_result.mesh
            layers.append(body_mesh)
            for obj in assembly_objects:
                if obj.container_type == ContainerType.COMPONENT:
                    surface_colors_by_owner_id[obj.entity_id] = (
                        component_display_color(obj)
                    )
        for obj in (
            () if assembly_cached_result is not None else assembly_objects
        ):
            if not component_is_visible(obj):
                continue
            if obj.container_type != ContainerType.COMPONENT:
                continue
            source_document = (component_documents or {}).get(obj.entity_id)
            source_result = None
            if source_document is not None:
                source_result = source_document.cached_body_result_at(
                    source_document.history_objects()
                )
            component_mesh = None
            if obj.entity_id in (component_mesh_overrides or {}):
                # An activated Part may share its source document with other
                # Assembly instances.  Those passive instances must retain
                # their last committed world-space display while the active
                # document is rolled back for Properties or Sketcher.
                shape = None
                component_mesh = component_mesh_overrides[
                    obj.entity_id
                ].with_owner(obj.entity_id)
            elif (
                obj.entity_id == uncut_component_id
                and uncut_component_mesh is not None
                and not uncut_component_mesh.is_empty
            ):
                shape = None
                component_mesh = transform_viewer_mesh(
                    uncut_component_mesh.with_owner(obj.entity_id),
                    coordinate_system_transform(obj.coordinate_system),
                )
            elif (
                obj.entity_id == uncut_component_id
                and uncut_component_shape is not None
            ):
                shape = transform_shape(
                    uncut_component_shape,
                    coordinate_system_transform(obj.coordinate_system),
                )
            elif not has_later_assembly_cut(obj):
                shape = None
                if source_document is not None and source_document.document_settings.get(
                    "type"
                ) == "assembly":
                    nested_meshes, nested_colors = nested_component_layers(
                        obj,
                        source_document,
                        coordinate_system_transform(obj.coordinate_system),
                    )
                    if nested_meshes:
                        layers.extend(nested_meshes)
                        component_body_layers.extend(nested_meshes)
                        surface_colors_by_owner_id.update(nested_colors)
                        continue
                if source_result is None:
                    continue
                component_mesh = transform_viewer_mesh(
                    source_result.mesh.with_owner(obj.entity_id),
                    coordinate_system_transform(obj.coordinate_system),
                )
            else:
                shape = document.build_assembly_component_shape(
                    obj,
                    assembly_objects,
                    source_document=source_document,
                )
            if shape is not None or component_mesh is not None:
                if shape is not None:
                    shapes_by_owner_id[obj.entity_id] = shape
                surface_colors_by_owner_id[obj.entity_id] = (
                    component_display_color(obj)
                )
                layers.append(
                    component_mesh
                    if component_mesh is not None
                    else triangulate_shape(shape, owner_id=obj.entity_id)
                )
                component_mesh = layers[-1]
                component_body_layers.append(component_mesh)
                if source_document is not None:
                    registry = (
                        None
                        if source_result is not None
                        else active_face_registry(source_document)
                    )
                    imported = next((
                        child
                        for source in source_document.history_objects()
                        for child in source.children
                        if child.kind == EntityKind.IMPORTED_STEP
                        and not child.locked
                    ), None)
                    for face_index in set(
                        component_mesh.triangle_face_indices
                    ):
                        source_surface = (
                            source_result.surface(
                                source_document.root.entity_id,
                                face_index,
                            )
                            if source_result is not None
                            else None
                        )
                        reference = (
                            parse_face_reference(source_surface.reference_id)
                            if source_surface is not None
                            else registry.reference_for_runtime_index(face_index)
                            if registry is not None
                            else None
                        )
                        if reference is None and imported is not None:
                            reference = FaceRef(
                                imported.entity_id,
                                "imported",
                                str(face_index),
                            )
                        if reference is not None:
                            face_reference_ids[(obj.entity_id, face_index)] = (
                                assembly_face_descriptor(AssemblyFaceRef(
                                    obj.entity_id,
                                    reference,
                                ))
                            )
                    for edge in component_mesh.edges:
                        if edge.element_kind != "edge":
                            continue
                        source_curve = (
                            source_result.curve(
                                source_document.root.entity_id,
                                edge.edge_index,
                            )
                            if source_result is not None
                            else None
                        )
                        reference = (
                            parse_edge_reference(source_curve.reference_id)
                            if source_curve is not None
                            else registry.edge_reference_for_runtime_index(
                                edge.edge_index
                            )
                            if registry is not None
                            else None
                        )
                        if reference is None and imported is not None:
                            reference = EdgeRef(
                                imported.entity_id,
                                "imported",
                                str(edge.edge_index),
                            )
                        if reference is not None:
                            edge_reference_ids[(
                                obj.entity_id, edge.edge_index
                            )] = assembly_edge_descriptor(AssemblyEdgeRef(
                                obj.entity_id,
                                reference,
                            ))
        if component_body_layers:
            body_mesh = combine_viewer_meshes(tuple(component_body_layers))
        # Component bodies may come from the persisted Assembly result, in
        # which case the body-building loop above is intentionally skipped.
        # Datum/generated axes are independent viewer data and must still be
        # appended from each linked Part document.
        if show_user_axes:
            for obj in assembly_objects:
                if (
                    obj.container_type != ContainerType.COMPONENT
                    or not component_is_visible(obj)
                ):
                    continue
                source_document = (component_documents or {}).get(
                    obj.entity_id
                )
                if source_document is None:
                    continue
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
        cache_keys = document._shape_history_cache_keys(history_objects)
        if (
            cached_body_result is None
            and cached_body_shape is None
            and cache_keys
        ):
            cached_body_result = document.cached_body_result_at(
                history_objects
            )
        if cached_body_mesh is None and cached_body_result is not None:
            cached_body_mesh = cached_body_result.mesh
        shape = (
            cached_body_shape
            if cached_body_shape is not None
            else None
            if cached_body_result is not None
            else document.build_shape_at(boundary)
        )
        if (
            (shape is not None or cached_body_mesh is not None)
            and document.body_is_visible()
        ):
            if shape is not None:
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
                        obj.container_type not in (
                            ContainerType.FILLET,
                            ContainerType.CHAMFER,
                        )
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

    if (
        not is_assembly
        and body_mesh is not None
        and cached_body_result is None
    ):
        # Semantic identities are part of a newly calculated body result.
        # Compute them here, while OCCT calculation is allowed, rather than
        # lazily during the first Sketcher reference click.
        root_id = document.root.entity_id
        if imported_face_count > INTERACTIVE_TOPOLOGY_FACE_LIMIT:
            imported = next((
                child
                for obj in document.history_objects_at(boundary)
                for child in obj.children
                if child.kind == EntityKind.IMPORTED_STEP
            ), None)
            if imported is not None:
                for face_index in set(body_mesh.triangle_face_indices):
                    face_reference_ids[(root_id, face_index)] = FaceRef(
                        imported.entity_id,
                        "imported",
                        str(face_index),
                    ).serialize()
        else:
            # A rollback/intermediate body needs the semantic topology of
            # that exact history boundary.  Using the active final registry
            # assigned unrelated final-face identities to the intermediate
            # mesh, so downstream containers could not follow their stored
            # surface_reference_id after an upstream edit.
            registry = face_registry_at(document, boundary)
            for face_index in set(body_mesh.triangle_face_indices):
                reference = registry.reference_for_runtime_index(face_index)
                if reference is not None:
                    face_reference_ids[(root_id, face_index)] = (
                        reference.serialize()
                    )
            for edge in body_mesh.edges:
                reference = registry.edge_reference_for_runtime_index(
                    edge.edge_index
                )
                if (
                    reference is None
                    and shape is not None
                    and history_objects
                ):
                    reference = geometric_edge_reference(
                        shape,
                        edge.edge_index,
                        history_objects[-1].entity_id,
                    )
                if reference is not None:
                    edge_reference_ids[(root_id, edge.edge_index)] = (
                        reference.serialize()
                    )
            for point in body_mesh.points:
                reference = registry.vertex_reference_for_runtime_index(
                    point.point_index
                )
                if reference is not None:
                    vertex_reference_ids[(root_id, point.point_index)] = (
                        reference.serialize()
                    )
            if shape is not None:
                global_edges = TopTools_IndexedMapOfShape()
                topexp.MapShapes(shape, TopAbs_EDGE, global_edges)
                for face_index in set(body_mesh.triangle_face_indices):
                    face = topology_subshape(
                        shape,
                        element_kind="face",
                        element_index=face_index,
                    )
                    if face is None:
                        continue
                    boundary_ids: set[str] = set()
                    explorer = TopExp_Explorer(face, TopAbs_EDGE)
                    while explorer.More():
                        edge_index = global_edges.FindIndex(
                            explorer.Current()
                        )
                        reference_id = edge_reference_ids.get(
                            (root_id, edge_index)
                        )
                        if reference_id:
                            boundary_ids.add(reference_id)
                        explorer.Next()
                    face_boundary_edge_ids[(root_id, face_index)] = tuple(
                        sorted(boundary_ids)
                    )

    reference_scene_size = _scene_diagonal(layers)
    consumed_sketch_ids = {
        str(feature.parameters.get("sketch_id", ""))
        for history_object in document.history_objects_at(boundary)
        for feature in history_object.children
        if feature.kind in (EntityKind.PROTRUSION, EntityKind.REVOLVE)
        and str(feature.parameters.get("sketch_id", ""))
    }
    display_objects = document.history_objects_at(boundary)
    if editing_object_id is not None:
        editing_entity = document.find_entity(editing_object_id)
        editing_owner = (
            editing_entity
            if editing_entity is not None
            and editing_entity.kind == EntityKind.CONTAINER
            else document.find_owning_object(editing_object_id)
        )
        if (
            editing_owner is not None
            and all(
                obj.entity_id != editing_owner.entity_id
                for obj in display_objects
            )
        ):
            # The edited container is excluded from the rollback body, but
            # its own datum geometry remains the active edit/preview. Other
            # downstream containers must not leak through the boundary.
            display_objects = [*display_objects, editing_owner]
    def append_origin_context(
        source_document: PartDocument,
        source_objects,
        parent_transform,
        *,
        component_origins: bool,
        object_planes: bool,
        object_origins: bool,
    ) -> None:
        for source_object in source_objects:
            _append_object_origins(
                source_document,
                source_object,
                parent_transform,
                layers,
                object_planes,
                object_origins,
                component_origins,
                show_user_points,
                editing_object_id,
                reference_scene_size,
                preview_coordinate_system is not None,
                reference_owner_ids,
            )

    for obj in display_objects:
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
            consumed_sketch_ids,
        )
    append_origin_context(
        document,
        display_objects,
        identity_transform(),
        component_origins=show_component_origins,
        object_planes=show_object_planes,
        object_origins=show_object_origins,
    )

    # An activated Assembly keeps the parent document as the display root so
    # the surrounding components remain visible.  Its source Assembly is a
    # second document context, however, and its children must enter the same
    # scene traversal with the active instance transform.  Keep this in the
    # scene builder (rather than adding a viewer overlay) so Origin, datum
    # planes and axes use the same ownership and transform rules everywhere.
    if (
        active_reference_document is not None
        and active_reference_document.document_settings.get("type")
        == "assembly"
        and active_reference_transform is not None
    ):
        def append_active_references(
            source_document: PartDocument,
            parent_transform,
        ) -> None:
            for source_object in source_document.history_objects_at(
                source_document.history_cursor()
            ):
                if (
                    source_object.container_type != ContainerType.COMPONENT
                    or not source_document.is_effectively_visible(
                        source_object.entity_id
                    )
                ):
                    continue
                append_origin_context(
                    source_document,
                    (source_object,),
                    parent_transform,
                    component_origins=True,
                    object_planes=True,
                    object_origins=True,
                )
                child_document = (component_documents or {}).get(
                    source_object.entity_id
                )
                if (
                    child_document is not None
                    and child_document.document_settings.get("type")
                    == "assembly"
                ):
                    append_active_references(
                        child_document,
                        multiply_transforms(
                            parent_transform,
                            coordinate_system_transform(
                                source_object.coordinate_system
                            ),
                        ),
                    )

        append_active_references(
            active_reference_document,
            active_reference_transform,
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
        plane_indices = {"xy": 1, "yz": 2, "xz": 3}
        # A definition preview always exposes its complete local Origin.
        # The feature plane is an additional datum; when it lies directly on
        # its matching Origin plane, that one drawing represents both.
        feature_plane = (
            preview_plane
            if preview_plane in ("xy", "yz", "xz")
            else None
        )
        for plane_name in ("xy", "yz", "xz"):
            is_feature_plane = plane_name == feature_plane
            feature_is_offset = (
                is_feature_plane and abs(preview_plane_offset) > 1.0e-12
            )
            if feature_is_offset:
                local_plane_offset = (0.0, 0.0, 0.0)
            else:
                local_plane_offset = {
                    "xy": (0.0, 0.0, preview_plane_offset),
                    "yz": (preview_plane_offset, 0.0, 0.0),
                    "xz": (0.0, preview_plane_offset, 0.0),
                }[plane_name] if is_feature_plane else (0.0, 0.0, 0.0)
            layers.append(
                transform_viewer_mesh(
                    transform_viewer_mesh(
                        datum_plane_mesh(
                            owner_id=preview_owner_id,
                            plane_index=plane_indices[plane_name],
                            size=(
                                max(float(preview_plane_size), 0.001)
                                if is_feature_plane
                                and not feature_is_offset
                                and preview_plane_size is not None
                                else max(reference_scene_size * 0.12, 4.0)
                            ),
                            plane=plane_name,
                            label=plane_name.upper(),
                            screen_constant=(
                                not is_feature_plane
                                or feature_is_offset
                                or preview_plane_size is None
                            ),
                        ),
                        coordinate_system_transform(
                            CoordinateSystem(origin=local_plane_offset)
                        ),
                    ),
                    preview_transform,
                )
            )
        if feature_plane is not None and abs(preview_plane_offset) > 1.0e-12:
            local_plane_offset = {
                "xy": (0.0, 0.0, preview_plane_offset),
                "yz": (preview_plane_offset, 0.0, 0.0),
                "xz": (0.0, preview_plane_offset, 0.0),
            }[feature_plane]
            layers.append(
                transform_viewer_mesh(
                    transform_viewer_mesh(
                        datum_plane_mesh(
                            owner_id=preview_owner_id,
                            plane_index=4,
                            size=max(float(preview_plane_size or 0.001), 0.001),
                            plane=feature_plane,
                            label=feature_plane.upper(),
                            screen_constant=False,
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
    calculated_body_result = (
        cached_body_result
        if cached_body_mesh is not None and cached_body_result is not None
        else BodyResult.from_mesh(
            body_mesh,
            face_reference_ids=face_reference_ids,
            face_boundary_edge_ids=face_boundary_edge_ids,
            edge_reference_ids=edge_reference_ids,
            vertex_reference_ids=vertex_reference_ids,
        )
        if body_mesh is not None
        else None
    )
    if (
        calculated_body_result is not None
        and shape is not None
        and body_mesh is not None
    ):
        calculated_body_result = with_occt_analytic_surfaces(
            calculated_body_result,
            shape,
            document.root.entity_id,
        )
    if calculated_body_result is not None:
        treatment_feature_ids = {
            child.entity_id
            for obj in document.history_objects_at(boundary)
            if obj.container_type in (
                ContainerType.FILLET,
                ContainerType.CHAMFER,
            )
            for child in obj.children
            if child.kind in (EntityKind.FILLET, EntityKind.CHAMFER)
        }
        treatment_face_keys = {
            key
            for key, descriptor in calculated_body_result.faces.items()
            if (
                (reference := parse_face_reference(descriptor.reference_id))
                is not None
                and reference.feature_id in treatment_feature_ids
                and reference.role == "generated"
            )
        }
        calculated_body_result = (
            calculated_body_result.with_inferred_face_boundaries(
                treatment_face_keys
            )
        )
    if calculated_body_result is not None:
        cache_keys = document._shape_history_cache_keys(
            document.history_objects_at(boundary)
        )
        if cache_keys:
            document._body_result_cache[cache_keys[-1]] = (
                calculated_body_result
            )
    scene_mesh = combine_viewer_meshes(tuple(layers))
    return DocumentViewerScene(
        mesh=scene_mesh,
        shapes_by_owner_id=shapes_by_owner_id,
        surface_colors_by_owner_id=surface_colors_by_owner_id,
        body_result=BodyResult.from_mesh(
            scene_mesh,
            face_reference_ids=face_reference_ids,
            face_boundary_edge_ids=face_boundary_edge_ids,
            edge_reference_ids=edge_reference_ids,
            vertex_reference_ids=vertex_reference_ids,
            inherited=calculated_body_result,
            skip_triangle_count=(
                body_mesh.triangle_count
                if body_mesh is not None
                and calculated_body_result is not None
                else 0
            ),
        ),
        calculated_body_result=calculated_body_result,
        calculated_body_mesh=body_mesh,
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
    consumed_sketch_ids: set[str],
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
            if (
                child.entity_id in consumed_sketch_ids
                and obj.entity_id != editing_object_id
            ):
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
                consumed_sketch_ids,
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
    suppress_editing_origin: bool = False,
    reference_owner_ids: frozenset[str] = frozenset(),
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
    # A container that owns an actively referenced datum (for example the
    # placement plane of a Point container being edited) must expose that
    # datum so the reference can be confirmed visually, even when its own
    # auxiliary-geometry toggle is off and it is not the object being edited.
    is_active_reference_owner = obj.entity_id in reference_owner_ids
    if origin is not None and (
        show_component_origin
        or (
            obj.container_type != ContainerType.COMPONENT
            and obj.show_auxiliary_geometry
            and show_object_origins
        )
        or (
            obj.entity_id == editing_object_id
            and not suppress_editing_origin
        )
        or is_active_reference_owner
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
        if (
            show_object_planes
            or obj.entity_id == editing_object_id
            or is_active_reference_owner
        ):
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
                suppress_editing_origin,
                reference_owner_ids,
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
