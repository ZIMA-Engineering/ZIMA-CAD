import math
import unittest

from zima_cad.analytic_intersections import (
    analytic_surface_side,
    ray_surface_intersections,
    ray_triangle_mesh_intersections,
)


class AnalyticIntersectionTests(unittest.TestCase):
    def assert_distances(self, result, expected):
        self.assertEqual(result.error, "")
        self.assertEqual(len(result.distances), len(expected))
        for actual, wanted in zip(result.distances, expected):
            self.assertAlmostEqual(actual, wanted, places=7)

    def test_plane_intersection_and_parallel_direction(self):
        result = ray_surface_intersections(
            "plane",
            (1.0, 2.0, 0.0),
            (0.0, 0.0, 2.0),
            plane_origin=(0.0, 0.0, 5.0),
            plane_normal=(0.0, 0.0, 1.0),
        )
        self.assert_distances(result, (5.0,))
        parallel = ray_surface_intersections(
            "plane",
            (0.0, 0.0, 0.0),
            (1.0, 0.0, 0.0),
            plane_origin=(0.0, 0.0, 5.0),
            plane_normal=(0.0, 0.0, 1.0),
        )
        self.assertEqual(parallel.error, "parallel")

    def test_sphere_has_near_and_far_intersections(self):
        result = ray_surface_intersections(
            "sphere",
            (-10.0, 0.0, 0.0),
            (1.0, 0.0, 0.0),
            center=(0.0, 0.0, 0.0),
            radius=3.0,
        )
        self.assert_distances(result, (7.0, 13.0))
        miss = ray_surface_intersections(
            "sphere",
            (-10.0, 4.0, 0.0),
            (1.0, 0.0, 0.0),
            center=(0.0, 0.0, 0.0),
            radius=3.0,
        )
        self.assertEqual(miss.error, "miss")

    def test_cylinder_supports_oblique_rays_but_not_axis_parallel_rays(self):
        result = ray_surface_intersections(
            "cylinder",
            (-5.0, 0.0, -2.0),
            (1.0, 0.0, 1.0),
            axis_origin=(0.0, 0.0, 0.0),
            axis_direction=(0.0, 0.0, 1.0),
            radius=2.0,
        )
        root_two = math.sqrt(2.0)
        self.assert_distances(result, (3.0 * root_two, 7.0 * root_two))
        parallel = ray_surface_intersections(
            "cylinder",
            (0.0, 0.0, -2.0),
            (0.0, 0.0, 1.0),
            axis_origin=(0.0, 0.0, 0.0),
            axis_direction=(0.0, 0.0, 1.0),
            radius=2.0,
        )
        self.assertEqual(parallel.error, "parallel")

    def test_cone_rejects_the_opposite_nappe(self):
        result = ray_surface_intersections(
            "cone",
            (3.0, 0.0, 5.0),
            (-1.0, 0.0, 0.0),
            apex=(0.0, 0.0, 0.0),
            axis_direction=(0.0, 0.0, 1.0),
            semi_angle=math.atan(0.5),
        )
        self.assert_distances(result, (0.5, 5.5))
        opposite = ray_surface_intersections(
            "cone",
            (3.0, 0.0, -5.0),
            (-1.0, 0.0, 0.0),
            apex=(0.0, 0.0, 0.0),
            axis_direction=(0.0, 0.0, 1.0),
            semi_angle=math.atan(0.5),
        )
        self.assertEqual(opposite.error, "behind")

    def test_persisted_triangle_mesh_supports_general_surface_preview(self):
        triangles = (
            ((-2.0, 5.0, -2.0), (2.0, 6.0, -2.0), (2.0, 7.0, 2.0)),
            ((-2.0, 5.0, -2.0), (2.0, 7.0, 2.0), (-2.0, 6.0, 2.0)),
        )
        result = ray_triangle_mesh_intersections(
            (0.0, 0.0, 0.0),
            (0.0, 1.0, 0.0),
            triangles,
        )
        self.assert_distances(result, (6.0,))
        miss = ray_triangle_mesh_intersections(
            (3.0, 0.0, 0.0),
            (0.0, 1.0, 0.0),
            triangles,
        )
        self.assertEqual(miss.error, "miss")

    def test_surface_side_classification_matches_clipping_semantics(self):
        self.assertEqual(
            analytic_surface_side(
                "sphere", (0.0, 0.0, 0.0),
                center=(0.0, 0.0, 0.0), radius=2.0,
            ),
            "inside",
        )
        self.assertEqual(
            analytic_surface_side(
                "cylinder", (2.0, 0.0, 8.0),
                axis_origin=(0.0, 0.0, 0.0),
                axis_direction=(0.0, 0.0, 1.0), radius=2.0,
            ),
            "on",
        )
        self.assertEqual(
            analytic_surface_side(
                "cone", (0.0, 0.0, -2.0),
                apex=(0.0, 0.0, 0.0),
                axis_direction=(0.0, 0.0, 1.0),
                semi_angle=math.pi / 6.0,
            ),
            "outside",
        )


if __name__ == "__main__":
    unittest.main()
