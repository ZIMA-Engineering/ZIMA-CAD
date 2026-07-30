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


class SketchModelTests(unittest.TestCase):
    def test_circle_round_trips_with_centre_and_radius_point(self):
        sketch = SketchModel()
        sketch.add_point(SketchPoint("centre", 2.0, 3.0))
        sketch.add_point(SketchPoint("rim", 7.0, 3.0))
        sketch.add_geometry(
            SketchGeometry(
                "circle1",
                GeometryType.CIRCLE,
                ("centre", "rim"),
            )
        )

        restored = SketchModel.from_dict(sketch.to_dict())

        self.assertEqual(
            restored.geometry["circle1"].geometry_type,
            GeometryType.CIRCLE,
        )
        self.assertEqual(
            restored.geometry["circle1"].point_ids,
            ("centre", "rim"),
        )
        entities, dimensions = restored.to_editor_data()
        editor_restored = SketchModel.from_editor_data(
            entities,
            dimensions,
        )
        self.assertEqual(
            editor_restored.geometry["circle1"].point_ids,
            ("centre", "rim"),
        )

    def test_circle_rejects_identical_centre_and_radius_point(self):
        sketch = SketchModel()
        sketch.add_point(SketchPoint("centre", 2.0, 3.0))

        with self.assertRaisesRegex(
            SketchModelError,
            "requires distinct centre",
        ):
            sketch.add_geometry(
                SketchGeometry(
                    "circle1",
                    GeometryType.CIRCLE,
                    ("centre", "centre"),
                )
            )

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
