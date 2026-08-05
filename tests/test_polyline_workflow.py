import math
import unittest

from zima_cad.sketch_geometry import (
    arc_cardinal_keypoints,
    elliptical_arc_cardinal_keypoints,
    outward_minor_arc_endpoint,
    polyline_arc_start_context,
    regular_polygon_vertices,
    valid_automatic_tangent,
)
from zima_cad.sketch_model import SketchModel


class PolylineArcWorkflowTests(unittest.TestCase):
    def test_regular_polygon_supports_four_six_and_eight_sides(self):
        for sides in (4, 6, 8):
            vertices = regular_polygon_vertices((0.0, 0.0), (10.0, 0.0), sides)
            self.assertEqual(len(vertices), sides)
            for vertex in vertices:
                self.assertAlmostEqual(math.dist((0.0, 0.0), vertex), 10.0)
            side_lengths = [
                math.dist(vertices[index], vertices[(index + 1) % sides])
                for index in range(sides)
            ]
            self.assertTrue(all(
                math.isclose(length, side_lengths[0], abs_tol=1.0e-9)
                for length in side_lengths
            ))

    def test_regular_polygon_rejects_unsupported_side_count(self):
        with self.assertRaises(ValueError):
            regular_polygon_vertices((0.0, 0.0), (10.0, 0.0), 5)

    def test_horizontal_constraint_rotates_attached_regular_octagon(self):
        vertices = regular_polygon_vertices((0.0, 0.0), (10.0, 3.0), 8)
        start_angle = math.atan2(3.0, 10.0)
        entities = [{"id": "c", "type": "point", "x": 0.0, "y": 0.0}]
        for index, position in enumerate(vertices):
            entities.append({
                "id": f"p{index}", "type": "point",
                "x": position[0], "y": position[1],
                "curve_attachment": {
                    "type": "circle", "geometry_id": "support",
                    "angle": start_angle + index * math.tau / 8.0,
                },
            })
        entities.append({
            "id": "support", "type": "circle", "point_ids": ["c"],
            "radius": math.sqrt(109.0), "role": "construction",
        })
        for index in range(8):
            segment = {
                "id": f"s{index}", "type": "segment",
                "point_ids": [f"p{index}", f"p{(index + 1) % 8}"],
            }
            constraints = []
            if index:
                constraints.append({"type": "equal_length", "geometry_id": "s0"})
            if index == 2:
                constraints.append({"type": "horizontal"})
            if constraints:
                segment["constraints"] = constraints
            entities.append(segment)
        model = SketchModel.from_editor_data(entities)
        self.assertTrue(model.solve())
        first = model.points["p2"].position()
        second = model.points["p3"].position()
        self.assertAlmostEqual(first[1], second[1], places=7)
        self.assertEqual(model.violated_equations(), ())

    def test_arc_offers_only_cardinal_points_inside_its_domain(self):
        center = (0.0, 0.0)
        start = (math.sqrt(0.5) * 10.0, -math.sqrt(0.5) * 10.0)
        end = (-math.sqrt(0.5) * 10.0, math.sqrt(0.5) * 10.0)
        keypoints = arc_cardinal_keypoints(center, start, end)
        self.assertEqual([angle for angle, _point in keypoints], [0, 90])
        self.assertAlmostEqual(keypoints[0][1][0], 10.0)
        self.assertAlmostEqual(keypoints[0][1][1], 0.0)

    def test_elliptical_arc_offers_only_internal_cardinal_points(self):
        root = math.sqrt(0.5)
        keypoints = elliptical_arc_cardinal_keypoints(
            (0.0, 0.0),
            (10.0, 0.0),
            (0.0, 5.0),
            (10.0 * root, -5.0 * root),
            (-10.0 * root, 5.0 * root),
        )
        self.assertEqual([angle for angle, _point in keypoints], [0, 90])
        self.assertEqual(keypoints[0][1], (10.0, 0.0))
        self.assertAlmostEqual(keypoints[1][1][0], 0.0, places=12)
        self.assertAlmostEqual(keypoints[1][1][1], 5.0)

    def test_second_arc_cannot_curl_back_into_polyline_segment(self):
        start = (10.0, 0.0)
        tangent = (10.0, 0.0)
        self.assertEqual(
            outward_minor_arc_endpoint(start, tangent, (4.0, 8.0)),
            (16.0, 8.0),
        )
        self.assertEqual(
            outward_minor_arc_endpoint(start, tangent, (16.0, 8.0)),
            (16.0, 8.0),
        )

    def test_arc_segment_arc_uses_outgoing_segment_direction(self):
        entities = [
            {"id": "c", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "a", "type": "point", "x": 0.0, "y": -10.0},
            {"id": "b", "type": "point", "x": 10.0, "y": 0.0},
            {"id": "d", "type": "point", "x": 20.0, "y": 0.0},
            {"id": "g1", "type": "arc", "point_ids": ["c", "a", "b"]},
            {"id": "g2", "type": "segment", "point_ids": ["b", "d"]},
        ]
        direction, tangent_id, center_reference = polyline_arc_start_context(
            entities, "d"
        )
        self.assertEqual(direction, (10.0, 0.0))
        self.assertEqual(tangent_id, "g2")
        self.assertIsNone(center_reference)

    def test_free_point_cannot_start_arc(self):
        entities = [{"id": "p1", "type": "point", "x": 2.0, "y": 3.0}]
        self.assertIsNone(polyline_arc_start_context(entities, "p1"))

    def test_model_segment_starts_tangent_arc(self):
        entities = [
            {"id": "p1", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "p2", "type": "point", "x": 10.0, "y": 0.0},
            {"id": "g1", "type": "segment", "point_ids": ["p1", "p2"]},
        ]
        direction, tangent_id, center_reference = (
            polyline_arc_start_context(entities, "p2")
        )
        self.assertEqual(direction, (10.0, 0.0))
        self.assertEqual(tangent_id, "g1")
        self.assertIsNone(center_reference)

    def test_construction_starts_normal_arc_and_constrains_center(self):
        entities = [
            {"id": "p1", "type": "point", "x": 0.0, "y": -10.0},
            {"id": "p2", "type": "point", "x": 0.0, "y": 10.0},
            {"id": "g1", "type": "construction", "point_ids": ["p1", "p2"]},
        ]
        direction, tangent_id, center_reference = (
            polyline_arc_start_context(entities, "p2")
        )
        self.assertEqual(direction, (-20.0, 0.0))
        self.assertIsNone(tangent_id)
        self.assertEqual(center_reference, "sketch_geometry:g1")

    def test_point_inside_construction_can_start_normal_arc(self):
        entities = [
            {"id": "a", "type": "point", "x": 0.0, "y": -20.0},
            {"id": "b", "type": "point", "x": 0.0, "y": 20.0},
            {"id": "p", "type": "point", "x": 0.0, "y": -5.0,
             "constraints": [{
                 "type": "point_on_line", "point_ids": ["a", "b"],
                 "bounded": False,
             }]},
            {"id": "g1", "type": "construction", "point_ids": ["a", "b"]},
        ]
        direction, tangent_id, center_reference = polyline_arc_start_context(
            entities, "p"
        )
        self.assertEqual(direction, (-40.0, 0.0))
        self.assertIsNone(tangent_id)
        self.assertEqual(center_reference, "sketch_geometry:g1")

    def test_axis_starts_normal_arc_and_constrains_center(self):
        entities = [{
            "id": "p1", "type": "point", "x": 0.0, "y": -10.0,
            "constraints": [{
                "type": "point_on_reference",
                "reference_id": "sketch_axis:y",
            }],
        }]
        direction, tangent_id, center_reference = (
            polyline_arc_start_context(entities, "p1")
        )
        self.assertEqual(direction, (1.0, 0.0))
        self.assertIsNone(tangent_id)
        self.assertEqual(center_reference, "sketch_axis:y")

    def test_stale_tangent_to_other_arc_is_rejected(self):
        entities = [
            {"id": "c", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "a", "type": "point", "x": 0.0, "y": -10.0},
            {"id": "b", "type": "point", "x": 0.0, "y": 10.0},
            {"id": "foreign", "type": "point", "x": 40.0, "y": 10.0},
            {
                "id": "arc", "type": "arc", "arc_mode": "center",
                "radius": 10.0, "point_ids": ["c", "a", "b"],
            },
        ]
        self.assertFalse(valid_automatic_tangent(entities, {
            "type": "tangent", "geometry_id": "arc",
            "contact_point_id": "foreign",
        }))
        self.assertTrue(valid_automatic_tangent(entities, {
            "type": "tangent", "geometry_id": "arc",
            "contact_point_id": "b",
        }))


if __name__ == "__main__":
    unittest.main()
