from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from zima_cad.model import (
    ContainerType,
    EntityKind,
    active_face_registry,
    create_empty_part,
    make_protrusion_shape,
    protrusion_face_registry,
    semantic_face_registry,
    ZimaEntity,
)
from zima_cad.sketch_model import SketchModel
from zima_cad.storage import load_part_document, save_part_document
from zima_cad.topology import (
    AssemblyFaceRef,
    FaceRef,
    TopologyRegistry,
    TopologyResolutionState,
    decode_face_reference,
    encode_face_reference,
    parse_face_reference,
)


class StableTopologyTests(unittest.TestCase):
    def test_face_reference_round_trip(self) -> None:
        reference = FaceRef(
            feature_id="extrusion-1",
            role="generated",
            source_id="sketch-edge-7",
            fragment=2,
        )
        self.assertEqual(FaceRef.deserialize(reference.serialize()), reference)
        self.assertEqual(parse_face_reference(reference.to_dict()), reference)
        self.assertEqual(
            decode_face_reference(encode_face_reference(reference)),
            reference,
        )
        self.assertIsNone(parse_face_reference("17"))

    def test_assembly_reference_keeps_instance_separate(self) -> None:
        face = FaceRef("extrusion-1", "end")
        first = AssemblyFaceRef.from_dict({
            "instance_id": "component-a",
            "face": face.to_dict(),
        })
        second = AssemblyFaceRef("component-b", face)
        self.assertNotEqual(first, second)
        self.assertEqual(first.face, second.face)

    def test_registry_never_silently_resolves_ambiguous_face(self) -> None:
        reference = FaceRef("extrusion-1", "generated", "edge-1")
        registry = TopologyRegistry()
        registry.register_face(reference, "face-a", runtime_index=3)
        registry.register_face(reference, "face-b", runtime_index=4)
        result = registry.resolve(reference)
        self.assertEqual(result.state, TopologyResolutionState.AMBIGUOUS)
        self.assertIsNone(result.shape)
        self.assertEqual(result.candidates, ("face-a", "face-b"))
        self.assertEqual(registry.reference_for_runtime_index(3), reference)

    def test_registry_distinguishes_missing_and_incompatible(self) -> None:
        registry = TopologyRegistry()
        registry.register_face(FaceRef("box-1", "x_min"), "face")
        self.assertEqual(
            registry.resolve(FaceRef("box-1", "x_max")).state,
            TopologyResolutionState.INCOMPATIBLE,
        )
        self.assertEqual(
            registry.resolve(FaceRef("box-2", "x_min")).state,
            TopologyResolutionState.MISSING,
        )

    def test_box_face_roles_survive_dimension_changes(self) -> None:
        document = create_empty_part()
        container = document.create_container("Box", ContainerType.BOX)
        box = document.create_primitive(container.entity_id, EntityKind.BOX)
        self.assertIsNotNone(box)
        first_shape = document.build_standalone_shape(container)
        first = semantic_face_registry(document, box, first_shape)
        expected = {
            FaceRef(box.entity_id, role)
            for role in (
                "x_min", "x_max", "y_min", "y_max", "z_min", "z_max"
            )
        }
        self.assertEqual(set(first.references), expected)

        box.parameters.update({"length": "240", "width": "35", "height": "81"})
        second_shape = document.build_standalone_shape(container)
        second = semantic_face_registry(document, box, second_shape)
        self.assertEqual(set(second.references), expected)
        for reference in expected:
            self.assertEqual(
                second.resolve(reference).state,
                TopologyResolutionState.RESOLVED,
            )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "box.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            loaded_registry = active_face_registry(loaded)
            self.assertEqual(set(loaded_registry.references), expected)
            for reference in expected:
                self.assertEqual(
                    loaded_registry.resolve(reference).state,
                    TopologyResolutionState.RESOLVED,
                )

    def test_protrusion_caps_survive_length_change(self) -> None:
        document = create_empty_part()
        container = ZimaEntity(
            "Protrusion001",
            EntityKind.CONTAINER,
            parameters={"container_type": ContainerType.PROTRUSION.value},
        )
        entities = [
            {"id": "a", "type": "point", "x": 0.0, "y": 0.0},
            {"id": "b", "type": "point", "x": 20.0, "y": 0.0},
            {"id": "c", "type": "point", "x": 20.0, "y": 10.0},
            {"id": "d", "type": "point", "x": 0.0, "y": 10.0},
            {"id": "ab", "type": "segment", "point_ids": ["a", "b"]},
            {"id": "bc", "type": "segment", "point_ids": ["b", "c"]},
            {"id": "cd", "type": "segment", "point_ids": ["c", "d"]},
            {"id": "da", "type": "segment", "point_ids": ["d", "a"]},
        ]
        sketch = ZimaEntity(
            "Sketch001",
            EntityKind.SKETCH,
            parameters={
                "plane": "xz",
                "profile": "entities",
                "sketch_data": json.dumps(
                    SketchModel.from_editor_data(entities).to_dict()
                ),
            },
        )
        feature = ZimaEntity(
            "Protrusion",
            EntityKind.PROTRUSION,
            parameters={
                "sketch_id": sketch.entity_id,
                "length_forward": "10",
                "extent_mode": "one_side",
                "direction": "forward",
            },
        )
        container.add_child(sketch)
        container.add_child(feature)
        document.root.add_child(container)
        expected = {
            FaceRef(feature.entity_id, "start"),
            FaceRef(feature.entity_id, "end"),
            *(
                FaceRef(feature.entity_id, "generated", source_id)
                for source_id in ("ab", "bc", "cd", "da")
            ),
        }

        first_shape = make_protrusion_shape(document, container)
        first = protrusion_face_registry(document, container, first_shape)
        self.assertEqual(set(first.references), expected)
        feature.parameters["length_forward"] = "125"
        second_shape = make_protrusion_shape(document, container)
        second = protrusion_face_registry(document, container, second_shape)
        self.assertEqual(set(second.references), expected)
        for reference in expected:
            self.assertEqual(
                second.resolve(reference).state,
                TopologyResolutionState.RESOLVED,
            )
        edited_entities, edited_dimensions = SketchModel.from_dict(
            json.loads(str(sketch.parameters["sketch_data"]))
        ).to_editor_data()
        for entity in edited_entities:
            if entity.get("id") in {"b", "c"}:
                entity["x"] = 35.0
            if entity.get("id") in {"c", "d"}:
                entity["y"] = 18.0
        sketch.parameters["sketch_data"] = json.dumps(
            SketchModel.from_editor_data(
                edited_entities,
                edited_dimensions,
            ).to_dict()
        )
        reshaped = active_face_registry(document)
        self.assertEqual(set(reshaped.references), expected)
        for reference in expected:
            self.assertEqual(
                reshaped.resolve(reference).state,
                TopologyResolutionState.RESOLVED,
            )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "extrusion.prtz"
            save_part_document(document, path)
            loaded = load_part_document(path)
            loaded_registry = active_face_registry(loaded)
            self.assertEqual(set(loaded_registry.references), expected)
            for reference in expected:
                self.assertEqual(
                    loaded_registry.resolve(reference).state,
                    TopologyResolutionState.RESOLVED,
                )


if __name__ == "__main__":
    unittest.main()
