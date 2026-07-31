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
    corner_radius_from_drag,
    evaluate_corner_radius,
)


class SketchModelTests(unittest.TestCase):
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
