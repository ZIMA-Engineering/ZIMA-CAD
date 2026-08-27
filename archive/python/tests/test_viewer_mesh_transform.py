import unittest

from zima_cad.viewer_data import EdgePolyline, ViewerMesh
from zima_cad.viewer_mesh import transform_viewer_mesh


class ViewerMeshTransformTests(unittest.TestCase):
    def test_transform_moves_triangles_edges_and_bounds_together(self) -> None:
        mesh = ViewerMesh(
            triangle_positions=(0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 0.0, 3.0, 0.0),
            triangle_normals=(0.0, 0.0, 1.0) * 3,
            triangle_face_indices=(1,),
            triangle_owner_ids=("component",),
            edges=(EdgePolyline(
                edge_index=1,
                points=((0.0, 0.0, 0.0), (2.0, 0.0, 0.0)),
                owner_id="component",
            ),),
            points=(),
            planes=(),
            bounds_min=(0.0, 0.0, 0.0),
            bounds_max=(2.0, 3.0, 0.0),
        )

        transformed = transform_viewer_mesh(mesh, (
            (1.0, 0.0, 0.0, 10.0),
            (0.0, 1.0, 0.0, -4.0),
            (0.0, 0.0, 1.0, 2.0),
            (0.0, 0.0, 0.0, 1.0),
        ))

        self.assertEqual(
            transformed.triangle_positions,
            (10.0, -4.0, 2.0, 12.0, -4.0, 2.0, 10.0, -1.0, 2.0),
        )
        self.assertEqual(
            transformed.edges[0].points,
            ((10.0, -4.0, 2.0), (12.0, -4.0, 2.0)),
        )
        self.assertEqual(transformed.triangle_normals, mesh.triangle_normals)
        self.assertEqual(transformed.bounds_min, (10.0, -4.0, 2.0))
        self.assertEqual(transformed.bounds_max, (12.0, -1.0, 2.0))


if __name__ == "__main__":
    unittest.main()
