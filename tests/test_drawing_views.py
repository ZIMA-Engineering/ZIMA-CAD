import unittest
import json

from PySide6.QtGui import QColor
from OCC.Core.Bnd import Bnd_Box
from OCC.Core.BRepBndLib import brepbndlib
from OCC.Core.BRepPrimAPI import BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder
from OCC.Core.TopAbs import TopAbs_FACE
from OCC.Core.TopExp import TopExp_Explorer

from zima_cad.app import (
    AssemblyComponentPropertiesDialog,
    AxisConstraintDialog,
    MainWindow,
)
from zima_cad.drawing import (
    DrawingCanvas,
    cosmetic_pen,
    delete_drawing_view,
    drawing_sheets,
    parallel_dimension_geometry,
    project_polylines,
    move_drawing_view,
    technical_projection,
    shaded_projection,
    update_view_bounds,
)
from zima_cad.model import (
    ContainerType,
    EntityKind,
    create_empty_drawing,
    create_empty_part,
    make_sketch_shape,
)
from zima_cad.sketch_model import SketchModel
from zima_cad.viewer import CameraState, STANDARD_VIEW_ORIENTATIONS
from zima_cad.viewer_mesh import (
    EdgePolyline,
    edge_visible_in_display,
    silhouette_segments,
    triangulate_shape,
)


class DrawingViewConventionTests(unittest.TestCase):
    def test_orientation_references_reduce_rotational_dof(self) -> None:
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
            {"y"},
        )
        self.assertEqual(editable_for([
            {"orientation_role": "normal"},
            {"orientation_role": "up"},
        ]), set())
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

    def test_drawing_wire_style_uses_hidden_line_removed_geometry(self) -> None:
        self.assertEqual(
            DrawingCanvas._view_line_source({"display_style": "wireframe"}),
            "polylines",
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
                "polylines": [[[-1.0, -2.0], [1.0, 2.0]]],
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
            {"left": 48.0, "right": 52.0, "bottom": 56.0, "top": 64.0},
        )
        self.assertEqual(
            sheet["dimensions"][0]["placement"], [55.0, 65.0]
        )

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

    def test_view_bounds_follow_position_and_scale(self) -> None:
        view = {
            "x": 100.0,
            "y": 80.0,
            "scale": 2.0,
            "polylines": [[[-5.0, -3.0], [5.0, 3.0]]],
        }
        self.assertEqual(
            update_view_bounds(view),
            {"left": 90.0, "right": 110.0, "bottom": 74.0, "top": 86.0},
        )

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

        self.assertEqual(len(projection["wireframe_polylines"]), 12)
        self.assertEqual(len(projection["polylines"]), 4)
        self.assertEqual(len(projection["hidden_polylines"]), 4)

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

        self.assertEqual(len(projection["wireframe_polylines"]), 2)
        self.assertEqual(len(projection["polylines"]), 5)

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
