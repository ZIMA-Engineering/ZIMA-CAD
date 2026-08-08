import copy
import math
import unittest

from zima_cad.sketch_model import (
    GeometryType,
    SketchConstraint,
    SketchDimension,
    SketchGeometry,
    SketchModel,
    SketchModelError,
    SketchPoint,
    classify_linear_dimension,
)
from zima_cad.sketch_geometry import (
    center_arc_points,
    corner_radius_from_drag,
    ellipse_points,
    elliptical_arc_points,
    evaluate_corner_radius,
)


class SketchModelTests(unittest.TestCase):
    def test_satisfied_constraint_skips_numerical_jacobian(self):
        model = SketchModel.from_editor_data(
            [
                {"id": "p1", "type": "point", "x": 0.0, "y": 2.0},
                {
                    "id": "p2",
                    "type": "point",
                    "x": 5.0,
                    "y": 2.0,
                    "constraints": [{
                        "type": "horizontal",
                        "point_id": "p1",
                    }],
                },
            ],
            [],
        )

        def unexpected_jacobian():
            raise AssertionError("satisfied geometry must not be perturbed")

        model._numerical_jacobian = unexpected_jacobian
        self.assertTrue(model.solve())
        self.assertEqual(model.points["p2"].position(), (5.0, 2.0))

    def test_driven_point_horizontal_constraint_round_trips(self):
        entities = [
            {"id": "reference", "type": "point", "x": 2.0, "y": 3.0},
            {
                "id": "driven",
                "type": "point",
                "x": 8.0,
                "y": 3.0,
                "constraints": [{
                    "type": "horizontal",
                    "point_id": "reference",
                    "relation_role": "driven",
                }],
            },
        ]

        model = SketchModel.from_editor_data(entities, [])
        constraint = next(iter(model.constraints.values()))
        self.assertEqual(constraint.constraint_type, "horizontal")
        self.assertEqual(constraint.point_ids, ("driven", "reference"))
        self.assertEqual(constraint.attributes["relation_role"], "driven")

        restored, _dimensions = model.to_editor_data()
        driven = next(entity for entity in restored if entity["id"] == "driven")
        restored_constraint = driven["constraints"][0]
        self.assertEqual(restored_constraint["type"], "horizontal")
        self.assertEqual(restored_constraint["point_id"], "reference")
        self.assertEqual(restored_constraint["relation_role"], "driven")

    def test_sketch_text_anchor_metadata_round_trips(self):
        entities = [{
            "id": "text-anchor",
            "type": "point",
            "x": 12.0,
            "y": 8.0,
            "text_group": "text:stable",
            "text_role": "anchor",
            "text_value": "RAZÍTKO",
            "text_height": 5.0,
            "text_horizontal": "center",
            "text_vertical": "middle",
            "text_angle": 30.0,
            "text_flip": True,
            "text_color": "green",
            "text_font": "osifont",
        }]

        model = SketchModel.from_editor_data(entities, [])
        restored, _dimensions = model.to_editor_data()

        self.assertEqual(restored, entities)

    def test_locked_circle_keypoint_keeps_quadrant(self):
        sketch = SketchModel()
        sketch.add_point(SketchPoint("center", 2.0, 3.0))
        sketch.add_point(SketchPoint(
            "key", 2.0, 8.0,
            attributes={"curve_attachment": {
                "type": "circle",
                "geometry_id": "circle",
                "angle": math.pi / 2.0,
                "locked": True,
            }},
        ))
        sketch.add_geometry(SketchGeometry(
            "circle", GeometryType.CIRCLE, ("center",),
            attributes={"radius": 5.0},
        ))
        self.assertTrue(all(
            abs(value) < 1.0e-9 for value in sketch._equation_values()
        ))
        sketch.points["key"].x = 3.0
        self.assertTrue(sketch.solve())
        self.assertAlmostEqual(
            sketch.points["key"].x - sketch.points["center"].x,
            0.0,
            places=6,
        )
        self.assertAlmostEqual(
            sketch.points["key"].y - sketch.points["center"].y,
            5.0,
            places=6,
        )

    def test_ellipse_axis_direction_constraints_round_trip_together(self):
        entities = [
            {"id": "c", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "a", "type": "point", "x": 4.0, "y": 0.0},
            {"id": "b", "type": "point", "x": 0.0, "y": 2.0},
            {
                "id": "ellipse",
                "type": "ellipse",
                "point_ids": ["c", "a", "b"],
                "constraints": [
                    {"type": "horizontal", "axis": "major"},
                    {"type": "vertical", "axis": "minor"},
                ],
            },
        ]
        model = SketchModel.from_editor_data(entities, [])
        self.assertEqual(len(model.constraints), 2)
        self.assertTrue(all(
            abs(value) < 1.0e-9 for value in model._equation_values()
        ))
        restored, _dimensions = model.to_editor_data()
        ellipse = next(item for item in restored if item["id"] == "ellipse")
        self.assertEqual(
            [(item["type"], item["axis"]) for item in ellipse["constraints"]],
            [("horizontal", "major"), ("vertical", "minor")],
        )

    def test_circle_circle_tangent_round_trips(self):
        sketch = SketchModel()
        for point in (
            SketchPoint("c1", 0.0, 0.0),
            SketchPoint("c2", 5.0, 0.0),
            SketchPoint("contact", 2.0, 0.0),
        ):
            sketch.add_point(point)
        sketch.add_geometry(SketchGeometry(
            "circle1", GeometryType.CIRCLE, ("c1",),
            attributes={"radius": 2.0},
        ))
        sketch.add_geometry(SketchGeometry(
            "circle2", GeometryType.CIRCLE, ("c2",),
            attributes={"radius": 3.0},
        ))
        sketch.add_constraint(SketchConstraint(
            "tangent", "tangent", ("c1", "c2", "contact"),
            attributes={
                "first_curve_geometry_id": "circle1",
                "second_curve_geometry_id": "circle2",
                "contact_point_id": "contact",
            },
        ))
        self.assertTrue(all(
            abs(value) < 1.0e-9
            for value in sketch.constraint_residuals("tangent")
        ))
        entities, dimensions = sketch.to_editor_data()
        restored = SketchModel.from_editor_data(entities, dimensions)
        self.assertEqual(restored.constraints["tangent"].point_ids, ("c1", "c2", "contact"))

    def test_circle_ellipse_tangent_round_trips(self):
        sketch = SketchModel()
        for point in (
            SketchPoint("circle-center", 0.0, 0.0),
            SketchPoint("ellipse-center", 5.0, 0.0),
            SketchPoint("major", 8.0, 0.0),
            SketchPoint("minor", 5.0, 1.0),
            SketchPoint("contact", 2.0, 0.0),
        ):
            sketch.add_point(point)
        sketch.add_geometry(SketchGeometry(
            "circle", GeometryType.CIRCLE, ("circle-center",),
            attributes={"radius": 2.0},
        ))
        sketch.add_geometry(SketchGeometry(
            "ellipse", GeometryType.ELLIPSE,
            ("ellipse-center", "major", "minor"),
        ))
        sketch.add_constraint(SketchConstraint(
            "tangent", "tangent",
            ("circle-center", "ellipse-center", "major", "minor", "contact"),
            attributes={
                "first_curve_geometry_id": "circle",
                "second_curve_geometry_id": "ellipse",
                "contact_point_id": "contact",
            },
        ))
        self.assertTrue(all(
            abs(value) < 1.0e-9
            for value in sketch.constraint_residuals("tangent")
        ))
        entities, dimensions = sketch.to_editor_data()
        restored = SketchModel.from_editor_data(entities, dimensions)
        self.assertEqual(
            restored.constraints["tangent"].attributes["second_curve_geometry_id"],
            "ellipse",
        )

    def test_circle_ellipse_concentric_round_trips(self):
        sketch = SketchModel()
        for point in (
            SketchPoint("c1", 0.0, 0.0),
            SketchPoint("c2", 0.0, 0.0),
            SketchPoint("major", 4.0, 0.0),
            SketchPoint("minor", 0.0, 2.0),
        ):
            sketch.add_point(point)
        sketch.add_geometry(SketchGeometry(
            "circle", GeometryType.CIRCLE, ("c1",),
            attributes={"radius": 1.0},
        ))
        sketch.add_geometry(SketchGeometry(
            "ellipse", GeometryType.ELLIPSE, ("c2", "major", "minor"),
        ))
        sketch.add_constraint(SketchConstraint(
            "concentric", "concentric", ("c1", "c2"),
            attributes={
                "first_geometry_id": "circle",
                "second_geometry_id": "ellipse",
            },
        ))
        entities, dimensions = sketch.to_editor_data()
        restored = SketchModel.from_editor_data(entities, dimensions)
        self.assertEqual(restored.constraints["concentric"].point_ids, ("c1", "c2"))

    def test_line_ellipse_tangent_round_trips(self):
        sketch = SketchModel()
        for point in (
            SketchPoint("l1", -5.0, 3.0),
            SketchPoint("l2", 5.0, 3.0),
            SketchPoint("center", 0.0, 0.0),
            SketchPoint("major", 5.0, 0.0),
            SketchPoint("minor", 0.0, 3.0),
            SketchPoint("contact", 0.0, 3.0),
        ):
            sketch.add_point(point)
        sketch.add_geometry(
            SketchGeometry("line", GeometryType.SEGMENT, ("l1", "l2"))
        )
        sketch.add_geometry(
            SketchGeometry(
                "ellipse",
                GeometryType.ELLIPSE,
                ("center", "major", "minor"),
            )
        )
        sketch.add_constraint(
            SketchConstraint(
                "tangent",
                "tangent",
                ("l1", "l2", "center", "major", "minor", "contact"),
                attributes={
                    "line_geometry_id": "line",
                    "curve_geometry_id": "ellipse",
                    "contact_point_id": "contact",
                },
            )
        )

        self.assertTrue(
            all(
                abs(value) < 1.0e-9
                for value in sketch.constraint_residuals("tangent")
            )
        )
        entities, dimensions = sketch.to_editor_data()
        restored = SketchModel.from_editor_data(entities, dimensions)
        self.assertEqual(
            restored.constraints["tangent"].point_ids,
            ("l1", "l2", "center", "major", "minor", "contact"),
        )

    def test_line_ellipse_tangent_solves_from_separated_line(self):
        sketch = SketchModel()
        for point in (
            SketchPoint("l1", -5.0, 4.0),
            SketchPoint("l2", 5.0, 4.0),
            SketchPoint("center", 0.0, 0.0),
            SketchPoint("major", 5.0, 0.0),
            SketchPoint("minor", 0.0, 3.0),
            SketchPoint("contact", 0.0, 3.0),
        ):
            sketch.add_point(point)
        sketch.add_geometry(
            SketchGeometry("line", GeometryType.SEGMENT, ("l1", "l2"))
        )
        sketch.add_geometry(SketchGeometry(
            "ellipse", GeometryType.ELLIPSE,
            ("center", "major", "minor"),
        ))
        sketch.add_constraint(SketchConstraint(
            "tangent", "tangent",
            ("l1", "l2", "center", "major", "minor", "contact"),
            attributes={
                "line_geometry_id": "line",
                "curve_geometry_id": "ellipse",
                "contact_point_id": "contact",
            },
        ))

        self.assertTrue(sketch.solve())
        self.assertEqual(sketch.violated_equations(), ())

    def test_ellipse_sampling_closes_and_arc_uses_requested_endpoints(self):
        ellipse = ellipse_points((0.0, 0.0), (8.0, 0.0), (0.0, 4.0))
        arc = elliptical_arc_points(
            (0.0, 0.0),
            (8.0, 0.0),
            (0.0, 4.0),
            (8.0, 0.0),
            (0.0, 4.0),
        )

        self.assertEqual(ellipse[0], ellipse[-1])
        self.assertAlmostEqual(arc[0][0], 8.0)
        self.assertAlmostEqual(arc[0][1], 0.0)
        self.assertAlmostEqual(arc[-1][0], 0.0, places=6)
        self.assertAlmostEqual(arc[-1][1], 4.0, places=6)

    def test_ellipse_and_elliptical_arc_round_trip_and_solve(self):
        entities = [
            {"id": "c", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "a", "type": "point", "x": 10.0, "y": 0.0},
            {"id": "b", "type": "point", "x": 1.0, "y": 5.0},
            {"id": "s", "type": "point", "x": 10.0, "y": 0.0},
            {"id": "e", "type": "point", "x": 0.0, "y": 5.0},
            {
                "id": "ellipse",
                "type": "ellipse",
                "point_ids": ["c", "a", "b"],
            },
            {
                "id": "elliptical_arc",
                "type": "elliptical_arc",
                "point_ids": ["c", "a", "b", "s", "e"],
                "clockwise": False,
            },
        ]
        model = SketchModel.from_editor_data(entities)

        self.assertTrue(model.solve())
        self.assertEqual(model.violated_equations(), ())
        restored, _dimensions = model.to_editor_data()
        restored_types = {
            item["id"]: item["type"] for item in restored
            if item["type"] != "point"
        }
        self.assertEqual(restored_types["ellipse"], "ellipse")
        self.assertEqual(
            restored_types["elliptical_arc"], "elliptical_arc"
        )

    def test_unlocked_user_dimension_remains_driving_and_unlocked(self):
        entities = [
            {"type": "point", "id": "a", "x": 0.0, "y": 0.0},
            {"type": "point", "id": "b", "x": 10.0, "y": 0.0},
            {"type": "segment", "id": "line", "point_ids": ["a", "b"]},
        ]
        dimensions = [{
            "id": "d1",
            "type": "distance",
            "point_ids": ["a", "b"],
            "value": 10.0,
            "locked": False,
        }]

        sketch = SketchModel.from_editor_data(entities, dimensions)
        self.assertTrue(sketch.dimensions["d1"].driving)
        _entities, restored = sketch.to_editor_data()
        self.assertTrue(restored[0]["driving"])
        self.assertFalse(restored[0]["locked"])

    def test_center_arc_uses_center_radius_and_two_end_directions(self):
        points = center_arc_points(
            (0.0, 0.0),
            (5.0, 0.0),
            (0.0, 9.0),
            segments=8,
        )

        self.assertEqual(len(points), 9)
        self.assertAlmostEqual(points[0][0], 5.0)
        self.assertAlmostEqual(points[0][1], 0.0)
        self.assertAlmostEqual(points[-1][0], 0.0)
        self.assertAlmostEqual(points[-1][1], 5.0)
        self.assertTrue(
            all(math.isclose(math.hypot(x, y), 5.0) for x, y in points)
        )

    def test_center_arc_supports_major_sweep(self):
        points = center_arc_points(
            (0.0, 0.0),
            (5.0, 0.0),
            (0.0, -5.0),
            segments=8,
        )

        self.assertGreater(len(points), 9)
        self.assertLess(points[len(points) // 2][0], 0.0)
        self.assertAlmostEqual(points[-1][0], 0.0, places=6)
        self.assertAlmostEqual(points[-1][1], -5.0, places=6)

    def test_center_arc_direction_selects_the_initial_side(self):
        counter_clockwise = center_arc_points(
            (0.0, 0.0),
            (5.0, 0.0),
            (0.0, 5.0),
            segments=8,
        )
        clockwise = center_arc_points(
            (0.0, 0.0),
            (5.0, 0.0),
            (0.0, 5.0),
            segments=8,
            clockwise=True,
        )

        self.assertGreater(counter_clockwise[1][1], 0.0)
        self.assertLess(clockwise[1][1], 0.0)
        self.assertGreater(len(clockwise), len(counter_clockwise))

    def test_arc_solver_keeps_both_endpoints_on_the_circle(self):
        model = SketchModel.from_editor_data(
            [
                {"id": "c", "type": "point", "x": 0.0, "y": 0.0},
                {"id": "s", "type": "point", "x": 5.0, "y": 0.0},
                {"id": "e", "type": "point", "x": 0.0, "y": 7.0},
                {
                    "id": "a",
                    "type": "arc",
                    "arc_mode": "center",
                    "radius": 5.0,
                    "point_ids": ["c", "s", "e"],
                },
            ],
            [],
        )

        self.assertTrue(model.solve())
        self.assertAlmostEqual(
            math.dist(model.points["c"].position(), model.points["s"].position()),
            math.dist(model.points["c"].position(), model.points["e"].position()),
            places=6,
        )

    def test_visible_arc_radius_dimension_drives_the_radius(self):
        model = SketchModel.from_editor_data(
            [
                {"id": "c", "type": "point", "x": 0.0, "y": 0.0},
                {"id": "s", "type": "point", "x": 5.0, "y": 0.0},
                {"id": "e", "type": "point", "x": 0.0, "y": 7.0},
                {
                    "id": "arc",
                    "type": "arc",
                    "arc_mode": "center",
                    "radius": 5.0,
                    "dimension_visible": True,
                    "dimension_mode": "radius",
                    "point_ids": ["c", "s", "e"],
                },
            ],
            [],
        )

        self.assertTrue(model.solve())
        self.assertAlmostEqual(model.geometry["arc"].attributes["radius"], 5.0)
        self.assertAlmostEqual(
            math.dist(model.points["c"].position(), model.points["e"].position()),
            5.0,
            places=6,
        )

    def test_circle_curve_attachment_is_part_of_the_solve(self):
        model = SketchModel.from_editor_data(
            [
                {"id": "c", "type": "point", "x": 0.0, "y": 0.0},
                {
                    "id": "p",
                    "type": "point",
                    "x": 7.0,
                    "y": 0.0,
                    "curve_attachment": {
                        "type": "circle",
                        "geometry_id": "circle",
                        "angle": 0.0,
                    },
                },
                {
                    "id": "circle",
                    "type": "circle",
                    "point_ids": ["c"],
                    "radius": 5.0,
                    "dimension_visible": True,
                },
            ],
            [],
        )

        self.assertTrue(model.solve())
        self.assertAlmostEqual(
            math.dist(model.points["c"].position(), model.points["p"].position()),
            5.0,
            places=6,
        )

    def test_line_endpoints_on_arc_solve_vertical_together(self):
        model = SketchModel.from_editor_data(
            [
                {"id": "c", "type": "point", "x": 0.0, "y": 0.0},
                {"id": "s", "type": "point", "x": 10.0, "y": 0.0},
                {"id": "e", "type": "point", "x": 0.0, "y": 10.0},
                {
                    "id": "q1",
                    "type": "point",
                    "x": 8.0,
                    "y": 6.0,
                    "curve_attachment": {
                        "type": "arc",
                        "geometry_id": "arc",
                        "fraction": 0.4,
                    },
                },
                {
                    "id": "q2",
                    "type": "point",
                    "x": 6.0,
                    "y": 8.0,
                    "curve_attachment": {
                        "type": "arc",
                        "geometry_id": "arc",
                        "fraction": 0.6,
                    },
                },
                {
                    "id": "arc",
                    "type": "arc",
                    "arc_mode": "center",
                    "radius": 10.0,
                    "point_ids": ["c", "s", "e"],
                },
                {
                    "id": "line",
                    "type": "construction",
                    "point_ids": ["q1", "q2"],
                    "constraints": [{"type": "vertical"}],
                },
            ],
            [],
        )

        self.assertTrue(model.solve())
        self.assertAlmostEqual(model.points["q1"].x, model.points["q2"].x, places=6)
        self.assertAlmostEqual(
            math.dist(model.points["c"].position(), model.points["q1"].position()),
            model.geometry["arc"].attributes["radius"],
            places=6,
        )

    def test_line_arc_tangency_round_trips_and_solves(self):
        entities = [
            {"id": "c", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "contact", "type": "point", "x": 10.0, "y": 0.0},
            {"id": "end", "type": "point", "x": 0.0, "y": 10.0},
            {"id": "line_end", "type": "point", "x": 10.0, "y": 8.0},
            {
                "id": "arc",
                "type": "arc",
                "arc_mode": "center",
                "radius": 10.0,
                "point_ids": ["c", "contact", "end"],
            },
            {
                "id": "line",
                "type": "segment",
                "point_ids": ["contact", "line_end"],
                "constraints": [{
                    "type": "tangent",
                    "geometry_id": "arc",
                    "contact_point_id": "contact",
                }],
            },
        ]
        model = SketchModel.from_editor_data(entities, [])

        self.assertTrue(model.solve())
        tangent = next(
            constraint
            for constraint in model.constraints.values()
            if constraint.constraint_type == "tangent"
        )
        self.assertEqual(tangent.attributes["curve_geometry_id"], "arc")
        self.assertTrue(
            all(abs(value) < 1.0e-6 for value in model.constraint_residuals(tangent.constraint_id))
        )
        restored, _dimensions = model.to_editor_data()
        restored_model = SketchModel.from_editor_data(restored, [])
        restored_tangent = next(
            constraint
            for constraint in restored_model.constraints.values()
            if constraint.constraint_type == "tangent"
        )
        self.assertEqual(restored_tangent.attributes["curve_geometry_id"], "arc")

    def test_corner_radius_evaluates_tangent_arc_and_drag_is_reversible(self):
        evaluated = evaluate_corner_radius(
            (0.0, 0.0),
            (10.0, 0.0),
            (0.0, 10.0),
            2.0,
        )

        self.assertIsNotNone(evaluated)
        assert evaluated is not None
        self.assertAlmostEqual(evaluated.first_tangent[0], 2.0)
        self.assertAlmostEqual(evaluated.first_tangent[1], 0.0)
        self.assertAlmostEqual(evaluated.second_tangent[0], 0.0)
        self.assertAlmostEqual(evaluated.second_tangent[1], 2.0)
        self.assertAlmostEqual(evaluated.center[0], 2.0)
        self.assertAlmostEqual(evaluated.center[1], 2.0)
        radius, maximum = corner_radius_from_drag(
            (0.0, 0.0),
            (10.0, 0.0),
            (0.0, 10.0),
            (3.0, 0.0),
        ) or (0.0, 0.0)
        self.assertAlmostEqual(radius, 3.0)
        self.assertGreater(maximum, radius)
        zero_radius, _maximum = corner_radius_from_drag(
            (0.0, 0.0),
            (10.0, 0.0),
            (0.0, 10.0),
            (0.0, 0.0),
        ) or (-1.0, 0.0)
        self.assertEqual(zero_radius, 0.0)

    def test_corner_radius_metadata_round_trips_with_segments(self):
        sketch = SketchModel()
        for point in (
            SketchPoint("a", 10.0, 0.0),
            SketchPoint("vertex", 0.0, 0.0),
            SketchPoint("b", 0.0, 10.0),
        ):
            sketch.add_point(point)
        sketch.add_geometry(
            SketchGeometry(
                "first",
                GeometryType.SEGMENT,
                ("a", "vertex"),
                {
                    "corner_radii": [{
                        "id": "radius:first:second:vertex",
                        "other_geometry_id": "second",
                        "vertex_id": "vertex",
                        "radius": 2.0,
                        "equal_radius_group": "equal-radius:1",
                    }],
                },
            )
        )
        sketch.add_geometry(
            SketchGeometry(
                "second",
                GeometryType.SEGMENT,
                ("vertex", "b"),
            )
        )

        entities, dimensions = sketch.to_editor_data()
        restored = SketchModel.from_editor_data(entities, dimensions)

        self.assertEqual(
            restored.geometry["first"].attributes["corner_radii"][0][
                "radius"
            ],
            2.0,
        )
        self.assertEqual(
            restored.geometry["first"].attributes["corner_radii"][0]["id"],
            "radius:first:second:vertex",
        )
        self.assertEqual(
            restored.geometry["first"].attributes["corner_radii"][0][
                "equal_radius_group"
            ],
            "equal-radius:1",
        )

    def test_corner_radius_does_not_change_rectangle_dimension_dof(self):
        sketch = SketchModel()
        for point in (
            SketchPoint("top_left", 0.0, 10.0),
            SketchPoint("top_right", 20.0, 10.0),
            SketchPoint("bottom_right", 20.0, 0.0),
            SketchPoint("bottom_left", 0.0, 0.0),
        ):
            sketch.add_point(point)
        segments = (
            ("top", "top_left", "top_right"),
            ("right", "top_right", "bottom_right"),
            ("bottom", "bottom_right", "bottom_left"),
            ("left", "bottom_left", "top_left"),
        )
        for geometry_id, first, second in segments:
            attributes = {}
            if geometry_id == "top":
                attributes["corner_radii"] = [{
                    "other_geometry_id": "left",
                    "vertex_id": "top_left",
                    "radius": 2.0,
                }]
            sketch.add_geometry(
                SketchGeometry(
                    geometry_id,
                    GeometryType.SEGMENT,
                    (first, second),
                    attributes,
                )
            )
        for index, (kind, points) in enumerate(
            (
                ("horizontal", ("top_left", "top_right")),
                ("vertical", ("top_right", "bottom_right")),
                ("horizontal", ("bottom_right", "bottom_left")),
                ("vertical", ("bottom_left", "top_left")),
            ),
            1,
        ):
            sketch.add_constraint(
                SketchConstraint(f"c{index}", kind, points)
            )

        top_reduction = sketch.dimension_dof_reduction(
            SketchDimension(
                "top_width",
                "distance_x",
                20.0,
                ("top_left", "top_right"),
            )
        )
        bottom_reduction = sketch.dimension_dof_reduction(
            SketchDimension(
                "bottom_width",
                "distance_x",
                20.0,
                ("bottom_left", "bottom_right"),
            )
        )

        self.assertEqual(top_reduction, 1)
        self.assertEqual(bottom_reduction, 1)

    def test_radius_dimension_is_redundant_when_profile_height_drives_arc(self):
        data = {
            "version": 3,
            "points": {
                "c": {"x": 0.0, "y": 0.0},
                "top": {"x": 0.0, "y": 2.0},
                "bottom": {"x": 0.0, "y": -2.0},
                "rt": {"x": 7.0, "y": 2.0},
                "rm": {"x": 7.0, "y": 0.0},
                "rb": {"x": 7.0, "y": -2.0},
            },
            "geometry": {
                "arc": {
                    "type": "arc",
                    "points": ["c", "top", "bottom"],
                    "radius": 2.0,
                    "dimension_visible": False,
                },
                "top_line": {"type": "segment", "points": ["top", "rt"]},
                "right_top": {"type": "segment", "points": ["rt", "rm"]},
                "bottom_line": {"type": "segment", "points": ["bottom", "rb"]},
                "right_bottom": {"type": "segment", "points": ["rm", "rb"]},
            },
            "constraints": {
                "c0": {"type": "point_on_reference", "points": ["c"], "references": ["sketch_origin"]},
                "c1": {"type": "point_on_reference", "points": ["top"], "references": ["sketch_axis:y"]},
                "c2": {"type": "point_on_reference", "points": ["bottom"], "references": ["sketch_axis:y"]},
                "c3": {"type": "point_on_reference", "points": ["rm"], "references": ["sketch_axis:x"]},
                "c4": {"type": "horizontal", "points": ["top", "rt"]},
                "c5": {"type": "vertical", "points": ["rt", "rm"]},
                "c6": {"type": "horizontal", "points": ["bottom", "rb"]},
                "c7": {"type": "vertical", "points": ["rm", "rb"]},
            },
            "dimensions": {
                "height": {
                    "type": "distance_y",
                    "value": 4.0,
                    "points": ["rt", "rb"],
                    "driving": True,
                }
            },
        }
        before = SketchModel.from_dict(data)
        after_data = copy.deepcopy(data)
        after_data["geometry"]["arc"]["dimension_visible"] = True
        after = SketchModel.from_dict(after_data)

        self.assertEqual(before.dof_analysis().degrees_of_freedom, 1)
        self.assertEqual(after.dof_analysis().degrees_of_freedom, 1)

    def test_circle_round_trips_with_centre_and_scalar_radius(self):
        sketch = SketchModel()
        sketch.add_point(SketchPoint("centre", 2.0, 3.0))
        sketch.add_geometry(
            SketchGeometry(
                "circle1",
                GeometryType.CIRCLE,
                ("centre",),
                {"radius": 5.0},
            )
        )

        restored = SketchModel.from_dict(sketch.to_dict())

        self.assertEqual(
            restored.geometry["circle1"].geometry_type,
            GeometryType.CIRCLE,
        )
        self.assertEqual(
            restored.geometry["circle1"].point_ids,
            ("centre",),
        )
        self.assertEqual(
            restored.geometry["circle1"].attributes["radius"],
            5.0,
        )
        entities, dimensions = restored.to_editor_data()
        editor_restored = SketchModel.from_editor_data(
            entities,
            dimensions,
        )
        self.assertEqual(
            editor_restored.geometry["circle1"].point_ids,
            ("centre",),
        )
        self.assertEqual(restored.dof_analysis().degrees_of_freedom, 3)

    def test_circle_rejects_non_positive_radius(self):
        sketch = SketchModel()
        sketch.add_point(SketchPoint("centre", 2.0, 3.0))

        with self.assertRaisesRegex(
            SketchModelError,
            "requires a positive radius",
        ):
            sketch.add_geometry(
                SketchGeometry(
                    "circle1",
                    GeometryType.CIRCLE,
                    ("centre",),
                    {"radius": 0.0},
                )
            )

    def test_line_circle_tangent_constraint_solves_and_round_trips(self):
        sketch = SketchModel()
        for point in (
            SketchPoint("a", -10.0, 0.0),
            SketchPoint("b", 10.0, 0.0),
            SketchPoint("centre", 0.0, 5.0),
            SketchPoint(
                "contact",
                0.0,
                0.0,
                attributes={
                    "derived": True,
                    "role": "tangent_contact",
                },
            ),
        ):
            sketch.add_point(point)
        sketch.add_geometry(
            SketchGeometry("line", GeometryType.SEGMENT, ("a", "b"))
        )
        sketch.add_geometry(
            SketchGeometry(
                "circle",
                GeometryType.CIRCLE,
                ("centre",),
                {"radius": 4.0},
            )
        )
        sketch.add_constraint(
            SketchConstraint(
                "t1",
                "tangent",
                ("a", "b", "centre", "contact"),
                attributes={
                    "line_geometry_id": "line",
                    "circle_geometry_id": "circle",
                    "side": 1,
                    "contact_point_id": "contact",
                },
            )
        )

        self.assertTrue(sketch.solve())
        self.assertEqual(sketch.violated_equations(), ())
        first = sketch.points["a"].position()
        second = sketch.points["b"].position()
        centre = sketch.points["centre"].position()
        line_length = math.dist(first, second)
        distance = abs(
            (second[0] - first[0]) * (centre[1] - first[1])
            - (second[1] - first[1]) * (centre[0] - first[0])
        ) / line_length
        self.assertAlmostEqual(
            distance,
            sketch.geometry["circle"].attributes["radius"],
            places=5,
        )
        entities, dimensions = sketch.to_editor_data()
        restored = SketchModel.from_editor_data(entities, dimensions)
        self.assertEqual(
            restored.constraints["t1"].attributes["line_geometry_id"],
            "line",
        )
        self.assertEqual(
            restored.constraints["t1"].attributes["circle_geometry_id"],
            "circle",
        )

    def test_circle_resize_preserves_two_tangent_constraints(self):
        sketch = SketchModel()
        for point in (
            SketchPoint("centre", 0.0, 0.0),
            SketchPoint("a1", -10.0, 5.0),
            SketchPoint("b1", 10.0, 5.0),
            SketchPoint("contact1", 0.0, 5.0),
            SketchPoint("a2", -10.0, -5.0),
            SketchPoint("b2", 10.0, -5.0),
            SketchPoint("contact2", 0.0, -5.0),
        ):
            sketch.add_point(point)
        sketch.add_geometry(
            SketchGeometry("line1", GeometryType.SEGMENT, ("a1", "b1"))
        )
        sketch.add_geometry(
            SketchGeometry("line2", GeometryType.SEGMENT, ("a2", "b2"))
        )
        sketch.add_geometry(
            SketchGeometry(
                "circle",
                GeometryType.CIRCLE,
                ("centre",),
                {"radius": 8.0, "dimension_visible": True},
            )
        )
        for index, line_id, contact_id, side in (
            (1, "line1", "contact1", 1),
            (2, "line2", "contact2", -1),
        ):
            sketch.add_constraint(
                SketchConstraint(
                    f"t{index}",
                    "tangent",
                    (
                        f"a{index}",
                        f"b{index}",
                        "centre",
                        contact_id,
                    ),
                    attributes={
                        "line_geometry_id": line_id,
                        "circle_geometry_id": "circle",
                        "side": side,
                        "contact_point_id": contact_id,
                    },
                )
            )

        self.assertTrue(sketch.solve())
        self.assertEqual(sketch.violated_equations(), ())
        self.assertEqual(set(sketch.constraints), {"t1", "t2"})
        self.assertEqual(sketch.geometry["circle"].attributes["radius"], 8.0)

    def test_horizontal_line_can_be_tangent_to_two_equal_circles(self):
        entities = [
            {"id": "center1", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "center2", "type": "point", "x": 20.0, "y": 0.0},
            {
                "id": "p1", "type": "point", "x": 0.0, "y": 5.0,
                "curve_attachment": {
                    "type": "circle", "geometry_id": "circle1", "angle": math.pi / 2,
                },
            },
            {
                "id": "p2", "type": "point", "x": 20.0, "y": 5.0,
                "curve_attachment": {
                    "type": "circle", "geometry_id": "circle2", "angle": math.pi / 2,
                },
            },
            {"id": "circle1", "type": "circle", "point_ids": ["center1"], "radius": 5.0},
            {"id": "circle2", "type": "circle", "point_ids": ["center2"], "radius": 5.0},
            {
                "id": "line", "type": "segment", "point_ids": ["p1", "p2"],
                "constraints": [
                    {"type": "tangent", "geometry_id": "circle1", "contact_point_id": "p1"},
                    {"type": "tangent", "geometry_id": "circle2", "contact_point_id": "p2"},
                    {"type": "horizontal"},
                ],
            },
        ]
        model = SketchModel.from_editor_data(entities, [])
        self.assertTrue(model.solve())
        self.assertEqual(model.violated_equations(), ())

        lower_entities = copy.deepcopy(entities)
        for entity in lower_entities:
            if entity.get("id") in ("p1", "p2"):
                entity["y"] = -5.0
                entity["curve_attachment"]["angle"] = -math.pi / 2
        lower_model = SketchModel.from_editor_data(lower_entities, [])
        self.assertTrue(lower_model.solve())
        self.assertEqual(lower_model.violated_equations(), ())

    def test_legacy_two_point_circle_loads_as_centre_and_radius(self):
        restored = SketchModel.from_dict({
            "version": 2,
            "points": {
                "centre": {"x": 1.0, "y": 2.0},
                "rim": {"x": 4.0, "y": 6.0},
            },
            "geometry": {
                "circle": {
                    "type": "circle",
                    "points": ["centre", "rim"],
                },
            },
            "constraints": {},
            "dimensions": {},
        })

        self.assertEqual(
            restored.geometry["circle"].point_ids,
            ("centre",),
        )
        self.assertEqual(
            restored.geometry["circle"].attributes["radius"],
            5.0,
        )
        self.assertNotIn("rim", restored.points)

    def test_fully_dimensioned_rectangle_rejects_driving_diagonal(self):
        width = 12.0
        height = 5.0
        sketch = SketchModel()
        for point in (
            SketchPoint("p1", 0.0, height),
            SketchPoint("p2", width, height),
            SketchPoint("p3", width, 0.0),
            SketchPoint("p4", 0.0, 0.0),
        ):
            sketch.add_point(point)
        for constraint in (
            SketchConstraint(
                "c1", "point_on_reference", ("p1",),
                ("sketch_axis:y",),
            ),
            SketchConstraint(
                "c2", "point_on_reference", ("p3",),
                ("sketch_axis:x",),
            ),
            SketchConstraint(
                "c3", "point_on_reference", ("p4",),
                ("sketch_origin",),
            ),
            SketchConstraint("c4", "horizontal", ("p1", "p2")),
            SketchConstraint("c5", "vertical", ("p2", "p3")),
        ):
            sketch.add_constraint(constraint)
        self.assertEqual(sketch.dof_analysis().degrees_of_freedom, 2)
        sketch.add_dimension(
            SketchDimension(
                "d1", "distance_x", width, ("p1", "p2")
            )
        )
        sketch.add_dimension(
            SketchDimension(
                "d2", "distance_y", height, ("p2", "p3")
            )
        )

        self.assertEqual(sketch.dof_analysis().degrees_of_freedom, 0)
        diagonal = SketchDimension(
            "d3",
            "distance",
            math.hypot(width, height),
            ("p4", "p2"),
        )
        self.assertEqual(sketch.dimension_dof_reduction(diagonal), 0)
        conflicting = copy.deepcopy(sketch)
        conflicting.add_dimension(
            SketchDimension(
                "d3", "distance", 20.0, ("p4", "p2")
            )
        )
        self.assertEqual(conflicting.violated_equations(), ("d3",))

        width_and_diagonal = copy.deepcopy(sketch)
        del width_and_diagonal.dimensions["d2"]
        width_and_diagonal.add_dimension(
            SketchDimension(
                "d3",
                "distance",
                math.hypot(width, height),
                ("p4", "p2"),
            )
        )
        width_and_diagonal.dimensions["d1"].value = 5.0
        self.assertTrue(width_and_diagonal.solve())
        self.assertEqual(width_and_diagonal.violated_equations(), ())
        self.assertAlmostEqual(
            width_and_diagonal.points["p2"].y,
            12.0,
            places=5,
        )

    def test_dimension_type_comes_from_ab_rectangle(self):
        first = (0.0, 0.0)
        second = (10.0, 5.0)

        self.assertEqual(
            classify_linear_dimension(first, second, (4.0, 2.0)),
            "distance",
        )
        self.assertEqual(
            classify_linear_dimension(first, second, (4.0, 8.0)),
            "distance_x",
        )
        self.assertEqual(
            classify_linear_dimension(first, second, (12.0, 2.0)),
            "distance_y",
        )
        self.assertEqual(
            classify_linear_dimension(first, second, (11.0, 20.0)),
            "distance_y",
        )

    def test_connected_segments_share_one_point(self):
        sketch = SketchModel()
        for point in (
            SketchPoint("p1", 0.0, 0.0),
            SketchPoint("p2", 10.0, 0.0),
            SketchPoint("p3", 10.0, 5.0),
        ):
            sketch.add_point(point)
        sketch.add_geometry(
            SketchGeometry("g1", GeometryType.SEGMENT, ("p1", "p2"))
        )
        sketch.add_geometry(
            SketchGeometry("g2", GeometryType.SEGMENT, ("p2", "p3"))
        )

        self.assertEqual(sketch.geometry["g1"].point_ids[1], "p2")
        self.assertEqual(sketch.geometry["g2"].point_ids[0], "p2")
        sketch.validate()

    def test_geometry_cannot_own_missing_point(self):
        sketch = SketchModel(points={"p1": SketchPoint("p1", 0.0, 0.0)})

        with self.assertRaises(SketchModelError):
            sketch.add_geometry(
                SketchGeometry("g1", GeometryType.SEGMENT, ("p1", "p2"))
            )

    def test_referenced_point_cannot_be_removed(self):
        sketch = SketchModel(
            points={
                "p1": SketchPoint("p1", 0.0, 0.0),
                "p2": SketchPoint("p2", 1.0, 0.0),
            }
        )
        sketch.add_geometry(
            SketchGeometry("g1", GeometryType.SEGMENT, ("p1", "p2"))
        )

        with self.assertRaises(SketchModelError):
            sketch.remove_point("p1")

    def test_imports_editor_entities(self):
        sketch = SketchModel.from_editor_data(
            [
                {"id": "p1", "type": "point", "x": 0, "y": 0},
                {"id": "p2", "type": "point", "x": 2, "y": 0},
                {
                    "id": "g1",
                    "type": "segment",
                    "point_ids": ["p1", "p2"],
                    "constraints": [{"type": "horizontal"}],
                },
            ]
        )

        self.assertEqual(set(sketch.points), {"p1", "p2"})
        self.assertEqual(sketch.geometry["g1"].point_ids, ("p1", "p2"))
        self.assertEqual(
            next(iter(sketch.constraints.values())).point_ids,
            ("p1", "p2"),
        )

    def test_perpendicularity_is_three_point_relation(self):
        sketch = SketchModel.from_editor_data(
            [
                {"id": "p1", "type": "point", "x": 0, "y": 10},
                {"id": "p2", "type": "point", "x": 0, "y": 0},
                {"id": "p3", "type": "point", "x": 10, "y": 0},
                {
                    "id": "g1",
                    "type": "segment",
                    "point_ids": ["p1", "p2"],
                },
                {
                    "id": "g2",
                    "type": "segment",
                    "point_ids": ["p2", "p3"],
                    "constraints": [
                        {"type": "perpendicular", "geometry_id": "g1"}
                    ],
                },
            ]
        )

        constraint = next(iter(sketch.constraints.values()))
        self.assertEqual(constraint.point_ids, ("p1", "p2", "p3"))
        self.assertNotIn("geometry", sketch.to_dict()["constraints"]["c1"])
        self.assertEqual(sketch.constraint_residuals("c1"), (0.0,))

        sketch.points["p3"].y = 2.0
        self.assertNotEqual(sketch.constraint_residuals("c1"), (0.0,))

    def test_perpendicularity_to_external_reference_round_trips(self):
        sketch = SketchModel.from_editor_data(
            [
                {"id": "p1", "type": "point", "x": 0.0, "y": 0.0},
                {"id": "p2", "type": "point", "x": 0.0, "y": 4.0},
                {
                    "id": "g1",
                    "type": "segment",
                    "point_ids": ["p1", "p2"],
                    "constraints": [{
                        "type": "perpendicular",
                        "reference_id": "external:1",
                        "reference_direction": [1.0, 0.0],
                    }],
                },
            ]
        )

        self.assertEqual(sketch.constraint_residuals("c1"), (0.0,))
        self.assertTrue(sketch.solve())
        entities, _dimensions = sketch.to_editor_data()
        constraint = entities[2]["constraints"][0]
        self.assertEqual(constraint["reference_id"], "external:1")
        self.assertEqual(constraint["reference_direction"], [1.0, 0.0])

    def test_perpendicularity_between_separate_lines_round_trips(self):
        sketch = SketchModel.from_editor_data(
            [
                {"id": "p1", "type": "point", "x": 0.0, "y": 0.0},
                {"id": "p2", "type": "point", "x": 3.0, "y": 0.0},
                {"id": "p3", "type": "point", "x": 5.0, "y": 2.0},
                {"id": "p4", "type": "point", "x": 5.0, "y": 6.0},
                {
                    "id": "g1",
                    "type": "construction",
                    "point_ids": ["p1", "p2"],
                },
                {
                    "id": "g2",
                    "type": "segment",
                    "point_ids": ["p3", "p4"],
                    "constraints": [{
                        "type": "perpendicular",
                        "geometry_id": "g1",
                    }],
                },
            ]
        )

        self.assertEqual(sketch.constraint_residuals("c1"), (0.0,))
        self.assertTrue(sketch.solve())
        entities, _dimensions = sketch.to_editor_data()
        constraint = entities[-1]["constraints"][0]
        self.assertEqual(constraint["geometry_id"], "g1")

    def test_canonical_round_trip_keeps_constraints_separate(self):
        sketch = SketchModel.from_editor_data(
            [
                {
                    "id": "p1",
                    "type": "point",
                    "x": 0,
                    "y": 0,
                    "constraints": [
                        {
                            "type": "point_on_reference",
                            "reference_id": "sketch_origin",
                        }
                    ],
                },
                {"id": "p2", "type": "point", "x": 2, "y": 0},
                {
                    "id": "g1",
                    "type": "segment",
                    "point_ids": ["p1", "p2"],
                    "constraints": [{"type": "horizontal"}],
                },
            ]
        )

        data = sketch.to_dict()
        self.assertNotIn("constraints", data["points"]["p1"])
        restored = SketchModel.from_dict(data)
        entities, _dimensions = restored.to_editor_data()
        point = next(entity for entity in entities if entity["id"] == "p1")
        self.assertEqual(
            point["constraints"][0]["reference_id"], "sketch_origin"
        )

    def test_coordinate_lock_is_persisted_as_dimension(self):
        sketch = SketchModel.from_editor_data(
            [
                {
                    "id": "p1",
                    "type": "point",
                    "x": 4.0,
                    "y": 3.0,
                    "dimension_locks": ["x"],
                }
            ]
        )

        data = sketch.to_dict()
        self.assertNotIn("dimension_locks", data["points"]["p1"])
        self.assertEqual(
            data["dimensions"]["coordinate:p1:x"]["type"],
            "coordinate_x",
        )
        entities, dimensions = SketchModel.from_dict(
            data
        ).to_editor_data()
        self.assertEqual(entities[0]["dimension_locks"], ["x"])
        self.assertEqual(dimensions, [])

    def test_coordinate_change_regenerates_dependent_distance(self):
        sketch = SketchModel()
        sketch.add_point(SketchPoint("p1", 0.0, 0.0))
        sketch.add_point(SketchPoint("p2", 10.0, 0.0))
        sketch.add_constraint(
            SketchConstraint("horizontal", "horizontal", ("p1", "p2"))
        )
        sketch.add_dimension(
            SketchDimension(
                "coordinate:p1:x",
                "coordinate_x",
                5.0,
                ("p1",),
                True,
            )
        )
        sketch.add_dimension(
            SketchDimension(
                "distance",
                "distance_x",
                10.0,
                ("p1", "p2"),
                True,
            )
        )

        self.assertTrue(sketch.solve())
        self.assertAlmostEqual(sketch.points["p1"].x, 5.0, places=6)
        self.assertAlmostEqual(sketch.points["p2"].x, 15.0, places=6)

    def test_symmetric_axis_dimension_uses_full_diameter_value(self):
        sketch = SketchModel()
        sketch.add_point(SketchPoint("p1", 0.0, 4.0))
        sketch.add_point(SketchPoint("p2", 10.0, 4.0))
        sketch.add_geometry(
            SketchGeometry("g1", GeometryType.SEGMENT, ("p1", "p2"))
        )
        sketch.add_dimension(
            SketchDimension(
                "diameter",
                "distance_axis",
                20.0,
                ("p1", "p2"),
                True,
                {
                    "reference_id": "sketch_axis:x",
                    "side_sign": 1.0,
                    "symmetric_diameter": True,
                },
            )
        )

        self.assertTrue(sketch.solve())
        self.assertAlmostEqual(sketch.points["p1"].y, 10.0, places=6)
        self.assertAlmostEqual(sketch.points["p2"].y, 10.0, places=6)

    def test_symmetric_dimension_uses_construction_line_as_axis(self):
        sketch = SketchModel()
        sketch.add_point(SketchPoint("target", 5.0, 4.0))
        sketch.add_point(SketchPoint("axis_a", 2.0, 0.0))
        sketch.add_point(SketchPoint("axis_b", 2.0, 10.0))
        for point_id, x, y in (("axis_a", 2.0, 0.0), ("axis_b", 2.0, 10.0)):
            sketch.add_dimension(SketchDimension(
                f"{point_id}_x", "coordinate_x", x, (point_id,), True
            ))
            sketch.add_dimension(SketchDimension(
                f"{point_id}_y", "coordinate_y", y, (point_id,), True
            ))
        sketch.add_dimension(
            SketchDimension(
                "diameter",
                "distance_symmetry",
                10.0,
                ("target", "axis_a", "axis_b"),
                True,
                {"target_count": 1, "side_sign": -1.0},
            )
        )

        self.assertTrue(sketch.solve())
        self.assertAlmostEqual(sketch.points["target"].x, 7.0, places=6)
        self.assertEqual(sketch.violated_equations(), ())

    def test_three_point_angle_dimension_is_solved(self):
        sketch = SketchModel()
        sketch.add_point(SketchPoint("first", 1.0, 0.0))
        sketch.add_point(SketchPoint("vertex", 0.0, 0.0))
        sketch.add_point(SketchPoint("second", 1.0, 1.0))
        sketch.add_dimension(
            SketchDimension(
                "angle",
                "angle",
                90.0,
                ("first", "vertex", "second"),
                True,
            )
        )

        self.assertTrue(sketch.solve())
        self.assertEqual(sketch.violated_equations(), ())

    def test_line_to_axis_angle_round_trip(self):
        sketch = SketchModel()
        sketch.add_point(SketchPoint("p1", 0.0, 0.0))
        sketch.add_point(SketchPoint("p2", 2.0, 1.0))
        sketch.add_dimension(
            SketchDimension(
                "angle",
                "angle",
                math.degrees(math.atan2(1.0, 2.0)),
                ("p1", "p2"),
                True,
                {"reference_id": "sketch_axis:x"},
            )
        )

        restored = SketchModel.from_dict(sketch.to_dict())
        dimension = restored.dimensions["angle"]
        self.assertEqual(
            dimension.attributes["reference_id"],
            "sketch_axis:x",
        )
        self.assertEqual(restored.violated_equations(), ())

    def test_parallel_line_to_axis_distance_is_solved_and_round_trips(self):
        sketch = SketchModel()
        sketch.add_point(SketchPoint("p1", 0.0, 5.0))
        sketch.add_point(SketchPoint("p2", 10.0, 5.0))
        sketch.add_dimension(
            SketchDimension(
                "axis-distance",
                "distance_axis",
                5.0,
                ("p1", "p2"),
                True,
                {
                    "reference_id": "sketch_axis:x",
                    "side_sign": 1.0,
                },
            )
        )

        self.assertEqual(sketch.violated_equations(), ())
        restored = SketchModel.from_dict(sketch.to_dict())
        self.assertEqual(restored.violated_equations(), ())
        self.assertEqual(
            restored.dimensions["axis-distance"].attributes[
                "reference_id"
            ],
            "sketch_axis:x",
        )

    def test_reflex_and_negative_angles_share_geometric_equation(self):
        for target in (270.0, -90.0, -270.0):
            sketch = SketchModel()
            sketch.add_point(SketchPoint("first", 1.0, 0.0))
            sketch.add_point(SketchPoint("vertex", 0.0, 0.0))
            sketch.add_point(SketchPoint("second", 0.0, 1.0))
            sketch.add_dimension(
                SketchDimension(
                    "angle",
                    "angle",
                    target,
                    ("first", "vertex", "second"),
                    True,
                )
            )
            self.assertEqual(sketch.violated_equations(), ())

    def test_point_on_construction_line_is_point_based_and_solved(self):
        sketch = SketchModel.from_editor_data([
            {
                "id": "p1",
                "type": "point",
                "x": 0.0,
                "y": 0.0,
                "dimension_locks": ["x", "y"],
            },
            {
                "id": "p2",
                "type": "point",
                "x": 10.0,
                "y": 0.0,
                "dimension_locks": ["x", "y"],
            },
            {
                "id": "p3",
                "type": "point",
                "x": 4.0,
                "y": 3.0,
                "constraints": [{
                    "type": "point_on_line",
                    "point_ids": ["p1", "p2"],
                    "bounded": True,
                }],
            },
            {
                "id": "g1",
                "type": "construction",
                "point_ids": ["p1", "p2"],
            },
        ])

        constraint = next(iter(sketch.constraints.values()))
        self.assertEqual(
            constraint.point_ids,
            ("p3", "p1", "p2"),
        )
        self.assertTrue(sketch.solve())
        self.assertAlmostEqual(sketch.points["p3"].y, 0.0, places=6)
        entities, _dimensions = sketch.to_editor_data()
        restored_point = next(
            entity for entity in entities if entity["id"] == "p3"
        )
        self.assertEqual(
            restored_point["constraints"][0]["point_ids"],
            ["p1", "p2"],
        )
        self.assertTrue(
            restored_point["constraints"][0]["bounded"]
        )

    def test_midpoint_constraint_round_trips_and_solves(self):
        sketch = SketchModel.from_editor_data([
            {
                "id": "p1",
                "type": "point",
                "x": 0.0,
                "y": 0.0,
                "dimension_locks": ["x", "y"],
            },
            {
                "id": "p2",
                "type": "point",
                "x": 8.0,
                "y": 4.0,
                "dimension_locks": ["x", "y"],
            },
            {
                "id": "middle",
                "type": "point",
                "x": 0.0,
                "y": 0.0,
                "constraints": [{
                    "type": "midpoint",
                    "point_ids": ["p1", "p2"],
                }],
            },
            {
                "id": "g1",
                "type": "segment",
                "point_ids": ["p1", "p2"],
            },
        ])

        self.assertTrue(sketch.solve())
        self.assertAlmostEqual(sketch.points["middle"].x, 4.0)
        self.assertAlmostEqual(sketch.points["middle"].y, 2.0)
        entities, _dimensions = sketch.to_editor_data()
        restored = next(
            entity for entity in entities if entity["id"] == "middle"
        )
        self.assertEqual(
            restored["constraints"][0]["point_ids"],
            ["p1", "p2"],
        )

    def test_geometry_role_round_trips_without_changing_type(self):
        sketch = SketchModel.from_editor_data([
            {"id": "p1", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "p2", "type": "point", "x": 4.0, "y": 0.0},
            {
                "id": "g1",
                "type": "segment",
                "role": "construction",
                "point_ids": ["p1", "p2"],
            },
        ])

        entities, _dimensions = sketch.to_editor_data()
        geometry = next(
            entity for entity in entities if entity["id"] == "g1"
        )
        self.assertEqual(geometry["type"], "segment")
        self.assertEqual(geometry["role"], "construction")

    def test_equal_length_is_distance_between_two_point_pairs(self):
        sketch = SketchModel()
        for point in (
            SketchPoint("p1", 0.0, 0.0),
            SketchPoint("p2", 10.0, 0.0),
            SketchPoint("p3", 0.0, 5.0),
            SketchPoint("p4", 4.0, 5.0),
        ):
            sketch.add_point(point)
        sketch.add_geometry(
            SketchGeometry(
                "g1", GeometryType.SEGMENT, ("p1", "p2")
            )
        )
        sketch.add_geometry(
            SketchGeometry(
                "g2", GeometryType.SEGMENT, ("p3", "p4")
            )
        )
        sketch.add_dimension(
            SketchDimension(
                "fixed_length",
                "distance",
                10.0,
                ("p1", "p2"),
                True,
            )
        )
        sketch.add_constraint(
            SketchConstraint(
                "equal",
                "equal_length",
                ("p1", "p2", "p3", "p4"),
            )
        )

        self.assertTrue(sketch.solve())
        self.assertAlmostEqual(
            math.dist(
                sketch.points["p3"].position(),
                sketch.points["p4"].position(),
            ),
            10.0,
            places=6,
        )
        self.assertEqual(sketch.violated_equations(), ())

    def test_dimensioned_hexagon_vertices_remain_on_support_circle(self):
        entities = [
            {"id": "c", "type": "point", "x": 0.0, "y": 0.0},
        ]
        for index in range(6):
            angle = index * math.tau / 6.0
            entities.append({
                "id": f"p{index}",
                "type": "point",
                "x": 10.0 * math.cos(angle),
                "y": 10.0 * math.sin(angle),
                "curve_attachment": {
                    "type": "circle",
                    "geometry_id": "support",
                    "angle": angle,
                },
            })
        entities.append({
            "id": "support",
            "type": "circle",
            "point_ids": ["c"],
            "radius": 10.0,
            "role": "construction",
        })
        for index in range(6):
            segment = {
                "id": f"s{index}",
                "type": "segment",
                "point_ids": [f"p{index}", f"p{(index + 1) % 6}"],
            }
            if index:
                segment["constraints"] = [{
                    "type": "equal_length",
                    "geometry_id": "s0",
                }]
            entities.append(segment)

        model = SketchModel.from_editor_data(entities, [{
            "id": "side",
            "type": "distance",
            "point_ids": ["p0", "p1"],
            "value": 15.0,
            "locked": True,
        }])

        self.assertTrue(model.solve())
        radius = float(model.geometry["support"].attributes["radius"])
        for index in range(6):
            self.assertAlmostEqual(
                math.dist(
                    model.points["c"].position(),
                    model.points[f"p{index}"].position(),
                ),
                radius,
                places=6,
            )
            self.assertAlmostEqual(
                math.dist(
                    model.points[f"p{index}"].position(),
                    model.points[f"p{(index + 1) % 6}"].position(),
                ),
                15.0,
                places=6,
            )

    def test_point_line_dimension_uses_perpendicular_distance(self):
        sketch = SketchModel()
        for point in (
            SketchPoint("point", 4.0, 3.0),
            SketchPoint("line_start", 0.0, 0.0),
            SketchPoint("line_end", 10.0, 0.0),
        ):
            sketch.add_point(point)
        sketch.add_dimension(
            SketchDimension(
                "point_line",
                "distance_line",
                3.0,
                ("point", "line_start", "line_end"),
                True,
            )
        )

        self.assertEqual(sketch.violated_equations(), ())
        sketch.points["point"].y = 4.0
        self.assertEqual(sketch.violated_equations(), ("point_line",))

    def test_point_line_dimension_does_not_collapse_reference_line(self):
        sketch = SketchModel()
        for point in (
            SketchPoint("origin", 0.0, 0.0),
            SketchPoint("measured", 0.0, 5.7735026919),
            SketchPoint("corner", -2.5, 4.3301270189),
            SketchPoint("line_start", 0.0, 10.3923048454),
            SketchPoint("line_end", -3.0, 8.65),
        ):
            sketch.add_point(point)
        for constraint in (
            SketchConstraint(
                "origin_lock",
                "point_on_reference",
                ("origin",),
                ("sketch_origin",),
            ),
            SketchConstraint(
                "measured_on_y",
                "point_on_reference",
                ("measured",),
                ("sketch_axis:y",),
            ),
            SketchConstraint(
                "line_start_on_y",
                "point_on_reference",
                ("line_start",),
                ("sketch_axis:y",),
            ),
            SketchConstraint(
                "perpendicular",
                "perpendicular",
                ("origin", "corner", "measured"),
            ),
            SketchConstraint(
                "parallel",
                "parallel",
                ("measured", "corner", "line_start", "line_end"),
            ),
        ):
            sketch.add_constraint(constraint)
        for dimension in (
            SketchDimension(
                "angle",
                "angle",
                120.0,
                ("origin", "corner"),
                True,
                {"reference_id": "sketch_axis:x"},
            ),
            SketchDimension(
                "base_length",
                "distance",
                5.0,
                ("origin", "corner"),
                True,
            ),
            SketchDimension(
                "point_line",
                "distance_line",
                20.0,
                ("measured", "line_start", "line_end"),
                True,
            ),
        ):
            sketch.add_dimension(dimension)

        self.assertTrue(sketch.solve())
        self.assertEqual(sketch.violated_equations(), ())
        self.assertGreater(
            math.dist(
                sketch.points["line_start"].position(),
                sketch.points["line_end"].position(),
            ),
            1.0,
        )

    def test_symmetric_constraint_round_trips_and_solves(self):
        entities = [
            {
                "id": "p1",
                "type": "point",
                "x": -3.0,
                "y": 2.0,
                "constraints": [{
                    "type": "symmetric",
                    "point_id": "p2",
                    "point_ids": ["a1", "a2"],
                }],
            },
            {"id": "p2", "type": "point", "x": 4.0, "y": 1.0},
            {"id": "a1", "type": "point", "x": 0.0, "y": -5.0},
            {"id": "a2", "type": "point", "x": 0.0, "y": 5.0},
            {
                "id": "axis",
                "type": "construction",
                "point_ids": ["a1", "a2"],
            },
        ]
        model = SketchModel.from_editor_data(entities, [])
        self.assertTrue(model.solve())
        residuals = next(
            model.constraint_residuals(constraint_id)
            for constraint_id, constraint in model.constraints.items()
            if constraint.constraint_type == "symmetric"
        )
        self.assertTrue(all(abs(value) < 1.0e-6 for value in residuals))
        round_tripped, _dimensions = model.to_editor_data()
        first = next(item for item in round_tripped if item["id"] == "p1")
        symmetric = next(
            item
            for item in first.get("constraints", ())
            if item.get("type") == "symmetric"
        )
        self.assertEqual(symmetric["point_id"], "p2")
        self.assertEqual(symmetric["point_ids"], ["a1", "a2"])

    def test_symmetric_constraint_supports_external_axis(self):
        entities = [
            {
                "id": "p1",
                "type": "point",
                "x": -3.0,
                "y": 2.0,
                "constraints": [{
                    "type": "symmetric",
                    "point_id": "p2",
                    "reference_id": "sketch_axis:y",
                    "reference_origin": [0.0, 0.0],
                    "reference_direction": [0.0, 1.0],
                }],
            },
            {"id": "p2", "type": "point", "x": 4.0, "y": 1.0},
        ]
        model = SketchModel.from_editor_data(entities, [])
        self.assertTrue(model.solve())
        residuals = next(
            model.constraint_residuals(constraint_id)
            for constraint_id, constraint in model.constraints.items()
            if constraint.constraint_type == "symmetric"
        )
        self.assertTrue(all(abs(value) < 1.0e-6 for value in residuals))
        round_tripped, _dimensions = model.to_editor_data()
        first = next(item for item in round_tripped if item["id"] == "p1")
        symmetric = next(
            item for item in first.get("constraints", ())
            if item.get("type") == "symmetric"
        )
        self.assertEqual(symmetric["point_id"], "p2")
        self.assertEqual(symmetric["reference_id"], "sketch_axis:y")

    def test_canonical_constraint_rejects_geometry_operand(self):
        with self.assertRaises(SketchModelError):
            SketchModel.from_dict(
                {
                    "version": 2,
                    "points": {
                        "p1": {"x": 0, "y": 0},
                        "p2": {"x": 1, "y": 0},
                    },
                    "geometry": {
                        "g1": {
                            "type": "segment",
                            "points": ["p1", "p2"],
                        }
                    },
                    "constraints": {
                        "c1": {
                            "type": "horizontal",
                            "geometry": ["g1"],
                        }
                    },
                    "dimensions": {},
                }
            )


if __name__ == "__main__":
    unittest.main()
