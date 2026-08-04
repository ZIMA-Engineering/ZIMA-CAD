import unittest
import json
import tempfile
from math import cos, radians, sin
from pathlib import Path

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
)
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
    create_empty_drawing,
    create_empty_part,
    make_sketch_shape,
    coordinate_system_transform,
    multiply_transforms,
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
    SilhouetteEdge,
    edge_visible_in_display,
    silhouette_segments,
    silhouette_segments_from_edges,
    triangulate_shape,
    topology_subshape,
)


class DrawingViewConventionTests(unittest.TestCase):
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

    def test_explicit_frame_drives_rotation_instead_of_position_roles(self):
        class RotationHarness:
            _normalized_vector = staticmethod(MainWindow._normalized_vector)
            _cross_product = staticmethod(MainWindow._cross_product)

            @staticmethod
            def _orientation_reference_vector(
                descriptor,
                *,
                allow_frame_fallback,
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

    def test_external_sketch_points_only_appear_on_hover_or_selection(self) -> None:
        reference = {"id": "external-point"}
        visible = ZimaOpenGLViewer._external_point_marker_visible
        self.assertFalse(visible(reference, None, True))
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

        self.assertEqual(remapped[0]["orientation_role"], "up")
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
