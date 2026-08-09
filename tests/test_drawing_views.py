import unittest
from unittest.mock import patch
import json
import tempfile
from math import cos, radians, sin
from pathlib import Path
from types import SimpleNamespace

from PySide6.QtCore import QPointF
from PySide6.QtGui import QColor
from OCC.Core.Bnd import Bnd_Box
from OCC.Core.BRepBndLib import brepbndlib
from OCC.Core.BRepPrimAPI import BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder
from OCC.Core.TopAbs import TopAbs_FACE
from OCC.Core.TopExp import TopExp_Explorer

from zima_cad.app import (
    AssemblyComponentPropertiesDialog,
    AxisConstraintDialog,
    FamilyTableDialog,
    MainWindow,
    SKETCH_CONSTRAINT_SELECTION_TOOLS,
    SKETCH_ENTITY_SELECTION_TOOLS,
    ViewSelectionMode,
)
from zima_cad.body_result import BodyResult, SurfaceDescriptor
from zima_cad.topology import FaceRef
from zima_cad.drawing import (
    DrawingCanvas,
    cosmetic_pen,
    drawing_scale_text,
    delete_drawing_view,
    drawing_sheets,
    parallel_dimension_geometry,
    projected_view_orientation,
    projection_axes,
    project_polylines,
    move_drawing_view,
    technical_projection,
    shaded_projection,
    update_view_bounds,
)
from zima_cad.model import (
    active_face_registry,
    CoordinateSystem,
    ContainerType,
    EntityKind,
    create_empty_assembly,
    create_empty_drawing,
    create_empty_part,
    make_sketch_shape,
    coordinate_system_transform,
    multiply_transforms,
    transform_shape,
)
from zima_cad.sketch_model import SketchModel
from zima_cad.viewer import (
    CameraState,
    ZimaOpenGLViewer,
    STANDARD_VIEW_ORIENTATIONS,
    _camera_rotation_matrix,
    _surface_pass_for_display_mode,
    _multiply_rotation_matrices,
    orbit_camera_state,
    camera_angles_for_view_direction,
    preserve_camera_for_scene_bounds,
)
from zima_cad.viewer_mesh import (
    EdgePolyline,
    PointMarker,
    SilhouetteEdge,
    ViewerMesh,
    edge_visible_in_display,
    silhouette_segments,
    silhouette_segments_from_edges,
    triangulate_shape,
    topology_subshape,
)


class DrawingViewConventionTests(unittest.TestCase):
    def test_coincident_is_classified_as_selection_not_placement(self):
        self.assertIn("coincident", SKETCH_ENTITY_SELECTION_TOOLS)
        self.assertIn("coincident", SKETCH_CONSTRAINT_SELECTION_TOOLS)

    def test_history_surface_refresh_updates_nested_orientation_reference(self):
        document = create_empty_part()
        window = MainWindow.__new__(MainWindow)
        window.document = document
        stable_id = "stable-face"
        nested = {
            "type": "face",
            "reference_scope": "history_result",
            "surface_reference_id": stable_id,
            "entity_id": "stale-body",
            "topology_key": "99",
            "key": "face:stale-body:99",
            "equations": [[1.0, 0.0, 0.0, 1.0]],
        }
        references = [{
            "type": "container_orientation",
            "mappings": [{"slot": "primary", "reference": nested}],
        }]
        empty_mesh = ViewerMesh(
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
        body = BodyResult(
            mesh=empty_mesh,
            faces={
                f"{document.root.entity_id}:face:4": SurfaceDescriptor(
                    reference_id=stable_id,
                    kind="plane",
                    origin=(0.0, 0.0, 7.0),
                    normal=(0.0, 0.0, 1.0),
                )
            },
        )

        changed = window._refresh_history_result_surface_references(
            references,
            body,
        )

        self.assertTrue(changed)
        self.assertEqual(nested["entity_id"], document.root.entity_id)
        self.assertEqual(nested["topology_key"], "4")
        self.assertEqual(nested["equations"], [[0.0, 0.0, 1.0, 7.0]])

    def test_rebinding_saved_body_preserves_historical_source_bodies(self):
        empty_mesh = ViewerMesh(
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
        source = BodyResult.from_mesh(empty_mesh)
        saved = BodyResult.from_mesh(empty_mesh)
        saved = BodyResult(
            mesh=saved.mesh,
            faces=saved.faces,
            edges=saved.edges,
            vertices=saved.vertices,
            physical=saved.physical,
            source_bodies={"container-1": source},
        )

        rebound = saved.with_owner("runtime-part")

        self.assertIs(
            rebound.source_bodies["container-1"],
            source,
        )

    def test_loaded_point_vertical_relation_is_reapplied(self):
        entities = [
            {
                "id": "driven",
                "type": "point",
                "x": 12.5,
                "y": 20.0,
                "constraints": [{
                    "type": "vertical",
                    "point_id": "reference",
                    "relation_role": "driven",
                }],
            },
            {
                "id": "reference",
                "type": "point",
                "x": 10.0,
                "y": 0.0,
            },
        ]

        MainWindow._apply_sketch_coincident_constraints(entities)

        self.assertEqual(entities[0]["x"], 10.0)
        self.assertEqual(entities[0]["y"], 20.0)

    def test_dimension_origin_axis_does_not_need_construction_proxy(self):
        window = MainWindow.__new__(MainWindow)
        sketch = SimpleNamespace(entity_id="sketch")
        window.document = SimpleNamespace(
            find_entity=lambda entity_id: sketch if entity_id == "sketch" else None
        )
        window._sketch_edit_entity_id = "sketch"
        stored_reference = {
            "id": "origin-axis-from-view",
            "source_kind": "axis",
            "owner_id": "origin",
        }
        window._stored_sketch_external_references = lambda _sketch: [
            stored_reference
        ]
        window._external_reference_source = lambda _reference: SimpleNamespace(
            kind=EntityKind.ORIGIN,
            locked=True,
        )
        window._resolved_sketch_external_references = lambda _sketch: [{
            "id": "origin-axis-from-view",
            "geometry": {
                "type": "line",
                "point": [0.0, 0.0],
                "direction": [3.0, 0.0],
            },
        }]

        reference = window._canonical_dimension_origin_axis(
            "origin-axis-from-view"
        )

        self.assertEqual(reference, "sketch_axis:x")

    def test_navigation_paints_datum_planes_continuously(self):
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._navigation_active = True
        viewer._sketch_frame = None
        viewer._object_overlay_persistent = False
        painted = []
        viewer._paint_screen_constant_edges = lambda: painted.append("edges")
        viewer._paint_planes = lambda: painted.append("planes")
        viewer._paint_reference_highlights = lambda: None
        viewer._paint_dimensions = lambda: None
        viewer._paint_passive_sketch_overlay = lambda: None
        viewer._paint_source_topology_hover = lambda: None
        viewer._paint_edge_labels = lambda **_kwargs: None

        viewer._paint_navigation_overlays()

        self.assertEqual(painted, ["edges", "planes"])

    def test_source_topology_pick_uses_persisted_viewer_mesh_only(self):
        window = MainWindow.__new__(MainWindow)
        source = SimpleNamespace(entity_id="source-1")
        mesh = SimpleNamespace(marker="persisted")
        window.document = SimpleNamespace(
            history_objects_at=lambda _boundary: [source],
        )
        window._definition_history_boundary = lambda: 1
        window._native_viewer_scene = SimpleNamespace(
            calculated_body_result=SimpleNamespace(
                source_bodies={
                    source.entity_id: SimpleNamespace(mesh=mesh),
                }
            )
        )
        window.native_viewer = SimpleNamespace(
            face_at_mesh=lambda candidate, _position:
                (source.entity_id, 7) if candidate is mesh else None,
            edge_at_mesh=lambda _candidate, _position: None,
            point_at_mesh=lambda _candidate, _position: None,
        )

        self.assertEqual(
            window._source_topology_reference_at_position(
                "face", QPointF(10.0, 20.0)
            ),
            (source.entity_id, 7, mesh),
        )

    def test_viewer_mesh_extracts_persisted_edge_and_point(self):
        mesh = ViewerMesh(
            triangle_positions=(),
            triangle_normals=(),
            triangle_face_indices=(),
            triangle_owner_ids=(),
            edges=(EdgePolyline(
                edge_index=3,
                points=((1.0, 2.0, 3.0), (4.0, 5.0, 6.0)),
                owner_id="source-1",
            ),),
            points=(PointMarker(
                point_index=4,
                position=(7.0, 8.0, 9.0),
                owner_id="source-1",
                element_kind="vertex",
            ),),
            planes=(),
            bounds_min=(1.0, 2.0, 3.0),
            bounds_max=(7.0, 8.0, 9.0),
        )

        self.assertEqual(len(mesh.edge_mesh("source-1", 3).edges), 1)
        self.assertEqual(len(mesh.point_mesh("source-1", 4).points), 1)

    def test_external_segment_uses_original_topology_reference_picking(self):
        window = MainWindow.__new__(MainWindow)
        window._sketch_reference_mode = False
        window.point_constraint_dialog = None
        window._selection_controller = SimpleNamespace(
            request=SimpleNamespace(command_id="sketch_external_segment")
        )

        self.assertTrue(window._original_topology_reference_pick_active())

    def test_reverse_one_side_dimension_uses_the_dialog_forward_parameter(
        self,
    ) -> None:
        feature = SimpleNamespace(parameters={
            "extent_mode": "one_side",
            "direction": "reverse",
        })

        self.assertEqual(
            MainWindow._feature_dimension_parameter_key(
                feature,
                "length_reverse",
            ),
            "length_forward",
        )
        self.assertEqual(
            MainWindow._feature_dimension_parameter_key(
                feature,
                "angle_reverse",
            ),
            "angle",
        )

    def test_two_side_dimension_keeps_its_own_dialog_parameter(self) -> None:
        feature = SimpleNamespace(parameters={"extent_mode": "two_sides"})

        self.assertEqual(
            MainWindow._feature_dimension_parameter_key(
                feature,
                "length_reverse",
            ),
            "length_reverse",
        )

    def test_return_from_sketch_drops_hidden_properties_dialog_before_reopen(
        self,
    ) -> None:
        document = create_empty_part()
        protrusion = document.create_container(
            "Protrusion001",
            ContainerType.PROTRUSION,
        )
        reopened = []
        selected = []
        window = MainWindow.__new__(MainWindow)
        window.document = document
        window._sketch_return_properties_id = protrusion.entity_id
        window.point_constraint_dialog = SimpleNamespace(
            isVisible=lambda: False,
        )
        window._select_tree_object_without_reference_event = selected.append
        window._edit_protrusion = lambda target, rebuild_rollback: reopened.append(
            (target, rebuild_rollback)
        )

        with patch("zima_cad.app.QTimer.singleShot"):
            window._reopen_sketch_properties("unused-sketch")

        self.assertEqual(selected, [protrusion.entity_id])
        self.assertEqual(reopened, [(protrusion, False)])

    def test_properties_dialog_consumes_face_when_general_selection_is_disabled(
        self,
    ) -> None:
        owner = SimpleNamespace(entity_id="Protrusion001")
        root = SimpleNamespace(entity_id="result-body")
        selected: list[tuple[object, object, int | None]] = []

        class VisibleDialog:
            @staticmethod
            def isVisible():
                return True

        class Shape:
            @staticmethod
            def ShapeType():
                return TopAbs_FACE

        class WindowState:
            document = SimpleNamespace(
                root=root,
                find_entity=lambda _owner_id: owner,
            )
            point_constraint_dialog = VisibleDialog()
            view_selection_enabled = False
            _sketch_reference_mode = False
            _active_component_entity_id = None
            _active_component_document = None
            view_selection_mode = ViewSelectionMode.CONTAINER

            @staticmethod
            def _try_pick_protrusion_profile(_obj):
                return False

            @staticmethod
            def _add_point_shape_constraint(
                obj,
                shape,
                *,
                topology_index=None,
            ):
                selected.append((obj, shape, topology_index))

            @staticmethod
            def _select_native_tree_object(_owner_id):
                return None

        shape = Shape()
        MainWindow._apply_native_view_selection(
            WindowState(),
            owner.entity_id,
            shape,
            topology_index=17,
        )
        self.assertEqual(selected, [(owner, shape, 17)])

    def test_non_displayed_history_mesh_picks_edges_and_vertices(self) -> None:
        mesh = ViewerMesh(
            triangle_positions=(),
            triangle_normals=(),
            triangle_face_indices=(),
            triangle_owner_ids=(),
            edges=(EdgePolyline(
                edge_index=7,
                points=((0.0, 0.0, 2.0), (10.0, 0.0, 2.0)),
                owner_id="Protrusion001",
            ),),
            points=(PointMarker(
                point_index=3,
                position=(0.0, 0.0, 2.0),
                owner_id="Protrusion001",
                element_kind="vertex",
            ),),
            planes=(),
            bounds_min=(0.0, 0.0, 2.0),
            bounds_max=(10.0, 0.0, 2.0),
        )

        class ViewerState:
            @staticmethod
            def devicePixelRatioF():
                return 1.0

            @staticmethod
            def _camera_point(point):
                return point

            @staticmethod
            def _screen_point(point):
                return QPointF(point[0], point[1])

            @staticmethod
            def _display_edge_points(edge):
                return edge.points

            _point_segment_distance = staticmethod(
                ZimaOpenGLViewer._point_segment_distance
            )

        viewer = ViewerState()
        self.assertEqual(
            ZimaOpenGLViewer.edge_at_mesh(viewer, mesh, QPointF(5.0, 0.0)),
            ("Protrusion001", 7),
        )
        self.assertEqual(
            ZimaOpenGLViewer.point_at_mesh(viewer, mesh, QPointF(0.0, 0.0)),
            ("Protrusion001", 3),
        )

    def test_container_frame_slots_expand_into_position_and_orientation(self):
        primary = {"type": "face", "key": "face", "label": "Face"}
        secondary = {"type": "edge", "key": "edge", "label": "Edge"}
        expanded = MainWindow._expanded_container_frame_references([{
            "type": "container_orientation",
            "work_plane_offset": 12.5,
            "mappings": [
                {
                    "slot": "primary",
                    "reference": primary,
                    "role": "back",
                },
                {
                    "slot": "secondary",
                    "reference": secondary,
                    "role": "right",
                },
            ],
        }])

        self.assertEqual(expanded[0]["orientation_role"], "opposite_normal")
        self.assertEqual(expanded[0]["position_role"], "orientation_only")
        self.assertEqual(expanded[1]["orientation_role"], "right")
        self.assertEqual(expanded[1]["position_role"], "orientation_only")
        self.assertEqual(MainWindow._container_plane_offset([{
            "type": "container_orientation",
            "work_plane_offset": 12.5,
        }]), 12.5)

    def test_container_frame_marks_slots_without_copying_position_references(self):
        position = {
            "type": "entity",
            "key": "position",
            "orientation_role": "up",
            "orientation_drives_rotation": True,
        }
        front = {"type": "face", "key": "front"}
        expanded = MainWindow._expanded_container_frame_references([
            position,
            {
                "type": "container_orientation",
                "mappings": [{
                    "slot": "primary",
                    "reference": front,
                    "role": "front",
                }],
            },
        ])

        self.assertEqual(expanded[0]["orientation_role"], "up")
        self.assertTrue(expanded[0]["orientation_drives_rotation"])
        self.assertEqual(
            expanded[1]["container_orientation_slot"], "primary"
        )
        self.assertEqual(expanded[1]["orientation_role"], "normal")
        self.assertTrue(expanded[1]["preserve_orientation_sign"])

    def test_explicit_frame_drives_rotation_instead_of_position_roles(self):
        class RotationHarness:
            _normalized_vector = staticmethod(MainWindow._normalized_vector)
            _cross_product = staticmethod(MainWindow._cross_product)

            @staticmethod
            def _orientation_reference_vector(
                descriptor,
                *,
                allow_frame_fallback,
                resolve_shape_references=False,
            ):
                return tuple(descriptor["direction"])

        rotation = MainWindow._plane_reference_rotation(
            RotationHarness(),
            [
                {
                    "type": "fixed_direction",
                    "direction": (1.0, 0.0, 0.0),
                    "orientation_role": "up",
                },
                {
                    "type": "fixed_direction",
                    "direction": (0.0, 1.0, 0.0),
                    "orientation_role": "normal",
                    "container_orientation_slot": "primary",
                },
            ],
        )

        for value in rotation:
            self.assertAlmostEqual(value, 0.0)

    def test_datum_plane_is_parallel_to_explicit_front_reference(self):
        class DatumHarness:
            _normalized_vector = staticmethod(MainWindow._normalized_vector)
            _cross_product = staticmethod(MainWindow._cross_product)
            _local_plane_for_normal = staticmethod(
                MainWindow._local_plane_for_normal
            )
            _datum_frame_references = staticmethod(
                MainWindow._datum_frame_references
            )
            _plane_reference_rotation = MainWindow._plane_reference_rotation

            @staticmethod
            def _orientation_reference_vector(
                descriptor,
                *,
                allow_frame_fallback,
            ):
                return tuple(descriptor["direction"])

        source_normal = (0.2, 0.4, 0.8944271909999159)
        plane, rotation = MainWindow._datum_plane_frame(
            DatumHarness(),
            [{
                "type": "fixed_direction",
                "direction": source_normal,
                "orientation_role": "normal",
                "container_orientation_slot": "primary",
            }],
            "xy",
        )
        local_normal = {
            "xy": (0.0, 0.0, 1.0),
            "yz": (1.0, 0.0, 0.0),
            "xz": (0.0, 1.0, 0.0),
        }[plane]
        transform = coordinate_system_transform(
            CoordinateSystem(rotation=rotation)
        )
        world_normal = tuple(
            sum(transform[row][column] * local_normal[column]
                for column in range(3))
            for row in range(3)
        )
        agreement = abs(sum(
            world_normal[index] * source_normal[index]
            for index in range(3)
        ))
        self.assertAlmostEqual(agreement, 1.0, places=7)

    def test_face_front_preserves_outward_normal_and_back_reverses_it(self):
        class DatumHarness:
            _normalized_vector = staticmethod(MainWindow._normalized_vector)
            _cross_product = staticmethod(MainWindow._cross_product)
            _local_plane_for_normal = staticmethod(
                MainWindow._local_plane_for_normal
            )
            _datum_frame_references = staticmethod(
                MainWindow._datum_frame_references
            )
            _plane_reference_rotation = MainWindow._plane_reference_rotation

            @staticmethod
            def _orientation_reference_vector(
                descriptor,
                *,
                allow_frame_fallback,
            ):
                return tuple(descriptor["direction"])

        outward = (-0.2, -0.4, -0.8944271909999159)
        for role, expected_agreement in (("front", 1.0), ("back", -1.0)):
            references = [{
                "type": "container_orientation",
                "mappings": [{
                    "slot": "primary",
                    "reference": {
                        "type": "face",
                        "direction": outward,
                    },
                    "role": role,
                }],
            }]
            plane, rotation = MainWindow._datum_plane_frame(
                DatumHarness(), references, "xz"
            )
            local_normal = {
                "xy": (0.0, 0.0, 1.0),
                "yz": (1.0, 0.0, 0.0),
                "xz": (0.0, 1.0, 0.0),
            }[plane]
            transform = coordinate_system_transform(
                CoordinateSystem(rotation=rotation)
            )
            world_normal = tuple(
                sum(
                    transform[row][column] * local_normal[column]
                    for column in range(3)
                )
                for row in range(3)
            )
            agreement = sum(
                world_normal[index] * outward[index]
                for index in range(3)
            )
            self.assertAlmostEqual(
                agreement, expected_agreement, places=7
            )

    def test_front_offset_does_not_override_locked_container_origin(self):
        class ConstraintHarness:
            document = None

            @staticmethod
            def _resolved_shape_reference_equations(_descriptor):
                return None

        origin = {
            "type": "vertex",
            "equations": [
                [1.0, 0.0, 0.0, 0.0],
                [0.0, 1.0, 0.0, 0.0],
                [0.0, 0.0, 1.0, 0.0],
            ],
        }
        front = {
            "type": "face",
            "equations": [[0.0, 0.0, 1.0, 0.0]],
        }
        solution, dof, _status, _constrained = (
            MainWindow._solve_point_constraints(
                ConstraintHarness(),
                [
                    origin,
                    {
                        "type": "container_orientation",
                        "work_plane_offset": 25.0,
                        "mappings": [{
                            "slot": "primary",
                            "reference": front,
                            "role": "front",
                        }],
                    },
                ],
            )
        )

        self.assertEqual(solution, (0.0, 0.0, 0.0))
        self.assertEqual(dof, 0)

    def test_stored_shape_equations_can_be_used_without_live_resolution(self):
        class ConstraintHarness:
            document = None

            @staticmethod
            def _resolved_shape_reference_equations(_descriptor):
                raise AssertionError("live OCCT reference resolution was used")

        solution, dof, _status, _constrained = (
            MainWindow._solve_point_constraints(
                ConstraintHarness(),
                [{
                    "type": "face",
                    "equations": [[0.0, 0.0, 1.0, 12.0]],
                }],
                (3.0, 4.0, 5.0),
                resolve_shape_references=False,
            )
        )

        self.assertEqual(solution, (3.0, 4.0, 12.0))
        self.assertEqual(dof, 2)

    def test_cached_external_sketch_geometry_avoids_live_projection(self):
        document = create_empty_part()
        container = document.create_container("Sketch", ContainerType.SKETCH)
        sketch = document.create_sketch(container.entity_id)
        cached = {
            "type": "polyline",
            "points": [[1.0, 2.0], [3.0, 4.0]],
        }
        sketch.parameters["external_references"] = json.dumps([{
            "id": "edge:cached",
            "source_kind": "edge",
            "owner_id": document.root.entity_id,
            "element_index": 1,
            "cached_geometry": cached,
        }])
        window = MainWindow.__new__(MainWindow)
        window.document = document
        window._active_component_return_document = None
        window._sketch_selected_external_reference_id = None
        window._sync_external_profile_segments = lambda *_args: None
        window._project_sketch_external_reference = lambda *_args: (
            (_ for _ in ()).throw(
                AssertionError("live OCCT projection was used")
            )
        )

        resolved = window._resolved_sketch_external_references(sketch)

        self.assertEqual(resolved[0]["geometry"], cached)

    def test_plane_offset_is_stored_along_its_local_normal(self):
        self.assertEqual(
            MainWindow._plane_local_offset("xy", 6.0),
            (0.0, 0.0, 6.0),
        )
        self.assertEqual(
            MainWindow._plane_local_offset("yz", 6.0),
            (6.0, 0.0, 0.0),
        )
        self.assertEqual(
            MainWindow._plane_local_offset("xz", 6.0),
            (0.0, 6.0, 0.0),
        )

    def test_work_plane_dimension_respects_front_back_role(self):
        document = create_empty_part()
        container = document.create_container(
            "Protrusion",
            ContainerType.PROTRUSION,
        )
        window = MainWindow.__new__(MainWindow)
        window.document = document
        window._resolved_shape_reference_equations = lambda _reference: None
        primary = {
            "type": "face",
            "equations": [[0.0, 0.0, 1.0, 0.0]],
        }

        def dimension_for(role):
            container.parameters["constraint_refs"] = json.dumps([{
                "type": "container_orientation",
                "work_plane_offset": 6.0,
                "mappings": [{
                    "slot": "primary",
                    "role": role,
                    "reference": primary,
                }],
            }])
            return next(
                dimension
                for dimension in window._reference_dimensions(container)
                if dimension.key == "work_plane_offset"
            )

        self.assertEqual(
            dimension_for("front").second_point,
            (0.0, 0.0, 6.0),
        )
        self.assertEqual(
            dimension_for("back").second_point,
            (0.0, 0.0, -6.0),
        )

    def test_rotation_offsets_are_composed_in_local_container_frame(self):
        base = (25.0, -35.0, 40.0)
        offset = (15.0, 20.0, -10.0)
        actual = MainWindow._rotation_with_local_offset(base, offset)
        actual_transform = coordinate_system_transform(
            CoordinateSystem(rotation=actual)
        )
        expected_transform = multiply_transforms(
            coordinate_system_transform(CoordinateSystem(rotation=base)),
            coordinate_system_transform(CoordinateSystem(rotation=offset)),
        )
        for row in range(3):
            for column in range(3):
                self.assertAlmostEqual(
                    actual_transform[row][column],
                    expected_transform[row][column],
                )

    def test_fillet_command_resolves_edge_after_command_activation(self) -> None:
        document = create_empty_part()
        container = document.create_container("Box", ContainerType.BOX)
        box = document.create_primitive(container.entity_id, EntityKind.BOX)
        self.assertIsNotNone(box)
        reference = MainWindow._fillet_edge_reference(
            document, document.root.entity_id, 1
        )
        self.assertIsNotNone(reference)
        self.assertEqual(reference.role, "boundary")
        self.assertIsNone(
            MainWindow._fillet_edge_reference(
                document, container.entity_id, 1
            )
        )

    def test_original_solid_reference_resolves_without_result_body(self) -> None:
        document = create_empty_part()
        container = document.create_container("Box", ContainerType.BOX)
        document.create_primitive(container.entity_id, EntityKind.BOX)
        reference = active_face_registry(
            document
        ).edge_reference_for_runtime_index(1)
        self.assertIsNotNone(reference)

        resolution = MainWindow._direct_source_reference_resolution(
            document,
            reference,
            "edge",
        )
        self.assertIsNotNone(resolution)
        self.assertIsNotNone(resolution.shape)

    def test_lazy_assembly_pick_registers_only_selected_frame(self) -> None:
        dialog = SimpleNamespace(
            _reference_frames={},
            _source_frame_keys=set(),
        )
        MainWindow._register_lazy_assembly_frame(
            dialog,
            "selected-face",
            (1.0, 2.0, 3.0),
            (0.0, 0.0, 1.0),
            True,
        )
        self.assertEqual(
            dialog._reference_frames,
            {"selected-face": ((1.0, 2.0, 3.0), (0.0, 0.0, 1.0))},
        )
        self.assertEqual(dialog._source_frame_keys, {"selected-face"})

    def test_assembly_displayed_face_maps_to_original_solid(self) -> None:
        source_document = create_empty_part()
        source = source_document.create_container("Box", ContainerType.BOX)
        source_document.create_primitive(source.entity_id, EntityKind.BOX)
        assembly = create_empty_assembly()
        component = assembly.create_container(
            "Component",
            ContainerType.COMPONENT,
        )
        component.coordinate_system.origin = (35.0, -12.0, 8.0)
        source_shape = source_document.build_standalone_shape(source)
        instance_shape = transform_shape(
            source_shape,
            coordinate_system_transform(component.coordinate_system),
        )
        explorer = TopExp_Explorer(instance_shape, TopAbs_FACE)
        self.assertTrue(explorer.More())

        window = MainWindow.__new__(MainWindow)
        match = window._component_source_face_from_displayed_face(
            component,
            source_document,
            explorer.Current(),
        )

        self.assertIsNotNone(match)
        self.assertEqual(match[0].entity_id, source.entity_id)
        self.assertGreater(match[1], 0)

    def test_assembly_mate_allows_edited_component_as_source(self) -> None:
        class VisibleDialog:
            @staticmethod
            def isVisible():
                return True

        window = MainWindow.__new__(MainWindow)
        window.assembly_component_dialog = VisibleDialog()
        window._definition_edit_objects = [
            SimpleNamespace(
                entity_id="edited-component",
                kind=EntityKind.CONTAINER,
                children=[],
            )
        ]

        self.assertFalse(
            window._current_definition_owns_reference("edited-component")
        )
        self.assertEqual(window._definition_reference_excluded_ids(), set())

    def test_large_mesh_face_pick_can_be_enabled_for_assembly_mates(self) -> None:
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._large_mesh_topology_enabled = False

        viewer.set_large_mesh_topology_enabled(True)

        self.assertTrue(viewer._large_mesh_topology_enabled)

    def test_surface_candidate_cycle_ignores_points_and_edges(self) -> None:
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._mesh = SimpleNamespace(
            points=(SimpleNamespace(
                element_kind="point",
                position=(0.0, 0.0, 0.0),
                owner_id="point-owner",
                point_index=1,
            ),),
            edges=(SimpleNamespace(
                element_kind="edge",
                topology_role="sharp",
                owner_id="edge-owner",
                edge_index=1,
            ),),
            planes=(),
        )
        viewer._selection_filter = "surface"
        face_queries = []

        def face_hits(*args, **kwargs):
            face_queries.append((args, kwargs))
            return [
                (2.0, "body", 1),
                (1.0, "body", 2),
            ]

        viewer._face_hits = face_hits
        viewer._topology_owner_is_selectable = lambda _owner_id: True
        viewer.devicePixelRatioF = lambda: 1.0

        candidates = viewer.topology_candidates_at(QPointF())

        self.assertEqual(
            candidates,
            (("face", "body", 1), ("face", "body", 2)),
        )
        self.assertEqual(len(face_queries), 1)
        self.assertEqual(face_queries[0][1]["bounds_tolerance"], 4.0)

    def test_cycled_hidden_face_preview_is_not_replaced_by_cursor_hit(self) -> None:
        window = MainWindow.__new__(MainWindow)
        window._dimension_inspection_visuals = ()
        cleared = []
        hovered = []
        window.native_viewer = SimpleNamespace(
            _cycled_topology_candidate=("face", "body", 6),
            set_source_topology_hover=lambda mesh: cleared.append(mesh),
            set_feature_hover_edges=lambda edges: cleared.append(edges),
        )
        window._set_view_hover = lambda *value: hovered.append(value)
        window._on_native_source_topology_hovered = lambda *_args: self.fail(
            "cycled preview must not be replaced by a cursor ray hit"
        )

        window._on_native_face_hovered("body", 6)

        self.assertEqual(cleared, [None, set()])
        self.assertEqual(hovered, [("face", "body", 6)])

    def test_result_body_can_be_excluded_from_object_picking(self) -> None:
        viewer = ZimaOpenGLViewer.__new__(ZimaOpenGLViewer)
        viewer._excluded_object_owner_ids = frozenset({"result-body"})
        viewer._pick_face = lambda _position: ("result-body", 1)
        viewer._pick_edge = lambda _position: ("result-body", 1)

        self.assertIsNone(viewer._pick_object(QPointF()))

        viewer._excluded_object_owner_ids = frozenset()
        self.assertEqual(viewer._pick_object(QPointF()), "result-body")

    def test_part_hover_resolves_latest_historical_container(self) -> None:
        document = create_empty_part()
        first = document.create_container("First", ContainerType.BOX)
        document.create_primitive(first.entity_id, EntityKind.BOX)
        second = document.create_container("Second", ContainerType.BOX)
        document.create_primitive(second.entity_id, EntityKind.BOX)
        window = MainWindow.__new__(MainWindow)
        window.document = document
        window._cached_source_model_shapes = []
        window._cached_source_model_meshes = {}
        window._definition_history_boundary = lambda: 2
        window.native_viewer = SimpleNamespace(
            mesh_is_under_cursor=lambda _mesh, _position: True,
        )

        hit = window._part_history_container_at_position(QPointF())

        self.assertIsNotNone(hit)
        self.assertIs(hit[0], second)

    def test_new_container_origin_uses_fallback_before_references(self) -> None:
        class Edit:
            def __init__(self, value):
                self._value = value

            def value(self):
                return self._value

        dialog = SimpleNamespace(
            point_object=None,
            solution=None,
            coordinate_edits=(Edit(4.0), Edit(5.0), Edit(6.0)),
            rotation_edits=None,
            point_rotation=lambda: (0.0, 0.0, 0.0),
            _solution_references=lambda: [],
        )
        window = MainWindow.__new__(MainWindow)
        window.point_constraint_dialog = dialog
        window._plane_reference_rotation = lambda _references: (0.0, 0.0, 0.0)
        window._rotation_with_local_offset = lambda base, _offset: base

        coordinate_system = window._definition_preview_coordinate_system()

        self.assertEqual(coordinate_system.origin, (4.0, 5.0, 6.0))

    def test_middle_dismiss_invalidates_the_stored_part_hover(self) -> None:
        window = MainWindow.__new__(MainWindow)
        window._sketch_edit_entity_id = None
        window._sketch_show_all_dimensions = False
        window._part_hover_container_id = "old-container"
        window._part_hover_container_mesh = object()
        window._dismiss_view_selection_requested = False
        window._dimension_overlays = {}
        window._dimension_object_id = None
        window._dimension_bindings = {}
        window._dimension_owner_ids = {}
        window.native_viewer = SimpleNamespace(
            set_dimensions=lambda _dimensions: None,
        )

        window._dismiss_dimension_overlays()

        self.assertTrue(window._dismiss_view_selection_requested)
        self.assertIsNone(window._part_hover_container_id)
        self.assertIsNone(window._part_hover_container_mesh)

    def test_imported_face_reference_is_created_lazily(self) -> None:
        imported = SimpleNamespace(
            entity_id="imported-step",
            kind=EntityKind.IMPORTED_STEP,
            locked=False,
        )
        source = SimpleNamespace(children=[imported])

        reference = MainWindow._lazy_imported_face_reference(source, 12001)

        self.assertEqual(
            reference,
            FaceRef("imported-step", "imported", "12001"),
        )

    def test_assembly_properties_forces_viewport_reference_mode(self) -> None:
        calls = []

        class Viewer:
            def __getattr__(self, name):
                return lambda value=None: calls.append((name, value))

        window = MainWindow.__new__(MainWindow)
        window.native_viewer = Viewer()

        window._configure_assembly_reference_picking()

        self.assertIn(("set_selection_enabled", True), calls)
        self.assertIn(("set_selection_filter", "surface"), calls)
        self.assertIn(("set_interaction_mode", "topology"), calls)
        self.assertIn(("set_excluded_topology_owners", set()), calls)
        self.assertIn(("set_large_mesh_topology_enabled", True), calls)

    def test_lazy_assembly_pick_filter_contains_real_components(self) -> None:
        assembly = create_empty_assembly()
        source = assembly.create_container("Source", ContainerType.COMPONENT)
        target = assembly.create_container("Target", ContainerType.COMPONENT)
        window = MainWindow.__new__(MainWindow)
        window.document = assembly

        self.assertEqual(
            window._assembly_reference_pick_owner_ids(source, "source"),
            {source.entity_id},
        )
        self.assertEqual(
            window._assembly_reference_pick_owner_ids(source, "target"),
            {target.entity_id},
        )

    def test_parent_transform_propagates_through_assembly_mate_chain(self) -> None:
        assembly = create_empty_assembly()
        parent = assembly.create_container("Parent", ContainerType.COMPONENT)
        child = assembly.create_container("Child", ContainerType.COMPONENT)
        grandchild = assembly.create_container(
            "Grandchild",
            ContainerType.COMPONENT,
        )
        child.coordinate_system.origin = (10.0, 0.0, 0.0)
        grandchild.coordinate_system.origin = (20.0, 0.0, 0.0)
        child.parameters["assembly_mates"] = json.dumps([{
            "target": f"{parent.entity_id}:plane:XY",
        }])
        grandchild.parameters["assembly_mates"] = json.dumps([{
            "target": f"{child.entity_id}:plane:XY",
        }])
        window = MainWindow.__new__(MainWindow)
        window.document = assembly
        previous = window._homogeneous_transform(parent.coordinate_system)

        parent.coordinate_system.origin = (5.0, 3.0, 0.0)
        window._propagate_assembly_component_transform(parent, previous)

        self.assertEqual(child.coordinate_system.origin, (15.0, 3.0, 0.0))
        self.assertEqual(
            grandchild.coordinate_system.origin,
            (25.0, 3.0, 0.0),
        )

    def test_standalone_sketch_edit_uses_its_own_history_boundary(self) -> None:
        document = create_empty_part()
        before = document.create_container("Before", ContainerType.BOX)
        document.create_primitive(before.entity_id, EntityKind.BOX)
        owner = document.create_container("Sketch", ContainerType.SKETCH)
        sketch = document.create_sketch(owner.entity_id)
        after = document.create_container("After", ContainerType.BOX)
        document.create_primitive(after.entity_id, EntityKind.BOX)

        window = MainWindow.__new__(MainWindow)
        window.document = document
        window._sketch_edit_entity_id = sketch.entity_id

        self.assertEqual(window._definition_history_boundary(), 1)

    def test_sketch_projects_face_from_original_history_solid(self) -> None:
        document = create_empty_part()
        source = document.create_container("Box", ContainerType.BOX)
        document.create_primitive(source.entity_id, EntityKind.BOX)
        sketch_owner = document.create_container(
            "Sketch",
            ContainerType.SKETCH,
        )
        sketch = document.create_sketch(sketch_owner.entity_id)

        window = MainWindow.__new__(MainWindow)
        window.document = document
        window._native_viewer_scene = None
        window._active_component_return_document = None
        window._active_component_entity_id = None

        projection = window._project_sketch_external_reference(
            sketch,
            {
                "source_kind": "face",
                "owner_id": source.entity_id,
                "element_index": 1,
            },
        )

        self.assertIsNotNone(projection)
        self.assertEqual(projection["type"], "polylines")

    def test_view_double_click_does_not_open_container_in_sketcher(self) -> None:
        window = MainWindow.__new__(MainWindow)
        window._sketch_edit_entity_id = "active-sketch"
        window._selected_object = lambda: self.fail(
            "Sketcher double-click reached container editing"
        )

        window._on_native_object_double_clicked("owning-protrusion")

    def test_view_double_click_uses_the_hovered_container_identity(self) -> None:
        document = create_empty_part()
        box = document.create_container("Box", ContainerType.BOX)
        protrusion = document.create_container(
            "Protrusion",
            ContainerType.PROTRUSION,
        )
        activated = []
        shown = []

        class Viewer:
            @staticmethod
            def blockSignals(_blocked):
                return False

            @staticmethod
            def _clear_topology_selection():
                return None

            @staticmethod
            def set_selected_container_contents(_ids):
                return None

            @staticmethod
            def set_selected_container_origin(_owner_id):
                return None

        window = MainWindow.__new__(MainWindow)
        window.document = document
        window._sketch_edit_entity_id = None
        window._part_hover_container_id = protrusion.entity_id
        window._selected_object = lambda: box
        window._edge_treatment_at_last_view_click = lambda _owner_id: None
        window._activate_object_for_editing = (
            lambda target: activated.append(target) or target
        )
        window._show_protrusion_profile_overlay = shown.append
        window.native_viewer = Viewer()

        window._on_native_object_double_clicked("")

        self.assertEqual(activated, [protrusion])
        self.assertEqual(shown, [protrusion])

    def test_sketch_placement_picks_original_face_directly(self) -> None:
        original = ("source-solid", 4, object())
        window = MainWindow.__new__(MainWindow)
        window._source_topology_reference_at_cursor = (
            lambda kind: original if kind == "face" else None
        )

        self.assertIs(
            window._source_face_reference_for_pick(7),
            original,
        )

    def test_stored_external_sketch_points_are_always_visible(self) -> None:
        reference = {"id": "external-point"}
        visible = ZimaOpenGLViewer._external_point_marker_visible
        self.assertTrue(visible(reference, None, True))
        self.assertTrue(visible(reference, "external-point", True))
        self.assertTrue(visible({**reference, "selected": True}, None, True))
        self.assertTrue(visible(reference, None, False))

    def test_solid_vertices_are_selectable_only_in_topology_mode(self) -> None:
        class ViewerState:
            _sketch_reference_selection_mode = False
            _interaction_mode = "object"

        state = ViewerState()
        selectable = ZimaOpenGLViewer._point_marker_is_selectable
        self.assertFalse(selectable(state, "vertex"))
        state._interaction_mode = "topology"
        self.assertTrue(selectable(state, "vertex"))
        state._interaction_mode = "object"
        self.assertTrue(selectable(state, "datum_point"))

    def test_solid_vertex_pick_maps_back_to_occt_vertex(self) -> None:
        shape = BRepPrimAPI_MakeBox(20.0, 30.0, 40.0).Shape()
        mesh = triangulate_shape(shape, owner_id="result")
        self.assertEqual(len(mesh.points), 8)
        for marker in mesh.points:
            self.assertIsNotNone(topology_subshape(
                shape,
                element_kind="point",
                element_index=marker.point_index,
            ))

    def test_stable_vertex_reference_constrains_point_and_reports_loss(self) -> None:
        document = create_empty_part()
        container = document.create_container("Box", ContainerType.BOX)
        box = document.create_primitive(container.entity_id, EntityKind.BOX)
        registry = active_face_registry(document)
        reference = registry.vertex_references[0]
        descriptor = {
            "type": "vertex",
            "entity_id": document.root.entity_id,
            "reference_scope": "history_result",
            "history_cursor": 1,
            "point_ref": reference.to_dict(),
            "equations": [],
        }

        class Harness:
            def __init__(self, part):
                self.document = part

            def _shape_for_reference_descriptor(self, _descriptor, _reference):
                return self.document.build_shape_at(1)

            def _resolved_shape_reference_equations(self, item):
                return MainWindow._resolved_shape_reference_equations(self, item)

        harness = Harness(document)
        solution, dof, _status, _constrained = (
            MainWindow._solve_point_constraints(
                harness, [descriptor], (9.0, 9.0, 9.0)
            )
        )
        self.assertEqual(dof, 0)
        self.assertIsNotNone(solution)

        box.suppressed = True
        solution, dof, _status, _constrained = (
            MainWindow._solve_point_constraints(
                harness, [descriptor], (9.0, 9.0, 9.0)
            )
        )
        self.assertEqual(dof, 3)
        self.assertEqual(solution, (9.0, 9.0, 9.0))
        self.assertEqual(descriptor["resolution_state"], "missing")

    def test_model_display_modes_use_distinct_surface_passes(self) -> None:
        self.assertEqual(_surface_pass_for_display_mode("wire"), "none")
        self.assertEqual(_surface_pass_for_display_mode("hidden_edges"), "depth")
        self.assertEqual(_surface_pass_for_display_mode("no_hidden"), "depth")
        self.assertEqual(
            _surface_pass_for_display_mode("shaded_with_edges"), "color"
        )
        self.assertEqual(_surface_pass_for_display_mode("shaded"), "color")

    def test_drawing_styles_map_to_shared_renderer_modes(self) -> None:
        mode_for = DrawingCanvas._renderer_display_mode
        self.assertEqual(mode_for({"display_style": "wireframe"}), "wire")
        self.assertEqual(
            mode_for({"display_style": "hidden_line"}), "hidden_edges"
        )
        self.assertEqual(
            mode_for({"display_style": "no_hidden"}), "no_hidden"
        )
        self.assertEqual(
            mode_for({"display_style": "shaded_edges"}),
            "shaded_with_edges",
        )
        self.assertEqual(mode_for({"display_style": "shaded"}), "shaded")

    def test_family_table_data_keeps_instances_and_optional_columns(self) -> None:
        document = create_empty_part()
        document.document_settings["family_table"] = json.dumps({
            "columns": ["LENGTH", "MATERIAL"],
            "instances": [{
                "name": "LONG",
                "values": {"LENGTH": "100", "MATERIAL": "STEEL"},
            }],
        })
        self.assertEqual(
            FamilyTableDialog.document_data(document, "generic.prtz"),
            {
                "columns": ["LENGTH", "MATERIAL"],
                "instances": [{
                    "name": "LONG",
                    "values": {
                        "LENGTH": "100",
                        "MATERIAL": "STEEL",
                    },
                }],
                "generic_name": "generic.prtz",
            },
        )

    def test_sketch_mirror_reflects_point_across_shifted_axis(self) -> None:
        self.assertEqual(
            MainWindow._mirrored_sketch_position(
                (5.0, 3.0),
                (2.0, 0.0),
                (0.0, 1.0),
            ),
            (-1.0, 3.0),
        )

    def test_working_directory_cleanup_only_collects_cad_versions(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in (
                "part.prtz",
                "part.prtz.1",
                "part.prtz.3",
                "machine.asmz.2",
                "sheet.drwz.4",
                "notes.txt.1",
                "part.prtz.tmp",
            ):
                (root / name).write_text(name, encoding="utf-8")

            groups = MainWindow._working_directory_archive_groups(root)

            self.assertEqual(
                {path.name for path in groups},
                {"part.prtz", "machine.asmz", "sheet.drwz"},
            )
            self.assertEqual(
                [path.name for path in groups[root / "part.prtz"]],
                ["part.prtz.1", "part.prtz.3"],
            )

    def test_curved_external_sketch_edge_remains_bounded(self) -> None:
        geometry = MainWindow._infinite_sketch_reference_geometry({
            "type": "polyline",
            "points": [[1.0, 0.0], [0.7, 0.7], [0.0, 1.0]],
        })

        self.assertIsNotNone(geometry)
        self.assertEqual(geometry["type"], "polyline")
        segment = MainWindow._sketch_reference_constraint_line(
            geometry,
            {},
            (0.7, 0.7),
        )
        self.assertIsNotNone(segment)
        self.assertTrue(segment["bounded"])

    def test_straight_external_sketch_edge_remains_infinite(self) -> None:
        geometry = MainWindow._infinite_sketch_reference_geometry({
            "type": "polyline",
            "points": [[0.0, 0.0], [1.0, 0.0], [2.0, 0.0]],
        })

        self.assertIsNotNone(geometry)
        self.assertEqual(geometry["type"], "line")

    def test_model_edge_reference_remains_a_finite_segment(self) -> None:
        projected_edge = {
            "type": "polyline",
            "points": [[0.0, 0.0], [2.0, 0.0]],
        }

        edge = MainWindow._normalized_sketch_reference_geometry(
            "edge", projected_edge
        )
        plane = MainWindow._normalized_sketch_reference_geometry(
            "plane", projected_edge
        )

        self.assertEqual(edge, projected_edge)
        self.assertEqual(plane["type"], "line")

    def test_axis_crossing_sketch_is_kept_as_axis_point(self) -> None:
        geometry = MainWindow._infinite_sketch_reference_geometry({
            "type": "axis_point",
            "point": [4.0, -3.0],
        })

        self.assertEqual(geometry, {
            "type": "axis_point",
            "point": [4.0, -3.0],
        })

    def test_orientation_references_leave_rotation_offsets_editable(self) -> None:
        dof_for = AxisConstraintDialog._rotation_degrees_of_freedom

        self.assertEqual(dof_for([]), 3)
        self.assertEqual(dof_for([{"orientation_role": "normal"}]), 1)
        self.assertEqual(dof_for([
            {"orientation_role": "normal"},
            {"orientation_role": "up"},
        ]), 0)
        editable_for = AxisConstraintDialog._editable_rotation_axes
        self.assertEqual(editable_for([]), {"x", "y", "z"})
        self.assertEqual(
            editable_for([{"orientation_role": "normal"}]),
            {"x", "y", "z"},
        )
        self.assertEqual(editable_for([
            {"orientation_role": "normal"},
            {"orientation_role": "up"},
        ]), {"x", "y", "z"})
        drives = AxisConstraintDialog._reference_drives_rotation_kind
        legacy_plane = {
            "type": "entity",
            "orientation_role": "normal",
        }
        axis = {
            "type": "entity",
            "orientation_role": "normal",
        }
        self.assertFalse(drives(legacy_plane, EntityKind.PLANE))
        self.assertTrue(drives(axis, EntityKind.AXIS))
        self.assertTrue(drives(
            {**legacy_plane, "orientation_drives_rotation": True},
            EntityKind.PLANE,
        ))

    def test_parallel_direction_does_not_complete_container_frame(self) -> None:
        class OrientationHarness:
            _normalized_vector = staticmethod(MainWindow._normalized_vector)

            @staticmethod
            def _orientation_reference_vector(
                descriptor,
                *,
                allow_frame_fallback,
                resolve_shape_references=False,
            ):
                return tuple(descriptor["direction"])

        independent = MainWindow._orientation_references_are_independent
        harness = OrientationHarness()
        primary = [{"direction": (1.0, 0.0, 0.0)}]

        self.assertFalse(independent(
            harness,
            primary,
            {"direction": (2.0, 0.0, 0.0)},
        ))
        self.assertFalse(independent(
            harness,
            primary,
            {"direction": (-1.0, 0.0, 0.0)},
        ))
        self.assertTrue(independent(
            harness,
            primary,
            {"direction": (0.0, 1.0, 0.0)},
        ))

    def test_edge_direction_falls_back_to_constraint_equations(self) -> None:
        class EdgeHarness:
            document = None
            _normalized_vector = staticmethod(MainWindow._normalized_vector)
            _cross_product = staticmethod(MainWindow._cross_product)

        direction = MainWindow._orientation_reference_vector(
            EdgeHarness(),
            {
                "type": "edge",
                "topology_key": "semantic:edge:key",
                "equations": [
                    [0.0, 1.0, 0.0, 2.0],
                    [0.0, 0.0, 1.0, 3.0],
                ],
            },
            allow_frame_fallback=False,
        )

        self.assertEqual(direction, (1.0, 0.0, 0.0))

    def test_inclined_sketch_view_uses_complete_local_frame(self) -> None:
        root_half = 2.0 ** -0.5
        base_x = (root_half, root_half, 0.0)
        base_y = (-0.5, 0.5, root_half)
        twist = radians(30.0)
        x_axis = tuple(
            cos(twist) * base_x[index]
            + sin(twist) * base_y[index]
            for index in range(3)
        )
        y_axis = tuple(
            -sin(twist) * base_x[index]
            + cos(twist) * base_y[index]
            for index in range(3)
        )
        direction, roll = MainWindow._sketch_view_orientation((
            (10.0, 20.0, 30.0),
            x_axis,
            y_axis,
        ))

        expected_normal = MainWindow._normalized_vector(
            MainWindow._cross_product(x_axis, y_axis)
        )
        for actual, expected in zip(direction, expected_normal):
            self.assertAlmostEqual(actual, -expected)
        self.assertNotAlmostEqual(roll, 0.0)

    def test_inclined_view_direction_maps_to_camera_depth(self) -> None:
        direction = MainWindow._normalized_vector((1.0, -2.0, 3.0))
        yaw, pitch = camera_angles_for_view_direction(direction)
        rotation = _camera_rotation_matrix(yaw, pitch, 0.0)
        camera_direction = tuple(
            sum(rotation[row][column] * direction[column]
                for column in range(3))
            for row in range(3)
        )

        self.assertAlmostEqual(camera_direction[0], 0.0)
        self.assertAlmostEqual(camera_direction[1], 0.0)
        self.assertAlmostEqual(camera_direction[2], -1.0)

    def test_live_bounds_change_preserves_camera_projection(self) -> None:
        camera = CameraState(
            yaw_degrees=37.0,
            pitch_degrees=-52.0,
            roll_degrees=11.0,
            pan_x=23.0,
            pan_y=-17.0,
            zoom=1.7,
        )
        previous_center = (1.0, 2.0, 3.0)
        previous_radius = 12.0
        new_center = (8.0, -4.0, 9.0)
        new_radius = 31.0
        point = (5.0, 7.0, -2.0)
        viewport_height = 600.0

        def projected(center, radius):
            rotation = _camera_rotation_matrix(
                camera.yaw_degrees,
                camera.pitch_degrees,
                camera.roll_degrees,
            )
            relative = tuple(
                point[axis] - center[axis] for axis in range(3)
            )
            rotated = tuple(
                sum(rotation[row][column] * relative[column]
                    for column in range(3))
                for row in range(3)
            )
            scale = viewport_height * 0.5 / radius * camera.zoom
            return (
                camera.pan_x + rotated[0] * scale,
                camera.pan_y - rotated[1] * scale,
                scale,
            )

        before = projected(previous_center, previous_radius)
        preserve_camera_for_scene_bounds(
            camera,
            previous_center,
            previous_radius,
            new_center,
            new_radius,
            viewport_height,
        )
        after = projected(new_center, new_radius)

        for actual, expected in zip(after, before):
            self.assertAlmostEqual(actual, expected)

    def test_three_independent_assembly_mates_lock_full_transform(self) -> None:
        rows = [
            {"target": "x", "orientation": True},
            {"target": "y", "orientation": True},
            {"target": "z", "orientation": False},
        ]
        normals = {
            "x": (1.0, 0.0, 0.0),
            "y": (0.0, 1.0, 0.0),
            "z": (0.0, 0.0, 1.0),
        }

        self.assertEqual(
            AssemblyComponentPropertiesDialog._mate_transform_locks(
                rows,
                normals,
            ),
            (True, True),
        )

    def test_partial_assembly_mates_leave_transform_editable(self) -> None:
        rows = [{"target": "z", "orientation": True}]

        self.assertEqual(
            AssemblyComponentPropertiesDialog._mate_transform_locks(
                rows,
                {"z": (0.0, 0.0, 1.0)},
            ),
            (False, False),
        )

    def test_unresolved_assembly_mate_rows_are_retained(self) -> None:
        unresolved = {
            "source": "assembly-face-ref:source",
            "target": "assembly-face-ref:target",
            "type": "plane",
        }
        self.assertEqual(
            AssemblyComponentPropertiesDialog._retained_mate_rows([
                unresolved,
                {"source": "incomplete"},
                "invalid",
            ]),
            [unresolved],
        )

    def test_assembly_edge_descriptor_is_an_axis_mate_reference(self) -> None:
        descriptor = "assembly-edge-ref:stable-edge"
        self.assertTrue(
            AssemblyComponentPropertiesDialog._is_axis_reference(descriptor)
        )
        self.assertTrue(
            AssemblyComponentPropertiesDialog._reference_matches_mate_type(
                descriptor, "axis"
            )
        )
        self.assertFalse(
            AssemblyComponentPropertiesDialog._reference_matches_mate_type(
                descriptor, "plane"
            )
        )

    def test_shaded_projection_contains_model_surface_triangles(self) -> None:
        mesh = triangulate_shape(
            BRepPrimAPI_MakeCylinder(10.0, 20.0).Shape()
        )

        triangles = shaded_projection(
            [(mesh, "#B9C2CC")],
            "isometric",
        )

        self.assertEqual(len(triangles), mesh.triangle_count)
        self.assertTrue(all(len(item["points"]) == 3 for item in triangles))
        self.assertTrue(all(item["color"] == "#B9C2CC" for item in triangles))
        self.assertTrue(all(
            len(item["vertex_brightness"]) == 3 for item in triangles
        ))
        self.assertTrue(all(
            len(item["vertex_depths"]) == 3 for item in triangles
        ))
        self.assertTrue(all(
            0.42 <= brightness <= 1.0
            for item in triangles
            for brightness in item["vertex_brightness"]
        ))

        colored = shaded_projection(
            [(mesh, {"": "#C04020"})],
            "isometric",
        )
        self.assertTrue(all(item["color"] == "#C04020" for item in colored))

    def test_model_edges_are_classified_by_topology(self) -> None:
        box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape()
        self.assertEqual(
            {edge.topology_role for edge in triangulate_shape(box).edges},
            {"sharp"},
        )

        face_explorer = TopExp_Explorer(box, TopAbs_FACE)
        face_mesh = triangulate_shape(face_explorer.Current())
        self.assertEqual(
            {edge.topology_role for edge in face_mesh.edges},
            {"boundary"},
        )

        cylinder = BRepPrimAPI_MakeCylinder(10.0, 20.0).Shape()
        cylinder_roles = {
            edge.topology_role for edge in triangulate_shape(cylinder).edges
        }
        self.assertIn("seam", cylinder_roles)
        self.assertIn("sharp", cylinder_roles)

    def test_edge_visibility_matches_display_mode(self) -> None:
        def edge(role: str) -> EdgePolyline:
            return EdgePolyline(
                edge_index=1,
                points=((0.0, 0.0, 0.0), (1.0, 0.0, 0.0)),
                topology_role=role,
            )

        self.assertTrue(edge_visible_in_display(edge("boundary"), "wire"))
        self.assertTrue(edge_visible_in_display(edge("tangent"), "wire"))
        self.assertFalse(edge_visible_in_display(edge("seam"), "wire"))
        self.assertFalse(
            edge_visible_in_display(edge("periodic_tangent"), "wire")
        )
        self.assertFalse(
            edge_visible_in_display(edge("tangent"), "shaded_with_edges")
        )
        self.assertTrue(
            edge_visible_in_display(edge("sharp"), "shaded_with_edges")
        )
        self.assertFalse(edge_visible_in_display(edge("sharp"), "shaded"))

    def test_exact_tangent_facet_remains_a_silhouette(self) -> None:
        tangent = SilhouetteEdge(
            first=(0.0, 0.0, 0.0),
            second=(1.0, 0.0, 0.0),
            adjacent_normals=((1.0, 0.0, 0.0), (0.0, 1.0, 0.0)),
        )
        self.assertEqual(
            silhouette_segments_from_edges((tangent,), (0.0, 1.0, 0.0)),
            ((tangent.first, tangent.second),),
        )

    def test_curved_surface_produces_view_dependent_silhouette(self) -> None:
        cylinder = triangulate_shape(
            BRepPrimAPI_MakeCylinder(10.0, 20.0).Shape()
        )

        self.assertTrue(any(
            len({
                tuple(cylinder.triangle_normals[
                    offset + vertex * 3:offset + vertex * 3 + 3
                ])
                for vertex in range(3)
            }) > 1
            for offset in range(0, len(cylinder.triangle_normals), 9)
        ))

        self.assertGreater(
            len(silhouette_segments(cylinder, (1.0, 0.0, 0.0))),
            0,
        )
        self.assertEqual(
            silhouette_segments(cylinder, (0.0, 0.0, 1.0)),
            (),
        )
        oblique_segments = silhouette_segments(
            cylinder,
            (1.0, 0.7, 0.3),
        )
        self.assertEqual(len(oblique_segments), 2)
        self.assertTrue(all(
            abs(first[2] - second[2]) > 19.0
            for first, second in oblique_segments
        ))

    def test_topological_edges_are_not_duplicated_as_silhouettes(self) -> None:
        box = triangulate_shape(
            BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape()
        )

        self.assertEqual(
            silhouette_segments(box, (1.0, 1.0, 1.0)),
            (),
        )

    def test_workspace_pen_is_one_pixel_and_cosmetic(self) -> None:
        pen = cosmetic_pen(QColor("#808080"))
        self.assertTrue(pen.isCosmetic())
        self.assertEqual(pen.widthF(), 1.0)

    def test_parallel_edge_dimension_uses_placement_coordinate(self) -> None:
        geometry = parallel_dimension_geometry(
            ((0.0, 0.0), (0.0, 10.0)),
            ((10.0, 0.0), (10.0, 10.0)),
            (4.0, 20.0),
        )
        self.assertIsNotNone(geometry)
        self.assertAlmostEqual(geometry["distance"], 10.0)
        self.assertEqual(geometry["first_point"], (0.0, 20.0))
        self.assertEqual(geometry["second_point"], (10.0, 20.0))

    def test_non_parallel_edges_cannot_form_basic_dimension(self) -> None:
        self.assertIsNone(parallel_dimension_geometry(
            ((0.0, 0.0), (10.0, 0.0)),
            ((0.0, 0.0), (0.0, 10.0)),
            (20.0, 20.0),
        ))

    def test_legacy_sheet_without_scale_defaults_to_one_to_one(self) -> None:
        document = create_empty_drawing()
        document.document_settings["drawing_sheets"] = json.dumps([
            {
                "id": "legacy-sheet",
                "name": "List 1",
                "format": "A4",
                "views": [],
            }
        ])

        sheet = drawing_sheets(document)[0]

        self.assertEqual(sheet["default_scale_numerator"], 1.0)
        self.assertEqual(sheet["default_scale"], 1.0)

    def test_deleting_drawing_view_removes_children_and_dimensions(self) -> None:
        sheet = {
            "views": [
                {"id": "parent"},
                {"id": "child", "parent_view_id": "parent"},
                {"id": "grandchild", "parent_view_id": "child"},
                {"id": "unrelated"},
            ],
            "dimensions": [
                {
                    "id": "dependent",
                    "first": {"view_id": "child"},
                    "second": {"view_id": "child"},
                },
                {
                    "id": "kept",
                    "first": {"view_id": "unrelated"},
                    "second": {"view_id": "unrelated"},
                },
            ],
        }

        removed = delete_drawing_view(sheet, "parent")

        self.assertEqual(removed, {"parent", "child", "grandchild"})
        self.assertEqual([view["id"] for view in sheet["views"]], ["unrelated"])
        self.assertEqual(
            [dimension["id"] for dimension in sheet["dimensions"]],
            ["kept"],
        )

    def test_moving_drawing_view_updates_position_and_bounds(self) -> None:
        sheet = {
            "views": [{
                "id": "view",
                "x": 10.0,
                "y": 20.0,
                "scale": 2.0,
                "model_extent": [2.0, 4.0],
                "caption_position": [12.0, 30.0],
            }],
            "dimensions": [{
                "first": {"view_id": "view"},
                "second": {"view_id": "view"},
                "placement": [15.0, 25.0],
            }],
        }

        self.assertTrue(move_drawing_view(sheet, "view", 50.0, 60.0))

        view = sheet["views"][0]
        self.assertEqual((view["x"], view["y"]), (50.0, 60.0))
        self.assertEqual(
            view["bounds"],
            {"left": 47.0, "right": 53.0, "bottom": 55.0, "top": 65.0},
        )
        self.assertEqual(view["caption_position"], [52.0, 70.0])
        self.assertEqual(
            sheet["dimensions"][0]["placement"], [55.0, 65.0]
        )

    def test_moving_parent_view_carries_projected_descendants(self) -> None:
        sheet = {
            "views": [
                {"id": "parent", "x": 10.0, "y": 20.0},
                {
                    "id": "child",
                    "parent_view_id": "parent",
                    "projection_direction": "right",
                    "x": 40.0,
                    "y": 20.0,
                },
                {
                    "id": "grandchild",
                    "parent_view_id": "child",
                    "projection_direction": "top",
                    "x": 40.0,
                    "y": 50.0,
                },
                {"id": "unrelated", "x": 5.0, "y": 5.0},
            ],
            "dimensions": [],
        }

        self.assertTrue(move_drawing_view(sheet, "parent", 20.0, 35.0))

        positions = {
            view["id"]: (view["x"], view["y"])
            for view in sheet["views"]
        }
        self.assertEqual(positions["parent"], (20.0, 35.0))
        self.assertEqual(positions["child"], (50.0, 35.0))
        self.assertEqual(positions["grandchild"], (50.0, 65.0))
        self.assertEqual(positions["unrelated"], (5.0, 5.0))

    def test_moving_projected_child_uses_only_its_free_axis(self) -> None:
        sheet = {
            "views": [
                {"id": "parent", "x": 10.0, "y": 20.0},
                {
                    "id": "child",
                    "parent_view_id": "parent",
                    "projection_direction": "right",
                    "x": 40.0,
                    "y": 20.0,
                },
                {
                    "id": "grandchild",
                    "parent_view_id": "child",
                    "projection_direction": "top",
                    "x": 40.0,
                    "y": 50.0,
                },
            ],
            "dimensions": [],
        }

        self.assertTrue(move_drawing_view(sheet, "child", 70.0, 90.0))

        positions = {
            view["id"]: (view["x"], view["y"])
            for view in sheet["views"]
        }
        self.assertEqual(positions["parent"], (10.0, 20.0))
        self.assertEqual(positions["child"], (70.0, 20.0))
        self.assertEqual(positions["grandchild"], (70.0, 50.0))

    def test_moving_diagonal_projection_stays_on_its_45_degree_ray(self) -> None:
        sheet = {
            "views": [
                {"id": "parent", "x": 50.0, "y": 50.0},
                {
                    "id": "child",
                    "parent_view_id": "parent",
                    "projection_direction": "top_right",
                    "x": 30.0,
                    "y": 70.0,
                },
            ],
            "dimensions": [],
        }

        self.assertTrue(move_drawing_view(sheet, "child", 10.0, 80.0))

        child = sheet["views"][1]
        self.assertAlmostEqual(50.0 - child["x"], child["y"] - 50.0)

    @staticmethod
    def projected_axis(
        orientation: str, endpoint: tuple[float, float, float]
    ) -> tuple[float, float]:
        line = project_polylines([[(0.0, 0.0, 0.0), endpoint]], orientation)[0]
        return line[1][0] - line[0][0], line[1][1] - line[0][1]

    def test_front_has_positive_x_left_and_positive_z_up(self) -> None:
        self.assertLess(self.projected_axis("front", (1.0, 0.0, 0.0))[0], 0.0)
        self.assertGreater(self.projected_axis("front", (0.0, 0.0, 1.0))[1], 0.0)

    def test_top_has_positive_x_left_and_positive_y_down(self) -> None:
        self.assertLess(self.projected_axis("top", (1.0, 0.0, 0.0))[0], 0.0)
        self.assertLess(self.projected_axis("top", (0.0, 1.0, 0.0))[1], 0.0)

    def test_right_has_positive_y_left_and_positive_z_up(self) -> None:
        self.assertLess(self.projected_axis("right", (0.0, 1.0, 0.0))[0], 0.0)
        self.assertGreater(self.projected_axis("right", (0.0, 0.0, 1.0))[1], 0.0)

    def test_default_camera_uses_reversed_isometric_side(self) -> None:
        self.assertEqual(
            (CameraState().yaw_degrees, CameraState().pitch_degrees),
            STANDARD_VIEW_ORIENTATIONS["default"],
        )

    def test_standard_orthographic_camera_convention(self) -> None:
        self.assertEqual(STANDARD_VIEW_ORIENTATIONS["front"], (180.0, -90.0))
        self.assertEqual(STANDARD_VIEW_ORIENTATIONS["back"], (0.0, -90.0))
        self.assertEqual(STANDARD_VIEW_ORIENTATIONS["top"], (180.0, 0.0))
        self.assertEqual(STANDARD_VIEW_ORIENTATIONS["bottom"], (180.0, 180.0))

    def test_projected_view_uses_sheet_projection_method(self) -> None:
        expectations = {
            ("third_angle", "right"): "right",
            ("third_angle", "left"): "left",
            ("third_angle", "top"): "top",
            ("third_angle", "bottom"): "bottom",
            ("first_angle", "right"): "left",
            ("first_angle", "left"): "right",
            ("first_angle", "top"): "bottom",
            ("first_angle", "bottom"): "top",
        }
        for (method, direction), expected_name in expectations.items():
            actual = projection_axes(projected_view_orientation(
                "front", direction, method
            ))
            expected = projection_axes(expected_name)
            for actual_axis, expected_axis in zip(actual, expected):
                for value, expected_value in zip(actual_axis, expected_axis):
                    self.assertAlmostEqual(value, expected_value, places=6)

    def test_projected_view_rotates_relative_to_parent(self) -> None:
        parent = projection_axes("isometric")
        actual = projection_axes(projected_view_orientation(
            "isometric", "right", "third_angle"
        ))
        expected = (
            tuple(-value for value in parent[2]),
            parent[1],
            parent[0],
        )
        for actual_axis, expected_axis in zip(actual, expected):
            for value, expected_value in zip(actual_axis, expected_axis):
                self.assertAlmostEqual(value, expected_value, places=6)

    def test_diagonal_projected_view_folds_parent_by_ninety_degrees(self) -> None:
        parent = projection_axes("front")
        actual = projection_axes(projected_view_orientation(
            "front", "top_right", "third_angle"
        ))
        diagonal = 2.0 ** -0.5
        expected_depth = tuple(
            diagonal * parent[0][axis] + diagonal * parent[1][axis]
            for axis in range(3)
        )
        for value, expected in zip(actual[2], expected_depth):
            self.assertAlmostEqual(value, expected, places=6)
        for axis in actual:
            self.assertAlmostEqual(
                sum(value * value for value in axis), 1.0, places=6
            )
        self.assertAlmostEqual(
            sum(actual[0][i] * actual[1][i] for i in range(3)),
            0.0,
            places=6,
        )

    def test_orbit_camera_remains_continuous_when_upside_down(self) -> None:
        camera = CameraState(
            yaw_degrees=25.0,
            pitch_degrees=180.0,
            roll_degrees=0.0,
        )
        before = _camera_rotation_matrix(
            camera.yaw_degrees,
            camera.pitch_degrees,
            camera.roll_degrees,
        )
        orbit_camera_state(camera, 5.0, 0.0)
        after = _camera_rotation_matrix(
            camera.yaw_degrees,
            camera.pitch_degrees,
            camera.roll_degrees,
        )

        # A horizontal drag is always a rotation around the screen Y axis,
        # even after the camera has crossed 90° and is upside down.
        angle = radians(5.0)
        expected = _multiply_rotation_matrices(
            (
                (cos(angle), 0.0, sin(angle)),
                (0.0, 1.0, 0.0),
                (-sin(angle), 0.0, cos(angle)),
            ),
            before,
        )
        for actual_row, expected_row in zip(after, expected):
            for actual, expected_value in zip(actual_row, expected_row):
                self.assertAlmostEqual(actual, expected_value, places=6)

    def test_orbit_camera_preserves_rotation_through_euler_conversion(self) -> None:
        camera = CameraState(
            yaw_degrees=215.264,
            pitch_degrees=-89.9,
            roll_degrees=0.0,
        )
        for _step in range(20):
            orbit_camera_state(camera, 3.0, 2.0)
            matrix = _camera_rotation_matrix(
                camera.yaw_degrees,
                camera.pitch_degrees,
                camera.roll_degrees,
            )
            for row in matrix:
                self.assertAlmostEqual(
                    sum(value * value for value in row), 1.0, places=6
                )

    def test_view_bounds_follow_position_and_scale(self) -> None:
        view = {
            "x": 100.0,
            "y": 80.0,
            "scale": 2.0,
            "model_extent": [10.0, 6.0],
        }
        self.assertEqual(
            update_view_bounds(view),
            {"left": 89.0, "right": 111.0, "bottom": 73.0, "top": 87.0},
        )

    def test_drawing_scale_caption_uses_ratio_notation(self) -> None:
        self.assertEqual(drawing_scale_text(1.0), "M1:1")
        self.assertEqual(drawing_scale_text(0.5), "M1:2")
        self.assertEqual(drawing_scale_text(2.0), "M2:1")

    def test_named_camera_orientation_projects_with_roll(self) -> None:
        projected = project_polylines(
            [[(0.0, 0.0, 0.0), (1.0, 0.0, 0.0)]],
            {"yaw_degrees": 0.0, "pitch_degrees": 0.0, "roll_degrees": 90.0},
        )[0]
        self.assertAlmostEqual(projected[1][0] - projected[0][0], 0.0, places=6)
        self.assertGreater(projected[1][1] - projected[0][1], 0.0)

    def test_technical_projection_classifies_box_edges(self) -> None:
        shape = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape()
        wire_polylines = [
            list(edge.points) for edge in triangulate_shape(shape).edges
        ]

        projection = technical_projection([shape], wire_polylines, "front")

        self.assertTrue(projection["wireframe_polylines"])
        self.assertEqual(len(projection["polylines"]), 1)
        for first, last in zip(
            projection["polylines"][0][0],
            projection["polylines"][0][-1],
        ):
            self.assertAlmostEqual(first, last)
        # Rear box edges coincide exactly with the visible front outline and
        # must not leave a dashed copy underneath it.
        self.assertFalse(projection["hidden_polylines"])

    def test_drawing_wireframe_uses_filtered_cylinder_edges_and_outlines(
        self,
    ) -> None:
        shape = BRepPrimAPI_MakeCylinder(10.0, 20.0).Shape()
        mesh = triangulate_shape(shape)
        wire_polylines = [
            list(edge.points)
            for edge in mesh.edges
            if edge_visible_in_display(edge, "wire")
        ]

        projection = technical_projection(
            [shape],
            wire_polylines,
            "isometric",
        )

        # Two circular topology edges plus view-dependent silhouette outlines.
        self.assertGreater(len(projection["wireframe_polylines"]), 2)
        self.assertGreaterEqual(
            sum(len(line) for line in projection["wireframe_polylines"]),
            sum(len(line) for line in wire_polylines),
        )
        self.assertTrue(projection["polylines"])
        self.assertTrue(projection["hidden_polylines"])
        self.assertLessEqual(len(projection["polylines"]), 5)
        self.assertLessEqual(len(projection["hidden_polylines"]), 2)

    def test_cylinder_edges_share_surface_triangulation_nodes(self) -> None:
        mesh = triangulate_shape(BRepPrimAPI_MakeCylinder(10.0, 20.0).Shape())
        surface_nodes = {
            tuple(round(mesh.triangle_positions[offset + axis], 7)
                  for axis in range(3))
            for offset in range(0, len(mesh.triangle_positions), 3)
        }
        circular_edges = [
            edge for edge in mesh.edges
            if len(edge.points) > 2 and edge.topology_role == "sharp"
        ]

        self.assertEqual(len(circular_edges), 2)
        for edge in circular_edges:
            self.assertTrue(all(
                tuple(round(value, 7) for value in point) in surface_nodes
                for point in edge.points
            ))

    def test_new_direct_sketch_defaults_to_front_xz_plane(self) -> None:
        document = create_empty_part()
        owner = document.create_container("Sketch", ContainerType.SKETCH)
        sketch = document.create_sketch(owner.entity_id)
        self.assertIsNotNone(sketch)
        self.assertEqual(sketch.parameters["plane"], "xz")

    def test_plane_normal_selects_nearest_global_local_plane(self) -> None:
        plane_for = MainWindow._local_plane_for_normal
        self.assertEqual(
            plane_for((1.0, 0.0, 0.0), "xz"),
            ("yz", "left"),
        )
        self.assertEqual(
            plane_for((0.0, -1.0, 0.0), "yz"),
            ("xz", "normal"),
        )
        self.assertEqual(
            plane_for((0.0, 0.0, -1.0), "yz"),
            ("xy", "up"),
        )

    def test_xy_front_and_xz_up_keep_distinct_datum_axes(self) -> None:
        references = [
            {"entity_id": "xy", "orientation_role": "normal"},
            {"entity_id": "xz", "orientation_role": "up"},
        ]

        remapped = MainWindow._datum_frame_references(
            references, primary_index=0, normal_role="up"
        )

        self.assertEqual(remapped[0]["orientation_role"], "up")
        self.assertEqual(remapped[1]["orientation_role"], "normal")
        self.assertEqual(references[0]["orientation_role"], "normal")
        self.assertEqual(references[1]["orientation_role"], "up")

    def test_xy_back_and_xz_down_preserve_secondary_sign(self) -> None:
        references = [
            {"entity_id": "xy", "orientation_role": "opposite_normal"},
            {"entity_id": "xz", "orientation_role": "down"},
        ]

        remapped = MainWindow._datum_frame_references(
            references, primary_index=0, normal_role="up"
        )

        self.assertEqual(remapped[0]["orientation_role"], "down")
        self.assertEqual(
            remapped[1]["orientation_role"], "opposite_normal"
        )

    def test_xy_protrusion_dimension_follows_global_z(self) -> None:
        extrusion, offset = MainWindow._protrusion_dimension_axes("xy")
        self.assertEqual(extrusion, (0.0, 0.0, 1.0))
        self.assertEqual(offset, (1.0, 0.0, 0.0))

    def test_equal_circle_group_copies_driver_radius(self) -> None:
        entities = [
            {
                "id": "g1",
                "type": "circle",
                "radius": 10.0,
                "equal_radius_group": "equal-circle:g1",
            },
            {
                "id": "g2",
                "type": "circle",
                "radius": 12.5,
                "equal_radius_group": "equal-circle:g1",
                "equal_radius_reference": True,
            },
        ]

        MainWindow._apply_sketch_equal_circle_radii(entities)

        self.assertEqual(entities[0]["radius"], 10.0)
        self.assertEqual(entities[1]["radius"], 10.0)

    def test_distance_seed_moves_only_the_freer_endpoint(self) -> None:
        window = MainWindow.__new__(MainWindow)
        fixed = {
            "type": "point",
            "id": "fixed",
            "x": 0.0,
            "y": 0.0,
            "constraints": [{
                "type": "point_on_reference",
                "reference_id": "sketch_origin",
            }],
        }
        free = {
            "type": "point",
            "id": "free",
            "x": 3.0,
            "y": 4.0,
        }
        dimension = {
            "type": "distance",
            "point_ids": ["fixed", "free"],
            "value": 10.0,
            "driving": True,
        }

        window._apply_sketch_distance_dimensions(
            SimpleNamespace(),
            [fixed, free],
            [dimension],
        )

        self.assertEqual((fixed["x"], fixed["y"]), (0.0, 0.0))
        self.assertAlmostEqual(free["x"], 6.0)
        self.assertAlmostEqual(free["y"], 8.0)

        dimension["point_ids"] = ["free", "fixed"]
        dimension["value"] = 5.0
        window._apply_sketch_distance_dimensions(
            SimpleNamespace(),
            [fixed, free],
            [dimension],
        )

        self.assertEqual((fixed["x"], fixed["y"]), (0.0, 0.0))
        self.assertAlmostEqual(free["x"], 3.0)
        self.assertAlmostEqual(free["y"], 4.0)

        free["dimension_locks"] = ["x", "y"]
        dimension["value"] = 20.0
        window._apply_sketch_distance_dimensions(
            SimpleNamespace(),
            [fixed, free],
            [dimension],
        )

        self.assertEqual((fixed["x"], fixed["y"]), (0.0, 0.0))
        self.assertAlmostEqual(free["x"], 3.0)
        self.assertAlmostEqual(free["y"], 4.0)

    def test_axis_distance_seed_respects_measured_coordinate_lock(self) -> None:
        window = MainWindow.__new__(MainWindow)
        first = {
            "type": "point",
            "id": "first",
            "x": 2.0,
            "y": 3.0,
        }
        second = {
            "type": "point",
            "id": "second",
            "x": 7.0,
            "y": 9.0,
            "dimension_locks": ["x"],
        }

        window._apply_sketch_distance_dimensions(
            SimpleNamespace(),
            [first, second],
            [{
                "type": "distance_x",
                "point_ids": ["first", "second"],
                "value": 12.0,
                "driving": True,
            }],
        )

        self.assertEqual((second["x"], second["y"]), (7.0, 9.0))
        self.assertEqual((first["x"], first["y"]), (-5.0, 3.0))

        second.pop("dimension_locks")
        window._apply_sketch_distance_dimensions(
            SimpleNamespace(),
            [first, second],
            [{
                "type": "distance_x",
                "point_ids": ["first", "second"],
                "value": -4.0,
                "driving": True,
            }],
        )

        self.assertEqual((first["x"], first["y"]), (-5.0, 3.0))
        self.assertEqual((second["x"], second["y"]), (-9.0, 9.0))

    def test_coordinate_dependencies_are_evaluated_per_direction(self) -> None:
        entities = [
            {"type": "point", "id": "p1", "x": 0.0, "y": 0.0},
            {"type": "point", "id": "p2", "x": 5.0, "y": 0.0},
            {
                "type": "segment",
                "id": "g1",
                "point_ids": ["p1", "p2"],
                "constraints": [{"type": "horizontal"}],
            },
        ]

        self.assertFalse(MainWindow._sketch_coordinate_has_dependencies(
            entities, [], "p1", "x"
        ))
        self.assertTrue(MainWindow._sketch_coordinate_has_dependencies(
            entities, [], "p1", "y"
        ))
        self.assertFalse(MainWindow._sketch_coordinate_has_dependencies(
            entities,
            [{
                "type": "distance_y",
                "point_ids": ["p1", "p2"],
            }],
            "p1",
            "x",
        ))
        self.assertTrue(MainWindow._sketch_coordinate_has_dependencies(
            entities,
            [{
                "type": "distance_x",
                "point_ids": ["p1", "p2"],
            }],
            "p1",
            "x",
        ))

    def test_point_line_dimension_placement_slides_along_line(self) -> None:
        first_dimension, second_dimension = (
            MainWindow._point_line_dimension_placement(
                point=(5.0, 10.0),
                projection=(5.0, 0.0),
                line_first=(0.0, 0.0),
                line_second=(10.0, 0.0),
                placement=(8.0, 20.0),
            )
        )

        self.assertEqual(first_dimension, (8.0, 0.0))
        self.assertEqual(second_dimension, (8.0, 10.0))

    def test_xz_sketch_geometry_is_mapped_without_owner_rotation(self) -> None:
        document = create_empty_part()
        owner = document.create_container("Sketch", ContainerType.SKETCH)
        sketch = document.create_sketch(owner.entity_id, plane="xz")
        model = SketchModel.from_editor_data(
            [
                {"type": "point", "id": "p1", "x": 0.0, "y": 0.0},
                {"type": "point", "id": "p2", "x": 10.0, "y": 20.0},
                {
                    "type": "segment",
                    "id": "g1",
                    "point_ids": ["p1", "p2"],
                },
            ],
            [],
        )
        sketch.parameters["sketch_data"] = json.dumps(model.to_dict())
        shape = make_sketch_shape(owner, sketch)
        self.assertIsNotNone(shape)
        bounds = Bnd_Box()
        brepbndlib.Add(shape, bounds)
        xmin, ymin, zmin, xmax, ymax, zmax = bounds.Get()
        self.assertAlmostEqual(xmin, 0.0, places=6)
        self.assertAlmostEqual(xmax, 10.0, places=6)
        self.assertAlmostEqual(ymin, 0.0, places=6)
        self.assertAlmostEqual(ymax, 0.0, places=6)
        self.assertAlmostEqual(zmin, 0.0, places=6)
        self.assertAlmostEqual(zmax, 20.0, places=6)


if __name__ == "__main__":
    unittest.main()
