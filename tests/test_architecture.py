import ast
import json
import unittest
from pathlib import Path

from zima_cad.body_result import BodyResult
from zima_cad.viewer_data import EdgePolyline, PointMarker, ViewerMesh


PROJECT_ROOT = Path(__file__).resolve().parents[1]


class OcctBoundaryTests(unittest.TestCase):
    def test_renderer_does_not_import_occt(self) -> None:
        source_path = PROJECT_ROOT / "zima_cad" / "viewer.py"
        tree = ast.parse(source_path.read_text(encoding="utf-8"))
        imported_modules = {
            node.module or ""
            for node in ast.walk(tree)
            if isinstance(node, ast.ImportFrom)
        }
        imported_modules.update(
            alias.name
            for node in ast.walk(tree)
            if isinstance(node, ast.Import)
            for alias in node.names
        )

        self.assertFalse(
            any(
                module == "OCC" or module.startswith("OCC.")
                for module in imported_modules
            ),
            "The renderer must consume ZIMA viewer data, not OCCT objects.",
        )
        self.assertNotIn(
            "zima_cad.viewer_mesh",
            imported_modules,
            "The renderer must not import the OCCT-backed meshing adapter.",
        )

    def test_body_result_contract_does_not_import_occt(self) -> None:
        source_path = PROJECT_ROOT / "zima_cad" / "body_result.py"
        tree = ast.parse(source_path.read_text(encoding="utf-8"))
        imports = {
            node.module or ""
            for node in ast.walk(tree)
            if isinstance(node, ast.ImportFrom)
        }
        self.assertFalse(any(name.startswith("OCC") for name in imports))

    def test_document_storage_does_not_import_occt(self) -> None:
        source_path = PROJECT_ROOT / "zima_cad" / "storage.py"
        tree = ast.parse(source_path.read_text(encoding="utf-8"))
        imports = {
            node.module or ""
            for node in ast.walk(tree)
            if isinstance(node, ast.ImportFrom)
        }
        self.assertFalse(
            any(name == "OCC" or name.startswith("OCC.") for name in imports),
            "Opening and saving current documents must use ZIMA data only.",
        )

    def test_body_result_builds_zima_topology_from_viewer_data(self) -> None:
        mesh = ViewerMesh(
            triangle_positions=(
                0.0, 0.0, 0.0,
                1.0, 0.0, 0.0,
                0.0, 1.0, 0.0,
            ),
            triangle_normals=(0.0,) * 9,
            triangle_face_indices=(4,),
            triangle_owner_ids=("body",),
            edges=(EdgePolyline(
                7,
                ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0)),
                "body",
                curve_kind="circle",
                curve_origin=(0.5, 0.0, 0.0),
                curve_direction=(0.0, 0.0, 1.0),
                curve_radius=0.5,
            ),),
            points=(PointMarker(3, (0.0, 0.0, 0.0), "body"),),
            planes=(),
            bounds_min=(0.0, 0.0, 0.0),
            bounds_max=(1.0, 0.0, 0.0),
        )

        result = BodyResult.from_mesh(
            mesh,
            face_reference_ids={("body", 4): "stable-face-id"},
            edge_reference_ids={("body", 7): "stable-edge-id"},
            vertex_reference_ids={("body", 3): "stable-point-id"},
        )

        self.assertIn("body:face:4", result.faces)
        self.assertEqual(result.surface("body", 4).kind, "plane")
        self.assertEqual(
            result.surface("body", 4).reference_id,
            "stable-face-id",
        )
        self.assertIn("body:edge:7", result.edges)
        self.assertIsNotNone(result.curve("body", 7))
        self.assertEqual(result.curve("body", 7).reference_id, "stable-edge-id")
        self.assertEqual(result.curve("body", 7).kind, "circle")
        self.assertEqual(result.curve("body", 7).origin, (0.5, 0.0, 0.0))
        self.assertEqual(result.curve("body", 7).radius, 0.5)
        self.assertIn("body:point:3", result.vertices)
        self.assertEqual(
            result.vertex("body", 3).position,
            (0.0, 0.0, 0.0),
        )
        self.assertEqual(
            result.vertex("body", 3).reference_id,
            "stable-point-id",
        )

        inherited = BodyResult.from_mesh(mesh)
        reused = BodyResult.from_mesh(
            mesh,
            inherited=inherited,
            skip_triangle_count=mesh.triangle_count,
        )
        self.assertIs(reused.surface("body", 4), inherited.surface("body", 4))

        restored = BodyResult.from_dict(json.loads(json.dumps(result.to_dict())))
        self.assertEqual(restored.mesh, result.mesh)
        self.assertEqual(restored.faces, result.faces)
        self.assertEqual(restored.edges, result.edges)
        self.assertEqual(restored.vertices, result.vertices)

        rebound = restored.with_owner("loaded-body")
        self.assertEqual(
            rebound.surface("loaded-body", 4).reference_id,
            "stable-face-id",
        )
        self.assertIsNotNone(rebound.curve("loaded-body", 7))
        self.assertIsNotNone(rebound.vertex("loaded-body", 3))


if __name__ == "__main__":
    unittest.main()
