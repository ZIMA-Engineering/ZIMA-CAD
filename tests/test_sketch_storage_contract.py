import json
import unittest
from pathlib import Path

from zima_cad.model import EntityKind, create_empty_part
from zima_cad.sketch_model import GeometryType, SketchModel
from zima_cad.storage import load_part_document, save_part_document


class SketchStorageContractTests(unittest.TestCase):
    def test_native_sketch_payload_maps_without_kernel_work(self) -> None:
        native = {
            "format": "zima-cad-cpp-sketch", "version": 20,
            "id": "Sketch001", "name": "Profile", "suppressed": False,
            "plane": "xz", "plane_offset": 2.5,
            "points": [
                {"id": "p0", "x": 0, "y": 0, "fixed": True, "construction": False},
                {"id": "p1", "x": 10, "y": 0, "fixed": False, "construction": True},
                {"id": "pc", "x": 5, "y": 5, "fixed": False, "construction": False},
            ],
            "segments": [{"id": "axis", "first": "p0", "second": "p1", "construction": True}],
            "circles": [{"id": "hole", "center": "pc", "radius": 2, "construction": False}],
            "arcs": [], "ellipses": [], "elliptical_arcs": [], "bsplines": [],
            "import_blocks": [], "texts": [],
            "external_references": [{
                "id": "ref", "kind": "edge", "source_document_id": "PartA",
                "source_owner_id": "Body", "source_semantic_key": "edge:outer",
                "source_instance_path": "root/0", "context_assembly_document_id": "",
                "context_instance_path": "", "cached_points": [[0, 0], [10, 0]],
                "cached_paths": [], "broken": False,
            }],
            "constraints": [{
                "id": "horizontal", "kind": "horizontal", "first": "p0",
                "second": "p1", "suppressed": False, "geometry": "",
                "second_geometry": "", "tangent_internal": False,
            }],
            "dimensions": [],
        }
        model = SketchModel.from_dict(native)
        self.assertEqual(model.sketch_id, "Sketch001")
        self.assertTrue(model.geometry["axis"].attributes["construction"])
        self.assertEqual(model.geometry["hole"].geometry_type, GeometryType.CIRCLE)
        self.assertEqual(model.external_references[0]["source_owner_id"], "Body")
        self.assertEqual(
            SketchModel.from_dict(model.to_dict()).points["p0"].attributes["fixed"],
            True,
        )

    def test_ini_round_trip_preserves_sketch_data_and_attachment(self) -> None:
        document = create_empty_part()
        container = document.create_container()
        sketch = document.create_sketch(container.entity_id, "xz")
        self.assertIsNotNone(sketch)
        sketch.parameters["sketch_data"] = json.dumps({
            "version": 3,
            "points": {"a": {"x": 0, "y": 0}, "b": {"x": 10, "y": 0, "construction": True}},
            "geometry": {"line": {"type": "segment", "points": ["a", "b"], "construction": True}},
            "constraints": {"horizontal": {"type": "horizontal", "points": ["a", "b"]}},
            "dimensions": {},
        })
        path = Path("tests/.sketch_storage_contract.ini")
        try:
            save_part_document(document, path)
            loaded = load_part_document(path)
            restored = loaded.find_entity(sketch.entity_id)
            self.assertIsNotNone(restored)
            model = SketchModel.from_dict(json.loads(restored.parameters["sketch_data"]))
            self.assertEqual(model.geometry["line"].point_ids, ("a", "b"))
            self.assertTrue(model.points["b"].construction)
        finally:
            path.unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()
